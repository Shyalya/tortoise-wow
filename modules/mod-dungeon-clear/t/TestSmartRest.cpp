/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcSmartRestDecision.h"

using DcSmartRestDecision::Decide;
using DcSmartRestDecision::Inputs;
using DcSmartRestDecision::Member;
using DcSmartRestDecision::Result;
using DcSmartRestDecision::kManaReleasePct;
using DcSmartRestDecision::kReleasePct;

namespace
{
    // A "healthy" five-member party at full everything, default triggers
    // (hp 50 / DPS mana 10 / healer mana 40). Individual tests flip one thing.
    // Roster: [0] tank (mana user — prot paladin shape), [1] healer,
    // [2] caster DPS, [3] melee DPS (no mana), [4] human caster DPS.
    std::vector<Member> BaseParty()
    {
        Member tank;   tank.isManaUser = true;
        Member healer; healer.isManaUser = true; healer.isHealer = true;
        Member caster; caster.isManaUser = true;
        Member melee;  // no mana
        Member human;  human.isManaUser = true;  // the real player's seat
        return {tank, healer, caster, melee, human};
    }

    Inputs BaseInputs()
    {
        Inputs in;
        in.latched = false;
        in.restElapsedMs = 0;
        in.rearmed = true;
        in.hpTriggerPct = 50.0f;
        in.dpsManaTriggerPct = 10.0f;
        in.healerManaTriggerPct = 40.0f;
        in.maxRestMs = 180000;
        return in;
    }

    Inputs LatchedInputs()
    {
        Inputs in = BaseInputs();
        in.latched = true;
        in.restElapsedMs = 1000;
        return in;
    }
}

// ---- entering the latch ---------------------------------------------------------

TEST(DcSmartRestTest, FullPartyDoesNotLatch)
{
    Result const r = Decide(BaseInputs(), BaseParty());
    EXPECT_FALSE(r.latched);
    EXPECT_TRUE(r.blockers.empty());
}

TEST(DcSmartRestTest, DpsManaBelowTriggerLatches)
{
    auto party = BaseParty();
    party[2].manaPct = 9.0f;
    Result const r = Decide(BaseInputs(), party);
    EXPECT_TRUE(r.latched);
    ASSERT_EQ(r.blockers.size(), 1u);
    EXPECT_EQ(r.blockers[0], 2u);
}

TEST(DcSmartRestTest, DpsManaJustAboveTriggerDoesNotLatch)
{
    auto party = BaseParty();
    party[2].manaPct = 11.0f;
    EXPECT_FALSE(Decide(BaseInputs(), party).latched);
}

TEST(DcSmartRestTest, HealerManaBelowHealerTriggerLatches)
{
    auto party = BaseParty();
    party[1].manaPct = 39.0f;
    EXPECT_TRUE(Decide(BaseInputs(), party).latched);
}

TEST(DcSmartRestTest, HealerManaJustAboveTriggerDoesNotLatch)
{
    auto party = BaseParty();
    party[1].manaPct = 41.0f;
    EXPECT_FALSE(Decide(BaseInputs(), party).latched);
}

TEST(DcSmartRestTest, HealerTriggerDoesNotApplyToDps)
{
    // 39% mana would latch a healer, but a DPS pushes on down to 10%.
    auto party = BaseParty();
    party[2].manaPct = 39.0f;
    EXPECT_FALSE(Decide(BaseInputs(), party).latched);
}

TEST(DcSmartRestTest, TankManaUsesDpsTrigger)
{
    auto party = BaseParty();
    party[0].manaPct = 9.0f;   // tank shares the DPS trigger
    EXPECT_TRUE(Decide(BaseInputs(), party).latched);
    party[0].manaPct = 39.0f;  // healer trigger must NOT catch the tank
    EXPECT_FALSE(Decide(BaseInputs(), party).latched);
}

TEST(DcSmartRestTest, LowHpLatchesAnyRole)
{
    auto party = BaseParty();
    party[3].hpPct = 49.0f;    // the manaless melee
    EXPECT_TRUE(Decide(BaseInputs(), party).latched);
}

TEST(DcSmartRestTest, HumanBelowTriggerLatches)
{
    auto party = BaseParty();
    party[4].manaPct = 5.0f;
    EXPECT_TRUE(Decide(BaseInputs(), party).latched);
}

TEST(DcSmartRestTest, MeleeLowManaFieldIgnored)
{
    // A non-mana-user's manaPct is meaningless and must never latch.
    auto party = BaseParty();
    party[3].manaPct = 0.0f;
    EXPECT_FALSE(Decide(BaseInputs(), party).latched);
}

