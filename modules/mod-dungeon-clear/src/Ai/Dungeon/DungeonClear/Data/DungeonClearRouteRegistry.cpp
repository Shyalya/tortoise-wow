/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearRouteRegistry.h"
#include "Config.h"
#include <cstdio>
#include <fstream>
#include <dirent.h>

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"

// Generated collector (routes/RecordedRoutes.cpp). Declared at file scope:
// inside the anonymous namespace it would get internal linkage and never
// find its definition.
void RegisterAllRecordedRoutes();

// Reads modules/mod-dungeon-clear/src/Routes/*.route (written by
// DcRouteRecorder alongside its .cpp twin) and registers each one. This is
// what makes a route usable after a plain restart instead of a rebuild.
static void LoadRecordedRoutesFromDisk()
{
    std::string dir = sConfig.GetStringDefault("DungeonClear.RouteRecorderDir", "");
    if (dir.empty())
        return;

    uint32 loaded = 0;
    if (DIR* d = opendir(dir.c_str()))
    {
        while (dirent* e = readdir(d))
        {
            std::string const name = e->d_name;
            if (name.size() < 7 || name.compare(name.size() - 6, 6, ".route") != 0)
                continue;
            std::ifstream in((dir + "/" + name).c_str());
            if (!in.is_open())
                continue;
            std::string header;
            std::getline(in, header);
            uint32 mapId = 0, bossEntry = 0;
            if (std::sscanf(header.c_str(), "# map %u boss %u", &mapId, &bossEntry) != 2)
                continue;
            std::vector<WaypointHint> hints;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            while (in >> x >> y >> z)
                hints.push_back(WaypointHint{x, y, z, 0, 0, 6.0f});
            if (hints.size() >= 3)
            {
                DungeonClearRouteRegistry::Register(mapId, DUNGEON_DIFFICULTY_NORMAL,
                                                    bossEntry, std::move(hints));
                ++loaded;
            }
        }
        closedir(d);
    }
    if (loaded)
        LOG_INFO("playerbots.dungeonclear",
                 "[DC-ROUTE] loaded {} recorded route(s) from {}", loaded, dir);
}

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
            // Everything the route recorder captured from live clears (see
            // modules/mod-dungeon-clear/routes/). Generated collector; a
            // recorded route only becomes live once it is called from here.
            RegisterAllRecordedRoutes();
            // ...and then whatever the recorder has captured SINCE that build.
            // Loaded last so a freshly recorded (and, by the recorder's own
            // shortest-wins rule, better) route wins over the compiled one.
            LoadRecordedRoutesFromDisk();
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
