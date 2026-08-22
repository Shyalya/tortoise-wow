/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRouteFilter.h"

#include "Ai/Dungeon/DungeonClear/Data/DcNavPenaltyRegistry.h"

DcRouteFilter::DcRouteFilter(uint32 mapId, float startX, float startY, float startZ)
    : _mapId(mapId)
    // The fence is live only when the map has rows AND the route does not begin
    // inside one of them. Starting inside is the "party zoned in on the wrong side
    // of the wall" case: from in there the fence has nothing left to protect (the
    // bad spot is already underfoot) and taxing the way out at 40x is what buys
    // the absurd detour instead. Dropping it for this one query is safe because it
    // is re-evaluated on every rebuild — one step outside and the fence is back.
    , _fenceActive(DcNavPenaltyRegistry::HasVolumes(mapId)
                   && !DcNavPenaltyRegistry::IsInsideRegion(mapId, startX, startY, startZ))
{
}

float DcRouteFilter::getCost(float const* pa, float const* pb,
    dtPolyRef prevRef, dtMeshTile const* prevTile, dtPoly const* prevPoly,
    dtPolyRef curRef, dtMeshTile const* curTile, dtPoly const* curPoly,
    dtPolyRef nextRef, dtMeshTile const* nextTile, dtPoly const* nextPoly) const
{
    // Stock cost (edge length × slope term × area cost). Reused verbatim so a map
    // without a no-go volume routes exactly as before this filter existed.
    float const base = dtQueryFilterExt::getCost(pa, pb,
        prevRef, prevTile, prevPoly, curRef, curTile, curPoly, nextRef, nextTile, nextPoly);

    if (!_fenceActive)
        return base;

    // Edge midpoint back in world space (undo Detour's {y, z, x} vertex order),
    // then multiply by the no-go penalty for that point (1.0 outside every box).
    float const wx = (pa[2] + pb[2]) * 0.5f;
    float const wy = (pa[0] + pb[0]) * 0.5f;
    float const wz = (pa[1] + pb[1]) * 0.5f;
    return base * DcNavPenaltyRegistry::PenaltyAt(_mapId, wx, wy, wz);
}
