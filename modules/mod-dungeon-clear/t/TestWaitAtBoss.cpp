/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcWaitAtBossDecision.h"

using DcWaitAtBossDecision::Decide;
using DcWaitAtBossDecision::Inputs;

namespace
{
    // The tank standing at a fresh boss with the feature ON — the one shape
    // that pauses. Every test flips one thing off this.
    Inputs PauseworthyInputs()
    {
        Inputs in;
        in.enabled = true;
        in.nextIsBoss = true;
        in.paused = false;
        in.inCombat = false;
        in.bossGuid = 0xF130001234000042ULL;
        in.lastWaitedGuid = 0;
        return in;
    }
}

// ---- the regression guard: OFF must be inert ------------------------------------

TEST(DcWaitAtBossTest, DisabledNeverPauses)
{
    Inputs in = PauseworthyInputs();
    in.enabled = false;
    EXPECT_FALSE(Decide(in).shouldAutoPause);
}

// ---- the one pausing shape -------------------------------------------------------

TEST(DcWaitAtBossTest, EnabledAtFreshBossPauses)
{
    EXPECT_TRUE(Decide(PauseworthyInputs()).shouldAutoPause);
}

// ---- once per boss ---------------------------------------------------------------

TEST(DcWaitAtBossTest, StampedBossDoesNotRePause)
{
    // Post-resume / post-`dc go` / wipe-re-approach: the stamp matches, so the
    // pull commits instead of pausing again.
    Inputs in = PauseworthyInputs();
    in.lastWaitedGuid = in.bossGuid;
    EXPECT_FALSE(Decide(in).shouldAutoPause);
}

TEST(DcWaitAtBossTest, DifferentBossReArms)
{
    Inputs in = PauseworthyInputs();
    in.lastWaitedGuid = 0xF130001234000041ULL;  // waited on the PREVIOUS boss
    EXPECT_TRUE(Decide(in).shouldAutoPause);
}

// ---- shapes that must never pause ------------------------------------------------

TEST(DcWaitAtBossTest, ObjectiveAnchorDoesNotPause)
{
    // Set-piece events (travel objectives, gauntlet anchors) flow uninterrupted.
    Inputs in = PauseworthyInputs();
    in.nextIsBoss = false;
    EXPECT_FALSE(Decide(in).shouldAutoPause);
}

TEST(DcWaitAtBossTest, AlreadyPausedDoesNotStack)
{
    Inputs in = PauseworthyInputs();
    in.paused = true;
    EXPECT_FALSE(Decide(in).shouldAutoPause);
}

TEST(DcWaitAtBossTest, InCombatDoesNotPause)
{
    Inputs in = PauseworthyInputs();
    in.inCombat = true;
    EXPECT_FALSE(Decide(in).shouldAutoPause);
}

TEST(DcWaitAtBossTest, UnresolvedBossGuidDoesNotPause)
{
    Inputs in = PauseworthyInputs();
    in.bossGuid = 0;
    EXPECT_FALSE(Decide(in).shouldAutoPause);
}
