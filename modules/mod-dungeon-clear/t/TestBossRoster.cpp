/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"

namespace
{
    DungeonBossInfo Boss(uint32 entry, uint32 idx, char const* name, uint32 mapId = 0)
    {
        DungeonBossInfo b;
        b.entry = entry;
        b.encounterIndex = idx;
        b.name = name;
        b.mapId = mapId;
        return b;
    }

    DungeonBossInfo const* Find(std::vector<DungeonBossInfo> const& v, uint32 entry)
    {
        for (DungeonBossInfo const& b : v)
            if (b.entry == entry)
                return &b;
        return nullptr;
    }
}

// --- HasPatch -------------------------------------------------------------

TEST(BossRosterRegistryTest, HasPatchOnlyForPatchedMaps)
{
    EXPECT_TRUE(BossRosterRegistry::HasPatch(189));   // SM Cathedral
    EXPECT_TRUE(BossRosterRegistry::HasPatch(109));   // Sunken Temple
    EXPECT_TRUE(BossRosterRegistry::HasPatch(209));   // ZulFarrak
    EXPECT_TRUE(BossRosterRegistry::HasPatch(230));   // Blackrock Depths
    EXPECT_TRUE(BossRosterRegistry::HasPatch(36));    // Deadmines
    EXPECT_TRUE(BossRosterRegistry::HasPatch(329));   // Stratholme
    EXPECT_TRUE(BossRosterRegistry::HasPatch(289));   // Scholomance
    EXPECT_TRUE(BossRosterRegistry::HasPatch(429));   // Dire Maul East
    EXPECT_TRUE(BossRosterRegistry::HasPatch(70));    // Uldaman — altar objectives
    EXPECT_TRUE(BossRosterRegistry::HasPatch(547));   // Slave Pens — drop objective
    EXPECT_TRUE(BossRosterRegistry::HasPatch(546));   // Underbog — drop objective
    EXPECT_TRUE(BossRosterRegistry::HasPatch(576));   // The Nexus — sphere objectives
    EXPECT_FALSE(BossRosterRegistry::HasPatch(0));
    EXPECT_FALSE(BossRosterRegistry::HasPatch(34));   // Stockades — no patch
}

// Uldaman: the two altar travel objectives MUST sort between Grimlok (the boss
// before them) and Archaedas (the final boss, reordered above them), in the
// order keeper-altar -> Archaedas-altar -> Archaedas. The keeper altar opens the
// temple door; the Archaedas altar wakes the stoned boss; both must precede him.
TEST(BossRosterRegistryTest, UldamanAltarObjectivesSortBeforeArchaedas)
{
    std::vector<DungeonBossInfo> base = {
        Boss(4854, 6, "Grimlok", 70),
        Boss(2748, 7, "Archaedas", 70),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(70, DUNGEON_DIFFICULTY_NORMAL, base);

    int grimlokIdx = -1, keeperIdx = -1, archAltarIdx = -1, archaedasIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 4854)
            grimlokIdx = i;
        else if (out[i].entry == 2748)
            archaedasIdx = i;
        else if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 2)
            keeperIdx = i;
        else if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 3)
            archAltarIdx = i;
    }

    ASSERT_NE(grimlokIdx, -1);
    ASSERT_NE(keeperIdx, -1);
    ASSERT_NE(archAltarIdx, -1);
    ASSERT_NE(archaedasIdx, -1);
    EXPECT_LT(grimlokIdx, keeperIdx);
    EXPECT_LT(keeperIdx, archAltarIdx);
    EXPECT_LT(archAltarIdx, archaedasIdx);

    // Archaedas keeps his real DBC kill-bit (7) — only his clear ORDER moved.
    EXPECT_EQ(out[archaedasIdx].encounterIndex, 7u);
}

// ZulFarrak: the Temple Summit event objective (orderOverride 4) MUST sort (and
// thus be reached) before Chief Ukorz (orderOverride 5) so the door-opening
// event runs first.
TEST(BossRosterRegistryTest, ZfSummitObjectiveSortsBeforeUkorz)
{
    std::vector<DungeonBossInfo> base = {
        Boss(7795, 0, "Hydromancer Velratha", 209),
        Boss(7267, 7, "Chief Ukorz Sandscalp", 209),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(209, DUNGEON_DIFFICULTY_NORMAL, base);

    int objIdx = -1, ukorzIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        // The summit objective shares bit 7 with Ukorz; the Gahz'rilla objective
        // sits at bit 8, so key off the index to pick the summit one.
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].encounterIndex == 7u)
            objIdx = i;
        if (out[i].entry == 7267)
            ukorzIdx = i;
    }
    ASSERT_GE(objIdx, 0) << "summit objective missing";
    ASSERT_GE(ukorzIdx, 0);
    EXPECT_LT(objIdx, ukorzIdx) << "objective must precede Ukorz despite equal bit";
    EXPECT_EQ(out[objIdx].encounterIndex, 7u);
}

// ZulFarrak: the optional Gahz'rilla gong objective (orderOverride 7) MUST sort
// AFTER Chief Ukorz (orderOverride 5) AND after Hydromancer Velratha (6) so the
// strictly-ordinal picker only routes the tank to the sacred pool once every
// real boss is dead — the "very last" stop.
TEST(BossRosterRegistryTest, ZfGahzrillaObjectiveSortsAfterUkorz)
{
    std::vector<DungeonBossInfo> base = {
        Boss(7795, 0, "Hydromancer Velratha", 209),
        Boss(7267, 7, "Chief Ukorz Sandscalp", 209),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(209, DUNGEON_DIFFICULTY_NORMAL, base);

    int gahzIdx = -1, ukorzIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].encounterIndex == 8u)
            gahzIdx = i;
        if (out[i].entry == 7267)
            ukorzIdx = i;
    }
    ASSERT_GE(gahzIdx, 0) << "Gahz'rilla objective missing";
    ASSERT_GE(ukorzIdx, 0);
    EXPECT_GT(gahzIdx, ukorzIdx) << "Gahz'rilla must come after Ukorz (ordered last)";
    EXPECT_EQ(out[gahzIdx].eventId, 2u);
}

