/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Unit tests for the shared progress watchdog (nav review F11). Pins the two
// progress signals the route-glide / door-walk-in wedge detectors and the swim
// leg were consolidated onto, at the exact thresholds those sites use, so the
// consolidation stays behavior-preserving.
//
// Also covers DcApproachState's blocked-state DOOR watchdog (last section) —
// a different clock with the same job: decide when a hold has gone on long
// enough to be a failure rather than a wait.

#include "gtest/gtest.h"
#include "DcProgressWatchdog.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"

namespace
{
    constexpr float MIN_MOVE  = 0.5f;  // DC_STUCK_DISPLACEMENT
    constexpr float MIN_CLOSE = 0.5f;  // swim closing-distance epsilon
}

// ---- TickDisplacement (route glide / door walk-in wedge) -----------------

TEST(DcProgressWatchdog, DisplacementCountsMovingButWedgedTicks)
{
    DcProgressWatchdog w;
    // Moving yet barely displaced == a wedge tick; the counter accumulates.
    EXPECT_EQ(w.TickDisplacement(true, 0.1f, MIN_MOVE), 1u);
    EXPECT_EQ(w.TickDisplacement(true, 0.0f, MIN_MOVE), 2u);
    EXPECT_EQ(w.TickDisplacement(true, 0.49f, MIN_MOVE), 3u);
}

TEST(DcProgressWatchdog, DisplacementRealMovementResets)
{
    DcProgressWatchdog w;
    w.TickDisplacement(true, 0.1f, MIN_MOVE);
    w.TickDisplacement(true, 0.1f, MIN_MOVE);
    ASSERT_EQ(w.stuckTicks, 2u);
    // A tick displaced at/over the threshold is real progress -> reset.
    EXPECT_EQ(w.TickDisplacement(true, MIN_MOVE, MIN_MOVE), 0u);
}

TEST(DcProgressWatchdog, DisplacementNotMovingResets)
{
    DcProgressWatchdog w;
    w.TickDisplacement(true, 0.0f, MIN_MOVE);
    w.TickDisplacement(true, 0.0f, MIN_MOVE);
    ASSERT_EQ(w.stuckTicks, 2u);
    // isMoving()==false (a spline micro-stop) is NOT a wedge -> reset. This is
    // the door-walk-in stutter fix and matches the original posStuck.
    EXPECT_EQ(w.TickDisplacement(false, 0.0f, MIN_MOVE), 0u);
}

TEST(DcProgressWatchdog, DisplacementFirstUnsampledTickIsNotAWedge)
{
    DcProgressWatchdog w;
    // The callers pass moving=false on the first, un-baselined tick (the old
    // (0,0,0) lastPos sentinel) so it can never read as a wedge.
    EXPECT_EQ(w.TickDisplacement(false, 0.0f, MIN_MOVE), 0u);
}

// ---- TickClosing (swim leg) ----------------------------------------------

TEST(DcProgressWatchdog, ClosingFirstSampleArmsAndIsProgress)
{
    DcProgressWatchdog w;
    EXPECT_TRUE(w.TickClosing(40.0f, MIN_CLOSE, 1000u));
    EXPECT_FLOAT_EQ(w.bestDist, 40.0f);
    EXPECT_EQ(w.lastProgressMs, 1000u);
}

TEST(DcProgressWatchdog, ClosingImprovementBeyondEpsilonIsProgress)
{
    DcProgressWatchdog w;
    w.TickClosing(40.0f, MIN_CLOSE, 1000u);
    EXPECT_TRUE(w.TickClosing(39.0f, MIN_CLOSE, 2000u));  // 1.0 closer > 0.5
    EXPECT_FLOAT_EQ(w.bestDist, 39.0f);
    EXPECT_EQ(w.lastProgressMs, 2000u);
}

