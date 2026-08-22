/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Tier-2 navmesh routing regression suite. Each scenario under t/fixtures/nav/
// is a frozen routing problem — (map, start, goal) + expected outcome — replayed
// against a real sliced navmesh through DcNavHarness. Every historical geometry
// bug (ledge target unreachable, under-map seam, jump-gap island, route ending
// short) becomes one scenario line here, the geometry twin of the decision
// fixtures.
//
// Client-derived map data is NEVER committed. The slice lives under a gitignored
// DC_MAPDATA_DIR/mmaps (produced by tools/slice_mapdata.py). With no slice the
// whole suite GTEST_SKIPs, so a clean checkout still builds and runs Tier 1.
//
// Scenario format: one flat JSON object per line (shared DcDecisionJson), keys:
//   name (str), map (uint), sx,sy,sz, tx,ty,tz (float),
//   expectReachable (bool, default true),
//   expectComplete  (bool, optional — assert route reaches the goal poly),
//   maxStepZ        (float, optional — assert no vertical pop exceeds it),
//   minPoints       (uint,  optional — assert the polyline has >= this many pts).

#include "gtest/gtest.h"
#include "NavHarness.h"
#include "Ai/Dungeon/DungeonClear/Util/DcDecisionJson.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifndef DC_FIXTURE_DIR
#define DC_FIXTURE_DIR "."
#endif
#ifndef DC_MAPDATA_DIR
#define DC_MAPDATA_DIR "."
#endif

namespace
{
    namespace fs = std::filesystem;

    struct Scenario
    {
        std::string name;
        uint32_t    map = 0;
        float sx = 0, sy = 0, sz = 0, tx = 0, ty = 0, tz = 0;
        bool  expectReachable = true;
        bool  hasExpectComplete = false; bool expectComplete = false;
        bool  hasMaxStepZ = false;       float maxStepZ = 0.0f;
        bool  hasMinPoints = false;      uint32_t minPoints = 0;
    };

    std::vector<Scenario> LoadScenarios()
    {
        std::vector<Scenario> out;
        fs::path const dir = fs::path(DC_FIXTURE_DIR) / "nav";
        if (!fs::exists(dir))
            return out;
        for (auto const& entry : fs::directory_iterator(dir))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;
            std::ifstream in(entry.path());
            std::string line;
            while (std::getline(in, line))
            {
                auto const parsed = DcDecisionJson::Parse(line);
                if (!parsed)
                    continue;  // blank / comment / not an object
                auto const& m = *parsed;
                Scenario s;
                s.name = DcDecisionJson::GetStr(m, "name", entry.path().stem().string());
                s.map = DcDecisionJson::GetU(m, "map", 0);
                s.sx = DcDecisionJson::GetF(m, "sx", 0);
                s.sy = DcDecisionJson::GetF(m, "sy", 0);
                s.sz = DcDecisionJson::GetF(m, "sz", 0);
                s.tx = DcDecisionJson::GetF(m, "tx", 0);
                s.ty = DcDecisionJson::GetF(m, "ty", 0);
                s.tz = DcDecisionJson::GetF(m, "tz", 0);
                s.expectReachable = DcDecisionJson::GetB(m, "expectReachable", true);
                if ((s.hasExpectComplete = DcDecisionJson::Has(m, "expectComplete")))
                    s.expectComplete = DcDecisionJson::GetB(m, "expectComplete", false);
                if ((s.hasMaxStepZ = DcDecisionJson::Has(m, "maxStepZ")))
                    s.maxStepZ = DcDecisionJson::GetF(m, "maxStepZ", 0);
                if ((s.hasMinPoints = DcDecisionJson::Has(m, "minPoints")))
                    s.minPoints = DcDecisionJson::GetU(m, "minPoints", 0);
                out.push_back(s);
            }
        }
        return out;
    }
}

