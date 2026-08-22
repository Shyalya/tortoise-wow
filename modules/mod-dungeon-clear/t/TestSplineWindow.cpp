/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Edge-pinning tests for DungeonPathFollower::AppendWindowPoints — the pure
// collection core of BuildSplineWindow, and the seam the AdvanceWindowYards
// distance cap lives behind. The cap is what bounds the Advance movement
// quantum (heroic over-pull transit plan, Phase A): a spline window is a
// movement commitment during which no route evaluation runs, so its length must
// never exceed what the blocking-trash detector can see ahead.

#include "gtest/gtest.h"

#include <cmath>

#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"

namespace
{
    // A single-segment path whose polyline runs along +X with `spacing` yards
    // between consecutive points: (spacing, 0, 0), (2*spacing, 0, 0), ...
    ChunkedPathfinder::Result StraightPath(size_t points, float spacing)
    {
        ChunkedPathfinder::Result path;
        PathSegment seg;
        for (size_t i = 1; i <= points; ++i)
            seg.polyline.push_back(G3D::Vector3(spacing * float(i), 0.0f, 0.0f));
        path.segments.push_back(seg);
        return path;
    }

    // The live-position seed BuildSplineWindow pushes before collecting.
    std::vector<G3D::Vector3> Seeded()
    {
        return { G3D::Vector3(0.0f, 0.0f, 0.0f) };
    }
}

// maxYards = 0 is the historical unbounded behaviour — the identical point run.
TEST(DcSplineWindowTest, WindowUncappedMatchesLegacy)
{
    ChunkedPathfinder::Result const path = StraightPath(10, 8.0f);

    std::vector<G3D::Vector3> capped = Seeded();
    DungeonPathFollower::AppendWindowPoints(path, 0, 0, 0.0f, capped);

    ASSERT_EQ(capped.size(), 11u);  // seed + all 10 points
    for (size_t i = 0; i < 10; ++i)
        EXPECT_FLOAT_EQ(capped[i + 1].x, 8.0f * float(i + 1));
}

// A 10-point x 8yd polyline with maxYards = 35 returns live-pos + 5 points:
// the cap binds at accumulated 40yd, with the point that CROSSES the cap
// included so the window always crosses it rather than stopping short.
TEST(DcSplineWindowTest, WindowStopsAtDistanceCap)
{
    ChunkedPathfinder::Result const path = StraightPath(10, 8.0f);

    std::vector<G3D::Vector3> window = Seeded();
    DungeonPathFollower::AppendWindowPoints(path, 0, 0, 35.0f, window);

    ASSERT_EQ(window.size(), 6u);  // seed + 5 points = 40yd of route
    EXPECT_FLOAT_EQ(window.back().x, 40.0f);
}

// A cap smaller than the first leg still yields one forward point — never an
// empty window, which would drop the caller into the MoveTo fallback and
// reintroduce the per-point stutter.
TEST(DcSplineWindowTest, WindowAlwaysHasOneForwardPoint)
{
    ChunkedPathfinder::Result const path = StraightPath(3, 8.0f);

    std::vector<G3D::Vector3> window = Seeded();
    DungeonPathFollower::AppendWindowPoints(path, 0, 0, 1.0f, window);

    ASSERT_EQ(window.size(), 2u);  // seed + first point, never seed alone
    EXPECT_FLOAT_EQ(window.back().x, 8.0f);
}

// The cap does not disturb the existing jump break: a jump segment stops the
// window regardless of remaining yardage.
TEST(DcSplineWindowTest, WindowStillStopsAtJumpLeg)
{
    ChunkedPathfinder::Result path = StraightPath(3, 8.0f);
    PathSegment jump;
    jump.jumpDown = true;
    jump.polyline.push_back(G3D::Vector3(100.0f, 0.0f, -10.0f));
    path.segments.push_back(jump);

    std::vector<G3D::Vector3> window = Seeded();
    DungeonPathFollower::AppendWindowPoints(path, 0, 0, 400.0f, window);

    ASSERT_EQ(window.size(), 4u);  // seed + the 3 ground points, jump excluded
    EXPECT_FLOAT_EQ(window.back().x, 24.0f);
}

// The cap measures 3D length, not point count: tighter spacing packs more
// points into the same yardage.
TEST(DcSplineWindowTest, WindowCapIsYardsNotPoints)
{
    ChunkedPathfinder::Result const path = StraightPath(50, 4.0f);

    std::vector<G3D::Vector3> window = Seeded();
    DungeonPathFollower::AppendWindowPoints(path, 0, 0, 35.0f, window);

    ASSERT_EQ(window.size(), 10u);  // seed + 9 points = 36yd crosses the cap
    EXPECT_FLOAT_EQ(window.back().x, 36.0f);
}

