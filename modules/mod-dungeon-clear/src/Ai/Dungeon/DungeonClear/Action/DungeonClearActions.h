/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARACTIONS_H
#define _PLAYERBOT_DUNGEONCLEARACTIONS_H

#include <optional>

#include "MovementActions.h"
#include "Position.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearApproach.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"

class PlayerbotAI;
class Unit;
class Creature;
class Map;
struct DungeonBossInfo;
struct EventStep;
struct DungeonEventProgress;

// Thin MovementAction subclass that every movement-issuing DC action derives
// from (directly or transitively). It exists for one reason: to expose the
// DcMovement point-move funnel. MovementAction::MoveTo is protected, so a free
// function in DcMovement cannot reach it; DcMoveTo is a protected member that
// can, and it runs the arbiter's paused gate + escort-conflict teardown before
// delegating to the inherited MoveTo. Stop / SplinePath / pause-gate intents
// that do not need the protected MoveTo stay as DcMovement free functions.
class DcMovementAction : public MovementAction
{
public:
    DcMovementAction(PlayerbotAI* botAI, std::string const name) : MovementAction(botAI, name) {}

protected:
    // Arbiter-funneled point move. Refuses while the run is paused (killing the
    // queued-action race), cancels a stale escort glide that would otherwise
    // coast under the new move, then delegates to MovementAction::MoveTo. Same
    // own-the-tick semantics and argument list as the inherited MoveTo.
    bool DcMoveTo(uint32 mapId, float x, float y, float z, bool idle = false, bool react = false,
                  bool normal_only = false, bool exact_waypoint = false,
                  MovementPriority priority = MovementPriority::MOVEMENT_NORMAL, bool lessDelay = false,
                  bool backwards = false);

    // Pick a standoff point on a ring around `center`: the first candidate (from
    // DungeonClearMath::StandoffCandidates, ordered bot-side first) that snaps to
    // the navmesh (8yd), sits within `maxRadius` (2D) of the center, has VMAP line
    // of sight to it (at a +2yd eye bump), and is PATHFIND_NORMAL-reachable from the
    // bot. Writes the accepted point to (x,y,z) and returns true; false if none
    // validate. Shared by the healer LOS reposition (ring around the hurt target)
    // and the contribution-gated combat regroup (ring around the fight anchor), so
    // both park a bot in the same validated band by one implementation.
    bool FindStandoffPoint(Map* map, Position const& center, float ringRadius,
                           float maxRadius, float& x, float& y, float& z);

    // What one glide tick did, so the caller can layer its own stall/park
    // bookkeeping without the driver needing the context.
    enum class GlideOutcome
    {
        Moved,        // issued a fresh movement (jump / rejoin / spline / hop). Own the tick.
        Riding,       // an in-flight escort glide is still travelling; left alone. Own the tick.
        ReachedEnd,   // the follower cursor hit the route end — as close as the navmesh allows.
        OffPathLost,  // knocked off the line and Resnap failed; the cached path was invalidated.
        Blocked,      // movement isn't allowed this tick.
    };

    // Drive ONE tick of a continuous escort-spline glide along `path` toward its
    // end, sharing the exact wedge-detect / off-path-resnap / ride-guard /
    // jump / off-line-rejoin / spline-window / single-hop-fallback ladder that the
    // Advance approach FSM runs (DcAdvanceAction's Tier-B/C effect handlers are the
    // DecideApproach-instrumented sibling of this sequence). Pure movement: it
    // mutates `follower`/`appr`/`wedgeWatch` and issues the arbiter-funneled moves,
    // but leaves stall-reason/park bookkeeping to the caller via the returned
    // outcome. `wedgeWatch` is the caller's progress watchdog (e.g. the door
    // walk-in vs the advance route-glide instance); `appr.lastPos` carries the
    // per-tick displacement baseline. This is the shared "DcGlideDriver": any
    // action that has to walk a bot to the end of a cached route reuses it instead
    // of hand-cloning the machinery.
    GlideOutcome DriveGlideToEnd(ChunkedPathfinder::Result const& path,
                                 DungeonFollowerState& follower, DcApproachState& appr,
                                 DcProgressWatchdog& wedgeWatch, uint32 mapId, char const* tag);
};

// Shared base for engage actions: walks into attack range and forces combat
// via bot->Attack directly. We deliberately bypass the pull pipeline — its
// reach/cast handshake has a dead zone where the bot is too far for the
// pull-range check but too close for ReachCombatTo to move, leaving the tank
// standing in sight of a mob just outside aggro range.
class DungeonClearEngageActionBase : public DcMovementAction
{
public:
    DungeonClearEngageActionBase(PlayerbotAI* botAI, std::string const name) : DcMovementAction(botAI, name) {}

protected:
    // Returns true if the bot took action (moved or attacked). Caller passes
    // the target it picked; this routine handles the movement-then-attack
    // sequence regardless of whether the target is a boss, blocking trash,
    // or a stalled-fallback obstacle. Its walk-in branch automatically detours
    // around an active room-aggro boss's aggro sphere (RoomAggroSkirtPoint), so
    // every EngageDirect consumer (room clear, run-event, stalled fallback)
    // inherits the skirt without its own copy.
    bool EngageDirect(Unit* target);