TEST(DcNavGeometry, ScenariosRouteAsExpected)
{
    fs::path const mmapsDir = fs::path(DC_MAPDATA_DIR) / "mmaps";
    if (!fs::exists(mmapsDir))
        GTEST_SKIP() << "no sliced map data at " << mmapsDir
                     << " — run tools/slice_mapdata.py (never committed)";

    std::vector<Scenario> const scenarios = LoadScenarios();
    if (scenarios.empty())
        GTEST_SKIP() << "no nav scenarios under " << (fs::path(DC_FIXTURE_DIR) / "nav");

    std::map<uint32_t, std::shared_ptr<dtNavMesh>> meshes;  // per-map cache
    uint32_t ran = 0;
    uint32_t skipped = 0;

    for (Scenario const& s : scenarios)
    {
        auto it = meshes.find(s.map);
        if (it == meshes.end())
            it = meshes.emplace(s.map, DcNavHarness::LoadMap(DC_MAPDATA_DIR, s.map)).first;
        std::shared_ptr<dtNavMesh> const& mesh = it->second;
        if (!mesh)
        {
            ++skipped;  // map not sliced — covered by another run
            continue;
        }

        DcNavHarness::RouteResult const r =
            DcNavHarness::Route(mesh.get(), s.map, s.sx, s.sy, s.sz, s.tx, s.ty, s.tz);
        ASSERT_TRUE(r.built) << s.name;
        ++ran;

        EXPECT_EQ(r.reachable, s.expectReachable)
            << s.name << ": reachable mismatch (" << r.failureReason << ")";
        if (s.hasExpectComplete)
            EXPECT_EQ(r.corridorComplete, s.expectComplete) << s.name << ": completeness";
        if (s.expectReachable && s.hasMaxStepZ)
            EXPECT_LE(r.maxStepZ, s.maxStepZ)
                << s.name << ": vertical pop " << r.maxStepZ
                << " exceeds " << s.maxStepZ << " (under-map / ledge seam?)";
        if (s.expectReachable && s.hasMinPoints)
            EXPECT_GE(r.pointCount, s.minPoints) << s.name << ": too few route points";
    }

    if (ran == 0)
        GTEST_SKIP() << "nav scenarios present but no matching map slices ("
                     << skipped << " scenarios skipped)";
}

// ===========================================================================
// Pure geometry: FirstViolatedSphereOnPolyline (Phase B of the heroic
// over-pull transit plan). No navmesh needed — these run on every checkout.
// ===========================================================================

#include <cmath>

#include "Ai/Dungeon/DungeonClear/Util/DcEngageGeometry.h"

namespace
{
    using AvoidSphere = DcEngageGeometry::AvoidSphere;

    AvoidSphere Sphere(float x, float y, float r)
    {
        AvoidSphere s;
        s.x = x;
        s.y = y;
        s.r = r;
        return s;
    }

    // A polyline along +X at y=0 with `spacing` yards between points, starting
    // at the origin.
    std::vector<G3D::Vector3> StraightPolyline(size_t points, float spacing)
    {
        std::vector<G3D::Vector3> line;
        for (size_t i = 0; i < points; ++i)
            line.push_back(G3D::Vector3(spacing * float(i), 0.0f, 0.0f));
        return line;
    }
}

TEST(DcPolylineAvoidTest, PolylineClearOfSpheresReportsNoViolation)
{
    std::vector<G3D::Vector3> const line = StraightPolyline(8, 10.0f);
    std::vector<AvoidSphere> const spheres{ Sphere(35.0f, 20.0f, 4.0f),
                                            Sphere(-15.0f, 0.0f, 4.0f) };
    size_t leg = 999;
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphereOnPolyline(line, spheres, leg), -1);
}

// Ordering along the ROUTE, not distance from the walker, is the contract: a
// route that doubles back can violate a straight-line-nearer sphere on a LATER
// leg, and the one to stop at is the earlier one.
TEST(DcPolylineAvoidTest, PolylineViolationReportsFirstLegNotNearest)
{
    // P0(0,0) -> P1(10,0) -> P2(10,10) -> P3(-10,10): out, up, then back past
    // the walker.
    std::vector<G3D::Vector3> const line{
        G3D::Vector3(0.0f, 0.0f, 0.0f), G3D::Vector3(10.0f, 0.0f, 0.0f),
        G3D::Vector3(10.0f, 10.0f, 0.0f), G3D::Vector3(-10.0f, 10.0f, 0.0f) };
    // Sphere 0 sits on leg 2 and is NEARER the walker (11.2yd) than sphere 1
    // (11.7yd), which sits on leg 1.
    std::vector<AvoidSphere> const spheres{ Sphere(-5.0f, 10.0f, 3.0f),
                                            Sphere(10.0f, 6.0f, 3.0f) };
    size_t leg = 999;
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphereOnPolyline(line, spheres, leg), 1);
    EXPECT_EQ(leg, 1u);
}

