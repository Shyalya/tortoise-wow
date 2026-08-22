/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARTRIGGERS_H
#define _PLAYERBOT_DUNGEONCLEARTRIGGERS_H

#include "Trigger.h"

#include "Ai/Dungeon/DungeonClear/Util/DcProgressWatchdog.h"

#include <cstdint>

class PlayerbotAI;

class DungeonClearIdleTrigger : public Trigger
{
public:
    DungeonClearIdleTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear idle", 1) {}
    bool IsActive() override;
};

class DungeonClearAtBossTrigger : public Trigger
{
public:
    DungeonClearAtBossTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear at boss", 1) {}
    bool IsActive() override;
};

// Fires when the next anchor is a travel OBJECTIVE (DungeonAnchorKind::Objective,
// injected by BossRosterRegistry — e.g. a Sunken Temple event waypoint) and the
// tank has arrived within its arriveRadius (or its gateEntry creature is alive).
// Drives DcObjectiveArriveAction, which runs an optional on-arrival hook and
// then marks the objective cleared so the clear advances. Objectives never reach
// the combat/engage triggers (those stand down for non-Boss anchors).
class DungeonClearAtObjectiveTrigger : public Trigger
{
public:
    DungeonClearAtObjectiveTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear at objective", 1) {}
    bool IsActive() override;
};

// Leader-only, non-combat. Fires when an off-path CONDITIONAL event
// (DungeonEventRegistry, EventActivation::Conditional) for this map is DUE — its
// bound activation predicate (DungeonEvent::condition) is true and it has not yet latched. Drives
// DcRunEventAction, which runs the event's steps (walk to a lever/NPC, gossip,
// wait for the gate to open). Relevance 31 — just above at-boss (30) — so a due
// pre-boss gate (e.g. "free the prisoner to open the courtyard door") preempts
// the boss pull and the door-blocked stall. Inert when no conditional event is
// due. See DungeonEventExecutor::FindDueConditionalEvent.
class DungeonClearEventDueTrigger : public Trigger
{
public:
    DungeonClearEventDueTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear event due", 1) {}
    bool IsActive() override;
};

// COMBAT-engine sibling of DungeonClearEventDueTrigger. Fires only while the
// leader IS in combat and the due conditional event is flagged
// DungeonEvent::drivesInCombat — a continuous WAVE encounter whose event IS the
// fight, so the driver must keep steering while the party is engaged.
//
// The non-combat copy stands down on bot->IsInCombat(), which is correct
// everywhere the event's work happens BETWEEN pulls. It is fatal where the party
// is in combat from the first pull to the last: the driver then runs only in the
// shrinking gaps between waves and stops running entirely once the party falls
// behind and combat stops dropping. Black Morass with two rifts open was exactly
// that — nothing ever walked the tank to a portal to kill the rift keeper, so the
// rifts never closed. See DungeonEvent::drivesInCombat.
class DungeonClearEventDueCombatTrigger : public Trigger
{
public:
    DungeonClearEventDueCombatTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "dungeon clear event due combat", 1)
    {
    }
    bool IsActive() override;
};

class DungeonClearBlockingTrashTrigger : public Trigger
{
public:
    DungeonClearBlockingTrashTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear blocking trash", 1) {}
    bool IsActive() override;
};

// Fires at a room-wide-aggro boss (RoomAggroRegistry) while room trash remains
// AND the player's chosen pull type is NOT pull-to-camp for this pack (pull mode
// current is false: Off, or Dynamic chose Leeroy). Drives the Leeroy room-clear
// engage. When pull-to-camp IS in effect the higher-priority pull pipeline owns
// the room clear instead, so this stands down. Either way the boss pull stays
// gated until the room is clear. See DcTargeting::IsRoomClearActive.
class DungeonClearRoomTrashTrigger : public Trigger
{
public:
    DungeonClearRoomTrashTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear room trash", 1) {}
    bool IsActive() override;
};

// Room pre-clear OWNER (fix #2). Active for the WHOLE pre-clear window — a flagged
// room-aggro boss with trash still up and the tank arrived at the standoff (same
// window as DcTargeting::IsRoomClearActive). Registered just ABOVE the default
// Advance (rel 16 vs 15) so that whenever no higher driver (pull/event/room-clear/
// engage) claims the tick, this HOLDS the tank at the standoff instead of letting
// the room-aggro-blind Advance creep at the boss centre. Closes the structural gap
// behind the recurring "boss woken mid-clear" failures: the standoff is now owned
// every tick, not just when the conditional Advance hold rung happens to fire.
class DungeonClearRoomPreClearHoldTrigger : public Trigger
{
public:
    DungeonClearRoomPreClearHoldTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear room preclear hold", 1) {}
    bool IsActive() override;
};

