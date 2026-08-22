/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "TestRun/DcTestRunSelect.h"

using DcTestRunSelect::kNone;
using DcTestRunSelect::Kind;
using DcTestRunSelect::NextWatchIndex;
using DcTestRunSelect::Resolve;
using DcTestRunSelect::Result;
using DcTestRunSelect::RunRef;

namespace
{
    std::vector<RunRef> None() { return {}; }

    std::vector<RunRef> One()
    {
        return {{"tr-1", "deadmines"}};
    }

    // Two distinct dungeons plus a second run of the first dungeon, so the
    // dungeon-token path can resolve to more than one index.
    std::vector<RunRef> Three()
    {
        return {{"tr-1", "deadmines"}, {"tr-2", "wailing-caverns"}, {"tr-3", "deadmines"}};
    }
}

// ---- bare selector ---------------------------------------------------------------

TEST(DcTestRunSelectTest, EmptySelectorNoRuns)
{
    Result const r = Resolve("", None());
    EXPECT_EQ(r.kind, Kind::NoRuns);
    EXPECT_TRUE(r.indices.empty());
}

TEST(DcTestRunSelectTest, EmptySelectorOneRunPicksIt)
{
    Result const r = Resolve("", One());
    EXPECT_EQ(r.kind, Kind::All);
    ASSERT_EQ(r.indices.size(), 1u);
    EXPECT_EQ(r.indices[0], 0u);
}

TEST(DcTestRunSelectTest, EmptySelectorTwoRunsAmbiguous)
{
    Result const r = Resolve("", Three());
    EXPECT_EQ(r.kind, Kind::Ambiguous);
    EXPECT_TRUE(r.indices.empty());
}

// ---- "all" -----------------------------------------------------------------------

TEST(DcTestRunSelectTest, AllSelectsEveryRun)
{
    Result const r = Resolve("all", Three());
    EXPECT_EQ(r.kind, Kind::All);
    ASSERT_EQ(r.indices.size(), 3u);
    EXPECT_EQ(r.indices[0], 0u);
    EXPECT_EQ(r.indices[1], 1u);
    EXPECT_EQ(r.indices[2], 2u);
}

TEST(DcTestRunSelectTest, AllWithNoRunsIsNoRuns)
{
    Result const r = Resolve("all", None());
    EXPECT_EQ(r.kind, Kind::NoRuns);
}

// ---- runId -----------------------------------------------------------------------

TEST(DcTestRunSelectTest, RunIdExactMatch)
{
    Result const r = Resolve("tr-2", Three());
    EXPECT_EQ(r.kind, Kind::Matched);
    ASSERT_EQ(r.indices.size(), 1u);
    EXPECT_EQ(r.indices[0], 1u);
}

// ---- dungeon token ---------------------------------------------------------------

TEST(DcTestRunSelectTest, DungeonTokenSingleRun)
{
    Result const r = Resolve("wailing-caverns", Three());
    EXPECT_EQ(r.kind, Kind::Matched);
    ASSERT_EQ(r.indices.size(), 1u);
    EXPECT_EQ(r.indices[0], 1u);
}

TEST(DcTestRunSelectTest, DungeonTokenMultipleRuns)
{
    Result const r = Resolve("deadmines", Three());
    EXPECT_EQ(r.kind, Kind::Matched);
    ASSERT_EQ(r.indices.size(), 2u);
    EXPECT_EQ(r.indices[0], 0u);
    EXPECT_EQ(r.indices[1], 2u);
}

// ---- precedence: runId beats a same-string dungeon token -------------------------

TEST(DcTestRunSelectTest, RunIdBeatsDungeonToken)
{
    // A run whose dungeon token literally equals another run's runId. The
    // exact-runId pass must win, resolving to that one run only.
    std::vector<RunRef> runs = {{"tr-x", "boss"}, {"boss", "wailing-caverns"}};
    Result const r = Resolve("boss", runs);
    EXPECT_EQ(r.kind, Kind::Matched);
    ASSERT_EQ(r.indices.size(), 1u);
    EXPECT_EQ(r.indices[0], 1u);  // the run whose runId == "boss"
}

// ---- not found -------------------------------------------------------------------

TEST(DcTestRunSelectTest, UnknownSelectorNotFound)
{
    Result const r = Resolve("nope", Three());
    EXPECT_EQ(r.kind, Kind::NotFound);
    EXPECT_TRUE(r.indices.empty());
}

// ---- `.dc test watch next` cycle --------------------------------------------------

TEST(DcTestRunSelectTest, NextWatchNoRunsGoesNowhere)
{
    EXPECT_EQ(NextWatchIndex(0, kNone), kNone);
    EXPECT_EQ(NextWatchIndex(0, 0), kNone);
}

TEST(DcTestRunSelectTest, NextWatchOneRunNotWatchingTakesTheSeat)
{
    EXPECT_EQ(NextWatchIndex(1, kNone), 0u);
}

TEST(DcTestRunSelectTest, NextWatchOneRunAlreadyOnItStaysPut)
{
    // The whole point of the refusal: hopping to the run you are already
    // watching is a loading screen that ends where it began.
    EXPECT_EQ(NextWatchIndex(1, 0), kNone);
}

TEST(DcTestRunSelectTest, NextWatchAdvancesOne)
{
    EXPECT_EQ(NextWatchIndex(3, 0), 1u);
    EXPECT_EQ(NextWatchIndex(3, 1), 2u);
}

TEST(DcTestRunSelectTest, NextWatchWrapsAtTheEnd)
{
    EXPECT_EQ(NextWatchIndex(3, 2), 0u);
}

TEST(DcTestRunSelectTest, NextWatchFromNowhereStartsAtTheFirst)
{
    EXPECT_EQ(NextWatchIndex(3, kNone), 0u);
}

TEST(DcTestRunSelectTest, NextWatchStaleIndexRestartsTheTour)
{
    // The run the watcher sat on finished and left the set: an index past the
    // end must not wrap arithmetically onto a random seat.
    EXPECT_EQ(NextWatchIndex(2, 7), 0u);
}

TEST(DcTestRunSelectTest, NextWatchTourVisitsEveryRunInOrder)
{
    std::vector<std::size_t> visited;
    std::size_t at = kNone;
    for (int i = 0; i < 4; ++i)
    {
        at = NextWatchIndex(4, at);
        visited.push_back(at);
    }
    EXPECT_EQ(visited, (std::vector<std::size_t>{0u, 1u, 2u, 3u}));
    EXPECT_EQ(NextWatchIndex(4, at), 0u);  // and back to the start
}
