/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_LEADER_SIGNAL_H
#define _DC_LEADER_SIGNAL_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"

#include <vector>

class Creature;
class Player;

class DcLeaderSignal
{
public:
    // --- Raid / multi-tank leadership ---------------------------------------
    // Elects the single tank that drives the clear for the whole group. A party
    // has one tank, but a raid can have several (one per sub-group); without a
    // single elected leader every tank would try to drive and each sub-group's
    // members would trail their own tank instead of one raid leader. The
    // candidate set is the alive tank BOTS on `reference`'s map (real-player
    // tanks are skipped — no PlayerbotAI to run the driving AI), and the winner
    // is chosen by group kind:
    //   * Party (5-man): the lowest-GUID candidate — a deterministic, state-free
    //     pick every member computes identically.
    //   * Raid: the candidate flagged Main Tank (MEMBER_FLAG_MAINTANK) wins
    //     outright; with no MT flagged (or the flagged member not an eligible tank
    //     bot), the candidate with the highest equipped gear score wins,
    //     GUID-tiebroken — the most survivable tank to hold raid-wide threat.
    // Either way every member computes the same result (GetFirstMember walks the
    // whole raid, not just a sub-group), so they all agree on whom to follow.
    // Returns nullptr when no tank bot is present on the map. `reference` may be
    // any group member: the issuing player, a follower, or a tank itself.
    static Player* FindLeaderTank(Player* reference);

    // True when `bot` is the elected dungeon-clear leader for its group (see
    // FindLeaderTank). Only the leader runs the driving trigger ladder and owns
    // the run's enabled/paused/progress state; every other member — non-tanks
    // AND non-leader (off-)tanks alike — follows it via the follow-tank trigger.
    static bool IsDungeonClearLeader(Player* bot);

    // The member whose DcRunState OWNS this run — DEAD OR ALIVE. FindLeaderTank
    // elects only among ALIVE tank bots, so in a 5-man with one tank it returns
    // nullptr the moment that tank dies, and every gate built on it goes inert
    // precisely when the run most needs deciding. This one falls back to scanning
    // the same-map group for the bot whose own run state is `enabled` — which is
    // only ever the tank that started the run, alive or a corpse.
    //
    // Use this (not FindLeaderTank) wherever the question is "what is this RUN's
    // state", and FindLeaderTank wherever it is "who is DRIVING right now".
    static Player* FindRunOwner(Player* bot);

    // Elects the single member that runs the TERMINAL rungs — the party-died
    // bailout and the all-cleared completion. These two are not driving decisions;
    // they are the run's own verdict on itself, and a run whose tank is a corpse
    // still has to reach one.
    //
    // The driving ladder is leader-gated (see IsDungeonClearLeader), which is right
    // for everything that moves the tank and fatal for these two: with the leader
    // dead there is no leader, so `DungeonClearPartyDiedTrigger` never consumes the
    // Disable verdict and `DungeonClearAllClearedTrigger` never fires. The run then
    // idles until the 600s no-progress watchdog kills it — 17 of 17 no_progress runs
    // in the MgT heroic 100-run audit (tp-20260805-005412-1) were exactly this, one
    // of which (run -76) had already killed all four bosses.
    //
    // Election, in order: the living elected leader (so nothing changes in the
    // healthy case), else the lowest-GUID living bot on the owner's map, else — a
    // full wipe, where nobody is alive to elect — the lowest-GUID bot on it. Every
    // member computes the same answer, so exactly one drives. Returns nullptr when
    // the run is off, paused, or has no owner.
    static Player* FindTerminalDriver(Player* bot);

    // True when `bot` is that member. The terminal triggers' replacement for the
    // driving ladder's IsEnabled gate.
    static bool IsTerminalDriver(Player* bot);

    // True when `bot` belongs to a dungeon-clear run that is currently PAUSED —
    // either it is the elected leader and its own run is paused, or it is a
    // follower whose elected leader's run is paused. Reads the leader's
    // enabled+paused flags cross-context (same pattern as DungeonClearPartyTankValue).
    //
    // Unlike the "dungeon clear party tank" value — which deliberately resolves
    // to null while paused so followers STOP trailing the leader and revert to
    // the player — this stays true through a pause. The loot-floor filter (see
    // DungeonClearFilterLootTrigger) uses it so DC's loot policy keeps applying
    // to the WHOLE party while paused: without it, paused followers fall back to
    // the stock playerbots loot pipeline, grab below-floor junk, and keep
    // IsAnyPartyMemberLooting true — which stalls the tank.
    static bool IsInPausedDungeonClearRun(Player* bot);

