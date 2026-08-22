/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "DcStrategyGate.h"

// The dungeon-gate decision kernel is the pure core of the invariant
// "DC strategies installed iff the bot is on a dungeon/raid map". It is
// deliberately free of game types so the truth table can be pinned headlessly;
// the runtime Reconcile()/ReconcileAllBots() wrappers (game-coupled) are
// exercised live, per the plan's validation checklist.
using DcStrategyGate::Action;
using DcStrategyGate::Decide;
using DcStrategyGate::MakePlan;
using DcStrategyGate::Plan;

// MakePlan argument order, spelled out once so the cases below stay readable:
//   (inDungeon, hasNonCombat, hasCombat, strayInCombat, strayInNonCombat)
// where a "stray" is a DC strategy installed in the engine it does not belong
// to — "dungeon clear" in the combat engine, or "dungeon clear combat" in the
// non-combat engine. See the header for how a bot gets there (issue #18).

TEST(DungeonClearStrategyGate, InstallsWhenInDungeonAndMissing)
{
    EXPECT_EQ(Decide(/*inDungeon*/ true, /*hasStrategy*/ false), Action::Install);
}

TEST(DungeonClearStrategyGate, StripsWhenOutsideDungeonButPresent)
{
    EXPECT_EQ(Decide(/*inDungeon*/ false, /*hasStrategy*/ true), Action::Strip);
}

TEST(DungeonClearStrategyGate, NoOpWhenAlreadyCompliantInDungeon)
{
    EXPECT_EQ(Decide(/*inDungeon*/ true, /*hasStrategy*/ true), Action::None);
}

TEST(DungeonClearStrategyGate, NoOpWhenAlreadyCompliantOutsideDungeon)
{
    EXPECT_EQ(Decide(/*inDungeon*/ false, /*hasStrategy*/ false), Action::None);
}

// The kernel is constexpr — fold the whole truth table at compile time so a
// regression can't even build.
static_assert(Decide(true, false) == Action::Install, "in dungeon, missing -> install");
static_assert(Decide(false, true) == Action::Strip, "out of dungeon, present -> strip");
static_assert(Decide(true, true) == Action::None, "in dungeon, present -> none");
static_assert(Decide(false, false) == Action::None, "out of dungeon, missing -> none");

// ---------------------------------------------------------------------------
// MakePlan: the two-engine plan, including cross-engine stray removal.
// ---------------------------------------------------------------------------

// The reporter's state in issue #18: outside a dungeon, no legitimate DC
// strategy anywhere, but "dungeon clear" wedged in the COMBAT engine — where it
// outranks the whole class rotation and idles the bot. The old gate probed only
// (nonCombat, NON_COMBAT) and (combat, COMBAT), so both probes read false, it
// took the already-compliant early return, and the stray survived every login.
TEST(DungeonClearStrategyGate, StripsNonCombatStrayFromCombatEngineOutsideDungeon)
{
    Plan const plan = MakePlan(false, false, false, /*strayInCombat*/ true, false);

    EXPECT_TRUE(plan.stripStrayInCombat);
    EXPECT_EQ(plan.nonCombat, Action::None);
    EXPECT_EQ(plan.combat, Action::None);
}

TEST(DungeonClearStrategyGate, StripsCombatStrayFromNonCombatEngineOutsideDungeon)
{
    Plan const plan = MakePlan(false, false, false, false, /*strayInNonCombat*/ true);

    EXPECT_TRUE(plan.stripStrayInNonCombat);
}

// A stray is wrong in a dungeon too — the invariant is per (strategy, engine),
// not "some DC strategy is installed somewhere".
TEST(DungeonClearStrategyGate, StripsStrayInsideDungeonWhileInstallingTheRealPair)
{
    Plan const plan = MakePlan(true, false, false, /*strayInCombat*/ true, false);

    EXPECT_TRUE(plan.stripStrayInCombat);
    EXPECT_EQ(plan.nonCombat, Action::Install);
    EXPECT_EQ(plan.combat, Action::Install);
}

// Teardown is phrased on the resulting state ("does any DC strategy survive?"),
// not on "was anything stripped" — so clearing a stray off a bot that is
// legitimately mid-run inside a dungeon must NOT abort the run.
TEST(DungeonClearStrategyGate, StrayRemovalDoesNotTearDownALiveRun)
{
    Plan const plan = MakePlan(true, /*hasNonCombat*/ true, /*hasCombat*/ true,
                               /*strayInCombat*/ true, false);

    EXPECT_TRUE(plan.stripStrayInCombat);
    EXPECT_FALSE(plan.teardown);
}

// Conversely, a bot whose only DC residue was the stray keeps nothing, so its
// run state (leader enabled flag, follower MoveFollow) must be torn down.
TEST(DungeonClearStrategyGate, StrayOnlyBotTearsDownRunState)
{
    EXPECT_TRUE(MakePlan(false, false, false, /*strayInCombat*/ true, false).teardown);
}

// The pre-existing teardown cases must be unchanged: leaving a dungeon with the
// real pair installed tears down; a compliant bot never does.
TEST(DungeonClearStrategyGate, LeavingDungeonStillTearsDown)
{
    Plan const plan = MakePlan(false, /*hasNonCombat*/ true, /*hasCombat*/ true, false, false);

    EXPECT_EQ(plan.nonCombat, Action::Strip);
    EXPECT_EQ(plan.combat, Action::Strip);
    EXPECT_TRUE(plan.teardown);
}

TEST(DungeonClearStrategyGate, PartialInstallInDungeonSelfHealsWithoutTeardown)
{
    Plan const plan = MakePlan(true, /*hasNonCombat*/ true, /*hasCombat*/ false, false, false);

    EXPECT_EQ(plan.nonCombat, Action::None);
    EXPECT_EQ(plan.combat, Action::Install);
    EXPECT_FALSE(plan.teardown);
}

TEST(DungeonClearStrategyGate, CompliantBotPlansNothing)
{
    Plan const in = MakePlan(true, true, true, false, false);
    EXPECT_EQ(in.nonCombat, Action::None);
    EXPECT_EQ(in.combat, Action::None);
    EXPECT_FALSE(in.stripStrayInCombat);
    EXPECT_FALSE(in.stripStrayInNonCombat);
    EXPECT_FALSE(in.teardown);

    Plan const out = MakePlan(false, false, false, false, false);
    EXPECT_EQ(out.nonCombat, Action::None);
    EXPECT_EQ(out.combat, Action::None);
    EXPECT_FALSE(out.stripStrayInCombat);
    EXPECT_FALSE(out.stripStrayInNonCombat);
    EXPECT_FALSE(out.teardown);
}

// MakePlan is constexpr too — pin the load-bearing cases at compile time.
static_assert(MakePlan(false, false, false, true, false).stripStrayInCombat,
              "stray in the combat engine is stripped outside a dungeon");
static_assert(MakePlan(true, true, true, true, false).stripStrayInCombat,
              "stray in the combat engine is stripped inside a dungeon too");
static_assert(!MakePlan(true, true, true, true, false).teardown,
              "removing a stray must not abort a live in-dungeon run");
static_assert(!MakePlan(false, false, false, false, false).teardown,
              "a compliant out-of-dungeon bot plans nothing at all");
