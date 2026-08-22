/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcStrandedDecision.h"

using DcStrandedDecision::Decide;
using DcStrandedDecision::Inputs;
using DcStrandedDecision::Member;
using DcStrandedDecision::Result;

namespace
{
    // A four-member party, all bots, all alive, all on-map, all in range of the
    // tank at [0]. Individual tests strand/kill/relocate members off this base.
    std::vector<Member> BaseParty()
    {
        auto make = [](bool tank, float dist)
        {
            Member m;
            m.isBot = true;
            m.isAlive = true;
            m.onMap = true;
            m.isTank = tank;
            m.distToTank = dist;
            return m;
        };
        return {
            make(/*tank*/ true, 0.0f),   // [0] leader tank
            make(false, 5.0f),           // [1] healer, in range
            make(false, 8.0f),           // [2] dps, in range
            make(false, 10.0f),          // [3] dps, in range
        };
    }

    // Clock armed and already stale: nowMs is one timeout past lastProgressMs, so
    // the no-progress window has elapsed. maxSpread 25 (the PartyMaxSpread default).
    Inputs StaleInputs()
    {
        Inputs in;
        in.enabled = true;
        in.nowMs = 400000;
        in.lastProgressMs = 100000;       // 300s ago ...
        in.noProgressTimeoutMs = 300000;  // ... == the window: elapsed
        in.partyEngaged = false;
        in.maxSpread = 25.0f;
        return in;
    }

    // Strand member `idx` at `dist` yards from the tank.
    void Strand(std::vector<Member>& p, std::size_t idx, float dist)
    {
        p[idx].distToTank = dist;
    }
}

// --- the two guards: window elapsed AND a stranded bot -----------------------

TEST(DcStrandedRecoveryTest, NoStrayNoRecover)
{
    // Window elapsed but everyone in range -> nothing to do.
    Result const r = Decide(StaleInputs(), BaseParty());
    EXPECT_FALSE(r.recover);
    EXPECT_TRUE(r.strandedIdx.empty());
}

TEST(DcStrandedRecoveryTest, StrandedBotPastWindowRecovers)
{
    std::vector<Member> party = BaseParty();
    Strand(party, 2, 60.0f);  // [2] fell out to 60yd
    Result const r = Decide(StaleInputs(), party);
    ASSERT_TRUE(r.recover);
    ASSERT_EQ(r.strandedIdx.size(), 1u);
    EXPECT_EQ(r.strandedIdx[0], 2);
}

TEST(DcStrandedRecoveryTest, MultipleStrandedAllSelected)
{
    std::vector<Member> party = BaseParty();
    Strand(party, 1, 40.0f);
    Strand(party, 3, 200.0f);  // fell under the world (huge 3D distance)
    Result const r = Decide(StaleInputs(), party);
    ASSERT_TRUE(r.recover);
    ASSERT_EQ(r.strandedIdx.size(), 2u);
    EXPECT_EQ(r.strandedIdx[0], 1);
    EXPECT_EQ(r.strandedIdx[1], 3);
}

// --- the timing guard --------------------------------------------------------

TEST(DcStrandedRecoveryTest, WindowNotElapsedNeverRecovers)
{
    Inputs in = StaleInputs();
    in.nowMs = in.lastProgressMs + in.noProgressTimeoutMs - 1;  // one ms short
    std::vector<Member> party = BaseParty();
    Strand(party, 2, 60.0f);
    EXPECT_FALSE(Decide(in, party).recover);
}

TEST(DcStrandedRecoveryTest, ExactlyAtWindowRecovers)
{
    Inputs in = StaleInputs();
    in.nowMs = in.lastProgressMs + in.noProgressTimeoutMs;  // exactly the window
    std::vector<Member> party = BaseParty();
    Strand(party, 2, 60.0f);
    EXPECT_TRUE(Decide(in, party).recover);
}