    // Drive the ACTIVE objective's KillCreature-ENGAGE step (KillCreatureEngage):
    // find the nearest reachable live creature of the step's entry and EngageDirect
    // it (long-range walk-in + Attack — no visibility gate, so it breaks a stealthed
    // target's stealth on the first swing). Returns true when it owned the tick
    // (moved/attacked); false when there is no active engage step or no reachable
    // creature of its entry. Shared by the non-combat objective driver and the
    // combat-side stealth-sapper rung. See DungeonEventExecutor::ActiveEngageStep.
    bool DriveObjectiveEngage();

    // Detour waypoint to approach `target` while skirting an ACTIVE room-aggro
    // boss's aggro sphere, or nullopt when no detour is needed: the direct line
    // is already clear, no room clear is active (DcTargeting::IsRoomClearActive),
    // the boss isn't loaded, or no walkable detour snaps. Resolves the flagged
    // boss via the "next dungeon boss" value + RoomAggroRegistry and consults
    // DcEngageGeometry::AggroSafeApproachPoint with RoomAggroPathPadding honoured.
    // The single home of the skirt geometry, shared by EngageDirect's walk-in and
    // MoveToSkirtingRoomAggro so all three room-clear drivers orbit identically.
    std::optional<Position> RoomAggroSkirtPoint(Unit* target);

    // Walk toward `target`, detouring around an active room-aggro boss sphere
    // when one lies between (RoomAggroSkirtPoint) — else straight at `target`.
    // Issues the MoveTo at `prio` and returns own-the-tick semantics (true while
    // a move was issued / is in flight). For drivers that only WALK with no
    // engage handshake (the pull-idle room-clear branch); EngageDirect consumers
    // get the skirt for free via EngageDirect itself.
    bool MoveToSkirtingRoomAggro(Unit* target, MovementPriority prio);

    // Drive an EscortCreature step (Wailing Caverns' Disciple of Naralex): START
    // its scripted escort via gossip, then each tick FOLLOW the escortee and
    // ENGAGE whatever attacks it (the entire reason for the step — mod-playerbots
    // gives a bot no threat event when only a non-party escortee is hit). Self-
    // heals if the escortee dies and resets to idle; auto-stalls on prolonged
    // dead air (its own watchdog, since at-objective preempts the normal stall
    // recovery and a flat timeout would mis-fire during the long banish/ritual
    // channels). Returns true while the escort OWNS the tick; false once the
    // final boss exists, so the caller falls through to Drive and the step's
    // completion gate latches the objective. `prog` carries the watchdog clock.
    bool DriveEscortCreature(EventStep const& step, DungeonEventProgress& prog);

    // Drive a DropInHole step (Wailing Caverns' return-fall off Verdan's shelf):
    // glide the leader a few yards OUT over the open hole-mouth with a raw spline
    // (a MovePoint would clamp it back onto the shelf edge), then MoveFall() it
    // pure-vertical into the water below. Owns the tick (returns true) the whole
    // way down so the at-objective Hold can't cancel the off-mesh nudge spline;
    // returns false once the leader is on the deep floor, so the caller falls
    // through to Drive and RunStep's gate pulls the followers down + latches.
    bool DriveDropInHole(EventStep const& step);

    // Drive a UseItemOnGO step's APPROACH (Old Hillsbrad's barrels, one inside each
    // house). Owns the tick (returns true) while walking the tank to the target GO
    // via the DC movement system, because the normal at-objective StopBot(Hold)
    // cancels a plain MovePoint spline every tick — which crawls over open ground
    // but can never thread a house DOORWAY (the tank stalls at the threshold, "close
    // but not inside"). Also drives the ANCHOR approach while the pooled GO hasn't
    // streamed in yet, and closes the last yards with a FORCED-destination walk-in:
    // the navmesh thins at house walls, so a plain nav move can run dry just outside
    // cast reach and deadlock (nothing else in the movement stack forces its
    // destination). Returns false once within cast range, so the caller falls
    // through to Drive and RunStep fires the GO.
    bool DriveUseItemOnGO(EventStep const& step);

