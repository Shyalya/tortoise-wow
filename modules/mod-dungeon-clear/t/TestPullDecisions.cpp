/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Edge-pinning unit tests for the Dynamic-pull governor decision core
// (DcPullDecision::DecidePull). The live planner routes every commit through
// this function, so these tests are the regression net for the governor's
// arbitration: the no-target grace, the in-combat / non-Idle lock, the same-pack
// UPGRADE-only latch + throttle, and the patrol-wait gate's three outcomes.

#include "gtest/gtest.h"
#include "DcPullDecision.h"

using DcPullDecision::DecidePull;
using DcPullDecision::PullObservation;
using DcPullDecision::PullVerdict;

namespace
{
    // A target-present, gates-passed, plain-Leeroy baseline. Individual tests
    // flip exactly the fields they exercise.
    PullObservation Base()
    {
        PullObservation o;
        o.hasTarget = true;
        o.inCombat = false;
        o.phaseIdle = true;
        o.hasStandingVerdict = false;
        o.verdictGraceExpired = false;
        o.sameTarget = false;
        o.currentlyAdvanced = false;
        o.recheckElapsed = true;
        o.advanced = false;
        o.patrolWaitEnabled = false;
        o.atCommitRange = false;
        o.patrolContended = false;
        o.patrolWaitExpired = false;
        return o;
    }
}

// ---- No-target branch ------------------------------------------------------

TEST(DcPullDecision, NoTargetNoStandingVerdictDropsToLeeroy)
{
    PullObservation o = Base();
    o.hasTarget = false;
    o.hasStandingVerdict = false;
    EXPECT_EQ(DecidePull(o), PullVerdict::DropToLeeroy);
}

TEST(DcPullDecision, NoTargetWithinGraceHolds)
{
    PullObservation o = Base();
    o.hasTarget = false;
    o.hasStandingVerdict = true;
    o.verdictGraceExpired = false;
    EXPECT_EQ(DecidePull(o), PullVerdict::HoldNoTarget);
}

TEST(DcPullDecision, NoTargetGraceExpiredDrops)
{
    PullObservation o = Base();
    o.hasTarget = false;
    o.hasStandingVerdict = true;
    o.verdictGraceExpired = true;
    EXPECT_EQ(DecidePull(o), PullVerdict::DropToLeeroy);
}

// ---- In-combat / non-Idle lock --------------------------------------------

TEST(DcPullDecision, InCombatIsNoOp)
{
    PullObservation o = Base();
    o.inCombat = true;
    o.advanced = true;  // would otherwise commit Advanced
    EXPECT_EQ(DecidePull(o), PullVerdict::NoOp);
}

TEST(DcPullDecision, NonIdlePhaseIsNoOp)
{
    PullObservation o = Base();
    o.phaseIdle = false;
    EXPECT_EQ(DecidePull(o), PullVerdict::NoOp);
}

// ---- Same-pack latch / throttle -------------------------------------------

TEST(DcPullDecision, SameTargetAlreadyAdvancedIsLocked)
{
    PullObservation o = Base();
    o.sameTarget = true;
    o.currentlyAdvanced = true;
    o.advanced = false;  // a downgrade must NOT happen — stays locked
    EXPECT_EQ(DecidePull(o), PullVerdict::NoOp);
}

TEST(DcPullDecision, SameTargetThrottledIsNoOp)
{
    PullObservation o = Base();
    o.sameTarget = true;
    o.currentlyAdvanced = false;
    o.recheckElapsed = false;  // throttle window not yet passed
    o.advanced = true;
    EXPECT_EQ(DecidePull(o), PullVerdict::NoOp);
}

TEST(DcPullDecision, SameTargetThrottleElapsedUpgradesToAdvanced)
{
    PullObservation o = Base();
    o.sameTarget = true;
    o.currentlyAdvanced = false;
    o.recheckElapsed = true;
    o.advanced = true;
    EXPECT_EQ(DecidePull(o), PullVerdict::Advanced);
}

// ---- Plain commit ----------------------------------------------------------

TEST(DcPullDecision, PlainLeeroy)
{
    PullObservation o = Base();
    o.advanced = false;
    EXPECT_EQ(DecidePull(o), PullVerdict::Leeroy);
}