// The reported leg START index is the truncation point: resizing the window to
// [0..legOut] leaves its last point OUTSIDE the sphere (the hazard threshold).
TEST(DcPolylineAvoidTest, WindowTruncatesBeforeSphereEntry)
{
    std::vector<G3D::Vector3> line = StraightPolyline(8, 10.0f);
    std::vector<AvoidSphere> const spheres{ Sphere(45.0f, 0.0f, 4.0f) };
    size_t leg = 999;
    int const idx = DcEngageGeometry::FirstViolatedSphereOnPolyline(line, spheres, leg);
    ASSERT_EQ(idx, 0);
    EXPECT_EQ(leg, 4u);

    line.resize(leg + 1);  // the caller's truncation
    ASSERT_EQ(line.size(), 5u);
    float const dx = line.back().x - spheres[0].x;
    float const dy = line.back().y - spheres[0].y;
    EXPECT_GT(std::sqrt(dx * dx + dy * dy), spheres[0].r);
}

// A sphere already covering the walker violates from the very first leg:
// legOut == 0, and a naive vertex truncation leaves <2 points. That degenerate
// case is WHY callers go through TruncateWindowAtSphere (below) rather than
// resizing themselves — a 1-point window is not a stop, it is the per-point
// MoveTo crawl.
TEST(DcPolylineAvoidTest, TruncationLeavesAtLeastOneForwardPoint)
{
    std::vector<G3D::Vector3> line = StraightPolyline(5, 10.0f);
    std::vector<AvoidSphere> const spheres{ Sphere(0.0f, 0.0f, 5.0f) };
    size_t leg = 999;
    ASSERT_EQ(DcEngageGeometry::FirstViolatedSphereOnPolyline(line, spheres, leg), 0);
    EXPECT_EQ(leg, 0u);

    line.resize(leg + 1);
    EXPECT_EQ(line.size(), 1u);   // seed only -> caller reads "no spline window"
    EXPECT_FALSE(line.empty());   // but never empty
}

// A 2-point polyline replicates FirstViolatedSphere exactly, including the
// nearest-centre tie-break when several spheres violate the single leg.
TEST(DcPolylineAvoidTest, ExtractionPreservesSingleSegmentBehaviour)
{
    std::vector<G3D::Vector3> const line{ G3D::Vector3(0.0f, 0.0f, 0.0f),
                                          G3D::Vector3(30.0f, 0.0f, 0.0f) };
    std::vector<AvoidSphere> const spheres{ Sphere(20.0f, 0.0f, 4.0f),
                                            Sphere(10.0f, 0.0f, 4.0f) };

    int const single = DcEngageGeometry::FirstViolatedSphere(0.0f, 0.0f,
                                                             30.0f, 0.0f, spheres);
    size_t leg = 999;
    int const poly = DcEngageGeometry::FirstViolatedSphereOnPolyline(line, spheres, leg);
    EXPECT_EQ(single, 1);  // centre nearest the walker
    EXPECT_EQ(poly, single);
    EXPECT_EQ(leg, 0u);
}

// ===========================================================================
// Pure geometry: AggroReach — the single-source aggro-reach formula (Phase C
// of the heroic over-pull transit plan).
// ===========================================================================

#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"

// The invariant the phase exists to establish: the detection band
// (AggroRangeOf, buffer 0) and the avoidance sphere (BystanderSpheres, buffer
// PullEnRouteMargin) differ by exactly the party buffer and nothing else.
TEST(DcAggroReachTest, AggroReachDetectionAndAvoidanceAgreeUpToPartyBuffer)
{
    float const detection =
        DcEngageGeometry::AggroReachYards(24.0f, 5.0f, 1.5f, 2.0f, 0.0f);
    float const avoidance =
        DcEngageGeometry::AggroReachYards(24.0f, 5.0f, 1.5f, 2.0f, 4.0f);
    EXPECT_FLOAT_EQ(avoidance - detection, 4.0f);
}

// Pins the C.1 widening explicitly: detection includes the bot's combat reach
// and AggroRangeMargin (the pre-unification band was aggroRange + mobReach
// only), so the normal-difficulty behaviour change is a deliberate, visible
// decision rather than a silent one.
TEST(DcAggroReachTest, AggroReachIncludesBotCombatReachAndMargin)
{
    EXPECT_FLOAT_EQ(DcEngageGeometry::AggroReachYards(24.0f, 5.0f, 1.5f, 2.0f, 0.0f),
                    32.5f);
    // Zeroing the bot reach and the margin recovers the old band.
    EXPECT_FLOAT_EQ(DcEngageGeometry::AggroReachYards(24.0f, 5.0f, 0.0f, 0.0f, 0.0f),
                    29.0f);
}

