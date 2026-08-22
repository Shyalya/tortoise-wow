/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "ObjectMgr.h"

#include "Ai/Dungeon/DungeonClear/Util/DcZoneLine.h"

#include <cmath>

// Pure geometry tests for the instance zone-line keep-out. No ObjectMgr stores,
// no map data and no live game state — every case builds its AreaTrigger from
// the real `areatrigger` row it names, so a wrong sign or a swapped half-extent
// shows up as a concrete dungeon getting it wrong rather than an abstract
// failure.
//
// The bug being pinned: an advanced pull at the mouth of a dungeon drags the
// camp BACK along the only cleared ground there is — the entrance — and plants
// it on, or past, the exit trigger. A headless bot survives that (it sends no
// CMSG_AREATRIGGER); a self-bot's client reports it and the player is teleported
// out of the instance mid-run.

namespace
{
    // The Stockade's exit (areatrigger 503): a plain 8yd sphere sitting in the
    // entrance tunnel. The arrival point players zone in on is (54.23, 0.28,
    // -18.34) — only ~14.8yd from this centre, which is why the module's first
    // pull is the one that trips over it.
    AreaTrigger Stockade()
    {
        AreaTrigger at{};
        at.entry = 503;
        at.map = 34;
        at.x = 39.3741f;
        at.y = 0.803469f;
        at.z = -12.7883f;
        at.radius = 8.0f;
        return at;
    }

    // Magisters' Terrace exit (areatrigger 4885): an axis-aligned box, 3.432
    // along X by 16.14 along Y by 7.543 tall. The narrow axis is the one the
    // party walks through, so it is the one a margin has to widen.
    AreaTrigger MagistersTerrace()
    {
        AreaTrigger at{};
        at.entry = 4885;
        at.map = 585;
        at.x = -5.89453f;
        at.y = -0.123698f;
        at.z = -2.80323f;
        at.length = 3.432f;
        at.width = 16.14f;
        at.height = 7.543f;
        return at;
    }

    // The Shattered Halls exit (areatrigger 4145): a box ROTATED 1.466 rad, i.e.
    // ~84 degrees, so its long axis runs almost along +Y in world space. Any
    // implementation that forgot the rotation (or rotated the wrong way) passes
    // the two axis-aligned cases above and fails this one.
    AreaTrigger ShatteredHalls()
    {
        AreaTrigger at{};
        at.entry = 4145;
        at.map = 540;
        at.x = -41.748f;
        at.y = -32.4536f;
        at.z = -13.5294f;
        at.length = 10.69f;
        at.width = 7.667f;
        at.height = 15.42f;
        at.orientation = 1.466f;
        return at;
    }

    // Shadowfang Keep's "South Fall Target" (areatrigger 2406): NOT an entrance
    // at all but a fall-catcher — a 98.86 x 185.6 x 80.58 slab hung off the
    // keep's south edge to bounce anyone who falls off back to the world map. It
    // still teleports, so it is still a zone line; the thing that matters is that
    // its ceiling (z = 36.886 + 40.29 = 77.18) sits 2.6yd UNDER the courtyard
    // floor at z ~= 79.84. Pad that height and a third of the keep becomes
    // un-campable.
    AreaTrigger ShadowfangSouthFall()
    {
        AreaTrigger at{};
        at.entry = 2406;
        at.map = 33;
        at.x = -287.071f;
        at.y = 2175.64f;
        at.z = 36.8861f;
        at.length = 98.86f;
        at.width = 185.6f;
        at.height = 80.58f;
        at.orientation = 2.88f;
        return at;
    }
}

// --- Sphere volumes --------------------------------------------------------

TEST(DcZoneLine, SphereMatchesTheCoreRadiusAtZeroMargin)
{
    AreaTrigger const at = Stockade();

    // Dead centre, and just inside / just outside the 8yd rim along +X.
    EXPECT_TRUE(DcZoneLine::PointInVolume(at, at.x, at.y, at.z, 0.0f));
    EXPECT_TRUE(DcZoneLine::PointInVolume(at, at.x + 7.5f, at.y, at.z, 0.0f));
    EXPECT_FALSE(DcZoneLine::PointInVolume(at, at.x + 8.5f, at.y, at.z, 0.0f));
}