    // True when `bot`'s elected leader is mid DropInHole — gliding off the ledge
    // and falling down a narrow one-way hole the followers CANNOT path (Wailing
    // Caverns' return-fall off Verdan's shelf). A follower's MoveFollow toward the
    // now-far-below tank can't find a navmesh route and produces a degenerate path
    // that clips the follower straight down through the hole wall; while this is
    // true the follow-tank action HOLDS the follower at the ledge top instead. The
    // leader teleports the whole party to the landing once it lands (the DropInHole
    // RunStep gate), at which point this flips false and the party follows again.
    // Derived live from the leader's event progress (the active step is a
    // DropInHole and the leader has not yet landed) — no manual flag to go stale.
    // False for the leader itself and off runs. Pass any group member.
    static bool IsLeaderDroppingInHole(Player* bot);

    // The live NPC the group is currently escorting, or nullptr when no escort is
    // active. Resolves `bot`'s elected leader, confirms its active anchored-event
    // step is an EscortCreature on an enabled, unpaused run, and returns that
    // step's escortee creature found near `bot` (alive). This is the "treat the
    // escortee as a party member" hook: the heal-target machinery folds the result
    // into its candidate set so a healer heals the escortee (Old Hillsbrad's
    // Thrall, Wailing Caverns' Disciple, any future escort) exactly as it heals a
    // teammate — reusing the whole stock heal rotation. Generic (keyed off the
    // active EscortCreature step, not a hardcoded entry); pass any group member.
    // Returns nullptr for off/paused runs, no active escort, or the escortee not
    // being in range of `bot`.
    static Creature* GetLeaderEscortee(Player* bot);

    // --- Advanced pulls -----------------------------------------------------
    // True for the "holding" pull phases (Forming/Advancing/Returning) during
    // which the party stays passive and camped; false for Idle/Engage.
    static bool IsPullPhaseHolding(uint32 phase);

    // Resolves `bot`'s elected leader and, if that leader has advanced-pull mode
    // on and is in a non-Idle phase, writes the leader's pull phase and camp
    // position out and returns true. Returns false (outputs untouched) when there
    // is no leader, the run is off/paused, pull mode is off, or the phase is
    // Idle. Reads the leader's context cross-bot (same pattern as
    // IsInPausedDungeonClearRun); pass any group member.
    static bool GetLeaderPullInfo(Player* bot, uint32& phaseOut, Position& campOut);

    // True when `bot` is a non-leader follower whose elected leader is running
    // advanced-pull mode with a camp marked — in which case, in pull mode, the
    // party HOLDS at the camp and leapfrogs camp-to-camp instead of following the
    // tank (which would trail it forward into every pull). Writes the leader's
    // current camp to `campOut` and whether the party must be PASSIVE right now to
    // `passiveOut` (true only during the holding pull phases Forming/Advancing/
    // Returning; outside those the party holds at camp but stays ready to defend).
    // Returns false (outputs untouched) when there is no leader, the run is
    // off/paused, pull mode is off, `bot` is the leader, or no camp is marked yet.
    // Unlike GetLeaderPullInfo (which is true only mid-maneuver, for the passive
    // teardown), this is true throughout pull mode so the party never follows.
    static bool GetLeaderCampHold(Player* bot, Position& campOut, bool& passiveOut);

    // True when `bot` is a non-leader follower whose elected leader tank is in
    // the advanced-pull camp fight RIGHT NOW: pull phase Engage (the pack has
    // been dragged back and handed to stock combat) AND the leader is actually in
    // combat. This is the window in which a released follower must pile into the
    // pack even if the drag parked it out of the camp's line of sight (around a
    // corner) — where the stock LOS-gated target picker never acquires a target
    // and the party would otherwise stand idle and never enter the fight. Drives
    // DungeonClearAssistCamp{,Combat}Trigger. Returns false (the common case) for
    // the leader, outside pull mode, or when the leader isn't mid camp-fight.
    static bool IsLeaderCampFightActive(Player* bot);

    // True while `bot`'s leader is fighting a SCRIPTED PULL's camp fight
    // (ScriptedPullRegistry stage in flight, phase Engage). The follower half of
    // the tank's camp leash: an ordinary camp fight releases the party to stock
    // combat, which is right when the pack is already on the tank — and wrong for
    // a scripted pull, where the tag is taken at range so the pack arrives late
    // and the rest of it is still standing in a room the party must not enter.
    // Followers stay ANCHORED (not passive — they fight what comes to them) for
    // the duration. Drives DungeonClearHoldAtCampCombatTrigger and the camp-hold
    // action's leash radius / movement priority.
    static bool IsLeaderScriptedCampFight(Player* bot);