TEST(DcSmartRestTest, ZeroTriggerDisablesDimension)
{
    Inputs in = BaseInputs();
    in.hpTriggerPct = 0.0f;
    auto party = BaseParty();
    party[3].hpPct = 5.0f;
    EXPECT_FALSE(Decide(in, party).latched);

    in = BaseInputs();
    in.dpsManaTriggerPct = 0.0f;
    party = BaseParty();
    party[2].manaPct = 1.0f;
    EXPECT_FALSE(Decide(in, party).latched);

    in = BaseInputs();
    in.healerManaTriggerPct = 0.0f;
    party = BaseParty();
    party[1].manaPct = 1.0f;
    EXPECT_FALSE(Decide(in, party).latched);
}

TEST(DcSmartRestTest, AllTriggersZeroNeverLatches)
{
    Inputs in = BaseInputs();
    in.hpTriggerPct = 0.0f;
    in.dpsManaTriggerPct = 0.0f;
    in.healerManaTriggerPct = 0.0f;
    auto party = BaseParty();
    for (Member& m : party)
    {
        m.hpPct = 1.0f;
        m.manaPct = 1.0f;
    }
    EXPECT_FALSE(Decide(in, party).latched);
}

TEST(DcSmartRestTest, NotRearmedBlocksLatch)
{
    auto party = BaseParty();
    party[2].manaPct = 5.0f;
    Inputs in = BaseInputs();
    in.rearmed = false;
    EXPECT_FALSE(Decide(in, party).latched);
    in.rearmed = true;
    EXPECT_TRUE(Decide(in, party).latched);
}

// ---- boss-pull top-off entry ------------------------------------------------------

TEST(DcSmartRestTest, BossPullLatchesBelowManaBar)
{
    // 50% mana clears every trash trigger (DPS 10) but must not open a boss.
    auto party = BaseParty();
    party[2].manaPct = 50.0f;
    Inputs in = BaseInputs();
    ASSERT_FALSE(Decide(in, party).latched);  // trash pull: pushes on
    in.bossPull = true;
    Result const r = Decide(in, party);       // boss pull: tops off first
    EXPECT_TRUE(r.latched);
    ASSERT_EQ(r.blockers.size(), 1u);
    EXPECT_EQ(r.blockers[0], 2u);
}

TEST(DcSmartRestTest, BossPullAtManaBarDoesNotLatch)
{
    auto party = BaseParty();
    for (Member& m : party)
        m.manaPct = kManaReleasePct;  // everyone AT the 90 bar
    Inputs in = BaseInputs();
    in.bossPull = true;
    EXPECT_FALSE(Decide(in, party).latched);
}

TEST(DcSmartRestTest, BossPullHealerBelowBarLatches)
{
    // The exact reported failure: healer just above its 40 trigger pulls a boss.
    auto party = BaseParty();
    party[1].manaPct = 45.0f;
    Inputs in = BaseInputs();
    ASSERT_FALSE(Decide(in, party).latched);
    in.bossPull = true;
    EXPECT_TRUE(Decide(in, party).latched);
}

TEST(DcSmartRestTest, BossPullIgnoresNonManaUsers)
{
    auto party = BaseParty();
    party[3].manaPct = 0.0f;  // manaless melee — field meaningless
    Inputs in = BaseInputs();
    in.bossPull = true;
    EXPECT_FALSE(Decide(in, party).latched);
}

TEST(DcSmartRestTest, BossPullIsManaOnly)
{
    // HP above its trigger never blocks a boss pull — topping HP between pulls
    // is the healer's job, and hpTriggerPct still catches the genuinely hurt.
    auto party = BaseParty();
    party[3].hpPct = 80.0f;
    Inputs in = BaseInputs();
    in.bossPull = true;
    EXPECT_FALSE(Decide(in, party).latched);
}

TEST(DcSmartRestTest, BossPullHumanOwesTheSameBarAsBots)
{
    // A human at 50% mana holds the boss pull exactly as a bot would: the
    // boss entry is the 90% mana bar for every member. Waiting for the player
    // to drink up before a boss IS the feature (mod-dungeon-clear#6); the
    // maxRestMs failsafe, not a lower bar, is what bounds an AFK player.
    auto party = BaseParty();
    party[4].manaPct = 50.0f;
    Inputs in = BaseInputs();
    in.bossPull = true;
    EXPECT_TRUE(Decide(in, party).latched);

    party[4].manaPct = kManaReleasePct;  // topped off — pull is free to go
    EXPECT_FALSE(Decide(in, party).latched);
}