    // Recovery verdict for a long non-combat set-piece (the Old Hillsbrad barrel
    // run). The objective drive owns the tick at DcRel::AtObjective (30), which
    // sits ABOVE the NeedsRest triggers (26.5) — so without an explicit yield the
    // tank would bomb all five houses without ever drinking, sprinting between
    // barrels on empty mana. The travel-leg drivers consult this each out-of-
    // combat tick:
    //   - Yield : the TANK itself is below its rest target -> the caller returns
    //             false so its own drink/food action (rel 26.5) wins the tick.
    //   - Hold  : the tank is topped but the PARTY is still recovering -> stop in
    //             place and own the tick so followers close up and drink (never
    //             yield here, or Advance/engage-trash would drag the tank off the
    //             event when its own rest trigger is inert).
    //   - None  : nobody needs to rest -> drive on.
    // In SmartRest mode this also keeps the party-wide hysteresis latch fresh: the
    // between-pulls gate that normally drives UpdateLatch never runs inside an
    // event. Never fires mid-cast (won't interrupt an in-flight plant/gossip) or
    // in combat (the combat engine owns those ticks). See EventRestDecision.
    enum class EventRest { None, Yield, Hold };
    EventRest EventRestDecision();
};

class DungeonClearAdvanceAction : public DcMovementAction
{
public:
    DungeonClearAdvanceAction(PlayerbotAI* botAI) : DcMovementAction(botAI, "dungeon clear advance") {}
    bool Execute(Event& event) override;

private:
    // Per-tick approach state computed at the top of Execute and threaded
    // through the extracted phase steps below. The boss snapshot (effective live
    // position, the engage-range gates, the at-boss predicate) is filled before
    // the ladder; the owned approach FSM (appr), the resolved long-path + its
    // follower cursor, and the current hop are filled as Execute walks the ladder
    // and the later phases consume them. Carried as pointers/value (not refetched
    // per phase) because the single NextHop call has follower side effects, so
    // the hop cannot be recomputed phase-by-phase.
    struct AdvanceState
    {
        DungeonBossInfo const* next = nullptr;
        Creature* liveBoss = nullptr;
        float bossX = 0.0f, bossY = 0.0f, bossZ = 0.0f;
        float engageDist = 0.0f, engageRange = 0.0f;
        bool atBoss = false;

        DcApproachState* appr = nullptr;                    // owned approach FSM state
        ChunkedPathfinder::Result const* path = nullptr;    // resolved after EnsureLongPath
        DungeonFollowerState* follower = nullptr;           // long-path follower cursor
        DungeonPathFollower::Hop hop;                       // current hop (single NextHop)

        // Values carried from observation-assembly into the matching effect
        // handler so the handler need not re-derive them (and cannot drift):
        uint32 offPathTicks = 0;                 // off-path ticks at a failed Resnap (DoOffPathRebuild log)
        float  routeDeviation = 0.0f;            // 2D route deviation (DoOffLineRejoin log)
        std::vector<G3D::Vector3> splineWindow;  // the >=2-pt window (DoIssueSplineWindow)
    };

    // A phase step either handles the tick (Execute returns the carried bool)
    // or falls through to the next phase. DoPursue additionally uses Continue to
    // signal "pursuit abdicated this tick — hand off to the long-path below".
    enum class Step { Continue, ReturnTrue, ReturnFalse };

    // Pre-route phases (boss snapshot only).
    Step TryEngageHold(AdvanceState const& st);
    Step TryLootYield(AdvanceState const& st);
    Step TryBetweenPullsRest(AdvanceState const& st);
    Step TryBossNotPresentStall(AdvanceState const& st);

    // Single-observation approach tail (fable2 T2.2 / nav F10). Execute assembles
    // ONE DungeonClearApproach::Observation across three lazy stages (so the
    // action still defers the long-path build and NextHop exactly as before),
    // consults the pure DecideApproach as the sole owner of the ladder order, and
    // dispatches the verdict to the matching effect handler below. The Fill*
    // helpers gather the observation (and carry every per-tick bookkeeping side
    // effect — the stuck/pursuit counters, the off-path Resnap, the escalation
    // counter). The Do* handlers are pure effects: their guard is the verdict.
    void FillStuckObs(AdvanceState& st, DungeonClearApproach::Observation& obs);   // Tier A
    void FillPursuitObs(AdvanceState& st, DungeonClearApproach::Observation& obs); // Tier A
    void FillPathObs(AdvanceState& st, DungeonClearApproach::Observation& obs);    // Tier B
    void FillHopObs(AdvanceState& st, DungeonClearApproach::Observation& obs);     // Tier C

    Step DoStuckRecover(AdvanceState& st);
    Step DoPursue(AdvanceState& st);                 // Continue = hand off to the long-path
    Step DoLongPathUnreachable(AdvanceState& st);    // PlanRouteWait / FarFromPoly / Swim / Stall
    Step DoOffPathRebuild(AdvanceState& st);
    Step TryReanchorStaleCursor(AdvanceState& st);   // never terminates; only re-anchors the cursor
    Step DoHopDoneEscalation(AdvanceState& st, DungeonClearApproach::Verdict v);
    Step DoJumpLeg(AdvanceState& st);
    Step DoRideLiveGlide(AdvanceState& st);
    Step DoOffLineRejoin(AdvanceState& st);
    Step DoIssueSplineWindow(AdvanceState& st);
    Step DoMoveToFallback(AdvanceState& st);         // terminal: always handles the tick
};