    // True while `bot`'s leader has ANY scripted-pull stage in flight — the tag leg
    // and the drag as well as the camp fight. Wider than IsLeaderScriptedCampFight
    // on purpose: the party must be kept out of the room for the WHOLE maneuver,
    // not only once the fight lands, and the earlier phases are exactly when the
    // room is still full of the pack that has not been pulled.
    static bool IsLeaderScriptedPullActive(Player* bot);

    // The GENERAL "tank is fighting -> the party assists" gate: true when `bot`'s
    // elected leader tank is in combat on an active (enabled, unpaused) run and
    // the party is expected to pile in RIGHT NOW. Includes the advanced-pull camp
    // fight (IsLeaderCampFightActive), but ALSO every fight the camp machinery
    // does not own: pull mode off, a Leeroy verdict in dynamic mode, boss
    // walk-ins, and unplanned aggro outside a camp hold. This is what covers a
    // tank that aggros around a corner or beyond a follower's natural engage
    // range — group combat never propagates that far, and DC's multiplier mutes
    // the stock proactive pickers, so without this push the party stands idle
    // while the tank solos. Defers (false) while an advanced-pull camp hold is
    // in effect outside the camp fight: the holding phases pin the party passive
    // and an Idle-phase aggro is dragged back to camp first. False for the
    // leader itself, off/paused runs, or a leader out of combat. Drives
    // DungeonClearAssistCamp{,Combat}Trigger.
    static bool IsLeaderFightAssistWanted(Player* bot);

    // The MIRROR of IsLeaderFightAssistWanted for the LEADER itself: true when
    // `bot` IS the elected leader tank, is OUT of combat on an active (enabled,
    // unpaused) run, has no engage target of its own in sight, yet a groupmate is
    // (latched) in combat. This is the case the followers' assist gate can't cover
    // because it bails for the leader: the tank declared the pull done and started
    // advancing — or a follower aggroed a pack around a sharp corner the tank
    // never saw — so the tank stands frozen on the Advance rest gate ("party not
    // ready / resting") while the DPS fight without it. Drives the tank back onto
    // the party's fight to take threat. Defers (false) while a pull maneuver is
    // holding the party at camp (the drag owns the tank's positioning) and while
    // the tank already has its own visible attacker (its engage scan handles
    // that). Uses the same PartyCombatLatch hysteresis as the followers' gate so
    // both sides ride out the combat-flicker TOCTOU consistently. Drives
    // DungeonClearLeaderAssistTrigger.
    static bool IsLeaderShouldAssistFight(Player* bot);

    // True when `bot`'s elected leader is running DYNAMIC pull (pull setting == 2)
    // and is still scouting/deciding the next pack — i.e. out of combat with the
    // pull phase Idle, before it has committed to a Leeroy or an Advanced camp.
    // This is the window in which the party must hang BACK so it doesn't trail the
    // tank into an accidental aggro before the verdict is in; DungeonClearFollow
    // TankAction widens its follow distance (PullDynamicPartyLag) while it holds.
    // The instant the tank commits (enters combat, or an Advanced camp is marked),
    // this returns false and the party reverts to the tight follow / camp hold.
    // Returns false for the leader itself, outside dynamic mode, or off/paused.
    static bool IsLeaderDynamicScouting(Player* bot);

    // Point `lag` yards back along the LEADER tank's breadcrumb trail (the ground
    // the tank actually walked, which the escort spline already corridor-centered),
    // for a follower to trail to during dynamic scouting. Walking the leader's
    // trail keeps followers on the centered route instead of bee-lining a geometric
    // lag point through the raw PathGenerator, which hugs walls and ledges. Reads
    // the LEADER's crumbs cross-bot (only the tank records them) and only returns a
    // crumb `bot` can reach over a complete generated path. False if there is no
    // leader, the trail is empty, or no reachable point lies far enough back.
    static bool GetLeaderScoutTrailPoint(Player* bot, float lag, Position& out);

