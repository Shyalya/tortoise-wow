/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "TestRun/DcTestRunVerdict.h"

using DcTestRun::Classify;
using DcTestRun::Limits;
using DcTestRun::Observation;
using DcTestRun::Verdict;
using DcTestRun::VerdictName;

namespace
{
    // A healthy mid-run tick: nothing fired, everyone present, timers low.
    Observation Healthy()
    {
        Observation o;
        o.gmOnline = true;
        o.elapsedMs = 60 * 1000;
        o.sinceProgressMs = 30 * 1000;
        return o;
    }

    Limits DefaultLimits() { return Limits{}; }
}

// ---- the one continuing shape ---------------------------------------------------

TEST(DcTestRunVerdictTest, HealthyRunContinues)
{
    EXPECT_EQ(Classify(Healthy(), DefaultLimits()), Verdict::Continue);
}

// ---- disable outcomes are authoritative -----------------------------------------

TEST(DcTestRunVerdictTest, DisableWithAllClearedIsSuccess)
{
    Observation o = Healthy();
    o.disableFired = true;
    o.disableAllCleared = true;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::Success);
}

TEST(DcTestRunVerdictTest, DisableWithOtherReasonIsFailDisabled)
{
    Observation o = Healthy();
    o.disableFired = true;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailDisabled);
}

TEST(DcTestRunVerdictTest, SuccessBeatsEverySimultaneousTimer)
{
    // Disable and every watchdog trip on the same tick — the run's own
    // verdict must win, or a success could be recorded as a timeout.
    Observation o;
    o.disableFired = true;
    o.disableAllCleared = true;
    o.abortRequested = true;
    o.leaderMissing = true;
    o.gmOnline = false;
    o.paused = true;
    o.pausedForMs = 10'000'000;
    o.stalled = true;
    o.stalledForMs = 10'000'000;
    o.sinceProgressMs = 10'000'000;
    o.elapsedMs = 10'000'000;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::Success);
}

// ---- wipe ------------------------------------------------------------------------

TEST(DcTestRunVerdictTest, WipeInsideItsGraceStillContinues)
{
    // A battle rez / soulstone gets the grace period to un-wipe the run.
    Observation o = Healthy();
    o.partyWiped = true;
    o.wipedForMs = DefaultLimits().wipeGraceMs - 1;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::Continue);
}

TEST(DcTestRunVerdictTest, WipePastItsGraceFails)
{
    Observation o = Healthy();
    o.partyWiped = true;
    o.wipedForMs = DefaultLimits().wipeGraceMs;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailPartyWiped);
}

TEST(DcTestRunVerdictTest, WipeBeatsTheWatchdogsItAlsoTrips)
{
    // The regression this rung exists for: a corpse party stops progressing,
    // so the no-progress net used to claim the run — reporting a livelock
    // where the truth was a wipe. Same for the pause/stall trackers, which a
    // dead party pins high too. Wipe must outrank all three.
    Observation o = Healthy();
    o.partyWiped = true;
    o.wipedForMs = 10'000'000;
    o.paused = true;
    o.pausedForMs = 10'000'000;
    o.stalled = true;
    o.stalledForMs = 10'000'000;
    o.sinceProgressMs = 10'000'000;
    o.elapsedMs = 10'000'000;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailPartyWiped);
}

TEST(DcTestRunVerdictTest, DisableStillOutranksAWipe)
{
    // The tank dying with a survivor left routes through the in-run death
    // bailout, which disables with the death reason; that verdict is richer
    // than a bare "wipe" and must not be overwritten if both land at once.
    Observation o = Healthy();
    o.disableFired = true;
    o.partyWiped = true;
    o.wipedForMs = 10'000'000;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailDisabled);
}

TEST(DcTestRunVerdictTest, AbortStillOutranksAWipe)
{
    Observation o = Healthy();
    o.abortRequested = true;
    o.partyWiped = true;
    o.wipedForMs = 10'000'000;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailAborted);
}

// ---- abort cluster ---------------------------------------------------------------

TEST(DcTestRunVerdictTest, AbortRequestFails)
{
    Observation o = Healthy();
    o.abortRequested = true;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailAborted);
}

TEST(DcTestRunVerdictTest, LeaderMissingFails)
{
    Observation o = Healthy();
    o.leaderMissing = true;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailAborted);
}

