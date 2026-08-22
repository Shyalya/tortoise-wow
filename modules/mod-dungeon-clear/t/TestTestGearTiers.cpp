/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>

#include "TestRun/DcTestDungeonRegistry.h"
#include "TestRun/DcTestGearTiers.h"

using namespace DcTestGearTiers;

namespace
{
    bool HasIlvl(std::vector<Choice> const& choices, std::uint32_t ilvl)
    {
        return std::any_of(choices.begin(), choices.end(),
                           [ilvl](Choice const& c) { return c.ilvl == ilvl; });
    }
}

TEST(DcTestGearTiersTest, ExpansionOfKnownMaps)
{
    EXPECT_EQ(ExpansionOf(36), Expansion::Classic);   // Deadmines
    EXPECT_EQ(ExpansionOf(429), Expansion::Classic);  // Dire Maul
    EXPECT_EQ(ExpansionOf(554), Expansion::Tbc);      // Mechanar
    EXPECT_EQ(ExpansionOf(269), Expansion::Tbc);      // Black Morass (CoT, TBC content)
    EXPECT_EQ(ExpansionOf(560), Expansion::Tbc);      // Old Hillsbrad (CoT, TBC content)
    EXPECT_EQ(ExpansionOf(595), Expansion::Wotlk);    // Culling of Stratholme (CoT, WotLK)
    EXPECT_EQ(ExpansionOf(668), Expansion::Wotlk);    // Halls of Reflection
}

TEST(DcTestGearTiersTest, EveryRegistryRowIsClassified)
{
    // A row whose map is in neither the TBC nor the WotLK list silently gets
    // the classic ladder, which would be wrong (and invisible) for a new row.
    // Pin the two things that must hold: a row offering heroic runs it at its
    // own expansion's cap (so the heroic ladder is that expansion's named tier
    // list, not a leveling band), and no row lands on an expansion whose cap is
    // below its own level.
    for (DcTestDungeonRegistry::Row const& row : DcTestDungeonRegistry::All())
    {
        Expansion const exp = ExpansionOf(row.mapId);
        std::uint32_t const cap = exp == Expansion::Classic ? 60 : (exp == Expansion::Tbc ? 70 : 80);
        if (row.heroicLevel)
        {
            EXPECT_NE(exp, Expansion::Classic) << row.token;
            EXPECT_EQ(row.heroicLevel, cap) << row.token;
        }
        EXPECT_LE(row.recommendedLevel, cap) << row.token;
    }
}

TEST(DcTestGearTiersTest, DungeonGearIlvlMatchesTheAnchors)
{
    // Classic is level + 5 across its whole range (checked against
    // item_template: max ItemLevel for gear requiring that level).
    EXPECT_EQ(DungeonGearIlvl(Expansion::Classic, 24), 29u);
    EXPECT_EQ(DungeonGearIlvl(Expansion::Classic, 40), 45u);
    EXPECT_EQ(DungeonGearIlvl(Expansion::Classic, 60), 65u);
    // TBC and WotLK inflate much faster than one ilvl per level.
    EXPECT_EQ(DungeonGearIlvl(Expansion::Tbc, 58), 65u);
    EXPECT_EQ(DungeonGearIlvl(Expansion::Tbc, 70), 115u);
    EXPECT_EQ(DungeonGearIlvl(Expansion::Wotlk, 70), 138u);
    EXPECT_EQ(DungeonGearIlvl(Expansion::Wotlk, 80), 187u);
    // Monotone in between, and clamped outside.
    for (std::uint32_t lvl = 59; lvl < 70; ++lvl)
        EXPECT_LE(DungeonGearIlvl(Expansion::Tbc, lvl), DungeonGearIlvl(Expansion::Tbc, lvl + 1));
    EXPECT_EQ(DungeonGearIlvl(Expansion::Wotlk, 12), DungeonGearIlvl(Expansion::Wotlk, 70));
}

TEST(DcTestGearTiersTest, LevelingLadderIsThreeStepsAroundDungeonGear)
{
    // Uldaman, level 44: nothing in the raid tables is wearable, so the ladder
    // brackets what the dungeon itself drops.
    std::vector<Choice> const ladder = Ladder(70, 44);
    ASSERT_EQ(ladder.size(), 3u);
    EXPECT_LT(ladder[0].ilvl, ladder[1].ilvl);
    EXPECT_LT(ladder[1].ilvl, ladder[2].ilvl);
    EXPECT_EQ(ladder[1].ilvl, DungeonGearIlvl(Expansion::Classic, 44));

    // Three choices that gear the party the same way are not three choices:
    // every step is at least a few item levels from its neighbour, including
    // down in the teens where a percentage band would round them together.
    for (std::uint32_t level = 15; level < 60; ++level)
    {
        std::vector<Choice> const rung = Ladder(36, level);  // Deadmines (classic)
        ASSERT_EQ(rung.size(), 3u) << level;
        EXPECT_GE(rung[1].ilvl - rung[0].ilvl, 3u) << level;
        EXPECT_GE(rung[2].ilvl - rung[1].ilvl, 3u) << level;
    }
    for (Choice const& c : ladder)
        EXPECT_NE(c.label.find(std::to_string(c.ilvl)), std::string::npos) << c.label;
}

