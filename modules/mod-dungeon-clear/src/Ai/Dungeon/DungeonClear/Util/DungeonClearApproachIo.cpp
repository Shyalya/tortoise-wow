/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearApproachIo.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace DungeonClearApproachIo
{
    namespace
    {
        // Verdict <-> name table. Order mirrors the enum for readability only;
        // the lookups below are by value/string, so reordering the enum is safe.
        struct VerdictNamePair { Verdict v; char const* name; };
        constexpr std::array<VerdictNamePair, 14> kVerdictNames = {{
            { Verdict::StuckRecover,       "StuckRecover"       },
            { Verdict::Pursue,             "Pursue"             },
            { Verdict::PlanRouteWait,      "PlanRouteWait"      },
            { Verdict::FarFromPolyRecover, "FarFromPolyRecover" },
            { Verdict::Swim,               "Swim"               },
            { Verdict::Stall,              "Stall"              },
            { Verdict::OffPathRebuild,     "OffPathRebuild"     },
            { Verdict::RebuildAndYield,    "RebuildAndYield"    },
            { Verdict::FinalApproach,      "FinalApproach"      },
            { Verdict::JumpLeg,            "JumpLeg"            },
            { Verdict::RideLiveGlide,      "RideLiveGlide"      },
            { Verdict::OffLineRejoin,      "OffLineRejoin"      },
            { Verdict::IssueSplineWindow,  "IssueSplineWindow"  },
            { Verdict::MoveToFallback,     "MoveToFallback"     },
        }};

        // Strip surrounding ASCII whitespace and a single pair of double quotes.
        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                                  s.front() == '\r' || s.front() == '\n'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                                  s.back() == '\r' || s.back() == '\n'))
                s.remove_suffix(1);
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            {
                s.remove_prefix(1);
                s.remove_suffix(1);
            }
            return s;
        }

        // Parse the flat object body (between the braces) into key->raw-value.
        // The format has no nested objects and no commas inside any value (the
        // only string value, the verdict, is a bare identifier), so a plain
        // split on ',' then ':' is sufficient and dependency-free.
        std::unordered_map<std::string, std::string> ParseFlat(std::string_view body)
        {
            std::unordered_map<std::string, std::string> out;
            while (!body.empty())
            {
                std::size_t const comma = body.find(',');
                std::string_view pair =
                    comma == std::string_view::npos ? body : body.substr(0, comma);
                std::size_t const colon = pair.find(':');
                if (colon != std::string_view::npos)
                {
                    std::string_view key = Trim(pair.substr(0, colon));
                    std::string_view val = Trim(pair.substr(colon + 1));
                    out.emplace(std::string(key), std::string(val));
                }
                if (comma == std::string_view::npos)
                    break;
                body.remove_prefix(comma + 1);
            }
            return out;
        }

        float GetF(std::unordered_map<std::string, std::string> const& m,
                   char const* key, float fallback)
        {
            auto const it = m.find(key);
            return it == m.end() ? fallback : std::strtof(it->second.c_str(), nullptr);
        }

        std::uint32_t GetU(std::unordered_map<std::string, std::string> const& m,
                           char const* key, std::uint32_t fallback)
        {
            auto const it = m.find(key);
            return it == m.end()
                       ? fallback
                       : static_cast<std::uint32_t>(
                             std::strtoul(it->second.c_str(), nullptr, 10));
        }

        std::uint64_t GetU64(std::unordered_map<std::string, std::string> const& m,
                             char const* key, std::uint64_t fallback)
        {
            auto const it = m.find(key);
            return it == m.end()
                       ? fallback
                       : static_cast<std::uint64_t>(
                             std::strtoull(it->second.c_str(), nullptr, 10));
        }

        bool GetB(std::unordered_map<std::string, std::string> const& m,
                  char const* key, bool fallback)
        {
            auto const it = m.find(key);
            if (it == m.end())
                return fallback;
            return it->second == "true" || it->second == "1";
        }
    }

    char const* VerdictName(Verdict v)
    {
        for (auto const& p : kVerdictNames)
            if (p.v == v)
                return p.name;
        return "MoveToFallback";  // unreachable; the terminal rung is total
    }

    bool VerdictFromName(std::string_view name, Verdict& out)
    {
        for (auto const& p : kVerdictNames)
            if (name == p.name)
            {
                out = p.v;
                return true;
            }
        return false;
    }

    std::string ToJsonl(DecisionRecord const& rec)
    {
        Observation const& o = rec.obs;
        std::ostringstream s;
        // Full float round-trip precision so a replay lands on the same side of
        // every threshold the live capture did.
        s.precision(9);
        s << '{'
          << "\"guid\":" << rec.guid
          << ",\"tick\":" << rec.tick
          << ",\"verdict\":\"" << VerdictName(rec.verdict) << '"'
          << ",\"engageDist\":" << o.engageDist
          << ",\"engageRange\":" << o.engageRange
          << ",\"posStuckTicks\":" << o.posStuckTicks
          << ",\"canPursue\":" << (o.canPursue ? "true" : "false")
          << ",\"pursuitFailTicks\":" << o.pursuitFailTicks
          << ",\"allowRecoveryMoves\":" << (o.allowRecoveryMoves ? "true" : "false")
          << ",\"pathReachable\":" << (o.pathReachable ? "true" : "false")
          << ",\"asyncPending\":" << (o.asyncPending ? "true" : "false")
          << ",\"startFarFromPoly\":" << (o.startFarFromPoly ? "true" : "false")
          << ",\"waterBetween\":" << (o.waterBetween ? "true" : "false")
          << ",\"offPath\":" << (o.offPath ? "true" : "false")
          << ",\"hopDone\":" << (o.hopDone ? "true" : "false")
          << ",\"hopIsJump\":" << (o.hopIsJump ? "true" : "false")
          << ",\"doneNotEngagedTicks\":" << o.doneNotEngagedTicks
          << ",\"splineRunning\":" << (o.splineRunning ? "true" : "false")
          << ",\"offLine\":" << (o.offLine ? "true" : "false")
          << ",\"haveSplineWindow\":" << (o.haveSplineWindow ? "true" : "false")
          << ",\"stuckTickLimit\":" << o.stuckTickLimit
          << ",\"pursuitFailLimit\":" << o.pursuitFailLimit
          << ",\"doneNotEngagedLimit\":" << o.doneNotEngagedLimit
          << '}';
        return s.str();
    }

    bool FromJsonl(std::string const& line, DecisionRecord& out)
    {
        std::string_view body = Trim(line);
        if (body.size() < 2 || body.front() != '{' || body.back() != '}')
            return false;
        body.remove_prefix(1);
        body.remove_suffix(1);

        auto const m = ParseFlat(body);

        auto const vIt = m.find("verdict");
        if (vIt == m.end() || !VerdictFromName(vIt->second, out.verdict))
            return false;

        out.guid = GetU64(m, "guid", 0);
        out.tick = GetU(m, "tick", 0);

        Observation o;  // struct defaults survive for any absent field
        o.engageDist          = GetF(m, "engageDist", o.engageDist);
        o.engageRange         = GetF(m, "engageRange", o.engageRange);
        o.posStuckTicks       = GetU(m, "posStuckTicks", o.posStuckTicks);
        o.canPursue           = GetB(m, "canPursue", o.canPursue);
        o.pursuitFailTicks    = GetU(m, "pursuitFailTicks", o.pursuitFailTicks);
        o.allowRecoveryMoves  = GetB(m, "allowRecoveryMoves", o.allowRecoveryMoves);
        o.pathReachable       = GetB(m, "pathReachable", o.pathReachable);
        o.asyncPending        = GetB(m, "asyncPending", o.asyncPending);
        o.startFarFromPoly    = GetB(m, "startFarFromPoly", o.startFarFromPoly);
        o.waterBetween        = GetB(m, "waterBetween", o.waterBetween);
        o.offPath             = GetB(m, "offPath", o.offPath);
        o.hopDone             = GetB(m, "hopDone", o.hopDone);
        o.hopIsJump           = GetB(m, "hopIsJump", o.hopIsJump);
        o.doneNotEngagedTicks = GetU(m, "doneNotEngagedTicks", o.doneNotEngagedTicks);
        o.splineRunning       = GetB(m, "splineRunning", o.splineRunning);
        o.offLine             = GetB(m, "offLine", o.offLine);
        o.haveSplineWindow    = GetB(m, "haveSplineWindow", o.haveSplineWindow);
        o.stuckTickLimit      = GetU(m, "stuckTickLimit", o.stuckTickLimit);
        o.pursuitFailLimit    = GetU(m, "pursuitFailLimit", o.pursuitFailLimit);
        o.doneNotEngagedLimit = GetU(m, "doneNotEngagedLimit", o.doneNotEngagedLimit);
        out.obs = o;
        return true;
    }

    std::string CapturePath()
    {
        if (char const* env = std::getenv("DUNGEONCLEAR_DECISIONS_FILE"))
            if (env[0])
                return env;
        return "dungeonclear_decisions.jsonl";
    }

    void Record(std::uint64_t guid, std::uint32_t tick, Observation const& obs,
                Verdict verdict)
    {
        // One process-wide append stream, opened on first capture and shared
        // across map threads. Guarded by a mutex: captures are rare, off by
        // default, and only enabled deliberately for a soak, so lock contention
        // is a non-issue versus the safety of serialized writes.
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

        file << ToJsonl(DecisionRecord{guid, tick, obs, verdict}) << '\n';
        file.flush();
    }
}
