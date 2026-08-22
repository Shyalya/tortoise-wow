/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCAPPROACHSTATE_H
#define _PLAYERBOT_DCAPPROACHSTATE_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "Timer.h"
#include "Ai/Dungeon/DungeonClear/Util/DcProgressWatchdog.h"

// All transient per-approach state for one boss-approach run, owned as a single
// value (DungeonClearApproachStateValue, "dungeon clear approach state") so the
// whole advance state machine resets in lockstep through exactly one Reset().
// This replaced nine loose ManualSetValue globals — the position-stuck and
// MoveTo-noop counters, the consecutive-rebuild count, the direct-pursuit and
// path-ends-short give-up latches, the loot-yield commit anchor, the position
// sentinel + last committed boss entry, and the long-path cache state — whose
// resets were scattered across two free functions in the action TU and ~25
// inline ->Set(0u) calls in the chat actions, kept in sync by hand. That was
// the identical failure mode the pull FSM fixed: "the approach is flaky" almost
// always meant "one of N globals didn't reset in lockstep" (a stale latch
// surviving pause / skip / resume / boss change).
//
// Add a new per-approach field HERE (never as a separate value) so it can never
// be forgotten by a reset. Mirrors DcPullContext. The two named subset resets
// (OnBossChange / OnEnteredEngageRange) replace the old ResetApproachOn* free
// functions so the reset subsets are named, visible, and co-located instead of
// inline ->Set(0u) clusters.
struct DcApproachState
{
    // --- per-approach stuck / recovery watchdogs (nav review F11) ---------
    // Four instances of the shared DcProgressWatchdog replace the per-movement-
    // mode stuck counters that had accreted one at a time. Two watch DISPLACEMENT
    // (an escort spline grinding against geometry): the route glide and the
    // door-blocked walk-in. Two watch CLOSING-DISTANCE to the boss (which also
    // sees a fully STALLED bot — the non-moving blind spot displacement can't):
    // the direct-pursuit give-up latch and the path-ends-short final approach.
    // Separate instances because they run in different phases and reset
    // independently. stuckCount / rebuildAttempts stay as the meta-layer.
    DcProgressWatchdog routeGlideWatch;      // advance route-glide wedge (was posStuckTicks)
    DcProgressWatchdog doorWalkInWatch;      // door walk-in wedge (was doorWalkInStuckTicks)
    DcProgressWatchdog pursuitWatch;         // direct-pursuit give-up latch (was pursuitFailTicks)
    DcProgressWatchdog finalApproachWatch;   // path-ends-short escalation (was doneNotEngagedTicks)
    // Net-progress gate for the stuck-recovery LADDER (resnap -> rebuild -> nudge ->
    // stall). Distinct from routeGlideWatch, which asks "did I move this tick"; this
    // asks "have I got any NEARER my objective since the last recovery". They must be
    // different questions: a bot shuttling back and forth displaces plenty every tick
    // while getting nowhere, and keying the ladder's reset on displacement made the
    // escalation unreachable by construction (tr-20260804-153254-2: 87 of 87 posStuck
    // events logged resnapAttempts=1 rebuildAttempts=0 — the ladder never once left
    // its first rung across a 27-minute run). See FillStuckObs.
    DcProgressWatchdog recoveryProgressWatch;
    // Bounds the engage-trash "far, long-route trash -> let Advance close the gap"
    // hand-off. It is only sound while Advance is actually walking us toward the pack;
    // for one beside or behind the route to the next boss the hand-off is permanent and
    // a pack DC already voted to fight is silently abandoned (tr-20260804-153254-2).
    // Tracks whether the gap to `longRouteDeferTarget` is closing — see
    // DungeonClearEngageTrashAction::Execute.
    DcProgressWatchdog longRouteDeferWatch;
    ObjectGuid longRouteDeferTarget;         // pack the deferral above is measuring
    // The deferral has been judged a failure for THIS target and must not be offered
    // again while it stays the target. A latch, because the budget alone cannot carry
    // the verdict: the one walk-in step taken on the tick the budget blew is itself an
    // improvement in the gap, which re-arms the closing test and hands the pack
    // straight back to Advance. Without this the action claims one tick in every
    // DC_LONGROUTE_DEFER_LIMIT and the tank is dragged away in between — a stutter
    // rather than an engagement. Cleared when the target changes or comes in range.
    bool longRouteDeferBlown = false;
    uint32 stuckCount          = 0;  // MoveTo-returned-false backup (was "stuck count")
    uint32 rebuildAttempts     = 0;  // consecutive rebuilds w/o progress ("stride rebuild attempts")
    uint32 resnapAttempts      = 0;  // consecutive Resnap recoveries w/o progress (rung-1 give-up)
    // Consecutive navmesh-nudges (the ladder's TOP rung) that failed to buy net
    // progress. The rung had no give-up of its own, and TryFarFromPolyRecovery
    // succeeds trivially whenever the bot is ON the mesh — a 5yd axis probe from
    // a walkable poly always paths — so it reset rebuildAttempts and returned
    // "recovered" forever, making the stall below it dead code. Nine such resets
    // in nine minutes on the Blackrock Spire ramp (tr-20260818-073620-14) with
    // zero nudge or stall reaching the player. Same failure shape as the
    // recoveryProgressWatch comment above, one rung higher up.
    uint32 nudgeAttempts       = 0;
    uint32 partyNotReadyTicks  = 0;  // consecutive between-pulls not-ready ticks (yield debounce)