class DungeonClearEngageTrashAction : public DungeonClearEngageActionBase
{
public:
    DungeonClearEngageTrashAction(PlayerbotAI* botAI) : DungeonClearEngageActionBase(botAI, "dungeon clear engage trash") {}
    bool Execute(Event& event) override;
};

class DungeonClearEngageBossAction : public DungeonClearEngageActionBase
{
public:
    DungeonClearEngageBossAction(PlayerbotAI* botAI) : DungeonClearEngageActionBase(botAI, "dungeon clear engage boss") {}
    bool Execute(Event& event) override;
};

// Clears a room-wide-aggro boss's room (RoomAggroRegistry) before the boss is
// pulled, for the Leeroy case (pull mode current false: Off, or Dynamic chose
// Leeroy). Walks the tank to the NEAREST remaining room-trash unit and tanks it
// in place via EngageDirect — nearest-first so the tank works the room from its
// edge inward and the boss's own aggro sphere (excluded from the remaining set)
// is approached last, never waking the boss. When pull-to-camp is in effect the
// pull pipeline owns the room clear instead (this trigger stands down). Sits at
// relevance 26 (between engage-trash 25 and engage-boss 30).
class DungeonClearRoomClearAction : public DungeonClearEngageActionBase
{
public:
    DungeonClearRoomClearAction(PlayerbotAI* botAI)
        : DungeonClearEngageActionBase(botAI, "dungeon clear room clear")
    {
    }
    bool Execute(Event& event) override;
};

// Room pre-clear OWNER (fix #2). Paired with DungeonClearRoomPreClearHoldTrigger
// at relevance 16 — just above the default Advance (15). Runs only on the ticks no
// higher driver claimed (party resting, between pulls, or no reachable pack right
// now). It HOLDS the tank at the standoff so the room-aggro-blind Advance can never
// take those ticks and creep at the boss centre. Defers (returns false) only when
// the tank has its own corpse to loot, so the loot pipeline below still runs.
class DungeonClearRoomPreClearHoldAction : public DcMovementAction
{
public:
    DungeonClearRoomPreClearHoldAction(PlayerbotAI* botAI)
        : DcMovementAction(botAI, "dungeon clear room preclear hold")
    {
    }
    bool Execute(Event& event) override;
};

// Fallback when the tank can't path to the next boss. Picks the closest
// reachable hostile anywhere on the map and pulls it; clearing obstacles
// usually unblocks the path on the next advance tick.
class DungeonClearClearStalledAction : public DungeonClearEngageActionBase
{
public:
    DungeonClearClearStalledAction(PlayerbotAI* botAI) : DungeonClearEngageActionBase(botAI, "dungeon clear clear stalled") {}
    bool Execute(Event& event) override;
};

// --- Sunken Temple (map 109) Avatar of Hakkar encounter ------------------
// Two in-combat behaviours for the Sanctum of the Fallen, gated to the live
// encounter (see the Hakkar triggers). Inert everywhere else.
//
// SUPPRESSOR: the Nightmare Suppressors (8497) channel a counter on the Shade
// that RESETS the event at 25. Their channel is an OUT-OF-COMBAT SmartAI event,
// so merely AGGROing one silences it — we just need to tag them. Top relevance
// so the party peels onto a suppressor the instant one spawns.
class DungeonClearHakkarSuppressorAction : public DungeonClearEngageActionBase
{
public:
    DungeonClearHakkarSuppressorAction(PlayerbotAI* botAI)
        : DungeonClearEngageActionBase(botAI, "dungeon clear hakkar suppressor")
    {
    }
    bool Execute(Event& event) override;
};

// FLAME: whoever is carrying Hakkari Blood (item 10460, looted from Bloodkeepers
// 8438) walks to the nearest un-doused Eternal Flame (148418-148421) and USES it
// — the flame's lock consumes the blood and bumps the Shade's douse counter. Four
// distinct flames -> the Shade summons the Avatar. Any member, not just the tank.
class DungeonClearHakkarFlameAction : public DcMovementAction
{
public:
    DungeonClearHakkarFlameAction(PlayerbotAI* botAI)
        : DcMovementAction(botAI, "dungeon clear hakkar flame")
    {
    }
    bool Execute(Event& event) override;
};

// LOOT: take Hakkari Blood (10460) from a freshly-killed Bloodkeeper (8438)
// corpse, driving the loot DIRECTLY (fill + StoreLootItem) so it lands mid-wave
// — the normal DC loot pipeline only runs out of combat and may quality-filter a
// white item. Feeds the flame douse; per-corpse latch so each Bloodkeeper yields
// its blood once. Falls back to a grant if the direct loot can't complete, so a
// hard-to-reach encounter never stalls for want of blood.
class DungeonClearHakkarLootBloodAction : public DcMovementAction
{
public:
    DungeonClearHakkarLootBloodAction(PlayerbotAI* botAI)
        : DcMovementAction(botAI, "dungeon clear hakkar loot blood")
    {
    }
    bool Execute(Event& event) override;
};