// Phase C must not widen the ALONG-ROUTE reach — a pack 60yd along the route
// stays out of scope. That axis belongs to Phase A's window cap; raising both
// at once makes the live signal unattributable.
TEST(DcAggroReachTest, TrashBandRespectsLookahead)
{
    EXPECT_FLOAT_EQ(DC_CORRIDOR_LOOKAHEAD, 35.0f);
}

// ===========================================================================
// Pure geometry: threshold-accurate window truncation
// (SegmentCircleEntry / TruncateWindowAtSphere).
//
// The bug these pin: truncating a spline window to the last VERTEX before a
// bystander sphere collapses it to the lone seed point whenever the tank is at
// or inside that sphere, and a <2-point window drops Advance into the legacy
// per-point MoveTo walk — the ~2yd/s crawl on approach ("a few steps, stop, a
// few steps, stop"). Live heroic: 181 of 302 truncations collapsed that way.
// ===========================================================================

TEST(DcWindowTruncateTest, EntryPointLandsJustOutsideTheCircle)
{
    // Leg along +X, circle centred at 20 with r=5 -> crossing at x=15, backed
    // off 1yd to x=14.
    std::optional<G3D::Vector3> const p = DcEngageGeometry::SegmentCircleEntry(
        G3D::Vector3(0.0f, 0.0f, 0.0f), G3D::Vector3(30.0f, 0.0f, 0.0f),
        20.0f, 0.0f, 5.0f, 1.0f);
    ASSERT_TRUE(p.has_value());
    EXPECT_NEAR(p->x, 14.0f, 0.01f);
    EXPECT_NEAR(p->y, 0.0f, 0.01f);
}

// Z rides the leg so the stop point stays ON the route, not at the leg start's
// height — a window point off the floor is the vertical-mismatch bug class.
TEST(DcWindowTruncateTest, EntryPointInterpolatesZ)
{
    std::optional<G3D::Vector3> const p = DcEngageGeometry::SegmentCircleEntry(
        G3D::Vector3(0.0f, 0.0f, 0.0f), G3D::Vector3(40.0f, 0.0f, 20.0f),
        40.0f, 0.0f, 20.0f, 0.0f);
    ASSERT_TRUE(p.has_value());
    EXPECT_NEAR(p->x, 20.0f, 0.01f);
    EXPECT_NEAR(p->z, 10.0f, 0.01f);
}

// Already inside: there is no threshold ahead of the walker to stop at. This is
// the case the vertex truncation answered with "stop where you stand".
TEST(DcWindowTruncateTest, NoEntryPointWhenWalkerIsAlreadyInside)
{
    EXPECT_FALSE(DcEngageGeometry::SegmentCircleEntry(
        G3D::Vector3(0.0f, 0.0f, 0.0f), G3D::Vector3(30.0f, 0.0f, 0.0f),
        2.0f, 0.0f, 10.0f, 1.0f).has_value());
    // A leg that never reaches the circle, and one that passes it by.
    EXPECT_FALSE(DcEngageGeometry::SegmentCircleEntry(
        G3D::Vector3(0.0f, 0.0f, 0.0f), G3D::Vector3(5.0f, 0.0f, 0.0f),
        40.0f, 0.0f, 5.0f, 1.0f).has_value());
    EXPECT_FALSE(DcEngageGeometry::SegmentCircleEntry(
        G3D::Vector3(0.0f, 0.0f, 0.0f), G3D::Vector3(30.0f, 0.0f, 0.0f),
        15.0f, 40.0f, 5.0f, 1.0f).has_value());
}

TEST(DcWindowTruncateTest, ClearWindowIsLeftAlone)
{
    std::vector<G3D::Vector3> window = StraightPolyline(6, 10.0f);
    std::vector<AvoidSphere> const spheres{ Sphere(20.0f, 40.0f, 5.0f) };
    size_t leg = 999;
    int idx = 99;
    EXPECT_FALSE(DcEngageGeometry::TruncateWindowAtSphere(
        window, spheres, 6.0f, 1.0f, leg, idx));
    EXPECT_EQ(window.size(), 6u);
    EXPECT_EQ(idx, -1);
}

