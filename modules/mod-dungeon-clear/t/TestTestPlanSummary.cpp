/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "TestRun/DcTestPlanSummary.h"

using DcTestPlan::RunOutcome;
using DcTestPlanSummary::Build;
using DcTestPlanSummary::Header;
using DcTestPlanSummary::Stats;
using DcTestPlanSummary::ToJsonl;

namespace
{
    RunOutcome Success(std::string const& runId, std::uint32_t durationS,
                       std::vector<std::string> bossKills = {})
    {
        RunOutcome o;
        o.runId = runId;
        o.result = "success";
        o.durationS = durationS;
        o.bossKills = std::move(bossKills);
        o.bossesKilled = static_cast<std::uint32_t>(o.bossKills.size());
        o.bossesTotal = 4;
        return o;
    }

    RunOutcome Failure(std::string const& runId, std::string const& result,
                       std::string const& reason, std::vector<std::string> bossKills = {})
    {
        RunOutcome o;
        o.runId = runId;
        o.result = result;
        o.failReason = reason;
        o.durationS = 3600;
        o.bossKills = std::move(bossKills);
        o.bossesKilled = static_cast<std::uint32_t>(o.bossKills.size());
        o.bossesTotal = 4;
        return o;
    }
}

// ---- counts + verdict histogram --------------------------------------------------

TEST(DcTestPlanSummaryTest, CountsAndVerdictHistogram)
{
    Stats const s = Build({Success("r1", 100), Success("r2", 200),
                           Failure("r3", "no_progress", "no boss progress"),
                           Failure("r4", "no_progress", "no boss progress"),
                           Failure("r5", "setup_failed", "bots did not log in")});
    EXPECT_EQ(s.launched, 5u);
    EXPECT_EQ(s.succeeded, 2u);
    EXPECT_EQ(s.failed, 3u);

    ASSERT_EQ(s.verdicts.size(), 3u);
    // Sorted by count desc; the 2-2 tie stays alphabetical (deterministic).
    EXPECT_EQ(s.verdicts[0].key, "no_progress");
    EXPECT_EQ(s.verdicts[0].count, 2u);
    EXPECT_EQ(s.verdicts[1].key, "success");
    EXPECT_EQ(s.verdicts[1].count, 2u);
    EXPECT_EQ(s.verdicts[2].key, "setup_failed");
    EXPECT_EQ(s.verdicts[2].count, 1u);

    ASSERT_EQ(s.failReasons.size(), 2u);
    EXPECT_EQ(s.failReasons[0].key, "no boss progress");
    EXPECT_EQ(s.failReasons[0].count, 2u);

    ASSERT_EQ(s.runIds.size(), 5u);
    EXPECT_EQ(s.runIds[0], "r1");
    EXPECT_EQ(s.runIds[4], "r5");
}

// ---- duration stats (successes only) ---------------------------------------------

TEST(DcTestPlanSummaryTest, DurationStatsUseSuccessesOnly)
{
    // Failure durations (3600s timeouts) must not pollute the stats.
    Stats const s = Build({Success("r1", 300), Success("r2", 100), Success("r3", 200),
                           Failure("r4", "overall_timeout", "exceeded limit")});
    EXPECT_EQ(s.minS, 100u);
    EXPECT_EQ(s.maxS, 300u);
    EXPECT_EQ(s.avgS, 200u);
    EXPECT_EQ(s.medianS, 200u);  // odd count: middle element
}

TEST(DcTestPlanSummaryTest, MedianOfEvenCountAverages)
{
    Stats const s = Build({Success("r1", 100), Success("r2", 200),
                           Success("r3", 300), Success("r4", 400)});
    EXPECT_EQ(s.medianS, 250u);
}

TEST(DcTestPlanSummaryTest, AllFailedLeavesDurationZero)
{
    Stats const s = Build({Failure("r1", "aborted", "stopped")});
    EXPECT_EQ(s.minS, 0u);
    EXPECT_EQ(s.avgS, 0u);
    EXPECT_EQ(s.medianS, 0u);
    EXPECT_EQ(s.maxS, 0u);
}

