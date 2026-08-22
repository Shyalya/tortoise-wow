/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Layer 1 of the orchestration replay harness (review report point #3): golden
// decision tables. Every fixture here is a KNOWN HISTORICAL FREEZE/STUTTER from
// the project memory, hand-authored as the observation that produced it and the
// verdict that fixes it. The point is regression-locking: each of these was once
// found only by running the live server, and is now a permanent offline test.
//
// This complements TestDungeonClearApproach.cpp (which pins the threshold EDGES
// of DecideApproach abstractly); this file pins the verdict for SPECIFIC named
// field bugs, so the citation travels with the assertion.
//
// What is deliberately NOT here: the door-engage-priority and loot-oscillation
// regressions from the harness plan's table. Those are decided trigger-side /
// in the loot pipeline, which have NO extracted pure decision function (only the
// advance core, DecideApproach, was made pure). They cannot be pinned at this
// seam; they would need their own pure boundary (a pull/loot §C that was never
// built). See the harness plan doc.

#include "gtest/gtest.h"
#include "DungeonClearApproach.h"

using DungeonClearApproach::DecideApproach;
using DungeonClearApproach::Observation;
using V = DungeonClearApproach::Verdict;

namespace
{
    // The action's real DC_* constants (DungeonClearTuning.h), passed into the
    // pure function as inputs — a fixture fixes them explicitly so it never
    // drifts with a tuning change.
    constexpr uint32_t STUCK_TICK_LIMIT       = 5;
    constexpr uint32_t PURSUIT_FAIL_LIMIT     = 5;
    constexpr uint32_t DONE_NOT_ENGAGED_LIMIT = 15;

    // A healthy, on-corridor tick (mirrors TestDungeonClearApproach::Healthy):
    // reachable, on the line, a spline window ready. Baseline verdict is
    // IssueSplineWindow; each fixture flips only the axes its bug is about.
    Observation Healthy()
    {
        Observation o;
        o.engageDist          = 100.0f;
        o.engageRange         = 22.0f;
        o.haveSplineWindow    = true;
        o.stuckTickLimit      = STUCK_TICK_LIMIT;
        o.pursuitFailLimit    = PURSUIT_FAIL_LIMIT;
        o.doneNotEngagedLimit = DONE_NOT_ENGAGED_LIMIT;
        return o;
    }
}

// dc-posstuck-false-positive: a healthy ~1.48yd/tick glide was tripping the
// 1.5yd displacement stuck-check and killing the escort spline. In decision
// terms the fix means a still-progressing bot (posStuckTicks reset to 0 by real
// movement) must NOT claim StuckRecover — it rides its window/glide.
TEST(DcApproachRegression, PosStuckFalsePositiveDoesNotRecover)
{
    Observation o = Healthy();
    o.posStuckTicks = 0;        // real movement keeps resetting it
    o.splineRunning = true;     // a healthy glide is in flight
    EXPECT_EQ(DecideApproach(o), V::RideLiveGlide);
    EXPECT_NE(DecideApproach(o), V::StuckRecover);
}

// dc-path-ends-short-livelock: a long-path completes just outside engage range
// (boss on a ledge / across a gap). The old code silently rebuilt a 0-point path
// forever — posStuck can't see a non-moving bot. The fix escalates to a bounded
// final-approach instead of livelocking.
TEST(DcApproachRegression, PathEndsShortFinalApproachesNotStall)
{
    Observation o = Healthy();
    o.hopDone             = true;
    o.engageDist          = 30.0f;   // 30 > 22 range: short of the boss
    o.doneNotEngagedTicks = 3;       // 3 < 15 budget
    EXPECT_EQ(DecideApproach(o), V::FinalApproach);
    EXPECT_NE(DecideApproach(o), V::Stall);
}

// dc-path-ends-short-livelock (budget end): once the final-approach retry budget
// is spent, a landlocked dead-end stalls (surfaces `dc skip`) rather than
// retrying forever.
TEST(DcApproachRegression, PathEndsShortStallsAtBudget)
{
    Observation o = Healthy();
    o.hopDone             = true;
    o.engageDist          = 30.0f;
    o.doneNotEngagedTicks = DONE_NOT_ENGAGED_LIMIT;  // budget exhausted
    o.waterBetween        = false;                   // no swim escape
    EXPECT_EQ(DecideApproach(o), V::Stall);
}

// dc-direct-pursuit-freeze: a live boss in LOS just outside pull range, MoveTo
// can't path (INVALID_HEIGHT / raw 74-cap). While the give-up latch is still
// open the action pursues directly.
TEST(DcApproachRegression, DirectPursuitWhileLatchOpen)
{
    Observation o = Healthy();
    o.canPursue        = true;
    o.pursuitFailTicks = 2;          // 2 < 5 limit
    EXPECT_EQ(DecideApproach(o), V::Pursue);
}

// dc-direct-pursuit-freeze (latch fix): the freeze was pursuit looping with no
// escalation. At the fail limit the latch trips, pursuit stands down, and the
// long-path machinery takes over (here a healthy window) — no freeze.
TEST(DcApproachRegression, DirectPursuitLatchHandsOffToLongPath)
{
    Observation o = Healthy();
    o.canPursue        = true;
    o.pursuitFailTicks = PURSUIT_FAIL_LIMIT;  // 5: latch tripped
    EXPECT_EQ(DecideApproach(o), V::IssueSplineWindow);
    EXPECT_NE(DecideApproach(o), V::Pursue);
}

// dc-async-pathfinding: an async long-path build in flight must read as "wait
// quietly", NOT a genuine unreachable that stalls. The async branch is EXPECTED
// state, not failure.
TEST(DcApproachRegression, AsyncPendingWaitsNotStall)
{
    Observation o = Healthy();
    o.pathReachable = false;
    o.asyncPending  = true;
    EXPECT_EQ(DecideApproach(o), V::PlanRouteWait);
    EXPECT_NE(DecideApproach(o), V::Stall);
}

// dc-water-swim-legs: a submerged tunnel leaves no navmesh route (lakebed
// discarded), but water lies between — take the swim leg instead of stalling.
TEST(DcApproachRegression, UnreachableWithWaterSwims)
{
    Observation o = Healthy();
    o.pathReachable = false;
    o.asyncPending  = false;
    o.waterBetween  = true;
    EXPECT_EQ(DecideApproach(o), V::Swim);
}

// boss-state-index / engage handoff: when the route completes already inside
// engage range, the benign case is a rebuild-and-yield (let the engage trigger
// take the tick), never an escalation.
TEST(DcApproachRegression, HopDoneInsideEngageRangeYields)
{
    Observation o = Healthy();
    o.hopDone    = true;
    o.engageDist = o.engageRange - 2.0f;  // inside
    EXPECT_EQ(DecideApproach(o), V::RebuildAndYield);
}
