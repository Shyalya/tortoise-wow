/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_NAVMESHSNAP_H
#define _PLAYERBOT_NAVMESHSNAP_H

#include "Common.h"

class Map;
class Player;

// Snap an arbitrary world coordinate to the nearest walkable polygon on a
// given mmap. Wraps the same dtNavMeshQuery::findNearestPoly call
// PathGenerator uses internally — gives us on-mesh anchors for inputs that
// might otherwise trigger PATHFIND_FARFROMPOLY_END (off-mesh boss DBC
// coordinates, hand-typed waypoint anchors, etc.).
class NavmeshSnap
{
public:
    struct Result
    {
        bool ok{false};
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        float distance{0.0f};   // 3D distance from the requested point to the snapped point
    };

    // Snap (x, y, z) to the nearest walkable polygon on `map`. Searches a
    // horizontal box of `maxRadius` yards and a vertical extent of 10yd.
    // One retry at 2× radius if the first search misses. Returns ok=false
    // if both attempts fail.
    static Result Snap(Map const* map, float x, float y, float z, float maxRadius = 30.0f);

    // Convenience overload — uses the bot's current map.
    static Result Snap(Player const* bot, float x, float y, float z, float maxRadius = 30.0f);

    // Column variant: small horizontal box, TALL vertical extent. For a
    // point parked far above (or below) the mesh - live, a bot grounded on
    // the OVERWORLD height 150y over the Deadmines floor - where the normal
    // 10yd vertical search can never see the real ground.
    static Result SnapColumn(Map const* map, float x, float y, float z,
                             float halfHeight = 250.0f, float radius = 6.0f);
};

#endif