// Run on non-tank party bots while their tank is in DC mode. Redirects follow
// from the player master to the tank so the party stays with whoever is
// leading the clear.
class DungeonClearFollowTankAction : public DcMovementAction
{
public:
    DungeonClearFollowTankAction(PlayerbotAI* botAI) : DcMovementAction(botAI, "dungeon clear follow tank") {}
    bool Execute(Event& event) override;
};

// ALL bots, non-combat — paired with DungeonClearRezPartyTrigger (which fires
// only on the elected rezzer). Walks the rezzer within cast range + line of
// sight of the first recoverable corpse (target priority: healer > tank >
// group order) with the module's standard no-force pathing, then casts the
// class resurrection via DoSpecificAction ("resurrection" / "redemption" /
// "ancestral spirit" / "revive"). Returns false while the cast is not yet
// possible (out of mana, target vanished) so the lower rungs — the drink/eat
// rung at 26.5 in particular — get the tick and the rezzer can afford the
// cast; the recovery timeout in DcRezRecovery backstops a rezzer that never
// manages it. The stock class-strategy pairing ("party member dead" -> class
// rez at rel 40) acts as an independent backup; both converge safely because
// the stock "party member to resurrect" value excludes targets with a pending
// rez request or an in-flight rez cast.
class DungeonClearRezPartyAction : public DcMovementAction
{
public:
    DungeonClearRezPartyAction(PlayerbotAI* botAI)
        : DcMovementAction(botAI, "dungeon clear rez party")
    {
    }
    bool Execute(Event& event) override;
};

// Drives a travel OBJECTIVE (DungeonAnchorKind::Objective from
// BossRosterRegistry) once the tank has reached it (DungeonClearAtObjectiveTrigger
// fired). Runs the objective's declarative event (DungeonEventRegistry) or its
// legacy on-arrival hook (ObjectiveHookRegistry): on Done it latches the anchor
// into "dungeon clear cleared anchors" so NextDungeonBossValue advances to the
// next target; on Running it holds the tank at the anchor; on Blocked it stalls
// the run for the human. Derives from DungeonClearEngageActionBase so a
// KillCreature(engage) step can drive the engage pipeline (EngageDirect) — the
// tank actively seeks out and fights the named creature (e.g. the temple bosses
// down ZulFarrak's stairs) instead of merely gating on its death while held.
class DcObjectiveArriveAction : public DungeonClearEngageActionBase
{
public:
    DcObjectiveArriveAction(PlayerbotAI* botAI)
        : DungeonClearEngageActionBase(botAI, "dungeon clear objective arrive")
    {
    }
    bool Execute(Event& event) override;
};

// Drives an off-path CONDITIONAL event (DungeonEventRegistry, activation
// Conditional) selected by DungeonClearEventDueTrigger. Cancels any escort glide
// so the tank holds, runs the event's steps via DungeonEventExecutor against the
// shared conditional-progress value, and on completion latches the event's
// synthetic key into "dungeon clear cleared anchors" so it never re-fires this
// run. Running holds; a required step that Blocks/times out stalls for the human;
// an optional one Skips (latches) and the clear proceeds. Sits at relevance 31,
// above the at-boss pull, so a due pre-boss gate is handled first.
//
// Milestone 3: a room-aggro PRE-CLEAR event (DungeonEventRegistry::
// IsRoomAggroPreClear — a Conditional gate with a lone KillCreature(0) step) is
// special-cased to drive the engage pipeline directly (EngageDirect on the
// nearest room trash) instead of the step executor, since the KillCreature step
// only GATES. That is why this derives from DungeonClearEngageActionBase rather
// than plain MovementAction.
class DcRunEventAction : public DungeonClearEngageActionBase
{
public:
    DcRunEventAction(PlayerbotAI* botAI)
        : DungeonClearEngageActionBase(botAI, "dungeon clear run event")
    {
    }
    bool Execute(Event& event) override;

protected:
    DcRunEventAction(PlayerbotAI* botAI, std::string const& name, bool requireDrivesInCombat)
        : DungeonClearEngageActionBase(botAI, name), _requireDrivesInCombat(requireDrivesInCombat)
    {
    }
    // Passed to DungeonEventExecutor::FindDueConditionalEvent so the combat copy
    // can only ever pick up an event that opted in (DungeonEvent::drivesInCombat).
    bool _requireDrivesInCombat{false};
};

