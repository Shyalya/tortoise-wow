/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_NAV_HARNESS_H
#define _DC_NAV_HARNESS_H

#include <memory>
#include <string>

#include "G3D/Vector3.h"

class dtNavMesh;

// Tier-2 standalone navmesh routing harness (headless-sim plan). Loads a map's
// Detour navmesh straight from a sliced mmaps directory (no worldserver, no DB,
// no creatures, no live Player/Map) and runs the DC long-range A* against it via
// LongRangePathfinder::BuildCoreFromMesh — which is explicitly worker-safe: it
// takes a raw dtNavMesh* and floats and touches NO live game state. That is what
// makes the routing half of DC testable offline: the geometry bug class
// (ledge/under-map/island/reachability) is a pure function of navmesh + coords.
//
// Map data is client-derived and NEVER committed; the slice lives under a
// gitignored mapdata dir produced by tools/slice_mapdata.py. When the slice for
// a map is absent, LoadMap returns nullptr and the test GTEST_SKIPs.
//
// NOT covered here: closed-door GO-LOS (the navmesh is baked door-blind, and a
// shut door is a runtime GameObject, not static geometry) — that needs a live
// GameObject and belongs to the live smoke tier, out of scope for this harness.
namespace DcNavHarness
{
    // Load every .mmtile present for `mapId` under `{mapDataDir}/mmaps/` into one
    // dtNavMesh (params from {mapId:03}.mmap). Returns nullptr if the .mmap is
    // missing (slice absent) or no tile loads. The shared_ptr owns the mesh.
    std::shared_ptr<dtNavMesh> LoadMap(std::string const& mapDataDir, uint32_t mapId);

    // One routing outcome — the worker-safe core of LongRangePathfinder, with the
    // derived route metrics the scenarios assert on. Mirrors
    // LongRangePathfinder::RawResult plus the geometry summaries.
    struct RouteResult
    {
        bool  built          = false;  // a mesh was present and the query ran
        bool  reachable      = false;
        bool  corridorComplete = false;  // route's last poly == target poly
        bool  startFarFromPoly = false;
        uint32_t pointCount   = 0;
        float routeLength2d  = 0.0f;   // summed 2D segment length of the polyline
        float maxStepZ       = 0.0f;   // largest |dz| between consecutive points
        std::string failureReason;
    };

    // Run BuildCoreFromMesh from (sx,sy,sz) to (tx,ty,tz) against `mesh` and
    // summarize. `mapId` selects the route-cost discouragements. `mesh` must
    // outlive the call.
    RouteResult Route(dtNavMesh const* mesh, uint32_t mapId,
                      float sx, float sy, float sz,
                      float tx, float ty, float tz);

    // Snap a WoW-space point to the nearest navmesh poly, using the same
    // {y,z,x} Detour coordinate order as BuildCoreFromMesh. `out` receives the
    // snapped WoW-space stand position. Returns false if no poly lies within
    // (hExtent, vExtent, hExtent) of the point — i.e. the coordinate is off the
    // navmesh. Used to validate/correct authored anchor + teleport coordinates
    // against the real mesh.
    //
    // `includeFlags` is the Detour include mask, default "any navigable poly".
    // Passing NAV_GROUND alone turns this into a LIQUID TEST: the mmap generator
    // meshes water at the liquid SURFACE and stamps those polys NAV_WATER, so a
    // point standing on water snaps to itself with 0xffff and jumps to the pool
    // floor (or misses entirely) with NAV_GROUND. That is the check that catches
    // a hand-authored route wading into a lake — see TestAzjolNerubRouteProbe.
    bool NearestPoint(dtNavMesh const* mesh,
                      float x, float y, float z,
                      float hExtent, float vExtent, G3D::Vector3& out,
                      unsigned short includeFlags = 0xffff);
}

#endif  // _DC_NAV_HARNESS_H