// Fires the disable-on-death bailout when a same-map party member is dead AND
// post-combat rez recovery is NOT viable (full wipe, no living rez class,
// recovery timed out, or DungeonClear.PostCombatRez off). While recovery IS
// viable the trigger stays silent — the rez-party rung below drives the
// resurrection and the readiness gates hold the run. See DcRezRecovery.
class DungeonClearPartyDiedTrigger : public Trigger
{
public:
    DungeonClearPartyDiedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear party died", 1) {}
    bool IsActive() override;
};

// ALL bots (leader AND followers), non-combat. Fires when post-combat rez
// recovery is in progress and THIS bot is the elected rezzer (deterministic
// from group order: first living rez-class bot, healers first — every member
// computes the same answer, so exactly one bot fires). Drives
// DungeonClearRezPartyAction: walk to the corpse, cast the class rez. Inert
// for off/paused runs, with no deaths, when a stock class-strategy rez already
// raised everyone, or when only the human can rez (the hold + prompt path).
class DungeonClearRezPartyTrigger : public Trigger
{
public:
    DungeonClearRezPartyTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear rez party", 1) {}
    bool IsActive() override;
};

class DungeonClearAllClearedTrigger : public Trigger
{
public:
    DungeonClearAllClearedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear all cleared", 1) {}
    bool IsActive() override;
};

// Stranded-member recovery failsafe. Fires ONLY on the leader tank when the run
// has shown no progress for DungeonClear.StrandedRecoveryNoProgressSecs while a
// bot party member is stuck out of range (fell under the world / wedged in
// geometry). DcStrandedRecovery::Evaluate owns the no-progress clock (this is its
// sole update site) and returns true only when a teleport should fire this tick;
// the paired DungeonClearRecoverStrandedAction relocates the strays to the tank.
class DungeonClearRecoverStrandedTrigger : public Trigger
{
public:
    DungeonClearRecoverStrandedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear recover stranded", 1) {}
    bool IsActive() override;
};

// --- Sunken Temple (map 109) Avatar of Hakkar encounter ------------------
// Both fire ONLY in the Sanctum of the Fallen while the encounter is live (the
// Shade 8440 / a Suppressor 8497 is up) — inert on every other map and run.

// A Nightmare Suppressor (8497) is alive nearby. Drives the suppressor-aggro
// action (top relevance) so the channel that would reset the event is silenced.
class DungeonClearHakkarSuppressorTrigger : public Trigger
{
public:
    DungeonClearHakkarSuppressorTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear hakkar suppressor", 1) {}
    bool IsActive() override;
};

// This bot carries Hakkari Blood (10460) AND an un-doused Eternal Flame remains.
// Drives the flame-douse action for the blood carrier (any member).
class DungeonClearHakkarFlameTrigger : public Trigger
{
public:
    DungeonClearHakkarFlameTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear hakkar flame", 1) {}
    bool IsActive() override;
};

// A freshly-killed Bloodkeeper (8438) corpse this party hasn't yet looted is
// nearby. Drives the in-combat blood looter so the flame douse has its key item.
class DungeonClearHakkarLootBloodTrigger : public Trigger
{
public:
    DungeonClearHakkarLootBloodTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear hakkar loot blood", 1) {}
    bool IsActive() override;
};

// Fires only while DC is enabled AND the advance/engage path has set a stall
// reason. Drives the fallback "kill anything reachable" action.
class DungeonClearStalledTrigger : public Trigger
{
public:
    DungeonClearStalledTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear stalled", 1) {}
    bool IsActive() override;
};

// Fires on non-tank party bots when a tank in their party has DC enabled and
// the bot is too far from that tank. Redirects follow from the player master
// to the tank for the duration of the clear.
class DungeonClearFollowTankTrigger : public Trigger
{
public:
    DungeonClearFollowTankTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear follow tank", 1) {}
    bool IsActive() override;
};

// Fires when the cached long-path corridor crosses a closed
// `GAMEOBJECT_TYPE_DOOR`. The bot stops advancing and stalls with a
// specific reason in party chat so the human player can open the door.
class DungeonClearDoorBlockedTrigger : public Trigger
{
public:
    DungeonClearDoorBlockedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear door blocked", 1) {}
    bool IsActive() override;
};