// The honoured case: the window ends ON the sphere threshold, not at the vertex
// before it — the ~26yd of route the vertex truncation used to throw away.
TEST(DcWindowTruncateTest, HonouredTruncationEndsAtTheThresholdNotTheVertex)
{
    std::vector<G3D::Vector3> window = StraightPolyline(8, 10.0f);  // 0..70
    std::vector<AvoidSphere> const spheres{ Sphere(55.0f, 0.0f, 10.0f) };
    size_t leg = 999;
    int idx = 99;
    ASSERT_TRUE(DcEngageGeometry::TruncateWindowAtSphere(
        window, spheres, 6.0f, 1.0f, leg, idx));
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(leg, 4u);                    // the 40->50 leg first clips r=10
    // The vertex truncation would have stopped at x=40, throwing away 5yd of
    // clean route; the threshold is x=45, less the 1yd backoff.
    EXPECT_EQ(window.size(), 6u);
    EXPECT_NEAR(window.back().x, 44.0f, 0.01f);
    float const dx = window.back().x - spheres[0].x;
    EXPECT_GT(std::fabs(dx), spheres[0].r);  // outside, by construction
}

// The anti-crawl rule. A sphere already covering the tank cannot be avoided by
// stopping, so the truncation is DECLINED and the full window survives — the
// glide runs at full speed instead of degenerating into the per-point walk. The
// violation is still reported so the caller can log it.
TEST(DcWindowTruncateTest, TruncationDeclinedWhenTankIsInsideTheSphere)
{
    std::vector<G3D::Vector3> window = StraightPolyline(6, 10.0f);
    std::vector<AvoidSphere> const spheres{ Sphere(0.0f, 0.0f, 30.0f) };
    size_t leg = 999;
    int idx = -1;
    EXPECT_FALSE(DcEngageGeometry::TruncateWindowAtSphere(
        window, spheres, 6.0f, 1.0f, leg, idx));
    EXPECT_EQ(window.size(), 6u);   // untouched — never 1 point
    EXPECT_EQ(idx, 0);              // but the violation is reported
    EXPECT_EQ(leg, 0u);
}

// Parked AT a threshold from a previous truncation: the surviving glide would be
// a yard or two, which is a stutter, not a glide. Declined for the same reason.
TEST(DcWindowTruncateTest, TruncationDeclinedWhenTheSurvivingGlideIsTooShort)
{
    // Tank at the origin, sphere edge 3yd ahead — below the 6yd floor.
    std::vector<G3D::Vector3> window = StraightPolyline(6, 10.0f);
    std::vector<AvoidSphere> const spheres{ Sphere(13.0f, 0.0f, 10.0f) };
    size_t leg = 999;
    int idx = -1;
    EXPECT_FALSE(DcEngageGeometry::TruncateWindowAtSphere(
        window, spheres, 6.0f, 1.0f, leg, idx));
    EXPECT_EQ(window.size(), 6u);
    EXPECT_EQ(idx, 0);

    // Push the same sphere out so the glide clears the floor and it is honoured.
    std::vector<AvoidSphere> const further{ Sphere(25.0f, 0.0f, 10.0f) };
    ASSERT_TRUE(DcEngageGeometry::TruncateWindowAtSphere(
        window, further, 6.0f, 1.0f, leg, idx));
    EXPECT_NEAR(window.back().x, 14.0f, 0.01f);
}

// A too-short window can never be handed back shorter than it came in: the
// caller reads window.size() >= 2 as "glide available", so a truncation that
// returns false must leave a launchable window behind.
TEST(DcWindowTruncateTest, DeclinedTruncationNeverShortensTheWindow)
{
    std::vector<G3D::Vector3> const original = StraightPolyline(4, 10.0f);
    std::vector<G3D::Vector3> window = original;
    std::vector<AvoidSphere> const spheres{ Sphere(0.0f, 0.0f, 25.0f) };
    size_t leg = 0;
    int idx = -1;
    ASSERT_FALSE(DcEngageGeometry::TruncateWindowAtSphere(
        window, spheres, 6.0f, 1.0f, leg, idx));
    ASSERT_EQ(window.size(), original.size());
    for (size_t i = 0; i < window.size(); ++i)
        EXPECT_FLOAT_EQ(window[i].x, original[i].x);
}