// Accumulation starts from the seed (the bot's live position): a bot standing
// short of the route counts the rejoin distance against the cap too.
TEST(DcSplineWindowTest, WindowCountsTheLegFromTheLivePosition)
{
    ChunkedPathfinder::Result const path = StraightPath(10, 8.0f);

    std::vector<G3D::Vector3> window = { G3D::Vector3(-16.0f, 0.0f, 0.0f) };
    DungeonPathFollower::AppendWindowPoints(path, 0, 0, 35.0f, window);

    // 24yd to the first point, 32 to the second; the third (accumulated 40yd)
    // crosses the 35 cap and is the last one included.
    ASSERT_EQ(window.size(), 4u);
    EXPECT_FLOAT_EQ(window.back().x, 24.0f);
}

// An empty window (no live-position seed) is a caller error and appends
// nothing rather than reading window.back().
TEST(DcSplineWindowTest, WindowRequiresTheSeed)
{
    ChunkedPathfinder::Result const path = StraightPath(3, 8.0f);

    std::vector<G3D::Vector3> window;
    DungeonPathFollower::AppendWindowPoints(path, 0, 0, 35.0f, window);
    EXPECT_TRUE(window.empty());
}

// ===========================================================================
// Pure geometry: PointIsBehind — the direction half of the stale-cursor guard.
//
// The bug it closes: RouteDeviation is a PERPENDICULAR distance, so a bot
// carried straight PAST its cursor along the same corridor reads as barely off
// the line. The off-line rejoin (OFF_PATH_THRESHOLD, 6yd) then issues a MoveTo
// to a hop point BEHIND the bot, while the distance-based re-anchor
// (DC_REANCHOR_DISTANCE, 12yd) does not fire. Everything in that 6-12yd band
// walked backward, re-anchored, and glided forward again — the short
// back-and-forth on approach.
// ===========================================================================

#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"

namespace
{
    // Bot at the origin, route heading +X.
    bool Behind(float px, float py, float dirX = 1.0f, float dirY = 0.0f)
    {
        return DungeonPathFollower::PointIsBehind(0.0f, 0.0f, px, py, dirX, dirY);
    }
}

TEST(DcHopDirectionTest, PointAheadIsNotBehind)
{
    EXPECT_FALSE(Behind(10.0f, 0.0f));
    EXPECT_FALSE(Behind(4.0f, 9.0f));    // ahead and well off to the side
}

TEST(DcHopDirectionTest, PointBehindIsBehind)
{
    EXPECT_TRUE(Behind(-10.0f, 0.0f));
    EXPECT_TRUE(Behind(-4.0f, 9.0f));    // behind despite being off to the side
}

// The case the distance rule misses: a bot 8yd PAST its cursor on a straight
// corridor. Perpendicular deviation is ~0 and the hop is only 8yd away (under
// the 12yd re-anchor limit), so direction is the only signal that catches it.
TEST(DcHopDirectionTest, CarriedPastCursorOnAStraightCorridorReadsBehind)
{
    EXPECT_TRUE(Behind(-8.0f, 0.5f));
}

// Exactly abeam is not behind — a hop level with the bot is still reachable
// without travelling against the route, and treating it as behind would
// re-anchor on every ordinary pass of a route point.
TEST(DcHopDirectionTest, AbeamIsNotBehind)
{
    EXPECT_FALSE(Behind(0.0f, 10.0f));
    EXPECT_FALSE(Behind(0.0f, -10.0f));
}

// A degenerate heading is "no opinion", never "behind": callers must fall back
// to the distance rule rather than re-anchor on a route with no direction.
TEST(DcHopDirectionTest, DegenerateHeadingIsNeverBehind)
{
    EXPECT_FALSE(Behind(-10.0f, 0.0f, 0.0f, 0.0f));
}

// Direction follows the route, not the world axes.
TEST(DcHopDirectionTest, HeadingRotatesWithTheRoute)
{
    EXPECT_TRUE(Behind(0.0f, -10.0f, 0.0f, 1.0f));   // route heads +Y
    EXPECT_FALSE(Behind(0.0f, 10.0f, 0.0f, 1.0f));
    EXPECT_TRUE(Behind(7.0f, 7.0f, -1.0f, -1.0f));   // route heads -X-Y
}