// Fires only while the run is PAUSED for a door the tank couldn't open
// (DungeonClearDoorBlockedAction stashed its GUID in "dungeon clear paused
// door"). Returns true the moment that specific door reads OPEN — a human
// walked up and opened it — or it despawns/unresolves, so the tank can
// auto-resume the route without the player also hitting Resume. Inert for a
// manual `dc pause` (which leaves the paused-door GUID empty).
class DungeonClearDoorReopenedTrigger : public Trigger
{
public:
    DungeonClearDoorReopenedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear door reopened", 1) {}
    bool IsActive() override;
};

// Rest-target triggers. Fire on every bot in an active DC run (tank AND
// followers) while out of combat and below the run's chosen rest target
// (DungeonClear.RestManaPct / RestHealthPct). They drive the stock playerbots
// "drink" / "food" actions so bots top up to the group's target even when it is
// above mod-playerbots' own stop thresholds; DungeonClearMultiplier caps the
// other side so a target below the stock stop is honoured too. Inert when the
// target is 0 (inherit the playerbots value).
class DungeonClearNeedsDrinkTrigger : public Trigger
{
public:
    DungeonClearNeedsDrinkTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear needs drink", 1) {}
    bool IsActive() override;
};

class DungeonClearNeedsEatTrigger : public Trigger
{
public:
    DungeonClearNeedsEatTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear needs eat", 1) {}
    bool IsActive() override;
};

// Fires on EVERY member of a DC run that is PAUSED — the leader AND its
// followers (see DcLeaderSignal::IsInPausedDungeonClearRun). While paused the
// driving ladder goes inert and "dungeon clear party tank" goes null, so the
// loot-floor filter that normally runs inline in the advance (leader) and
// follow-tank (follower) actions never gets a tick: the party reverts to the
// stock playerbots loot pipeline and loots everything, ignoring the DC loot
// policy (DungeonClear.LootMinQuality / IgnoreChests). Followers grabbing
// below-floor junk also keep IsAnyPartyMemberLooting true, which stalls the
// tank. This trigger fills that gap: it keeps the same filter running every
// non-combat tick while paused, so the DC loot settings stay in force for the
// whole party exactly as they do during an active run.
class DungeonClearFilterLootTrigger : public Trigger
{
public:
    DungeonClearFilterLootTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear filter loot", 1) {}
    bool IsActive() override;
};

// Fires while this bot has an un-answered group loot roll (its vote is still
// NOT_EMITED_YET). Stock playerbots only reaches the "loot roll" action off
// the "very often" RandomTrigger — a 1-in-3 coin flipped at most once per
// AiPlayerbot.RepeatDelay (2s default), so bots routinely sit on an open roll
// window for many seconds. This trigger drives the same "loot roll" action
// (the BetterLootRollAction override) every non-combat tick until the vote is
// cast, so bots roll as soon as the window opens. Gated by
// DungeonClear.BetterLootRolling; inert for self-bots, where the human owns
// the roll (improvement #1 — see BetterLootRollAction.h).
class DungeonClearLootRollPendingTrigger : public Trigger
{
public:
    DungeonClearLootRollPendingTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "dungeon clear loot roll pending", 1)
    {
    }
    bool IsActive() override;
};

// --- Advanced pulls -------------------------------------------------------
// Leader-only, non-combat. Fires when advanced-pull mode is on and either a pull
// is already mid-flight (phase Forming/Advancing, or a post-fight Engage cleanup
// while out of combat) or a fresh, pullable trash pack is in range and the party
// is ready. Drives DungeonClearPullAction, which marks the camp, runs the tank
// in to grab aggro, and (on the combat engine) drags the pack back. Sits above
// engage-trash so the pull preempts the normal walk-in; trash-only — never
// preempts the at-boss engage.
//
// Two things widen the "mode is on" gate, both because the decision to pull was not
// the mode's to make: a BossPullbackRegistry drag (`bossPullback`), and a
// ScriptedPullRegistry stage already IN FLIGHT. The second is a cleanup guarantee,
// not a licence — a stage's mode is forced on only while its row is still DUE, so it
// can drop the moment the pack dies, taking the Engage cleanup that unlatches the
// stage down with it. Scoped to phase != Idle so it can never open the start path.
class DungeonClearPullTrigger : public Trigger
{
public:
    DungeonClearPullTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear pull", 1) {}
    bool IsActive() override;
};