// ---- boss funnel -----------------------------------------------------------------

TEST(DcTestPlanSummaryTest, FunnelOrdersByProgressionAndCountsKills)
{
    // Two full clears, one death at the second boss: funnel must read
    // Drake(3) -> Skarloc(2) -> Epoch(2) in progression order even though the
    // map iteration order of the names is alphabetical.
    Stats const s = Build({Success("r1", 100, {"Lieutenant Drake", "Captain Skarloc", "Epoch Hunter"}),
                           Success("r2", 110, {"Lieutenant Drake", "Captain Skarloc", "Epoch Hunter"}),
                           Failure("r3", "no_progress", "wipe", {"Lieutenant Drake"})});
    ASSERT_EQ(s.funnel.size(), 3u);
    EXPECT_EQ(s.funnel[0].name, "Lieutenant Drake");
    EXPECT_EQ(s.funnel[0].killed, 3u);
    EXPECT_EQ(s.funnel[1].name, "Captain Skarloc");
    EXPECT_EQ(s.funnel[1].killed, 2u);
    EXPECT_EQ(s.funnel[2].name, "Epoch Hunter");
    EXPECT_EQ(s.funnel[2].killed, 2u);
}

TEST(DcTestPlanSummaryTest, FunnelDedupesRepeatKillsWithinARun)
{
    // A re-fired encounter bit must not double-count a run's kill.
    Stats const s = Build({Success("r1", 100, {"Boss A", "Boss A", "Boss B"})});
    ASSERT_EQ(s.funnel.size(), 2u);
    EXPECT_EQ(s.funnel[0].name, "Boss A");
    EXPECT_EQ(s.funnel[0].killed, 1u);
    EXPECT_EQ(s.funnel[1].name, "Boss B");
    EXPECT_EQ(s.funnel[1].killed, 1u);
}

// ---- per-boss wipe attribution ---------------------------------------------------

namespace
{
    // A run that ended with the party down to `opponent`.
    RunOutcome Wipe(std::string const& runId, std::string const& opponent, bool onBoss,
                    std::vector<std::string> bossKills = {},
                    std::string const& result = "wipe")
    {
        RunOutcome o = Failure(runId, result, "party wiped", std::move(bossKills));
        o.wipeOpponent = opponent;
        o.wipeOnBoss = onBoss;
        return o;
    }

    std::vector<std::string> const kRoster = {"Lieutenant Drake", "Captain Skarloc",
                                              "Epoch Hunter"};

    RunOutcome WithRoster(RunOutcome o)
    {
        o.bossRoster = kRoster;
        return o;
    }
}

TEST(DcTestPlanSummaryTest, FunnelCountsWipesPerBossAlongsideKills)
{
    Stats const s = Build({
        WithRoster(Success("r1", 100, {"Lieutenant Drake", "Captain Skarloc", "Epoch Hunter"})),
        WithRoster(Wipe("r2", "Captain Skarloc", true, {"Lieutenant Drake"})),
        WithRoster(Wipe("r3", "Captain Skarloc", true, {"Lieutenant Drake"})),
        WithRoster(Wipe("r4", "Epoch Hunter", true,
                        {"Lieutenant Drake", "Captain Skarloc"})),
    });

    ASSERT_EQ(s.funnel.size(), 3u);
    EXPECT_EQ(s.funnel[0].name, "Lieutenant Drake");
    EXPECT_EQ(s.funnel[0].killed, 4u);
    EXPECT_EQ(s.funnel[0].wiped, 0u);
    EXPECT_EQ(s.funnel[1].name, "Captain Skarloc");
    EXPECT_EQ(s.funnel[1].killed, 2u);
    EXPECT_EQ(s.funnel[1].wiped, 2u);
    EXPECT_EQ(s.funnel[2].name, "Epoch Hunter");
    EXPECT_EQ(s.funnel[2].killed, 1u);
    EXPECT_EQ(s.funnel[2].wiped, 1u);
    EXPECT_TRUE(s.trashWipes.empty());
    EXPECT_EQ(s.unattributedWipes, 0u);
}

