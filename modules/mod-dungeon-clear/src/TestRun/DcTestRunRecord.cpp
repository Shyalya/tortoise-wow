/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcTestRunRecord.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

namespace DcTestRunRecord
{
    namespace
    {
        void AppendEscaped(std::ostringstream& s, std::string_view v)
        {
            s << '"' << EscapeJson(v) << '"';
        }

        void AppendStatus(std::ostringstream& s, StatusEntry const& e)
        {
            s << "{\"t\":" << e.t << ",\"state\":";
            AppendEscaped(s, e.state);
            s << ",\"detail\":";
            AppendEscaped(s, e.detail);
            s << '}';
        }
    }

    std::string EscapeJson(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                        out += static_cast<char>(c);
                    break;
            }
        }
        return out;
    }

    std::string ToJsonl(Record const& rec)
    {
        std::ostringstream s;
        s.precision(9);
        s << "{\"schema\":" << rec.schema
          << ",\"runId\":";
        AppendEscaped(s, rec.runId);
        s << ",\"planId\":";
        AppendEscaped(s, rec.planId);
        s << ",\"dungeon\":";
        AppendEscaped(s, rec.dungeon);
        s << ",\"dungeonName\":";
        AppendEscaped(s, rec.dungeonName);
        s << ",\"wing\":";
        AppendEscaped(s, rec.wing);
        s << ",\"mapId\":" << rec.mapId
          << ",\"instanceId\":" << rec.instanceId
          << ",\"level\":" << rec.level
          << ",\"heroic\":" << (rec.heroic ? "true" : "false")
          << ",\"compSeed\":" << rec.compSeed
          << ",\"gearIlvl\":" << rec.gearIlvl
          << ",\"gearQuality\":" << rec.gearQuality
          << ",\"roster\":" << (rec.roster ? "true" : "false")
          << ",\"comp\":[";
        for (std::size_t i = 0; i < rec.comp.size(); ++i)
        {
            CompEntry const& c = rec.comp[i];
            if (i)
                s << ',';
            s << "{\"name\":";
            AppendEscaped(s, c.name);
            s << ",\"class\":";
            AppendEscaped(s, c.className);
            s << ",\"spec\":";
            AppendEscaped(s, c.spec);
            s << ",\"role\":";
            AppendEscaped(s, c.role);
            s << ",\"guid\":" << c.guid
              << ",\"level\":" << c.level
              << ",\"detectedRole\":";
            AppendEscaped(s, c.detectedRole);
            s << ",\"roleMismatch\":" << (c.roleMismatch ? "true" : "false")
              << ",\"from\":{\"map\":" << c.fromMap
              << ",\"x\":" << c.fromX << ",\"y\":" << c.fromY << ",\"z\":" << c.fromZ
              << ",\"o\":" << c.fromO << "}}";
        }
        s << "],\"startedAtMs\":" << rec.startedAtMs
          << ",\"endedAtMs\":" << rec.endedAtMs
          << ",\"durationS\":" << rec.durationS
          << ",\"result\":";
        AppendEscaped(s, rec.result);
        s << ",\"failReason\":";
        AppendEscaped(s, rec.failReason);
        s << ",\"disableReason\":";
        AppendEscaped(s, rec.disableReason);
        s << ",\"bossesTotal\":" << rec.bossesTotal
          << ",\"bossesKilled\":" << rec.bossesKilled
          << ",\"bossRoster\":[";
        for (std::size_t i = 0; i < rec.bossRoster.size(); ++i)
        {
            if (i)
                s << ',';
            AppendEscaped(s, rec.bossRoster[i]);
        }
        s << "],\"bossTimeline\":[";
        for (std::size_t i = 0; i < rec.bossTimeline.size(); ++i)
        {
            BossKill const& b = rec.bossTimeline[i];
            if (i)
                s << ',';
            s << "{\"t\":" << b.t << ",\"entry\":" << b.entry << ",\"name\":";
            AppendEscaped(s, b.name);
            s << ",\"via\":";
            AppendEscaped(s, b.via);
            s << '}';
        }
        s << "],\"deaths\":[";
        for (std::size_t i = 0; i < rec.deaths.size(); ++i)
        {
            DeathEntry const& d = rec.deaths[i];
            if (i)
                s << ',';
            s << "{\"t\":" << d.t << ",\"name\":";
            AppendEscaped(s, d.name);
            s << ",\"opponent\":";
            AppendEscaped(s, d.opponent);
            s << ",\"opponentEntry\":" << d.opponentEntry
              << ",\"onBoss\":" << (d.onBoss ? "true" : "false") << '}';
        }
        s << "],\"pulls\":[";
        for (std::size_t i = 0; i < rec.pulls.size(); ++i)
        {
            PullEntry const& p = rec.pulls[i];
            if (i)
                s << ',';
            s << "{\"t\":" << p.t
              << ",\"entry\":" << p.targetEntry
              << ",\"predicted\":" << p.predictedCount
              << ",\"predictedThirds\":" << p.predictedThirds
              << ",\"ceilingThirds\":" << p.ceilingThirds
              << ",\"observed\":" << p.observedMax
              << ",\"observedElites\":" << p.observedElites
              << ",\"advanced\":" << (p.advanced ? "true" : "false")
              << ",\"wipedHere\":" << (p.wipedHere ? "true" : "false") << '}';
        }
        s << "],\"pullsElided\":" << rec.pullsElided
          << ",\"statusTimeline\":[";
        {
            std::vector<StatusEntry> const& st = rec.statusTimeline;
            bool first = true;
            if (st.size() <= kStatusHead + kStatusTail)
            {
                for (StatusEntry const& e : st)
                {
                    if (!first)
                        s << ',';
                    first = false;
                    AppendStatus(s, e);
                }
            }
            else
            {
                for (std::size_t i = 0; i < kStatusHead; ++i)
                {
                    if (!first)
                        s << ',';
                    first = false;
                    AppendStatus(s, st[i]);
                }
                s << ",{\"t\":" << st[kStatusHead].t << ",\"state\":\"...\",\"detail\":\""
                  << (st.size() - kStatusHead - kStatusTail) << " transitions elided\"}";
                for (std::size_t i = st.size() - kStatusTail; i < st.size(); ++i)
                {
                    s << ',';
                    AppendStatus(s, st[i]);
                }
            }
        }
        s << "],\"pauses\":[";
        for (std::size_t i = 0; i < rec.pauses.size(); ++i)
        {
            if (i)
                s << ',';
            s << "{\"t\":" << rec.pauses[i].t << ",\"reason\":";
            AppendEscaped(s, rec.pauses[i].reason);
            s << '}';
        }
        s << "],\"finalTankPos\":{\"map\":" << rec.finalMap
          << ",\"x\":" << rec.finalX << ",\"y\":" << rec.finalY << ",\"z\":" << rec.finalZ
          << "},\"watchdog\":{\"pauseGraceS\":" << rec.pauseGraceS
          << ",\"stallGraceS\":" << rec.stallGraceS
          << ",\"noProgressS\":" << rec.noProgressS
          << ",\"overallS\":" << rec.overallS
          << "},\"setupStage\":";
        AppendEscaped(s, rec.setupStage);
        s << ",\"stallAtEnd\":";
        AppendEscaped(s, rec.stallAtEnd);
        s << ",\"phaseAtEnd\":";
        AppendEscaped(s, rec.phaseAtEnd);
        s << ",\"wipeOnBoss\":" << (rec.wipeOnBoss ? "true" : "false")
          << ",\"wipeOpponentEntry\":" << rec.wipeOpponentEntry
          << ",\"wipeOpponent\":";
        AppendEscaped(s, rec.wipeOpponent);
        s << ",\"diag\":";
        DcDiag::AppendJson(s, rec.diag);
        s << '}';
        return s.str();
    }

    std::string CapturePath()
    {
        if (char const* env = std::getenv("DC_TESTRUNS_FILE"))
            if (env[0])
                return env;
        return "dc_testruns.jsonl";
    }

    std::string LivePath()
    {
        if (char const* env = std::getenv("DC_TESTRUN_LIVE_FILE"))
            if (env[0])
                return env;
        return "dc_testrun_live.json";
    }

    void Append(Record const& rec)
    {
        // One process-wide append stream; runs end minutes apart, so a mutex
        // plus flush-per-line is plenty (same shape as DungeonClearApproachIo).
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

        file << ToJsonl(rec) << '\n';
        file.flush();
    }
}
