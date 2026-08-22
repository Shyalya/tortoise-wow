/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Route probe for the Hellfire Ramparts (map 543) zone-in platform — the
// "started the run next to the ledge and the tank ran off it" report.
//
// The platform is a rampart: floor at z~68 ringed by cliff on every side, with
// the next surface ~160yd below. The party zones in at (-1355.24, 1641.12) and
// the first boss (Watchkeeper Gargolmar) is at (-1187.19, 1530.46) — NORTH-EAST
// and ~200yd away. Anything that leaves the z~68 platform on the way there is
// the bug.
//
// Not a committed regression (needs client-derived mmaps): reads the FULL mmaps
// dir from env DC_PROBE_MMAPS and GTEST_SKIPs when unset, same contract as
// MechanarElevatorProbe.
//
//   DC_PROBE_MMAPS=/home/jared/azerothcore/env/dist/bin \
//     ./dungeon_clear_tests --gtest_filter='RampartsLedgeProbe.*'

#include "gtest/gtest.h"
#include "NavHarness.h"

#include "Ai/Dungeon/DungeonClear/Data/DcNavPenaltyRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Util/LongRangePathfinder.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{
    constexpr uint32 MAP_RAMPARTS = 543;

    // First boss, ~200yd north-east of the zone-in point.
    constexpr float GARGOLMAR[3] = { -1187.19f, 1530.46f, 68.50f };

    // The platform floor. Anything below this is off the rampart.
    constexpr float PLATFORM_Z_FLOOR = 60.0f;

    void RouteLog(dtNavMesh const* mesh, char const* label,
                  float sx, float sy, float sz)
    {
        LongRangePathfinder::RawResult const r = LongRangePathfinder::BuildCoreFromMesh(
            mesh, MAP_RAMPARTS, sx, sy, sz, GARGOLMAR[0], GARGOLMAR[1], GARGOLMAR[2]);

        float minZ = 1e9f;
        size_t offPlatform = 0;
        G3D::Vector3 worst(0.0f, 0.0f, 0.0f);
        for (G3D::Vector3 const& p : r.rawPts)
        {
            if (p.z < minZ) { minZ = p.z; worst = p; }
            if (p.z < PLATFORM_Z_FLOOR)
                ++offPlatform;
        }

        std::printf("\n  [%s]\n", label);
        std::printf("    start (%.2f, %.2f, %.2f)  inFence=%d\n", sx, sy, sz,
                    DcNavPenaltyRegistry::IsInsideRegion(MAP_RAMPARTS, sx, sy, sz) ? 1 : 0);
        std::printf("    reachable=%d complete=%d startFar=%d pts=%zu  %s\n",
                    r.reachable, r.corridorComplete, r.startFarFromPoly,
                    r.rawPts.size(), r.failureReason.c_str());
        std::printf("    minZ=%.2f  points below z%.0f: %zu  (worst %.2f, %.2f, %.2f)\n",
                    minZ, PLATFORM_Z_FLOOR, offPlatform, worst.x, worst.y, worst.z);

        // First 14 points — enough to see which way it leaves the platform.
        size_t const show = r.rawPts.size() < 14 ? r.rawPts.size() : 14;
        for (size_t i = 0; i < show; ++i)
            std::printf("      %2zu  (%9.2f, %9.2f, %8.2f)%s\n", i,
                        r.rawPts[i].x, r.rawPts[i].y, r.rawPts[i].z,
                        r.rawPts[i].z < PLATFORM_Z_FLOOR ? "   <-- OFF THE PLATFORM" : "");
        if (show < r.rawPts.size())
            std::printf("      ... %zu more\n", r.rawPts.size() - show);
    }
}

TEST(RampartsLedgeProbe, RouteOffTheZoneInPlatform)
{
    char const* dir = std::getenv("DC_PROBE_MMAPS");
    if (!dir || !*dir)
        GTEST_SKIP() << "set DC_PROBE_MMAPS to a dir containing mmaps/ for map 543";

    std::shared_ptr<dtNavMesh> const mesh = DcNavHarness::LoadMap(dir, MAP_RAMPARTS);
    ASSERT_TRUE(mesh) << "no mmaps for map 543 under " << dir;

    // a) the registry zone-in point — well clear of every fence.
    RouteLog(mesh.get(), "zone-in point", -1355.24f, 1641.12f, 68.25f);

    // b) on the south-west ledge, inside the fence (the mid-strip point).
    RouteLog(mesh.get(), "SW ledge, inside the fence", -1351.55f, 1656.98f, 68.46f);

    // c) the pocket the old strip marooned — now inside the fence.
    RouteLog(mesh.get(), "SW ledge pocket", -1349.00f, 1662.00f, 68.70f);

    // d) hard against the south-east lip of the entrance walkway.
    RouteLog(mesh.get(), "SE walkway lip", -1360.00f, 1630.00f, 68.60f);
}