// ZulFarrak FULL clear order: the DBC bits (Velratha 0, Antu'sul 2, Theka 3,
// Zum'rah 4, Ukorz 7) do not match the travel path. The `reorder` patch stamps
// orderOverride 1..6 on the five real bosses (kill-bits untouched) and slots the
// two objectives in at 4 and 7, yielding:
//   Theka -> Antu'sul -> Zum'rah -> Temple Summit -> Ukorz -> Velratha -> Pool.
TEST(BossRosterRegistryTest, ZfFullClearOrder)
{
    // Auto-derived list as BossSpawnIndex emits it (DBC encounterIndex order).
    std::vector<DungeonBossInfo> base = {
        Boss(7795, 0, "Hydromancer Velratha", 209),
        Boss(8127, 2, "Antu'sul", 209),
        Boss(7272, 3, "Theka the Martyr", 209),
        Boss(7271, 4, "Witch Doctor Zum'rah", 209),
        Boss(7267, 7, "Chief Ukorz Sandscalp", 209),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(209, DUNGEON_DIFFICULTY_NORMAL, base);

    auto pos = [&](uint32 entry)
    {
        for (int i = 0; i < (int)out.size(); ++i)
            if (out[i].entry == entry)
                return i;
        return -1;
    };
    int summitPos = -1, poolPos = -1;
    for (int i = 0; i < (int)out.size(); ++i)
        if (out[i].kind == DungeonAnchorKind::Objective)
        {
            if (out[i].eventId == 1u) summitPos = i;
            if (out[i].eventId == 2u) poolPos = i;
        }
    ASSERT_GE(summitPos, 0) << "Temple Summit objective missing";
    ASSERT_GE(poolPos, 0) << "Sacred Pool objective missing";

    // Theka -> Antu'sul -> Zum'rah -> Temple Summit -> Ukorz -> Velratha -> Pool.
    EXPECT_LT(pos(7272), pos(8127));
    EXPECT_LT(pos(8127), pos(7271));
    EXPECT_LT(pos(7271), summitPos);
    EXPECT_LT(summitPos, pos(7267));
    EXPECT_LT(pos(7267), pos(7795));
    EXPECT_LT(pos(7795), poolPos);

    // Reordering must NOT disturb the real DBC kill-bits.
    EXPECT_EQ(Find(out, 7795)->encounterIndex, 0u);  // Velratha
    EXPECT_EQ(Find(out, 7267)->encounterIndex, 7u);  // Ukorz
    EXPECT_EQ(BossOrderKey(*Find(out, 7795)), 6u);
    EXPECT_EQ(BossOrderKey(*Find(out, 7267)), 5u);
}

// Hellfire Ramparts (543): the auto-derived list ends at Omor because the final
// boss's credit creature, Vazruden (17537), is a TempSummon with no DB spawn so
// BossSpawnIndex never emits it. The patch adds it explicitly at the lower
// platform with DBC kill-bit 2 (set directly — there is no base entry to inherit
// from), so the run does not stop one boss short.
// The Mechanar Gatewatchers (Gyro-Kill 19218, Iron-Hand 19710) are NOT DBC
// encounters (no instance_encounters KILL_CREATURE row), so BossSpawnIndex never
// derives them and the auto-derived base holds only the three real bosses. The
// patch must ADD them as bosses (Gyro-Kill order 1, Iron-Hand order 3, with
// Capacitus reordered to 2 between them) with instance-boss-state completion
// (doneBossStateIndex 0/1) and a no-DBC-bit encounterIndex, or the picker skips
// straight to Capacitus and the party never opens the Mo'arg doors.
TEST(BossRosterRegistryTest, MechanarAddsGatewatchersAheadOfCapacitus)
{
    // Base as BossSpawnIndex emits it — only the three DBC bosses.
    std::vector<DungeonBossInfo> base = {
        Boss(19219, 0, "Mechano-Lord Capacitus", 554),
        Boss(19221, 1, "Nethermancer Sepethrea", 554),
        Boss(19220, 2, "Pathaleon the Calculator", 554),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(554, DUNGEON_DIFFICULTY_NORMAL, base);

    DungeonBossInfo const* gyro = Find(out, 19218);
    DungeonBossInfo const* iron = Find(out, 19710);
    ASSERT_NE(gyro, nullptr) << "Gyro-Kill must be injected as a boss";
    ASSERT_NE(iron, nullptr) << "Iron-Hand must be injected as a boss";

    EXPECT_EQ(gyro->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(iron->kind, DungeonAnchorKind::Boss);
    // Completion is the instance boss-state slot, not a DBC bit.
    EXPECT_EQ(gyro->doneBossStateIndex, 0);
    EXPECT_EQ(iron->doneBossStateIndex, 1);
    // encounterIndex parked past bit 31 so the completedMask check never matches
    // a real boss's bit (default 0 would collide with DBC bit 0 = Capacitus).
    EXPECT_GE(gyro->encounterIndex, 32u);
    EXPECT_GE(iron->encounterIndex, 32u);

    // Ordered first: Gyro-Kill(1) -> Capacitus(2) -> Iron-Hand(3) -> ...
    // Capacitus sits between the Gatewatchers so the tank approaches his pit from
    // Gyro-Kill (NW) and the SE Driller pack falls on the walk down to Iron-Hand
    // (which is why the room-aggro pre-clear for 19219 was dropped).
    ASSERT_GE(out.size(), 3u);
    EXPECT_EQ(out[0].entry, 19218u);
    EXPECT_EQ(out[1].entry, 19219u);
    EXPECT_EQ(out[2].entry, 19710u);
}

TEST(BossRosterRegistryTest, HellfireRampartsAddsFinalBoss)
{
    // Auto-derived list as BossSpawnIndex emits it (only the two spawned bosses).
    std::vector<DungeonBossInfo> base = {
        Boss(17306, 0, "Watchkeeper Gargolmar", 543),
        Boss(17308, 1, "Omor the Unscarred", 543),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(543, DUNGEON_DIFFICULTY_NORMAL, base);

    DungeonBossInfo const* vaz = Find(out, 17537);
    ASSERT_NE(vaz, nullptr) << "Vazruden must be injected";
    EXPECT_EQ(vaz->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(vaz->encounterIndex, 2u);  // DBC bit 2, the final encounter
    EXPECT_EQ(vaz->inheritCompletionFrom, 0u);  // set directly, not inherited
    EXPECT_FLOAT_EQ(vaz->x, -1378.0f);
    EXPECT_FLOAT_EQ(vaz->y, 1718.0f);

    // Sorted last, after Gargolmar (0) and Omor (1).
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].entry, 17306u);
    EXPECT_EQ(out[1].entry, 17308u);
    EXPECT_EQ(out[2].entry, 17537u);
}

// Stratholme full clear-path order (issue #5). The DBC order is
//   Unforgiven 0, Hearthsinger 1, Timmy 2, Cannon 3, Malor 4, Galford 5,
//   Baroness 7, Nerub'enkan 8, Maleki 9, Barthilas 10, Baron 12
// (Balnazzar bit 6 is NOT auto-derived: its credit creature 10813 has no
// creature.sql spawn, so BossSpawnIndex drops it and the patch fills the gap
// with a Dathrohan objective). Two structural problems: the DBC runs the
// ziggurats before Barthilas though the path runs Barthilas first, and it lists
// The Unforgiven / Hearthsinger Forresten first, forcing a full circle before
// the live side. The patch stamps a contiguous 1..13 order key so the clear runs
//   Timmy(1) -> Malor(2) -> Cannon(3) -> Galford(4) -> Dathrohan/Balnazzar(5) ->
//   Unforgiven(6) -> Hearthsinger(7) -> Barthilas(8) -> Baroness(9) ->
//   Nerub'enkan(10) -> Maleki(11) -> Slaughterhouse(12) -> Baron(13)
// while every boss keeps its real DBC kill-bit (encounterIndex) untouched.
TEST(BossRosterRegistryTest, StratholmeFullClearPathOrder)
{
    std::vector<DungeonBossInfo> base = {
        Boss(10516, 0, "The Unforgiven", 329),
        Boss(10558, 1, "Hearthsinger Forresten", 329),
        Boss(10808, 2, "Timmy the Cruel", 329),
        Boss(10997, 3, "Cannon Master Willey", 329),
        Boss(11032, 4, "Malor the Zealous", 329),
        Boss(10811, 5, "Archivist Galford", 329),
        Boss(10436, 7, "Baroness Anastari", 329),
        Boss(10437, 8, "Nerub'enkan", 329),
        Boss(10438, 9, "Maleki the Pallid", 329),
        Boss(10435, 10, "Magistrate Barthilas", 329),
        Boss(10440, 12, "Baron Rivendare", 329),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(329, DUNGEON_DIFFICULTY_NORMAL, base);

    int timmyIdx = -1, malorIdx = -1, cannonIdx = -1, unforgivenIdx = -1,
        hearthIdx = -1, galfordIdx = -1, dathrohanIdx = -1, barthIdx = -1,
        baronessIdx = -1, malekiIdx = -1, slaughterIdx = -1, baronIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 10808) timmyIdx = i;
        if (out[i].entry == 11032) malorIdx = i;
        if (out[i].entry == 10997) cannonIdx = i;
        if (out[i].entry == 10516) unforgivenIdx = i;
        if (out[i].entry == 10558) hearthIdx = i;
        if (out[i].entry == 10811) galfordIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 5u) dathrohanIdx = i;
        if (out[i].entry == 10435) barthIdx = i;
        if (out[i].entry == 10436) baronessIdx = i;
        if (out[i].entry == 10438) malekiIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 4u) slaughterIdx = i;
        if (out[i].entry == 10440) baronIdx = i;
    }
    ASSERT_GE(timmyIdx, 0);
    ASSERT_GE(malorIdx, 0);
    ASSERT_GE(cannonIdx, 0);
    ASSERT_GE(unforgivenIdx, 0);
    ASSERT_GE(hearthIdx, 0);
    ASSERT_GE(galfordIdx, 0);
    ASSERT_GE(dathrohanIdx, 0) << "Dathrohan (Balnazzar) objective missing";
    ASSERT_GE(barthIdx, 0);
    ASSERT_GE(baronessIdx, 0);
    ASSERT_GE(slaughterIdx, 0) << "slaughterhouse objective missing";
    ASSERT_GE(baronIdx, 0);

    // Timmy leads: he comes before The Unforgiven and Hearthsinger (issue #5).
    EXPECT_LT(timmyIdx, unforgivenIdx) << "Timmy must precede The Unforgiven";
    EXPECT_LT(timmyIdx, hearthIdx) << "Timmy must precede Hearthsinger";

    // Malor the Zealous clears before Cannon Master Willey (issue #5 follow-up).
    EXPECT_LT(timmyIdx, malorIdx) << "Timmy still leads the live side";
    EXPECT_LT(malorIdx, cannonIdx) << "Malor must precede the Cannon Master";
    EXPECT_LT(cannonIdx, galfordIdx) << "Cannon Master before Galford";

    // Live side (Timmy -> ... -> Galford -> Dathrohan/Balnazzar) before the two
    // relocated bosses, which in turn precede the dead side.
    EXPECT_LT(galfordIdx, dathrohanIdx) << "Dathrohan follows Galford on the live side";
    EXPECT_LT(dathrohanIdx, unforgivenIdx) << "live side before The Unforgiven";
    EXPECT_LT(unforgivenIdx, hearthIdx) << "Unforgiven before Hearthsinger";
    EXPECT_LT(hearthIdx, barthIdx) << "relocated bosses before the dead side";
    EXPECT_LT(barthIdx, baronessIdx) << "Barthilas must precede the ziggurats";
    EXPECT_LT(malekiIdx, slaughterIdx) << "ziggurats before the slaughterhouse";
    EXPECT_LT(slaughterIdx, baronIdx) << "slaughterhouse before Baron";

    // The contiguous 1..13 order keys.
    EXPECT_EQ(BossOrderKey(out[timmyIdx]), 1u);
    EXPECT_EQ(BossOrderKey(out[malorIdx]), 2u);
    EXPECT_EQ(BossOrderKey(out[cannonIdx]), 3u);
    EXPECT_EQ(BossOrderKey(out[galfordIdx]), 4u);
    EXPECT_EQ(out[dathrohanIdx].kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(BossOrderKey(out[dathrohanIdx]), 5u);
    EXPECT_EQ(BossOrderKey(out[unforgivenIdx]), 6u);
    EXPECT_EQ(BossOrderKey(out[hearthIdx]), 7u);
    EXPECT_EQ(BossOrderKey(out[slaughterIdx]), 12u);
    EXPECT_EQ(BossOrderKey(out[baronIdx]), 13u);

    // Reordering must NOT disturb real kill-bits: completion still keys on the DBC
    // encounterIndex, only the order key moves.
    EXPECT_EQ(out[barthIdx].encounterIndex, 10u);
    EXPECT_EQ(out[barthIdx].orderOverride, 8);
    EXPECT_EQ(BossOrderKey(out[barthIdx]), 8u);
    EXPECT_EQ(out[unforgivenIdx].encounterIndex, 0u) << "Unforgiven keeps kill-bit 0";
    EXPECT_EQ(out[hearthIdx].encounterIndex, 1u) << "Hearthsinger keeps kill-bit 1";
    EXPECT_EQ(out[baronIdx].encounterIndex, 12u) << "Baron keeps kill-bit 12";
}

// Blackrock Depths: the Ring of Law objective (its own DungeonEncounter bit 3,
// credited to the spawn-less Grimstone) is injected between Houndmaster Grebmar
// (bit 2) and Pyromancer Loregrain (bit 4), carrying the arena event.
TEST(BossRosterRegistryTest, RingOfLawObjectiveSortsBetweenGrebmarAndLoregrain)
{
    std::vector<DungeonBossInfo> base = {
        Boss(9319, 2, "Houndmaster Grebmar", 230),
        Boss(9024, 4, "Pyromancer Loregrain", 230),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(230, DUNGEON_DIFFICULTY_NORMAL, base);

    int grebmarIdx = -1, ringIdx = -1, loregrainIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 9319)
            grebmarIdx = i;
        // Key on the event id, not on "is an objective": map 230 carries a
        // second objective (the Shadowforge Lock, event 2) further down the list.
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 1u)
            ringIdx = i;
        if (out[i].entry == 9024)
            loregrainIdx = i;
    }
    ASSERT_GE(ringIdx, 0) << "Ring of Law objective missing";
    ASSERT_GE(grebmarIdx, 0);
    ASSERT_GE(loregrainIdx, 0);
    EXPECT_LT(grebmarIdx, ringIdx) << "Ring of Law must follow Grebmar";
    EXPECT_LT(ringIdx, loregrainIdx) << "Ring of Law must precede Loregrain";
    EXPECT_EQ(out[ringIdx].encounterIndex, 3u);
    EXPECT_EQ(out[ringIdx].eventId, 1u);
}

