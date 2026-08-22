/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "TestRun/DcWipeContext.h"

using DcTestRun::Engagement;
using DcTestRun::EngagementSample;
using DcTestRun::UpdateEngagement;

namespace
{
    EngagementSample OnBoss(std::uint32_t entry, char const* name)
    {
        EngagementSample s;
        s.anyAlive = true;
        s.anyAliveInCombat = true;
        s.bossEntry = entry;
        s.bossName = name;
        // A boss fight normally has adds on the party too; the boss must win.
        s.trashEntry = 9001;
        s.trashName = "Underbog Lurker";
        return s;
    }

    EngagementSample OnTrash(std::uint32_t entry, char const* name)
    {
        EngagementSample s;
        s.anyAlive = true;
        s.anyAliveInCombat = true;
        s.trashEntry = entry;
        s.trashName = name;
        return s;
    }

    EngagementSample Idle()
    {
        EngagementSample s;
        s.anyAlive = true;
        return s;
    }

    // Every member on the leader's map is a corpse: nothing to read at all.
    EngagementSample Wiped() { return EngagementSample(); }
}

// ---- latching --------------------------------------------------------------------

TEST(DcWipeContextTest, BossOutranksTrashInTheSameSample)
{
    Engagement const e = UpdateEngagement({}, OnBoss(18105, "Ghaz'an"));
    EXPECT_TRUE(e.isBoss);
    EXPECT_EQ(e.entry, 18105u);
    EXPECT_EQ(e.name, "Ghaz'an");
}

TEST(DcWipeContextTest, TrashLatchesWhenNoBossIsPresent)
{
    Engagement const e = UpdateEngagement({}, OnTrash(17826, "Bog Giant"));
    EXPECT_FALSE(e.isBoss);
    EXPECT_EQ(e.entry, 17826u);
    EXPECT_EQ(e.name, "Bog Giant");
}

TEST(DcWipeContextTest, LaterEngagementReplacesTheEarlierOne)
{
    Engagement e = UpdateEngagement({}, OnTrash(17826, "Bog Giant"));
    e = UpdateEngagement(e, OnBoss(18105, "Ghaz'an"));
    EXPECT_TRUE(e.isBoss);
    EXPECT_EQ(e.name, "Ghaz'an");
}

// ---- clearing --------------------------------------------------------------------

TEST(DcWipeContextTest, CleanDisengageClearsTheLatch)
{
    Engagement e = UpdateEngagement({}, OnTrash(17826, "Bog Giant"));
    e = UpdateEngagement(e, Idle());
    EXPECT_TRUE(e.Empty());
    EXPECT_FALSE(e.isBoss);
    EXPECT_EQ(e.entry, 0u);
}

TEST(DcWipeContextTest, TrashAfterABossKillIsNotReportedAsTheBoss)
{
    Engagement e = UpdateEngagement({}, OnBoss(18105, "Ghaz'an"));
    e = UpdateEngagement(e, Idle());                        // boss died, party at rest
    e = UpdateEngagement(e, OnTrash(17826, "Bog Giant"));   // next pack pulled
    EXPECT_FALSE(e.isBoss);
    EXPECT_EQ(e.name, "Bog Giant");
}

// ---- the invariant the whole latch exists for ------------------------------------

TEST(DcWipeContextTest, WipeDoesNotClearTheLatch)
{
    Engagement e = UpdateEngagement({}, OnBoss(18105, "Ghaz'an"));
    // The party dies one member at a time; the samples keep coming until the
    // last one drops, and then carry nothing at all. None of them may erase the
    // fight that killed the run.
    for (int i = 0; i < 20; ++i)
        e = UpdateEngagement(e, Wiped());
    EXPECT_TRUE(e.isBoss);
    EXPECT_EQ(e.name, "Ghaz'an");
}

TEST(DcWipeContextTest, WipeOutOfCombatReportsNothing)
{
    // Never engaged (a hazard, a fall, a dot ticking out after a flee): the
    // latch stays empty and the verdict must not invent an opponent.
    Engagement const e = UpdateEngagement({}, Wiped());
    EXPECT_TRUE(e.Empty());
}

