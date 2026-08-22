/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcPullDecisionIo.h"
#include "DcDecisionJson.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <mutex>

namespace DcPullDecisionIo
{
    namespace
    {
        struct VerdictNamePair { PullVerdict v; char const* name; };
        constexpr std::array<VerdictNamePair, 7> kVerdictNames = {{
            { PullVerdict::NoOp,             "NoOp"             },
            { PullVerdict::HoldNoTarget,     "HoldNoTarget"     },
            { PullVerdict::DropToLeeroy,     "DropToLeeroy"     },
            { PullVerdict::Leeroy,           "Leeroy"           },
            { PullVerdict::ApproachAsLeeroy, "ApproachAsLeeroy" },
            { PullVerdict::PatrolWaitHold,   "PatrolWaitHold"   },
            { PullVerdict::Advanced,         "Advanced"         },
        }};
    }

    char const* VerdictName(PullVerdict v)
    {
        for (auto const& p : kVerdictNames)
            if (p.v == v)
                return p.name;
        return "NoOp";  // unreachable; the table is total over the enum
    }

    bool VerdictFromName(std::string_view name, PullVerdict& out)
    {
        for (auto const& p : kVerdictNames)
            if (name == p.name)
            {
                out = p.v;
                return true;
            }
        return false;
    }

    std::string ToJsonl(PullDecisionRecord const& rec)
    {
        PullObservation const& o = rec.obs;
        return DcDecisionJson::Writer()
            .Add("guid", rec.guid)
            .Add("tick", rec.tick)
            .AddStr("verdict", VerdictName(rec.verdict))
            .Add("hasTarget", o.hasTarget)
            .Add("inCombat", o.inCombat)
            .Add("phaseIdle", o.phaseIdle)
            .Add("hasStandingVerdict", o.hasStandingVerdict)
            .Add("verdictGraceExpired", o.verdictGraceExpired)
            .Add("sameTarget", o.sameTarget)
            .Add("currentlyAdvanced", o.currentlyAdvanced)
            .Add("recheckElapsed", o.recheckElapsed)
            .Add("advanced", o.advanced)
            .Add("patrolWaitEnabled", o.patrolWaitEnabled)
            .Add("atCommitRange", o.atCommitRange)
            .Add("patrolContended", o.patrolContended)
            .Add("patrolWaitExpired", o.patrolWaitExpired)
            .Str();
    }

    bool FromJsonl(std::string const& line, PullDecisionRecord& out)
    {
        auto const parsed = DcDecisionJson::Parse(line);
        if (!parsed)
            return false;
        auto const& m = *parsed;

        std::string const vName = DcDecisionJson::GetStr(m, "verdict", "");
        if (vName.empty() || !VerdictFromName(vName, out.verdict))
            return false;

        out.guid = DcDecisionJson::GetU64(m, "guid", 0);
        out.tick = DcDecisionJson::GetU(m, "tick", 0);

        PullObservation o;  // struct defaults survive any absent field
        o.hasTarget           = DcDecisionJson::GetB(m, "hasTarget", o.hasTarget);
        o.inCombat            = DcDecisionJson::GetB(m, "inCombat", o.inCombat);
        o.phaseIdle           = DcDecisionJson::GetB(m, "phaseIdle", o.phaseIdle);
        o.hasStandingVerdict  = DcDecisionJson::GetB(m, "hasStandingVerdict", o.hasStandingVerdict);
        o.verdictGraceExpired = DcDecisionJson::GetB(m, "verdictGraceExpired", o.verdictGraceExpired);
        o.sameTarget          = DcDecisionJson::GetB(m, "sameTarget", o.sameTarget);
        o.currentlyAdvanced   = DcDecisionJson::GetB(m, "currentlyAdvanced", o.currentlyAdvanced);
        o.recheckElapsed      = DcDecisionJson::GetB(m, "recheckElapsed", o.recheckElapsed);
        o.advanced            = DcDecisionJson::GetB(m, "advanced", o.advanced);
        o.patrolWaitEnabled   = DcDecisionJson::GetB(m, "patrolWaitEnabled", o.patrolWaitEnabled);
        o.atCommitRange       = DcDecisionJson::GetB(m, "atCommitRange", o.atCommitRange);
        o.patrolContended     = DcDecisionJson::GetB(m, "patrolContended", o.patrolContended);
        o.patrolWaitExpired   = DcDecisionJson::GetB(m, "patrolWaitExpired", o.patrolWaitExpired);
        out.obs = o;
        return true;
    }

    std::string CapturePath()
    {
        if (char const* env = std::getenv("DUNGEONCLEAR_PULL_DECISIONS_FILE"))
            if (env[0])
                return env;
        return "dungeonclear_pull_decisions.jsonl";
    }

    void Record(std::uint64_t guid, std::uint32_t tick, PullObservation const& obs,
                PullVerdict verdict)
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

        file << ToJsonl(PullDecisionRecord{guid, tick, obs, verdict}) << '\n';
        file.flush();
    }
}