TEST(DcSmartRestTest, BossPullCannotInstantlyRelatchAfterRelease)
{
    // Hysteresis proof for the boss entry: a fresh release leaves every bot AT
    // its bars (hp 99.5 / mana 90), and the boss entry only fires strictly
    // below the mana bar.
    auto party = BaseParty();
    for (Member& m : party)
    {
        m.hpPct = kReleasePct;
        m.manaPct = kManaReleasePct;
    }
    ASSERT_FALSE(Decide(LatchedInputs(), party).latched);
    Inputs in = BaseInputs();
    in.bossPull = true;
    EXPECT_FALSE(Decide(in, party).latched);
}

TEST(DcSmartRestTest, BossPullRespectsRearmCooldown)
{
    // A timeout release (AFK human below its bar) must not flap straight back
    // into a boss-pull latch during the rearm window.
    auto party = BaseParty();
    party[2].manaPct = 50.0f;
    Inputs in = BaseInputs();
    in.bossPull = true;
    in.rearmed = false;
    EXPECT_FALSE(Decide(in, party).latched);
}

TEST(DcSmartRestTest, BossPullWorksWithAllTriggersDisabled)
{
    // The top-off is a property of the boss pull, not of any trigger dimension.
    auto party = BaseParty();
    party[2].manaPct = 50.0f;
    Inputs in = BaseInputs();
    in.hpTriggerPct = 0.0f;
    in.dpsManaTriggerPct = 0.0f;
    in.healerManaTriggerPct = 0.0f;
    in.bossPull = true;
    EXPECT_TRUE(Decide(in, party).latched);
}

// ---- holding / releasing the latch ----------------------------------------------

TEST(DcSmartRestTest, LatchedHoldsUntilBotsRested)
{
    auto party = BaseParty();
    party[2].manaPct = 85.0f;  // still drinking, short of the 90 mana bar
    Result const r = Decide(LatchedInputs(), party);
    EXPECT_TRUE(r.latched);
    ASSERT_EQ(r.blockers.size(), 1u);
    EXPECT_EQ(r.blockers[0], 2u);
}

TEST(DcSmartRestTest, BotManaReleasesAtBarNotFull)
{
    // The last ~10% of mana tops off for free while walking to the next pack,
    // so a bot at the 90 mana bar releases without clawing back to full.
    auto party = BaseParty();
    for (Member& m : party)
        m.manaPct = kManaReleasePct;
    EXPECT_FALSE(Decide(LatchedInputs(), party).latched);  // 90 releases

    party[2].manaPct = kManaReleasePct - 0.1f;
    EXPECT_TRUE(Decide(LatchedInputs(), party).latched);   // just under holds
}

TEST(DcSmartRestTest, BotHpReleaseEpsilon)
{
    // HP keeps the full float-safe bar (kReleasePct), independent of mana.
    auto party = BaseParty();
    for (Member& m : party)
        m.hpPct = kReleasePct;
    EXPECT_FALSE(Decide(LatchedInputs(), party).latched);  // 99.5 releases

    party[2].hpPct = 99.4f;
    EXPECT_TRUE(Decide(LatchedInputs(), party).latched);   // 99.4 holds

    party[2].hpPct = 100.0f;
    EXPECT_FALSE(Decide(LatchedInputs(), party).latched);  // 100.0 releases
}

TEST(DcSmartRestTest, LatchedHoldsOnBotHpToo)
{
    auto party = BaseParty();
    party[3].hpPct = 90.0f;    // above the 50 trigger but short of full
    EXPECT_TRUE(Decide(LatchedInputs(), party).latched);
}

TEST(DcSmartRestTest, ReleaseCannotInstantlyRelatch)
{
    // Hysteresis proof: the release state (everything at the release bar) is
    // strictly above every trigger, so an immediate re-eval stays unlatched.
    auto party = BaseParty();
    for (Member& m : party)
    {
        m.hpPct = kReleasePct;
        m.manaPct = kReleasePct;
    }
    ASSERT_FALSE(Decide(LatchedInputs(), party).latched);
    EXPECT_FALSE(Decide(BaseInputs(), party).latched);
}

// ---- humans: identical bars to bots (mod-dungeon-clear#6) -----------------------

TEST(DcSmartRestTest, HumanHoldsToTheSameManaBarAsBots)
{
    // The bug: the human used to release at its trigger + a 5% margin, so at
    // the default DPS trigger of 10 the party walked off the moment the player
    // hit 15% — a five-point "rest" that made Smart Rest look inert. The human
    // now holds the party until it reaches kManaReleasePct like everyone else.
    auto party = BaseParty();
    party[4].manaPct = 16.0f;
    Result const held = Decide(LatchedInputs(), party);
    EXPECT_TRUE(held.latched);
    ASSERT_EQ(held.blockers.size(), 1u);
    EXPECT_EQ(held.blockers[0], 4u);

    party[4].manaPct = 89.0f;  // still short of the bar — still held
    EXPECT_TRUE(Decide(LatchedInputs(), party).latched);

    party[4].manaPct = kManaReleasePct;
    EXPECT_FALSE(Decide(LatchedInputs(), party).latched);
}