TEST(DcZoneLine, CampMarginPushesTheKeepOutPastTheStockadeArrivalPoint)
{
    AreaTrigger const at = Stockade();

    // Where the party materialises on zone-in (areatrigger_teleport 101). It is
    // 14.8yd from the trigger centre: clear of the 8yd sphere itself, but well
    // inside the 8yd camp margin — so a camp is refused here and the planner
    // falls back to camping where the tank stands, which is the whole point.
    float const arrivalX = 54.23f;
    float const arrivalY = 0.28f;
    float const arrivalZ = -18.34f;

    EXPECT_FALSE(DcZoneLine::PointInVolume(at, arrivalX, arrivalY, arrivalZ, 0.0f));
    EXPECT_TRUE(DcZoneLine::PointInVolume(at, arrivalX, arrivalY, arrivalZ,
                                          DcZoneLine::CampMargin));
}

TEST(DcZoneLine, SphereZReachIsTheRadiusAndIsNeverPadded)
{
    AreaTrigger const at = Stockade();

    // Directly overhead: inside the sphere's vertical reach, then past it. The
    // margin must not extend that reach, or a camp on the floor above a stacked
    // entrance reads as over the line.
    EXPECT_TRUE(DcZoneLine::PointInVolume(at, at.x, at.y, at.z + 7.0f, 0.0f));
    EXPECT_FALSE(DcZoneLine::PointInVolume(at, at.x, at.y, at.z + 9.0f, 0.0f));
    EXPECT_FALSE(DcZoneLine::PointInVolume(at, at.x, at.y, at.z + 9.0f,
                                           DcZoneLine::CampMargin));
}

// --- Box volumes -----------------------------------------------------------

TEST(DcZoneLine, AxisAlignedBoxUsesHalfExtents)
{
    AreaTrigger const at = MagistersTerrace();

    EXPECT_TRUE(DcZoneLine::PointInVolume(at, at.x, at.y, at.z, 0.0f));
    // Half-length is 1.716 along X, half-width 8.07 along Y.
    EXPECT_TRUE(DcZoneLine::PointInVolume(at, at.x + 1.5f, at.y + 7.5f, at.z, 0.0f));
    EXPECT_FALSE(DcZoneLine::PointInVolume(at, at.x + 2.5f, at.y, at.z, 0.0f));
    EXPECT_FALSE(DcZoneLine::PointInVolume(at, at.x, at.y + 8.5f, at.z, 0.0f));

    // The MGT arrival point (7.09, -0.45, -2.8) is 11.3yd clear of the box's
    // near face — outside even with the camp margin, so MGT's first pull keeps a
    // real camp.
    EXPECT_FALSE(DcZoneLine::PointInVolume(at, 7.09f, -0.45f, -2.8f, DcZoneLine::CampMargin));
}

TEST(DcZoneLine, RotatedBoxFollowsItsOwnAxesNotTheWorldAxes)
{
    AreaTrigger const at = ShatteredHalls();

    EXPECT_TRUE(DcZoneLine::PointInVolume(at, at.x, at.y, at.z, 0.0f));

    // The box is rotated ~84 degrees, so its LONG axis (half-length 5.345) runs
    // nearly along world +Y and its short axis (half-width 3.834) nearly along
    // world +X. A 5yd offset therefore lands INSIDE along Y and OUTSIDE along X
    // — the exact pair a rotation-blind test gets backwards.
    EXPECT_TRUE(DcZoneLine::PointInVolume(at, at.x, at.y + 5.0f, at.z, 0.0f));
    EXPECT_FALSE(DcZoneLine::PointInVolume(at, at.x + 5.0f, at.y, at.z, 0.0f));
}

TEST(DcZoneLine, BoxHeightIsNeverPaddedSoFallSlabsStayUnderTheFloor)
{
    AreaTrigger const at = ShadowfangSouthFall();

    // A Shadowfang courtyard spawn (Deathstalker at -227.76, 2234.48, 79.77)
    // sits 2.6yd above the slab's ceiling and squarely over its footprint.
    // Padding Z would swallow it; padding XY only leaves it alone.
    EXPECT_FALSE(DcZoneLine::PointInVolume(at, -227.76f, 2234.48f, 79.7656f, 0.0f));
    EXPECT_FALSE(DcZoneLine::PointInVolume(at, -227.76f, 2234.48f, 79.7656f,
                                           DcZoneLine::CampMargin));

    // Drop the same XY down into the slab's own height band and it IS inside —
    // proving the rejection above was the Z gate doing its job, not the
    // footprint missing.
    EXPECT_TRUE(DcZoneLine::PointInVolume(at, -227.76f, 2234.48f, 40.0f, 0.0f));
}

