/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRegroupDecision.h"

namespace DcRegroupDecision
{
    RegroupVerdict DecideCombatRegroup(RegroupInputs const& in)
    {
        // Never clip a cast; never fight the CC handler. Same guard set as the
        // heal-reposition trigger. This wins even over the hard tether: a bot
        // mid-cast or stunned cannot move usefully anyway, so hold.
        if (in.casting || in.ccd)
            return RegroupVerdict::None;

        // Drifted past the outer tether: reconnect regardless of the contribution
        // test — the chased-runner-into-nowhere / left-far-behind case. Bypasses
        // debounce at the call site (the emergency path).
        if (in.tankDist2d > in.hardTether)
            return RegroupVerdict::HardTether;

        if (!in.isHealer)
        {
            // DPS (melee & ranged). A non-empty stock LOS-filtered attacker list
            // means the rotation + reach-spell(20)/MoveChase(30) have real work —
            // exactly the emptiness test ShouldAssistCampFight relies on. Empty =>
            // nothing visible to fight from here => reconnect. A ranged DPS whose
            // visible target is merely out of cast range is intentionally None:
            // stock `reach spell` closes toward the TARGET, which regroup-to-tank
            // used to fight (defect D1). A melee chasing a runner keeps a visible
            // attacker the whole chase => None until the hard tether.
            return in.hasVisibleAttacker ? RegroupVerdict::None
                                         : RegroupVerdict::Reconnect;
        }

        // Healer. HealReposition (rel 41) owns the hurt-target case — do not
        // double-own it. When a heal target is hurt, stand down here.
        if (in.hasHurtHealTarget)
            return RegroupVerdict::None;

        // Healer pre-positioning: nobody is hurt yet, but the healer is parked where
        // it could not heal the tank the moment damage starts (out of LOS, or beyond
        // heal range less a slack band so a step of tank movement doesn't drop it
        // straight back out). Reconnect to a heal-range LOS point; otherwise None.
        if (!in.tankLos || in.tankDist2d > (in.healRange - in.slack))
            return RegroupVerdict::Reconnect;

        return RegroupVerdict::None;
    }
}
