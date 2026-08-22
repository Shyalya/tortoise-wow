/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "Ai/Dungeon/DungeonClear/Data/DcDifficultyGate.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"

// Heroic-dungeon difficulty plumbing (see
// deployment-files/docs/mod-dungeon-clear_heroic-dungeons_plan.md): the gate
// type itself, the roster registry's difficulty-aware Apply, the route
// registry's normal-difficulty fallback, and the Conditional-event difficulty
// overload.

// --- DcGateMatches truth table --------------------------------------------

TEST(DcDifficultyGateTest, GateMatchesTruthTable)
{
    EXPECT_TRUE(DcGateMatches(DcDifficultyGate::Any, DUNGEON_DIFFICULTY_NORMAL));
    EXPECT_TRUE(DcGateMatches(DcDifficultyGate::Any, DUNGEON_DIFFICULTY_HEROIC));

    EXPECT_TRUE(DcGateMatches(DcDifficultyGate::NormalOnly, DUNGEON_DIFFICULTY_NORMAL));
    EXPECT_FALSE(DcGateMatches(DcDifficultyGate::NormalOnly, DUNGEON_DIFFICULTY_HEROIC));

    EXPECT_FALSE(DcGateMatches(DcDifficultyGate::HeroicOnly, DUNGEON_DIFFICULTY_NORMAL));
    EXPECT_TRUE(DcGateMatches(DcDifficultyGate::HeroicOnly, DUNGEON_DIFFICULTY_HEROIC));
}

// --- BossRosterRegistry::Apply is difficulty-aware ------------------------

namespace
{
    DungeonBossInfo GateBoss(uint32 entry, uint32 idx, char const* name, uint32 mapId)
    {
        DungeonBossInfo b;
        b.entry = entry;
        b.encounterIndex = idx;
        b.name = name;
        b.mapId = mapId;
        return b;
    }
}

TEST(DcDifficultyGateTest, AnyGatedPatchAppliesOnBothDifficulties)
{
    // Every currently registered patch is gate Any, so a patched map must
    // produce the SAME roster on normal and heroic (Hellfire Ramparts: the
    // Vazruden add lands on both). This pins the default-gate behaviour so
    // heroic runs don't silently lose the hand-authored corrections.
    std::vector<DungeonBossInfo> base = {
        GateBoss(17306, 0, "Watchkeeper Gargolmar", 543),
        GateBoss(17308, 1, "Omor the Unscarred", 543),
    };

    std::vector<DungeonBossInfo> normal =
        BossRosterRegistry::Apply(543, DUNGEON_DIFFICULTY_NORMAL, base);
    std::vector<DungeonBossInfo> heroic =
        BossRosterRegistry::Apply(543, DUNGEON_DIFFICULTY_HEROIC, base);

    ASSERT_EQ(normal.size(), heroic.size());
    for (size_t i = 0; i < normal.size(); ++i)
    {
        EXPECT_EQ(normal[i].entry, heroic[i].entry);
        EXPECT_EQ(normal[i].encounterIndex, heroic[i].encounterIndex);
    }
}

TEST(DcDifficultyGateTest, GatedPatchesRideAnAnyBasePatch)
{
    // A difficulty-gated patch that REMOVES or REORDERS entries is a CORRECTION
    // layered on a map's Any base patch (Apply chains them in registration
    // order), so such a patch with no preceding Any patch for the same map is
    // almost certainly an authoring slip — its reorder targets (the base patch's
    // objectives) would not exist.
    //
    // A pure ADD-only gated patch is exempt: it stands alone (e.g. Sethekk
    // Halls' heroic-only Anzu anchor, whose ordering key sorts against the
    // auto-derived DBC roster, not against a base patch). Map 556 needs no
    // normal-mode correction, so it carries only the HeroicOnly add.
    for (std::size_t i = 0; i < BossRosterRegistry::AllPatches().size(); ++i)
    {
        BossRosterPatch const& patch = BossRosterRegistry::AllPatches()[i];
        if (patch.gate == DcDifficultyGate::Any)
            continue;
        if (patch.remove.empty() && patch.reorder.empty())
            continue;  // add-only gated patch — no base-patch dependency
        bool hasBase = false;
        for (std::size_t j = 0; j < i; ++j)
        {
            BossRosterPatch const& earlier = BossRosterRegistry::AllPatches()[j];
            if (earlier.mapId == patch.mapId && earlier.gate == DcDifficultyGate::Any)
                hasBase = true;
        }
        EXPECT_TRUE(hasBase) << "map " << patch.mapId
                             << " has a gated patch with no preceding Any base patch";
    }
}

