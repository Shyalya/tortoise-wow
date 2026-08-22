/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <limits>

#include "Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h"
#include "TestRun/DcTestPlan.h"

using DcTestPlan::Counters;
using DcTestPlan::DriverWait;
using DcTestPlan::DriverWaitVerdict;
using DcTestPlan::IsFinished;
using DcTestPlan::LaunchesWanted;
using DcTestPlan::ParseResult;
using DcTestPlan::ParseStartArgs;
using DcTestPlan::Spec;

namespace
{
    Spec MakeSpec(std::uint32_t total, std::uint32_t concurrent)
    {
        Spec s;
        s.dungeonToken = "old-hillsbrad";
        s.total = total;
        s.concurrent = concurrent;
        return s;
    }

    constexpr std::uint32_t kUnlimited = std::numeric_limits<std::uint32_t>::max();
}

// ---- LaunchesWanted --------------------------------------------------------------

TEST(DcTestPlanScheduleTest, FreshPlanWantsUpToConcurrent)
{
    EXPECT_EQ(LaunchesWanted(MakeSpec(20, 5), Counters{}, kUnlimited, 0), 5u);
}

TEST(DcTestPlanScheduleTest, ActiveRunsReducePlanHeadroom)
{
    Counters c;
    c.launched = 3;
    c.activeNow = 3;
    EXPECT_EQ(LaunchesWanted(MakeSpec(20, 5), c, kUnlimited, 0), 2u);
}

TEST(DcTestPlanScheduleTest, GlobalHeadroomCaps)
{
    EXPECT_EQ(LaunchesWanted(MakeSpec(20, 5), Counters{}, 2, 0), 2u);
    EXPECT_EQ(LaunchesWanted(MakeSpec(20, 5), Counters{}, 0, 0), 0u);
}

TEST(DcTestPlanScheduleTest, RemainingBudgetCaps)
{
    Counters c;
    c.launched = 19;  // one launch left of 20, plenty of concurrency headroom
    c.succeeded = 15;
    c.failed = 3;
    c.activeNow = 1;
    EXPECT_EQ(LaunchesWanted(MakeSpec(20, 5), c, kUnlimited, 0), 1u);
}

TEST(DcTestPlanScheduleTest, FullyLaunchedWantsZeroWhileDraining)
{
    Counters c;
    c.launched = 20;
    c.succeeded = 16;
    c.failed = 2;
    c.activeNow = 2;
    EXPECT_EQ(LaunchesWanted(MakeSpec(20, 5), c, kUnlimited, 0), 0u);
}

TEST(DcTestPlanScheduleTest, BackoffBlocksAllLaunches)
{
    EXPECT_EQ(LaunchesWanted(MakeSpec(20, 5), Counters{}, kUnlimited, 1), 0u);
}

TEST(DcTestPlanScheduleTest, OverLaunchedNeverUnderflows)
{
    // launched somehow beyond total (e.g. total lowered mid-flight) must clamp
    // to zero, not wrap.
    Counters c;
    c.launched = 25;
    c.activeNow = 6;
    EXPECT_EQ(LaunchesWanted(MakeSpec(20, 5), c, kUnlimited, 0), 0u);
}

// ---- IsFinished ------------------------------------------------------------------

TEST(DcTestPlanScheduleTest, FinishedWhenTotalCompletedAndDrained)
{
    Counters c;
    c.launched = 20;
    c.succeeded = 17;
    c.failed = 3;
    EXPECT_TRUE(IsFinished(MakeSpec(20, 5), c, false));
}

TEST(DcTestPlanScheduleTest, NotFinishedWhileChildrenActive)
{
    Counters c;
    c.launched = 20;
    c.succeeded = 17;
    c.failed = 1;
    c.activeNow = 2;
    EXPECT_FALSE(IsFinished(MakeSpec(20, 5), c, false));
    EXPECT_FALSE(IsFinished(MakeSpec(20, 5), c, true));  // stopping still drains
}

