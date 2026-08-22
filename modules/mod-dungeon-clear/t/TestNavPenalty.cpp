/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "Ai/Dungeon/DungeonClear/Data/DcNavPenaltyRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRouteFilter.h"

// Pure tests for the hand-authored no-go volume table, plus the one decision the
// route filter makes off it (does the fence apply to this query at all). No
// navmesh / map data required, so these run in every build (unlike the Tier-2 nav
// geometry suite).

TEST(DcNavPenaltyRegistry, ReportsMapsWithVolumes)
{
    EXPECT_TRUE(DcNavPenaltyRegistry::HasVolumes(229));   // Lower Blackrock Spire
    EXPECT_TRUE(DcNavPenaltyRegistry::HasVolumes(556));   // Sethekk Halls
    EXPECT_TRUE(DcNavPenaltyRegistry::HasVolumes(546));   // Underbog
    EXPECT_TRUE(DcNavPenaltyRegistry::HasVolumes(543));   // Hellfire Ramparts
    EXPECT_TRUE(DcNavPenaltyRegistry::HasVolumes(389));   // Ragefire Chasm
    EXPECT_FALSE(DcNavPenaltyRegistry::HasVolumes(0));     // no rows
    EXPECT_FALSE(DcNavPenaltyRegistry::HasVolumes(230));   // BRD — no rows
    EXPECT_FALSE(DcNavPenaltyRegistry::HasVolumes(560));   // Old Hillsbrad — no rows
}

TEST(DcNavPenaltyRegistry, PenalizesTheSethekkBackDoorRamp)
{
    // A point partway up the narrow x≈45 shortcut ramp (door y151,z0 ->
    // platform y250,z27): squarely inside the box, so it must be taxed.
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(556, 45.0f, 200.0f, 14.0f), 1.0f);

    // The legitimate western approach arrives on the platform at y>=250 — north
    // of the box. Ikiss's own position must be untaxed so the final hop is free.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(556, 44.7f, 287.0f, 25.2f), 1.0f);

    // The lower lobby south of the door (y<150) is the normal pre-Syth floor —
    // untaxed so early routing is unchanged.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(556, 45.0f, 130.0f, 0.3f), 1.0f);

    // The west ramp the long way actually uses (~(-250, 210)) is far from the
    // box — untaxed.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(556, -250.0f, 210.0f, 27.0f), 1.0f);
}

TEST(DcNavPenaltyRegistry, FencesTheSethekkFallThroughCorner)
{
    // The five measured arc vertices round off a room corner where the navmesh
    // stitches a sliver of floor over a drop. A point in the middle of the pocket
    // (centroid of the arc, on the z≈26.7 floor) must be taxed.
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(556, -211.69f, 297.73f, 26.7f), 1.0f);

    // The arc's bounding-box top corners sit OUTSIDE the arc (the curve pulls
    // away from them) — open floor that must stay untaxed, which a box couldn't
    // achieve.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(556, -233.0f, 326.0f, 26.7f), 1.0f);

    // Same XY as the pocket but well below the floor band → a level below this
    // corner is not this hazard, so it is untaxed.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(556, -211.69f, 297.73f, 5.0f), 1.0f);

    // Geometrically inside the pocket, but a different map → no region applies.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(0, -211.69f, 297.73f, 26.7f), 1.0f);
}

TEST(DcNavPenaltyRegistry, FencesTheHellfireRampartsCorridorWall)
{
    // A point on the wall line's midpoint (≈(-1351.55, 1656.98) at floor z68) sits
    // squarely inside the strip laid along the wall, so it must be taxed.
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(543, -1351.55f, 1656.98f, 68.46f), 1.0f);

    // A few yards off the wall, into the corridor centre (offset ~5yd along the
    // strip's inboard perpendicular): clear of the footprint, so untaxed. The
    // re-cut moved only the strip's FAR side, so this side is unchanged.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(543, -1348.58f, 1652.96f, 68.46f), 1.0f);

    // Same XY as the wall midpoint but well below the Z band → a different level is
    // not this hazard, so it is untaxed.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(543, -1351.55f, 1656.98f, 50.0f), 1.0f);

    // Geometrically on the wall, but a different map → no region applies.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(0, -1351.55f, 1656.98f, 68.46f), 1.0f);
}

