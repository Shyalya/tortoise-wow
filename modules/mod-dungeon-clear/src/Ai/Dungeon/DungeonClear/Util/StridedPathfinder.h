/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_STRIDEDPATHFINDER_H
#define _PLAYERBOT_STRIDEDPATHFINDER_H

#include <string>
#include <vector>

#include "Common.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"  // PathSegment, ChunkedPathfinder::Result

class Player;

// StridedPathfinder replaces the v1 ChunkedPathfinder::Build with a
// bee-line stride approach. Each stride asks PathGenerator for a path
// shorter than its safe-output envelope (~180yd vs. PathGenerator's ~296yd
// internal cap), so PATHFIND_SHORT — which the v1 builder silently treated
// as a complete chunk — never occurs. The result format matches the v1
// builder so downstream callers (Advance, blocking-trash, door scan)
// don't change.
//
// Algorithm:
//   1. If a hand-tuned anchor route is registered for the boss, use it.
//   2. Otherwise, every stride, probe straight at the boss target. Detour
//      follows the real corridor around bends and returns either a full
//      path (NORMAL) or a valid partial (INCOMPLETE, truncated at its
//      74-poly cap); we chunk along that real corridor, advancing to the
//      partial's end and re-probing the boss from there next stride. The
//      smoothed corridor is LOS-screened: isolated corner grazes on sharp
//      bends are tolerated, but a sustained wall-crossing truncates the
//      chunk to its verified prefix (re-probed from the clean end).
//   3. Only when the direct probe can't return a usable result (NOPATH on a
//      corridor longer than the ~292yd smoothing envelope, PATHFIND_SHORT in
//      a big open room, or an LOS reject right at the bot) fall back to a
//      bee-line step at decreasing reaches, then lateral arcs ±30°/±60° at
//      decreasing reaches, then the dungeon spawn-graph corridor. Shorter
//      reaches keep the probe point on the near arm of a bend.
//   4. Anchor-free build caps at maxStrides probes.
class StridedPathfinder
{
public:
    // Same Result/PathSegment types as v1 to keep the value cache stable.
    using Result = ChunkedPathfinder::Result;

    // Build a chained path from the bot's current position to the boss
    // target. Caller (Advance) walks each segment sequentially.
    //
    // skipLongRange skips the LongRangePathfinder primary tier (anchor route +
    // strided fallback only). The async path uses this on the map thread after
    // the offloaded LongRangePathfinder build already came back unreachable, so
    // we don't redo that heavy A* synchronously just to reach the fallback.
    static Result Build(Player* bot, uint32 mapId, uint32 bossEntry, float tx, float ty, float tz,
                        uint32 maxStrides = 16, bool skipLongRange = false);

    // Cheap reachability check. Equivalent to Build(...).reachable but
    // exits as soon as one usable segment is produced. Used by the stall
    // classifier and FindNearestReachableHostile.
    static bool IsReachable(Player* bot, float tx, float ty, float tz);
};

#endif
