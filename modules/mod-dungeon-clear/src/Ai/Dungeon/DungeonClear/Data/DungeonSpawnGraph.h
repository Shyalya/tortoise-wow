/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONSPAWNGRAPH_H
#define _PLAYERBOT_DUNGEONSPAWNGRAPH_H

#include <unordered_map>
#include <vector>

#include "Common.h"

class Map;

// A simple set of known on-mesh "waypoint candidates" for each dungeon map,
// derived from where the dungeon's creature spawns sit. Used by
// StridedPathfinder as a fallback when straight bee-line strides can't
// produce a navigable corridor — every creature spawn is, by construction,
// somewhere a player can stand, so projecting a few of them onto the
// bot→boss line gives us an "implicit anchor route" without any
// hand-tuned per-dungeon data.
struct SpawnNode
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    uint32 entry{0};
};

class DungeonSpawnGraph
{
public:
    // Returns a sorted-by-progress sequence of spawn nodes that lie within
    // `corridorRadius` yards of the line from (fx,fy,fz) to (tx,ty,tz),
    // strictly between the endpoints (parametric 0.05..0.95). Empty if no
    // spawns in range or if the dungeon hasn't been indexed.
    //
    // `bot`'s map is consulted only to attempt an on-mesh snap of returned
    // points; the index itself is map-global and pre-snapped at startup.
    static std::vector<SpawnNode> FindCorridor(Map const* map, uint32 mapId,
                                               float fx, float fy, float fz,
                                               float tx, float ty, float tz,
                                               float corridorRadius = 25.0f);

    // Build the per-dungeon spawn graph once. Called from DungeonClearLoader's
    // WorldScript::OnStartup. Walks sObjectMgr.GetAllCreatureData(), filters
    // to dungeon maps, attempts a navmesh snap (best-effort — many maps
    // aren't loaded yet at startup, so snap may fail and we fall back to the
    // raw spawn coord). Subsequent calls are no-ops.
    static void Build();

private:
    static std::unordered_map<uint32, std::vector<SpawnNode>>& Store();
    static bool _built;
};

#endif