    // Multi-point variant of GetLeaderScoutTrailPoint: the centered breadcrumb
    // POLYLINE a follower should glide to catch up to the tank, rather than the
    // single crumb the point variant returns. `out` is filled with the spline
    // window [follower live pos, ...intermediate crumbs..., the crumb `lag` yards
    // behind the tank] in forward (toward-tank) order, ready to hand to
    // DcMovement::SplinePath. This lets followers ride ONE continuous escort
    // spline along the centered trail instead of re-issuing a per-tick single-
    // point MoveTo to each crumb — the per-crumb relaunch made followers visibly
    // half-step (accel/decel at every waypoint), the same stutter the tank's
    // advance shed when it moved to MoveSplinePath. The window only spans from
    // the follower up to the lag crumb (never up to the tank), so the glide
    // preserves the lag instead of closing it. Reachability of the entry leg is
    // probed (one PathGenerator build, as the point variant does); the rest of
    // the window is the tank's own contiguous walked ground. Returns false (out
    // cleared) when there is no leader, the trail is empty, the follower is at or
    // ahead of the lag crumb (nothing to glide — caller falls back to the point
    // step / Follow fan), the follower is too far off-trail for a safe entry leg,
    // or fewer than two window points result.
    static bool GetLeaderScoutTrail(Player* bot, float lag, std::vector<Position>& out);

    // --- Room-aggro clear ----------------------------------------------------
    // True when `bot`'s elected leader tank is mid room-aggro clear (a flagged
    // RoomAggroRegistry boss with room trash still to clear, the tank at the boss
    // room) — the window in which the tank skirts the boss's aggro sphere on its
    // own approach (DungeonClearEngageActionBase::RoomAggroSkirtPoint). Writes the
    // LIVE boss centre to `centerOut` and the avoid-sphere radius (the boss's real
    // aggro range for THIS follower + both reaches + AggroRangeMargin +
    // RoomAggroPathPadding — the same sizing the tank's skirt uses) to `radiusOut`.
    // Followers read this so their close-follow to the tank can detour AROUND the
    // sphere instead of cutting a straight line through it: the tank dodges the
    // boss correctly, but a follower bee-lining to a tank parked on the far side of
    // the sphere would otherwise run the party into aggro and wake the room. Reads
    // the leader's context cross-bot (same pattern as IsLeaderDynamicScouting);
    // pass any group member. Returns false (outputs untouched) for the leader
    // itself, off/paused runs, when no room clear is active, or when the boss isn't
    // loaded.
    static bool GetLeaderRoomAggroSphere(Player* bot, Position& centerOut,
                                         float& radiusOut);

    // Force the leader of `bot`'s group to abandon the current pull and release
    // the party (sets the leader's pull phase to Engage). Used by the CC-abort
    // path when the tank is control-locked mid-drag and the pull is genuinely
    // failing. No-op if there is no leader or it isn't mid-pull.
    static void AbortLeaderPull(Player* bot);

    // Release the party from their passive camp hold WITHOUT tearing down the
    // leader's pull. The tank keeps dragging to camp; the followers stop being
    // punching bags and fight back from the camp the drag is headed to. Used by
    // the camp-safety valve, which wants "let them fight" and not "abandon the
    // maneuver" — the abandon variant lands the whole party in the middle of the
    // room the drag existed to leave. Per leader phase (DcPullContext::
    // SafetyRelease): Forming/Returning keep the pull and only release the party;
    // Advancing aborts as AbortLeaderPull did (the tank is still walking TOWARD
    // the pack — a drag-back from a pull that never tagged is meaningless);
    // Idle/Engage no-op.
    static void ReleaseLeaderPullHold(Player* bot);

    // True while the leader's current pull carries a standing camp-safety release
    // (DcPullContext::partyReleased): the maneuver is still in flight but the
    // party must NOT be passive, and any DC passive still applied is stripped at
    // once (no graceful release delay). False when there is no leader / no pull.
    static bool IsLeaderPullHoldReleased(Player* bot);

    // The pack the leader's current pull tagged (DcPullContext::pullTarget), or
    // an empty guid when there is no leader / no pull / nothing tagged yet. The
    // camp-safety valve's attacker attribution reads this to decide whether what
    // is hitting a held follower is the dragged pack (ordinary splash) or a
    // second pack (the maneuver is compromised).
    static ObjectGuid GetLeaderPullTarget(Player* bot);

    // Grant (or revoke) the leader tank immunity to the Daze mechanic for the
    // duration of an advanced-pull session. A creature hitting a moving target
    // from behind has up to a 40% chance to Daze it (spell 1604, -50% move
    // speed) — which cripples the pull-to-camp drag-back exactly when the tank
    // most needs to retreat (it is running AWAY from the pack, so every hit
    // lands from behind). We "cheat a little" per design and make the driving
    // tank daze-proof while pull mode is on. Idempotent; also strips any Daze
    // aura already on the tank when applied. Paired with the pull-mode toggle.
    static void SetLeaderDazeImmunity(Player* leader, bool apply);

};

#endif  // _DC_LEADER_SIGNAL_H
