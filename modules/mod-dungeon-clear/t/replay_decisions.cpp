/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Layer 2 of the orchestration replay harness (review report point #3): the
// offline capture->replay runner. Two jobs:
//
//   1. Round-trip sanity — a DecisionRecord serialized to JSONL and parsed back
//      reproduces the same observation and verdict, AND DecideApproach agrees
//      with the recorded verdict. This proves the serializer the LIVE server
//      captures with is loss-free, so a capture is a faithful record.
//
//   2. Fixture replay — every line of every t/fixtures/*.jsonl is parsed and
//      asserted: DecideApproach(rec.obs) == rec.verdict. A fixture is a frozen
//      real (or hand-authored historical) run; this is the regression gate. A
//      capture taken BEFORE a refactor, replayed against the AFTER function,
//      shows zero divergence — that is the "no behavioral change" acceptance
//      test the whole refactor series is held to.
//
// The runner links the exact serialization code the action uses
// (DungeonClearApproachIo), so a fixture round-trips byte-for-byte with a live
// capture.

#include "gtest/gtest.h"
#include "DungeonClearApproach.h"
#include "DungeonClearApproachIo.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef DC_FIXTURE_DIR
#define DC_FIXTURE_DIR "."
#endif

using DungeonClearApproach::DecideApproach;
using DungeonClearApproach::Observation;
using DungeonClearApproach::Verdict;
using DungeonClearApproachIo::DecisionRecord;
using DungeonClearApproachIo::FromJsonl;
using DungeonClearApproachIo::ToJsonl;
using DungeonClearApproachIo::VerdictName;

namespace
{
    // Field-by-field observation equality, so a round-trip failure reports which
    // field drifted rather than an opaque struct compare.
    void ExpectObsEq(Observation const& a, Observation const& b)
    {
        EXPECT_FLOAT_EQ(a.engageDist, b.engageDist);
        EXPECT_FLOAT_EQ(a.engageRange, b.engageRange);
        EXPECT_EQ(a.posStuckTicks, b.posStuckTicks);
        EXPECT_EQ(a.canPursue, b.canPursue);
        EXPECT_EQ(a.pursuitFailTicks, b.pursuitFailTicks);
        EXPECT_EQ(a.allowRecoveryMoves, b.allowRecoveryMoves);
        EXPECT_EQ(a.pathReachable, b.pathReachable);
        EXPECT_EQ(a.asyncPending, b.asyncPending);
        EXPECT_EQ(a.startFarFromPoly, b.startFarFromPoly);
        EXPECT_EQ(a.waterBetween, b.waterBetween);
        EXPECT_EQ(a.offPath, b.offPath);
        EXPECT_EQ(a.hopDone, b.hopDone);
        EXPECT_EQ(a.hopIsJump, b.hopIsJump);
        EXPECT_EQ(a.doneNotEngagedTicks, b.doneNotEngagedTicks);
        EXPECT_EQ(a.splineRunning, b.splineRunning);
        EXPECT_EQ(a.offLine, b.offLine);
        EXPECT_EQ(a.haveSplineWindow, b.haveSplineWindow);
        EXPECT_EQ(a.stuckTickLimit, b.stuckTickLimit);
        EXPECT_EQ(a.pursuitFailLimit, b.pursuitFailLimit);
        EXPECT_EQ(a.doneNotEngagedLimit, b.doneNotEngagedLimit);
    }

    // A spread of records across the ladder for the round-trip test.
    std::vector<DecisionRecord> SampleRecords()
    {
        Observation base;
        base.engageDist          = 100.0f;
        base.engageRange         = 22.0f;
        base.haveSplineWindow    = true;
        base.stuckTickLimit      = 5;
        base.pursuitFailLimit    = 5;
        base.doneNotEngagedLimit = 15;

        std::vector<DecisionRecord> recs;
        uint32_t tick = 1000;

        // IssueSplineWindow (baseline)
        recs.push_back({1, tick++, base, Verdict::MoveToFallback});

        // StuckRecover
        Observation stuck = base;
        stuck.posStuckTicks = 5;
        recs.push_back({1, tick++, stuck, Verdict::MoveToFallback});

        // Pursue
        Observation pursue = base;
        pursue.canPursue = true;
        recs.push_back({1, tick++, pursue, Verdict::MoveToFallback});

        // PlanRouteWait
        Observation wait = base;
        wait.pathReachable = false;
        wait.asyncPending = true;
        recs.push_back({1, tick++, wait, Verdict::MoveToFallback});

        // FinalApproach
        Observation fa = base;
        fa.hopDone = true;
        fa.engageDist = 30.0f;
        recs.push_back({1, tick++, fa, Verdict::MoveToFallback});

        // Fill each record's verdict with the real decision so the round-trip
        // also checks the verdict field, not just the observation.
        for (auto& r : recs)
            r.verdict = DecideApproach(r.obs);
        return recs;
    }
}

// ---- Round-trip: serialize -> parse reproduces obs + verdict ---------------

TEST(DcReplayRoundTrip, RecordsSurviveJsonlRoundTrip)
{
    for (auto const& rec : SampleRecords())
    {
        std::string const line = ToJsonl(rec);
        DecisionRecord back;
        ASSERT_TRUE(FromJsonl(line, back)) << "failed to parse: " << line;

        EXPECT_EQ(back.guid, rec.guid);
        EXPECT_EQ(back.tick, rec.tick);
        EXPECT_EQ(back.verdict, rec.verdict)
            << "verdict drifted: " << VerdictName(rec.verdict) << " -> "
            << VerdictName(back.verdict);
        ExpectObsEq(back.obs, rec.obs);

        // And the parsed observation still decides to the recorded verdict.
        EXPECT_EQ(DecideApproach(back.obs), rec.verdict);
    }
}

TEST(DcReplayRoundTrip, BlankAndMalformedLinesRejected)
{
    DecisionRecord r;
    EXPECT_FALSE(FromJsonl("", r));
    EXPECT_FALSE(FromJsonl("   ", r));
    EXPECT_FALSE(FromJsonl("not json", r));
    EXPECT_FALSE(FromJsonl("{\"guid\":1}", r));  // no verdict
    EXPECT_FALSE(FromJsonl("{\"verdict\":\"Nonsense\"}", r));  // bad verdict
}

// ---- Fixture replay: every captured line still decides identically ---------

TEST(DcReplayFixtures, EveryFixtureLineDecidesToItsRecordedVerdict)
{
    namespace fs = std::filesystem;
    fs::path const dir(DC_FIXTURE_DIR);
    ASSERT_TRUE(fs::exists(dir)) << "fixture dir missing: " << dir;

    size_t totalLines = 0;
    size_t fileCount = 0;
    for (auto const& entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".jsonl")
            continue;
        ++fileCount;

        std::ifstream in(entry.path());
        ASSERT_TRUE(in.is_open()) << "cannot open " << entry.path();

        std::string line;
        size_t lineNo = 0;
        while (std::getline(in, line))
        {
            ++lineNo;
            // Skip blank lines and # comments (the parser rejects them too, but
            // be explicit so a commented fixture stays legal).
            std::string trimmed = line;
            size_t const first = trimmed.find_first_not_of(" \t\r\n");
            if (first == std::string::npos || trimmed[first] == '#')
                continue;

            DecisionRecord rec;
            ASSERT_TRUE(FromJsonl(line, rec))
                << entry.path().filename().string() << ":" << lineNo
                << " did not parse: " << line;

            EXPECT_EQ(DecideApproach(rec.obs), rec.verdict)
                << entry.path().filename().string() << ":" << lineNo
                << " replay divergence: recorded " << VerdictName(rec.verdict)
                << " but DecideApproach now returns "
                << VerdictName(DecideApproach(rec.obs));
            ++totalLines;
        }
    }

    EXPECT_GT(fileCount, 0u) << "no .jsonl fixtures found in " << dir;
    EXPECT_GT(totalLines, 0u) << "fixtures present but contained no decision lines";
}