// --- Segment (walk) tests --------------------------------------------------

TEST(DcZoneLine, SegmentCatchesACampBEYONDTheLineWithBothEndsClear)
{
    AreaTrigger const at = Stockade();

    // The shape the bug reports: the tank stands 20yd inside, the camp candidate
    // sits 20yd out the other side. Neither endpoint is in the sphere, so a
    // point-only gate waves it through — and the party walks straight through
    // the zone line to get there.
    float const inX = at.x + 20.0f;
    float const outX = at.x - 20.0f;

    EXPECT_FALSE(DcZoneLine::PointInVolume(at, inX, at.y, at.z, 0.0f));
    EXPECT_FALSE(DcZoneLine::PointInVolume(at, outX, at.y, at.z, 0.0f));
    EXPECT_TRUE(DcZoneLine::SegmentClipsVolume(at, inX, at.y, at.z,
                                               outX, at.y, at.z, 0.0f));
}

TEST(DcZoneLine, SegmentPassingWideOfTheVolumeIsClean)
{
    AreaTrigger const at = Stockade();

    // Same in-to-out span, offset 30yd to the side: it never enters the sphere,
    // and the thin WalkMargin must not invent a hit. Walking NEAR a zone line is
    // harmless; only entering it teleports.
    EXPECT_FALSE(DcZoneLine::SegmentClipsVolume(at, at.x + 20.0f, at.y + 30.0f, at.z,
                                                at.x - 20.0f, at.y + 30.0f, at.z,
                                                DcZoneLine::WalkMargin));
}

TEST(DcZoneLine, SegmentThroughARotatedBoxIsCaught)
{
    AreaTrigger const at = ShatteredHalls();

    // Cross the box along its SHORT axis (world +/-X), both endpoints well
    // outside. This is the leg a drag-back to the Shattered Halls entrance walks.
    EXPECT_TRUE(DcZoneLine::SegmentClipsVolume(at, at.x + 15.0f, at.y, at.z,
                                               at.x - 15.0f, at.y, at.z, 0.0f));

    // The same crossing pushed 20yd along +Y is past the long axis' 5.345yd
    // half-length, so it misses the box entirely.
    EXPECT_FALSE(DcZoneLine::SegmentClipsVolume(at, at.x + 15.0f, at.y + 20.0f, at.z,
                                                at.x - 15.0f, at.y + 20.0f, at.z, 0.0f));
}

TEST(DcZoneLine, SegmentEntirelyAboveTheZBandMisses)
{
    AreaTrigger const at = ShadowfangSouthFall();

    // A walk across the Shadowfang courtyard between two spawns that BOTH sit
    // over the slab's footprint, 2.6yd above its ceiling the whole way. Both
    // endpoints out of band on the SAME side => clean.
    EXPECT_FALSE(DcZoneLine::SegmentClipsVolume(at, -227.76f, 2234.48f, 79.7656f,
                                                -217.135f, 2247.13f, 79.8579f,
                                                DcZoneLine::WalkMargin));

    // Same XY span dropped into the slab's height band clips it, so the miss
    // above was the Z gate and not the footprint.
    EXPECT_TRUE(DcZoneLine::SegmentClipsVolume(at, -227.76f, 2234.48f, 40.0f,
                                               -217.135f, 2247.13f, 40.0f,
                                               DcZoneLine::WalkMargin));
}

TEST(DcZoneLine, SegmentDescendingThroughTheZBandIsCaught)
{
    AreaTrigger const at = Stockade();

    // Both endpoints are out of the sphere's vertical reach but on OPPOSITE
    // sides, so the leg passes through the trigger's height on the way down. The
    // cheap "both out of band => clean" shortcut would miss it.
    EXPECT_TRUE(DcZoneLine::SegmentClipsVolume(at, at.x, at.y, at.z + 20.0f,
                                               at.x, at.y, at.z - 20.0f, 0.0f));
}

TEST(DcZoneLine, MarginsKeepTheirIntendedOrdering)
{
    // A camp anchor buys the whole AoE step-out width; a walk only buys the slop
    // between our straight segment and the navmesh path actually taken. Swap
    // them and every candidate whose approach merely skirts the entrance is
    // vetoed, which would strand the party at the tank on far more than the
    // first pull.
    EXPECT_GT(DcZoneLine::CampMargin, DcZoneLine::WalkMargin);
    EXPECT_GT(DcZoneLine::WalkMargin, 0.0f);
}