// Blackrock Depths: the Shadowforge Lock objective SHARES General Angerforge's
// bit (9) — it has no encounter of its own — so the objective-before-boss
// tie-break is what puts the lever after Bael'Gar (bit 8) and before Angerforge.
// Sharing a live boss's bit is only safe because NextDungeonBossValue consults
// the completion mask for Boss anchors alone; assert the ordering here so a
// change to that tie-break can't silently strand the lever behind Angerforge.
TEST(BossRosterRegistryTest, ShadowforgeLockSortsBetweenBaelGarAndAngerforge)
{
    std::vector<DungeonBossInfo> base = {
        Boss(9016, 8, "Bael'Gar", 230),
        Boss(9033, 9, "General Angerforge", 230),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(230, DUNGEON_DIFFICULTY_NORMAL, base);

    int baelIdx = -1, lockIdx = -1, angerIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 9016)
            baelIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 2u)
            lockIdx = i;
        if (out[i].entry == 9033)
            angerIdx = i;
    }
    ASSERT_GE(lockIdx, 0) << "Shadowforge Lock objective missing";
    ASSERT_GE(baelIdx, 0);
    ASSERT_GE(angerIdx, 0);
    EXPECT_LT(baelIdx, lockIdx) << "the lever must be pulled after Bael'Gar";
    EXPECT_LT(lockIdx, angerIdx) << "the lever must be pulled before Angerforge";
    EXPECT_EQ(out[lockIdx].encounterIndex, 9u);
    EXPECT_EQ(out[lockIdx].kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(out[angerIdx].encounterIndex, 9u) << "Angerforge keeps kill-bit 9";
}

// Deadmines: the Defias Cannon objective shares Mr. Smite's bit (3); the
// objective-before-boss tie-break must order it after Gilnid (bit 2) and before
// Mr. Smite, so the tank opens the Iron Clad Door before heading to the ship.
TEST(BossRosterRegistryTest, IronCladDoorSortsBetweenGilnidAndMrSmite)
{
    std::vector<DungeonBossInfo> base = {
        Boss(1763, 2, "Gilnid", 36),
        Boss(646, 3, "Mr. Smite", 36),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(36, DUNGEON_DIFFICULTY_NORMAL, base);

    int gilnidIdx = -1, doorIdx = -1, smiteIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 1763)
            gilnidIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective)
            doorIdx = i;
        if (out[i].entry == 646)
            smiteIdx = i;
    }
    ASSERT_GE(doorIdx, 0) << "Iron Clad Door objective missing";
    ASSERT_GE(gilnidIdx, 0);
    ASSERT_GE(smiteIdx, 0);
    EXPECT_LT(gilnidIdx, doorIdx) << "cannon must follow Gilnid";
    EXPECT_LT(doorIdx, smiteIdx) << "cannon must precede Mr. Smite";
    EXPECT_EQ(out[doorIdx].encounterIndex, 3u);
    EXPECT_EQ(out[doorIdx].eventId, 1u);
}

// The Slave Pens (547): the post-Mennu drop-down objective shares Rokmar's bit
// (1); the objective-before-boss tie-break must order it after Mennu (bit 0) and
// before Rokmar, so the party is teleported across the navmesh break before
// boss-nav routes to Rokmar. (No boss surgery — all three bosses auto-derive.)
TEST(BossRosterRegistryTest, SlavePensDropSortsBetweenMennuAndRokmar)
{
    std::vector<DungeonBossInfo> base = {
        Boss(17941, 0, "Mennu the Betrayer", 547),
        Boss(17991, 1, "Rokmar the Crackler", 547),
        Boss(17942, 2, "Quagmirran", 547),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(547, DUNGEON_DIFFICULTY_NORMAL, base);

    int mennuIdx = -1, dropIdx = -1, rokmarIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 17941)
            mennuIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective)
            dropIdx = i;
        if (out[i].entry == 17991)
            rokmarIdx = i;
    }
    ASSERT_GE(dropIdx, 0) << "drop-down objective missing";
    ASSERT_GE(mennuIdx, 0);
    ASSERT_GE(rokmarIdx, 0);
    EXPECT_LT(mennuIdx, dropIdx) << "drop must follow Mennu";
    EXPECT_LT(dropIdx, rokmarIdx) << "drop must precede Rokmar";
    EXPECT_EQ(out[dropIdx].encounterIndex, 1u);
    EXPECT_EQ(out[dropIdx].eventId, 1u);
    // The three real bosses survive untouched (no remove/re-add).
    EXPECT_NE(Find(out, 17942), nullptr) << "Quagmirran must remain";
}

// The Underbog (546): the post-Ghaz'an two-hop drop objective shares Swamplord's
// bit (2); the objective-before-boss tie-break must order it after Ghaz'an (bit 1)
// and before Swamplord, so the party is teleported down the navmesh break before
// boss-nav routes to Swamplord. (No boss surgery — all four bosses auto-derive.)
TEST(BossRosterRegistryTest, UnderbogDropSortsBetweenGhazanAndSwamplord)
{
    std::vector<DungeonBossInfo> base = {
        Boss(17770, 0, "Hungarfen", 546),
        Boss(18105, 1, "Ghaz'an", 546),
        Boss(17826, 2, "Swamplord Musel'ek", 546),
        Boss(17882, 3, "The Black Stalker", 546),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(546, DUNGEON_DIFFICULTY_NORMAL, base);

    int ghazanIdx = -1, dropIdx = -1, swamplordIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 18105)
            ghazanIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective)
            dropIdx = i;
        if (out[i].entry == 17826)
            swamplordIdx = i;
    }
    ASSERT_GE(dropIdx, 0) << "drop-down objective missing";
    ASSERT_GE(ghazanIdx, 0);
    ASSERT_GE(swamplordIdx, 0);
    EXPECT_LT(ghazanIdx, dropIdx) << "drop must follow Ghaz'an";
    EXPECT_LT(dropIdx, swamplordIdx) << "drop must precede Swamplord";
    EXPECT_EQ(out[dropIdx].encounterIndex, 2u);
    EXPECT_EQ(out[dropIdx].eventId, 1u);
    // The four real bosses survive untouched (no remove/re-add).
    EXPECT_NE(Find(out, 17882), nullptr) << "The Black Stalker must remain";
}

// Dire Maul East: the Ironbark / Conservatory Door objective (orderOverride 40,
// eventId 1) must sort after the three southern bosses (reordered 10/20/30) and
// before Alzzin the Wildshaper (reordered 50), so the tank gossips Ironbark to
// open the door before heading to Alzzin's grove. The reorder leaves the real
// DBC kill-bits (the base encounterIndex) untouched.
TEST(BossRosterRegistryTest, DireMaulEastIronbarkSortsBeforeAlzzin)
{
    std::vector<DungeonBossInfo> base = {
        Boss(11490, 0, "Zevrim Thornhoof", 429),
        Boss(13280, 1, "Hydrospawn", 429),
        Boss(14327, 2, "Lethtendris", 429),
        Boss(11492, 3, "Alzzin the Wildshaper", 429),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(429, DUNGEON_DIFFICULTY_NORMAL, base);

    // NOTE: Dire Maul shares ONE map-429 patch across wings, so Apply() also
    // appends the West pylon objectives (eventId 4-8). Identify Ironbark by his
    // eventId (1), not "any objective", which would now match a West pylon.
    int lethIdx = -1, ironbarkIdx = -1, alzzinIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 14327)
            lethIdx = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 1)
            ironbarkIdx = i;
        if (out[i].entry == 11492)
            alzzinIdx = i;
    }
    ASSERT_GE(ironbarkIdx, 0) << "Ironbark / Conservatory Door objective missing";
    ASSERT_GE(lethIdx, 0);
    ASSERT_GE(alzzinIdx, 0);
    EXPECT_LT(lethIdx, ironbarkIdx) << "Ironbark must follow the southern bosses";
    EXPECT_LT(ironbarkIdx, alzzinIdx) << "Ironbark must precede Alzzin";
    EXPECT_EQ(out[ironbarkIdx].eventId, 1u);

    // Reorder keys: 10/20/30/objective 40/50; real kill-bits untouched.
    EXPECT_EQ(BossOrderKey(*Find(out, 14327)), 30u);
    EXPECT_EQ(BossOrderKey(out[ironbarkIdx]), 40u);
    EXPECT_EQ(BossOrderKey(*Find(out, 11492)), 50u);
    EXPECT_EQ(Find(out, 11492)->encounterIndex, 3u) << "Alzzin DBC kill-bit intact";
}

