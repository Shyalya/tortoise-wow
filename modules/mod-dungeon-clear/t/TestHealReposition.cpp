/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cmath>

#include "DungeonClearMath.h"

using DungeonClearMath::HealCandidate;
using DungeonClearMath::HealTargetNone;
using DungeonClearMath::SelectHealTarget;
using DungeonClearMath::HealStandoffCandidates;

namespace
{
    constexpr float kFloor = 90.0f;
    constexpr float kBias = 15.0f;
}

// Nobody below the floor -> no target.
TEST(DungeonClearHealRepositionTest, AllHealthyNoTarget)
{
    std::vector<HealCandidate> m = {
        { 100.0f, false }, { 95.0f, true }, { 92.0f, false }
    };
    EXPECT_EQ(SelectHealTarget(m, kFloor, kBias), HealTargetNone);
}

// A single hurt member is picked.
TEST(DungeonClearHealRepositionTest, SingleHurtPicked)
{
    std::vector<HealCandidate> m = {
        { 100.0f, false }, { 40.0f, false }, { 95.0f, true }
    };
    EXPECT_EQ(SelectHealTarget(m, kFloor, kBias), 1u);
}

// Tank bias breaks the pick toward the tank when both need healing and are
// close in health (tank 85 vs dps 80: 85-15=70 < 80).
TEST(DungeonClearHealRepositionTest, TankBiasFavoursTank)
{
    std::vector<HealCandidate> m = {
        { 80.0f, false },  // dps, idx 0
        { 85.0f, true }    // tank, idx 1
    };
    EXPECT_EQ(SelectHealTarget(m, kFloor, kBias), 1u);
}

// A HEALTHY tank above the floor never steals the pick from a hurt dps — the
// "needs healing" gate is on raw health, applied before the bias.
TEST(DungeonClearHealRepositionTest, HealthyTankNeverStealsPick)
{
    std::vector<HealCandidate> m = {
        { 80.0f, false },  // dps, idx 0 (hurt)
        { 95.0f, true }    // tank, idx 1 (above floor -> excluded)
    };
    EXPECT_EQ(SelectHealTarget(m, kFloor, kBias), 0u);
}

// A clearly more-hurt dps still beats the tank even with the bias.
TEST(DungeonClearHealRepositionTest, MuchLowerDpsBeatsTank)
{
    std::vector<HealCandidate> m = {
        { 20.0f, false },  // dps, idx 0
        { 88.0f, true }    // tank, idx 1 (88-15=73 > 20)
    };
    EXPECT_EQ(SelectHealTarget(m, kFloor, kBias), 0u);
}

// Empty input is handled.
TEST(DungeonClearHealRepositionTest, EmptyNoTarget)
{
    std::vector<HealCandidate> m;
    EXPECT_EQ(SelectHealTarget(m, kFloor, kBias), HealTargetNone);
}

// Candidate count is ringPoints + 1.
TEST(DungeonClearHealRepositionTest, StandoffCount)
{
    Position target(100.0f, 100.0f, 50.0f, 0.0f);
    Position bot(80.0f, 100.0f, 50.0f, 0.0f);
    auto pts = HealStandoffCandidates(target, bot, 10.0f, 7);
    EXPECT_EQ(pts.size(), 8u);
}

// Every candidate lies on the standoff circle around the target.
TEST(DungeonClearHealRepositionTest, StandoffOnCircle)
{
    Position target(100.0f, 100.0f, 50.0f, 0.0f);
    Position bot(60.0f, 130.0f, 50.0f, 0.0f);
    float const r = 12.0f;
    auto pts = HealStandoffCandidates(target, bot, r, 7);
    for (Position const& p : pts)
    {
        float const dx = p.GetPositionX() - target.GetPositionX();
        float const dy = p.GetPositionY() - target.GetPositionY();
        EXPECT_NEAR(std::sqrt(dx * dx + dy * dy), r, 1e-2f);
    }
}

// First candidate is on the bot's side of the target (shortest reposition):
// it is the closest of all candidates to the bot.
TEST(DungeonClearHealRepositionTest, StandoffFirstIsBotSide)
{
    Position target(100.0f, 100.0f, 50.0f, 0.0f);
    Position bot(70.0f, 100.0f, 50.0f, 0.0f);  // due -X of target
    auto pts = HealStandoffCandidates(target, bot, 10.0f, 7);
    ASSERT_FALSE(pts.empty());
    // First candidate should sit between target and bot (x ~ 90).
    EXPECT_NEAR(pts[0].GetPositionX(), 90.0f, 1e-2f);
    EXPECT_NEAR(pts[0].GetPositionY(), 100.0f, 1e-2f);

    float const fdx = pts[0].GetPositionX() - bot.GetPositionX();
    float const fdy = pts[0].GetPositionY() - bot.GetPositionY();
    float const firstDist = std::sqrt(fdx * fdx + fdy * fdy);
    for (std::size_t i = 1; i < pts.size(); ++i)
    {
        float const dx = pts[i].GetPositionX() - bot.GetPositionX();
        float const dy = pts[i].GetPositionY() - bot.GetPositionY();
        EXPECT_LE(firstDist, std::sqrt(dx * dx + dy * dy) + 1e-3f);
    }
}

// Degenerate bot-on-target input falls back to +X without NaNs.
TEST(DungeonClearHealRepositionTest, StandoffDegenerate)
{
    Position target(100.0f, 100.0f, 50.0f, 0.0f);
    Position bot(100.0f, 100.0f, 50.0f, 0.0f);
    auto pts = HealStandoffCandidates(target, bot, 10.0f, 7);
    ASSERT_EQ(pts.size(), 8u);
    EXPECT_NEAR(pts[0].GetPositionX(), 110.0f, 1e-2f);
    EXPECT_NEAR(pts[0].GetPositionY(), 100.0f, 1e-2f);
}
