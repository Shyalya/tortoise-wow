/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCREZDECISION_H
#define _PLAYERBOT_DCREZDECISION_H

#include <cstdint>
#include <vector>

// Pure decision kernel for post-combat party resurrection. Historically the
// FIRST death of any party member ended the run: the party-died trigger fired
// the disable-on-death bailout the moment combat dropped. That throws away runs
// a Priest/Paladin/Shaman/Druid in the party could trivially recover — the
// module even preserves rez-able corpses (StayDeadAction / PreventBotRelease)
// naming a party rez as the intended wake path, but nothing ever performed it.
//
// This kernel answers, from a plain snapshot of the same-map party: should the
// run HOLD for a resurrection (and if so, who rezzes whom), or DISABLE because
// recovery is not viable (full wipe, no living rez class, out-of-combat
// recovery clock expired)?
//
// Election is deterministic from group order so every bot computes the same
// answer independently (no cross-bot negotiation, no stored rezzer that can go
// stale — a dead rezzer simply drops out of the next evaluation and the next
// candidate is elected):
//   rezzer: first living rez-class BOT, healers before non-healers. If only
//           non-bot (human) rezzers are alive -> WaitingOnHuman.
//   target: dead healer -> dead tank -> group order (recover the run's spine
//           first).
//
// The recovery clock is OWNED BY THE GLUE (DcRezRecovery stamps/clears
// DcRunState::rezPendingSinceMs); the kernel only compares. partyEngaged
// freezes the timeout so fighting time never burns the recovery budget (a
// mid-recovery add pull must not expire the clock). It is engagement, not the
// combat flag: a rez cannot be cast while flagged, so under a phantom flag
// (DcCombatFlag) the flag would freeze the very timeout meant to bound a
// recovery that can never complete.
//
// Extracted engine-free so it is unit-testable in isolation, mirroring
// DcSmartRestDecision / DecidePull. Header-only; nothing here touches a
// Player/Unit/context, so no game headers are needed.

namespace DcRezDecision
{
    // One same-map group member, snapshotted by the glue (which, unlike the
    // Smart Rest snapshot, KEEPS dead members — they are the whole point).
    struct Member
    {
        bool isDead = false;
        bool canRezClass = false;  // Priest / Paladin / Shaman / Druid
        bool isHealerRole = false; // PlayerbotAI::IsHeal — healers rez first
        bool isTankRole = false;   // PlayerbotAI::IsTank — target priority
        bool isBot = false;        // has a PlayerbotAI to drive (humans don't)
    };

    struct Inputs
    {
        bool          enabled = true;         // DungeonClear.PostCombatRez
        std::uint32_t nowMs = 0;
        std::uint32_t pendingSinceMs = 0;     // recovery clock; 0 = not running
        std::uint32_t timeoutMs = 90000;      // PostCombatRezTimeoutSecs * 1000
        bool          partyEngaged = false;  // freezes the timeout

        // --- the NoRezzer floor (see the branch below) ------------------------
        // Is any LIVING survivor still carrying the core combat flag? Deliberately
        // the flag and not engagement: the case this exists for is a boss that has
        // stopped fighting but not yet released the party.
        bool          anySurvivorCombatFlagged = false;
        // getMSTime() the survivors first read BOTH unengaged and unflagged, with
        // no rezzer left; 0 = not quiet. Owned by the glue, cleared by any tick
        // that is not quiet.
        std::uint32_t noRezzerQuietSinceMs = 0;
        std::uint32_t noRezzerQuietGraceMs = 0;   // quiet must hold this long
        // getMSTime() the party first had no rezzer at all; 0 = not yet. Bounds
        // the flag-only hold so a flag nothing ever clears cannot hang the run.
        std::uint32_t noRezzerSinceMs = 0;
        std::uint32_t noRezzerHoldMaxMs = 0;      // 0 = no ceiling
    };

    enum class Outcome
    {
        None,     // no deaths (or the feature is disabled — the glue converts)
        Hold,     // suppress the bailout; recovery is in progress
        Disable   // recovery not viable — run the classic disable funnel
    };

    enum class Reason
    {
        NoDeaths,
        Disabled,        // feature off — kernel stands down (glue decides)
        Recovering,      // a bot rezzer is elected and will act
        WaitingOnHuman,  // only a human can rez — hold and prompt
        Wipe,            // everyone on the map is dead
        NoRezzer,        // no living member's class can rez
        NoRezzerInFight, // ditto, but the survivors are still swinging — hold
        TimedOut         // out-of-combat recovery clock expired
    };

    struct Result
    {
        Outcome outcome = Outcome::None;
        Reason  reason  = Reason::NoDeaths;
        int     rezzerIdx = -1;  // elected rezzer (the human for WaitingOnHuman)
        int     targetIdx = -1;  // dead member to raise first
    };

    // Target priority: dead healer -> dead tank -> group order. The healer
    // back up first restores the party's ability to survive the next mistake;
    // the tank next restores the run's driver.
    inline int PickTarget(std::vector<Member> const& members)
    {
        int firstDead = -1, deadTank = -1;
        for (std::size_t i = 0; i < members.size(); ++i)
        {
            if (!members[i].isDead)
                continue;
            if (members[i].isHealerRole)
                return static_cast<int>(i);
            if (deadTank < 0 && members[i].isTankRole)
                deadTank = static_cast<int>(i);
            if (firstDead < 0)
                firstDead = static_cast<int>(i);
        }
        return deadTank >= 0 ? deadTank : firstDead;
    }