TEST(DcWipeContextTest, InCombatWithNoResolvedOpponentHoldsTheLatch)
{
    // The pack died on the same tick the sample ran: still flagged in combat,
    // but nothing alive left to name. Holding beats blanking.
    Engagement e = UpdateEngagement({}, OnBoss(18105, "Ghaz'an"));
    EngagementSample s;
    s.anyAlive = true;
    s.anyAliveInCombat = true;
    e = UpdateEngagement(e, s);
    EXPECT_EQ(e.name, "Ghaz'an");
}

// ---- blame selection -------------------------------------------------------------

TEST(DcWipeContextTest, BlamePrefersTheLiveLatch)
{
    Engagement const live{true, 18105, "Ghaz'an"};
    Engagement const atDeath{false, 17826, "Bog Giant"};
    EXPECT_EQ(DcTestRun::BlameFor(live, atDeath).name, "Ghaz'an");
}

TEST(DcWipeContextTest, BlameFallsBackToTheLastDeathWhenTheLatchCleared)
{
    // The rez-timeout shape: the survivors killed the pack and disengaged, so
    // the live latch is legitimately empty — but the corpse still knows what
    // put it there, and that is the whole point of the death log.
    Engagement const atDeath{true, 23035, "Anzu"};
    Engagement const blame = DcTestRun::BlameFor({}, atDeath);
    EXPECT_TRUE(blame.isBoss);
    EXPECT_EQ(blame.name, "Anzu");
}

TEST(DcWipeContextTest, BlameStaysEmptyWhenTheLastDeathHadNoOpponent)
{
    // A member who dropped combat and then fell must not be filed against a
    // boss the party disengaged from long before.
    EXPECT_TRUE(DcTestRun::BlameFor({}, {}).Empty());
}

// ---- report strings --------------------------------------------------------------

TEST(DcWipeContextTest, BlameSuffixDistinguishesBossFromTrash)
{
    EXPECT_EQ(DcTestRun::BlameSuffix({true, 23035, "Anzu"}),
              " \xe2\x80\x94 killed by Anzu");
    EXPECT_EQ(DcTestRun::BlameSuffix({false, 18129, "Avian Ripper"}),
              " \xe2\x80\x94 killed by trash: Avian Ripper");
    EXPECT_EQ(DcTestRun::BlameSuffix({}), "");
}

TEST(DcWipeContextTest, StripResumeHintCutsTheChatCallToAction)
{
    // The two rez-recovery bailouts verbatim.
    EXPECT_EQ(DcTestRun::StripResumeHint(
                  "Eranel died and no one left alive can resurrect \xe2\x80\x94 dungeon clear "
                  "disabled. Type 'dc on' when ready to resume."),
              "Eranel died and no one left alive can resurrect");
    EXPECT_EQ(DcTestRun::StripResumeHint(
                  "Couldn't get Zesh resurrected in time \xe2\x80\x94 dungeon clear "
                  "disabled. Type 'dc on' when ready to resume."),
              "Couldn't get Zesh resurrected in time");
}

TEST(DcWipeContextTest, StripResumeHintLeavesReasonsWithoutTheHintAlone)
{
    EXPECT_EQ(DcTestRun::StripResumeHint("test run teardown"), "test run teardown");
    EXPECT_EQ(DcTestRun::StripResumeHint(""), "");
}

TEST(DcWipeContextTest, StripResumeHintKeepsAnEmDashInTheReasonsOwnBody)
{
    // Bounded lookback: only the dash INTRODUCING the hint is a cut point, so a
    // reason that legitimately uses one further back keeps it.
    EXPECT_EQ(DcTestRun::StripResumeHint(
                  "the tank \xe2\x80\x94 who was leading \xe2\x80\x94 vanished, and this trailing "
                  "clause is well over forty bytes long. Type 'dc on' when ready to resume."),
              "the tank \xe2\x80\x94 who was leading \xe2\x80\x94 vanished, and this trailing "
              "clause is well over forty bytes long");
}

TEST(DcWipeContextTest, StripResumeHintHandlesTheHintWithNoLeadingDash)
{
    EXPECT_EQ(DcTestRun::StripResumeHint("Bero died. Type 'dc on' when ready to resume."),
              "Bero died");
}