TEST(DcTestPlanSummaryTest, RosterKeepsUnreachedBossesInTheFunnel)
{
    // Every run dies to the first boss. Skarloc and Epoch Hunter must still be
    // reported, at 0 killed — "nobody got that far" is the finding.
    Stats const s = Build({WithRoster(Wipe("r1", "Lieutenant Drake", true)),
                           WithRoster(Wipe("r2", "Lieutenant Drake", true))});
    ASSERT_EQ(s.funnel.size(), 3u);
    EXPECT_EQ(s.funnel[0].name, "Lieutenant Drake");
    EXPECT_EQ(s.funnel[0].wiped, 2u);
    EXPECT_EQ(s.funnel[1].name, "Captain Skarloc");
    EXPECT_EQ(s.funnel[1].killed, 0u);
    EXPECT_EQ(s.funnel[1].wiped, 0u);
    EXPECT_EQ(s.funnel[2].name, "Epoch Hunter");
    EXPECT_EQ(s.funnel[2].killed, 0u);
}

TEST(DcTestPlanSummaryTest, RosterOrderComesFromTheLongestRosterReported)
{
    // A setup failure reports no roster at all; a run that got in reports the
    // full one. The full one must win, not the first one seen.
    RunOutcome bare = Failure("r1", "setup_failed", "bots did not log in");
    Stats const s = Build({bare, WithRoster(Success("r2", 100, {"Captain Skarloc"}))});
    ASSERT_EQ(s.funnel.size(), 3u);
    EXPECT_EQ(s.funnel[0].name, "Lieutenant Drake");
    EXPECT_EQ(s.funnel[1].name, "Captain Skarloc");
    EXPECT_EQ(s.funnel[1].killed, 1u);
    EXPECT_EQ(s.funnel[2].name, "Epoch Hunter");
}

TEST(DcTestPlanSummaryTest, OffRosterBossesAppendAfterTheRoster)
{
    // Anzu-style summoned bonus boss: never on the roster, but runs die to him.
    Stats const s = Build({WithRoster(Wipe("r1", "Anzu", true,
                                           {"Lieutenant Drake", "Captain Skarloc"})),
                           WithRoster(Wipe("r2", "Anzu", true, {"Lieutenant Drake"}))});
    ASSERT_EQ(s.funnel.size(), 4u);
    EXPECT_EQ(s.funnel[0].name, "Lieutenant Drake");
    EXPECT_EQ(s.funnel[3].name, "Anzu");
    EXPECT_EQ(s.funnel[3].killed, 0u);
    EXPECT_EQ(s.funnel[3].wiped, 2u);
}

TEST(DcTestPlanSummaryTest, TrashWipesAreCountedSeparatelyFromBosses)
{
    Stats const s = Build({WithRoster(Wipe("r1", "Avian Ripper", false)),
                           WithRoster(Wipe("r2", "Avian Ripper", false)),
                           WithRoster(Wipe("r3", "Dark Vortex", false)),
                           WithRoster(Wipe("r4", "Lieutenant Drake", true))});
    ASSERT_EQ(s.trashWipes.size(), 2u);
    EXPECT_EQ(s.trashWipes[0].key, "Avian Ripper");
    EXPECT_EQ(s.trashWipes[0].count, 2u);
    EXPECT_EQ(s.trashWipes[1].key, "Dark Vortex");
    EXPECT_EQ(s.trashWipes[1].count, 1u);
    // Trash never appears in the boss funnel.
    ASSERT_EQ(s.funnel.size(), 3u);
    EXPECT_EQ(s.funnel[0].wiped, 1u);
}