TEST(DcTestRunVerdictTest, GmOfflineFails)
{
    Observation o = Healthy();
    o.gmOnline = false;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailAborted);
}

TEST(DcTestRunVerdictTest, AbortBeatsPauseTimeout)
{
    Observation o = Healthy();
    o.abortRequested = true;
    o.paused = true;
    o.pausedForMs = 10'000'000;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailAborted);
}

// ---- pause grace -----------------------------------------------------------------

TEST(DcTestRunVerdictTest, PauseInsideGraceContinues)
{
    Observation o = Healthy();
    o.paused = true;
    o.pausedForMs = DefaultLimits().pauseGraceMs - 1;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::Continue);
}

TEST(DcTestRunVerdictTest, PauseAtGraceBoundaryFails)
{
    Observation o = Healthy();
    o.paused = true;
    o.pausedForMs = DefaultLimits().pauseGraceMs;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailPausedTimeout);
}

TEST(DcTestRunVerdictTest, UnpausedIgnoresPauseTimer)
{
    // pausedForMs is stale bookkeeping once the run resumed; paused=false
    // must gate it off entirely.
    Observation o = Healthy();
    o.paused = false;
    o.pausedForMs = 10'000'000;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::Continue);
}

// ---- stall grace -----------------------------------------------------------------

TEST(DcTestRunVerdictTest, StallInsideGraceContinues)
{
    Observation o = Healthy();
    o.stalled = true;
    o.stalledForMs = DefaultLimits().stallGraceMs - 1;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::Continue);
}

TEST(DcTestRunVerdictTest, StallAtGraceBoundaryFails)
{
    Observation o = Healthy();
    o.stalled = true;
    o.stalledForMs = DefaultLimits().stallGraceMs;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailStalledTimeout);
}

TEST(DcTestRunVerdictTest, PauseTimeoutBeatsStallTimeout)
{
    Observation o = Healthy();
    o.paused = true;
    o.pausedForMs = 10'000'000;
    o.stalled = true;
    o.stalledForMs = 10'000'000;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailPausedTimeout);
}

// ---- progress + overall timers ---------------------------------------------------

TEST(DcTestRunVerdictTest, NoProgressAtLimitFails)
{
    Observation o = Healthy();
    o.sinceProgressMs = DefaultLimits().noProgressMs;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailNoProgress);
}

TEST(DcTestRunVerdictTest, OverallTimeoutAtLimitFails)
{
    Observation o = Healthy();
    o.elapsedMs = DefaultLimits().overallTimeoutMs;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailOverallTimeout);
}

TEST(DcTestRunVerdictTest, NoProgressBeatsOverallTimeout)
{
    Observation o = Healthy();
    o.sinceProgressMs = 10'000'000;
    o.elapsedMs = 10'000'000;
    EXPECT_EQ(Classify(o, DefaultLimits()), Verdict::FailNoProgress);
}

TEST(DcTestRunVerdictTest, CustomLimitsAreHonored)
{
    Limits l;
    l.overallTimeoutMs = 1000;
    Observation o = Healthy();
    o.elapsedMs = 1000;
    o.sinceProgressMs = 0;
    EXPECT_EQ(Classify(o, l), Verdict::FailOverallTimeout);
}

// ---- names -----------------------------------------------------------------------

TEST(DcTestRunVerdictTest, EveryTerminalVerdictHasAStableName)
{
    EXPECT_STREQ(VerdictName(Verdict::Success), "success");
    EXPECT_STREQ(VerdictName(Verdict::FailDisabled), "disabled");
    EXPECT_STREQ(VerdictName(Verdict::FailPartyWiped), "wipe");
    EXPECT_STREQ(VerdictName(Verdict::FailPausedTimeout), "paused_timeout");
    EXPECT_STREQ(VerdictName(Verdict::FailStalledTimeout), "stalled_timeout");
    EXPECT_STREQ(VerdictName(Verdict::FailNoProgress), "no_progress");
    EXPECT_STREQ(VerdictName(Verdict::FailOverallTimeout), "overall_timeout");
    EXPECT_STREQ(VerdictName(Verdict::FailAborted), "aborted");
    EXPECT_STREQ(VerdictName(Verdict::Continue), "continue");
}
