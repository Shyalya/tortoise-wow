/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestRunLiveJson.h"

#include <iomanip>
#include <sstream>

#include "TestRun/DcTestRunRecord.h"

namespace DcTestRunLive
{
    // One-decimal fixed formatting keeps the coordinate payload small and the
    // file readable — sub-decimetre precision is meaningless for a map dot and
    // would only bloat the heartbeat with noise digits.
    static std::string Dp1(float v)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision(1) << v;
        return o.str();
    }

    std::string Build(std::uint64_t tsS, std::vector<RunSnapshot> const& runs,
                      std::vector<PlanSnapshot> const& plans)
    {
        using DcTestRunRecord::EscapeJson;

        std::ostringstream s;
        s << "{\"active\":" << (runs.empty() && plans.empty() ? "false" : "true")
          << ",\"ts\":" << tsS
          << ",\"plans\":[";
        for (std::size_t p = 0; p < plans.size(); ++p)
        {
            PlanSnapshot const& plan = plans[p];
            if (p)
                s << ',';
            s << "{\"planId\":\"" << EscapeJson(plan.planId) << '"'
              << ",\"dungeon\":\"" << EscapeJson(plan.dungeon) << '"'
              << ",\"total\":" << plan.total
              << ",\"launched\":" << plan.launched
              << ",\"succeeded\":" << plan.succeeded
              << ",\"failed\":" << plan.failed
              << ",\"active\":" << plan.activeNow
              << ",\"concurrent\":" << plan.concurrent
              << ",\"heroic\":" << (plan.heroic ? "true" : "false")
              << ",\"state\":\"" << EscapeJson(plan.state) << '"'
              << ",\"elapsedS\":" << plan.elapsedS << '}';
        }
        s << "],\"runs\":[";
        for (std::size_t r = 0; r < runs.size(); ++r)
        {
            RunSnapshot const& run = runs[r];
            if (r)
                s << ',';
            s << "{\"runId\":\"" << EscapeJson(run.runId) << '"'
              << ",\"planId\":\"" << EscapeJson(run.planId) << '"'
              << ",\"dungeon\":\"" << EscapeJson(run.dungeon) << '"'
              << ",\"dungeonName\":\"" << EscapeJson(run.dungeonName) << '"'
              << ",\"stage\":\"" << EscapeJson(run.stage) << '"'
              << ",\"level\":" << run.level
              << ",\"heroic\":" << (run.heroic ? "true" : "false")
              << ",\"elapsedS\":" << run.elapsedS
              << ",\"bossesKilled\":" << run.bossesKilled
              << ",\"bossesTotal\":" << run.bossesTotal
              << ",\"state\":\"" << EscapeJson(run.state) << '"'
              << ",\"mapId\":" << run.mapId
              << ",\"stall\":\"" << EscapeJson(run.stall) << '"'
              << ",\"bossName\":\"" << EscapeJson(run.bossName) << '"'
              << ",\"sinceProgressS\":" << run.sinceProgressS
              << ",\"inCombat\":" << (run.inCombat ? "true" : "false")
              << ",\"wiped\":" << (run.wiped ? "true" : "false")
              << ",\"wipeOnBoss\":" << (run.wipeOnBoss ? "true" : "false")
              << ",\"wipeOpponent\":\"" << EscapeJson(run.wipeOpponent) << '"'
              << ",\"bots\":[";
            for (std::size_t b = 0; b < run.bots.size(); ++b)
            {
                BotPos const& p = run.bots[b];
                if (b)
                    s << ',';
                s << "{\"role\":\"" << EscapeJson(p.role) << '"'
                  << ",\"name\":\"" << EscapeJson(p.name) << '"'
                  << ",\"cls\":" << static_cast<unsigned>(p.classId)
                  << ",\"x\":" << Dp1(p.x)
                  << ",\"y\":" << Dp1(p.y)
                  << ",\"z\":" << Dp1(p.z)
                  << ",\"alive\":" << (p.alive ? "true" : "false")
                  << ",\"hp\":" << static_cast<unsigned>(p.hp)
                  << ",\"inCombat\":" << (p.inCombat ? "true" : "false")
                  << ",\"mp\":" << static_cast<int>(p.mp) << '}';
            }
            s << "]"
              << ",\"recent\":[";
            for (std::size_t i = 0; i < run.recent.size(); ++i)
            {
                StatusEntry const& e = run.recent[i];
                if (i)
                    s << ',';
                s << "{\"t\":" << e.t << ",\"state\":\"" << EscapeJson(e.state)
                  << "\",\"detail\":\"" << EscapeJson(e.detail) << "\"}";
            }
            s << "]}";
        }
        s << "]}";
        return s.str();
    }
}
