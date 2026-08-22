/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "Ai/Dungeon/DungeonClear/Data/DcFactionEntrySwapRegistry.h"

// The Nexus' Frozen Commander is the row this registry exists for. creature_data
// holds one spawn, entered as 26796 Commander Stoutbeard;
// instance_nexus::OnCreatureCreate UpdateEntry's it to 26798 Commander Kolurg
// when the instance's team is Alliance. Without the rewrite the derived roster
// names a creature that is not in the world, and Advance hard-stalls "not
// spawned" 150yd out — on the FIRST anchor of a heroic run.
TEST(DcFactionEntrySwapTest, NexusFrozenCommanderSwapsForAlliance)
{
    EXPECT_EQ(DcFactionEntrySwap::Resolve(576, 26796, TEAM_ALLIANCE), 26798u);
}

TEST(DcFactionEntrySwapTest, HordeKeepsTheSpawnEntry)
{
    // A Horde instance never runs UpdateEntry, so 26796 IS what stands there.
    EXPECT_EQ(DcFactionEntrySwap::Resolve(576, 26796, TEAM_HORDE), 26796u);

    // TEAM_NEUTRAL is what a non-two-faction instance reports; no rule may fire
    // on it, or a bot in a map that never stamped a team would chase a ghost.
    EXPECT_EQ(DcFactionEntrySwap::Resolve(576, 26796, TEAM_NEUTRAL), 26796u);
}

TEST(DcFactionEntrySwapTest, TheRuleIsScopedToItsOwnMapAndEntry)
{
    // The other four Nexus bosses have one entry each on both factions.
    EXPECT_EQ(DcFactionEntrySwap::Resolve(576, 26731, TEAM_ALLIANCE), 26731u);  // Telestra
    EXPECT_EQ(DcFactionEntrySwap::Resolve(576, 26763, TEAM_ALLIANCE), 26763u);  // Anomalus
    EXPECT_EQ(DcFactionEntrySwap::Resolve(576, 26794, TEAM_ALLIANCE), 26794u);  // Ormorok
    EXPECT_EQ(DcFactionEntrySwap::Resolve(576, 26723, TEAM_ALLIANCE), 26723u);  // Keristrasza

    // Same entry on another map must pass through untouched.
    EXPECT_EQ(DcFactionEntrySwap::Resolve(575, 26796, TEAM_ALLIANCE), 26796u);
    EXPECT_EQ(DcFactionEntrySwap::Resolve(0, 0, TEAM_ALLIANCE), 0u);
}

// The rewrite is one-way: it maps the SPAWN entry to the live one. Feeding it an
// already-swapped entry must be a no-op, so re-running the pass over a roster it
// has already touched (the value is recomputed every cache interval) can never
// walk the entry somewhere else.
TEST(DcFactionEntrySwapTest, ResolveIsIdempotent)
{
    uint32 const once = DcFactionEntrySwap::Resolve(576, 26796, TEAM_ALLIANCE);
    EXPECT_EQ(DcFactionEntrySwap::Resolve(576, once, TEAM_ALLIANCE), once);
}

// HasRules is the caller's cheap gate — it must be true for exactly the maps the
// table covers, or the swap silently stops running.
TEST(DcFactionEntrySwapTest, HasRulesGatesOnlyTheCoveredMaps)
{
    EXPECT_TRUE(DcFactionEntrySwap::HasRules(576));   // The Nexus
    EXPECT_FALSE(DcFactionEntrySwap::HasRules(575));  // Utgarde Keep
    EXPECT_FALSE(DcFactionEntrySwap::HasRules(0));
}

// Every rule must actually swap something, on a real team. A row whose spawn and
// swapped entries match, or that is pinned to TEAM_NEUTRAL, is dead data that
// reads as coverage.
TEST(DcFactionEntrySwapTest, EveryRuleIsWellFormed)
{
    for (DcFactionEntrySwap::Rule const& rule : DcFactionEntrySwap::kRules)
    {
        EXPECT_NE(rule.spawnEntry, 0u);
        EXPECT_NE(rule.swappedEntry, 0u);
        EXPECT_NE(rule.spawnEntry, rule.swappedEntry) << "map " << rule.mapId;
        EXPECT_TRUE(rule.swappedForTeam == TEAM_ALLIANCE || rule.swappedForTeam == TEAM_HORDE)
            << "map " << rule.mapId;

        // No rule may target an entry another rule already produces — that would
        // make the pass order-dependent and break idempotence.
        for (DcFactionEntrySwap::Rule const& other : DcFactionEntrySwap::kRules)
            if (other.mapId == rule.mapId)
                EXPECT_NE(other.spawnEntry, rule.swappedEntry)
                    << "map " << rule.mapId << " chains " << rule.spawnEntry
                    << " -> " << rule.swappedEntry << " -> ...";
    }
}