    // --- approach bookkeeping ---------------------------------------------
    Position lastPos;                // previous-tick world pos; (0,0,0) = not yet sampled
    uint32 lastTickLogMs       = 0;  // advance-tick debug-log throttle (getMSTime)
    uint32 lastObjectiveDiagMs = 0;  // at-objective "near but not arrived" diag throttle (getMSTime)
    uint32 lastTargetEntry     = 0;  // committed boss entry the approach is for
    uint32 lootYieldStartMs    = 0;  // loot-yield commit anchor (getMSTime)

    // --- room-aggro skirt orbit latch -------------------------------------
    // When the room-clear must reach a trash pack on the far side of a boss's
    // aggro sphere it ORBITS the safe ring. If the short arc (toward the target's
    // bearing) is wall-blocked the skirt rounds "the long way" instead — but
    // re-deriving the short/long choice every tick made the tank step the long
    // way once, then immediately flip back toward the target next tick (the short
    // arc reads clean again from the new spot), bouncing between two ring points
    // forever and never committing to going BACKWARD around the boss. Latching
    // the chosen rotation direction (+1/-1) for the duration of one orbit makes
    // the tank round the whole long way to the open side. Released the instant a
    // straight shot at the target clears the sphere, when the skirt target
    // changes, and on every approach reset / boss change.
    int8 skirtOrbitDir         = 0;  // 0 = unlatched, +/-1 = committed orbit rotation
    ObjectGuid skirtOrbitTarget;     // trash GUID the current orbit latch is for

    // En-route pack avoidance (DcEngageGeometry::EnRoutePackAvoidPoint) reuses
    // the same orbit machinery, but around a BYSTANDER pack rather than a
    // room-aggro boss — and which bystander is "the one in the way" changes as
    // the tank rounds them one at a time. The latch is therefore keyed by the
    // SPHERE being rounded, not by the destination: inheriting a "round left"
    // committed for a pack we have already passed is the same two-point bounce
    // the boss skirt latch exists to prevent. Reset when the chosen sphere
    // changes, and by the approach reset below.
    int8 avoidOrbitDir         = 0;  // 0 = unlatched, +/-1 = committed rotation
    ObjectGuid avoidOrbitSphere;     // pack GUID the current avoid orbit is for

    // --- chase leash (approach to a MOVING trash target) ------------------
    // A trash target is latched by GUID and read live, so a walking mob turns
    // every approach into a pursuit: the tank follows it across the room and
    // wakes everything its route passes behind. The leash pins the approach to
    // the ground the pull was PLANNED against — `chaseAnchor` is where the mob
    // stood when we committed to walking at it, `chaseOrigin` where the tank
    // stood then (the fixed origin the receding test is measured from; from the
    // tank's live position the gap always shrinks as we walk and the leash could
    // never engage). A target that recedes past the leash is waited out rather
    // than chased — see DungeonClearMath::DecideChase.
    //
    // Keyed by `chaseTarget`, so a different pack simply re-anchors; there is no
    // stale-latch window. Shared by BOTH walks that aim at a live trash unit (the
    // pull's tag leg and the engage walk-in) so they can never disagree about how
    // far the same mob has drifted.
    ObjectGuid chaseTarget;          // target the anchor below belongs to
    Position   chaseAnchor;          // where THAT MOB stood when we committed
    Position   chaseOrigin;          // where the TANK stood then (gap origin)
    uint32     chaseHoldSince  = 0;  // getMSTime() the current hold began; 0 = not holding

