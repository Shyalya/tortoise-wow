/*
 * mod-dungeon-clear - DcEncounterMask.cpp  (Tortoise port; see the header)
 */

#include "Ai/Dungeon/DungeonClear/Util/DcEncounterMask.h"

#include "Maps/Map.h"
#include "Maps/MapManager.h"
#include "Timer.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    struct Entry
    {
        uint32 mapId = 0;
        uint32 mask = 0;
    };

    // Writers run on the instance's own map thread (the kill), readers mostly
    // on that same thread (the bots inside), but the test-run monitor reads
    // from the world tick - so the registry itself takes a mutex. Accesses
    // are short and dungeon-kill-rare.
    std::mutex g_mutex;
    std::unordered_map<uint32 /*instanceId*/, Entry> g_masks;
}

namespace DcEncounterMask
{
    void OnBossKilled(Map const* map, uint32 encounterIndex)
    {
        if (!map || !map->IsDungeon() || encounterIndex >= 32)
            return;

        std::lock_guard<std::mutex> lock(g_mutex);
        Entry& e = g_masks[map->GetInstanceId()];
        if (e.mapId != map->GetId())
        {
            // Recycled instance id (or first sighting) - start clean.
            e.mapId = map->GetId();
            e.mask = 0;
        }
        e.mask |= (1u << encounterIndex);
    }

    uint32 Get(Map const* map)
    {
        if (!map || !map->IsDungeon())
            return 0;

        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_masks.find(map->GetInstanceId());
        if (it == g_masks.end() || it->second.mapId != map->GetId())
            return 0;
        return it->second.mask;
    }

    void Sweep()
    {
        static uint32 lastSweep = 0;
        uint32 const now = WorldTimer::getMSTime();
        if (lastSweep && WorldTimer::getMSTimeDiff(lastSweep, now) < 60000)
            return;
        lastSweep = now;

        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto it = g_masks.begin(); it != g_masks.end();)
        {
            if (!sMapMgr.FindMap(it->second.mapId, it->first))
                it = g_masks.erase(it);
            else
                ++it;
        }
    }
}