TEST(DcDifficultyGateTest, ShatteredHallsHeroicReordersHallwaySweep)
{
    // Heroic 540 DBC inserts Porung at bit 1 (Nethekurse 0, Porung 1,
    // O'mrogg 2, Kargath 3). The Any patch's objectives borrow NORMAL keys;
    // the HeroicOnly patch must re-key the stealth-hallway sweep (OBJ(3),
    // normal key 2) to 3 so it still precedes KARGATH, not O'mrogg.
    std::vector<DungeonBossInfo> heroicBase = {
        GateBoss(16807, 0, "Grand Warlock Nethekurse", 540),
        GateBoss(20923, 1, "Blood Guard Porung", 540),
        GateBoss(16809, 2, "Warbringer O'mrogg", 540),
        GateBoss(16808, 3, "Warchief Kargath Bladefist", 540),
    };
    std::vector<DungeonBossInfo> const out =
        BossRosterRegistry::Apply(540, DUNGEON_DIFFICULTY_HEROIC, heroicBase);

    auto indexOf = [&](uint32 entry) -> std::size_t
    {
        for (std::size_t i = 0; i < out.size(); ++i)
            if (out[i].entry == entry)
                return i;
        ADD_FAILURE() << "entry " << entry << " missing from heroic roster";
        return 0;
    };

    uint32 const objHallway = BossRosterRegistry::ObjectiveEntry(3);
    // Hallway sweep lands between O'mrogg and Kargath...
    EXPECT_LT(indexOf(16809), indexOf(objHallway));
    EXPECT_LT(indexOf(objHallway), indexOf(16808));
    // ...and the gauntlet objective still precedes Porung.
    EXPECT_LT(indexOf(BossRosterRegistry::ObjectiveEntry(2)), indexOf(20923));

    // The lone O'mrogg-approach sentinel (OBJ(4), normal key 1) must be re-keyed
    // to 2 on heroic so it lands AFTER Porung. Left on key 1 it sorts ahead of
    // him, which would send the tank ~260yd south through the training yard with
    // Porung still alive — and only his death cancels the scout's 45s zealot
    // summons, so the party would walk that whole leg with waves chasing and
    // then walk back north for him.
    uint32 const objSentinel = BossRosterRegistry::ObjectiveEntry(4);
    EXPECT_LT(indexOf(20923), indexOf(objSentinel));
    EXPECT_LT(indexOf(objSentinel), indexOf(16809));

    // On NORMAL (no Porung row, hallway keeps key 2) the sweep still precedes
    // Kargath — the heroic patch must not leak into normal ordering.
    std::vector<DungeonBossInfo> normalBase = {
        GateBoss(16807, 0, "Grand Warlock Nethekurse", 540),
        GateBoss(16809, 1, "Warbringer O'mrogg", 540),
        GateBoss(16808, 2, "Warchief Kargath Bladefist", 540),
    };
    std::vector<DungeonBossInfo> const normalOut =
        BossRosterRegistry::Apply(540, DUNGEON_DIFFICULTY_NORMAL, normalBase);
    auto normalIndexOf = [&](uint32 entry) -> std::size_t
    {
        for (std::size_t i = 0; i < normalOut.size(); ++i)
            if (normalOut[i].entry == entry)
                return i;
        ADD_FAILURE() << "entry " << entry << " missing from normal roster";
        return 0;
    };
    EXPECT_LT(normalIndexOf(16809), normalIndexOf(objHallway));
    EXPECT_LT(normalIndexOf(objHallway), normalIndexOf(16808));

    // And on normal the sentinel keeps its own key-1 slot: gauntlet -> sentinel
    // -> O'mrogg, with no Porung row in between. The heroic re-key must not have
    // moved it here.
    EXPECT_LT(normalIndexOf(BossRosterRegistry::ObjectiveEntry(2)),
              normalIndexOf(objSentinel));
    EXPECT_LT(normalIndexOf(objSentinel), normalIndexOf(16809));
}

