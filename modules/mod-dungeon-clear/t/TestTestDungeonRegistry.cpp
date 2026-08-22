/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <set>
#include <string>

#include "TestRun/DcTestDungeonRegistry.h"

using DcTestDungeonRegistry::All;
using DcTestDungeonRegistry::Find;
using DcTestDungeonRegistry::Row;

TEST(DcTestDungeonRegistryTest, TokensAreUnique)
{
    std::set<std::string> seen;
    for (Row const& row : All())
        EXPECT_TRUE(seen.insert(row.token).second) << "duplicate token: " << row.token;
}

TEST(DcTestDungeonRegistryTest, EveryRowIsPlausible)
{
    for (Row const& row : All())
    {
        EXPECT_NE(row.mapId, 0u) << row.token;
        EXPECT_GT(row.recommendedLevel, 0u) << row.token;
        EXPECT_LE(row.recommendedLevel, 80u) << row.token;
        EXPECT_STRNE(row.name, "") << row.token;
        // Entrance must be a real point, not a zero-initialized placeholder.
        EXPECT_TRUE(row.x != 0.f || row.y != 0.f) << row.token;
    }
}

TEST(DcTestDungeonRegistryTest, HeroicLevelMatchesTheExpansionCap)
{
    // heroicLevel is both the heroic default level AND the "heroic offered"
    // gate, and a heroic run is run at its expansion's cap: TBC rows carry 70,
    // WotLK rows 80. Classic dungeons have no heroic difficulty, so 0.
    std::set<std::uint32_t> const tbcMaps =
        {543, 542, 547, 546, 557, 558, 556, 560, 555, 545, 540, 269, 553, 554, 552, 585};
    std::set<std::uint32_t> const wotlkMaps =
        {574, 575, 576, 578, 595, 599, 600, 601, 602, 604, 608, 619, 632, 650, 658, 668};
    for (Row const& row : All())
    {
        if (tbcMaps.count(row.mapId))
            EXPECT_EQ(row.heroicLevel, 70u) << row.token;
        else if (wotlkMaps.count(row.mapId))
            EXPECT_EQ(row.heroicLevel, 80u) << row.token;
        else
            EXPECT_EQ(row.heroicLevel, 0u) << row.token;
    }
}

TEST(DcTestDungeonRegistryTest, FindByToken)
{
    Row const* row = Find("deadmines");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->mapId, 36u);
    EXPECT_STREQ(row->wing, "");
}

TEST(DcTestDungeonRegistryTest, FindByNumericMapId)
{
    Row const* row = Find("36");
    ASSERT_NE(row, nullptr);
    EXPECT_STREQ(row->token, "deadmines");
}

TEST(DcTestDungeonRegistryTest, NumericLookupOnWingSplitMapIsRefused)
{
    // 429 = Dire Maul, three isolated wings; a bare mapId cannot pick one.
    EXPECT_EQ(Find("429"), nullptr);
    // 189 = Scarlet Monastery, four wings.
    EXPECT_EQ(Find("189"), nullptr);
}

TEST(DcTestDungeonRegistryTest, WingSplitMapsHaveWingTokens)
{
    // Dire Maul: three wing rows, each labelled.
    int dmRows = 0;
    for (Row const& row : All())
        if (row.mapId == 429)
        {
            ++dmRows;
            EXPECT_STRNE(row.wing, "") << row.token;
        }
    EXPECT_EQ(dmRows, 3);

    // Scarlet Monastery: four wing rows.
    int smRows = 0;
    for (Row const& row : All())
        if (row.mapId == 189)
        {
            ++smRows;
            EXPECT_STRNE(row.wing, "") << row.token;
        }
    EXPECT_EQ(smRows, 4);

    // Maraudon's wings interconnect — one unlabelled row, findable by mapId.
    EXPECT_NE(Find("349"), nullptr);
}

TEST(DcTestDungeonRegistryTest, UnknownLookupsReturnNull)
{
    EXPECT_EQ(Find(""), nullptr);
    EXPECT_EQ(Find("naxxramas"), nullptr);
    EXPECT_EQ(Find("0"), nullptr);
    EXPECT_EQ(Find("99999"), nullptr);
    EXPECT_EQ(Find("36x"), nullptr);
}