    // Mid-glide hazard probe (Advance). While a continuous escort spline is in
    // flight the hop ladder short-circuits (the ride outranks everything), so a
    // PATROL can wander into the committed window after launch unobserved. The
    // probe re-tests the remaining window against the bystander avoid-spheres at
    // most every DC_GLIDE_HAZARD_PROBE_MS and halts the glide (escort-aware
    // stop) when something violates it. The ignore latch is what stops a
    // stop/launch ping-pong: the sphere that caused the last interrupt is
    // skipped, so the tank interrupts only for something NEW.
    uint32 glideHazardProbeMs  = 0;  // last probe timestamp (getMSTime)

    // Flagged-in-combat driving gate. getMSTime the "flagged in combat but nobody
    // is actually fighting" state began; 0 = not streaking. Shared by every driver
    // trigger (they all ask the same question about the same bot), and cleared the
    // instant a real engagement reappears. See DungeonClearMath::MayDriveWhileFlagged
    // and DC_FLAGGED_NO_ENGAGE_GRACE_MS.
    uint32 flaggedNoEngageSinceMs = 0;

    // The rest gates' twin of the latch above (DcCombatFlag::IsPhantomFlag). Same
    // grace, same kernel, but its `flagged` input is the WHOLE party's flag rather
    // than this bot's own: the between-pulls gate waits for every member to be
    // topped up, so one member held in combat by an aura is enough to make the
    // wait unsatisfiable. It needs its own streak because a different input means
    // a different streak — folding it into flaggedNoEngageSinceMs would make the
    // driving ladder wait out a grace it never used to.
    uint32 partyFlaggedNoEngageSinceMs = 0;
    ObjectGuid glideHazardIgnore;    // sphere behind the last interrupt

    // --- blocking-door interaction ----------------------------------------
    // Last door the bot clicked open and when, so the door-blocked action can
    // re-click an auto-closing gate (Strat's King's Square Gate re-shuts 3s
    // after opening) on a cooldown instead of the old one-shot announce latch,
    // which deadlocked: if the gate re-closed before Advance ran and cleared
    // the latch, the bot never clicked again and sat "Blocked" forever.
    ObjectGuid lastDoorUseGuid;      // door the last Use() was issued on
    uint32 lastDoorUseMs       = 0;  // when it was issued (getMSTime)

    // Blocked-state watchdog: how long the door-blocked action has been parked
    // at one closed door it believes it can open, without the door actually
    // opening. The entitlement check is template-level and CAN be wrong (SFK's
    // Arugal's Lair is an event door wearing the same empty-lock-85 template as
    // a plain clickable Deadmines door), and the click gate measures range to
    // the GO origin, which on wide gates sits outside DC_DOOR_USE_RANGE of the
    // path-side parking spot — both leave the bot "working" a door forever.
    // After DungeonClear.DoorBlockedTimeout seconds the action gives up and
    // takes the same auto-pause path as a door it knows it can't open.
    // doorStallLastMs re-arms the window: a gap in observations means the stall
    // ended (door opened, run moved on) and a later stall starts fresh.
    //
    // "Parked AT the door" is load-bearing, not shorthand: the action's park is
    // also the fall-through for every walk-in failure, and the door is flagged
    // up to 80yd ahead along the corridor, so those parks land anywhere on the
    // approach. Only the arrival park feeds this watchdog. When it fed all of
    // them the (5s default) budget went on travel time and runs auto-paused at
    // doors they had never touched — see the atDoor gate in
    // DungeonClearDoorBlockedAction.
    ObjectGuid doorStallGuid;        // door the current Blocked stall is on
    uint32 doorStallSinceMs    = 0;  // when that stall began (getMSTime)
    uint32 doorStallLastMs     = 0;  // last tick the stall was observed

    // --- long-path cache state --------------------------------------------
    // The cached long-range A* result lives in its own value ("dungeon clear
    // long path"); these are the bookkeeping fields that govern when it rebuilds
    // and what it was built toward. longPathTargetEntry doubles as the cache key
    // (read by DcStatusPublisher for the route-target display).
    uint32 longPathTargetEntry = 0;  // boss entry the cache was built for (cache key)
    Position longPathTargetPos;      // world pos the cache was built toward (retarget check)
    uint32 longPathExpiresMs   = 0;  // cache TTL deadline (getMSTime); 0 = force rebuild
    uint64 pendingPathJob      = 0;  // in-flight async build job id (0 = none)
    uint32 pendingPathSinceMs  = 0;  // when the pending job was submitted (watchdog)
    Position pendingPathStartPos;    // bot pos at submit; a result whose start the
                                     // bot has since left far behind (teleport /
                                     // event relocation mid-build) is stale and
                                     // discarded at drain instead of installed

