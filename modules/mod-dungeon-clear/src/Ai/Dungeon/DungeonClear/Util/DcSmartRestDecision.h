/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSMARTRESTDECISION_H
#define _PLAYERBOT_DCSMARTRESTDECISION_H

#include <cstddef>
#include <cstdint>
#include <vector>

// Pure decision kernel for Smart Rest — the hysteresis rest latch. The legacy
// rest gate is a single threshold used for BOTH ends (the party stops whenever
// anyone is below RestHealthPct/RestManaPct and rests back up to that same
// value), which produces constant micro-rests. Smart Rest splits the two ends:
// a LOW, role-based trigger (DPS/tank mana, healer mana, any-role HP) latches a
// party-wide rest, and the release bar is full health but only a near-full mana
// (the last sliver tops off free while walking) — fewer, longer rests. Between
// rests eating/drinking is fully suppressed (the multiplier's
// job, keyed off this latch). One exception to the low entry: a BOSS pull
// (Inputs::bossPull) latches at the mana release bar itself, so the party
// always opens a boss fight topped off, never just-above the trash triggers.
//
// Extracted engine-free so it is unit-testable in isolation, mirroring
// DecidePull / DecideCombatRegroup. DcSmartRest (the glue) gathers the live
// group into Member snapshots, calls Decide against the latch stored in the
// leader's DcRunState, and writes the verdict back. Nothing here touches a
// Player/Unit/context, so no game headers are needed.

namespace DcSmartRestDecision
{
    // HP release bar, every member: "full", float-safe. GetHealthPct/
    // GetPowerPct return 100.0f exactly when full (cur/max with cur==max), but a
    // buff or aura shifting the max pool mid-rest could strand a bot at 99.x
    // forever — half a percent of slack costs nothing perceptible.
    constexpr float kReleasePct = 99.5f;

    // Mana release bar, every member: deliberately short of full. The last
    // sliver of mana regenerates for free while the party walks to the next
    // pack, so holding the whole party at rest to claw back the final ~10% just
    // burns real time for no combat benefit. 90% stays far above every mana
    // trigger (DPS 10 / healer 40), so hysteresis holds — a release can never
    // instantly re-latch. HP keeps the full kReleasePct bar (a low HP bar would
    // send bots into the next pull hurt).
    constexpr float kManaReleasePct = 90.0f;

    // Humans hold to the SAME bars as bots. They used to owe only their role
    // trigger plus a 5% margin, on the reasoning that a human can't be forced
    // to drink and would otherwise deadlock the latch. That made the feature
    // useless in a group with a real player: at the default DPS trigger of 10
    // the human released at 15%, a five-point rest indistinguishable from the
    // legacy gate, and the party walked off while the player was still sitting
    // (mod-dungeon-clear#6). The deadlock it guarded against is already bounded
    // by the maxRestMs failsafe below — an AFK human costs one timeout, not a
    // stalled run — so the split bought nothing and cost the whole point of
    // Smart Rest: the party waits for YOU to finish drinking.

    // One living, same-map group member, snapshotted by the glue. Dead and
    // off-map members are the snapshot builder's job to exclude, not ours.
    struct Member
    {
        float hpPct = 100.0f;
        float manaPct = 100.0f;   // meaningful only when isManaUser
        bool  isManaUser = false; // powerType == POWER_MANA && maxMana > 0
        bool  isHealer = false;   // PlayerbotAI::IsHeal — selects the mana trigger
        // No isBot: bots and humans are held to identical bars. See the
        // kHumanReleaseMarginPct removal note above.
    };

    struct Inputs
    {
        bool          latched = false;     // stored latch (DcRunState::smartRestLatched)
        std::uint32_t restElapsedMs = 0;   // now - smartRestSinceMs; 0 when not latched
        bool          rearmed = true;      // false during the post-timeout cooldown
        float         hpTriggerPct = 50.0f;       // SmartRestHealthPct (all roles)
        float         dpsManaTriggerPct = 10.0f;  // SmartRestDpsManaPct (DPS + tanks)
        float         healerManaTriggerPct = 40.0f;  // SmartRestHealerManaPct
        std::uint32_t maxRestMs = 0;       // timeout failsafe; 0 = never time out

        // The next pull is a BOSS and the tank is inside its engage range. Raises
        // the latch entry from the low role triggers to each member's MANA release
        // bar: a healer at 45% is fine to push trash on, but must not open a boss
        // fight — the party tops off to the 90% bar first. Mana only; HP has no
        // boss-specific bar (the healer keeps the party topped between pulls, and
        // a genuinely hurt member is the hpTriggerPct trigger's job). Entry at the
        // release bar keeps the hysteresis exact: a fresh release leaves everyone
        // AT the bar, so it can never instantly re-latch.
        bool          bossPull = false;
    };

    struct Result
    {
        bool latched = false;
        bool timedOut = false;             // released by the failsafe this eval
        std::vector<std::size_t> blockers; // members below trigger (entering) or
                                           // below their release bar (holding)
    };

    // The member's mana trigger for its role. 0 = that dimension disabled.
    float ManaTriggerPct(Member const& m, Inputs const& in);

    // Release bars for one member, bot and human alike: kReleasePct on HP and
    // kManaReleasePct on mana, on every applicable dimension even one whose
    // trigger is disabled (a rest is a rest). Mana bar is 0 for non-mana users.
    // Neither bar reads the triggers, so both ignore Inputs entirely — the
    // parameter stays for call-site symmetry with the rest of the kernel.
    float HpReleaseBar(Member const& m, Inputs const& in);
    float ManaReleaseBar(Member const& m, Inputs const& in);

    // The two halves of the hysteresis, exposed so the glue's DescribeWait can
    // name blockers with the exact same rules Decide applies. BelowTrigger also
    // owns the boss-pull entry (in.bossPull raises the mana entry to the mana
    // release bar).
    bool BelowTrigger(Member const& m, Inputs const& in);
    bool BelowRelease(Member const& m, Inputs const& in);

    // The verdict:
    //   Not latched: latch when rearmed and ANY member is BelowTrigger.
    //   Latched:     time out when maxRestMs > 0 and restElapsedMs >= maxRestMs;
    //                otherwise release only when NO member is BelowRelease.
    // Empty member list never latches.
    Result Decide(Inputs const& in, std::vector<Member> const& members);
}

#endif  // _PLAYERBOT_DCSMARTRESTDECISION_H