TEST(DcSmartRestTest, HumanHpBarIsTheSameAsBots)
{
    auto party = BaseParty();
    party[4].hpPct = 56.0f;    // clears the old trigger+margin bar of 55...
    EXPECT_TRUE(Decide(LatchedInputs(), party).latched);  // ...but not kReleasePct
    party[4].hpPct = kReleasePct;
    EXPECT_FALSE(Decide(LatchedInputs(), party).latched);
}

TEST(DcSmartRestTest, HumanRestsToBarOnDisabledDimension)
{
    // A rest is a rest, for humans too: a disabled trigger only means the
    // human never STARTS a rest on that dimension, not that a rest already
    // under way ignores it.
    Inputs in = LatchedInputs();
    in.hpTriggerPct = 0.0f;
    auto party = BaseParty();
    party[4].hpPct = 3.0f;
    EXPECT_TRUE(Decide(in, party).latched);
}

TEST(DcSmartRestTest, AfkHumanBelowBarIsFreedByTheTimeout)
{
    // The failsafe is what bounds a player who never drinks — this is the
    // guard that lets humans owe the full bar without deadlocking the run.
    Inputs in = LatchedInputs();
    in.maxRestMs = 180000;
    auto party = BaseParty();
    party[4].manaPct = 4.0f;

    in.restElapsedMs = 179000;
    EXPECT_TRUE(Decide(in, party).latched);

    in.restElapsedMs = 180000;
    Result const freed = Decide(in, party);
    EXPECT_FALSE(freed.latched);
    EXPECT_TRUE(freed.timedOut);
}

TEST(DcSmartRestTest, BotsStillRestToBarOnDisabledDimension)
{
    // A rest is a rest: even with the mana trigger disabled, a bot mid-drink
    // holds the latch until it reaches the mana release bar.
    Inputs in = LatchedInputs();
    in.dpsManaTriggerPct = 0.0f;
    auto party = BaseParty();
    party[2].manaPct = 80.0f;  // below the 90 bar
    EXPECT_TRUE(Decide(in, party).latched);
}

// ---- timeout failsafe -----------------------------------------------------------

TEST(DcSmartRestTest, TimeoutReleases)
{
    auto party = BaseParty();
    party[4].manaPct = 5.0f;   // AFK human never drinks
    Inputs in = LatchedInputs();
    in.restElapsedMs = in.maxRestMs;
    Result const r = Decide(in, party);
    EXPECT_FALSE(r.latched);
    EXPECT_TRUE(r.timedOut);
}

TEST(DcSmartRestTest, ZeroMaxRestNeverTimesOut)
{
    auto party = BaseParty();
    party[4].manaPct = 5.0f;
    Inputs in = LatchedInputs();
    in.maxRestMs = 0;
    in.restElapsedMs = 0xFFFFFFF0u;
    Result const r = Decide(in, party);
    EXPECT_TRUE(r.latched);
    EXPECT_FALSE(r.timedOut);
}

TEST(DcSmartRestTest, NaturalReleaseIsNotTimedOut)
{
    Result const r = Decide(LatchedInputs(), BaseParty());
    EXPECT_FALSE(r.latched);
    EXPECT_FALSE(r.timedOut);
}

// ---- degenerates ----------------------------------------------------------------

TEST(DcSmartRestTest, EmptyPartyNeverLatches)
{
    EXPECT_FALSE(Decide(BaseInputs(), {}).latched);
    // A latched run whose snapshot went empty (everyone died/left) releases.
    EXPECT_FALSE(Decide(LatchedInputs(), {}).latched);
}

TEST(DcSmartRestTest, SoloTankLatchesOnItself)
{
    Member solo;
    solo.isManaUser = true;
    solo.manaPct = 5.0f;
    Result const r = Decide(BaseInputs(), {solo});
    EXPECT_TRUE(r.latched);
    ASSERT_EQ(r.blockers.size(), 1u);
    EXPECT_EQ(r.blockers[0], 0u);
}

TEST(DcSmartRestTest, MixedRolesBlockersNameOnlyTheTriggering)
{
    // Healer at 35% is below ITS trigger; a DPS at 35% is not.
    auto party = BaseParty();
    party[1].manaPct = 35.0f;
    party[2].manaPct = 35.0f;
    Result const r = Decide(BaseInputs(), party);
    EXPECT_TRUE(r.latched);
    ASSERT_EQ(r.blockers.size(), 1u);
    EXPECT_EQ(r.blockers[0], 1u);
}
