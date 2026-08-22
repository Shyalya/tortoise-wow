/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcDoorIndex.h"

#include <map>
#include <mutex>
#include <utility>

#include "GameObject.h"
#include "Map.h"
#include "SharedDefines.h"
#include "Timer.h"

namespace
{
    struct DoorIndexEntry
    {
        std::vector<ObjectGuid> doors;
        std::uint32_t builtMs = 0;        // 0 = never built
        std::size_t storeSize = 0;        // GO store size at build time
    };

    // Keyed by (mapId, instanceId): two continent maps both report instanceId 0,
    // so the map id disambiguates them. Mutex-guarded like the other file-scope
    // DC registries (DcLeaderSignal): every caller runs on the world/map thread
    // today, but the lock is uncontended and keeps the structure correct if bot
    // updates ever move off-thread.
    std::map<std::pair<std::uint32_t, std::uint32_t>, DoorIndexEntry> g_doorIndex;
    std::mutex g_doorIndexMutex;

    // Lazy-janitor bound: past this many cached maps, drop the whole table on the
    // next build (dungeon instances are torn down constantly; a handful of live
    // maps is the steady state, so a periodic full clear is simpler than tracking
    // per-instance liveness and costs one rebuild per surviving map).
    constexpr std::size_t DOOR_INDEX_SWEEP_SIZE = 64;
}

bool DcDoorIndex::NeedsRebuild(std::uint32_t builtMs, std::uint32_t now,
                               std::size_t storeSizeAtBuild, std::size_t storeSizeNow)
{
    if (builtMs == 0)
        return true;                        // never built
    if (storeSizeAtBuild != storeSizeNow)
        return true;                        // grids streamed in/out
    return getMSTimeDiff(builtMs, now) >= kRebuildMs;
}

std::vector<ObjectGuid> const& DcDoorIndex::Get(Map* map)
{
    static std::vector<ObjectGuid> const kEmpty;
    if (!map)
        return kEmpty;

    std::uint32_t const now = getMSTime();
    auto const key = std::make_pair(map->GetId(), map->GetInstanceId());

    std::lock_guard<std::mutex> lock(g_doorIndexMutex);

    if (g_doorIndex.size() > DOOR_INDEX_SWEEP_SIZE)
        g_doorIndex.clear();

    DoorIndexEntry& entry = g_doorIndex[key];
    std::size_t const storeSize = map->GetGameObjectBySpawnIdStore().size();
    if (NeedsRebuild(entry.builtMs, now, entry.storeSize, storeSize))
    {
        entry.doors.clear();
        for (auto const& kv : map->GetGameObjectBySpawnIdStore())
        {
            GameObject* go = kv.second;
            if (!go)
                continue;
            GameObjectTemplate const* info = go->GetGOInfo();
            if (info && info->type == GAMEOBJECT_TYPE_DOOR)
                entry.doors.push_back(go->GetObjectGuid());
        }
        entry.builtMs = now ? now : 1;     // never store 0 (== "never built")
        entry.storeSize = storeSize;
    }
    return entry.doors;
}