// Leader-only, COMBAT engine. Fires once the tank is in combat during a pull
// (phase Advancing or Returning) so DungeonClearPullManeuverAction can run the
// tank back to camp instead of letting stock combat chase/fight at the pack.
// Inert at phase Engage (tank is back at camp; stock combat takes the fight).
class DungeonClearPullManeuverTrigger : public Trigger
{
public:
    DungeonClearPullManeuverTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear pull maneuver", 1) {}
    bool IsActive() override;
};

// Follower-only, non-combat. Fires while this bot's leader is in a holding pull
// phase (Forming/Advancing/Returning). Drives DungeonClearHoldAtCampAction,
// which puts the bot passive and parks it at the camp instead of trailing the
// tank into the pull. Outranks follow-tank so the party stays put while the tank
// pulls. (Passive removal is handled centrally by ReapStrandedPassives.)
class DungeonClearHoldAtCampTrigger : public Trigger
{
public:
    DungeonClearHoldAtCampTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear hold at camp", 1) {}
    bool IsActive() override;
};

// Follower-only, COMBAT engine. The combat-side twin of the trigger above: fires
// while this bot's leader is in a holding pull phase AND this bot is IN combat.
// A held follower is dragged into combat the moment the tank aggros (group
// combat), switching it to the combat engine where the non-combat hold can't
// run; without a combat-side hold the follower then runs stock follow (which
// PassiveMultiplier permits even while passive) and trails the tank. Drives
// DungeonClearStayAtCampAction. Inert at Engage so the released party fights.
class DungeonClearHoldAtCampCombatTrigger : public Trigger
{
public:
    DungeonClearHoldAtCampCombatTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "dungeon clear stay at camp", 1)
    {
    }
    bool IsActive() override;
};

// Follower-only, non-combat. Fires while this bot's leader tank is in the
// advanced-pull camp fight (phase Engage, leader in combat) and this bot is not
// yet in combat — REGARDLESS of line of sight. The drag-back can park the pack
// out of the camp's line of sight, but an idle follower can't self-engage even
// WITH sight: DC's multiplier suppresses the stock proactive-engagement pickers
// for every follower while a clear is active, so the party stands idle and the
// camp never enters combat. Drives DungeonClearAssistCampAction, which
// force-targets the pack and forces the bot into combat — flipping it into the
// combat engine where its own rotation/heal logic (un-suppressed there) runs.
// Outranks hold-at-camp so it preempts the camp yield. Goes inert the instant the
// bot is in combat (the combat-engine twin below takes any out-of-LOS handoff).
// See DcLeaderSignal::IsLeaderCampFightActive.
class DungeonClearAssistCampTrigger : public Trigger
{
public:
    DungeonClearAssistCampTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "dungeon clear assist camp", 1)
    {
    }
    bool IsActive() override;
};

// Follower-only, COMBAT engine. Combat-side twin of the trigger above: the same
// "close on the out-of-LOS camp fight" assist for when the follower is already
// IN combat (dragged in by group combat / a stray hit) but, with the pack around
// a corner, has an empty LOS attacker list and so stands idle in the combat
// engine. Drives DungeonClearAssistCampCombatAction. Inert the instant a valid
// attacker comes into sight, handing the fight back to stock combat.
class DungeonClearAssistCampCombatTrigger : public Trigger
{
public:
    DungeonClearAssistCampCombatTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "dungeon clear assist camp combat", 1)
    {
    }
    bool IsActive() override;
};

// LEADER-only, non-combat engine. The mirror of the follower assist above for the
// tank itself: a groupmate is fighting a pack the tank never saw (a follower
// aggroed around a sharp corner, or the tank called the pull done and walked off),
// so the tank stands frozen on the Advance rest gate instead of rejoining. Drives
// DungeonClearLeaderAssistAction, which moves the tank onto the party's fight and
// forces it into combat so it takes threat. Goes inert the instant the tank sees a
// target of its own (its engage scan owns it) or the party drops combat. See
// DcLeaderSignal::IsLeaderShouldAssistFight.
class DungeonClearLeaderAssistTrigger : public Trigger
{
public:
    DungeonClearLeaderAssistTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "dungeon clear leader assist", 1)
    {
    }
    bool IsActive() override;
};