TEST(DcNavPenaltyRegistry, RampartsStripLeavesNoWalkableFloorBehindIt)
{
    // Regression: the strip used to be the measured line inflated ±2yd, but that
    // line is a straight chord across a navmesh edge that BOWS away from it, so
    // the middle of the strip ran 2-3yd inboard of the real drop-off and marooned
    // ≈29 sq yd of ordinary room floor between the strip and the cliff — floor a
    // party can stand on, reachable only by crossing a hard-reject region.
    //
    // (-1349.00, 1662.00) is in that pocket: real navmesh floor at z 68.70,
    // measured 2.5yd on the far side of the chord. It must be INSIDE the strip
    // now — not because the party should never be there, but so that "behind the
    // strip" is over the drop everywhere and there is no pocket left to be cut
    // off in the first place.
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(543, -1349.0f, 1662.0f, 68.70f), 1.0f);

    // ...and so is the deepest floor the pocket reached: (-1353.57, 1662.63) at
    // z 68.61, 5.75yd out, the furthest any walkable sample gets from the chord.
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(543, -1353.57f, 1662.63f, 68.61f), 1.0f);

    // The far boundary is 10yd out — past any floor, over the drop — so a point
    // beyond THAT is off the mesh entirely and needs no fencing.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(543, -1358.48f, 1666.77f, 68.46f), 1.0f);

    // The zone-in point itself is well clear of the strip and stays untaxed —
    // an ordinary start must route normally.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(543, -1355.24f, 1641.12f, 68.25f), 1.0f);
}

TEST(DcNavPenaltyRegistry, IsInsideRegionAgreesWithPenaltyAt)
{
    // The named predicate both consumers ask "is the party standing in a fence?"
    // with. It must be exactly "PenaltyAt says taxed", on every kind of row.
    EXPECT_TRUE(DcNavPenaltyRegistry::IsInsideRegion(543, -1351.55f, 1656.98f, 68.46f));  // polygon
    EXPECT_TRUE(DcNavPenaltyRegistry::IsInsideRegion(229, -126.1f, -390.3f, 44.4f));      // box
    EXPECT_TRUE(DcNavPenaltyRegistry::IsInsideRegion(389, -271.13f, -20.04f, -57.4f));    // RFC wall

    EXPECT_FALSE(DcNavPenaltyRegistry::IsInsideRegion(543, -1355.24f, 1641.12f, 68.25f)); // clear floor
    EXPECT_FALSE(DcNavPenaltyRegistry::IsInsideRegion(543, -1351.55f, 1656.98f, 50.0f));  // wrong Z
    EXPECT_FALSE(DcNavPenaltyRegistry::IsInsideRegion(0, -1351.55f, 1656.98f, 68.46f));   // no rows
}

// A fence says where routes may GO. It must never cage a party that is already
// standing inside one — which happens for real: the Ramparts strip covers ordinary
// room floor a few yards from where players zone in, so "started the run on the
// wrong side of the invisible wall" is a routine start, and taxing the way out at
// 40x is what sends the tank round the far side of the room instead.
TEST(DcRouteFilterTest, FenceIsLiveForAnOrdinaryStart)
{
    // Map with rows, start on clear floor → the fence applies as before.
    DcRouteFilter const filter(543, -1355.24f, 1641.12f, 68.25f);
    EXPECT_TRUE(filter.IsFenceActive());
}

TEST(DcRouteFilterTest, FenceStandsDownWhenTheRouteStartsInsideIt)
{
    // Start inside the Ramparts strip → the fence is off for this query, so the
    // A* can cost the way out at face value and leave.
    DcRouteFilter const rampart(543, -1351.55f, 1656.98f, 68.46f);
    EXPECT_FALSE(rampart.IsFenceActive());

    // Same for a box row (LBRS chasm) and for the RFC funnel wall — the rule is a
    // property of the registry, not of one hand-authored spot.
    DcRouteFilter const lbrs(229, -126.1f, -390.3f, 44.4f);
    EXPECT_FALSE(lbrs.IsFenceActive());

    DcRouteFilter const rfc(389, -271.13f, -20.04f, -57.4f);
    EXPECT_FALSE(rfc.IsFenceActive());
}