// Dire Maul West: the kill order is fixed (Immol'thar before the friendly-until-
// his-death Prince), and Immol'thar's five Crystal Generator pylons are added as
// travel objectives — three before Tendris, two before Immol'thar. Real DBC
// kill-bits stay intact; objective encounterIndex values are synthetic highs.
TEST(BossRosterRegistryTest, DireMaulWestPylonsAndOrder)
{
    // Arbitrary DBC bit order (deliberately NOT the kill order) to prove the
    // reorder is what fixes it.
    std::vector<DungeonBossInfo> base = {
        Boss(11486, 0, "Prince Tortheldrin", 429),
        Boss(11496, 1, "Immol'thar", 429),
        Boss(11487, 2, "Magister Kalendris", 429),
        Boss(11488, 3, "Illyanna Ravenoak", 429),
        Boss(11489, 4, "Tendris Warpwood", 429),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(429, DUNGEON_DIFFICULTY_NORMAL, base);

    // All five West bosses survive with their real kill-bits untouched.
    ASSERT_NE(Find(out, 11489), nullptr);
    EXPECT_EQ(Find(out, 11489)->encounterIndex, 4u) << "Tendris DBC kill-bit intact";
    EXPECT_EQ(Find(out, 11486)->encounterIndex, 0u) << "Prince DBC kill-bit intact";

    // Reorder keys put the kill order right: Tendris < Illyanna < Kalendris <
    // Immol'thar < Prince.
    EXPECT_EQ(BossOrderKey(*Find(out, 11489)), 10u);
    EXPECT_EQ(BossOrderKey(*Find(out, 11488)), 20u);
    EXPECT_EQ(BossOrderKey(*Find(out, 11487)), 30u);
    EXPECT_EQ(BossOrderKey(*Find(out, 11496)), 45u);
    EXPECT_EQ(BossOrderKey(*Find(out, 11486)), 50u);

    // Immol'thar strictly precedes the Prince in the final order.
    int immolIdx = -1, princeIdx = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 11496)
            immolIdx = i;
        if (out[i].entry == 11486)
            princeIdx = i;
    }
    ASSERT_GE(immolIdx, 0);
    ASSERT_GE(princeIdx, 0);
    EXPECT_LT(immolIdx, princeIdx) << "Immol'thar must die before the Prince";

    // The five pylon objectives are present, wired to events 4-8, with synthetic
    // (>= 32, so never confused with a real DBC bit) encounterIndex values.
    int pylonCount = 0;
    for (DungeonBossInfo const& b : out)
    {
        if (b.kind == DungeonAnchorKind::Objective && b.eventId >= 4 && b.eventId <= 8)
        {
            ++pylonCount;
            EXPECT_GE(b.encounterIndex, 32u)
                << "pylon objective kill-bit must be synthetic-high";
        }
    }
    EXPECT_EQ(pylonCount, 5) << "all five Crystal Generator objectives present";

    // Two pylons are placed after Kalendris (order 40/41), three before Tendris
    // (order 5/6/7) — i.e. the northern pair comes after Immol'thar's order in
    // the list? No: orderOverride 40/41 < Immol'thar's 45, so both northern
    // pylons precede the Immol'thar engage.
    EXPECT_LT(BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(5))), 45u);
    EXPECT_LT(BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(6))), 45u);
    EXPECT_LT(BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(2))),
              BossOrderKey(*Find(out, 11489)))
        << "southern pylon trio routed before Tendris";

    // Barrier-skirt waypoint (OBJ 7) is sequenced strictly between the two
    // northern pylons (Gen4 OBJ5 -> waypoint -> Gen5 OBJ6) so the route around
    // Immol'thar's force field is taken between them, and it carries no event.
    DungeonBossInfo const* wp = Find(out, BossRosterRegistry::ObjectiveEntry(7));
    ASSERT_NE(wp, nullptr);
    EXPECT_EQ(wp->kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(wp->eventId, 0u) << "waypoint is a pure travel anchor (no event)";
    EXPECT_LT(BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(5))),
              BossOrderKey(*wp));
    EXPECT_LT(BossOrderKey(*wp),
              BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(6))));

    // The Warpwood entrance sweep (OBJ 8 west / OBJ 9 east) exists and is ordered
    // FIRST — before every pylon and Tendris — so the entrance room is swept on
    // the way in.
    for (uint32 obj : {8u, 9u})
    {
        DungeonBossInfo const* sweep = Find(out, BossRosterRegistry::ObjectiveEntry(obj));
        ASSERT_NE(sweep, nullptr);
        EXPECT_LT(BossOrderKey(*sweep),
                  BossOrderKey(*Find(out, BossRosterRegistry::ObjectiveEntry(2))))
            << "entrance sweep OBJ" << obj << " precedes crystal generator 1";
        EXPECT_LT(BossOrderKey(*sweep), BossOrderKey(*Find(out, 11489)));
    }

    // REGRESSION GUARD: every objective that carries a ClearRadius (the entrance
    // pre-clear AND the five crystals) must have a MODERATE arriveRadius. Two ways
    // it has broken live, both guarded here:
    //   * too SMALL (< the ~10-17yd the tank parks short of an off-mesh GO dais)
    //     -> the tank never "arrives", thrashes in travel (deadlock). Need >= 25.
    //   * too LARGE -> the tank "arrives" far from the mobs, the ClearRadius gate
    //     finds nothing loaded and completes in 0ms (premature no-op). Keep it
    //     within ~ClearRadius + 20 so "arrived" means "among the mobs".
    for (uint32 obj : {8u, 9u, 2u, 3u, 4u, 5u, 6u})
    {
        DungeonBossInfo const* o = Find(out, BossRosterRegistry::ObjectiveEntry(obj));
        ASSERT_NE(o, nullptr);
        DungeonEvent const* e = DungeonEventRegistry::Find(429, o->eventId);
        ASSERT_NE(e, nullptr);
        for (auto const& step : e->steps)
            if (step.kind == EventStepKind::ClearRadius)
            {
                EXPECT_GE(o->arriveRadius, 25.0f)
                    << "OBJ" << obj << " arriveRadius too small -> deadlock risk";
                EXPECT_LE(o->arriveRadius, step.radius + 20.0f)
                    << "OBJ" << obj << " arriveRadius too large -> premature 0ms clear";
            }
    }
}

// Sunken Temple: the DBC bit order is NOT a valid clear order. The roster removes
// the three phase/puzzle-gated bosses (Weaver 5720, Dreamscythe 5721, Atal'alarion
// 8580) from their low bits and re-adds them — plus the statue/idol/Avatar pit
// wing — as event-bearing objective anchors. Weaver & Dreamscythe land on the
// required spine (after Jammal'an, before Eranikus); the whole pit wing lands at
// the route tail (after Eranikus).
TEST(BossRosterRegistryTest, SunkenTempleReordersPhaseGatedBosses)
{
    std::vector<DungeonBossInfo> base = {
        Boss(8580, 0, "Atal'alarion", 109),
        Boss(5721, 1, "Dreamscythe", 109),
        Boss(5720, 2, "Weaver", 109),
        Boss(5710, 3, "Jammal'an the Prophet", 109),
        Boss(5719, 4, "Morphaz", 109),
        Boss(5722, 6, "Hazzas", 109),
        Boss(5709, 8, "Shade of Eranikus", 109),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(109, DUNGEON_DIFFICULTY_NORMAL, base);

    // The three phase/puzzle-gated bosses are gone as combat bosses.
    EXPECT_EQ(Find(out, 8580), nullptr);
    EXPECT_EQ(Find(out, 5721), nullptr);
    EXPECT_EQ(Find(out, 5720), nullptr);

    // Kept auto bosses survive with their real bits.
    ASSERT_NE(Find(out, 5710), nullptr);
    EXPECT_EQ(Find(out, 5710)->encounterIndex, 3u);
    ASSERT_NE(Find(out, 5709), nullptr);

    auto pos = [&](uint32 entry)
    {
        for (int i = 0; i < (int)out.size(); ++i)
            if (out[i].entry == entry)
                return i;
        return -1;
    };
    // Locate the re-added objectives by their event ids: a forcefield ring anchor
    // (1), Weaver & Dreamscythe (10), statue 1 (2), and the Avatar (9).
    int ffPos = -1, wdPos = -1, statuePos = -1, avatarPos = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].kind != DungeonAnchorKind::Objective)
            continue;
        if (out[i].eventId == 1)
            ffPos = i;
        if (out[i].eventId == 10)
            wdPos = i;
        if (out[i].eventId == 2)
            statuePos = i;
        if (out[i].eventId == 9)
            avatarPos = i;
    }
    ASSERT_GE(ffPos, 0) << "forcefield ring anchor missing";
    ASSERT_GE(wdPos, 0) << "Weaver & Dreamscythe objective missing";
    ASSERT_GE(statuePos, 0) << "statue objective missing";
    ASSERT_GE(avatarPos, 0) << "Avatar objective missing";

    // Forcefield ring anchors come first, before Jammal'an (the gate to him).
    EXPECT_LT(ffPos, pos(5710));

    // Required spine: Weaver & Dreamscythe after Jammal'an, before Eranikus.
    EXPECT_LT(pos(5710), wdPos);
    EXPECT_LT(wdPos, pos(5709));

    // Optional pit wing (statues..Avatar) at the tail, after Eranikus.
    EXPECT_LT(pos(5709), statuePos);
    EXPECT_LT(statuePos, avatarPos);
}