// COMBAT-engine sibling of DcRunEventAction: the same step driver, selected by
// DungeonClearEventDueCombatTrigger and restricted to events flagged
// DungeonEvent::drivesInCombat. Registered in DungeonClearCombatStrategy at
// DcRel::EventDueCombat — above the stock combat movers, so a wave encounter's
// driver can actually reposition the tank mid-fight (walk it to the next rift)
// instead of being frozen out for the whole encounter. See
// DungeonEvent::drivesInCombat for why the non-combat-only rung was not enough.
class DcRunEventCombatAction : public DcRunEventAction
{
public:
    DcRunEventCombatAction(PlayerbotAI* botAI)
        : DcRunEventAction(botAI, "dungeon clear run event combat", /*requireDrivesInCombat*/ true)
    {
    }
};

// COMBAT-engine sibling of the objective KillCreature-engage driver. A stealthed
// mob (Shattered Halls' Shattered Hand Assassins) can Sap the tank: the sap flags
// the party into combat AND the mob stays/re-stealthed, so once the incapacitate
// wears off stock combat has no detectable victim and the run wedges "in combat,
// nothing to hit". The non-combat DcObjectiveArriveAction engage branch can't run
// then (combat owns the engine), so this drives the same DriveObjectiveEngage()
// from the combat engine: walk the tank onto the undetected sapper by ENTRY and
// Attack it, breaking stealth on the first swing. Gated (in the trigger) to fire
// only while an engage objective is active and an undetected, reachable creature
// of its entry sits nearby; inert the instant it becomes detectable, handing the
// kill back to stock combat.
class DcObjectiveEngageCombatAction : public DungeonClearEngageActionBase
{
public:
    DcObjectiveEngageCombatAction(PlayerbotAI* botAI)
        : DungeonClearEngageActionBase(botAI, "dungeon clear objective engage combat")
    {
    }
    bool Execute(Event& event) override;
};

class DungeonClearDisableOnDeathAction : public Action
{
public:
    DungeonClearDisableOnDeathAction(PlayerbotAI* botAI) : Action(botAI, "dungeon clear disable on death") {}
    bool Execute(Event& event) override;
};

class DungeonClearDisableOnClearedAction : public Action
{
public:
    DungeonClearDisableOnClearedAction(PlayerbotAI* botAI) : Action(botAI, "dungeon clear disable on cleared") {}
    bool Execute(Event& event) override;
};

// Phantom-combat escape hatch. Fired by DungeonClearBreakStuckCombatTrigger once a
// DC member has been flagged in combat with nothing fightable (no attacker, no
// victim, no reachable holder) for DungeonClear.StuckCombatTimeout seconds. Force-
// clears the bot's combat AND drops it from every threat list — the same effect as a
// GM `.combatstop`, which is what unwedges a member ghost-flagged by a mob that
// spawned far away / behind a gate. The gating all lives in the trigger; this action
// is the unconditional recovery.
class DungeonClearBreakStuckCombatAction : public Action
{
public:
    DungeonClearBreakStuckCombatAction(PlayerbotAI* botAI) : Action(botAI, "dungeon clear break stuck combat") {}
    bool Execute(Event& event) override;
};

// Stranded-member recovery failsafe. Paired with DungeonClearRecoverStrandedTrigger
// (which fires only on the leader once the run has frozen for the configured window
// with a bot member out of range): teleports every stuck bot member to the tank and
// re-arms the no-progress clock. See DcStrandedRecovery.
class DungeonClearRecoverStrandedAction : public Action
{
public:
    DungeonClearRecoverStrandedAction(PlayerbotAI* botAI) : Action(botAI, "dungeon clear recover stranded") {}
    bool Execute(Event& event) override;
};

// Walks the tank up to the blocking door, then stalls with an explicit
// "door is closed" message in party chat. The door is detected up to 80yd
// ahead, so without the walk-in the tank would park wherever it was when the
// door entered look-ahead — often far short of the door. Bot stays enabled so
// the player can open the door and the next tick resumes; only the
// position-stuck recovery or `dc off` cancels.
class DungeonClearDoorBlockedAction : public DcMovementAction
{
public:
    DungeonClearDoorBlockedAction(PlayerbotAI* botAI) : DcMovementAction(botAI, "dungeon clear door blocked") {}
    bool Execute(Event& event) override;
};

// Keeps the DC loot policy (DungeonClear.LootMinQuality / IgnoreChests) enforced
// while the run is PAUSED. The driving ladder is inert when paused, so the
// loot-floor filter that normally runs inline in advance/follow-tank never gets
// a tick and the bot reverts to the stock playerbots loot pipeline. This action
// runs that same filter above the loot pipeline's relevance, then returns false
// so the stock loot actions still collect whatever survives the filter.
class DungeonClearFilterLootAction : public Action
{
public:
    DungeonClearFilterLootAction(PlayerbotAI* botAI) : Action(botAI, "dungeon clear filter loot") {}
    bool Execute(Event& event) override;
};

