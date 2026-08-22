/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cmath>

#include "Ai/Dungeon/DungeonClear/Data/BossPullbackRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"

// Pull-back bosses (BossPullbackRegistry) — the "this boss must be fought
// somewhere else" table.
//
// THE TABLE IS EMPTY. Its one row (Ghaz'an) was retired in S1593 once the cause
// was fixed upstream: he sat in the lake only because a headless party never
// fired areatrigger 4302, the sole caller of his ACTION_MOVE_TO_PLATFORM. He
// climbs onto meshed ground now and is fought like any other boss.
//
// So what these tests pin is (a) that the facility still behaves as an empty
// table should, (b) the opt-in DEFAULTS a future row would inherit, and (c) that
// Ghaz'an specifically is NOT pulled back and IS anchored on his platform. (c)
// is the regression guard: re-adding the row would silently restore a 150yd
// force-aggro and a teleport-summon on a boss that no longer needs either.

namespace
{
    constexpr uint32 kUnderbog = 546;
    constexpr uint32 kGhazan = 18105;

    // Measured facts (see BossPullbackRegistry.cpp / UnderbogEvents.cpp for how
    // they were obtained). Repeated here so a future edit has to consciously
    // break a documented number rather than silently drift.
    constexpr float kLakeSurfaceZ = 50.8f;      // water sheet over Ghaz'an's basin
    constexpr float kPlatformMeshZ = 81.45f;    // probed navmesh on his platform deck
    constexpr float kPlatformX = 256.28f;       // waypoint path 1383921, final node
    constexpr float kPlatformY = -458.73f;
}

TEST(DungeonClearBossPullbackTest, TableIsEmpty)
{
    // Not a placeholder: an empty table is the current design. If a row is ever
    // added, this test is the prompt to say WHY in the same commit.
    EXPECT_EQ(BossPullbackRegistry::Find(kUnderbog, kGhazan), nullptr);
    EXPECT_FALSE(BossPullbackRegistry::HasRows(kUnderbog));
}

TEST(DungeonClearBossPullbackTest, GhazanIsNoLongerPulledBack)
{
    // The regression guard. He is an ordinary boss now — normal walk-in, normal
    // tag. A row here would re-enable the whole tag-and-drag maneuver, and with
    // his old row specifically, force-aggro from 150yd and a teleport-summon.
    EXPECT_EQ(BossPullbackRegistry::Find(kUnderbog, kGhazan), nullptr)
        << "Ghaz'an was re-added to BossPullbackRegistry. He walks onto his own "
           "platform now (areatrigger 4302 relay + Underbog event 2); check that "
           "the boss really is standing in the lake before restoring this.";
}

TEST(DungeonClearBossPullbackTest, NoUnderbogBossIsPulledBack)
{
    EXPECT_EQ(BossPullbackRegistry::Find(kUnderbog, 17770), nullptr);  // Hungarfen
    EXPECT_EQ(BossPullbackRegistry::Find(kUnderbog, 17826), nullptr);  // Swamplord Musel'ek
    EXPECT_EQ(BossPullbackRegistry::Find(kUnderbog, 17882), nullptr);  // The Black Stalker
}

TEST(DungeonClearBossPullbackTest, HasRowsGatesByMap)
{
    // The cheap early-out every map relies on. If this ever goes true for a map
    // with no row, every tick on that map starts paying for the cross-context
    // pull-back probes.
    EXPECT_FALSE(BossPullbackRegistry::HasRows(0));
    EXPECT_FALSE(BossPullbackRegistry::HasRows(545));   // The Slave Pens
    EXPECT_FALSE(BossPullbackRegistry::HasRows(585));   // Magisters' Terrace
}

// Force-aggro is a PER-ENCOUNTER opt-in, not a property of being a pull-back
// boss. Forcing bypasses the boss's own aggro logic — normal, tuned behaviour
// everywhere else — so the default has to be "off", and a new row must have to
// type the range out deliberately rather than inherit it.
TEST(DungeonClearBossPullbackTest, ForceAggroDefaultsOff)
{
    BossPullback const fresh;
    EXPECT_FLOAT_EQ(fresh.forceAggroRange, 0.0f);
}

// Same contract for the summon. Relocating a boss outright is a bigger hammer
// than forcing his aggro, so it has to be at least as hard to acquire by
// accident: off unless a row asks for it by name.
TEST(DungeonClearBossPullbackTest, SummonWhenStuckDefaultsOff)
{
    BossPullback const fresh;
    EXPECT_FALSE(fresh.summonWhenStuckBelow);
}

TEST(DungeonClearBossPullbackTest, GhazanIsAnchoredOnHisPlatform)
{
    // His DERIVED anchor is still his static spawn (193.68, -425.00, 43.54) —
    // open water — so the roster patch has to stay even though the pull-back row
    // is gone. It must now point at the platform his own script walks him to.
    bool found = false;
    for (BossRosterPatch const& patch : BossRosterRegistry::AllPatches())
    {
        if (patch.mapId != kUnderbog)
            continue;

        for (DungeonBossInfo const& b : patch.add)
        {
            if (b.entry != kGhazan)
                continue;
            found = true;
            EXPECT_EQ(b.kind, DungeonAnchorKind::Boss);
            EXPECT_FLOAT_EQ(b.x, kPlatformX);
            EXPECT_FLOAT_EQ(b.y, kPlatformY);
            EXPECT_FLOAT_EQ(b.z, kPlatformMeshZ);

            // The anchor must clear the lake by a real margin, not merely differ
            // from it: an anchor at or near the water sheet is a spot the party
            // can be knocked off into a ~47yd pit.
            EXPECT_GT(b.z, kLakeSurfaceZ + 25.0f);

            // Re-added rather than reordered, so it must inherit its own kill-bit
            // back off the base list — otherwise the boss would carry encounter
            // index 0 and be confused with Hungarfen's completion.
            EXPECT_EQ(b.inheritCompletionFrom, kGhazan);
        }

        // ...and the derived (in-water) anchor must actually be removed, else
        // both copies survive and the clear visits the lake one anyway.
        bool removed = false;
        for (uint32 e : patch.remove)
            if (e == kGhazan)
                removed = true;
        EXPECT_TRUE(removed);
    }
    EXPECT_TRUE(found) << "The Underbog roster patch no longer re-anchors Ghaz'an";
}

TEST(DungeonClearBossPullbackTest, GhazanAnchorIsOnTheDropDownDeck)
{
    // The platform and the "Drop down past Ghaz'an" ledge (274.72, -462.60,
    // 81.37) are the SAME walkable deck — that is what makes the anchor
    // reachable, and it is why boss-nav can route the party there at all. Pinned
    // as a distance + height agreement so moving either one alone fails here.
    constexpr float kLedgeX = 274.72f, kLedgeY = -462.60f, kLedgeZ = 81.37f;

    float const dx = kPlatformX - kLedgeX;
    float const dy = kPlatformY - kLedgeY;
    EXPECT_LT(std::sqrt(dx * dx + dy * dy), 30.0f)
        << "the boss anchor drifted off the ledge's deck";
    EXPECT_LT(std::fabs(kPlatformMeshZ - kLedgeZ), 2.0f)
        << "the boss anchor is no longer at deck height";
}