// --- Apply: pass-through for unpatched maps -------------------------------

TEST(BossRosterRegistryTest, UnpatchedMapReturnsBaseUnchanged)
{
    std::vector<DungeonBossInfo> base = {
        Boss(1001, 0, "A"),
        Boss(1002, 1, "B"),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(34, DUNGEON_DIFFICULTY_NORMAL, base);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].entry, 1001u);
    EXPECT_EQ(out[1].entry, 1002u);
}

// --- Apply: SM Cathedral patch (the shipped worked example) ---------------

TEST(BossRosterRegistryTest, SmCathedralSwapsWhitemaneForMograine)
{
    // The auto-derived Cathedral list as BossSpawnIndex emits it: Fairbanks
    // (idx 4) and Whitemane (idx 5). Plus a non-Cathedral SM boss to prove the
    // patch only touches the entries it names.
    std::vector<DungeonBossInfo> base = {
        Boss(3975, 2, "Herod", 189),               // Armory — untouched
        Boss(4542, 4, "High Inquisitor Fairbanks", 189),
        Boss(3977, 5, "High Inquisitor Whitemane", 189),
    };

    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(189, DUNGEON_DIFFICULTY_NORMAL, base);

    // Whitemane removed.
    EXPECT_EQ(Find(out, 3977), nullptr);

    // Mograine injected as a real Boss anchor with coords.
    DungeonBossInfo const* mograine = Find(out, 3976);
    ASSERT_NE(mograine, nullptr);
    EXPECT_EQ(mograine->kind, DungeonAnchorKind::Boss);
    EXPECT_GT(mograine->x, 1100.0f);
    EXPECT_LT(mograine->x, 1200.0f);

    // He BORROWS Whitemane's encounter index (kill-bit) via inheritCompletionFrom,
    // and the authoring field is consumed (cleared) by Apply.
    EXPECT_EQ(mograine->encounterIndex, 5u);
    EXPECT_EQ(mograine->inheritCompletionFrom, 0u);

    // ORDER FIX: room-clear + Mograine run BEFORE Fairbanks. Mograine carries
    // orderOverride 3 (< Fairbanks's bit 4) so the picker reaches him first,
    // while his completion still keys on Whitemane's real bit 5.
    EXPECT_EQ(mograine->orderOverride, 3);
    EXPECT_EQ(BossOrderKey(*mograine), 3u);

    DungeonBossInfo const* fairbanks = Find(out, 4542);
    ASSERT_NE(fairbanks, nullptr);
    // Result is ordered by BossOrderKey, so Mograine precedes Fairbanks.
    auto pos = [&](uint32 entry)
    {
        for (size_t i = 0; i < out.size(); ++i)
            if (out[i].entry == entry)
                return (int)i;
        return -1;
    };
    EXPECT_LT(pos(3976), pos(4542));

    // Untouched bosses survive.
    EXPECT_NE(Find(out, 3975), nullptr);
    EXPECT_NE(Find(out, 4542), nullptr);
}

// --- Apply: Scholomance merges Marduk & Vectus into one boss --------------

TEST(BossRosterRegistryTest, ScholomanceMergesMardukAndVectus)
{
    // The auto-derived list carries Vectus (10432) and Marduk (10433) as two
    // separate encounters. Plus an untouched Scholomance boss to prove the patch
    // only touches the entries it names.
    std::vector<DungeonBossInfo> base = {
        Boss(10506, 0, "Kirtonos the Herald", 289),  // untouched
        Boss(10432, 3, "Vectus", 289),
        Boss(10433, 4, "Marduk Blackpool", 289),
    };

    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(289, DUNGEON_DIFFICULTY_NORMAL, base);

    // Both originals collapse: Marduk is gone entirely and Vectus's entry is
    // re-added as the single merged anchor.
    EXPECT_EQ(Find(out, 10433), nullptr);

    DungeonBossInfo const* merged = Find(out, 10432);
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(merged->name, "Marduk & Vectus");
    // Anchored on Vectus's spawn (the boss nearest the close room trash).
    EXPECT_GT(merged->x, 140.0f);
    EXPECT_LT(merged->x, 150.0f);
    // Inherits Vectus's own kill-bit; the authoring field is consumed.
    EXPECT_EQ(merged->encounterIndex, 3u);
    EXPECT_EQ(merged->inheritCompletionFrom, 0u);

    // Exactly one anchor remains for the pair (no duplicate 10432).
    int merged10432 = 0;
    for (DungeonBossInfo const& b : out)
        if (b.entry == 10432)
            ++merged10432;
    EXPECT_EQ(merged10432, 1);

    EXPECT_NE(Find(out, 10506), nullptr);
}

TEST(BossRosterRegistryTest, ResultStaysClearOrdered)
{
    std::vector<DungeonBossInfo> base = {
        Boss(3975, 2, "Herod", 189),
        Boss(4542, 4, "High Inquisitor Fairbanks", 189),
        Boss(3977, 5, "High Inquisitor Whitemane", 189),
    };

    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(189, DUNGEON_DIFFICULTY_NORMAL, base);

    // The result is sorted by clear ORDER key (orderOverride when set, else
    // encounterIndex), NOT by raw encounterIndex — Mograine's orderOverride 3
    // deliberately places him (kill-bit 5) ahead of Fairbanks (bit 4).
    ASSERT_FALSE(out.empty());
    for (size_t i = 1; i < out.size(); ++i)
        EXPECT_LE(BossOrderKey(out[i - 1]), BossOrderKey(out[i]))
            << "roster not clear-ordered at index " << i;
}

// inheritCompletionFrom must resolve against the PRE-removal base, so removing
// the source entry and inheriting from it in the same patch both work.
TEST(BossRosterRegistryTest, InheritResolvesBeforeRemoval)
{
    // Mograine inherits from 3977 while the same patch removes 3977 — the
    // inherited index must still be the one 3977 carried in `base`.
    std::vector<DungeonBossInfo> base = {
        Boss(3977, 9, "High Inquisitor Whitemane", 189),  // non-default idx
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(189, DUNGEON_DIFFICULTY_NORMAL, base);

    DungeonBossInfo const* mograine = Find(out, 3976);
    ASSERT_NE(mograine, nullptr);
    EXPECT_EQ(mograine->encounterIndex, 9u);
    EXPECT_EQ(Find(out, 3977), nullptr);
}

// Wailing Caverns: the Mutanus finale. The Disciple/escort OBJECTIVE (eventId 2)
// and the summoned Mutanus BOSS both key at 7 (after Verdan's bit 6); the
// objective-before-boss tie-break puts the escort FIRST, then Mutanus. Mutanus is
// a TempSummon BossSpawnIndex can't emit, so he is added with his real
// DungeonEncounter bit 7 (instance_encounters credit 3654) so his kill completes
// the dungeon.
TEST(BossRosterRegistryTest, WailingCavernsEscortObjectiveSortsBeforeMutanus)
{
    // Auto-derived static bosses (DBC encounterIndex order), bits 0-6.
    std::vector<DungeonBossInfo> base = {
        Boss(3671, 0, "Lady Anacondra", 43),
        Boss(3669, 1, "Lord Cobrahn", 43),
        Boss(3653, 2, "Kresh", 43),
        Boss(3670, 3, "Lord Pythas", 43),
        Boss(3674, 4, "Skum", 43),
        Boss(3673, 5, "Lord Serpentis", 43),
        Boss(5775, 6, "Verdan the Everliving", 43),
    };

    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(43, DUNGEON_DIFFICULTY_NORMAL, base);

    DungeonBossInfo const* escort = nullptr;
    DungeonBossInfo const* drop = nullptr;
    DungeonBossInfo const* mutanus = Find(out, 3654);
    int escortIdx = -1, dropIdx = -1, mutanusIdx = -1, verdanIdx = -1;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 2)
        {
            escort = &out[i];
            escortIdx = static_cast<int>(i);
        }
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 3)
        {
            drop = &out[i];
            dropIdx = static_cast<int>(i);
        }
        if (out[i].entry == 3654) mutanusIdx = static_cast<int>(i);
        if (out[i].entry == 5775) verdanIdx = static_cast<int>(i);
    }

    ASSERT_NE(escort, nullptr);
    ASSERT_NE(drop, nullptr);
    ASSERT_NE(mutanus, nullptr);

    // Mutanus carries his real kill-bit 7 and is a combat boss.
    EXPECT_EQ(mutanus->encounterIndex, 7u);
    EXPECT_EQ(mutanus->kind, DungeonAnchorKind::Boss);

    // The escort objective wires event 2 and arrives generously (among the
    // Disciple, who the gossip then closes the rest of the way to).
    EXPECT_EQ(escort->encounterIndex, 7u);
    EXPECT_FLOAT_EQ(escort->arriveRadius, 18.0f);

    // The return-fall objective wires event 3 and also keys at 7 (an objective's
    // index is an ordering hint; it latches by entry, so sharing Mutanus's bit is
    // harmless). It sorts BEFORE the escort by insertion order (stable_sort).
    EXPECT_EQ(drop->encounterIndex, 7u);
    EXPECT_EQ(drop->kind, DungeonAnchorKind::Objective);

    // Order: Verdan (6) -> hole-drop (7, obj) -> escort (7, obj) -> Mutanus (7, boss).
    EXPECT_LT(verdanIdx, dropIdx);
    EXPECT_LT(dropIdx, escortIdx);
    EXPECT_LT(escortIdx, mutanusIdx);
}

