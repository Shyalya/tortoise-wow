/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearRouteRegistry.h"
#include <mutex>
#include "Config.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <system_error>

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
    // std::filesystem statt dirent.h: MSVC kennt dirent nicht, und der
    // Iterator spart den manuellen Endungsvergleich samt closedir.
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec), ende;
    if (!ec)
    {
        for (; it != ende; it.increment(ec))
        {
            if (ec)
                break;
            if (!it->is_regular_file(ec) || ec)
                continue;
            if (it->path().extension() != ".route")
                continue;
            std::ifstream in(it->path());
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
                // "pinned" anywhere in the header marks a route the recorder may
                // neither replace nor discard. Pin BEFORE registering, so the
                // very first Register is the one that sticks.
                bool const pinned = header.find("pinned") != std::string::npos;
                if (pinned)
                    DungeonClearRouteRegistry::Pin(mapId, DUNGEON_DIFFICULTY_NORMAL, bossEntry);
                DungeonClearRouteRegistry::Register(mapId, DUNGEON_DIFFICULTY_NORMAL,
                                                    bossEntry, std::move(hints));
                if (pinned)
                    LOG_INFO("playerbots.dungeonclear",
                             "[DC-ROUTE] map {} boss {}: route is PINNED (recorder will not touch it)",
                             mapId, bossEntry);
                ++loaded;
            }
        }
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

std::mutex& DungeonClearRouteRegistry::RegistryLock()
{
    static std::mutex instance;
    return instance;
}

std::unordered_map<DungeonClearRouteRegistry::Key, std::vector<WaypointHint>, DungeonClearRouteRegistry::KeyHash>&
DungeonClearRouteRegistry::Store()
{
    static std::unordered_map<Key, std::vector<WaypointHint>, KeyHash> instance;
    return instance;
}

std::unordered_set<DungeonClearRouteRegistry::Key, DungeonClearRouteRegistry::KeyHash>&
DungeonClearRouteRegistry::PinnedSet()
{
    static std::unordered_set<Key, KeyHash> instance;
    return instance;
}

void DungeonClearRouteRegistry::Pin(uint32 mapId, Difficulty difficulty, uint32 bossEntry)
{
    std::lock_guard<std::mutex> lock(RegistryLock());
    PinnedSet().insert(Key{mapId, difficulty, bossEntry});
}

bool DungeonClearRouteRegistry::IsPinned(uint32 mapId, Difficulty difficulty, uint32 bossEntry)
{
    std::lock_guard<std::mutex> lock(RegistryLock());
    return PinnedSet().count(Key{mapId, difficulty, bossEntry}) != 0;
}

void DungeonClearRouteRegistry::Register(uint32 mapId, Difficulty difficulty, uint32 bossEntry,
                                         std::vector<WaypointHint> hints)
{
    std::lock_guard<std::mutex> lock(RegistryLock());
    Key const key{mapId, difficulty, bossEntry};
    // A pinned route is never replaced. The recorder's shortest-wins rule is
    // right for ordinary ground and wrong for a ledge: "shorter" there usually
    // means the line was cut across the water the ledge exists to avoid.
    if (PinnedSet().count(key) && !Store()[key].empty())
        return;
    Store()[key] = std::move(hints);
}

bool DungeonClearRouteRegistry::Forget(uint32 mapId, Difficulty difficulty, uint32 bossEntry)
{
    SeedAuthoredRoutes();
    std::lock_guard<std::mutex> lock(RegistryLock());
    Key const key{mapId, difficulty, bossEntry};
    // A pinned route survives the stuck ladder. Returning false here also stops
    // the caller from renaming the files to .bad (DcRouteRecorder::DiscardRoute
    // runs only when Forget reported a removal), so the decision holds across
    // restarts instead of only until the next wedge.
    if (PinnedSet().count(key))
    {
        LOG_INFO("playerbots.dungeonclear",
                 "[DC-ROUTE] keeping PINNED route for map {} boss {} despite a stuck ladder",
                 mapId, bossEntry);
        return false;
    }
    return Store().erase(key) > 0;
}

bool DungeonClearRouteRegistry::Has(uint32 mapId, Difficulty difficulty, uint32 bossEntry)
{
    // Seed BEFORE taking the lock: seeding registers, and Register() takes
    // this same lock.
    SeedAuthoredRoutes();
    std::lock_guard<std::mutex> lock(RegistryLock());
    auto const it = Store().find(Key{mapId, difficulty, bossEntry});
    return it != Store().end() && !it->second.empty();
}

bool DungeonClearRouteRegistry::TryGet(uint32 mapId, Difficulty difficulty, uint32 bossEntry,
                                       std::vector<WaypointHint>& out)
{
    // Same order as Has(): seed first, lock second.
    SeedAuthoredRoutes();
    std::lock_guard<std::mutex> lock(RegistryLock());
    auto const it = Store().find(Key{mapId, difficulty, bossEntry});
    if (it == Store().end() || it->second.empty())
        return false;
    out = it->second;
    return true;
}