TEST(DcProgressWatchdog, ClosingTinyImprovementIsNotProgress)
{
    DcProgressWatchdog w;
    w.TickClosing(40.0f, MIN_CLOSE, 1000u);
    // Only 0.4yd closer (< 0.5 epsilon): no progress, best/clock unchanged so the
    // stale timer keeps running.
    EXPECT_FALSE(w.TickClosing(39.6f, MIN_CLOSE, 2000u));
    EXPECT_FLOAT_EQ(w.bestDist, 40.0f);
    EXPECT_EQ(w.lastProgressMs, 1000u);
}

TEST(DcProgressWatchdog, ClosingMovingAwayIsNotProgress)
{
    DcProgressWatchdog w;
    w.TickClosing(40.0f, MIN_CLOSE, 1000u);
    EXPECT_FALSE(w.TickClosing(45.0f, MIN_CLOSE, 2000u));
    EXPECT_FLOAT_EQ(w.bestDist, 40.0f);  // best is the global minimum
    EXPECT_EQ(w.lastProgressMs, 1000u);
}

TEST(DcProgressWatchdog, ClosingTracksGlobalMinimumAcrossPoints)
{
    DcProgressWatchdog w;
    // Approach the first point down to 5yd...
    w.TickClosing(40.0f, MIN_CLOSE, 1000u);
    w.TickClosing(5.0f, MIN_CLOSE, 2000u);
    ASSERT_FLOAT_EQ(w.bestDist, 5.0f);
    // ...cursor advances, distance to the NEW point jumps to 12yd. That is not
    // nearer than the 5yd global best, so it is not progress (matches the
    // original leg-wide lastDistToPoint), and the stale clock keeps its stamp.
    EXPECT_FALSE(w.TickClosing(12.0f, MIN_CLOSE, 3000u));
    EXPECT_EQ(w.lastProgressMs, 2000u);
    // Swimming past the old min re-registers progress and re-stamps the clock.
    EXPECT_TRUE(w.TickClosing(4.0f, MIN_CLOSE, 4000u));
    EXPECT_EQ(w.lastProgressMs, 4000u);
}

// ---- TickClosing tick budget (pursuit / final approach) ------------------

TEST(DcProgressWatchdog, ClosingNoProgressIncrementsStuckTicks)
{
    DcProgressWatchdog w;
    ASSERT_TRUE(w.TickClosing(40.0f, MIN_CLOSE, 1000u));  // arms, stuck stays 0
    EXPECT_EQ(w.stuckTicks, 0u);
    // Not getting nearer (frozen bot, or bee-line grinding a corner) accrues the
    // tick budget the pursuit / final-approach latches read.
    EXPECT_FALSE(w.TickClosing(40.0f, MIN_CLOSE, 1100u));
    EXPECT_EQ(w.stuckTicks, 1u);
    EXPECT_FALSE(w.TickClosing(40.2f, MIN_CLOSE, 1200u));  // 0.2 < 0.5 epsilon
    EXPECT_EQ(w.stuckTicks, 2u);
}

TEST(DcProgressWatchdog, ClosingProgressResetsStuckTicks)
{
    DcProgressWatchdog w;
    w.TickClosing(40.0f, MIN_CLOSE, 1000u);
    w.TickClosing(40.0f, MIN_CLOSE, 1100u);
    w.TickClosing(40.0f, MIN_CLOSE, 1200u);
    ASSERT_EQ(w.stuckTicks, 2u);
    // A real gain toward the boss clears the latch.
    EXPECT_TRUE(w.TickClosing(38.0f, MIN_CLOSE, 1300u));
    EXPECT_EQ(w.stuckTicks, 0u);
}

// ---- Reset ---------------------------------------------------------------

TEST(DcProgressWatchdog, ResetClearsEverything)
{
    DcProgressWatchdog w;
    w.TickDisplacement(true, 0.0f, MIN_MOVE);
    w.TickClosing(10.0f, MIN_CLOSE, 500u);
    w.Reset();
    EXPECT_EQ(w.stuckTicks, 0u);
    EXPECT_LT(w.bestDist, 0.0f);       // unset sentinel
    EXPECT_EQ(w.lastProgressMs, 0u);
    // After Reset the next closing sample re-arms as a fresh first sample.
    EXPECT_TRUE(w.TickClosing(99.0f, MIN_CLOSE, 600u));
}