// ===========================================================================
// Pure geometry: OrbitRing — the ring an avoidance orbit rides.
//
// The bug these pin: EnRoutePackAvoidPoint reused the room-aggro BOSS orbit for
// ordinary bystander trash, which places the waypoint on a FIXED ring
// (safeRadius + RoomAggroPartyMargin). safeRadius already carries
// PullEnRouteMargin, so a trash pack got a second party buffer on top and the
// waypoint landed ~40yd out — behind a tank standing at 35. The tank ran
// backward, which by itself cleared the bot->target line, which cancelled the
// detour, which sent it forward again over the same ground.
// ===========================================================================

using OrbitProfile = DcEngageGeometry::OrbitProfile;

TEST(DcOrbitRingTest, BossProfileKeepsTheFixedWideRing)
{
    DcEngageGeometry::OrbitStep const o = DcEngageGeometry::OrbitRing(
        OrbitProfile::RoomAggroBoss, 30.0f, 12.0f, 10.0f, 12.0f);
    EXPECT_FLOAT_EQ(o.radius, 40.0f);   // safeRadius + partyMargin, wherever the bot is
    EXPECT_FLOAT_EQ(o.step, 0.6f);      // fixed angle, leg length unbounded
}

// The regression, in numbers taken from a live heroic detour (r=29.3 sphere,
// RoomAggroPartyMargin 10, tank standing 35yd off): the boss ring puts the
// waypoint 4.3yd FURTHER from the pack than the tank already is. Bystander puts
// it exactly where the tank stands.
TEST(DcOrbitRingTest, BystanderNeverStepsRadiallyOutward)
{
    DcEngageGeometry::OrbitStep const boss = DcEngageGeometry::OrbitRing(
        OrbitProfile::RoomAggroBoss, 29.3f, 35.0f, 10.0f, 12.0f);
    EXPECT_GT(boss.radius, 35.0f);      // backward — the reported behaviour

    DcEngageGeometry::OrbitStep const by = DcEngageGeometry::OrbitRing(
        OrbitProfile::Bystander, 29.3f, 35.0f, 10.0f, 12.0f);
    EXPECT_FLOAT_EQ(by.radius, 35.0f);  // pure sidestep
}

// The other half of the same contract: a tank passing WIDE of a pack must not be
// dragged in toward it either, which the fixed ring did whenever botRadius
// exceeded safeRadius + partyMargin.
TEST(DcOrbitRingTest, BystanderNeverStepsRadiallyInward)
{
    DcEngageGeometry::OrbitStep const boss = DcEngageGeometry::OrbitRing(
        OrbitProfile::RoomAggroBoss, 20.0f, 60.0f, 10.0f, 12.0f);
    EXPECT_LT(boss.radius, 60.0f);      // pulled 30yd toward the pack

    DcEngageGeometry::OrbitStep const by = DcEngageGeometry::OrbitRing(
        OrbitProfile::Bystander, 20.0f, 60.0f, 10.0f, 12.0f);
    EXPECT_FLOAT_EQ(by.radius, 60.0f);
}

// safeRadius is the floor: a mob that closed the gap after the caller selected
// it must not leave the orbit riding a ring inside its own aggro sphere.
TEST(DcOrbitRingTest, BystanderRadiusFloorsAtTheSphere)
{
    DcEngageGeometry::OrbitStep const o = DcEngageGeometry::OrbitRing(
        OrbitProfile::Bystander, 25.0f, 18.0f, 10.0f, 12.0f);
    EXPECT_FLOAT_EQ(o.radius, 25.0f);
}