// --- Route registry falls back to normal ----------------------------------

TEST(DcDifficultyGateTest, RouteRegistryHeroicFallsBackToNormalRow)
{
    // Synthetic map far outside real content so the static store is not
    // polluted for other suites' lookups.
    constexpr uint32 kMap = 999901;
    constexpr uint32 kBoss = 42;

    WaypointHint normalHint;
    normalHint.x = 1.0f;
    DungeonClearRouteRegistry::Register(kMap, DUNGEON_DIFFICULTY_NORMAL, kBoss, {normalHint});

    // Heroic lookup with no heroic row: the normal route is returned.
    std::vector<WaypointHint> const* viaFallback =
        DungeonClearRouteRegistry::Get(kMap, DUNGEON_DIFFICULTY_HEROIC, kBoss);
    ASSERT_NE(viaFallback, nullptr);
    EXPECT_FLOAT_EQ((*viaFallback)[0].x, 1.0f);

    // A heroic-specific row, once registered, wins over the fallback.
    WaypointHint heroicHint;
    heroicHint.x = 2.0f;
    DungeonClearRouteRegistry::Register(kMap, DUNGEON_DIFFICULTY_HEROIC, kBoss, {heroicHint});
    std::vector<WaypointHint> const* heroicRow =
        DungeonClearRouteRegistry::Get(kMap, DUNGEON_DIFFICULTY_HEROIC, kBoss);
    ASSERT_NE(heroicRow, nullptr);
    EXPECT_FLOAT_EQ((*heroicRow)[0].x, 2.0f);

    // A boss with no row on either difficulty still misses cleanly.
    EXPECT_EQ(DungeonClearRouteRegistry::Get(kMap, DUNGEON_DIFFICULTY_HEROIC, kBoss + 1), nullptr);
}

// --- Conditional(mapId, difficulty) overload ------------------------------

TEST(DcDifficultyGateTest, ConditionalOverloadFiltersExactlyByGate)
{
    // For every map with conditional events, the difficulty overload must keep
    // exactly the events whose gate matches — no more (a gated event leaking
    // into the wrong difficulty) and no less (an Any event dropped). Holds for
    // today's all-Any table and stays correct once gated content lands.
    for (DungeonEvent const& seed : DungeonEventRegistry::AllEvents())
    {
        std::vector<DungeonEvent const*> const all =
            DungeonEventRegistry::Conditional(seed.mapId);
        if (all.empty())
            continue;

        size_t expectNormal = 0;
        size_t expectHeroic = 0;
        for (DungeonEvent const* ev : all)
        {
            if (DcGateMatches(ev->gate, DUNGEON_DIFFICULTY_NORMAL))
                ++expectNormal;
            if (DcGateMatches(ev->gate, DUNGEON_DIFFICULTY_HEROIC))
                ++expectHeroic;
        }

        EXPECT_EQ(DungeonEventRegistry::Conditional(seed.mapId, DUNGEON_DIFFICULTY_NORMAL).size(),
                  expectNormal) << "map " << seed.mapId;
        EXPECT_EQ(DungeonEventRegistry::Conditional(seed.mapId, DUNGEON_DIFFICULTY_HEROIC).size(),
                  expectHeroic) << "map " << seed.mapId;
    }
}