TEST(DcPullDecision, PlainAdvancedWithPatrolWaitOff)
{
    PullObservation o = Base();
    o.advanced = true;
    o.patrolWaitEnabled = false;
    EXPECT_EQ(DecidePull(o), PullVerdict::Advanced);
}

// ---- Patrol-wait gate (the three outcomes) --------------------------------

TEST(DcPullDecision, PatrolContendedApproachingWalksInAsLeeroy)
{
    PullObservation o = Base();
    o.advanced = true;
    o.patrolWaitEnabled = true;
    o.atCommitRange = false;     // still approaching
    o.patrolContended = true;
    EXPECT_EQ(DecidePull(o), PullVerdict::ApproachAsLeeroy);
}

TEST(DcPullDecision, PatrolContendedAtCommitHolds)
{
    PullObservation o = Base();
    o.advanced = true;
    o.patrolWaitEnabled = true;
    o.atCommitRange = true;
    o.patrolContended = true;
    o.patrolWaitExpired = false;  // still within the wait budget
    EXPECT_EQ(DecidePull(o), PullVerdict::PatrolWaitHold);
}

TEST(DcPullDecision, PatrolContendedAtCommitWaitExpiredCommits)
{
    PullObservation o = Base();
    o.advanced = true;
    o.patrolWaitEnabled = true;
    o.atCommitRange = true;
    o.patrolContended = true;
    o.patrolWaitExpired = true;   // budget ran out — proceed with the heavy verdict
    EXPECT_EQ(DecidePull(o), PullVerdict::Advanced);
}

TEST(DcPullDecision, AdvancedAtCommitNotContendedCommits)
{
    PullObservation o = Base();
    o.advanced = true;
    o.patrolWaitEnabled = true;
    o.atCommitRange = true;
    o.patrolContended = false;    // genuine heavy pull, not a lone patrol
    EXPECT_EQ(DecidePull(o), PullVerdict::Advanced);
}

TEST(DcPullDecision, AdvancedApproachingNotContendedCommits)
{
    PullObservation o = Base();
    o.advanced = true;
    o.patrolWaitEnabled = true;
    o.atCommitRange = false;
    o.patrolContended = false;
    EXPECT_EQ(DecidePull(o), PullVerdict::Advanced);
}

// ---------------------------------------------------------------------------
// DcPullContext::SafetyRelease — the camp-safety valve's per-phase policy.
// The valve wants "let them fight", not "abandon the maneuver": an in-flight
// drag is kept (aborting it fights the pack wherever the party happens to be
// standing — the over-pull the drag exists to prevent); only a pull still
// walking OUT to its pack aborts as before.
// ---------------------------------------------------------------------------

#include "Ai/Dungeon/DungeonClear/DcPullContext.h"

TEST(DcPullSafetyRelease, SafetyReleaseKeepsAReturningDrag)
{
    DcPullContext pull;
    pull.Transition(DcPullPhase::Returning, 1000u);
    EXPECT_FALSE(pull.SafetyRelease(2000u));
    EXPECT_EQ(pull.phase, DcPullPhase::Returning);  // the drag continues
    EXPECT_TRUE(pull.partyReleased);                // the party is freed
}

TEST(DcPullSafetyRelease, SafetyReleaseKeepsAFormingPull)
{
    DcPullContext pull;
    pull.Transition(DcPullPhase::Forming, 1000u);
    EXPECT_FALSE(pull.SafetyRelease(2000u));
    EXPECT_EQ(pull.phase, DcPullPhase::Forming);
    EXPECT_TRUE(pull.partyReleased);
}

TEST(DcPullSafetyRelease, SafetyReleaseAbortsAnAdvancingPull)
{
    DcPullContext pull;
    pull.Transition(DcPullPhase::Advancing, 1000u);
    EXPECT_TRUE(pull.SafetyRelease(2000u));
    EXPECT_EQ(pull.phase, DcPullPhase::Engage);
    // The flag survives the Engage transition so the reaper releases the party
    // at once (a safety release never waits out the graceful Engage delay).
    EXPECT_TRUE(pull.partyReleased);
    EXPECT_EQ(pull.phaseSince, 2000u);
}

TEST(DcPullSafetyRelease, SafetyReleaseIsANoOpOutsideAManeuver)
{
    DcPullContext pull;
    EXPECT_FALSE(pull.SafetyRelease(2000u));
    EXPECT_EQ(pull.phase, DcPullPhase::Idle);
    EXPECT_FALSE(pull.partyReleased);

    pull.Transition(DcPullPhase::Engage, 1000u);
    EXPECT_FALSE(pull.SafetyRelease(2000u));
    EXPECT_EQ(pull.phase, DcPullPhase::Engage);
    EXPECT_FALSE(pull.partyReleased);
}