TEST(DcTestPlanScheduleTest, StoppedPlanFinishesShortOfTotal)
{
    Counters c;
    c.launched = 4;
    c.succeeded = 3;
    c.failed = 1;
    EXPECT_TRUE(IsFinished(MakeSpec(20, 5), c, true));
    EXPECT_FALSE(IsFinished(MakeSpec(20, 5), c, false));  // mid-launch gap, keep going
}

// ---- ParseStartArgs --------------------------------------------------------------

TEST(DcTestPlanParseTest, FullArgLine)
{
    ParseResult const r =
        ParseStartArgs("old-hillsbrad total=20 concurrent=5 level=68 seed=7");
    ASSERT_TRUE(r.ok) << r.err;
    EXPECT_EQ(r.spec.dungeonToken, "old-hillsbrad");
    EXPECT_EQ(r.spec.total, 20u);
    EXPECT_EQ(r.spec.concurrent, 5u);
    EXPECT_EQ(r.spec.level, 68u);
    EXPECT_EQ(r.spec.seedBase, 7u);
}

TEST(DcTestPlanParseTest, MinimalArgLineDefaultsRest)
{
    ParseResult const r = ParseStartArgs("deadmines total=10");
    ASSERT_TRUE(r.ok) << r.err;
    EXPECT_EQ(r.spec.dungeonToken, "deadmines");
    EXPECT_EQ(r.spec.total, 10u);
    EXPECT_EQ(r.spec.concurrent, 0u);
    EXPECT_EQ(r.spec.level, 0u);
    EXPECT_FALSE(r.spec.heroic);
    EXPECT_EQ(r.spec.seedBase, 0u);
}

TEST(DcTestPlanParseTest, HeroicBareWord)
{
    // `heroic` is a bare-word flag, position-independent, and never mistaken
    // for the dungeon token.
    ParseResult const r = ParseStartArgs("ramparts heroic total=10");
    ASSERT_TRUE(r.ok) << r.err;
    EXPECT_EQ(r.spec.dungeonToken, "ramparts");
    EXPECT_TRUE(r.spec.heroic);

    ParseResult const r2 = ParseStartArgs("heroic ramparts total=10");
    ASSERT_TRUE(r2.ok) << r2.err;
    EXPECT_EQ(r2.spec.dungeonToken, "ramparts");
    EXPECT_TRUE(r2.spec.heroic);

    // Still exactly one dungeon token allowed alongside the flag.
    EXPECT_FALSE(ParseStartArgs("deadmines heroic wailing total=10").ok);
}

TEST(DcTestPlanParseTest, MissingTotalFails)
{
    ParseResult const r = ParseStartArgs("deadmines");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.err.find("total=N"), std::string::npos);
}

TEST(DcTestPlanParseTest, MissingDungeonFails)
{
    EXPECT_FALSE(ParseStartArgs("").ok);
    EXPECT_FALSE(ParseStartArgs("total=10").ok);
}

TEST(DcTestPlanParseTest, SecondBareWordFails)
{
    EXPECT_FALSE(ParseStartArgs("deadmines wailing total=10").ok);
}

TEST(DcTestPlanParseTest, MalformedValueFails)
{
    EXPECT_FALSE(ParseStartArgs("deadmines total=abc").ok);
    EXPECT_FALSE(ParseStartArgs("deadmines total=").ok);
    EXPECT_FALSE(ParseStartArgs("deadmines total=10x").ok);
}