    // The verdict. Empty roster / no deaths -> None. See the header comment
    // for the election and timeout rules.
    inline Result Decide(Inputs const& in, std::vector<Member> const& members)
    {
        Result r;

        bool anyDead = false, anyAlive = false;
        for (Member const& m : members)
        {
            anyDead  = anyDead  || m.isDead;
            anyAlive = anyAlive || !m.isDead;
        }
        if (!anyDead)
            return r;  // None / NoDeaths

        if (!in.enabled)
        {
            // Feature off: the kernel stands down entirely. The glue converts
            // this into the classic immediate disable.
            r.reason = Reason::Disabled;
            return r;
        }

        if (!anyAlive)
        {
            // Full wipe. A dead self-res candidate (soulstone/reincarnation)
            // deliberately does not count in v1.
            r.outcome = Outcome::Disable;
            r.reason = Reason::Wipe;
            return r;
        }

        // Election: first living rez-class bot, healers before non-healers;
        // remember the first living human rezzer for the WaitingOnHuman path.
        int botHealer = -1, botAny = -1, humanAny = -1;
        for (std::size_t i = 0; i < members.size(); ++i)
        {
            Member const& m = members[i];
            if (m.isDead || !m.canRezClass)
                continue;
            if (m.isBot)
            {
                if (m.isHealerRole && botHealer < 0)
                    botHealer = static_cast<int>(i);
                if (botAny < 0)
                    botAny = static_cast<int>(i);
            }
            else if (humanAny < 0)
                humanAny = static_cast<int>(i);
        }

        int const botRezzer = botHealer >= 0 ? botHealer : botAny;
        if (botRezzer < 0 && humanAny < 0)
        {
            // NO REZ CLASS LEFT — but not necessarily a lost run.
            //
            // This fires the instant the last rez-capable member dies, and it used
            // to end the run on the spot regardless of what the survivors were
            // doing. The sole healer dying is the NORMAL shape of a hard heroic
            // pull, and the remaining four finishing the pack off is a normal way
            // for it to end: twice in the MgT heroic audit (tp-20260805-005412-1)
            // a run was disabled with four members alive and the fight already
            // being won. NoRezzer is 7 of that plan's 12 disables.
            //
            // Nothing about the verdict is wrong, only its TIMING. Deciding "we
            // cannot recover from this death" while the death is still being
            // avenged reads a mid-fight snapshot as a final state. Hold while the
            // party is engaged; the moment combat ends, partyEngaged goes false and
            // this same branch returns the Disable it always did. A party that goes
            // on to wipe reaches Reason::Wipe above instead, which is the more
            // accurate verdict anyway.
            //
            // THAT HOLD WAS STILL ONE TICK WIDE, and one tick is not a fight ending.
            // partyEngaged is `victim || attackers`, which goes false in the gap
            // between a mob's swings — and, decisively, for the ELEVEN SECONDS
            // Magisters' Terrace's Kael'thas spends immune, passive and summonless
            // at 1 HP playing his defeat sequence before KillSelf. In
            // tp-20260808-162331-1 that ended two runs that had already won:
            // tr-20260808-162337-13 was disabled two seconds after its tank was
            // logged kiting him through gravity lapse, tr-20260808-165249-40 eight
            // seconds after, both with four members alive and all four still
            // carrying his combat flag. Eight of that plan's twenty failures were
            // this branch, and EVERY ONE had 2-4 members standing.
            //
            // So three gates now, not one:
            //   * engaged            — a fight is visibly in progress (unchanged);
            //   * still FLAGGED      — something has us and has not let go, which is
            //                          exactly the defeat-sequence shape;
            //   * quiet not yet held — even with both clear, the silence has to last
            //                          noRezzerQuietGraceMs before it counts as over.
            // The flag hold is capped by noRezzerHoldMaxMs so a flag nothing ever
            // clears degrades to the old behaviour instead of hanging the run.
            bool const holdCapped =
                in.noRezzerHoldMaxMs != 0 && in.noRezzerSinceMs != 0 &&
                in.nowMs >= in.noRezzerSinceMs &&
                (in.nowMs - in.noRezzerSinceMs) >= in.noRezzerHoldMaxMs;
            bool const quietHeld =
                in.noRezzerQuietGraceMs == 0 ||
                (in.noRezzerQuietSinceMs != 0 && in.nowMs >= in.noRezzerQuietSinceMs &&
                 (in.nowMs - in.noRezzerQuietSinceMs) >= in.noRezzerQuietGraceMs);
            if (!holdCapped &&
                (in.partyEngaged || in.anySurvivorCombatFlagged || !quietHeld))
            {
                r.outcome = Outcome::Hold;
                r.reason = Reason::NoRezzerInFight;
                r.targetIdx = PickTarget(members);
                return r;
            }
            r.outcome = Outcome::Disable;
            r.reason = Reason::NoRezzer;
            return r;
        }

        r.targetIdx = PickTarget(members);
        r.rezzerIdx = botRezzer >= 0 ? botRezzer : humanAny;

        // Timeout: only while the glue's no-fight clock is running. A fight
        // freezes it (the glue additionally clears the stamp, so a fight resets
        // the budget rather than expiring it early — the safe direction).
        if (!in.partyEngaged && in.pendingSinceMs != 0 && in.timeoutMs != 0 &&
            in.nowMs - in.pendingSinceMs >= in.timeoutMs)
        {
            r.outcome = Outcome::Disable;
            r.reason = Reason::TimedOut;
            return r;
        }

        r.outcome = Outcome::Hold;
        r.reason = botRezzer >= 0 ? Reason::Recovering : Reason::WaitingOnHuman;
        return r;
    }
}

#endif  // _PLAYERBOT_DCREZDECISION_H
