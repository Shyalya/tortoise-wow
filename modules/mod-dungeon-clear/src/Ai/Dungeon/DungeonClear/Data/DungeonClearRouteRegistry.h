/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARROUTEREGISTRY_H
#define _PLAYERBOT_DUNGEONCLEARROUTEREGISTRY_H

#include <unordered_map>
#include <vector>

#include "Common.h"
#include "DBCEnums.h"
#include "WaypointHint.h"

// Per-dungeon, per-boss list of waypoint anchors. The chunked pathfinder uses
// these as trusted intermediate hops between the bot's current position and
// the boss creature's spawn point. Lookup is keyed by (mapId, difficulty,
// bossEntry); a non-normal miss falls back to the map's NORMAL row (heroic
// shares the geometry, and routes are authored under normal); a full miss
// simply means anchor-free chunking is used for that boss.
//
// Registration mirrors the other DungeonClear data registries: the dungeon's
// route lives with the rest of its clear data in Data/Events/<Dungeon>Events.cpp
// as a Register<Dungeon>Route() appender, declared in DungeonEventTables.h and
// called from SeedAuthoredRoutes() in the .cpp. The call is EXPLICIT on purpose
// — a self-registering static in a TU nothing references is stripped from the
// module static lib, taking its route with it. Adding a route is one appender
// plus one line in the seed.
class DungeonClearRouteRegistry
{
public:
    static void Register(uint32 mapId, Difficulty difficulty, uint32 bossEntry, std::vector<WaypointHint> hints);
    static std::vector<WaypointHint> const* Get(uint32 mapId, Difficulty difficulty, uint32 bossEntry);

private:
    struct Key
    {
        uint32 mapId;
        Difficulty difficulty;
        uint32 bossEntry;
        bool operator==(Key const& other) const
        {
            return mapId == other.mapId && difficulty == other.difficulty && bossEntry == other.bossEntry;
        }
    };
    struct KeyHash
    {
        std::size_t operator()(Key const& k) const noexcept
        {
            // Mix all three fields — encounters at the same boss across
            // different difficulties land in different buckets.
            std::size_t h = std::hash<uint32>{}(k.mapId);
            h = h * 31 + std::hash<uint32>{}(static_cast<uint32>(k.difficulty));
            h = h * 31 + std::hash<uint32>{}(k.bossEntry);
            return h;
        }
    };

    static std::unordered_map<Key, std::vector<WaypointHint>, KeyHash>& Store();
};

#endif
