/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARAPPROACH_H
#define _PLAYERBOT_DUNGEONCLEARAPPROACH_H

#include <cstdint>

// Pure decision core for the boss-approach tail of DungeonClearAdvanceAction.
//
// The action's tail is an ordered phase ladder (stuck recovery -> direct pursuit
// -> long-path-unreachable -> off-path resnap -> [NextHop] -> hop-done escalation
// -> jump leg -> ride live glide -> off-line rejoin -> spline window -> per-point
// fallback). Each phase decides whether it owns the tick from a handful of
// threshold comparisons, then does engine work (MoveTo, glide control, ...).
//
// This function isolates the DECISION — "given the current observation, which
// phase owns this tick?" — from the EFFECT, which stays in the action. The whole
// regression-prone threshold graph (posStuck just under/over the tick limit, the
// pursuit-fail latch boundary, async-pending vs. a genuine unreachable, hopDone
// with engageDist just under/over engageRange, the dead-end escalation budget)
// becomes one readable, engine-free function the gtest suite can pin at every
// edge instead of a live raid. The thresholds are inputs (the action's DC_*
// constants), so a test fixes them explicitly and the action passes its own.
//
// Engine-free on purpose: no Player/Creature/MotionMaster — the action gathers
// the booleans/distances (LOS, reachability, geometry) and fills an observation.
namespace DungeonClearApproach
{
    // One verdict per owning phase, in ladder order. Reanchor is deliberately NOT
    // here: it is a cursor mutation between OffPath and the hop phases, never a
    // tick-ownership decision. RebuildAndYield is the benign hopDone-while-already-
    // in-engage-range case (force a rebuild, yield the tick).
    enum class Verdict : std::uint8_t
    {
        StuckRecover,        // posStuckTicks >= stuckTickLimit
        Pursue,              // live boss in LOS+range, pursuit latch not tripped
        PlanRouteWait,       // no path yet, async build in flight — hold quietly
        FarFromPolyRecover,  // no path, wedged off the navmesh — nudge onto it
        Swim,                // no path / dead-end, water lies between — swim leg
        Stall,               // no navigable route / route dead-ends short
        OffPathRebuild,      // drifted off the corridor past the tick budget
        RebuildAndYield,     // hop done but already inside engage range — rebuild
        FinalApproach,       // hop done short of boss, within the retry budget
        JumpLeg,             // current hop is an anchor-declared jump
        RideLiveGlide,       // a healthy escort spline is already gliding
        OffLineRejoin,       // physically off the line — rejoin via a pathed route
        IssueSplineWindow,   // normal case: launch the upcoming polyline window
        MoveToFallback,      // window < 2 points — single per-point MoveTo (terminal)
    };

    // Pure data gathered by the action. Booleans/distances only — the action
    // resolves LOS, navmesh reachability, and geometry; the thresholds are the
    // action's DC_* constants passed in so the decision graph has no hidden deps.
    struct Observation
    {
        // --- geometry / engage ---
        float engageDist  = 0.0f;
        float engageRange  = 0.0f;

        // --- pre-route gates ---
        std::uint32_t posStuckTicks = 0;   // consecutive no-displacement ticks
        bool  canPursue            = false; // live boss, in LOS, within pursuit range
        std::uint32_t pursuitFailTicks = 0; // direct-pursuit give-up latch
        bool  allowRecoveryMoves   = true;  // DC_ALLOW_RECOVERY_MOVES

        // --- long-path state ---
        bool  pathReachable    = true;
        bool  asyncPending     = false;     // an async build is in flight
        bool  startFarFromPoly = false;     // wedged off the navmesh
        bool  waterBetween     = false;     // a swim leg could span the gap
        bool  offPath          = false;     // off the corridor past OFF_PATH_TICK_LIMIT

        // --- post-NextHop / hop cluster ---
        bool  hopDone          = false;
        bool  hopIsJump        = false;
        std::uint32_t doneNotEngagedTicks = 0;
        bool  splineRunning    = false;     // a healthy escort spline is gliding
        bool  offLine          = false;     // RouteDeviation > OFF_PATH_THRESHOLD
        bool  haveSplineWindow = false;     // BuildSplineWindow produced >= 2 points

        // --- thresholds (the action's DC_* constants) ---
        std::uint32_t stuckTickLimit      = 0; // DC_STUCK_TICK_LIMIT
        std::uint32_t pursuitFailLimit    = 0; // DC_PURSUIT_FAIL_LIMIT
        std::uint32_t doneNotEngagedLimit = 0; // DC_DONE_NOT_ENGAGED_LIMIT
    };

    // Returns the verdict for the first phase whose guard fires, in ladder order.
    // Total: the bottom rung (MoveToFallback) always matches, so there is always
    // a verdict. The action consults this at each stage with the fields it knows
    // so far and acts only on its own verdict (later-stage fields default to
    // values whose guards are inactive, biasing toward the terminal rung — which
    // an earlier stage never claims).
    Verdict DecideApproach(Observation const& o);
}

#endif