// Sethekk Halls (556): the forced-Anzu objective and its room pre-clear.
//
// REGRESSION GUARD for a live heroic failure: the tank walked to the Anzu summon
// statue, poked the summon into a completely untouched room, and the freshly
// summoned Anzu (SetInCombatWithZone) then chased the party the length of the
// hall and on into Ikiss's chamber, so they fought Anzu, Ikiss and the uncleared
// NE/NW packs together.
//
// The cause was the anchor's arriveRadius, not the sweep volume. DcRel::
// AtObjective (30) outranks DcRel::BlockingTrash (25), so crossing the arrive
// radius hands the tick to the objective action for good. At 55yd that happened
// a whole room-length from the statue: the ordinary corridor trash-clear was
// suppressed for the entire approach, and the ClearRadius gate was then judged
// (and the summon poked) from ~80yd out, where the volume's STRICT per-candidate
// reachability probe cannot confirm a route to anything and the gate answers
// "clear" over a room full of live trash.
//
// Both halves are pinned here:
//   * arriveRadius must stay SMALL — the walk in belongs to the corridor clear
//     and the pull pipeline, and the event may only take over once the tank is
//     actually at the statue. It must still exceed step 0's MoveTo radius so that
//     step completes on arrival and IsPersistentAnchoredEventActive latches the
//     trigger on, which is what frees the sweep to range the full clear radius.
//   * the ClearRadius volume must cover the WHOLE room — see the companion test
//     SethekkAnzuSweepSpansTheWholeAnteChamber, which pins it against the actual
//     map-556 spawn coordinates rather than against a radius number.
TEST(BossRosterRegistryTest, SethekkAnzuObjectiveHandsOverOnlyAtTheStatue)
{
    std::vector<DungeonBossInfo> base = {
        Boss(18472, 0, "Darkweaver Syth", 556),
        Boss(18473, 2, "Talon King Ikiss", 556),
    };
    std::vector<DungeonBossInfo> out =
        BossRosterRegistry::Apply(556, DUNGEON_DIFFICULTY_HEROIC, base);

    DungeonBossInfo const* anzu = Find(out, BossRosterRegistry::ObjectiveEntry(1));
    ASSERT_NE(anzu, nullptr) << "heroic Sethekk must carry the Anzu objective anchor";
    EXPECT_EQ(anzu->kind, DungeonAnchorKind::Objective);
    EXPECT_EQ(anzu->eventId, 1u);

    DungeonEvent const* ev = DungeonEventRegistry::Find(556, anzu->eventId);
    ASSERT_NE(ev, nullptr);

    float moveRadius = -1.0f;
    float clearRadius = -1.0f;
    for (EventStep const& s : ev->steps)
    {
        if (s.kind == EventStepKind::MoveTo && moveRadius < 0.0f)
            moveRadius = s.radius;
        if (s.kind == EventStepKind::ClearRadius)
            clearRadius = s.radius;
    }
    ASSERT_GT(moveRadius, 0.0f) << "step 0 must park the tank on the anchor";
    ASSERT_GT(clearRadius, 0.0f) << "the event must carry a ClearRadius sweep";

    EXPECT_LE(anzu->arriveRadius, 15.0f)
        << "arriveRadius too large -> the objective seizes the tick a room-length "
           "out, suppressing the corridor trash-clear and letting the pre-clear be "
           "judged (and the summon poked) from outside the room";
    EXPECT_GT(anzu->arriveRadius, moveRadius)
        << "arriveRadius must exceed the MoveTo radius so step 0 completes on "
           "arrival and the persistent at-objective latch engages";

    EXPECT_GT(clearRadius, 0.0f) << "the event must carry a ClearRadius sweep";
}

// Sethekk Halls (556): the Anzu pre-clear must sweep the WHOLE ante-chamber.
//
// REGRESSION GUARD for heroic run tr-20260726-112544-3 (tank Zeeron). By then the
// vantage-point half of the earlier fix was working — the gate certified from
// botDistToCentre=0.0, and everything inside the old 60yd volume really was dead
// — but the summon still fired into a room with five elites standing in it. The
// old geometry note called them "the Ikiss-corridor packs at 73-87yd" and
// excluded them on purpose. They are not in Ikiss's corridor: Ikiss stands at
// (44.7,287), a further 45-60yd on with nothing in between. They are the last
// pack of THIS room, on the same flat floor, in a hall with no doors in it — and
// the pull pipeline never reaches them either, because the tank stops at the
// statue. So nothing cleared them before the poke; the party met them after Anzu
// died (run ...-4 shows the same as a post-Anzu pull of entry 21904, 5 observed).
//
// The fix re-centres the sweep on the ROOM instead of the statue, which sits 53yd
// from the room's south end and 87yd from its north end. This test pins the
// INTENT against the real map-556 spawn coordinates rather than a radius number:
// both ends of the room are inside the volume, and the two things that are NOT
// this room's problem — the Syth-approach pack behind the party and Ikiss ahead
// of it — stay outside.
TEST(BossRosterRegistryTest, SethekkAnzuSweepSpansTheWholeAnteChamber)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(556, 1);
    ASSERT_NE(ev, nullptr);

    EventStep const* clear = nullptr;
    for (EventStep const& s : ev->steps)
        if (s.kind == EventStepKind::ClearRadius)
            clear = &s;
    ASSERT_NE(clear, nullptr) << "the Anzu event must carry a ClearRadius sweep";

    auto covers = [&](float x, float y)
    {
        float const dx = x - clear->x;
        float const dy = y - clear->y;
        return std::sqrt(dx * dx + dy * dy) <= clear->radius;
    };

    // Both ends of the ante-chamber. South: the doorway trio (Sethekk Ravenguard
    // 18322 guid 138666). North: the landing pack at the mouth of Ikiss's chamber
    // (Sethekk Prophet 18325 guid 138688 is its far member, and the Avian Warhawk
    // 21904 guid 138757 at its near end PATROLS).
    EXPECT_TRUE(covers(-141.7f, 283.0f))
        << "sweep must reach the south-doorway trio (Ravenguards + Cobalt Serpent)";
    EXPECT_TRUE(covers(-1.2f, 289.9f))
        << "sweep must reach the north-landing pack — this is the pack that was "
           "left standing when the summon fired; it is part of the room, not of "
           "the walk on to Ikiss";
    EXPECT_TRUE(covers(-14.9f, 293.1f))
        << "sweep must reach the north landing's patrolling Avian Warhawk";

    // ...and no further. The Time-Lost Scryer (18319 guid 138648) is on the Syth
    // approach BEHIND the party, dead on the way in; Talon King Ikiss is the next
    // encounter and belongs to boss-nav, not to a trash sweep.
    EXPECT_FALSE(covers(-171.1f, 282.3f))
        << "sweep must not reach back down the Syth approach";
    EXPECT_FALSE(covers(44.7f, 287.0f))
        << "sweep must stop well short of Talon King Ikiss";

    // Margin for the two patrollers at the volume's edges: the room's far corners
    // sit ~70yd out, so a radius that only just reaches them would drop a mob that
    // happened to be mid-patrol at judging time.
    EXPECT_GE(clear->radius, 78.0f) << "no patrol margin on the room's far ends";

    // A sweep that must walk the hall and fight what it finds cannot live on the
    // 30s EventStepTimeout default: a timed-out step is Failed, and this event is
    // Optional, so Failed skips the rest of the event — dropping the summon on
    // exactly the runs where the pre-clear was needed.
    EXPECT_GE(clear->timeoutMs, 120000u)
        << "ClearRadius needs an explicit long timeout, or an uncleared room "
           "silently skips the Anzu summon";

    // The sweep leaves the tank at the room's centre; Anzu lands at the statue.
    // A MoveTo must gather the party back before the summon step pokes.
    size_t clearIdx = ev->steps.size();
    size_t settleIdx = ev->steps.size();
    size_t customIdx = ev->steps.size();
    for (size_t i = 0; i < ev->steps.size(); ++i)
    {
        if (ev->steps[i].kind == EventStepKind::ClearRadius)
            clearIdx = i;
        else if (ev->steps[i].kind == EventStepKind::MoveTo && clearIdx < i &&
                 settleIdx == ev->steps.size())
            settleIdx = i;
        else if (ev->steps[i].kind == EventStepKind::Custom &&
                 customIdx == ev->steps.size())
            customIdx = i;
    }
    ASSERT_LT(settleIdx, ev->steps.size())
        << "no MoveTo after the sweep — the party would sit through the ~40s "
           "theatrics spread down the hall wherever the last straggler died";
    EXPECT_LT(settleIdx, customIdx) << "the re-settle must precede the summon poke";
    EXPECT_NEAR(ev->steps[settleIdx].x, -88.0f, 1.0f);
    EXPECT_NEAR(ev->steps[settleIdx].y, 288.0f, 1.0f);
}

// --- Apply: Maraudon drops Rotgrip ---------------------------------------

