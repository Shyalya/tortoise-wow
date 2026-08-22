/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearRouteRegistry.h"

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"

namespace
{
    // One-time seed of the hand-authored routes.
    //
    // The per-dungeon appenders are called EXPLICITLY, for the same reason the
    // event and roster tables do it (see DungeonEventTables.h): the module is a
    // static lib, and a translation unit whose only output is constructor side
    // effects — which is what the "static Register instance" pattern this header
    // used to describe would be — is dropped by the linker along with its rows.
    //
    // Seeded lazily from Get() rather than from a namespace-scope initialiser so
    // it cannot race the Store() static's own construction. Register() is still
    // callable directly; the unit tests use it with synthetic map ids.
    void SeedAuthoredRoutes()
    {
        static bool const seeded = []
        {
            RegisterAzjolNerubRoute();
            return true;
        }();
        (void)seeded;
    }
}

std::unordered_map<DungeonClearRouteRegistry::Key, std::vector<WaypointHint>, DungeonClearRouteRegistry::KeyHash>&
DungeonClearRouteRegistry::Store()
{
    static std::unordered_map<Key, std::vector<WaypointHint>, KeyHash> instance;
    return instance;
}

void DungeonClearRouteRegistry::Register(uint32 mapId, Difficulty difficulty, uint32 bossEntry,
                                         std::vector<WaypointHint> hints)
{
    Store()[Key{mapId, difficulty, bossEntry}] = std::move(hints);
}

std::vector<WaypointHint> const* DungeonClearRouteRegistry::Get(uint32 mapId, Difficulty difficulty, uint32 bossEntry)
{
    SeedAuthoredRoutes();
    auto const& s = Store();
    auto it = s.find(Key{mapId, difficulty, bossEntry});
    // Heroic shares the normal dungeon's geometry, and the hand-authored routes
    // are registered under normal — fall back so a heroic run still gets its
    // waypoint hints. A difficulty-specific row, when one exists, wins.
    if (it == s.end() && difficulty != DUNGEON_DIFFICULTY_NORMAL)
        it = s.find(Key{mapId, DUNGEON_DIFFICULTY_NORMAL, bossEntry});
    if (it == s.end())
        return nullptr;
    return &it->second;
}
