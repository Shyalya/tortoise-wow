/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcZoneLine.h"

#include "DungeonClearMath.h"
#include "DungeonClearTuning.h"

#include "ObjectMgr.h"
#include "Player.h"

#include <cmath>
#include <unordered_map>
#include <vector>

namespace
{
    // Every map-changing areatrigger, bucketed by the map it stands ON.
    //
    // Built once, lazily, and held BY VALUE. By value because the pointers
    // ObjectMgr hands out live in an unordered_map that a `.reload` would
    // rehash out from under us, and an AreaTrigger is ten scalars — copying 272
    // of them once is cheaper than reasoning about that lifetime. Lazily
    // because the stores are only populated during world load, and this header
    // is reachable from static-init order we do not control. Both stores are
    // read-only afterwards, so the snapshot never needs invalidating and the
    // predicates need no lock (function-local static init is thread-safe, and
    // bots on different maps tick on different threads).
    using ZoneLineIndex = std::unordered_map<uint32, std::vector<AreaTrigger>>;

    ZoneLineIndex const& Index()
    {
        static ZoneLineIndex const index = []
        {
            ZoneLineIndex built;
            for (auto const& [triggerId, teleport] : sObjectMgr.GetAllAreaTriggerTeleports())
            {
                AreaTrigger const* at = sObjectMgr.GetAreaTrigger(triggerId);
                if (!at)
                    continue;

                // A trigger that lands you back on the map you are already on is
                // an in-instance hop (a wing shortcut), not a way out of the run.
                if (teleport.destination.mapId == at->map)
                    continue;

                // Degenerate row: no sphere and no box. Core's
                // IsInAreaTriggerRadius can never report a hit on it, so neither
                // should we — padding a zero-sized box would invent an 8yd
                // keep-out around a trigger that does not exist in game.
                if (at->radius <= 0.0f && at->length <= 0.0f && at->width <= 0.0f &&
                    at->height <= 0.0f)
                    continue;

                built[at->map].push_back(*at);
            }
            return built;
        }();
        return index;
    }

    // Rotate (x,y) into the box's own frame, exactly as Position::IsWithinBox
    // does: the point is rotated by -orientation about the centre so the box can
    // be tested as an axis-aligned one. Ingame orientation is counter-clockwise,
    // hence the 2*PI - o.
    void ToBoxLocal(AreaTrigger const& at, float x, float y, float& lx, float& ly)
    {
        double const rotation = 2.0 * static_cast<double>(DC_PI) - static_cast<double>(at.orientation);
        double const sinVal = std::sin(rotation);
        double const cosVal = std::cos(rotation);

        double const dx = static_cast<double>(x) - static_cast<double>(at.x);
        double const dy = static_cast<double>(y) - static_cast<double>(at.y);

        lx = static_cast<float>(dx * cosVal - dy * sinVal);
        ly = static_cast<float>(dy * cosVal + dx * sinVal);
    }

    // Half the volume's vertical reach — the Z band a point has to be inside for
    // the XY test to mean anything. Never padded (see the header): for a sphere
    // it is the radius, for a box half its height.
    float ZBand(AreaTrigger const& at)
    {
        return at.radius > 0.0f ? at.radius : at.height * 0.5f;
    }

    // True when the segment's two endpoints are both outside the Z band ON THE
    // SAME SIDE, i.e. the leg never reaches the volume's height at all. Two
    // endpoints out of band on OPPOSITE sides means the leg descends straight
    // through it (a ramp down past an entrance landing), so that case must NOT
    // be rejected here. Same shape as DcHazardRegistry::SegmentClips.
    bool SegmentMissesZBand(AreaTrigger const& at, float az, float bz)
    {
        float const band = ZBand(at);
        float const da = az - at.z;
        float const db = bz - at.z;
        return std::fabs(da) > band && std::fabs(db) > band && (da > 0.0f) == (db > 0.0f);
    }

    // Walk this map's zone lines, calling `test(trigger)` on each. False (no
    // trigger touched) on a map with no rows, on a null bot, or on a bot with no
    // map — the early-out every public predicate shares.
    template <typename Test>
    bool AnyZoneLine(Player* bot, Test&& test)
    {
        if (!bot)
            return false;

        ZoneLineIndex const& index = Index();
        auto const itr = index.find(bot->GetMapId());
        if (itr == index.end())
            return false;

        for (AreaTrigger const& at : itr->second)
            if (test(at))
                return true;

        return false;
    }
}

bool DcZoneLine::PointInVolume(AreaTrigger const& at, float x, float y, float z, float margin)
{
    if (std::fabs(z - at.z) > ZBand(at))
        return false;

    float const dx = x - at.x;
    float const dy = y - at.y;

    if (at.radius > 0.0f)
    {
        float const reach = at.radius + margin;
        return dx * dx + dy * dy <= reach * reach;
    }

    float lx;
    float ly;
    ToBoxLocal(at, x, y, lx, ly);
    return std::fabs(lx) <= at.length * 0.5f + margin &&
           std::fabs(ly) <= at.width * 0.5f + margin;
}

bool DcZoneLine::SegmentClipsVolume(AreaTrigger const& at, float ax, float ay, float az,
                                    float bx, float by, float bz, float margin)
{
    if (SegmentMissesZBand(at, az, bz))
        return false;

    if (at.radius > 0.0f)
    {
        float const reach = at.radius + margin;
        return DungeonClearMath::DistSqToSegment2D(at.x, at.y, ax, ay, bx, by) <= reach * reach;
    }

    // Rotate BOTH endpoints into the box frame and slab-clip against the padded
    // axis-aligned extents. Liang-Barsky (SegmentIntersectsAABB2D) catches the
    // case both endpoints are outside while the segment passes straight through
    // — the whole reason a leg test exists.
    float lax;
    float lay;
    float lbx;
    float lby;
    ToBoxLocal(at, ax, ay, lax, lay);
    ToBoxLocal(at, bx, by, lbx, lby);

    float const halfLength = at.length * 0.5f + margin;
    float const halfWidth = at.width * 0.5f + margin;
    return DungeonClearMath::SegmentIntersectsAABB2D(lax, lay, lbx, lby,
                                                     -halfLength, -halfWidth,
                                                     halfLength, halfWidth);
}

bool DcZoneLine::MapHasZoneLines(uint32 mapId)
{
    ZoneLineIndex const& index = Index();
    return index.find(mapId) != index.end();
}

bool DcZoneLine::PointIsOverTheLine(Player* bot, float x, float y, float z)
{
    return AnyZoneLine(bot, [&](AreaTrigger const& at)
    {
        return PointInVolume(at, x, y, z, CampMargin);
    });
}

bool DcZoneLine::LegCrossesTheLine(Player* bot, float x, float y, float z)
{
    return AnyZoneLine(bot, [&](AreaTrigger const& at)
    {
        return SegmentClipsVolume(at,
                                  bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                  x, y, z, WalkMargin);
    });
}

bool DcZoneLine::WouldCrossTheLine(Player* bot, float x, float y, float z)
{
    // One pass over the map's triggers, both tests per trigger: the point gate
    // and the leg gate reject for the same reason and are never asked
    // separately at a placement site.
    return AnyZoneLine(bot, [&](AreaTrigger const& at)
    {
        return PointInVolume(at, x, y, z, CampMargin) ||
               SegmentClipsVolume(at,
                                  bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                  x, y, z, WalkMargin);
    });
}
