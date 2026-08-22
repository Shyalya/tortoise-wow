/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_LONGRANGEPATHFINDER_H
#define _PLAYERBOT_LONGRANGEPATHFINDER_H

#include <string>
#include <vector>

#include "Common.h"
#include "G3D/Vector3.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"  // PathSegment, ChunkedPathfinder::Result

class Player;
class dtNavMesh;

// LongRangePathfinder computes the WHOLE smoothed route to a target in one
// shot, bypassing the engine PathGenerator's 74-poly / 74-point caps that
// forced StridedPathfinder to chunk long routes into ~180yd strides.
//
// It runs Detour directly against the map's navmesh using its OWN
// dtNavMeshQuery initialised with a large search-node pool (the shared
// engine query is only 1024 nodes — far too small for a dungeon-length A*,
// and we must not perturb it since every creature's movement uses it). The
// poly corridor and smoothed point buffers are sized for an entire dungeon,
// so total distance no longer matters: the result is a single PathSegment
// whose polyline is the full smoothed corridor, which DungeonPathFollower
// already walks point-by-point.
//
// This is the PRIMARY producer behind StridedPathfinder::Build. When it can
// build a reachable route the caller returns it directly; when it can't
// (no navmesh, target off-mesh, immediate static obstruction) the caller
// falls through to the hardened bee-line / arc / spawn-graph stride tiers.
//
// SPLIT FOR ASYNC OFFLOAD (review point #1). The expensive, navmesh-only A* +
// smoothing is BuildCoreFromMesh(): it takes a dtNavMesh* and plain floats and
// touches NO live game state (no Player*, no Map*, no VMAP), so it is safe to
// run on a background worker thread. The cheap, VMAP/live-state post-processing
// (per-point Z-snap, corridor centering, line-of-sight screen, segment
// assembly) is Finalize(): it must run on the owning map thread. Build()
// composes the two for the synchronous path (config toggle OFF).
class LongRangePathfinder
{
public:
    // Same Result/PathSegment shape as the strided builder so the value
    // cache (DungeonClearLongPathValue) and follower stay unchanged.
    using Result = ChunkedPathfinder::Result;

    // Raw, navmesh-only output of the offloadable A*+smooth stage. It is a
    // pure value type (vector of plain floats + flags) with no pointers into
    // game state, so it is safe to move across a thread boundary. `rawPts`
    // is the smoothed corridor INCLUDING the leading start point (== snapped
    // bot position); Z is the navmesh getPolyHeight estimate, NOT yet refined
    // by Player::UpdateAllowedPositionZ (that happens in Finalize). The target
    // (tx,ty,tz) is carried so Finalize can compute completeness without the
    // caller re-supplying it.
    struct RawResult
    {
        bool reachable{false};
        bool startFarFromPoly{false};   // bot off the navmesh (no start poly)
        bool corridorComplete{false};   // last corridor poly == target poly
        float tx{0.0f}, ty{0.0f}, tz{0.0f};
        std::vector<G3D::Vector3> rawPts;
        std::string failureReason;
    };

    // WORKER-SAFE. Pure navmesh A* + smoothing against `mesh`, from
    // (sx,sy,sz) to (tx,ty,tz). `mapId` selects the route-cost discouragements
    // (any DcNavPenaltyRegistry no-go volumes on that map); it reads only the
    // registry table, no live game state. `mesh` must be kept
    // alive by the caller for the duration of the call (hold its shared_ptr). No
    // Player*/Map*/VMAP access — must not, and cannot, touch live game state.
    static RawResult BuildCoreFromMesh(dtNavMesh const* mesh, uint32 mapId,
                                       float sx, float sy, float sz,
                                       float tx, float ty, float tz);

    // MAP-THREAD ONLY. Turns a RawResult into the final route: refines each
    // point's Z (Player::UpdateAllowedPositionZ), centers the corridor off
    // walls (CorridorCenter, which reads VMAP), LOS-screens the line, drops
    // the leading start point, and assembles the single PathSegment. Same
    // Result contract as Build(). `bot` is dereferenced here, so call it from
    // within the bot's own AI tick.
    static Result Finalize(Player* bot, RawResult const& raw);

    // Synchronous convenience: BuildCoreFromMesh + Finalize in one call on the
    // current thread. Used by the synchronous code path (toggle OFF) and by
    // any caller that doesn't offload. Identical output to the pre-split Build.
    static Result Build(Player* bot, float tx, float ty, float tz);
};

#endif