// What the "Approach Vazruden" event actually asks for when it mis-fires from the
// zone-in platform: a route to the final-encounter trigger spot, which is across
// the chasm and ~15yd up. This is the move the tank makes when it "runs off the
// ledge".
TEST(RampartsLedgeProbe, RouteToTheFinalEncounterTrigger)
{
    char const* dir = std::getenv("DC_PROBE_MMAPS");
    if (!dir || !*dir)
        GTEST_SKIP() << "set DC_PROBE_MMAPS to a dir containing mmaps/ for map 543";

    std::shared_ptr<dtNavMesh> const mesh = DcNavHarness::LoadMap(dir, MAP_RAMPARTS);
    ASSERT_TRUE(mesh) << "no mmaps for map 543 under " << dir;

    // The event's MoveTo target, between the two Hellfire Sentries.
    constexpr float TRIGGER[3] = { -1378.0f, 1718.0f, 82.9f };

    auto probe = [&](char const* label, float sx, float sy, float sz)
    {
        DcNavHarness::RouteResult const r = DcNavHarness::Route(
            mesh.get(), MAP_RAMPARTS, sx, sy, sz, TRIGGER[0], TRIGGER[1], TRIGGER[2]);
        std::printf("  [%-30s] reachable=%d complete=%d startFar=%d pts=%u len2d=%.1f maxStepZ=%.2f  %s\n",
                    label, r.reachable, r.corridorComplete, r.startFarFromPoly,
                    r.pointCount, r.routeLength2d, r.maxStepZ, r.failureReason.c_str());
    };

    std::printf("\n  route TO the final-encounter trigger (%.1f, %.1f, %.1f):\n",
                TRIGGER[0], TRIGGER[1], TRIGGER[2]);
    // The ledge floor closest to the Sentries — the spot that trips the 70yd scan.
    probe("SW ledge (trips the scan)", -1352.00f, 1663.00f, 68.61f);
    probe("zone-in point", -1355.24f, 1641.12f, 68.25f);
    // From the upper level, where the event is actually meant to fire.
    probe("upper level near Omor", -1122.34f, 1718.41f, 89.43f);
}

// THE ACTUAL BUG. The final platform sits across the chasm from the zone-in room,
// so "within N yards of a Hellfire Sentry" does NOT mean "on the final approach".
// At the old 70yd scan, the south-west ledge of the ZONE-IN platform was 59.8yd
// from a Sentry, so starting a run there fired the approach event immediately and
// sent the tank on a 729yd trek to the final encounter before Gargolmar.
//
// This pins the gate against the real mesh: the ledge is genuinely walkable floor,
// it is outside the scan radius, and it is below the height floor.
TEST(RampartsLedgeProbe, FinalApproachGateExcludesTheZoneInPlatform)
{
    char const* dir = std::getenv("DC_PROBE_MMAPS");
    if (!dir || !*dir)
        GTEST_SKIP() << "set DC_PROBE_MMAPS to a dir containing mmaps/ for map 543";

    std::shared_ptr<dtNavMesh> const mesh = DcNavHarness::LoadMap(dir, MAP_RAMPARTS);
    ASSERT_TRUE(mesh) << "no mmaps for map 543 under " << dir;

    // Vazruden's Reset summons these; they have no DB spawn, so the coordinates
    // come from the boss script and are the gate's real reference points.
    constexpr float SENTRY_A[3] = { -1372.56f, 1724.31f, 82.8f };
    constexpr float SENTRY_B[3] = { -1383.39f, 1711.82f, 82.8f };

    auto dist3 = [](float const* a, G3D::Vector3 const& b)
    {
        float const dx = a[0] - b.x, dy = a[1] - b.y, dz = a[2] - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };

    // The worst case on the zone-in platform: the ledge floor closest to a Sentry.
    // Snap it so the test is asserting about REAL walkable floor, not a coordinate.
    G3D::Vector3 ledge;
    ASSERT_TRUE(DcNavHarness::NearestPoint(mesh.get(), -1352.00f, 1663.00f, 68.61f,
                                           /*hExt*/ 4.0f, /*vExt*/ 6.0f, ledge))
        << "the SW ledge sample is not on the navmesh — re-measure it";

    float const dLedge = std::min(dist3(SENTRY_A, ledge), dist3(SENTRY_B, ledge));
    std::printf("\n  closest zone-in floor to a Sentry: (%.2f, %.2f, %.2f) at %.1f yd\n",
                ledge.x, ledge.y, ledge.z, dLedge);

    // Both halves of the gate must reject it — either alone would do, and having
    // both is what stops a radius tweak from reopening the bug.
    EXPECT_GT(dLedge, DcHellfireRamparts::FINAL_APPROACH_SCAN)
        << "the zone-in ledge is inside the Sentry scan again";
    EXPECT_LT(ledge.z, DcHellfireRamparts::FINAL_APPROACH_MIN_Z)
        << "the zone-in ledge is above the approach height floor again";

    // ...and the gate must still ADMIT the real approach: the trigger spot itself
    // is on the upper level and right between the two Sentries.
    G3D::Vector3 trigger;
    ASSERT_TRUE(DcNavHarness::NearestPoint(mesh.get(), -1378.0f, 1718.0f, 82.9f,
                                           /*hExt*/ 4.0f, /*vExt*/ 6.0f, trigger));
    float const dTrigger = std::min(dist3(SENTRY_A, trigger), dist3(SENTRY_B, trigger));
    std::printf("  final-encounter trigger:           (%.2f, %.2f, %.2f) at %.1f yd\n",
                trigger.x, trigger.y, trigger.z, dTrigger);

    EXPECT_LT(dTrigger, DcHellfireRamparts::FINAL_APPROACH_SCAN)
        << "the gate no longer fires at the trigger spot — the event would never run";
    EXPECT_GT(trigger.z, DcHellfireRamparts::FINAL_APPROACH_MIN_Z)
        << "the trigger spot is below the approach height floor";
}