TEST(DcStrandedRecoveryTest, UnarmedClockNeverRecovers)
{
    Inputs in = StaleInputs();
    in.lastProgressMs = 0;  // clock never stamped (no run underway)
    std::vector<Member> party = BaseParty();
    Strand(party, 2, 60.0f);
    EXPECT_FALSE(Decide(in, party).recover);
}

TEST(DcStrandedRecoveryTest, ZeroTimeoutDisablesTheClock)
{
    Inputs in = StaleInputs();
    in.noProgressTimeoutMs = 0;  // "never give up"
    std::vector<Member> party = BaseParty();
    Strand(party, 2, 60.0f);
    EXPECT_FALSE(Decide(in, party).recover);
}

// --- the feature / combat gates ----------------------------------------------

TEST(DcStrandedRecoveryTest, FeatureOffNeverRecovers)
{
    Inputs in = StaleInputs();
    in.enabled = false;
    std::vector<Member> party = BaseParty();
    Strand(party, 2, 60.0f);
    EXPECT_FALSE(Decide(in, party).recover);
}

TEST(DcStrandedRecoveryTest, CombatNeverRecovers)
{
    Inputs in = StaleInputs();
    in.partyEngaged = true;  // a fight is progress; never teleport mid-fight
    std::vector<Member> party = BaseParty();
    Strand(party, 2, 60.0f);
    EXPECT_FALSE(Decide(in, party).recover);
}

TEST(DcStrandedRecoveryTest, APhantomCombatFlagStillRecovers)
{
    // The Arcatraz freeze (tr-20260801-194932-20). A 45yd hostile area aura holds
    // the whole party's combat flag with NOTHING aggroed, so followers stop where
    // they stand and the tank waits on them forever. The glue used to pass the raw
    // flag in here, which made this failsafe — the only thing that could have
    // broken that deadlock — permanently unreachable. `partyEngaged` is a fight,
    // and a flag with no fight behind it is not one.
    Inputs in = StaleInputs();
    in.partyEngaged = false;  // flagged, but nothing is fighting
    std::vector<Member> party = BaseParty();
    Strand(party, 2, 30.0f);
    Strand(party, 3, 31.0f);
    Result const r = Decide(in, party);
    EXPECT_TRUE(r.recover);
    EXPECT_EQ(r.strandedIdx.size(), 2u);
}

// --- who qualifies as a stray ------------------------------------------------

TEST(DcStrandedRecoveryTest, HumanIsNeverTeleported)
{
    std::vector<Member> party = BaseParty();
    party[2].isBot = false;   // the real player's seat
    Strand(party, 2, 80.0f);  // stranded, but a human — respect player agency
    EXPECT_FALSE(Decide(StaleInputs(), party).recover);
}

TEST(DcStrandedRecoveryTest, DeadMemberIsRezRecoverysJobNotOurs)
{
    std::vector<Member> party = BaseParty();
    party[2].isAlive = false;
    Strand(party, 2, 80.0f);
    EXPECT_FALSE(Decide(StaleInputs(), party).recover);
}

TEST(DcStrandedRecoveryTest, OffMapMemberIsIgnored)
{
    std::vector<Member> party = BaseParty();
    party[2].onMap = false;   // zoned out / different instance
    Strand(party, 2, 80.0f);
    EXPECT_FALSE(Decide(StaleInputs(), party).recover);
}

TEST(DcStrandedRecoveryTest, TankItselfIsNeverAStray)
{
    std::vector<Member> party = BaseParty();
    Strand(party, 0, 90.0f);  // the tank's own distance reads high (defensive)
    EXPECT_FALSE(Decide(StaleInputs(), party).recover);
}

TEST(DcStrandedRecoveryTest, RangeThresholdIsStrictlyGreater)
{
    Inputs const in = StaleInputs();  // maxSpread 25
    std::vector<Member> party = BaseParty();

    Strand(party, 2, 25.0f);          // exactly at the spread: still in range
    EXPECT_FALSE(Decide(in, party).recover);

    Strand(party, 2, 25.01f);         // just past it: stranded
    EXPECT_TRUE(Decide(in, party).recover);
}
