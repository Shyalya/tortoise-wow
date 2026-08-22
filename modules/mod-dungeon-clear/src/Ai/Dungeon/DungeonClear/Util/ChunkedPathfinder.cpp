/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "ChunkedPathfinder.h"

#include "Player.h"
#include "Ai/Dungeon/DungeonClear/Util/StridedPathfinder.h"

// ChunkedPathfinder is now a thin compatibility shim. The v1 chunked
// build-from-target-back algorithm had a fatal bug — it accepted
// PATHFIND_SHORT results as complete chunks, but those results are
// straight-line shortcuts through geometry, not real corridors. The work
// happens in StridedPathfinder; we keep the old class name so the value
// cache, downstream Result references, and the route-registry signatures
// don't have to churn.

ChunkedPathfinder::Result ChunkedPathfinder::Build(Player* bot, uint32 mapId, uint32 bossEntry, float tx, float ty,
                                                   float tz, uint32 maxDepth)
{
    return StridedPathfinder::Build(bot, mapId, bossEntry, tx, ty, tz, maxDepth);
}

bool ChunkedPathfinder::IsReachable(Player* bot, float tx, float ty, float tz)
{
    return StridedPathfinder::IsReachable(bot, tx, ty, tz);
}
