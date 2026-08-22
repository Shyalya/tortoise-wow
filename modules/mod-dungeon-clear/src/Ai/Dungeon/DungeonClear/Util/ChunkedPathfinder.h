/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_CHUNKEDPATHFINDER_H
#define _PLAYERBOT_CHUNKEDPATHFINDER_H

#include <string>
#include <vector>

#include "Common.h"
#include "G3D/Vector3.h"
#include "Ai/Dungeon/DungeonClear/Data/WaypointHint.h"

class Player;

// One leg of the planned route. Either an anchor lifted from a registered
// route (anchored = true, hint carries flags and door entry) or the
// truncated endpoint of a chained PathGenerator call (anchored = false).
//
// `arriveRadius` is the distance at which Advance considers the segment
// resolved and moves on to the next.
//
// `polyline` is the full smoothed-corridor output of the PathGenerator
// probe that produced this segment, with the probe's starting point
// dropped (it equals the previous segment's endpoint). For anchored
// segments it's a single point: the snapped anchor itself. The follower
// in Advance walks this point-by-point so each engine-side MoveTo stays
// short enough to never trip PATHFIND_SHORT.
struct PathSegment
{
    float ex{0.0f}, ey{0.0f}, ez{0.0f};
    float arriveRadius{6.0f};
    bool anchored{false};
    bool jumpDown{false};     // populated from anchor flag in phase 1; geometry heuristic added later
    bool jumpGap{false};
    uint32 doorGoEntry{0};
    std::vector<G3D::Vector3> polyline;
};

// Build() returns a chained navigable path from the bot's current position
// to a boss target. Designed around the fact that PathGenerator caps each
// call at ~296yd (74 polys × 4yd smoothing); for any longer route we have
// to chain calls or use registered anchors.
//
// IsReachable() is the cheap "do we have at least one segment of progress"
// gate used by the stall classifier and by tests that don't need the polyline.
class ChunkedPathfinder
{
public:
    struct Result
    {
        bool reachable{false};
        bool complete{false};            // true iff the last segment endpoint is within arriveRadius of the target
        bool startFarFromPoly{false};    // bot itself is off the navmesh — recovery candidate
        std::vector<PathSegment> segments;
        std::string failureReason;       // populated when reachable == false; empty otherwise
    };

    // Build the chained path. maxDepth caps chained PathGenerator calls
    // (strides). Default 16: when the direct boss probe keeps working each
    // stride covers up to ~292yd, but on a winding corridor the fallback tiers
    // take shorter reaches (down to STRIDE_LEN/4), so the budget needs headroom
    // to chain enough short chunks around the bends to reach a far boss.
    //
    // bossEntry is used only for route-registry lookup. Pass 0 to force
    // anchor-free chunking (useful for non-boss targets like fallback hostiles).
    static Result Build(Player* bot, uint32 mapId, uint32 bossEntry, float tx, float ty, float tz, uint32 maxDepth = 16);

    // Cheap reachability probe. Equivalent to Build(...).reachable but
    // exits as soon as the first segment is resolved successfully.
    // Used by the stall classifier so we don't pay for the full polyline.
    static bool IsReachable(Player* bot, float tx, float ty, float tz);
};

#endif