// ---- Blocked-state door watchdog (DcApproachState::ObserveDoorStall) ------
//
// The door-blocked action's give-up clock. Only its ARRIVAL park observes it;
// the walk-in failure parks (which fire anywhere along the up-to-80yd approach)
// must not, or the budget goes on travel time and the run auto-pauses at a door
// it never touched. Scholomance batch tp-20260815-132009-1 lost two runs that
// way with zero Use() calls behind either pause.

namespace
{
    constexpr uint32 REARM_MS   = 10000;  // DC_DOOR_STALL_REARM_MS
    constexpr uint32 TIMEOUT_MS = 5000;   // DoorBlockedTimeout default, 5s

    // Scholomance's Iron Gates, by their real spawn low-guids. Entry is
    // irrelevant here — the watchdog keys on GUID identity only.
    ObjectGuid DoorGuid(ObjectGuid::LowType low)
    {
        return ObjectGuid(HighGuid::GameObject, /*entry*/ 175612, low);
    }
}

TEST(DcDoorStallWatchdog, ArmsOnFirstObservationAndTimesOutAtTheBudget)
{
    DcApproachState s;
    ObjectGuid const gate = DoorGuid(40);
    EXPECT_FALSE(s.ObserveDoorStall(gate, 1000u, REARM_MS, TIMEOUT_MS));
    EXPECT_EQ(s.doorStallGuid, gate);
    EXPECT_EQ(s.doorStallSinceMs, 1000u);
    // Still inside the window.
    EXPECT_FALSE(s.ObserveDoorStall(gate, 1000u + TIMEOUT_MS - 1, REARM_MS, TIMEOUT_MS));
    // Budget spent working the SAME door -> give up.
    EXPECT_TRUE(s.ObserveDoorStall(gate, 1000u + TIMEOUT_MS, REARM_MS, TIMEOUT_MS));
}

TEST(DcDoorStallWatchdog, ADifferentDoorStartsAFreshWindow)
{
    DcApproachState s;
    s.ObserveDoorStall(DoorGuid(40), 1000u, REARM_MS, TIMEOUT_MS);
    // The corridor's next gate, observed after the first one's budget would
    // have expired: it gets its own full window, not the predecessor's accrual.
    EXPECT_FALSE(s.ObserveDoorStall(DoorGuid(32), 9000u, REARM_MS, TIMEOUT_MS));
    EXPECT_EQ(s.doorStallSinceMs, 9000u);
}

TEST(DcDoorStallWatchdog, AnObservationGapRearmsTheWindow)
{
    DcApproachState s;
    ObjectGuid const gate = DoorGuid(40);
    s.ObserveDoorStall(gate, 1000u, REARM_MS, TIMEOUT_MS);
    // The run moved on and came back (a fight, a loot pass, a re-route). A gap
    // of at least REARM_MS means the previous stall ended, so the door gets a
    // clean budget rather than instantly timing out on stale accrual.
    EXPECT_FALSE(s.ObserveDoorStall(gate, 1000u + REARM_MS, REARM_MS, TIMEOUT_MS));
    EXPECT_EQ(s.doorStallSinceMs, 1000u + REARM_MS);
}

TEST(DcDoorStallWatchdog, GapUnderTheRearmKeepsAccruingWhenTheDoorNeverOpens)
{
    DcApproachState s;
    ObjectGuid const gate = DoorGuid(40);
    s.ObserveDoorStall(gate, 1000u, REARM_MS, TIMEOUT_MS);
    // A short gap in observations on its own proves nothing — the bot may simply
    // have missed a tick. Without an explicit "the door opened" signal the window
    // keeps accruing, so a bot genuinely livelocked on a door still times out.
    EXPECT_FALSE(s.ObserveDoorStall(gate, 4000u, REARM_MS, TIMEOUT_MS));
    EXPECT_EQ(s.doorStallSinceMs, 1000u);
    EXPECT_TRUE(s.ObserveDoorStall(gate, 6000u, REARM_MS, TIMEOUT_MS));
}