    // Follower-cursor snapshot at the last install / TTL re-arm. EnsureLongPath
    // defers the TTL rebuild while the cursor has advanced past this baseline
    // (and the bot isn't position-stuck): a 15s TTL expiry on a route the bot is
    // actively walking otherwise triggers a churny full A*+Finalize rebuild of a
    // perfectly good path and resets the cursor. The TTL is honoured only once
    // forward progress stalls. Reset to 0/0 by InstallLongPath (which also zeroes
    // the follower state), so the baseline always matches a fresh cursor.
    uint32 lastProgressSegmentIdx = 0;
    uint32 lastProgressPointIdx   = 0;

    // Full reset: every approach AND long-path-cache field. Used on dc on/off,
    // death, all-cleared, and every pull interrupt — the run-state teardown.
    void Reset() { *this = DcApproachState{}; }

    // Committed boss changed: wipe every per-approach counter/latch and the
    // position sentinel so nothing from the previous pull bleeds into the new
    // approach. Leaves the long-path cache fields alone — EnsureLongPath manages
    // those off the new entry. (The sticky engage-trash target is a separate
    // value reset at the call site, not part of this struct.)
    void OnBossChange(uint32 newEntry)
    {
        lastTargetEntry     = newEntry;
        stuckCount          = 0;
        routeGlideWatch.Reset();
        doorWalkInWatch.Reset();
        pursuitWatch.Reset();
        finalApproachWatch.Reset();
        recoveryProgressWatch.Reset();
        longRouteDeferWatch.Reset();
        longRouteDeferTarget.Clear();
        longRouteDeferBlown = false;
        rebuildAttempts     = 0;
        resnapAttempts      = 0;
        nudgeAttempts       = 0;
        partyNotReadyTicks  = 0;
        lastPos             = Position();
        skirtOrbitDir       = 0;
        skirtOrbitTarget.Clear();
        avoidOrbitDir       = 0;
        avoidOrbitSphere.Clear();
        chaseTarget.Clear();
        chaseAnchor         = Position();
        chaseOrigin         = Position();
        chaseHoldSince      = 0;
        glideHazardProbeMs  = 0;
        glideHazardIgnore.Clear();
        doorStallGuid.Clear();
        doorStallSinceMs    = 0;
        doorStallLastMs     = 0;
    }

    // Reached engage range: clear the dead-end escalation counter and the
    // direct-pursuit give-up latch, so a boss that wanders back out can be
    // re-pursued cleanly instead of staying latched off the pursuit shortcut.
    void OnEnteredEngageRange()
    {
        finalApproachWatch.Reset();
        pursuitWatch.Reset();
    }

    // One observation of the blocked-state watchdog, from the door-blocked
    // action's ARRIVAL park only (see the doorStall* field comments for why the
    // approach parks must not call this). Arms the window on a new door or after
    // a rearmMs observation gap, records the observation, and returns whether the
    // bot has now been working this door past timeoutMs without it opening.
    bool ObserveDoorStall(ObjectGuid door, uint32 now, uint32 rearmMs, uint32 timeoutMs)
    {
        if (doorStallGuid != door || getMSTimeDiff(doorStallLastMs, now) >= rearmMs)
        {
            doorStallGuid    = door;
            doorStallSinceMs = now;
        }
        doorStallLastMs = now;
        return getMSTimeDiff(doorStallSinceMs, now) >= timeoutMs;
    }

    // The stall is OVER: the door has been seen open, or the corridor flag moved
    // to a different door / cleared entirely. The next arrival park arms a fresh
    // window instead of resuming the old one.
    //
    // Load-bearing for auto-closing gates. Stratholme's two King's Square Gates
    // carry door.autoCloseTime 3000, so the cycle is: click, gate opens, tank
    // walks through, gate re-shuts 3s later, click again. The observation-gap
    // re-arm inside ObserveDoorStall cannot see that — DC_DOOR_STALL_REARM_MS is
    // 10s and the gap between two arrival parks on a 3s gate is only ~3s — so
    // three successful opens accumulated into one 7s "stall" and tripped the
    // (5s default) watchdog on a gate the bot was opening every single time.
    // Run tr-20260816-151006-14 died exactly there, 27.9yd from Hearthsinger
    // Forresten with 8/13 bosses down. "The door opened" is the signal; a gap in
    // observations is only a proxy for it.
    void ClearDoorStall()
    {
        doorStallGuid.Clear();
        doorStallSinceMs = 0;
        doorStallLastMs  = 0;
    }
};

#endif
