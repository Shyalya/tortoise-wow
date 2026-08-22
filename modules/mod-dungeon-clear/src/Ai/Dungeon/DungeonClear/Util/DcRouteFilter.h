/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCROUTEFILTER_H
#define _PLAYERBOT_DCROUTEFILTER_H

#include "Define.h"
#include "DetourExtended.h"   // dtQueryFilterExt

// Route-cost filter for the dungeon-clear long-range pathfinder. Extends the
// engine's dtQueryFilterExt with the DcNavPenaltyRegistry no-go volumes: a hand-
// authored axis-aligned box over a known-bad spot (e.g. the LBRS chasm climb)
// multiplies the cost of any A* edge through it so heavily that the detour always
// wins — steering the corridor off a navmesh shortcut a real player can't follow.
//
// It is a COST, never a hard rejection (passFilter is untouched), so a boxed edge
// stays traversable: if it is genuinely the only way through, the route still
// takes it and the bot is never stranded. Outside a volume getCost is byte-for-
// byte the stock dtQueryFilterExt cost (the base call below), so on every map
// without a volume — i.e. almost all of them — routing is completely unchanged.
//
// A FENCE MUST NEVER TRAP SOMEONE ALREADY INSIDE IT. A no-go region is authored
// to keep a route from ENTERING a bad spot; it says nothing about a party that is
// already standing in one, and a party can be — the Hellfire Ramparts wall covers
// ~190 sq yd of ordinary room floor 11yd from where players zone in, so "start the
// run standing in the fence" is a routine occurrence, not a corner case. Taxing
// the way out by 40x is what turns that into the reported symptom: the detour
// round the far side of the room costs less than the four yards of fenced floor
// between the party and the rest of the instance, so the tank sets off the wrong
// way. So the ctor takes the route's START and, when that start is itself inside
// a region, this query's fence is switched off entirely — the party's only job
// from in there is to get out. The fence re-arms on the very next route build
// (Advance rebuilds every tick), by which point the start is outside it again.
//
// (A general steep-slope penalty was prototyped here and removed: Detour measures
// slope portal-midpoint to portal-midpoint, not along the walkable surface, so it
// misfired on legitimate staircases; and the mmap generator's own walkable limit
// is 60°, which overlaps the ~50° shortcut slope, so slope can't cleanly separate
// "bad shortcut" from "legit steep ramp". The surgical box is the reliable lever.)
//
// One instance per Build (cheap: one registry membership check in the ctor).
// getAreaCost / include/exclude flags are inherited unchanged, so callers still
// apply the liquid-avoidance area costs (DungeonClearGeometry::ApplyLiquidAreaCosts)
// on top.
class DcRouteFilter : public dtQueryFilterExt
{
public:
    // `startX/Y/Z` is where the route begins (the bot's position). Passing it is
    // mandatory: it is what lets a party that is standing inside a fence route
    // out of one. See the "must never trap" note above.
    DcRouteFilter(uint32 mapId, float startX, float startY, float startZ);

    float getCost(float const* pa, float const* pb,
        dtPolyRef prevRef, dtMeshTile const* prevTile, dtPoly const* prevPoly,
        dtPolyRef curRef, dtMeshTile const* curTile, dtPoly const* curPoly,
        dtPolyRef nextRef, dtMeshTile const* nextTile, dtPoly const* nextPoly) const override;

    // Whether the no-go rows apply to this query at all: false on a map with no
    // rows, and false when the route starts inside one. Exposed so the "a fence
    // must not cage the party standing in it" wiring is unit-testable without a
    // navmesh (getCost needs live dtMeshTile/dtPoly).
    bool IsFenceActive() const { return _fenceActive; }

private:
    uint32 _mapId;
    // Map has a no-go region AND the route does not start inside one — i.e. the
    // fence applies to this query. Also the per-edge region-test gate, so a map
    // with no rows pays nothing.
    bool   _fenceActive;
};

#endif
