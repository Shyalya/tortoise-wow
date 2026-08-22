/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonSpawnGraph.h"

#include <algorithm>
#include <cmath>

#include "CreatureData.h"
#include "CreatureSpawnEntry.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "ObjectMgr.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"

bool DungeonSpawnGraph::_built = false;

std::unordered_map<uint32, std::vector<SpawnNode>>& DungeonSpawnGraph::Store()
{
    static std::unordered_map<uint32, std::vector<SpawnNode>> s;
    return s;
}

void DungeonSpawnGraph::Build()
{
    if (_built)
        return;
    _built = true;

    auto& store = Store();
    store.clear();

    // Spawn data is indexed by spawn id, not by map. Walk once, partition by
    // dungeon mapId. Non-dungeon maps (overworld, BGs, arenas) are skipped —
    // dungeon clear is dungeon-only and the spawn graph is not consulted
    // anywhere else.
    CreatureDataMap const& spawns = sObjectMgr.GetAllCreatureData();
    for (auto const& kv : spawns)
    {
        CreatureData const& data = kv.second;
        uint32 const entry = DungeonClear::SpawnEntry(data);
        if (!entry)
            continue;

        MapEntry const* mapEntry = sMapStore.LookupEntry(data.position.mapId);
        if (!mapEntry || !mapEntry->IsNonRaidDungeon())
            continue;

        SpawnNode node;
        node.x = data.position.x;
        node.y = data.position.y;
        node.z = data.position.z;
        node.entry = entry;
        store[data.position.mapId].push_back(node);
    }

    // Cap pathological dungeons (CoT Strat / Old Hillsbrad have thousands of
    // entries) at a few hundred candidates to keep FindCorridor's per-call
    // cost bounded. We don't need every spawn — we just need enough to seed
    // the corridor.
    constexpr size_t MAX_NODES_PER_MAP = 800;
    for (auto& mapKv : store)
    {
        if (mapKv.second.size() > MAX_NODES_PER_MAP)
            mapKv.second.resize(MAX_NODES_PER_MAP);
    }
}

namespace
{
    // 2D perpendicular distance from (px, py) to the line through (ax, ay)
    // and (bx, by). Returns both the perpendicular distance and the
    // parametric position `t` along the segment.
    struct Projection
    {
        float distance2D;   // perpendicular distance to the infinite line
        float t;            // parametric position; 0 at A, 1 at B
    };

    Projection ProjectOntoLine2D(float px, float py, float ax, float ay, float bx, float by)
    {
        Projection p{0.0f, 0.0f};
        float const ex = bx - ax;
        float const ey = by - ay;
        float const len2 = ex * ex + ey * ey;
        if (len2 <= 1e-6f)
        {
            float const dx = px - ax;
            float const dy = py - ay;
            p.distance2D = std::sqrt(dx * dx + dy * dy);
            p.t = 0.0f;
            return p;
        }
        p.t = ((px - ax) * ex + (py - ay) * ey) / len2;
        float const cx = ax + p.t * ex;
        float const cy = ay + p.t * ey;
        float const dx = px - cx;
        float const dy = py - cy;
        p.distance2D = std::sqrt(dx * dx + dy * dy);
        return p;
    }
}

std::vector<SpawnNode> DungeonSpawnGraph::FindCorridor(Map const* map, uint32 mapId,
                                                      float fx, float fy, float fz,
                                                      float tx, float ty, float tz,
                                                      float corridorRadius)
{
    (void)fz;  // z used only for snap below; horizontal projection is the filter
    (void)tz;

    Build();  // Lazy-init on first lookup, same pattern as BossSpawnIndex.

    auto& store = Store();
    auto it = store.find(mapId);
    if (it == store.end())
        return {};

    std::vector<std::pair<float, SpawnNode>> candidates;  // (t, node)
    candidates.reserve(64);

    for (SpawnNode const& node : it->second)
    {
        Projection const p = ProjectOntoLine2D(node.x, node.y, fx, fy, tx, ty);
        // Filter: must be inside the corridor and strictly between the
        // endpoints. The endpoint exclusions drop nodes at the bot's foot
        // (would emit a useless self-anchor) and nodes inside the boss room
        // (would loop the tank through trash on top of the boss).
        if (p.distance2D > corridorRadius)
            continue;
        if (p.t <= 0.05f || p.t >= 0.95f)
            continue;

        SpawnNode snapped = node;
        // Best-effort snap. Spawn coords are usually on-mesh already, but
        // some are placed ~0.5yd above the floor.
        if (map)
        {
            NavmeshSnap::Result const r = NavmeshSnap::Snap(map, node.x, node.y, node.z, /*maxRadius*/ 8.0f);
            if (r.ok)
            {
                snapped.x = r.x;
                snapped.y = r.y;
                snapped.z = r.z;
            }
        }
        candidates.emplace_back(p.t, snapped);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](auto const& a, auto const& b) { return a.first < b.first; });

    // Coalesce adjacent candidates: spawns frequently cluster in packs, so
    // anchoring on every node would emit 6-10 anchors within a few yards.
    // Keep one node per `kSpacing` of parametric progress.
    constexpr float kSpacing = 0.05f;  // 5% of the segment length
    std::vector<SpawnNode> result;
    result.reserve(candidates.size());
    float lastT = -1.0f;
    for (auto const& kv : candidates)
    {
        if (kv.first - lastT < kSpacing)
            continue;
        result.push_back(kv.second);
        lastT = kv.first;
    }
    return result;
}