// Rotgrip lives in the Pristine Waters lake — open water the party cannot be
// navigated to — so he is removed from the clear entirely. The rest of the
// Maraudon roster must survive the patch untouched and stay in clear order.
TEST(BossRosterRegistryTest, MaraudonDropsRotgrip)
{
    // The auto-derived Maraudon list as BossSpawnIndex emits it (DBC bits).
    std::vector<DungeonBossInfo> base = {
        Boss(13282, 0, "Noxxion", 349),
        Boss(12258, 1, "Razorlash", 349),
        Boss(12236, 2, "Lord Vyletongue", 349),
        Boss(12225, 3, "Celebras the Cursed", 349),
        Boss(12203, 4, "Landslide", 349),
        Boss(13601, 5, "Tinkerer Gizlock", 349),
        Boss(13596, 6, "Rotgrip", 349),
        Boss(12201, 7, "Princess Theradras", 349),
    };

    std::vector<DungeonBossInfo> const out =
        BossRosterRegistry::Apply(349, DUNGEON_DIFFICULTY_NORMAL, base);

    EXPECT_TRUE(BossRosterRegistry::HasPatch(349));
    EXPECT_EQ(Find(out, 13596), nullptr)
        << "Rotgrip is unreachable (open water) — he must not be in the clear list";
    ASSERT_EQ(out.size(), base.size() - 1);

    // Everything else survives, in DBC-bit order — the patch removes only.
    uint32 const expected[] = {13282, 12258, 12236, 12225, 12203, 13601, 12201};
    for (size_t i = 0; i < out.size(); ++i)
        EXPECT_EQ(out[i].entry, expected[i]) << "clear order changed at slot " << i;

    // Princess Theradras (the real end boss, one bit after Rotgrip) must keep
    // her own kill-bit: removing Rotgrip must not renumber anything.
    DungeonBossInfo const* princess = Find(out, 12201);
    ASSERT_NE(princess, nullptr);
    EXPECT_EQ(princess->encounterIndex, 7u);
}

// --- Apply: Dire Maul North drops Cho'Rush -------------------------------

// Cho'Rush the Observer carries a real DungeonEncounter row but his SmartAI
// sets faction 35 (friendly to all) 5s after spawn and never reverts, so his
// kill-bit can never be set. Left in, he is the anchor the North clear parks on
// forever (tr-20260816-061103-16 idled on him for 40 minutes after King Gordok
// died). The rest of the North roster must survive untouched and in order.
TEST(BossRosterRegistryTest, DireMaulNorthDropsChoRush)
{
    // The auto-derived North list as BossSpawnIndex emits it (DBC bits).
    std::vector<DungeonBossInfo> base = {
        Boss(14326, 1, "Guard Mol'dar", 429),
        Boss(14322, 2, "Stomper Kreeg", 429),
        Boss(14321, 3, "Guard Fengus", 429),
        Boss(14323, 4, "Guard Slip'kik", 429),
        Boss(14325, 5, "Captain Kromcrush", 429),
        Boss(14324, 6, "Cho'Rush the Observer", 429),
        Boss(11501, 7, "King Gordok", 429),
    };

    std::vector<DungeonBossInfo> const out =
        BossRosterRegistry::Apply(429, DUNGEON_DIFFICULTY_NORMAL, base);

    EXPECT_EQ(Find(out, 14324), nullptr)
        << "Cho'Rush is permanently friendly — he must not be in the clear list";

    // The North bosses that remain keep their DBC order. (Dire Maul shares ONE
    // map-429 patch across wings, so Apply() also appends the East/West
    // objectives — filter to real creatures before checking the order.)
    std::vector<uint32> kept;
    for (DungeonBossInfo const& b : out)
        if (b.kind != DungeonAnchorKind::Objective)
            kept.push_back(b.entry);

    std::vector<uint32> const expected = {14326, 14322, 14321, 14323, 14325, 11501};
    EXPECT_EQ(kept, expected) << "North clear order changed";

    // King Gordok (the real end boss, one bit after Cho'Rush) keeps his own
    // kill-bit: removing Cho'Rush must not renumber anything.
    DungeonBossInfo const* gordok = Find(out, 11501);
    ASSERT_NE(gordok, nullptr);
    EXPECT_EQ(gordok->encounterIndex, 7u);
}

// Utgarde Keep (574): the three forge objectives must sort AHEAD of all three
// bosses, in west->east->north order, and the bosses must keep their real DBC
// kill-bits. The reorder is the whole risk here — encounterIndex is what the
// completion mask is read against, so a patch that moved the bits instead of the
// order keys would silently un-complete the dungeon.
TEST(BossRosterRegistryTest, UtgardeKeepForgesSortAheadOfEveryBoss)
{
    std::vector<DungeonBossInfo> base = {
        Boss(23953, 0, "Prince Keleseth", 574),
        Boss(24201, 1, "Skarvold & Dalronn", 574),
        Boss(23954, 2, "Ingvar the Plunderer", 574),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(574, DUNGEON_DIFFICULTY_NORMAL, base);

    ASSERT_EQ(out.size(), 6u) << "3 bosses + 3 forge objectives";

    // Positions in clear order.
    int forge[3] = { -1, -1, -1 };
    int keleseth = -1, dalronn = -1, ingvar = -1;
    int objectivesSeen = 0;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].kind == DungeonAnchorKind::Objective)
        {
            ASSERT_LT(objectivesSeen, 3);
            forge[objectivesSeen++] = i;
        }
        if (out[i].entry == 23953)
            keleseth = i;
        if (out[i].entry == 24201)
            dalronn = i;
        if (out[i].entry == 23954)
            ingvar = i;
    }
    ASSERT_EQ(objectivesSeen, 3) << "three forge objectives expected";
    ASSERT_GE(keleseth, 0);
    ASSERT_GE(dalronn, 0);
    ASSERT_GE(ingvar, 0);

    // Every forge before every boss, and the bosses in unchanged relative order.
    EXPECT_LT(forge[2], keleseth) << "all three forges are done before Keleseth";
    EXPECT_LT(keleseth, dalronn);
    EXPECT_LT(dalronn, ingvar);

    // The forges themselves are in script order: forge 1 (349.6,-39.3) then
    // forge 2 (385.8,-16.2) then forge 3 (347.6,4.6). Engaging one out of order
    // makes it evade, so this sequence IS the feature.
    EXPECT_EQ(out[forge[0]].eventId, 1u);
    EXPECT_EQ(out[forge[1]].eventId, 2u);
    EXPECT_EQ(out[forge[2]].eventId, 3u);
    EXPECT_NEAR(out[forge[0]].x, 349.6f, 0.5f);
    EXPECT_NEAR(out[forge[1]].x, 385.8f, 0.5f);
    EXPECT_NEAR(out[forge[2]].x, 347.6f, 0.5f);

    // The real kill-bits survive the reorder untouched — the clear orders by
    // orderOverride, completion still keys on encounterIndex.
    EXPECT_EQ(Find(out, 23953)->encounterIndex, 0u);
    EXPECT_EQ(Find(out, 24201)->encounterIndex, 1u);
    EXPECT_EQ(Find(out, 23954)->encounterIndex, 2u);
    EXPECT_EQ(Find(out, 23953)->orderOverride, 4);
    EXPECT_EQ(Find(out, 24201)->orderOverride, 5);
    EXPECT_EQ(Find(out, 23954)->orderOverride, 6);

    // No boss was removed or re-added: they keep the auto-derived spawn coords.
    for (DungeonBossInfo const& b : out)
        if (b.kind == DungeonAnchorKind::Boss)
            EXPECT_EQ(b.inheritCompletionFrom, 0u)
                << "Utgarde Keep needs no boss surgery — the derived list is correct";
}

TEST(BossRosterRegistryTest, NexusSpheresSortBetweenOrmorokAndKeristrasza)
{
    // Derived roster, normal difficulty (DungeonEncounter.dbc bits 0-3).
    std::vector<DungeonBossInfo> base = {
        Boss(26731, 0, "Grand Magus Telestra", 576),
        Boss(26763, 1, "Anomalus", 576),
        Boss(26794, 2, "Ormorok the Tree-Shaper", 576),
        Boss(26723, 3, "Keristrasza", 576),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(576, DUNGEON_DIFFICULTY_NORMAL, base);

    ASSERT_EQ(out.size(), 7u) << "4 bosses + 3 sphere objectives";

    int sphere[3] = { -1, -1, -1 };
    int telestra = -1, anomalus = -1, ormorok = -1, keristrasza = -1;
    int objectivesSeen = 0;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].kind == DungeonAnchorKind::Objective)
        {
            ASSERT_LT(objectivesSeen, 3);
            sphere[objectivesSeen++] = i;
        }
        if (out[i].entry == 26731)
            telestra = i;
        if (out[i].entry == 26763)
            anomalus = i;
        if (out[i].entry == 26794)
            ormorok = i;
        if (out[i].entry == 26723)
            keristrasza = i;
    }
    ASSERT_EQ(objectivesSeen, 3) << "three sphere objectives expected";
    ASSERT_GE(telestra, 0);
    ASSERT_GE(anomalus, 0);
    ASSERT_GE(ormorok, 0);
    ASSERT_GE(keristrasza, 0);

    // A sphere is only clickable once its own boss is dead (instance_nexus clears
    // GO_FLAG_NOT_SELECTABLE in SetBossState), and all three must be clicked
    // before Keristrasza can be attacked at all.
    EXPECT_LT(telestra, anomalus);
    EXPECT_LT(anomalus, ormorok);
    EXPECT_LT(ormorok, sphere[0]) << "every orb boss dies before the first click";
    EXPECT_LT(sphere[2], keristrasza) << "all three clicks land before Keristrasza";

    // Walk order across the hub: Telestra's sphere (south), Ormorok's (north-west),
    // Anomalus' (north-east) — 81yd rather than the 98yd of any other order.
    EXPECT_EQ(out[sphere[0]].eventId, 1u);
    EXPECT_EQ(out[sphere[1]].eventId, 2u);
    EXPECT_EQ(out[sphere[2]].eventId, 3u);
    EXPECT_NEAR(out[sphere[0]].x, 281.9f, 0.5f);
    EXPECT_NEAR(out[sphere[0]].y, -25.5f, 0.5f);
    EXPECT_NEAR(out[sphere[1]].x, 281.8f, 0.5f);
    EXPECT_NEAR(out[sphere[1]].y, 15.2f, 0.5f);
    EXPECT_NEAR(out[sphere[2]].x, 322.2f, 0.5f);
    EXPECT_NEAR(out[sphere[2]].y, 14.7f, 0.5f);

    // The real kill-bits survive the reorder untouched.
    EXPECT_EQ(Find(out, 26731)->encounterIndex, 0u);
    EXPECT_EQ(Find(out, 26763)->encounterIndex, 1u);
    EXPECT_EQ(Find(out, 26794)->encounterIndex, 2u);
    EXPECT_EQ(Find(out, 26723)->encounterIndex, 3u);

    // No boss surgery — the derived list is already in travel order.
    for (DungeonBossInfo const& b : out)
        if (b.kind == DungeonAnchorKind::Boss)
            EXPECT_EQ(b.inheritCompletionFrom, 0u);
}

