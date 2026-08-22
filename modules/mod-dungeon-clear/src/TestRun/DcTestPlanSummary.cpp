/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestPlanSummary.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>

#include "TestRun/DcTestRunRecord.h"

namespace DcTestPlanSummary
{
    namespace
    {
        // Ordered accumulation (std::map keyed by string) then a stable sort by
        // count desc — ties stay alphabetical, so output order is deterministic.
        std::vector<KeyCount> CountSorted(std::map<std::string, std::uint32_t> const& counts)
        {
            std::vector<KeyCount> out;
            out.reserve(counts.size());
            for (auto const& [key, count] : counts)
                out.push_back({key, count});
            std::stable_sort(out.begin(), out.end(),
                             [](KeyCount const& a, KeyCount const& b) { return a.count > b.count; });
            return out;
        }

        // Nearest-rank percentile on an ALREADY-SORTED sample: no interpolation,
        // exact on a sample of one, and every returned value is a value that
        // really occurred. Empty sample -> 0, which is also the natural "no
        // pulls were observed" reading.
        template <typename T>
        T Percentile(std::vector<T> const& sorted, std::uint32_t pct)
        {
            if (sorted.empty())
                return T{};
            std::size_t idx = (static_cast<std::size_t>(pct) * sorted.size()) / 100;
            if (idx >= sorted.size())
                idx = sorted.size() - 1;
            return sorted[idx];
        }
    }

