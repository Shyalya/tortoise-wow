/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h"

// Invariants of the settings table itself. The resolution CHAIN
// (override -> heroic conf -> heroic default -> conf -> default) needs a live
// sConfigMgr and a Player on a heroic map, so it is exercised at runtime rather
// than here; what these pin is the data the chain reads, which is where an
// authoring mistake would actually land.

namespace
{
    std::vector<DcSettingDef const*> HeroicRows()
    {
        std::vector<DcSettingDef const*> out;
        for (DcSettingDef const& d : kDcSettings)
            if (DcHasHeroicDefault(d))
                out.push_back(&d);
        return out;
    }
}

TEST(DcSettingsRegistryTest, SentinelMeansNoHeroicLayer)
{
    DcSettingDef plain{"X", DcType::Float, 1, 0, 10, true};
    EXPECT_FALSE(DcHasHeroicDefault(plain));
    EXPECT_TRUE(std::isnan(plain.heroicVal));

    DcSettingDef heroic{"Y", DcType::Float, 1, 0, 10, true, 4};
    EXPECT_TRUE(DcHasHeroicDefault(heroic));
    EXPECT_EQ(heroic.heroicVal, 4);
}

TEST(DcSettingsRegistryTest, HeroicDefaultsSitInsideTheRowsOwnRange)
{
    // A heroic default outside [minVal, maxVal] would be a value the addon could
    // never reproduce as an override, and the two layers would disagree about
    // what is legal for the same key.
    for (DcSettingDef const* d : HeroicRows())
    {
        EXPECT_GE(d->heroicVal, d->minVal) << d->key;
        EXPECT_LE(d->heroicVal, d->maxVal) << d->key;
    }
}

TEST(DcSettingsRegistryTest, HeroicDefaultsAreWholeNumbersForDiscreteTypes)
{
    // Bool/UInt/Int rows round on read; an authored 2.5 would silently become 2
    // (or 3) and the table would not say what the server actually uses.
    for (DcSettingDef const* d : HeroicRows())
    {
        if (d->type == DcType::Float)
            continue;
        EXPECT_EQ(d->heroicVal, std::round(d->heroicVal)) << d->key;
        if (d->type == DcType::Bool)
            EXPECT_TRUE(d->heroicVal == 0 || d->heroicVal == 1) << d->key;
    }
}

TEST(DcSettingsRegistryTest, HeroicDefaultsActuallyDifferFromNormal)
{
    // A heroic value equal to the normal default is dead weight that reads, to
    // anyone scanning the table, as a deliberate difficulty split that isn't one.
    for (DcSettingDef const* d : HeroicRows())
        EXPECT_NE(d->heroicVal, d->defVal) << d->key;
}

TEST(DcSettingsRegistryTest, ServerOnlyRowsCarryNoHeroicDefault)
{
    // Server-only rows govern the harness, the path workers and the spectator
    // camera — none of which is a property of the dungeon's difficulty. Keeping
    // them out means the difficulty lookup is never reached from the worker
    // threads that read them (see DcSettings::IsHeroicRun).
    for (DcSettingDef const* d : HeroicRows())
        EXPECT_TRUE(d->playerFacing) << d->key;
}

TEST(DcSettingsRegistryTest, HeroicProfileIsExactlyThePullSafetySet)
{
    // The heroic layer is deliberately narrow: the pull safety profile, nothing
    // else. Pinning the membership means a heroic default cannot be added to an
    // unrelated key without this failing and making someone justify it — and it
    // doubles as the readable list of what heroic actually changes. Keep in step
    // with the HEROIC DEFAULTS block in mod_dungeon_clear.conf.dist.
    //
    // Smart Rest was in this set and was removed: forcing it on in heroics made
    // runs crawl (see the SmartRest block in DcSettingsRegistry.h). It is opt-in
    // on both difficulties now.
    std::vector<std::string> const expected{
        "PullSetback",
        "PullCampSafeRadius",
        "PullMaxDrag",
        "PullRangedMaxDrag",
        "PullPlayerReleaseDelay",
        "PullThreatLeadPanicHp",
        "PullSafetyHpPct",
        "PullSafetyGrace",
        "PullPetReleaseDelay",
        "PullCommitRangeFloor",
        "PullDynamicMaxLeeroyMobs",
        "PullCombatSpread",
        "PullDynamicPartyLag",
        "PullPatrolWaitSec",
        "PullEnRouteAvoid",
        "AdvanceWindowYards",
        "TrashWidthCap",
    };

    std::vector<std::string> actual;
    for (DcSettingDef const* d : HeroicRows())
        actual.emplace_back(d->key);

    // Order follows the table, so compare as sets.
    std::vector<std::string> sortedExpected = expected;
    std::vector<std::string> sortedActual = actual;
    std::sort(sortedExpected.begin(), sortedExpected.end());
    std::sort(sortedActual.begin(), sortedActual.end());
    EXPECT_EQ(sortedActual, sortedExpected);
}

TEST(DcSettingsRegistryTest, ForceAdvancedIsOffOnBothDifficulties)
{
    // The A/B lever must never ship on: it costs the full pull FSM on every
    // single-mob pack. An operator opts in per difficulty with a conf line.
    DcSettingDef const* d = FindDcSetting("PullForceAdvanced");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->defVal, 0);
    EXPECT_FALSE(DcHasHeroicDefault(*d));
}

TEST(DcSettingsRegistryTest, KeysAreUniqueAndLookupFindsThem)
{
    for (DcSettingDef const& d : kDcSettings)
    {
        DcSettingDef const* found = FindDcSetting(d.key);
        ASSERT_NE(found, nullptr) << d.key;
        // Unique keys: the linear lookup returns the FIRST match, so a duplicate
        // would silently shadow whatever came after it.
        EXPECT_EQ(found, &d) << d.key;
    }
    EXPECT_EQ(FindDcSetting("NotARealSetting"), nullptr);
}

TEST(DcSettingsRegistryTest, TrashBandClampedToHeroicCap)
{
    // C.2: the heroic band cap. With the unified reach a common heroic elite's
    // band lands ~32-36yd, which the normal 30 cap silently clips — discarding
    // exactly the reach the unification added. Normal keeps 30.
    DcSettingDef const* d = FindDcSetting("TrashWidthCap");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->defVal, 30);
    ASSERT_TRUE(DcHasHeroicDefault(*d));
    EXPECT_EQ(d->heroicVal, 42);
}
