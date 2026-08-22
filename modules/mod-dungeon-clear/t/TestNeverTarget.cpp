/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "Ai/Dungeon/DungeonClear/Data/DcNeverTargetRegistry.h"

// The Nexus's Crystalline Frayer is the row this registry exists for. It cannot
// be killed while Ormorok lives (npc_crystalline_frayer::DamageTaken discards the
// lethal hit and parks the mob in a 90s seed pod, from which it returns at full
// health), and instance_nexus::KillAllFrayers kills every one of them the moment
// Ormorok dies — so no live frayer is ever worth the clear's attention, and one
// that IS killable is already a corpse.
TEST(DcNeverTargetRegistryTest, NexusCrystallineFrayerIsNeverAClearTarget)
{
    EXPECT_TRUE(DcNeverTargetRegistry::IsNeverTarget(576, 26793));
}

TEST(DcNeverTargetRegistryTest, TheRowIsScopedToItsOwnMapAndEntry)
{
    // Same entry on another map, and another entry on the same map, must both
    // pass — this table is a scalpel, not a species ban.
    EXPECT_FALSE(DcNeverTargetRegistry::IsNeverTarget(575, 26793));
    EXPECT_FALSE(DcNeverTargetRegistry::IsNeverTarget(576, 26794));  // Ormorok himself
    EXPECT_FALSE(DcNeverTargetRegistry::IsNeverTarget(576, 26723));  // Keristrasza
    EXPECT_FALSE(DcNeverTargetRegistry::IsNeverTarget(0, 0));
}

// The heroic twin (30528) must NOT be listed: Creature::InitEntry resolves
// difficulty_entry_1 into m_creatureInfo but calls SetEntry with the NORMAL
// entry, so a heroic frayer still answers to 26793. A row for 30528 would be
// dead data that reads as coverage.
TEST(DcNeverTargetRegistryTest, HeroicTwinIsNotListedBecauseGetEntryStaysNormal)
{
    EXPECT_FALSE(DcNeverTargetRegistry::IsNeverTarget(576, 30528));
}