    Stats Build(std::vector<DcTestPlan::RunOutcome> const& outcomes)
    {
        Stats s;
        s.launched = static_cast<std::uint32_t>(outcomes.size());

        std::map<std::string, std::uint32_t> verdicts;
        std::map<std::string, std::uint32_t> reasons;
        std::vector<std::uint32_t> successDurations;

        // Funnel: per boss name, kill count (deduped within a run), wipe count,
        // and the sum of timeline positions, so entries with no roster to order
        // them still sort into progression order by mean position even when runs
        // kill in slightly different orders.
        struct FunnelAcc
        {
            std::uint32_t killed = 0;
            std::uint32_t wiped = 0;
            std::uint64_t posSum = 0;
        };
        std::map<std::string, FunnelAcc> funnel;
        std::map<std::string, std::uint32_t> trashWipes;

        // Pull population, pooled across every run in the plan. Pooled rather
        // than averaged per run on purpose: one 90-pull run and one that wiped
        // on its second pull contribute the pulls they actually made, so a plan
        // that keeps dying early cannot flatter its own numbers by weighting a
        // two-sample run as heavily as a full clear.
        std::vector<std::uint32_t> observed;
        std::vector<std::int32_t> errors;

        // Progression order, taken from the longest roster any run reported.
        // Longest rather than first because a run that failed during setup never
        // got one, and a multi-wing map filters the roster to the wing the party
        // spawned in — the fullest list is the one that names the most bosses.
        std::vector<std::string> roster;

        for (DcTestPlan::RunOutcome const& o : outcomes)
        {
            if (o.bossRoster.size() > roster.size())
                roster = o.bossRoster;

            s.runIds.push_back(o.runId);
            ++verdicts[o.result];
            if (o.result == "success")
            {
                ++s.succeeded;
                successDurations.push_back(o.durationS);
            }
            else
            {
                ++s.failed;
                if (!o.failReason.empty())
                    ++reasons[o.failReason];
            }

            std::vector<std::string> seen;
            for (std::size_t pos = 0; pos < o.bossKills.size(); ++pos)
            {
                std::string const& name = o.bossKills[pos];
                if (std::find(seen.begin(), seen.end(), name) != seen.end())
                    continue;
                seen.push_back(name);
                FunnelAcc& acc = funnel[name];
                ++acc.killed;
                acc.posSum += pos;
            }

            if (!o.wipeOpponent.empty())
            {
                if (o.wipeOnBoss)
                    ++funnel[o.wipeOpponent].wiped;
                else
                    ++trashWipes[o.wipeOpponent];
            }
            else if (o.result == "wipe")
            {
                // A wipe the harness could not pin on anything — the party was
                // out of combat when the last member fell.
                ++s.unattributedWipes;
            }

            for (DcTestPlan::PullSample const& p : o.pulls)
            {
                ++s.pulls.pulls;
                if (p.advanced)
                    ++s.pulls.advanced;
                if (p.observed > p.predicted)
                    ++s.pulls.underestimated;
                observed.push_back(p.observed);
                errors.push_back(static_cast<std::int32_t>(p.observed) -
                                 static_cast<std::int32_t>(p.predicted));
                if (p.wipedHere)
                {
                    ++s.pulls.wipePulls;
                    s.pulls.wipeObservedMax =
                        std::max(s.pulls.wipeObservedMax, p.observed);
                }
            }
        }

        s.verdicts = CountSorted(verdicts);
        s.failReasons = CountSorted(reasons);

        if (!successDurations.empty())
        {
            std::sort(successDurations.begin(), successDurations.end());
            s.minS = successDurations.front();
            s.maxS = successDurations.back();
            std::uint64_t sum = 0;
            for (std::uint32_t d : successDurations)
                sum += d;
            s.avgS = static_cast<std::uint32_t>(sum / successDurations.size());
            std::size_t const n = successDurations.size();
            s.medianS = n % 2 ? successDurations[n / 2]
                              : (successDurations[n / 2 - 1] + successDurations[n / 2]) / 2;
        }

        if (!observed.empty())
        {
            std::sort(observed.begin(), observed.end());
            std::sort(errors.begin(), errors.end());
            s.pulls.observedP50 = Percentile(observed, 50);
            s.pulls.observedP90 = Percentile(observed, 90);
            s.pulls.observedMax = observed.back();
            s.pulls.errorP50 = Percentile(errors, 50);
            s.pulls.errorP90 = Percentile(errors, 90);
        }

        s.trashWipes = CountSorted(trashWipes);

        // Roster order first, so a boss no run ever reached still gets a row
        // (0 killed, 0 wiped) instead of vanishing from the funnel entirely —
        // "nobody got that far" is the single most useful thing a plan can say.
        std::vector<std::string> placed;
        for (std::string const& name : roster)
        {
            if (std::find(placed.begin(), placed.end(), name) != placed.end())
                continue;
            placed.push_back(name);
            auto const it = funnel.find(name);
            if (it == funnel.end())
                s.funnel.push_back({name, 0, 0});
            else
                s.funnel.push_back({name, it->second.killed, it->second.wiped});
        }

        // Then anything the roster never listed — a summoned bonus boss, or a
        // creature the wipe latch resolved off its own boss flags. Ordered by
        // mean kill position; wipe-only entries have no position at all, so they
        // sort last (and alphabetically among themselves, via the map order).
        std::vector<std::pair<double, std::string>> ordered;
        for (auto const& [name, acc] : funnel)
        {
            if (std::find(placed.begin(), placed.end(), name) != placed.end())
                continue;
            double const pos = acc.killed ? static_cast<double>(acc.posSum) / acc.killed
                                          : std::numeric_limits<double>::max();
            ordered.emplace_back(pos, name);
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](auto const& a, auto const& b) { return a.first < b.first; });
        for (auto const& [pos, name] : ordered)
            s.funnel.push_back({name, funnel[name].killed, funnel[name].wiped});

