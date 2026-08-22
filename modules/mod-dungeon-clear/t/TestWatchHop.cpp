/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "Util/DcWatchHop.h"

using DcWatchHop::Bind;
using DcWatchHop::Decide;
using DcWatchHop::Plan;
using DcWatchHop::Where;

namespace
{
    // Ramparts (map 543), normal, two different live copies.
    constexpr uint32_t RAMPARTS = 543;
    constexpr uint32_t BLOOD_FURNACE = 542;
    constexpr uint8_t NORMAL = 0;
    constexpr uint8_t HEROIC = 1;

    Bind RunA() { return {RAMPARTS, NORMAL, 100}; }
    Bind RunB() { return {RAMPARTS, NORMAL, 200}; }
    Bind OtherMapRun() { return {BLOOD_FURNACE, NORMAL, 300}; }

    bool Releases(Plan const& p, Bind const& b)
    {
        for (Bind const& r : p.release)
            if (r.mapId == b.mapId && r.difficulty == b.difficulty && r.instanceId == b.instanceId)
                return true;
        return false;
    }
}

// --- cold start: no bind, standing in the open world -------------------------

TEST(DcWatchHop, ColdStartBindsAndDoesNotForceFarTeleport)
{
    Plan const p = Decide({0, 0}, RunA(), 0, {});

    EXPECT_FALSE(p.alreadyThere);
    EXPECT_TRUE(p.bindToTarget);
    EXPECT_FALSE(p.forceNewInstance);   // different map id; the normal branch works
    EXPECT_TRUE(p.release.empty());
}

// --- already seated ----------------------------------------------------------

TEST(DcWatchHop, StandingInTheTargetCopyIsANoOp)
{
    Plan const p = Decide({RAMPARTS, 100}, RunA(), 100, {RunA()});

    EXPECT_TRUE(p.alreadyThere);
    EXPECT_FALSE(p.bindToTarget);
    EXPECT_FALSE(p.forceNewInstance);
    EXPECT_TRUE(p.release.empty());
}

// Same map, DIFFERENT copy is emphatically not "already there" — this is the
// case that made a second watch slide the GM around inside the finished run.
TEST(DcWatchHop, SameMapDifferentCopyIsNotAlreadyThere)
{
    Plan const p = Decide({RAMPARTS, 100}, RunB(), 100, {RunA()});

    EXPECT_FALSE(p.alreadyThere);
    EXPECT_TRUE(p.forceNewInstance);
    EXPECT_TRUE(p.bindToTarget);
    EXPECT_TRUE(Releases(p, RunA()));
}

// --- the reported bug: watch run A, then watch run B -------------------------

TEST(DcWatchHop, HopBetweenRunsOnTheSameMapReleasesTheStaleBindFirst)
{
    Plan const p = Decide({RAMPARTS, 100}, RunB(), /*bound to*/ 100, {RunA()});

    ASSERT_EQ(p.release.size(), 1u);
    EXPECT_EQ(p.release[0].instanceId, 100u);
    EXPECT_EQ(p.release[0].mapId, RAMPARTS);
    EXPECT_TRUE(p.bindToTarget);
}

// The run just watched was on another map: its bind still has to go, even
// though it can't hijack THIS teleport — it holds a finished copy open.
TEST(DcWatchHop, HopToAnotherMapStillReleasesTheOldRunsBind)
{
    Plan const p = Decide({BLOOD_FURNACE, 300}, RunA(), /*bound to*/ 0, {OtherMapRun()});

    EXPECT_FALSE(p.alreadyThere);
    EXPECT_FALSE(p.forceNewInstance);   // map id changes; the normal branch works
    EXPECT_TRUE(p.bindToTarget);
    ASSERT_EQ(p.release.size(), 1u);
    EXPECT_EQ(p.release[0].mapId, BLOOD_FURNACE);
    EXPECT_EQ(p.release[0].instanceId, 300u);
}

TEST(DcWatchHop, ManyStaleBindsAllGetReleased)
{
    std::vector<Bind> const held{RunA(), OtherMapRun(), {RAMPARTS, HEROIC, 400}};
    Plan const p = Decide({0, 0}, RunB(), 0, held);

    EXPECT_EQ(p.release.size(), 3u);
    EXPECT_TRUE(Releases(p, RunA()));
    EXPECT_TRUE(Releases(p, OtherMapRun()));
    EXPECT_TRUE(Releases(p, Bind{RAMPARTS, HEROIC, 400}));
    EXPECT_TRUE(p.bindToTarget);
}

// --- binds we did not make ---------------------------------------------------

// A lockout the human earned on the run's map blocks the hop just as hard as
// one of ours, so it is released too — but only that one, and only because it
// names a copy other than the target.
TEST(DcWatchHop, ForeignBindOnTheTargetMapIsReleased)
{
    Plan const p = Decide({0, 0}, RunB(), /*bound to*/ 100, {});

    ASSERT_EQ(p.release.size(), 1u);
    EXPECT_EQ(p.release[0].mapId, RAMPARTS);
    EXPECT_EQ(p.release[0].difficulty, NORMAL);
    EXPECT_EQ(p.release[0].instanceId, 100u);
    EXPECT_TRUE(p.bindToTarget);
}

// Already bound to exactly the copy we want (a re-watch after zoning out):
// nothing to release, nothing to re-bind, but we still have to get there.
TEST(DcWatchHop, AlreadyBoundToTheTargetSkipsTheBind)
{
    Plan const p = Decide({0, 0}, RunA(), /*bound to*/ 100, {RunA()});

    EXPECT_FALSE(p.alreadyThere);
    EXPECT_FALSE(p.bindToTarget);
    EXPECT_TRUE(p.release.empty());     // the held bind IS the target's
}

TEST(DcWatchHop, TheTargetsOwnBindIsNeverReleased)
{
    Plan const p = Decide({RAMPARTS, 100}, RunB(), 100, {RunA(), RunB()});

    EXPECT_TRUE(Releases(p, RunA()));
    EXPECT_FALSE(Releases(p, RunB()));
}

// The core bind and a held bind naming the same copy must not be unbound twice.
TEST(DcWatchHop, DuplicateReleasesAreCollapsed)
{
    Plan const p = Decide({RAMPARTS, 100}, RunB(), /*bound to*/ 100, {RunA()});

    EXPECT_EQ(p.release.size(), 1u);
}

// --- degenerate destination --------------------------------------------------

// No instance id means the destination isn't instanced — there is no copy to
// pick, so no bind work is possible or needed.
TEST(DcWatchHop, NonInstancedTargetPlansNoBindWork)
{
    Plan const p = Decide({0, 0}, Bind{1, NORMAL, 0}, 0, {RunA()});

    EXPECT_FALSE(p.bindToTarget);
    EXPECT_FALSE(p.forceNewInstance);
    EXPECT_TRUE(p.release.empty());
}