TEST(DcRouteFilterTest, MapsWithoutRowsNeverArmTheFence)
{
    // No rows on the map → nothing to test per edge, whatever the start.
    DcRouteFilter const none(0, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(none.IsFenceActive());
}

TEST(DcNavPenaltyRegistry, FencesTheRagefireChasmFunnelWall)
{
    // The four measured points the wall is drawn through. Each must be taxed —
    // including the two outer ends, which is why every leg is extended past its
    // endpoints instead of terminating exactly on them (a point sitting on the
    // polygon's boundary edge is ill-defined for the even-odd test).
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(389, -283.38f, -37.05f, -58.46f), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(389, -277.23f, -22.82f, -58.18f), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(389, -265.03f, -17.26f, -56.65f), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(389, -238.73f, -22.41f, -58.18f), 1.0f);

    // The midpoint of each leg — the wall is continuous along its whole length,
    // not just at the authored corners.
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(389, -280.31f, -29.94f, -58.3f), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(389, -271.13f, -20.04f, -57.4f), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(389, -251.88f, -19.84f, -57.4f), 1.0f);

    // Six yards off the wall on either side (measured along leg 3's perpendicular):
    // the strip is thin, so open floor either side of it stays untaxed. This is the
    // point of a polygon here — leg 3's bounding box alone would be ~28x6yd and
    // would swallow floor the party legitimately uses.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(389, -253.03f, -25.72f, -58.0f), 1.0f);
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(389, -250.73f, -13.95f, -58.0f), 1.0f);

    // Same, off leg 1 — and far enough from leg 2 that the bend's overlap does not
    // reach it either.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(389, -274.80f, -32.32f, -58.0f), 1.0f);

    // On the wall in XY but well outside the Z band → a different level is not
    // this wall, so it is untaxed.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(389, -271.13f, -20.04f, -20.0f), 1.0f);

    // The instance's own start position is nowhere near the wall.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(389, 3.81f, -14.82f, -17.84f), 1.0f);

    // Geometrically on the wall, but a different map → no region applies.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(0, -271.13f, -20.04f, -57.4f), 1.0f);
}

TEST(DcNavPenaltyRegistry, PenalizesInsideTheLbrsShaft)
{
    // The midpoint of the observed shortcut climb
    //   [-127.33,-402.11,30.32] -> [-124.88,-378.42,58.40]
    // is ≈(-126.1,-390.3,44.4): squarely inside the box, so it must be taxed.
    float const p = DcNavPenaltyRegistry::PenaltyAt(229, -126.1f, -390.3f, 44.4f);
    EXPECT_GT(p, 1.0f);
}

TEST(DcNavPenaltyRegistry, PenalizesInsideTheLbrsLedgeHop)
{
    // The midpoint of the second (small) shortcut
    //   [-61.70,-382.77,48.88] <-> [-64.34,-378.49,54.70]
    // is ≈(-63.0,-380.6,51.8): inside box #2's mid-Z band, so it is taxed.
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(229, -63.0f, -380.6f, 51.8f), 1.0f);
    // The lower walkway end (z below the band) is the legit approach — untaxed.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(229, -61.7f, -382.77f, 48.88f), 1.0f);
    // The upper platform end (z above the band), reached by the proper route from
    // another direction — untaxed.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(229, -64.34f, -378.49f, 54.7f), 1.0f);
}

TEST(DcNavPenaltyRegistry, PenalizesTheUnderbogShortcut)
{
    // Both observed shortcut endpoints, and their midpoint, fall inside the box
    // that spans the whole wide-open run — all taxed.
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(546, 35.17f, -364.37f, 27.57f), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(546, 66.6f, -357.99f, 33.77f), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(546, 50.9f, -361.2f, 30.7f), 1.0f);
    // Well outside the box on X → untaxed.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(546, 90.0f, -361.0f, 30.0f), 1.0f);
    // Below the box's Z floor → the legit floor beneath the climb is untaxed.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(546, 50.0f, -361.0f, 15.0f), 1.0f);
    // Inside the box geometrically, but a different map → no volume applies.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(0, 50.9f, -361.2f, 30.7f), 1.0f);
}

TEST(DcNavPenaltyRegistry, DoesNotPenalizeOutsideTheBox)
{
    // Same X/Y as the shaft but down on the lower floor (below the mid-Z band):
    // a route that legitimately belongs at the bottom must not be taxed.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(229, -126.0f, -390.0f, 30.0f), 1.0f);
    // Far away on the same map.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(229, 200.0f, 200.0f, 44.0f), 1.0f);
    // Inside the box geometrically, but a different map → no volume applies.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(0, -126.1f, -390.3f, 44.4f), 1.0f);
}
