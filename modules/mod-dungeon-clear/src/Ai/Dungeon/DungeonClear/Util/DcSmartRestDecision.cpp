/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcSmartRestDecision.h"

namespace DcSmartRestDecision
{
    float ManaTriggerPct(Member const& m, Inputs const& in)
    {
        if (!m.isManaUser)
            return 0.0f;
        return m.isHealer ? in.healerManaTriggerPct : in.dpsManaTriggerPct;
    }

    float HpReleaseBar(Member const&, Inputs const&)
    {
        return kReleasePct;
    }

    float ManaReleaseBar(Member const& m, Inputs const&)
    {
        return m.isManaUser ? kManaReleasePct : 0.0f;
    }

    bool BelowTrigger(Member const& m, Inputs const& in)
    {
        if (in.hpTriggerPct > 0.0f && m.hpPct < in.hpTriggerPct)
            return true;
        // Boss pull imminent: any mana user short of its RELEASE bar latches a
        // top-off rest — never open a boss fight on a half tank of mana that
        // merely clears the low trash triggers.
        if (in.bossPull && m.isManaUser && m.manaPct < ManaReleaseBar(m, in))
            return true;
        float const manaTrigger = ManaTriggerPct(m, in);
        return manaTrigger > 0.0f && m.manaPct < manaTrigger;
    }

    bool BelowRelease(Member const& m, Inputs const& in)
    {
        return m.hpPct < HpReleaseBar(m, in) || m.manaPct < ManaReleaseBar(m, in);
    }

    Result Decide(Inputs const& in, std::vector<Member> const& members)
    {
        Result out;

        if (!in.latched)
        {
            // The post-timeout cooldown: a timed-out release must not re-latch
            // on the very member that timed it out (an AFK human, a bot with no
            // food) or the party flaps latch/timeout forever.
            if (!in.rearmed)
                return out;

            for (std::size_t i = 0; i < members.size(); ++i)
                if (BelowTrigger(members[i], in))
                    out.blockers.push_back(i);
            out.latched = !out.blockers.empty();
            return out;
        }

        // Latched. Failsafe first: a member that CANNOT reach its release bar
        // (human never drinks, bot with no food and the food cheat off) must
        // not stall the run forever — the legacy gate could; we bound it.
        if (in.maxRestMs > 0 && in.restElapsedMs >= in.maxRestMs)
        {
            out.timedOut = true;
            return out;  // released
        }

        for (std::size_t i = 0; i < members.size(); ++i)
            if (BelowRelease(members[i], in))
                out.blockers.push_back(i);
        out.latched = !out.blockers.empty();
        return out;
    }
}
