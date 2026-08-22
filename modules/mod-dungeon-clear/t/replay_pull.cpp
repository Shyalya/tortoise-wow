/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Capture->replay runner for the Dynamic-pull governor decision core, the pull
// twin of t/replay_decisions.cpp. Two jobs:
//   1. Round-trip sanity — a PullDecisionRecord serialized to JSONL and parsed
//      back reproduces the observation AND DecidePull agrees with the recorded
//      verdict, proving the serializer the live planner captures with is loss-free.
//   2. Fixture replay — every line of every t/fixtures/pull/*.jsonl is parsed and
//      asserted: DecidePull(rec.obs) == rec.verdict. A fixture is a frozen real
//      (or hand-authored) governor run; this is the regression gate.

#include "gtest/gtest.h"
#include "DcPullDecision.h"
#include "DcPullDecisionIo.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef DC_FIXTURE_DIR
#define DC_FIXTURE_DIR "."
#endif

using DcPullDecision::DecidePull;
using DcPullDecision::PullObservation;
using DcPullDecision::PullVerdict;
using DcPullDecisionIo::FromJsonl;
using DcPullDecisionIo::PullDecisionRecord;
using DcPullDecisionIo::ToJsonl;
using DcPullDecisionIo::VerdictName;

namespace
{
    void ExpectObsEq(PullObservation const& a, PullObservation const& b)
    {
        EXPECT_EQ(a.hasTarget, b.hasTarget);
        EXPECT_EQ(a.inCombat, b.inCombat);
        EXPECT_EQ(a.phaseIdle, b.phaseIdle);
        EXPECT_EQ(a.hasStandingVerdict, b.hasStandingVerdict);
        EXPECT_EQ(a.verdictGraceExpired, b.verdictGraceExpired);
        EXPECT_EQ(a.sameTarget, b.sameTarget);
        EXPECT_EQ(a.currentlyAdvanced, b.currentlyAdvanced);
        EXPECT_EQ(a.recheckElapsed, b.recheckElapsed);
        EXPECT_EQ(a.advanced, b.advanced);
        EXPECT_EQ(a.patrolWaitEnabled, b.patrolWaitEnabled);
        EXPECT_EQ(a.atCommitRange, b.atCommitRange);
        EXPECT_EQ(a.patrolContended, b.patrolContended);
        EXPECT_EQ(a.patrolWaitExpired, b.patrolWaitExpired);
    }

    // A spread of records across the governor's outcomes for the round-trip test.
    std::vector<PullDecisionRecord> SampleRecords()
    {
        std::vector<PullDecisionRecord> recs;
        uint32_t tick = 2000;

        auto push = [&](PullObservation const& o)
        {
            recs.push_back({7, tick++, o, DecidePull(o)});
        };

        PullObservation base;
        base.hasTarget = true;

        push(base);  // Leeroy

        PullObservation drop;  // DropToLeeroy
        drop.hasTarget = false;
        push(drop);

        PullObservation hold;  // HoldNoTarget
        hold.hasTarget = false;
        hold.hasStandingVerdict = true;
        push(hold);

        PullObservation adv = base;  // Advanced
        adv.advanced = true;
        push(adv);

        PullObservation hold2 = base;  // PatrolWaitHold
        hold2.advanced = true;
        hold2.patrolWaitEnabled = true;
        hold2.atCommitRange = true;
        hold2.patrolContended = true;
        push(hold2);

        PullObservation approach = base;  // ApproachAsLeeroy
        approach.advanced = true;
        approach.patrolWaitEnabled = true;
        approach.patrolContended = true;
        push(approach);

        PullObservation noop = base;  // NoOp (throttled)
        noop.sameTarget = true;
        noop.recheckElapsed = false;
        push(noop);

        return recs;
    }
}

// ---- Round-trip ------------------------------------------------------------

TEST(DcPullReplayRoundTrip, RecordsSurviveJsonlRoundTrip)
{
    for (auto const& rec : SampleRecords())
    {
        std::string const line = ToJsonl(rec);
        PullDecisionRecord back;
        ASSERT_TRUE(FromJsonl(line, back)) << "failed to parse: " << line;

        EXPECT_EQ(back.guid, rec.guid);
        EXPECT_EQ(back.tick, rec.tick);
        EXPECT_EQ(back.verdict, rec.verdict)
            << "verdict drifted: " << VerdictName(rec.verdict) << " -> "
            << VerdictName(back.verdict);
        ExpectObsEq(back.obs, rec.obs);
        EXPECT_EQ(DecidePull(back.obs), rec.verdict);
    }
}

TEST(DcPullReplayRoundTrip, BlankAndMalformedLinesRejected)
{
    PullDecisionRecord r;
    EXPECT_FALSE(FromJsonl("", r));
    EXPECT_FALSE(FromJsonl("   ", r));
    EXPECT_FALSE(FromJsonl("not json", r));
    EXPECT_FALSE(FromJsonl("{\"guid\":1}", r));            // no verdict
    EXPECT_FALSE(FromJsonl("{\"verdict\":\"Nonsense\"}", r));  // bad verdict
}

// ---- Fixture replay --------------------------------------------------------

TEST(DcPullReplayFixtures, EveryFixtureLineDecidesToItsRecordedVerdict)
{
    namespace fs = std::filesystem;
    fs::path const dir = fs::path(DC_FIXTURE_DIR) / "pull";
    if (!fs::exists(dir))
        GTEST_SKIP() << "no pull fixtures dir: " << dir;

    size_t totalLines = 0;
    for (auto const& entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".jsonl")
            continue;

        std::ifstream in(entry.path());
        ASSERT_TRUE(in.is_open()) << "cannot open " << entry.path();

        std::string line;
        size_t lineNo = 0;
        while (std::getline(in, line))
        {
            ++lineNo;
            size_t const first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos || line[first] == '#')
                continue;

            PullDecisionRecord rec;
            ASSERT_TRUE(FromJsonl(line, rec))
                << entry.path().filename().string() << ":" << lineNo
                << " did not parse: " << line;

            EXPECT_EQ(DecidePull(rec.obs), rec.verdict)
                << entry.path().filename().string() << ":" << lineNo
                << " replay divergence: recorded " << VerdictName(rec.verdict)
                << " but DecidePull now returns " << VerdictName(DecidePull(rec.obs));
            ++totalLines;
        }
    }

    EXPECT_GT(totalLines, 0u) << "pull fixtures present but contained no decision lines";
}
