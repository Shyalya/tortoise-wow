/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "BossSpawnIndex.h"

#include <algorithm>

#include "CreatureData.h"
#include "CreatureSpawnEntry.h"
#include "DcBossEntries1121.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "ObjectMgr.h"

std::unordered_map<uint64, std::vector<DungeonBossInfo>> BossSpawnIndex::_store;
bool BossSpawnIndex::_built = false;

std::vector<DungeonBossInfo> const& BossSpawnIndex::Get(uint32 mapId, Difficulty difficulty)
{
    EnsureBuilt();
    static std::vector<DungeonBossInfo> const empty;
    auto it = _store.find(MakeKey(mapId, difficulty));
    if (it == _store.end())
        return empty;
    return it->second;
}

void BossSpawnIndex::EnsureBuilt()
{
    if (_built)
        return;
    Build();
    _built = true;
}

void BossSpawnIndex::Build()
{
    // 1. Build creditEntry -> encounter list, keyed by (mapId, difficulty).
    //    Only kill-creature encounters are usable here.
    struct EncounterRow
    {
        uint32 mapId;
        Difficulty difficulty;
        uint32 encounterIndex;
        std::string name;
        uint32 creditEntry;
    };

    // Tortoise port: there is no DungeonEncounter data on a 1.12 core - the
    // DBC arrived with Wrath. The boss set comes from the baked-in list
    // (DcBossEntries1121.h, provenance there); map and coordinates join in
    // from the spawns below, the name from the creature template, and the
    // encounter index is assigned per map in entry order - see the header
    // note on what that costs.
    std::unordered_multimap<uint32, EncounterRow> byCreditEntry;
    {
        std::map<uint32, uint32> nextIndexOnMap; // deterministic: entries ascend
        for (uint32 entry : DC_BOSS_ENTRIES_1121)
        {
            CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
            if (!info)
                continue; // realm without this custom entry

            EncounterRow row;
            row.mapId = 0;               // filled per spawn below
            row.difficulty = DUNGEON_DIFFICULTY_NORMAL;
            row.encounterIndex = 0;      // assigned when the spawn fixes the map
            row.name = info->name;
            row.creditEntry = entry;
            byCreditEntry.emplace(entry, std::move(row));
        }

        // Door bosses and friends that only the order table names - they are
        // bosses to the router even though the curated list skipped them.
        for (DcBossOrderRow const& orow : DC_BOSS_ORDER_1121)
        {
            if (byCreditEntry.count(orow.entry))
                continue;
            CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(orow.entry);
            if (!info)
                continue;

            EncounterRow row;
            row.mapId = 0;
            row.difficulty = DUNGEON_DIFFICULTY_NORMAL;
            row.encounterIndex = 0;
            row.name = info->name;
            row.creditEntry = orow.entry;
            byCreditEntry.emplace(orow.entry, std::move(row));
        }
    }

    if (byCreditEntry.empty())
        return;

    // Encounter numbering: an authored order wins (DC_BOSS_ORDER_1121,
    // 1-based there, 0-based here to line up with the mask bits); everything
    // else on the map numbers upward from just past the authored block.
    auto orderFor = [](uint32 mapId, uint32 entry) -> uint32
    {
        for (DcBossOrderRow const& r : DC_BOSS_ORDER_1121)
            if (r.mapId == mapId && r.entry == entry)
                return r.order;
        return 0;
    };
    std::map<uint32, uint32> firstFallback;
    for (DcBossOrderRow const& r : DC_BOSS_ORDER_1121)
        firstFallback[r.mapId] = std::max<uint32>(firstFallback[r.mapId], r.order);
    std::map<uint32, uint32> encounterIndexOnMap;

    // 2. Walk every creature spawn once. For each spawn whose entry matches a
    //    boss credit entry on its mapId, record it.
    CreatureDataMap const& spawns = sObjectMgr.GetAllCreatureData();
    for (auto const& kv : spawns)
    {
        CreatureData const& data = kv.second;
        uint32 const entry = DungeonClear::SpawnEntry(data);
        if (!entry)
            continue;

        auto range = byCreditEntry.equal_range(entry);
        for (auto it = range.first; it != range.second; ++it)
        {
            EncounterRow const& row = it->second;

            // No spawn masks on a 1.12 core - one difficulty, every spawn is in
            // it. The map comes from the spawn itself, and the index counts up
            // per map in the order the entries arrive (ascending, see above).
            uint32 const mapId = data.position.mapId;

            DungeonBossInfo info;
            info.entry = entry;
            if (uint32 const order = orderFor(mapId, entry))
                info.encounterIndex = order - 1;
            else
            {
                uint32& next = encounterIndexOnMap[mapId];
                if (next == 0)
                    next = firstFallback.count(mapId) ? firstFallback[mapId] : 0;
                info.encounterIndex = next++;
            }
            info.name = row.name;
            info.mapId = mapId;
            info.x = data.position.x;
            info.y = data.position.y;
            info.z = data.position.z;
            _store[MakeKey(mapId, DUNGEON_DIFFICULTY_NORMAL)].push_back(info);
        }
    }

    // 3. Sort each bucket by encounter index; deduplicate by entry (keep the
    //    first spawn — there is usually only one per dungeon).
    for (auto& kv : _store)
    {
        auto& v = kv.second;
        std::sort(v.begin(), v.end(),
                  [](DungeonBossInfo const& a, DungeonBossInfo const& b)
                  { return a.encounterIndex < b.encounterIndex; });

        std::vector<DungeonBossInfo> deduped;
        deduped.reserve(v.size());
        for (auto const& info : v)
        {
            bool seen = false;
            for (auto const& kept : deduped)
            {
                if (kept.entry == info.entry)
                {
                    seen = true;
                    break;
                }
            }
            if (!seen)
                deduped.push_back(info);
        }
        v = std::move(deduped);
    }
}