// Bound the LEG, not the angle: one sidestep stays cheap enough that the pull's
// early-out can throw it away without the tank having run half a room.
TEST(DcOrbitRingTest, BystanderBoundsTheChordNotTheAngle)
{
    // Wide stand-off: the angle shrinks so the chord stays at the cap.
    DcEngageGeometry::OrbitStep const wide = DcEngageGeometry::OrbitRing(
        OrbitProfile::Bystander, 20.0f, 60.0f, 10.0f, 12.0f);
    EXPECT_LT(wide.step, 0.6f);
    EXPECT_NEAR(wide.radius * wide.step, 12.0f, 0.01f);

    // Close in, where 0.6rad is already under the cap: the angle binds instead,
    // so the orbit never speeds UP to burn its whole budget.
    DcEngageGeometry::OrbitStep const close = DcEngageGeometry::OrbitRing(
        OrbitProfile::Bystander, 10.0f, 15.0f, 10.0f, 12.0f);
    EXPECT_FLOAT_EQ(close.step, 0.6f);
    EXPECT_LT(close.radius * close.step, 12.0f);

    // The boss profile is deliberately NOT bounded — a 60yd stand-off still
    // steps the full angle, a 36yd leg.
    DcEngageGeometry::OrbitStep const boss = DcEngageGeometry::OrbitRing(
        OrbitProfile::RoomAggroBoss, 50.0f, 60.0f, 10.0f, 12.0f);
    EXPECT_GT(boss.radius * boss.step, 12.0f);
}

// ===========================================================================
// STORMWIND STOCKADE (map 34) — the geometry behind issue #17, pinned from the
// live `creature` rows so a future tuning change has to face the real numbers.
//
// The first boss (Targorr, 159.6/1.3) sits 105yd dead ahead of the entrance
// (54.2/0.3) up the central axis. The cells flanking that axis hold lvl 23-25
// ELITES 10-21yd off the route line. Effective aggro reach for them is
// GetAggroRange(~20) + both combat reaches + AggroRangeMargin(2) ~= 25yd, and
// the avoidance sphere adds PullEnRouteMargin(4) on top -> ~29yd.
//
// These two tests establish, in pure geometry, why the answer had to be a
// TARGETING change (DcTargeting::FindEnRouteAggroPack) and not an avoidance
// one: the corridor is inside the cells' aggro, and there is nowhere else to
// walk. See the SweepApplies / FindEnRouteAggroPack comments.
// ===========================================================================

namespace
{
    // Live spawn positions, `creature` rows on map 34. Y grows WEST, so the
    // signs are which side of the central corridor a cell sits on.
    constexpr float kStockadeSphereR = 29.0f;   // aggro reach + party margin

    AvoidSphere StockadeCell(float x, float y) { return Sphere(x, y, kStockadeSphereR); }
}

// The premise. Walking the central axis violates cell after cell — the tank
// cannot traverse it without waking them, whatever it is aiming at.
TEST(DcPolylineAvoidTest, StockadeCentralAxisRunsInsideTheFlankingCellsAggro)
{
    // Entrance -> Targorr, sampled every ~21yd along the axis.
    std::vector<G3D::Vector3> const axis{
        G3D::Vector3(54.2f, 0.3f, -18.3f),  G3D::Vector3(75.3f, 0.5f, -25.5f),
        G3D::Vector3(96.4f, 0.7f, -25.5f),  G3D::Vector3(117.5f, 0.9f, -25.5f),
        G3D::Vector3(138.6f, 1.1f, -25.6f), G3D::Vector3(159.6f, 1.3f, -25.6f)};

    // Every one of these is a cell mob OFF the centre line — none of them would
    // ever be "standing on the path". (The DEEP bank, at |y| ~ 29-30, sits a
    // hair outside 29yd and is pinned as a non-violation below: the sweep's
    // reach really does stop, and it stops right about where this dungeon's
    // second rank of cells begins.)
    std::vector<AvoidSphere> const cells{
        StockadeCell(80.7f, 16.8f), StockadeCell(82.3f, -10.6f),
        StockadeCell(86.8f, 21.1f), StockadeCell(103.2f, 12.9f),
        StockadeCell(108.7f, 21.5f)};

    // Each cell, on its own, is violated by the walk.
    for (size_t i = 0; i < cells.size(); ++i)
    {
        size_t leg = 999;
        std::vector<AvoidSphere> const one{cells[i]};
        EXPECT_GE(DcEngageGeometry::FirstViolatedSphereOnPolyline(axis, one, leg), 0)
            << "cell " << i << " should be inside aggro of the central axis";
    }
}