TEST(DcTestPlanSummaryTest, DeathBailoutsCountAsWipesOnTheirBoss)
{
    // A rez-recovery bailout ends as "disabled", not "wipe", but it is still a
    // run the boss took down — it must land in the same column.
    Stats const s = Build({WithRoster(Wipe("r1", "Epoch Hunter", true,
                                           {"Lieutenant Drake", "Captain Skarloc"},
                                           "disabled"))});
    ASSERT_EQ(s.funnel.size(), 3u);
    EXPECT_EQ(s.funnel[2].name, "Epoch Hunter");
    EXPECT_EQ(s.funnel[2].wiped, 1u);
}

TEST(DcTestPlanSummaryTest, UnattributedWipesCountOnlyTheWipeVerdict)
{
    // A wipe with no opponent is a real unexplained loss; a "disabled" run with
    // no opponent may simply be a run nobody died in.
    Stats const s = Build({WithRoster(Failure("r1", "wipe", "party wiped out of combat")),
                           WithRoster(Failure("r2", "disabled", "run disabled: all cleared"))});
    EXPECT_EQ(s.unattributedWipes, 1u);
    EXPECT_TRUE(s.trashWipes.empty());
}

// ---- pull population -------------------------------------------------------------

namespace
{
    // predicted / observed pairs, in order, onto one run's outcome.
    RunOutcome WithPulls(RunOutcome o,
                         std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs,
                         bool advanced = false)
    {
        for (auto const& [predicted, observed] : pairs)
            o.pulls.push_back({predicted, observed, advanced, false});
        return o;
    }
}

TEST(DcTestPlanSummaryTest, PullStatsPoolAcrossRuns)
{
    // Pooled, not averaged per run: the four-pull run contributes four samples
    // and the one-pull run contributes one, so a run that wipes early cannot
    // weigh as heavily as a full clear.
    Stats const s = Build({WithPulls(Success("r1", 100), {{2, 2}, {3, 3}, {2, 4}, {5, 5}}),
                           WithPulls(Failure("r2", "wipe", "wiped"), {{2, 9}})});
    EXPECT_EQ(s.pulls.pulls, 5u);
    EXPECT_EQ(s.pulls.observedMax, 9u);
    // observed sorted: 2 3 4 5 9 -> nearest-rank p50 = idx 2 (4), p90 = idx 4 (9).
    EXPECT_EQ(s.pulls.observedP50, 4u);
    EXPECT_EQ(s.pulls.observedP90, 9u);
    // Two pulls brought more than predicted (2->4 and 2->9).
    EXPECT_EQ(s.pulls.underestimated, 2u);
}

TEST(DcTestPlanSummaryTest, PullErrorIsSignedObservedMinusPredicted)
{
    // An over-estimate (feared 6, got 2) must read NEGATIVE, or a plan whose
    // governor is merely pessimistic would look identical to one that is blind.
    Stats const s = Build({WithPulls(Success("r1", 100), {{6, 2}, {3, 3}, {2, 7}})});
    // errors sorted: -4 0 5 -> p50 = idx 1 (0), p90 = idx 2 (5).
    EXPECT_EQ(s.pulls.errorP50, 0);
    EXPECT_EQ(s.pulls.errorP90, 5);
    EXPECT_EQ(s.pulls.underestimated, 1u);
}

TEST(DcTestPlanSummaryTest, PullStatsCountAdvancedVerdicts)
{
    Stats const s = Build({WithPulls(Success("r1", 100), {{2, 2}, {3, 3}}, /*advanced*/ true),
                           WithPulls(Success("r2", 100), {{1, 1}}, /*advanced*/ false)});
    EXPECT_EQ(s.pulls.pulls, 3u);
    EXPECT_EQ(s.pulls.advanced, 2u);
}