// ===========================================================================
// Ramp arrival: PointIsReached / PointIsVerticallyStranded — the pure core of
// NextHop's cursor-advance test.
//
// The bug they close (tr-20260818-073620-14, Blackrock Spire, the ramp below
// Overlord Wyrmthalak): the arrival test was a single 3D radius of 3.0yd.
// Recast rasterizes an incline into discrete plateaus, so a route point on a
// ramp floats above the collision floor the bot actually stands on — measured
// at 3.1yd there (navmesh Z 78.74 at (-55,-366), real floor Z 75.6). The bot
// walked to the point in plan view, arrived directly underneath it, and read
// 3.1 > 3.0: the cursor could never advance. The tank paced a 5yd box for nine
// minutes while posStuck -> resnap -> rebuild -> re-anchor -> off-line rejoin
// cycled 444 times.
//
// Splitting the axes makes a ramp arrival decidable: horizontal answers "am I
// there", vertical only rejects a point on another storey.
// ===========================================================================

namespace
{
    bool Reached(float botZ, G3D::Vector3 const& p, float botX = 0.0f, float botY = 0.0f)
    {
        return DungeonPathFollower::PointIsReached(botX, botY, botZ, p);
    }

    bool Stranded(float botZ, G3D::Vector3 const& p, float botX = 0.0f, float botY = 0.0f)
    {
        return DungeonPathFollower::PointIsVerticallyStranded(botX, botY, botZ, p);
    }
}

// The exact live geometry. Bot on the ramp floor at Z 75.6, route point on the
// navmesh plateau 3.1yd above it and 0.4yd away in plan view. Under the old 3D
// radius this read 3.13yd — unreached, forever.
TEST(DcRampArrivalTest, StandingUnderARampPointCountsAsReached)
{
    G3D::Vector3 const navPoint(0.4f, 0.0f, 78.7f);
    EXPECT_TRUE(Reached(75.6f, navPoint));
    EXPECT_FALSE(Stranded(75.6f, navPoint));

    // Pin the regression itself: the old 3D test rejected this point.
    float const dx = 0.4f, dz = 78.7f - 75.6f;
    EXPECT_GT(std::sqrt(dx * dx + dz * dz), DungeonPathFollower::POINT_REACHED);
}

// Horizontal distance still governs arrival — a point down the corridor is not
// reached just because it is level with the bot.
TEST(DcRampArrivalTest, HorizontalDistanceStillGovernsArrival)
{
    EXPECT_FALSE(Reached(70.0f, G3D::Vector3(0.0f, 0.0f, 70.0f), /*botX*/ 8.0f));
    EXPECT_TRUE(Reached(70.0f, G3D::Vector3(0.0f, 0.0f, 70.0f), /*botX*/ 2.0f));
    // Exactly on the horizontal limit counts as reached (closed interval).
    EXPECT_TRUE(Reached(70.0f, G3D::Vector3(0.0f, 0.0f, 70.0f),
                        /*botX*/ DungeonPathFollower::POINT_REACHED));
}

// The guard the vertical band exists for: a point on another storey directly
// overhead must NOT be swallowed as "reached".
TEST(DcRampArrivalTest, PointOnAnotherStoreyIsNotReached)
{
    G3D::Vector3 const upstairs(1.0f, 0.0f, 70.0f + 12.0f);
    EXPECT_FALSE(Reached(70.0f, upstairs));
    EXPECT_TRUE(Stranded(70.0f, upstairs));   // and it is named as stranded
}

// Stranded is strictly the "arrived in plan view but off the floor" case — a
// point the bot simply has not walked to yet is never stranded, however far
// below/above it sits.
TEST(DcRampArrivalTest, DistantPointIsNeverStranded)
{
    EXPECT_FALSE(Stranded(70.0f, G3D::Vector3(0.0f, 0.0f, 90.0f), /*botX*/ 20.0f));
    EXPECT_FALSE(Reached(70.0f, G3D::Vector3(0.0f, 0.0f, 90.0f), /*botX*/ 20.0f));
}

// Reached and Stranded partition the plan-view-arrived case: exactly one holds.
TEST(DcRampArrivalTest, ReachedAndStrandedArePartitionedWhenArrivedInPlanView)
{
    for (float dz = 0.0f; dz <= 14.0f; dz += 0.5f)
    {
        G3D::Vector3 const p(1.0f, 0.0f, 70.0f + dz);
        EXPECT_NE(Reached(70.0f, p), Stranded(70.0f, p)) << "dz=" << dz;
    }
}

// A ramp's whole plateau band must be walkable, not just the one live sample:
// every float up to the vertical tolerance resolves as reached.
TEST(DcRampArrivalTest, WholeNavmeshFloatBandIsReachable)
{
    for (float dz = 0.0f; dz <= DungeonPathFollower::POINT_REACHED_Z; dz += 0.25f)
    {
        EXPECT_TRUE(Reached(70.0f, G3D::Vector3(1.0f, 0.0f, 70.0f + dz))) << "above dz=" << dz;
        EXPECT_TRUE(Reached(70.0f, G3D::Vector3(1.0f, 0.0f, 70.0f - dz))) << "below dz=" << dz;
    }
}