TEST(BossRosterRegistryTest, NexusHeroicCommanderStaysFirst)
{
    // Heroic shifts every DBC bit up by one to make room for the Frozen Commander
    // at bit 0. The HeroicOnly patch re-adds him on order key 1, which must still
    // sort ahead of the 2..8 scale the rest of the map is reordered onto.
    std::vector<DungeonBossInfo> base = {
        Boss(26796, 0, "Frozen Commander", 576),
        Boss(26731, 1, "Grand Magus Telestra", 576),
        Boss(26763, 2, "Anomalus", 576),
        Boss(26794, 3, "Ormorok the Tree-Shaper", 576),
        Boss(26723, 4, "Keristrasza", 576),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(576, DUNGEON_DIFFICULTY_HEROIC, base);

    ASSERT_EQ(out.size(), 8u) << "5 bosses + 3 sphere objectives";
    EXPECT_EQ(out.front().entry, 26796u) << "the Frozen Commander still leads the clear";
    EXPECT_EQ(out.back().entry, 26723u) << "Keristrasza is still last";
    EXPECT_EQ(Find(out, 26796)->orderOverride, 1)
        << "the Commander sits on key 1, between bit 0 and the 2..8 scale";

    // The three spheres are still the last thing before Keristrasza.
    ASSERT_GE(out.size(), 4u);
    for (size_t i = out.size() - 4; i + 1 < out.size(); ++i)
        EXPECT_EQ(out[i].kind, DungeonAnchorKind::Objective)
            << "index " << i << " should be a sphere objective";
}

// The Frozen Commander's DBC kill-bit is UNUSABLE: instance_encounters has
// PRIMARY KEY (`entry`), so encounter 519 can only credit one creature (26796),
// and an Alliance party kills the UpdateEntry'd 26798 — bit 0 never flips. The
// heroic patch must therefore re-add him reading the instance script's own
// DATA_COMMANDER_EVENT slot, with encounterIndex parked out of mask range so the
// dead bit is never consulted on EITHER faction.
TEST(BossRosterRegistryTest, NexusHeroicCommanderCompletesViaBossState)
{
    std::vector<DungeonBossInfo> base = {
        Boss(26796, 0, "Frozen Commander", 576),
        Boss(26731, 1, "Grand Magus Telestra", 576),
        Boss(26763, 2, "Anomalus", 576),
        Boss(26794, 3, "Ormorok the Tree-Shaper", 576),
        Boss(26723, 4, "Keristrasza", 576),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(576, DUNGEON_DIFFICULTY_HEROIC, base);

    DungeonBossInfo const* cmd = Find(out, 26796);
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->kind, DungeonAnchorKind::Boss);
    EXPECT_EQ(cmd->doneBossStateIndex, 4) << "DATA_COMMANDER_EVENT from nexus.h";
    EXPECT_GE(cmd->encounterIndex, 32u)
        << "parked out of the completed-encounter mask's range so bit 0 is never read";

    // The re-add restates the spawn coords; losing them would send the tank to
    // (0,0,0). The floor here is the lower ring (-34.9), not the hub's -16.
    EXPECT_NEAR(cmd->x, 424.5f, 0.5f);
    EXPECT_NEAR(cmd->y, 186.0f, 0.5f);
    EXPECT_NEAR(cmd->z, -34.9f, 0.5f);

    // No other Nexus anchor borrows a boss-state slot — Keristrasza in particular
    // owns heroic bit 4, the same NUMBER as DATA_COMMANDER_EVENT in the instance
    // script's unrelated index space.
    for (DungeonBossInfo const& b : out)
        if (b.entry != 26796u)
            EXPECT_EQ(b.doneBossStateIndex, -1) << "entry " << b.entry;
}

// The commander is heroic-only (spawnMask 2, DBC difficulty 1), so BossSpawnIndex
// never puts him in the normal bucket. The HeroicOnly gate must keep the patch off
// a normal run entirely — a stray re-add there would invent an anchor at a spawn
// that does not exist on that difficulty.
TEST(BossRosterRegistryTest, NexusNormalHasNoFrozenCommander)
{
    std::vector<DungeonBossInfo> base = {
        Boss(26731, 0, "Grand Magus Telestra", 576),
        Boss(26763, 1, "Anomalus", 576),
        Boss(26794, 2, "Ormorok the Tree-Shaper", 576),
        Boss(26723, 3, "Keristrasza", 576),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(576, DUNGEON_DIFFICULTY_NORMAL, base);

    ASSERT_EQ(out.size(), 7u) << "4 bosses + 3 sphere objectives";
    EXPECT_EQ(Find(out, 26796), nullptr) << "no Frozen Commander on normal";
    EXPECT_EQ(Find(out, 26798), nullptr) << "nor his Alliance-side entry";
    for (DungeonBossInfo const& b : out)
        EXPECT_EQ(b.doneBossStateIndex, -1) << "entry " << b.entry;
}

// Azjol-Nerub (601): Hadronox is re-anchored from her spawn ledge onto the
// platform her own script climbs to, and the two objectives must bracket her —
// the crusher/web hold BEFORE her, the drop AFTER. She is the only boss on the
// clear that is remove+re-added purely to move where the fight happens, so the
// risk is the usual one: a patch that moved her kill-bit instead of her order
// key would silently un-complete the dungeon.
TEST(BossRosterRegistryTest, AzjolNerubBracketsHadronoxWithTheWebHoldAndTheDrop)
{
    std::vector<DungeonBossInfo> base = {
        Boss(28684, 0, "Krik'thir the Gatewatcher", 601),
        Boss(28921, 1, "Hadronox", 601),
        Boss(29120, 2, "Anub'arak", 601),
    };
    std::vector<DungeonBossInfo> out = BossRosterRegistry::Apply(601, DUNGEON_DIFFICULTY_NORMAL, base);

    ASSERT_EQ(out.size(), 5u) << "3 bosses + the web hold + the drop";

    int krikthir = -1, hadronox = -1, anubarak = -1, webHold = -1, drop = -1;
    for (int i = 0; i < (int)out.size(); ++i)
    {
        if (out[i].entry == 28684) krikthir = i;
        if (out[i].entry == 28921) hadronox = i;
        if (out[i].entry == 29120) anubarak = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 1) webHold = i;
        if (out[i].kind == DungeonAnchorKind::Objective && out[i].eventId == 2) drop = i;
    }
    ASSERT_GE(krikthir, 0);
    ASSERT_GE(hadronox, 0);
    ASSERT_GE(anubarak, 0);
    ASSERT_GE(webHold, 0) << "the 'web the doors' objective (eventId 1) is missing";
    ASSERT_GE(drop, 0) << "the drop objective (eventId 2) is missing";

    // Krik'thir -> web hold -> Hadronox -> drop -> Anub'arak.
    EXPECT_LT(krikthir, webHold);
    EXPECT_LT(webHold, hadronox)
        << "the swarm's off-switch has to be thrown BEFORE she is handed to boss nav";
    EXPECT_LT(hadronox, drop);
    EXPECT_LT(drop, anubarak)
        << "the lower kingdom is across a hard navmesh break — the drop must come first";

    // Hadronox now stands on the platform (her own MOVE3 destination), NOT on her
    // spawn ledge at (522.5, 544.9, 674.7) 60yd below it.
    EXPECT_NEAR(out[hadronox].x, 530.4f, 1.0f);
    EXPECT_NEAR(out[hadronox].y, 560.0f, 1.0f);
    EXPECT_GT(out[hadronox].z, 700.0f)
        << "anchoring her on the z~675 spawn ledge walks the party into the add funnel";

    // The web hold shares the platform with her; the drop sits on the pit floor.
    EXPECT_NEAR(out[webHold].x, 530.4f, 1.0f);
    EXPECT_NEAR(out[webHold].z, 733.8f, 1.0f);
    EXPECT_NEAR(out[drop].x, 522.0f, 1.0f);
    EXPECT_NEAR(out[drop].z, 648.9f, 1.0f);

    // Kill-bits untouched: the clear orders by orderOverride, completion still
    // keys on encounterIndex, and Hadronox keeps HER OWN bit across remove+re-add.
    EXPECT_EQ(Find(out, 28684)->encounterIndex, 0u);
    EXPECT_EQ(Find(out, 28921)->encounterIndex, 1u);
    EXPECT_EQ(Find(out, 29120)->encounterIndex, 2u);
}