TEST(DcTestPlanParseTest, GearOptionsTakeWordsAsWellAsNumbers)
{
    ParseResult const r = ParseStartArgs("mechanar total=5 ilvl=141 quality=epic");
    ASSERT_TRUE(r.ok) << r.err;
    EXPECT_EQ(r.spec.gear.ilvl, 141);
    EXPECT_EQ(r.spec.gear.quality, 4u);

    // "none" removes the cap for the plan's runs; the numeric parse below the
    // gear branch would have rejected the word outright.
    ParseResult const none = ParseStartArgs("mechanar total=5 ilvl=none");
    ASSERT_TRUE(none.ok) << none.err;
    EXPECT_EQ(none.spec.gear.ilvl, DcTestGearTiers::kNoLimit);

    // Omitted = inherit the AutoGear* conf values.
    ParseResult const bare = ParseStartArgs("mechanar total=5");
    ASSERT_TRUE(bare.ok) << bare.err;
    EXPECT_TRUE(bare.spec.gear.IsDefault());
}

TEST(DcTestPlanParseTest, BadGearValuesFail)
{
    EXPECT_FALSE(ParseStartArgs("mechanar total=5 ilvl=abc").ok);
    EXPECT_FALSE(ParseStartArgs("mechanar total=5 ilvl=99999").ok);
    EXPECT_FALSE(ParseStartArgs("mechanar total=5 quality=shiny").ok);
    EXPECT_FALSE(ParseStartArgs("mechanar total=5 quality=9").ok);
}

TEST(DcTestPlanParseTest, UnknownOptionFails)
{
    ParseResult const r = ParseStartArgs("deadmines total=10 bogus=1");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.err.find("bogus"), std::string::npos);
}

// ---- DriverWaitVerdict -----------------------------------------------------------
//
// A console/dashboard plan start registers the plan on the same click that
// kicks the headless driver's login off, so "no issuing GM yet" is the normal
// first-tick state — the scheduler must wait it out rather than fail the plan.

TEST(DcTestPlanDriverWaitTest, WaitsWhileLoginIsInFlight)
{
    EXPECT_EQ(DriverWaitVerdict(true, 0, 120000), DriverWait::Wait);
    EXPECT_EQ(DriverWaitVerdict(true, 119999, 120000), DriverWait::Wait);
}

TEST(DcTestPlanDriverWaitTest, AbortsOnceTheWaitCapIsReached)
{
    EXPECT_EQ(DriverWaitVerdict(true, 120000, 120000), DriverWait::Abort);
    EXPECT_EQ(DriverWaitVerdict(true, 999999, 120000), DriverWait::Abort);
}

// An unresolvable driver (no such character, empty config name) can't be fixed
// by retrying, so it fails the plan immediately instead of parking it.
TEST(DcTestPlanDriverWaitTest, AbortsImmediatelyWhenTheDriverCannotComeUp)
{
    EXPECT_EQ(DriverWaitVerdict(false, 0, 120000), DriverWait::Abort);
}

TEST(DcTestPlanDriverWaitTest, ZeroCapDisablesWaiting)
{
    EXPECT_EQ(DriverWaitVerdict(true, 0, 0), DriverWait::Abort);
}

// ---- Harness cap defaults --------------------------------------------------------
//
// The test harness deliberately imposes no ceiling of its own: how many runs
// and plans the box can field is a property of the box (AiPlayerbot.MaxAddedBots,
// the addclass pool, CPU), and those refuse an over-budget start by name. A
// harness-local cap only ever refused starts the machine could have served, so
// all three ship at 0 = unlimited. These guards fail if a default drifts back to
// a positive number — the operator opts a cap back in via the conf, not the code.

TEST(DcSettingsRegistryTest, TestRunCapsDefaultToUnlimited)
{
    for (char const* key : {"TestRun.MaxConcurrent", "TestRun.MaxPlans",
                            "TestRun.Plan.MaxTotal"})
    {
        DcSettingDef const* d = FindDcSetting(key);
        ASSERT_NE(d, nullptr) << key;
        EXPECT_EQ(d->defVal, 0.0) << key << " must default to 0 (= unlimited)";
        // 0 has to survive the clamp, or "unlimited" is unreachable.
        EXPECT_EQ(d->minVal, 0.0) << key << " must admit 0";
        EXPECT_FALSE(d->playerFacing) << key << " is server-only";
    }
}