// ANY DC party member (leader OR follower), BOTH engines. Phantom-combat escape
// hatch. A member can be left FLAGGED in combat by a mob that spawned far across the
// map or behind a gate (a proximity/gate event spawn) and tagged it: the core combat
// reference never drops because the holder is unreachable, and DC's own gates that
// key off "someone is in combat" (the fight-assist arm, the party-engaged latch)
// then spin forever — a hard deadlock a `dc off`/`on` cannot clear because the flag
// lives in the core CombatManager, not in DC. This trigger fires only when the bot is
// in combat but nothing is fightable — nothing meleeing it, no victim, and EVERY unit
// holding it in combat is unreachable-by-path, evading, forbidden by its own AI from
// attacking us, or REACHABLE BUT NOT COMING — sustained for
// DungeonClear.StuckCombatTimeout seconds (long by default so a scripted encounter
// that intentionally holds combat is never mistaken for a stuck flag). Keying on
// REACHABILITY (not distance) is the safety property: a fleeing or kiting party's
// pursuers are always path-reachable, so it can never fire there; a combat forced by
// a script with no unit reference is likewise never touched.
//
// The "reachable but not coming" arm is the S1487 addition. In an instance a creature
// never leashes (the dungeon short-circuit in CanCreatureAttack), so a mob that tagged
// the party and then stopped keeps its combat reference alive from wherever it stands
// — alive, non-evading, path-reachable, allowed to attack, and completely inert. That
// read as a real fight, so this hatch stood down while every DC gate keyed off "someone
// is in combat" spun. It is bounded by CLOSING DISTANCE, not by distance or time alone:
// a holder inside DC_ENGAGE_RANGE is a fight whatever the numbers say, and one that is
// improving its closest-ever distance to us is chasing. Only a holder that is far AND
// has stopped closing counts as stale. Drives
// DungeonClearBreakStuckCombatAction, which force-clears combat + threat (the same
// effect as a GM `.combatstop`). Inert the instant anything becomes fightable, outside
// a live/unpaused DC run, and — deliberately — in any RAID zone, where an errant
// combat drop could reset a boss for the whole raid (the deadlock this recovers is a
// 5-man problem). Verdict is the pure DungeonClearMath::IsPhantomCombat +
// ShouldBreakStuckCombat kernels.
//
// Registered in BOTH engines, and the NON-combat registration is the load-bearing
// one. Engine transitions are action-driven, not derived from IsInCombat: stock
// `drop target` (relevance 99) moves a bot to BOT_STATE_NON_COMBAT without clearing
// the core flag, and nothing moves it back while a DC run suppresses the stock
// attack/pull actions that call ChangeEngine(BOT_STATE_COMBAT). A bot in that state
// is flagged with every non-combat rung bailing on IsInCombat() and every combat rung
// out of reach — which, while this hatch was combat-only, included the hatch itself.
// See DungeonClearStrategy for the live case.
class DungeonClearBreakStuckCombatTrigger : public Trigger
{
public:
    DungeonClearBreakStuckCombatTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "dungeon clear break stuck combat", 1)
    {
    }
    bool IsActive() override;

private:
    // Streak clock owned per-bot (the context creates one trigger object per bot):
    // getMSTime the phantom-combat state was first observed this streak, 0 = not
    // streaking. Reset to 0 the instant anything becomes fightable. The action reads
    // nothing from here — the force-clear is stateless.
    std::uint32_t stuckCombatSinceMs = 0;

    // Closing-distance tracker over the NEAREST legitimate combat holder. Reachability
    // proves a holder COULD come; this proves whether it IS coming. Re-armed only when
    // there is nothing to measure (out of combat, a real fight, no legitimate holder),
    // never on a tick where the holder merely happened to close — see IsActive.
    DcProgressWatchdog holderCloseWatch;
};

// LEADER-only, COMBAT engine. The combat-side rung of the KillCreature-engage
// objective (Shattered Halls' stealthed Shattered Hand Assassins). A stealthed
// mob can Sap the tank: the sap flags the party into combat AND the assassin stays
// stealthed, so once the incapacitate wears off stock combat has no detectable
// victim and the run wedges "in combat, nothing to hit". The non-combat objective
// driver (DcObjectiveArriveAction's engage branch) cannot run then — combat owns
// the engine — so this fires it from the combat side. Active only when: DC leader
// on a live/unpaused run, in combat, an active KillCreature-ENGAGE objective step
// (DungeonEventExecutor::ActiveEngageStep), AND a live creature of that step's
// entry sits nearby, REACHABLE, but this bot canNOT see/detect it — the exact
// stealthed-sapper deadlock signature. Drives DcObjectiveEngageCombatAction, which
// walks the tank onto the assassin by ENTRY and Attacks it (breaking stealth on the
// first swing). Inert the instant the target becomes detectable, handing the kill
// back to stock combat.
class DungeonClearObjectiveEngageCombatTrigger : public Trigger
{
public:
    DungeonClearObjectiveEngageCombatTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "dungeon clear objective engage combat", 1)
    {
    }
    bool IsActive() override;
};

