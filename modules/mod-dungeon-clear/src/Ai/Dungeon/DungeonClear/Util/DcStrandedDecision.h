/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSTRANDEDDECISION_H
#define _PLAYERBOT_DCSTRANDEDDECISION_H

#include <cstdint>
#include <vector>

// Pure decision kernel for the stranded-member recovery failsafe. The dominant
// way a run stalls now is a party member falling THROUGH the world geometry (or
// wedging somewhere the navmesh can't recover from): it drifts out of range, the
// between-pulls spread gate then holds the tank forever waiting for it to catch
// up, and the whole run freezes with nothing to do about it. Follow-tank keeps
// re-issuing a path that can never close the gap, so the deadlock is permanent.
//
// This kernel answers, from the run's no-progress clock plus a plain snapshot of
// each same-map member's distance to the tank: should the leader teleport the
// stuck member(s) to itself right now, and which members?
//
// Two guards, in order:
//   1. The run must have shown NO sign of progress for noProgressTimeoutMs. The
//      GLUE (DcStrandedRecovery) owns that clock on DcRunState::progressMs,
//      re-stamping it on any sign of life (a boss/objective completed, the tank
//      closing on the next anchor); a FIGHT re-arms it too, so a legitimately
//      slow pull or a long fight never trips the failsafe. The kernel only
//      compares. "Fight" is engagement (a victim / attackers), never the bare
//      combat flag — a hostile area aura sets that flag with nothing aggroed, and
//      keying on it made this failsafe permanently inert exactly when the run
//      needed it (see DcCombatFlag).
//   2. At least one BOT member must be stranded beyond maxSpread of the tank
//      (PartyMaxSpread — the module's canonical "out of range"). A human is never
//      relocated (player agency); dead members are the rez recovery's job.
//
// Extracted engine-free so it is unit-testable in isolation, mirroring
// DcRezDecision / DcSmartRestDecision. Header-only; nothing here touches a
// Player/Unit/context, so no game headers are needed.

namespace DcStrandedDecision
{
    // One same-map group member, snapshotted by the glue.
    struct Member
    {
        bool  isBot = false;       // has a PlayerbotAI to drive (only bots teleport)
        bool  isAlive = false;     // dead members belong to the rez recovery, not here
        bool  onMap = false;       // same map/instance as the tank
        bool  isTank = false;      // the reference we teleport TO; never itself moved
        float distToTank = 0.0f;   // tank->GetDistance(member)
    };

    struct Inputs
    {
        bool          enabled = true;             // DungeonClear.StrandedRecovery
        std::uint32_t nowMs = 0;
        std::uint32_t lastProgressMs = 0;         // clock; 0 = unarmed (no verdict yet)
        std::uint32_t noProgressTimeoutMs = 60000;   // StrandedRecoveryNoProgressSecs * 1000
        bool          partyEngaged = false;      // a real fight (not the bare flag)
        float         maxSpread = 25.0f;          // PartyMaxSpread: the out-of-range threshold
    };

    struct Result
    {
        bool             recover = false;   // teleport the strays this tick
        std::vector<int> strandedIdx;       // members to teleport (indices into the snapshot)
    };

    // The verdict. Returns recover=false (no strays) whenever the feature is off,
    // the party is in combat, the clock is unarmed / disabled, or the no-progress
    // window has not elapsed. See the header comment for the two guards.
    inline Result Decide(Inputs const& in, std::vector<Member> const& members)
    {
        Result r;

        if (!in.enabled || in.partyEngaged)
            return r;

        // Clock must be armed (a run underway) and the timeout enabled.
        if (in.lastProgressMs == 0 || in.noProgressTimeoutMs == 0)
            return r;

        // Wrap-safe elapsed compare (matches the getMSTimeDiff the glue passes in
        // via nowMs - lastProgressMs on the same monotonic clock).
        if (in.nowMs - in.lastProgressMs < in.noProgressTimeoutMs)
            return r;

        for (std::size_t i = 0; i < members.size(); ++i)
        {
            Member const& m = members[i];
            if (m.isTank || !m.isBot || !m.isAlive || !m.onMap)
                continue;
            if (m.distToTank > in.maxSpread)
                r.strandedIdx.push_back(static_cast<int>(i));
        }
        r.recover = !r.strandedIdx.empty();
        return r;
    }
}

#endif  // _PLAYERBOT_DCSTRANDEDDECISION_H