// --- Advanced pulls -------------------------------------------------------
// Leader-only, non-combat. The out-of-combat half of the pull-to-camp maneuver:
//   Idle      -> stamp camp at the tank's spot, signal Forming.
//   Forming   -> hold a beat so followers go passive, then -> Advancing.
//   Advancing -> run in to the trash pack to grab aggro. The moment combat
//                starts, control passes to DungeonClearPullManeuverAction on the
//                combat engine. Aborts (-> normal walk-in engage) if the run-in
//                wedges or overshoots without aggroing.
//   Engage    -> out-of-combat cleanup: reset to Idle so the next pull is fresh.
class DungeonClearPullAction : public DungeonClearEngageActionBase
{
public:
    DungeonClearPullAction(PlayerbotAI* botAI) : DungeonClearEngageActionBase(botAI, "dungeon clear pull") {}
    bool Execute(Event& event) override;
};

// Leader-only, COMBAT engine. The in-combat half of the maneuver: once aggro is
// confirmed it runs the tank back to the camp (suppressing stock chase/attack by
// owning the tick), then hands the fight to stock combat at camp (phase Engage),
// which is also when ReapStrandedPassives releases the party. Gives up to fight
// in place if the return leg wedges.
class DungeonClearPullManeuverAction : public DcMovementAction
{
public:
    DungeonClearPullManeuverAction(PlayerbotAI* botAI) : DcMovementAction(botAI, "dungeon clear pull maneuver") {}
    bool Execute(Event& event) override;
};

// Shared body for the two follower-only "hold the party at camp" actions. Puts
// the bot passive (DcFollowerLifecycle::ApplyFollowerPassive), cancels any stale
// follow generator, walks it to the leader's camp, and OWNS the tick (always
// returns true while the leader is in a holding pull phase) so neither
// follow-tank nor stock follow can drag the follower off camp. The two concrete
// subclasses below register this same body under different names on different
// engines.
class DungeonClearCampHoldActionBase : public DcMovementAction
{
public:
    DungeonClearCampHoldActionBase(PlayerbotAI* botAI, std::string const& name)
        : DcMovementAction(botAI, name)
    {
    }
    bool Execute(Event& event) override;
};

// Non-combat engine: holds the party at camp while the leader pulls and the
// follower is OUT of combat (DungeonClearHoldAtCampTrigger).
class DungeonClearHoldAtCampAction : public DungeonClearCampHoldActionBase
{
public:
    DungeonClearHoldAtCampAction(PlayerbotAI* botAI)
        : DungeonClearCampHoldActionBase(botAI, "dungeon clear hold at camp")
    {
    }
};

// Combat engine: the same hold, for when the follower is IN combat. A held
// follower enters combat the instant the tank does (group combat), which
// switches it to the combat engine where the non-combat hold can't run AND
// PassiveMultiplier explicitly green-lights stock "follow" — so without this the
// party trails the tank the moment a pull aggros. The action NAME deliberately
// contains "stay" so PassiveMultiplier's substring whitelist lets it run while
// the follower is +passive; registered above the stock combat movers so it owns
// the tick and pins the follower at camp until release. See
// DungeonClearHoldAtCampCombatTrigger.
class DungeonClearStayAtCampAction : public DungeonClearCampHoldActionBase
{
public:
    DungeonClearStayAtCampAction(PlayerbotAI* botAI)
        : DungeonClearCampHoldActionBase(botAI, "dungeon clear stay at camp")
    {
    }
};

// Shared body for the two follower-only "join the leader's fight" actions.
// Resolves the nearest live unit attacking the leader tank — LINE-OF-SIGHT BLIND
// on purpose — sets it as the bot's current target, forces the bot into combat
// with it, and moves the bot onto it. This is the fix for a fight the follower
// can't see or reach: a camp parked near a corner, or a Leeroy/dynamic/boss
// pull the tank took around a corner or beyond the follower's natural engage
// range — anywhere the stock LOS-gated target picker never acquires the pack
// and the party stands idle while the tank solos. Moving the follower in
// regains sight, at which point its trigger goes inert (a valid attacker is
// now visible) and stock combat owns the fight. Gated by
// DcLeaderSignal::IsLeaderFightAssistWanted; registered under two names on the
// two engines (see the subclasses below).
class DungeonClearAssistCampActionBase : public DcMovementAction
{
public:
    DungeonClearAssistCampActionBase(PlayerbotAI* botAI, std::string const& name)
        : DcMovementAction(botAI, name)
    {
    }
    bool Execute(Event& event) override;
};

// Non-combat engine: a follower that never took a hit, sitting idle while the
// leader tank fights.
class DungeonClearAssistCampAction : public DungeonClearAssistCampActionBase
{
public:
    DungeonClearAssistCampAction(PlayerbotAI* botAI)
        : DungeonClearAssistCampActionBase(botAI, "dungeon clear assist camp")
    {
    }
};