// And avoidance has no answer for it. Standing where the first corridor mob
// died (76.2/0.3), the east cell's sphere already covers the tank, so the
// truncation is correctly DECLINED and the glide runs straight through it.
// That is the whole argument for pulling the cell instead of dodging it.
TEST(DcPolylineAvoidTest, StockadeCellCannotBeAvoidedOnlyPulled)
{
    std::vector<G3D::Vector3> window{
        G3D::Vector3(76.2f, 0.3f, -25.5f), G3D::Vector3(96.4f, 0.7f, -25.5f),
        G3D::Vector3(117.5f, 0.9f, -25.5f)};
    // 12.5yd from the tank — well inside its own 29yd sphere.
    std::vector<AvoidSphere> const spheres{StockadeCell(82.3f, -10.6f)};

    size_t leg = 999;
    int idx = -1;
    EXPECT_FALSE(DcEngageGeometry::TruncateWindowAtSphere(
        window, spheres, 6.0f, 1.0f, leg, idx));
    EXPECT_EQ(window.size(), 3u);   // untouched: the tank walks through it
    EXPECT_EQ(idx, 0);              // the violation is real, just unavoidable
}

// Route order picks the cell BESIDE the walk over the mob further along the
// centre line — the substitution FindEnRouteAggroPack makes. Both are hostile
// and both are within the lookahead; the corridor band answers "what is on my
// path" and would reach past the cell to the on-line mob whenever the cell is
// LOS-shadowed by its own doorway. The sphere scan cannot be shadowed.
TEST(DcPolylineAvoidTest, StockadeSweepTakesTheCellBesideTheWalk)
{
    // 35yd of route (DC_CORRIDOR_LOOKAHEAD) from where the first corridor mob
    // stood, heading for Targorr.
    std::vector<G3D::Vector3> const window{
        G3D::Vector3(76.2f, 0.3f, -25.5f), G3D::Vector3(111.2f, 0.7f, -25.5f)};

    // Index 0 is the on-line mob 34.5yd further up the corridor; index 1 is the
    // east cell 12.5yd away and 10.6yd off the line.
    std::vector<AvoidSphere> const spheres{StockadeCell(110.7f, -0.8f),
                                           StockadeCell(82.3f, -10.6f)};

    size_t leg = 999;
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphereOnPolyline(window, spheres, leg), 1);
    EXPECT_EQ(leg, 0u);
}

// The reach is not unbounded — and in this dungeon the boundary is genuinely
// close-run, which is worth knowing before anyone retunes PullEnRouteMargin.
// The deep cell at (87.7, -29.0) is 29.62yd from the walk against a 29yd
// sphere: a 0.62yd miss. It is left alone, so the sweep pulls what the walk
// would wake and not the whole dungeon — but a yard more margin would sweep the
// second rank of cells too, and a yard less would drop part of the first.
TEST(DcPolylineAvoidTest, StockadeSweepReachStopsAtTheDeepCellBank)
{
    std::vector<G3D::Vector3> const window{
        G3D::Vector3(75.3f, 0.5f, -25.5f), G3D::Vector3(96.4f, 0.7f, -25.5f)};

    size_t leg = 999;
    std::vector<AvoidSphere> const outside{StockadeCell(87.7f, -29.0f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphereOnPolyline(window, outside, leg), -1);

    // Pinning the margin itself: one more yard of reach and it is swept.
    std::vector<AvoidSphere> const inside{Sphere(87.7f, -29.0f, 30.0f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphereOnPolyline(window, inside, leg), 0);
}

// ---------------------------------------------------------------------------
// RouteSweepRegistry — the scope of the whole en-route sweep, pinned so widening
// it is a deliberate act with a test to update rather than a silent table edit.
// ---------------------------------------------------------------------------

#include "Ai/Dungeon/DungeonClear/Data/RouteSweepRegistry.h"

TEST(DcRouteSweepRegistryTest, OnlyTheStockadeSweepsForNow)
{
    EXPECT_TRUE(RouteSweepRegistry::SweepsRoute(34));    // The Stockade

    // Every other dungeon keeps the historical corridor-band pick AND the
    // untouched Dynamic verdict. Forcing Advanced reshapes how every fight in a
    // dungeon happens, so a map earns its row on its own run data — adding one
    // here means the evidence exists for it.
    EXPECT_FALSE(RouteSweepRegistry::SweepsRoute(36));   // Deadmines
    EXPECT_FALSE(RouteSweepRegistry::SweepsRoute(33));   // Shadowfang Keep
    EXPECT_FALSE(RouteSweepRegistry::SweepsRoute(109));  // Sunken Temple
    EXPECT_FALSE(RouteSweepRegistry::SweepsRoute(585));  // Magisters' Terrace
    EXPECT_FALSE(RouteSweepRegistry::SweepsRoute(0));    // not a dungeon at all
}
