/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_CORRIDORCENTER_H
#define _PLAYERBOT_CORRIDORCENTER_H

#include <vector>

#include "G3D/Vector3.h"

class Player;
class dtNavMeshQuery;
class dtQueryFilter;

// CorridorCenter nudges a smoothed navmesh polyline OFF the walls toward the
// middle of the corridor it runs through.
//
// Why this exists: every route the dungeon-clear pathfinders produce is built
// by Detour's funnel + moveAlongSurface smoothing, which by construction
// returns the taut "shortest string" through the navmesh corridor — it hugs
// the inside of every bend and rides the navmesh boundary. Because that
// boundary IS the wall edge / ledge edge / decoration cutout, the bot walks
// flush against walls, grazes ledges (falls), and clips wall-lining props.
// Detour has no "center me" mode; centering has to be a post-process.
//
// The primitive that makes it cheap is dtNavMeshQuery::findDistanceToWall:
// for any point it returns the distance to the nearest navmesh boundary AND a
// normal pointing away from it. We push each interior point along that normal
// until it has `clearance` yards of room. In a corridor narrower than
// 2*clearance the one-sided push would overshoot toward the far wall, so a
// second findDistanceToWall on the pushed point detects the opposite wall and
// the push is reduced to the true midpoint (medial axis). In open rooms the
// nearest wall is beyond the search radius, so nothing moves — correct.
//
// Every pushed point is re-validated on-mesh (findNearestPoly) and re-pinned
// to a walkable Z (Player::UpdateAllowedPositionZ); a push that would leave
// the mesh is reverted. The producers run their existing LOS screen AFTER
// centering, so any nudge that would cross static geometry is still caught and
// the corridor truncated to its verified prefix exactly as before.
//
// The per-point push is independent, and the "nearest wall" can flip sides
// between adjacent points, leaving a sawtooth the spline follower renders as
// stutter. So after pushing we run a few Laplacian smoothing passes
// (neighbour-averaging with endpoints pinned and every result re-validated
// on-mesh) to pull the kinks back out into a flowing line.
//
// Main-thread only (reads the static navmesh, touches no live game state).
class CorridorCenter
{
public:
    // Tunables resolved from config (DungeonClear.PathCenter*). Passed by the
    // caller so the live-config lookup happens once per route, not per point.
    struct Params
    {
        bool  enable{true};
        float clearance{3.0f};   // target yards from the nearest wall
        float maxPush{3.0f};     // cap on per-point displacement
        int   smoothIters{2};    // Laplacian passes to de-kink after centering
    };

    // Read the live config (honours `.reload config`) into a Params.
    static Params LoadParams();

    // Center `pts` in place using an ALREADY-OPEN query/filter (the primary
    // producer's big-pool query). pts[0] (start) and pts.back() (target) are
    // never moved. No-op when params.enable is false or pts has < 3 points.
    static void Center(dtNavMeshQuery const* query, dtQueryFilter const* filter,
                       Player* bot, std::vector<G3D::Vector3>& pts, Params const& params);

    // Convenience overload for callers without a query (the strided fallback,
    // whose points come from the engine PathGenerator). Allocates a modest
    // Detour query against the bot's map navmesh for the duration of the call.
    // No-op when params.enable is false, pts has < 3 points, or no navmesh.
    static void Center(Player* bot, std::vector<G3D::Vector3>& pts, Params const& params);
};

#endif