        return s;
    }

    std::string ToJsonl(Header const& h, Stats const& s)
    {
        using DcTestRunRecord::EscapeJson;

        auto str = [](std::string const& v) { return "\"" + EscapeJson(v) + "\""; };

        std::ostringstream o;
        o << "{\"schema\":" << h.schema
          << ",\"planId\":" << str(h.planId)
          << ",\"dungeon\":" << str(h.dungeon)
          << ",\"dungeonName\":" << str(h.dungeonName)
          << ",\"requested\":{\"total\":" << h.total
          << ",\"concurrent\":" << h.concurrent
          << ",\"level\":" << h.level
          << ",\"heroic\":" << (h.heroic ? "true" : "false")
          << ",\"seedBase\":" << h.seedBase
          << ",\"gearIlvl\":" << h.gearIlvl
          << ",\"gearQuality\":" << h.gearQuality
          << "},\"startedAtMs\":" << h.startedAtMs
          << ",\"endedAtMs\":" << h.endedAtMs
          << ",\"durationS\":" << h.durationS
          << ",\"result\":" << str(h.result)
          << ",\"abortReason\":" << str(h.abortReason)
          << ",\"runs\":{\"launched\":" << s.launched
          << ",\"succeeded\":" << s.succeeded
          << ",\"failed\":" << s.failed
          << "},\"verdicts\":{";
        for (std::size_t i = 0; i < s.verdicts.size(); ++i)
        {
            if (i)
                o << ',';
            o << str(s.verdicts[i].key) << ':' << s.verdicts[i].count;
        }
        o << "},\"failReasons\":[";
        for (std::size_t i = 0; i < s.failReasons.size(); ++i)
        {
            if (i)
                o << ',';
            o << "{\"reason\":" << str(s.failReasons[i].key)
              << ",\"count\":" << s.failReasons[i].count << '}';
        }
        o << "],\"duration\":{\"minS\":" << s.minS
          << ",\"avgS\":" << s.avgS
          << ",\"medianS\":" << s.medianS
          << ",\"maxS\":" << s.maxS
          << "},\"bossFunnel\":[";
        for (std::size_t i = 0; i < s.funnel.size(); ++i)
        {
            if (i)
                o << ',';
            o << "{\"name\":" << str(s.funnel[i].name)
              << ",\"killed\":" << s.funnel[i].killed
              << ",\"wiped\":" << s.funnel[i].wiped << '}';
        }
        o << "],\"trashWipes\":[";
        for (std::size_t i = 0; i < s.trashWipes.size(); ++i)
        {
            if (i)
                o << ',';
            o << "{\"name\":" << str(s.trashWipes[i].key)
              << ",\"count\":" << s.trashWipes[i].count << '}';
        }
        o << "],\"unattributedWipes\":" << s.unattributedWipes
          << ",\"pulls\":{\"count\":" << s.pulls.pulls
          << ",\"advanced\":" << s.pulls.advanced
          << ",\"underestimated\":" << s.pulls.underestimated
          << ",\"observedP50\":" << s.pulls.observedP50
          << ",\"observedP90\":" << s.pulls.observedP90
          << ",\"observedMax\":" << s.pulls.observedMax
          << ",\"errorP50\":" << s.pulls.errorP50
          << ",\"errorP90\":" << s.pulls.errorP90
          << ",\"wipePulls\":" << s.pulls.wipePulls
          << ",\"wipeObservedMax\":" << s.pulls.wipeObservedMax
          << "},\"runIds\":[";
        for (std::size_t i = 0; i < s.runIds.size(); ++i)
        {
            if (i)
                o << ',';
            o << str(s.runIds[i]);
        }
        o << "]}";
        return o.str();
    }

    std::string CapturePath()
    {
        if (char const* env = std::getenv("DC_TESTPLANS_FILE"))
            if (env[0])
                return env;
        return "dc_testplans.jsonl";
    }

    void Append(Header const& h, Stats const& s)
    {
        static std::mutex mtx;
        static std::ofstream file;
        static bool opened = false;

        std::lock_guard<std::mutex> lock(mtx);
        if (!opened)
        {
            file.open(CapturePath(), std::ios::out | std::ios::app);
            opened = true;
        }
        if (!file.is_open())
            return;

        file << ToJsonl(h, s) << '\n';
        file.flush();
    }
}
