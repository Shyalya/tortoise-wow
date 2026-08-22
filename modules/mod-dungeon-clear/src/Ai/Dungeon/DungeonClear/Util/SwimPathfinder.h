/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_SWIMPATHFINDER_H
#define _PLAYERBOT_SWIMPATHFINDER_H

#include <string>
#include <vector>

#include "G3D/Vector3.h"

class Player;

// Tier A "swim leg" pathfinder: a greedy 3D navigator through a water volume
// that needs NO navmesh and NO per-zone data.
//
// Why this exists: the mmap generator discards the floor under liquid and keeps
// only a flat water-surface navmesh sheet (TerrainBuilder "pick the higher
// one"), so there is no navmesh inside a submerged tunnel. A submerged area
// behind that tunnel (e.g. Blackfathom Deeps -> Lady Sarevess) is therefore a
// disconnected mesh island the normal route planner declares unreachable.
//
// Build() runs only when the navmesh planner has already given up AND water lies
// between the bot and the goal (see WaterBetween). It walks toward the goal in
// 3D; when a straight hop is blocked by geometry it probes a fan of yaw/pitch
// deflections and takes the clearest one that still makes progress and stays in
// water — a bug-algorithm tuned for tubular flooded corridors. Collision is
// tested with VMAP line-of-sight (liquid is transparent to VMAP, so only rock /
// WMO geometry blocks); "in water" is tested with the map's liquid status.
//
// The produced polyline carries SUBMERGED Z verbatim — callers must NOT pass it
// through UpdateAllowedPositionZ / NavmeshSnap / corridor centering (those are
// navmesh-bound and would yank the points back up to the surface sheet, which is
// the whole bug). Drive it with a raw 3D escort spline.
//
// This is the cheap primary tier; a bounded 3D lattice A* (Tier B) is the
// designed fallback for concave traps the greedy probe can't see around, and is
// not implemented here.
namespace SwimPathfinder
{
    struct Result
    {
        bool ok{false};
        std::vector<G3D::Vector3> points;   // 3D polyline toward the goal, EXCLUDING the start point
        std::string failureReason;          // populated when ok == false
    };

    // Greedy 3D swim route from `start` to `goal`. Both are world coords on the
    // bot's map. On success `points` ends at (or within arrive-radius of) the
    // goal. Synchronous and map-thread only (touches VMAP + liquid).
    Result Build(Player* bot, G3D::Vector3 const& start, G3D::Vector3 const& goal);

    // Cheap gate: is there any water on the straight line between a and b?
    // Sampled, so it can miss a thin sliver, but a submerged tunnel is never
    // thin along its length. Used to confirm a dead-end is a swim candidate
    // before paying for Build().
    bool WaterBetween(Player* bot, G3D::Vector3 const& a, G3D::Vector3 const& b);
}

#endif
