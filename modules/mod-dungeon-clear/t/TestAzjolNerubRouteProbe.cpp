/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Liquid probe for the hand-authored Azjol-Nerub (map 601) route to Anub'arak.
//
// The first 145yd of the lower kingdom are a lake: the mmap generator meshes the
// liquid SURFACE and stamps those polys NAV_WATER, so a route can sit perfectly
// on the navmesh and still have the party swimming the whole way. The stock
// pathfinder prices that in (DungeonClearGeometry::ApplyLiquidAreaCosts charges
// NAV_WATER edges DungeonClear.WaterPathCost), but the anchor fast-path in
// StridedPathfinder::Build bypasses the filter entirely — an authored route has
// to be dry by construction. The first cut of this route was not, and the party
// swam ~80yd diagonally across the drop chamber (tp-20260818-200553-1).
//
// The test: snap every anchor twice, once permissively and once with NAV_GROUND
// alone. On dry ground the two agree; on water the ground-only snap drops to the
// pool floor 8-12yd below (or misses), so the disagreement IS the liquid test.
//
// Not a committed regression: reads the FULL (unsliced) mmaps dir from env
// DC_PROBE_MMAPS and GTEST_SKIPs when unset, same contract as the Mechanar and
// Ramparts probes.
//
//   DC_PROBE_MMAPS=/home/jared/azerothcore/env/dist/bin \
//     ./dungeon_clear_tests --gtest_filter='AzjolNerubRouteProbe.*'

#include "gtest/gtest.h"
#include "NavHarness.h"

#include "MapDefines.h"

#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t MAP_AZJOL_NERUB = 601;
    constexpr uint32 AN_ANUBARAK = 29120;

    // Anub'arak's spawn — StridedPathfinder appends this as the goal segment, so
    // it is the far end of the last authored leg.
    constexpr float ANUBARAK[3] = { 551.0f, 248.3f, 224.0f };

    // The drop landing (AzjolNerubEvents.cpp AN_DROP_LANDING_*). Deliberately
    // south of the lake, on dry ground.
    constexpr float LANDING[3] = { 544.18f, 481.26f, 288.98f };

    // Control: the middle of the drop chamber, directly under the hole. This is
    // where the landing used to be and it is unambiguously water, so it is what
    // proves the probe is still measuring liquid at all.
    constexpr float UNDER_THE_HOLE[3] = { 536.0f, 549.5f, 288.7f };

    // How far the ground-only snap may move a point before we call it wet. The
    // pool floor is 8-12yd under the surface, so anything over a couple of yards
    // is unambiguous; 2.0 leaves room for ordinary mesh/detail float.
    constexpr float DRY_TOLERANCE = 2.0f;

    // Snap box. Horizontal stays tight so a miss means "not here", not "found
    // something across the room"; vertical is deep enough to reach the pool floor
    // from the surface, which is what makes a wet anchor visibly move.
    constexpr float SNAP_H = 4.0f;
    constexpr float SNAP_V = 15.0f;

    // Returns the vertical drop from the permissive snap to the NAV_GROUND-only
    // snap, or a large sentinel when no ground poly is in range at all. Both
    // cases mean the point is standing on liquid.
    float GroundDrop(dtNavMesh const* mesh, float x, float y, float z)
    {
        G3D::Vector3 any;
        if (!DcNavHarness::NearestPoint(mesh, x, y, z, SNAP_H, SNAP_V, any))
            return 1e9f;  // off the navmesh entirely — caller reports it

        G3D::Vector3 ground;
        if (!DcNavHarness::NearestPoint(mesh, x, y, z, SNAP_H, SNAP_V, ground, NAV_GROUND))
            return 1e9f;

        return std::fabs(any.z - ground.z);
    }
}