TEST(DcTestPlanSummaryTest, WipePullsAreCountedWithTheirSize)
{
    // The number that says whether the party is dying to over-pulls or losing
    // fights it pulled correctly.
    RunOutcome a = Failure("r1", "wipe", "wiped");
    a.pulls.push_back({2, 2, false, false});
    a.pulls.push_back({3, 8, false, true});
    RunOutcome b = Failure("r2", "wipe", "wiped");
    b.pulls.push_back({2, 3, true, true});

    Stats const s = Build({a, b});
    EXPECT_EQ(s.pulls.wipePulls, 2u);
    EXPECT_EQ(s.pulls.wipeObservedMax, 8u);
}

TEST(DcTestPlanSummaryTest, NoPullsLeavesPullStatsZero)
{
    // Off/On pull modes never run the classifier, so their runs report nothing —
    // that must read as "no samples", not as a plan of perfectly-called pulls.
    Stats const s = Build({Success("r1", 100), Failure("r2", "wipe", "wiped")});
    EXPECT_EQ(s.pulls.pulls, 0u);
    EXPECT_EQ(s.pulls.observedP50, 0u);
    EXPECT_EQ(s.pulls.errorP90, 0);
    EXPECT_EQ(s.pulls.wipePulls, 0u);
}

// ---- empty plan ------------------------------------------------------------------

TEST(DcTestPlanSummaryTest, EmptyOutcomesProduceEmptyStats)
{
    Stats const s = Build({});
    EXPECT_EQ(s.launched, 0u);
    EXPECT_TRUE(s.verdicts.empty());
    EXPECT_TRUE(s.funnel.empty());
    EXPECT_TRUE(s.runIds.empty());
}

// ---- JSONL shape -----------------------------------------------------------------

TEST(DcTestPlanSummaryTest, ToJsonlCarriesHeaderAndStats)
{
    Header h;
    h.planId = "tp-1";
    h.dungeon = "old-hillsbrad";
    h.dungeonName = "Old Hillsbrad Foothills";
    h.total = 20;
    h.concurrent = 5;
    h.level = 68;
    h.seedBase = 7;
    h.gearIlvl = 141;
    h.gearQuality = 4;
    h.startedAtMs = 1000;
    h.endedAtMs = 61000;
    h.durationS = 60;
    h.result = "completed";

    Stats const s = Build({Success("r1", 100, {"Lieutenant Drake"}),
                           Failure("r2", "no_progress", "he said \"no\"")});
    std::string const line = ToJsonl(h, s);

    EXPECT_NE(line.find("\"schema\":5"), std::string::npos);
    EXPECT_NE(line.find("\"planId\":\"tp-1\""), std::string::npos);
    // The requested block is the campaign's inputs verbatim — including the
    // gear ceiling, without which two campaigns' numbers are not comparable.
    EXPECT_NE(line.find("\"requested\":{\"total\":20,\"concurrent\":5,\"level\":68"
                        ",\"heroic\":false,\"seedBase\":7,\"gearIlvl\":141,\"gearQuality\":4}"),
              std::string::npos);
    EXPECT_NE(line.find("\"result\":\"completed\""), std::string::npos);
    EXPECT_NE(line.find("\"runs\":{\"launched\":2,\"succeeded\":1,\"failed\":1}"),
              std::string::npos);
    EXPECT_NE(line.find("\"verdicts\":{"), std::string::npos);
    EXPECT_NE(line.find("\"success\":1"), std::string::npos);
    EXPECT_NE(line.find("\"bossFunnel\":[{\"name\":\"Lieutenant Drake\",\"killed\":1,\"wiped\":0}]"),
              std::string::npos);
    EXPECT_NE(line.find("\"trashWipes\":[]"), std::string::npos);
    EXPECT_NE(line.find("\"unattributedWipes\":0"), std::string::npos);
    EXPECT_NE(line.find("\"pulls\":{\"count\":0"), std::string::npos);
    EXPECT_NE(line.find("\"runIds\":[\"r1\",\"r2\"]"), std::string::npos);
    // Free-text reason is escaped, and no raw newline leaks into the line.
    EXPECT_NE(line.find("\\\"no\\\""), std::string::npos);
    EXPECT_EQ(line.find('\n'), std::string::npos);
}
