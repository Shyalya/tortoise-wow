/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCCOMBATFLAG_H
#define _PLAYERBOT_DCCOMBATFLAG_H

namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
class Player;

// "Is this a FIGHT, or only the combat FLAG?" — the one shared answer for every
// DC rung that used to stand down on a bare `bot->IsInCombat()`.
//
// A hostile area aura sets the flag with nothing behind it. The Arcatraz Eredar
// room's auras — Entropic Aura (36784) and Unholy Aura (27987 / 38844 heroic) —
// both reach 45yd against a ~20yd creature aggro radius, so there is a wide
// annulus where the whole party is "in combat" with nothing aggroed: no victim,
// no attacker, no threat. Whether the aura also HURTS decides how fast the
// freeze kills (Unholy is 450/750 every 2s, Entropic is harmless), but the
// freeze itself is the same either way. Playerbots never flips to the
// combat engine on the flag alone (only AttackAction / PullMyTargetAction do, and
// both need a chosen target), so the combat ladder does not run either. Every
// rung that stands down on the raw flag therefore goes inert with no fight to
// hand off to, and the run freezes on the spot.
//
// Three questions, one latch:
//   AnyPartyEngagement — is anyone in the party actually fighting?
//   MayDrive           — may a DC rung own this tick? (the leader's driving
//                        ladder, and the follower's follow-tank rung)
//   IsPhantomFlag      — are we flagged with NOTHING behind it? The rest gates
//                        ask this: nobody can eat or drink while flagged, so a
//                        gate that waits for HP/mana here waits forever.
//
// The grace inside MayDrive (DungeonClearMath::MayDriveWhileFlagged) is what
// keeps this from yanking a bot out of a real fight during a one-tick retarget
// hole; the latch lives on the bot's own DcApproachState, shared because every
// caller is asking the same question about the same bot on the same tick.
namespace DcCombatFlag
{
    // Is anyone in this bot's party ACTUALLY FIGHTING right now — as opposed to
    // merely carrying the core combat flag?
    //
    // Party-wide on purpose. Testing only the leader would let it walk off while
    // a pack is chewing on the healer, whenever the leader's own target happened
    // to die first. `getAttackers()` covers everything that has Attack()ed the
    // member (melee and casters alike, since CreatureAI::AttackStart sets it),
    // and the victim covers the member attacking something that is not hitting
    // back yet.
    //
    // Is THIS player actually fighting: it has a victim, or something is
    // attacking it — either one WITHIN DC_ENGAGEMENT_RADIUS. The one definition
    // of "engaged" the module shares.
    //
    // The radius is not a fight radius, it is the sanity bound that keeps a
    // combat reference from outliving the geometry it was made in. Nothing
    // clears the attacker set or the victim pointer when a one-way relocation
    // puts hundreds of yards of unwalkable map between the two, and an
    // unqualified `victim || attackers` then reports a fight for the rest of the
    // run. See DC_ENGAGEMENT_RADIUS for what that cost.
    bool IsEngaged(Player* p);

    bool AnyPartyEngagement(Player* bot);

    // Is anyone in the party carrying the core combat FLAG (nothing about whether
    // a fight is behind it)? The rest gates' half of the question: a member held
    // in combat cannot eat or drink, and the gate waits for every member.
    bool AnyPartyCombatFlag(Player* bot);

    // What a walk of a player's PvE combat references found.
    //
    // `opaque` means there were no references at all — script-forced or otherwise
    // unattributable combat, which the phantom hatch treats as legitimate by
    // default and callers asking "is a live enemy on us" must treat as "cannot
    // say". Kept separate from `found` precisely because those two callers want
    // opposite answers for it.
    struct HolderScan
    {
        bool  opaque = false;
        bool  found = false;        // >=1 live, same-map, non-evading, WITHIN
                                    // DC_ENGAGEMENT_RADIUS and reachable holder
                                    // its own AI allows to attack us
        float nearestDist = 0.0f;   // distance to the closest such holder
    };

    // The one walk of a player's PvE combat refs, shared by the phantom-combat
    // hatch (which wants opaque || found) and by IsHeldByLiveEnemy (which does
    // not). Costs a pathfind per reference — call it behind the cheap reads.
    HolderScan ScanCombatHolders(Player* p);

    // Is a live enemy still on this player, close enough to keep swinging?
    //
    // The question IsEngaged cannot answer. IsEngaged is `victim || attackers`,
    // which is a snapshot of who is mid-swing THIS INSTANT: it goes false in the
    // gap between a mob's swings, while a caster is casting, while a pack walks
    // back into range, and — the case that cost two finished runs — for the 11
    // seconds Kael'thas spends immune and passive at 1 HP playing his defeat
    // sequence before KillSelf. Anything that ends a run, or sends a lone bot
    // walking across a room, has to ask this instead.
    //
    // `radius` keeps it honest about the case DcCombatFlag was built for: a
    // hostile AREA AURA holds the flag from 45yd with nothing on us, and no
    // radius a fight actually happens at will admit it.
    bool IsHeldByLiveEnemy(Player* p, float radius);
    bool AnyPartyHeldByLiveEnemy(Player* bot, float radius);

    // May a DC rung own this tick? True when not flagged at all, and true again
    // once the flag has had no engagement behind it continuously for
    // DC_FLAGGED_NO_ENGAGE_GRACE_MS. False while a real fight owns the bot.
    bool MayDrive(Player* bot, AiObjectContext* context);

    // Flagged in combat with no fight behind it (past the same grace). The rest
    // gates' question: while this holds, eating and drinking are impossible and
    // nothing resolves the flag by itself, so any wait keyed on HP/mana is a
    // deadlock — and inside a damage aura, a fatal one.
    bool IsPhantomFlag(Player* bot, AiObjectContext* context);
}

#endif  // _PLAYERBOT_DCCOMBATFLAG_H
