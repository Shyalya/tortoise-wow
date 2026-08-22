/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCREGROUPDECISION_H
#define _PLAYERBOT_DCREGROUPDECISION_H

// Pure decision kernel for the contribution-gated combat regroup (Option B). Combat
// Regroup is no longer a distance tether: a follower only reconnects to the fight
// when it has NO useful work it can do from where it stands. This kernel is the
// role-aware "can this bot contribute?" test, extracted engine-free so it is
// unit-testable in isolation — mirroring DecidePull / DecideApproach / SelectHeal-
// Target. The trigger gathers the game-state reads into RegroupInputs, calls
// DecideCombatRegroup, then layers debounce/latch/cooldown on the verdict; the
// action turns a fire verdict into a role-correct standoff move. Nothing here
// touches a Player/Unit/context, so no game headers are needed.

namespace DcRegroupDecision
{
    // What the regroup rung should do this tick, before flap control is applied.
    //   None       — the bot can contribute (or must not move): stay put, let stock
    //                 combat / the rotation / the heal stack own the tick.
    //   Reconnect   — the bot cannot contribute from here: walk to a role-correct
    //                 standoff point with LOS on the fight (debounced).
    //   HardTether  — the bot has drifted past the outer tether: reconnect
    //                 regardless of the contribution test (bypasses debounce).
    enum class RegroupVerdict
    {
        None,
        Reconnect,
        HardTether
    };

    // Everything the verdict needs, resolved from live game state by the trigger.
    struct RegroupInputs
    {
        bool  isHealer = false;          // PlayerbotAI::IsHeal(bot)
        bool  isMelee = false;           // PlayerbotAI::IsMelee(bot) — role split for the action's radius
        bool  casting = false;           // a generic or channeled spell is in flight
        bool  ccd = false;               // stunned / fleeing / confused / rooted
        bool  hasVisibleAttacker = false;// stock LOS-filtered `attackers` value non-empty
        bool  hasHurtHealTarget = false; // DcKey::HealTarget resolves to a live hurt member (healers)
        bool  tankLos = false;           // bot->IsWithinLOSInMap(tank)
        float tankDist2d = 0.0f;         // bot->GetExactDist2d(tank)
        float healRange = 0.0f;          // botAI->GetRange("heal")
        float hardTether = 0.0f;         // DungeonClear.CombatRegroupDistance
        float slack = 0.0f;              // DungeonClear.CombatRegroupSlack
    };

    // The verdict, in priority order (see the plan §1):
    //   1. casting || ccd            -> None       (never clip a cast; never fight CC)
    //   2. tankDist2d > hardTether   -> HardTether (drifted-into-nowhere safety net)
    //   3. DPS (melee & ranged):
    //        hasVisibleAttacker      -> None       (rotation + reach/chase have work)
    //        else                    -> Reconnect
    //   4. Healer:
    //        hasHurtHealTarget       -> None       (HealReposition rel 41 owns that case)
    //        else (pre-positioning): !tankLos || tankDist2d > healRange - slack
    //                                -> Reconnect  else None
    RegroupVerdict DecideCombatRegroup(RegroupInputs const& in);
}

#endif  // _PLAYERBOT_DCREGROUPDECISION_H
