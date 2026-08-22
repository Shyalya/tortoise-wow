/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearGeometry.h"

#include <cmath>

#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "DetourExtended.h"   // dtQueryFilterExt
#include "Map.h"
#include "MapDefines.h"       // NAV_WATER, NAV_MAGMA
#include "ModelIgnoreFlags.h"
#include "ObjectGuid.h"
#include "Player.h"

namespace
{
    // Vertical bump for the LOS raycast so floor-touching points don't
    // false-fail on slope/stair transitions. Roughly player eye height.
    constexpr float LOS_Z_BUMP = 1.5f;

    // Hops below this distance get skipped — Detour's smoothing puts very short
    // hops around concave corners that are guaranteed to LOS-pass and would only
    // burn raycasts.
    constexpr float LOS_MIN_HOP = 3.0f;

    // Longest run of consecutive blocked chords treated as a benign corner graze
    // rather than a real obstruction. A sharp convex bend makes the ~4yd smoothed
    // secants clip a wall corner for one to a few points before the corridor
    // straightens; the navmesh routed safely around it and the follower re-paths
    // each short hop, so these never produce an actual wall-clip.
    constexpr std::size_t LOS_GRAZE_BRIDGE = 3;
}

namespace DungeonClearGeometry
{
    float Dist2D(float ax, float ay, float bx, float by)
    {
        float const dx = bx - ax;
        float const dy = by - ay;
        return std::sqrt(dx * dx + dy * dy);
    }

    float Dist3D(float ax, float ay, float az, float bx, float by, float bz)
    {
        float const dx = bx - ax;
        float const dy = by - ay;
        float const dz = bz - az;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    bool ChordClear(Player* bot, G3D::Vector3 const& a, G3D::Vector3 const& b)
    {
        if (!bot)
            return true;
        float const dx = b.x - a.x;
        float const dy = b.y - a.y;
        float const dz = b.z - a.z;
        if ((dx * dx + dy * dy + dz * dz) < (LOS_MIN_HOP * LOS_MIN_HOP))
            return true;  // Detour smoothing artifact, too short to matter
        Map const* map = bot->GetMap();
        if (!map)
            return true;  // no map data — don't block routing
        return map->isInLineOfSight(a.x, a.y, a.z + LOS_Z_BUMP,
                                    b.x, b.y, b.z + LOS_Z_BUMP,
                                    bot->GetPhaseMask(), LINEOFSIGHT_CHECK_VMAP,
                                    VMAP::ModelIgnoreFlags::Nothing);
    }

    std::size_t LosCleanPrefixCount(Player* bot, std::vector<G3D::Vector3> const& pts)
    {
        if (!bot || pts.size() < 2)
            return pts.size();
        if (!bot->GetMap())
            return pts.size();

        std::size_t committed = 0;       // highest index reachable via a clean corridor
        std::size_t consecBlocked = 0;   // length of the current blocked-chord run
        for (std::size_t k = 0; k + 1 < pts.size(); ++k)
        {
            if (ChordClear(bot, pts[k], pts[k + 1]))
            {
                committed = k + 1;
                consecBlocked = 0;
            }
            else if (++consecBlocked > LOS_GRAZE_BRIDGE)
            {
                break;  // sustained obstruction — clean corridor ends at committed
            }
            // else: within the graze tolerance; don't commit past it until a
            // clear chord confirms the corridor reconnected.
        }
        return committed + 1;
    }

    void ApplyLiquidAreaCosts(dtQueryFilterExt& filter)
    {
        // dtQueryFilterExt::getCost multiplies each edge by getAreaCost(area), and
        // the mmap generator stamps liquid polys with area == the NavTerrain value
        // (NAV_WATER / NAV_MAGMA), so these indices line up 1:1 with poly areas.
        // NAV_GROUND keeps its default 1.0 cost. Server-only conf reads — safe off
        // the map thread.
        filter.setAreaCost(NAV_WATER, DcSettings::GetFloat(ObjectGuid::Empty, "WaterPathCost"));
        filter.setAreaCost(NAV_MAGMA, DcSettings::GetFloat(ObjectGuid::Empty, "MagmaPathCost"));
    }
}
