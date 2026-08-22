/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCFACTIONENTRYSWAPREGISTRY_H
#define _PLAYERBOT_DCFACTIONENTRYSWAPREGISTRY_H

#include "Common.h"
#include "SharedDefines.h"

// Bosses whose CREATURE ENTRY depends on the team that opened the instance.
//
// A handful of two-faction instances (InstanceScript::IsTwoFactionInstance) ship
// ONE creature_data row for a boss and have the instance script UpdateEntry it to
// the opposing faction's creature in OnCreatureCreate. BossSpawnIndex only ever
// sees the entry the spawn row carries, so on the swapped team the derived roster
// names a creature that is not in the world, and every entry-keyed lookup
// downstream misses:
//
//   * NextDungeonBossValue::BuildLiveness — the boss never reads as present, so
//     it never reads as a corpse either and stays a candidate after it dies;
//   * DcTargeting::GetLiveBoss / DungeonClearLiveBossValue — no engage geometry,
//     no first-contact, no boss pull plan;
//   * DcTargeting::IsCreaturePresentOnMap — Advance hard-stalls "not spawned"
//     once inside DC_BOSS_GRID_LOADED_RANGE (150yd), which for a boss the party
//     has to walk up to is ~130yd BEFORE aggro range. The run wedges there.
//
// The fix is a single rewrite of DungeonBossInfo::entry in DungeonBossesValue,
// keyed off the instance's team. Everything else about the anchor — name, coords,
// order key, completion source — is unchanged, and because the skip / sticky /
// cleared-anchor sets are all keyed off that same list, they stay consistent.
//
// This table is data only (no Player, no Map) so the registry gtests can pin it.
namespace DcFactionEntrySwap
{
    struct Rule
    {
        uint32 mapId;
        uint32 spawnEntry;    // what creature_data (and so BossSpawnIndex) carries
        uint32 swappedEntry;  // what the instance script UpdateEntry's it to
        TeamId swappedForTeam;
    };

    // The Nexus (576) — the Frozen Commander, heroic-only bonus boss. The spawn
    // (guid 4764, spawnMask 2, at (424.5,186,-34.9)) is entered as 26796 Commander
    // Stoutbeard; `instance_nexus::OnCreatureCreate` UpdateEntry's it to 26798
    // Commander Kolurg when GetTeamIdInInstance() == TEAM_ALLIANCE.
    //
    // Its completion does NOT ride the DBC kill-bit either — instance_encounters
    // has PRIMARY KEY (`entry`), so encounter 519 can only ever credit ONE entry
    // (26796), and KillRewarder passes the victim's CURRENT entry. See the heroic
    // roster patch in NexusEvents.cpp, which reads DATA_COMMANDER_EVENT instead.
    inline constexpr Rule kRules[] =
    {
        { 576, 26796, 26798, TEAM_ALLIANCE },
    };

    // The entry to track for `entry` on `mapId` given the instance's team, or
    // `entry` itself when no rule applies (the overwhelmingly common case).
    inline constexpr uint32 Resolve(uint32 mapId, uint32 entry, TeamId team)
    {
        for (Rule const& rule : kRules)
            if (rule.mapId == mapId && rule.spawnEntry == entry && rule.swappedForTeam == team)
                return rule.swappedEntry;
        return entry;
    }

    // True if any rule could fire on this map (cheap gate for callers).
    inline constexpr bool HasRules(uint32 mapId)
    {
        for (Rule const& rule : kRules)
            if (rule.mapId == mapId)
                return true;
        return false;
    }
}

#endif