TEST(DcTestGearTiersTest, EndgameLadderIsTheDocumentedTiers)
{
    // Mechanar heroic (level 70) — the TBC ceilings from the
    // AiPlayerbot.AutoGearScoreLimit comment block, plus the pre-raid floor.
    std::vector<Choice> const tbc = Ladder(554, 70);
    EXPECT_TRUE(HasIlvl(tbc, 115));  // pre-raid / heroic 5-man
    EXPECT_TRUE(HasIlvl(tbc, 120));
    EXPECT_TRUE(HasIlvl(tbc, 125));
    EXPECT_TRUE(HasIlvl(tbc, 141));
    EXPECT_TRUE(HasIlvl(tbc, 164));
    EXPECT_FALSE(HasIlvl(tbc, 200));  // WotLK ceilings never leak into a TBC run

    std::vector<Choice> const classic = Ladder(329, 60);  // Stratholme
    EXPECT_TRUE(HasIlvl(classic, 66));
    EXPECT_TRUE(HasIlvl(classic, 92));
    EXPECT_FALSE(HasIlvl(classic, 120));

    std::vector<Choice> const wotlk = Ladder(668, 80);  // Halls of Reflection
    EXPECT_TRUE(HasIlvl(wotlk, 200));
    EXPECT_TRUE(HasIlvl(wotlk, 290));
    EXPECT_FALSE(HasIlvl(wotlk, 92));
}

TEST(DcTestGearTiersTest, LaddersAreAscendingAndDistinct)
{
    for (DcTestDungeonRegistry::Row const& row : DcTestDungeonRegistry::All())
        for (std::uint32_t level : {row.recommendedLevel, row.heroicLevel})
        {
            if (!level)
                continue;
            std::vector<Choice> const ladder = Ladder(row.mapId, level);
            ASSERT_FALSE(ladder.empty()) << row.token;
            for (std::size_t i = 1; i < ladder.size(); ++i)
                EXPECT_LT(ladder[i - 1].ilvl, ladder[i].ilvl) << row.token;
            for (Choice const& c : ladder)
            {
                EXPECT_GT(c.ilvl, 0u) << row.token;
                EXPECT_FALSE(c.label.empty()) << row.token;
            }
        }
}

TEST(DcTestGearTiersTest, ResolveInheritsTheConfWhenNothingIsAsked)
{
    Resolved const r = Resolve(Spec{}, 125, 4);
    EXPECT_EQ(r.ilvl, 125u);
    EXPECT_EQ(r.quality, 4u);
}

TEST(DcTestGearTiersTest, ResolveHonoursTheOverrides)
{
    Spec spec;
    spec.ilvl = 164;
    spec.quality = 3;
    Resolved const r = Resolve(spec, 125, 4);
    EXPECT_EQ(r.ilvl, 164u);
    EXPECT_EQ(r.quality, 3u);
}

TEST(DcTestGearTiersTest, ResolveNoLimitBeatsAConfCap)
{
    Spec spec;
    spec.ilvl = kNoLimit;
    Resolved const r = Resolve(spec, 125, 4);
    EXPECT_EQ(r.ilvl, 0u);    // 0 = uncapped, the conf's own encoding
    EXPECT_EQ(r.quality, 4u);
}

TEST(DcTestGearTiersTest, ResolveNeverYieldsQualityZero)
{
    // A 0 quality would make PlayerbotFactory discard the item-level cap and
    // fall back to the RandomGear* settings — the resolved pair must never
    // carry it, however the conf is set.
    EXPECT_EQ(Resolve(Spec{}, 125, 0).quality, 3u);
    EXPECT_EQ(Resolve(Spec{}, 0, -1).quality, 3u);
    EXPECT_EQ(Resolve(Spec{}, 0, 0).ilvl, 0u);
}

TEST(DcTestGearTiersTest, ParseIlvlAcceptsNumbersAndTheNoLimitWords)
{
    bool ok = false;
    EXPECT_EQ(ParseIlvl("125", &ok), 125);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseIlvl("none", &ok), kNoLimit);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseIlvl("off", &ok), kNoLimit);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseIlvl("0", &ok), kNoLimit);  // matches "0 = no limit" in the conf
    EXPECT_TRUE(ok);

    ParseIlvl("", &ok);
    EXPECT_FALSE(ok);
    ParseIlvl("epic", &ok);
    EXPECT_FALSE(ok);
    ParseIlvl("125x", &ok);
    EXPECT_FALSE(ok);
    ParseIlvl("12500", &ok);
    EXPECT_FALSE(ok);  // a typo must not read as "unlimited"
}

TEST(DcTestGearTiersTest, ParseQualityTakesNamesAndNumbers)
{
    EXPECT_EQ(ParseQuality("epic"), 4u);
    EXPECT_EQ(ParseQuality("4"), 4u);
    EXPECT_EQ(ParseQuality("blue"), 3u);
    EXPECT_EQ(ParseQuality("legendary"), 5u);
    EXPECT_EQ(ParseQuality(""), 0u);
    EXPECT_EQ(ParseQuality("6"), 0u);
    EXPECT_EQ(ParseQuality("shiny"), 0u);
}

TEST(DcTestGearTiersTest, DescribeReadsAsASentenceFragment)
{
    EXPECT_EQ(Describe(Spec{}), "server default");

    Spec capped;
    capped.ilvl = 141;
    EXPECT_EQ(Describe(capped), "ilvl<=141");

    capped.quality = 4;
    EXPECT_EQ(Describe(capped), "ilvl<=141 epic");

    Spec unlimited;
    unlimited.ilvl = kNoLimit;
    EXPECT_EQ(Describe(unlimited), "unlimited ilvl");
}