TEST(AzjolNerubRouteProbe, EveryAnchorStandsOnDryGround)
{
    char const* dir = std::getenv("DC_PROBE_MMAPS");
    if (!dir || !*dir)
        GTEST_SKIP() << "set DC_PROBE_MMAPS to a dir containing mmaps/ for map 601";

    std::shared_ptr<dtNavMesh> mesh = DcNavHarness::LoadMap(dir, MAP_AZJOL_NERUB);
    if (!mesh)
        GTEST_SKIP() << "no map-601 navmesh under " << dir << "/mmaps";

    std::vector<WaypointHint> const* route =
        DungeonClearRouteRegistry::Get(MAP_AZJOL_NERUB, DUNGEON_DIFFICULTY_NORMAL, AN_ANUBARAK);
    ASSERT_NE(route, nullptr) << "no authored route to Anub'arak on map 601";
    ASSERT_FALSE(route->empty());

    std::printf("=== Azjol-Nerub (601) route liquid probe ===\n");

    // Control FIRST: under the hole MUST read wet, otherwise the probe is not
    // measuring liquid and every assertion below is worthless.
    float const holeDrop =
        GroundDrop(mesh.get(), UNDER_THE_HOLE[0], UNDER_THE_HOLE[1], UNDER_THE_HOLE[2]);
    std::printf("  [control] under the hole (%.2f, %.2f, %.2f)  ground-drop %.2f\n",
                UNDER_THE_HOLE[0], UNDER_THE_HOLE[1], UNDER_THE_HOLE[2], holeDrop);
    ASSERT_GT(holeDrop, DRY_TOLERANCE)
        << "the drop chamber reads as dry ground — either the mesh changed or this "
           "probe is no longer measuring liquid";

    // The landing itself is the first thing that has to be dry: the whole point
    // of moving it 68yd south of the hole was to stop the party swimming.
    float const landingDrop = GroundDrop(mesh.get(), LANDING[0], LANDING[1], LANDING[2]);
    std::printf("  [landing] (%.2f, %.2f, %.2f)  ground-drop %.2f\n",
                LANDING[0], LANDING[1], LANDING[2], landingDrop);
    EXPECT_LT(landingDrop, DRY_TOLERANCE)
        << "the drop landing is back in the lake — the party swims from the moment "
           "it arrives";

    for (size_t i = 0; i < route->size(); ++i)
    {
        WaypointHint const& h = (*route)[i];
        float const drop = GroundDrop(mesh.get(), h.x, h.y, h.z);
        std::printf("  [anchor %2zu] (%.2f, %.2f, %.2f)  ground-drop %.2f\n",
                    i + 1, h.x, h.y, h.z, drop);
        EXPECT_LT(drop, DRY_TOLERANCE)
            << "anchor " << (i + 1) << " at (" << h.x << ", " << h.y << ", " << h.z
            << ") stands on NAV_WATER — the party swims this leg instead of walking it";
    }

    std::printf("===========================================\n");
}

TEST(AzjolNerubRouteProbe, TheLandingPathsOnToTheBoss)
{
    char const* dir = std::getenv("DC_PROBE_MMAPS");
    if (!dir || !*dir)
        GTEST_SKIP() << "set DC_PROBE_MMAPS to a dir containing mmaps/ for map 601";

    std::shared_ptr<dtNavMesh> mesh = DcNavHarness::LoadMap(dir, MAP_AZJOL_NERUB);
    if (!mesh)
        GTEST_SKIP() << "no map-601 navmesh under " << dir << "/mmaps";

    std::vector<WaypointHint> const* route =
        DungeonClearRouteRegistry::Get(MAP_AZJOL_NERUB, DUNGEON_DIFFICULTY_NORMAL, AN_ANUBARAK);
    ASSERT_NE(route, nullptr);
    ASSERT_FALSE(route->empty());

    // The teleport has to put the party somewhere that can actually go on. A
    // landing that snaps off-mesh, or onto a shelf the boss is not reachable
    // from, strands the run with no way back up the shaft.
    G3D::Vector3 snapped;
    ASSERT_TRUE(DcNavHarness::NearestPoint(mesh.get(), LANDING[0], LANDING[1], LANDING[2],
                                           4.0f, 4.0f, snapped))
        << "the drop landing is off the navmesh";
    std::printf("  [snap] landing -> (%.2f, %.2f, %.2f)  dz=%+.2f\n",
                snapped.x, snapped.y, snapped.z, snapped.z - LANDING[2]);

    DcNavHarness::RouteResult const r = DcNavHarness::Route(
        mesh.get(), MAP_AZJOL_NERUB, LANDING[0], LANDING[1], LANDING[2],
        ANUBARAK[0], ANUBARAK[1], ANUBARAK[2]);
    std::printf("  [route] landing -> Anub'arak reachable=%d complete=%d pts=%u len2d=%.1f %s\n",
                r.reachable, r.corridorComplete, r.pointCount, r.routeLength2d,
                r.failureReason.c_str());
    EXPECT_TRUE(r.reachable) << "the drop landing cannot path to Anub'arak at all";

    // First anchor sits on the way, not behind: a leg the follower cannot resnap
    // over is how a mid-route rebuild sends the party back north.
    WaypointHint const& first = (*route)[0];
    float const firstLeg = std::hypot(first.x - LANDING[0], first.y - LANDING[1]);
    std::printf("  [leg] landing -> anchor 1 = %.1fyd\n", firstLeg);
    EXPECT_LT(firstLeg, 40.0f);
}