TEST(DcPullSafetyRelease, NextManeuverClearsAStandingRelease)
{
    DcPullContext pull;
    pull.Transition(DcPullPhase::Returning, 1000u);
    pull.SafetyRelease(2000u);
    EXPECT_TRUE(pull.partyReleased);
    // The kept drag finishes: Returning -> Engage. The release must survive
    // (immediate strip, no graceful delay for an already-released party).
    pull.Transition(DcPullPhase::Engage, 3000u);
    EXPECT_TRUE(pull.partyReleased);
    // A NEW pull starting (Engage -> Forming) belongs to a fresh maneuver.
    pull.Transition(DcPullPhase::Forming, 4000u);
    EXPECT_FALSE(pull.partyReleased);
}

// ---------------------------------------------------------------------------
// DcPullContext::ClearDynamicVerdict — the stand-down teardown.
//
// Regression net for tr-20260817-100413-43/44/45 (Shattered Halls). The Dynamic
// governor (DcPullPlanner::UpdateDynamicPullMode) is the ONLY writer of these
// fields, so anything that stops it from running freezes the verdict instead of
// clearing it. DungeonClearPullModeCurrentValue stands the pull system down while
// a persistent anchored event drives — and it used to `return false` while leaving
// `decision == PatrolHold` latched. The pull trigger keeps its rung live on that
// code by design, and DcRel::Pull (35) outranks DcRel::AtObjective (30), so the
// event could never drive another step: three runs stalled 913s in the assassin
// hallway. Every field the governor latches has to go, not just `decision`.
// ---------------------------------------------------------------------------

TEST(DcPullStandDown, ClearDynamicVerdictDropsAPatrolHold)
{
    DcPullContext pull;
    pull.decision = DcPullDecisionCode::PatrolHold;
    pull.decisionTarget = ObjectGuid(uint64(0xF130004123456789ull));
    pull.decisionSince = 5000u;
    pull.patrolWaitSince = 4000u;

    pull.ClearDynamicVerdict();

    // The code the pull trigger reads to keep its rung live must be gone.
    EXPECT_EQ(pull.decision, DcPullDecisionCode::None);
    // ...and so must the per-pack latches, or the governor's own same-target
    // branch would resume mid-throttle against a pack it never re-classified.
    EXPECT_TRUE(pull.decisionTarget.IsEmpty());
    EXPECT_EQ(pull.decisionSince, 0u);
    EXPECT_EQ(pull.patrolWaitSince, 0u);
}

TEST(DcPullStandDown, ClearDynamicVerdictDropsAStandingAdvanced)
{
    DcPullContext pull;
    pull.decision = DcPullDecisionCode::Advanced;
    pull.decisionTarget = ObjectGuid(uint64(0xF130004123456789ull));
    pull.targetLostSince = 7000u;

    pull.ClearDynamicVerdict();

    EXPECT_EQ(pull.decision, DcPullDecisionCode::None);
    EXPECT_TRUE(pull.decisionTarget.IsEmpty());
    // The no-target grace latch too: a stand-down that ends with the pack gone
    // must not hand the governor a countdown that started before the event.
    EXPECT_EQ(pull.targetLostSince, 0u);
}

TEST(DcPullStandDown, ClearDynamicVerdictIsIdempotentAndLeavesTheManeuverAlone)
{
    DcPullContext pull;
    pull.Transition(DcPullPhase::Returning, 1000u);
    pull.pullTarget = ObjectGuid(uint64(0xF130004123456789ull));
    pull.decision = DcPullDecisionCode::PatrolHold;

    // Called on every tick the stand-down holds, so it must be cheap to repeat.
    pull.ClearDynamicVerdict();
    pull.ClearDynamicVerdict();
    EXPECT_EQ(pull.decision, DcPullDecisionCode::None);

    // It clears the VERDICT, not the maneuver: an in-flight drag and its per-pull
    // latches belong to the pull FSM, which stands down through its own phases.
    EXPECT_EQ(pull.phase, DcPullPhase::Returning);
    EXPECT_FALSE(pull.pullTarget.IsEmpty());
}