// Follower-only, COMBAT engine. A CONTRIBUTION-GATED reconnector (Option B): it no
// longer tethers on distance. It fires only when a follower in combat has no useful
// work it can do from where it stands — a DPS with an empty LOS attacker list, or a
// healer parked where it could not heal the tank when damage starts (nobody hurt
// yet) — and drives DungeonClearRegroupCombatAction to a role-correct standoff point
// with LOS on the fight, never onto the tank's cell. The verdict is the pure
// DcRegroupDecision::DecideCombatRegroup kernel; this trigger gathers the game-state
// reads, then layers debounce (predicate must hold DC_REGROUP_DEBOUNCE_MS before
// firing) + a latch (keep firing until it clears) + a post-fire cooldown
// (DungeonClear.CombatRegroupCooldown) so it cannot flap on an LOS flicker.
// DungeonClear.CombatRegroupDistance survives as a HARD OUTER TETHER: beyond it the
// bot reconnects regardless of the contribution test, bypassing debounce/cooldown
// (the drifted-into-nowhere emergency path). The hurt-heal-target case is owned by
// DungeonClearHealRepositionTrigger (rel 41); this stands down whenever it holds.
// Deliberately INERT while the party is held passive at an advanced-pull camp
// (GetLeaderCampHold passive) — the camp/assist actions own positioning there.
// Gated by DungeonClear.CombatRegroup.
class DungeonClearRegroupCombatTrigger : public Trigger
{
public:
    DungeonClearRegroupCombatTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "dungeon clear regroup combat", 1)
    {
    }
    bool IsActive() override;

private:
    // Flap control. Per-bot (the context creates one trigger object per bot); the
    // action never reads these — the move is stateless and re-derives everything.
    uint32 _pendingSince  = 0;  // first getMSTime the contribution predicate held (0 = not pending)
    uint32 _cooldownUntil = 0;  // no non-emergency re-fire before this getMSTime
    bool   _latched       = false;  // currently firing: keep firing until the predicate clears
};

// Healer-only, BOTH engines. The real fix for the long-standing "healer stops
// healing once the tank is dragged out of line of sight" bug. Fires when this
// bot is a healer on an active run and the most-hurt party member (the DC
// `dungeon clear heal target` value — chosen LOS-blind, tank-biased) is below
// the heal HP floor but currently UNHEALABLE from where the bot stands (out of
// LOS or beyond heal range). Drives DungeonClearHealRepositionAction to move to
// a point with line of sight + heal range, after which the stock heal stack
// re-acquires the target. Stands down when there is in-LOS heal work the stock
// engine can already do (it defers to a visible hurt member), during advanced-
// pull passive camp holds, for the leader/tank itself, while CC'd, and when the
// target is implausibly far. Gated by DungeonClear.HealReposition.
class DungeonClearHealRepositionTrigger : public Trigger
{
public:
    DungeonClearHealRepositionTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "dungeon clear heal reposition", 1)
    {
    }
    bool IsActive() override;
};

// ANY role, BOTH engines. Fires when the bot is standing inside the pulse of an
// active-vacate DcHazardRegistry row it cannot fight. Two of those exist:
//
//   * the Arcatraz "Destroyed Sentinel" (21761) summoned at a Sentinel's corpse,
//     NOT_SELECTABLE, pulsing 15yd/1s until it despawns;
//   * Scholomance's "Cloud of Disease" (17742), the persistent area aura a dying
//     Diseased Ghoul (10495) leaves on the ground — 5yd, 350/s, 20s. There is no
//     creature at all here, only a DynamicObject, so nothing in the combat AI can
//     even see it.
//
// Drives DungeonClearHazardVacateAction to clear the pulse; once out, normal
// driving resumes and the party advances past the spot (it does NOT hold at the
// rim for the emitter's whole lifetime — see the band note in DcHazard.h). No
// combat gate (both tick after the kill, often out of combat, and the ghoul pool
// drops mid-pack-fight) and no role exemption (neither can be tanked). Inert on
// maps with no rows of either kind, while CC'd/rooted (can't move), and when
// DungeonClear.HazardVacate is off. See DcHazard::NearestVacate.
class DungeonClearHazardVacateTrigger : public Trigger
{
public:
    DungeonClearHazardVacateTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "dungeon clear hazard vacate", 1)
    {
    }
    bool IsActive() override;
};

#endif