TEST(DcDoorStallWatchdog, SeeingTheDoorOpenEndsTheWindow)
{
    // The tr-20260816-151006-14 regression. Stratholme's King's Square Gate
    // (175352) carries door.autoCloseTime 3000: the tank clicks it, walks
    // through, and ~3s later it re-shuts and re-flags. Three of those cycles
    // are three SUCCESSFUL opens, but the gaps between arrival parks are ~3s —
    // far under REARM_MS — so the raw gap rule stitched them into one 7s stall
    // and tripped the 5s budget on a gate the bot was opening every time. The
    // run auto-paused 27.9yd from Hearthsinger Forresten with 8/13 bosses down.
    //
    // ClearDoorStall is the missing signal: the door-blocked action calls it the
    // moment the GO reads open, and the blocking-door value calls it whenever
    // the flagged blocker changes or clears.
    DcApproachState s;
    ObjectGuid const gate = DoorGuid(98);

    // Cycle 1: park, click, gate swings open.
    EXPECT_FALSE(s.ObserveDoorStall(gate, 1000u, REARM_MS, TIMEOUT_MS));
    s.ClearDoorStall();
    EXPECT_EQ(s.doorStallGuid, ObjectGuid::Empty);
    EXPECT_EQ(s.doorStallSinceMs, 0u);
    EXPECT_EQ(s.doorStallLastMs, 0u);

    // Cycle 2: it auto-closed 3s later. Fresh window, not 3s of stale accrual.
    EXPECT_FALSE(s.ObserveDoorStall(gate, 4000u, REARM_MS, TIMEOUT_MS));
    EXPECT_EQ(s.doorStallSinceMs, 4000u);
    s.ClearDoorStall();

    // Cycle 3: the tick that used to auto-pause the run (7s past cycle 1).
    EXPECT_FALSE(s.ObserveDoorStall(gate, 8000u, REARM_MS, TIMEOUT_MS));
    EXPECT_EQ(s.doorStallSinceMs, 8000u);

    // And the give-up clock still works on the door that stops opening: no
    // ClearDoorStall this time, so the budget runs out as designed.
    EXPECT_TRUE(s.ObserveDoorStall(gate, 8000u + TIMEOUT_MS, REARM_MS, TIMEOUT_MS));
}

TEST(DcDoorStallWatchdog, UnobservedApproachTicksNeverAccrue)
{
    // The regression itself, expressed on the state: the bot walks the corridor
    // for well over the timeout while the approach parks decline to observe.
    // Arrival must then get a FULL window — under the old code the walk-in's
    // parks had already armed and spent it, and the arrival tick auto-paused
    // without ever clicking.
    DcApproachState s;
    ObjectGuid const gate = DoorGuid(40);
    // 60s of approach: not one observation.
    EXPECT_EQ(s.doorStallGuid, ObjectGuid::Empty);
    EXPECT_EQ(s.doorStallSinceMs, 0u);
    // Arrival.
    EXPECT_FALSE(s.ObserveDoorStall(gate, 60000u, REARM_MS, TIMEOUT_MS));
    EXPECT_EQ(s.doorStallSinceMs, 60000u);
    // A click's worth of ticks still inside the fresh budget.
    EXPECT_FALSE(s.ObserveDoorStall(gate, 62500u, REARM_MS, TIMEOUT_MS));
}

TEST(DcDoorStallWatchdog, OnBossChangeClearsTheStall)
{
    DcApproachState s;
    ObjectGuid const gate = DoorGuid(40);
    s.ObserveDoorStall(gate, 1000u, REARM_MS, TIMEOUT_MS);
    s.OnBossChange(10503);
    EXPECT_EQ(s.doorStallGuid, ObjectGuid::Empty);
    EXPECT_EQ(s.doorStallSinceMs, 0u);
    EXPECT_EQ(s.doorStallLastMs, 0u);
}