// Combat engine: a follower dragged into combat but with the pack out of sight,
// idling in the combat engine with an empty LOS attacker list.
class DungeonClearAssistCampCombatAction : public DungeonClearAssistCampActionBase
{
public:
    DungeonClearAssistCampCombatAction(PlayerbotAI* botAI)
        : DungeonClearAssistCampActionBase(botAI, "dungeon clear assist camp combat")
    {
    }
};

// Combat engine: a follower the contribution gate decided cannot help from where it
// stands (a DPS with no visible attacker, or a healer parked where it can't heal the
// tank), or one that drifted past the hard tether. Moves it to a ROLE-CORRECT
// standoff point with LOS on the fight — a ring point at spell range around the
// fight anchor for a ranged DPS, a heal-range point around the tank for a healer,
// a corner-rounding fractional approach for a melee / when no ring point validates —
// never onto the tank's cell. Driven by DungeonClearRegroupCombatTrigger.
class DungeonClearRegroupCombatAction : public DcMovementAction
{
public:
    DungeonClearRegroupCombatAction(PlayerbotAI* botAI)
        : DcMovementAction(botAI, "dungeon clear regroup combat")
    {
    }
    bool Execute(Event& event) override;

private:
    // Re-issue guard: the last destination handed to DcMoveTo. The trigger can
    // re-fire every tick while latched; re-issuing a near-identical move each time
    // re-plots the spline and stutters/cast-clips the bot (cf. the spline-reissue
    // freeze). Skip the move while already travelling toward within 3yd of it.
    Position _lastDest;
    bool     _lastDestValid = false;
};

// Healer-only, BOTH engines. Moves the healer to a point with line of sight AND
// heal range to its most-hurt heal target (the DC `dungeon clear heal target`
// value), so a healer dragged out of sight of the tank walks back into a spot it
// can heal from — after which the stock heal stack re-acquires the target on its
// own. Samples a ring of standoff points around the target and takes the nearest
// one that is navmesh-valid, has LOS, and is path-reachable; falls back to a
// pathfound approach toward the target (5yd standoff) when none validate. Banded
// COMBAT/NORMAL priority like the assist/regroup actions to avoid plowing a mob
// train on a long run back. Driven by DungeonClearHealRepositionTrigger.
class DungeonClearHealRepositionAction : public DcMovementAction
{
public:
    DungeonClearHealRepositionAction(PlayerbotAI* botAI)
        : DcMovementAction(botAI, "dungeon clear heal reposition")
    {
    }
    bool Execute(Event& event) override;
};

// ANY role, BOTH engines. Moves the bot OUT of an active-vacate hazard emitter's
// pulse (DcHazard::NearestVacate — the Arcatraz Destroyed Sentinel's 15yd Energy
// Discharge, a Maraudon Creeping Sludge's 5yd Poison Shock). Aims a point directly
// away from the emitter, past its pulse radius plus that row's retreat slack,
// snapped to the navmesh, clear of every other hazard's placement keep-out AND of
// every live danger band, and path-reachable; if the straight-away point is
// blocked it fans the away-bearing around until one validates. MOVEMENT_COMBAT
// priority so it overrides the bot's MoveChase / advance. Driven by
// DungeonClearHazardVacateTrigger.
class DungeonClearHazardVacateAction : public DcMovementAction
{
public:
    DungeonClearHazardVacateAction(PlayerbotAI* botAI)
        : DcMovementAction(botAI, "dungeon clear hazard vacate")
    {
    }
    bool Execute(Event& event) override;

private:
    // The committed retreat point. This action used to recompute every tick, which
    // is fine with ONE emitter (the bearing is stable) and catastrophic with a
    // field of them: NearestVacate re-elects a different centre on a step of drift
    // and the bearing reverses, so the bot re-plots its spline several times a
    // second and travels nowhere. While this point is still outside every danger
    // band and the bot is still walking to it, the action owns the tick and leaves
    // the move alone. Cleared when the bot is clear, or when no bearing validated.
    //
    // `_fleeSetAtMs` caps how long that ride may last. A commitment is only as good
    // as the point behind it, and an unbounded one turned a bad candidate into a
    // 47-second march in tr-20260815-154816-5. Committed is not unsupervised.
    Position _fleeTo;
    bool     _fleeToValid = false;
    uint32   _fleeSetAtMs = 0;
};

// Leader-only, non-combat engine. The tank's mirror of the follower assist: a
// groupmate is fighting a pack the tank never saw, so rather than stalling on the
// Advance rest gate, find what the party is fighting, force the tank into combat
// with it, and move onto it to take threat. Once in sight / in combat the tank
// flips to its own combat engine (pull-maneuver/rotation) and this stands down.
// Driven by DungeonClearLeaderAssistTrigger.
class DungeonClearLeaderAssistAction : public DcMovementAction
{
public:
    DungeonClearLeaderAssistAction(PlayerbotAI* botAI)
        : DcMovementAction(botAI, "dungeon clear leader assist")
    {
    }
    bool Execute(Event& event) override;
};

#endif
