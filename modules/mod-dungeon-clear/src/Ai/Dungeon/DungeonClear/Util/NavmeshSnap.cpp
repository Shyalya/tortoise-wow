/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "NavmeshSnap.h"

#include <cmath>

#include "DetourExtended.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "Map.h"
#include "MapCollisionData.h"
#include "MapDefines.h"
#include "Player.h"

namespace
{
    constexpr float SNAP_VERT_EXTENT = 10.0f;
}

NavmeshSnap::Result NavmeshSnap::Snap(Map const* map, float x, float y, float z, float maxRadius)
{
    Result result;
    if (!map)
        return result;

    // GetMMapData()/GetNavMeshQuery() is non-const because the query is lazily
    // built on first access; const_cast is the same pattern PathGenerator uses
    // when constructed from a const WorldObject. The query itself is then
    // accessed read-only.
    // By value: this core hands out a snapshot handle, not a live member.
    Map::MapCollisionData coll = map->GetMapCollisionData();
    dtNavMesh const* navMesh = coll.GetMMapData().GetNavMesh();
    dtNavMeshQuery const* query = coll.GetMMapData().GetNavMeshQuery();
    if (!navMesh || !query)
        return result;

    // Simple ground/water filter — matches the player branch of
    // PathGenerator::CreateFilter so we snap to polys a player can stand on.
    dtQueryFilterExt filter;
    filter.setIncludeFlags(NAV_GROUND | NAV_WATER | NAV_MAGMA);
    filter.setExcludeFlags(0);

    // World (x, y, z) maps to detour (y, z, x).
    float const point[3] = { y, z, x };

    // Two passes: requested radius, then double. Two attempts is enough — if
    // a point is more than 60yd from any walkable poly it's truly off-mesh.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        float const radius = (attempt == 0) ? maxRadius : maxRadius * 2.0f;
        float const extents[3] = { radius, SNAP_VERT_EXTENT, radius };
        dtPolyRef polyRef = 0;
        float closest[3] = { 0.0f, 0.0f, 0.0f };
        if (dtStatusSucceed(query->findNearestPoly(point, extents, &filter, &polyRef, closest)) &&
            polyRef != 0)
        {
            result.ok = true;
            result.x = closest[2];
            result.y = closest[0];
            result.z = closest[1];
            float const dx = result.x - x;
            float const dy = result.y - y;
            float const dz = result.z - z;
            result.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            return result;
        }
    }

    return result;
}

NavmeshSnap::Result NavmeshSnap::Snap(Player const* bot, float x, float y, float z, float maxRadius)
{
    if (!bot)
        return {};
    return Snap(bot->FindMap(), x, y, z, maxRadius);
}
