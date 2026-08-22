/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearBlockingDoorValue.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "DBCStores.h"
#include "DBCStructure.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Ai/Dungeon/DungeonClear/Data/DcEventDoorRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcDoorIndex.h"
#include "Ai/Dungeon/DungeonClear/Util/DcEngageGeometry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

namespace
{
    // How far ahead along the corridor to look for closed doors. Beyond
    // this distance we don't care — the chunked pathfinder will rebuild
    // by the time the bot gets there and the value will re-evaluate.
    constexpr float DOOR_LOOK_AHEAD = 80.0f;
    // Lateral slack added around the door's real model footprint before testing
    // whether a path leg passes through it. Covers the bot's body radius plus
    // the gap between the smoothed route centerline and the line the bot
    // actually walks / navmesh snapping. Small on purpose: the footprint box
    // already spans the true doorway (wide gate or narrow door alike), so this
    // is only tolerance, not a "is the door roughly near the path" band.
    constexpr float DOOR_PATH_PAD = 2.0f;
    // Vertical tolerance on top of the door model's own Z extent. A door whose
    // footprint doesn't overlap the leg's height (plus this) sits on another
    // floor (a stacked deck, a ramp over/under us) and is not blocking, even
    // when its 2D footprint lands under the path. Mirrors DC_DOOR_Z_BAND.
    constexpr float DOOR_Z_BAND = 6.0f;

    // Transform a world point into the door's local (orientation-aligned) frame,
    // matching GameObject::IsInRange2d. The door's model bounding box
    // (GameObjectDisplayInfo min/max, scaled) is axis-aligned in THIS frame, so
    // once both leg endpoints are transformed we can do a plain segment-vs-AABB
    // test against the real, oriented doorway footprint.
    inline void ToDoorLocal(GameObject const* go, float wx, float wy,
                            float& lx, float& ly)
    {
        float const sinA = std::sin(go->GetOrientation());
        float const cosA = std::cos(go->GetOrientation());
        float const ddx = wx - go->GetPositionX();
        float const ddy = wy - go->GetPositionY();
        lx = sinA * ddx + cosA * ddy;
        ly = cosA * ddx - sinA * ddy;
    }

    // The door's vertical extent in world Z: its model box scaled onto the GO's
    // position, padded by DOOR_Z_BAND.
    void DoorZSpan(GameObject const* go, GameObjectDisplayInfoEntry const* info,
                   float& loZ, float& hiZ)
    {
        float const scale = go->GetObjectScale();
        float const gz = go->GetPositionZ();
        loZ = gz + info->minZ * scale - DOOR_Z_BAND;
        hiZ = gz + info->maxZ * scale + DOOR_Z_BAND;
    }

    // True when a path leg shares the door's floor. A leg whose height span
    // misses the door's entirely runs over or under it (a stacked deck, a ramp,
    // the storey below a tower staircase) and cannot be transiting it, however
    // close the two look from above.
    inline bool LegSharesDoorFloor(float loZ, float hiZ, float az, float bz)
    {
        return std::max(az, bz) >= loZ && std::min(az, bz) <= hiZ;
    }

    // True when a path leg actually passes THROUGH this door's footprint, rather
    // than merely running near its origin. This is the whole point of the value:
    // doors a route runs PAST (e.g. Shadowfang Keep's first courtyard gate, which
    // you skirt to reach the prison event that unlocks it) must NOT be flagged —
    // only doors the corridor transits.
    bool PathLegCrossesDoor(GameObject const* go,
                            GameObjectDisplayInfoEntry const* info,
                            float ax, float ay, float az,
                            float bx, float by, float bz)
    {
        float const scale = go->GetObjectScale();
        // Floor / height gate first (cheap): the leg's Z span must overlap the
        // door's vertical extent, padded.
        float doorLoZ, doorHiZ;
        DoorZSpan(go, info, doorLoZ, doorHiZ);
        if (!LegSharesDoorFloor(doorLoZ, doorHiZ, az, bz))
            return false;

        float lax, lay, lbx, lby;
        ToDoorLocal(go, ax, ay, lax, lay);
        ToDoorLocal(go, bx, by, lbx, lby);

        float const minX = info->minX * scale - DOOR_PATH_PAD;
        float const maxX = info->maxX * scale + DOOR_PATH_PAD;
        float const minY = info->minY * scale - DOOR_PATH_PAD;
        float const maxY = info->maxY * scale + DOOR_PATH_PAD;
        return DungeonClearMath::SegmentIntersectsAABB2D(lax, lay, lbx, lby,
                                                         minX, minY, maxX, maxY);
    }

}

ObjectGuid DungeonClearBlockingDoorValue::Calculate()
{
    if (!bot || !bot->IsInWorld())
        return ObjectGuid::Empty;
    if (!DcRun::Of(context).enabled || DcRun::Of(context).paused)
        return ObjectGuid::Empty;

    Map* map = bot->FindMap();
    if (!map)
        return ObjectGuid::Empty;

    ChunkedPathfinder::Result const& path =
        AI_VALUE(ChunkedPathfinder::Result&, DcKey::LongPath);
    if (!path.reachable || path.segments.empty())
        return ObjectGuid::Empty;

    // Build a series of 2D segments tracing the corridor AHEAD of the bot,
    // clipped to DOOR_LOOK_AHEAD yards. Anything past the look-ahead doesn't
    // count — by the time the bot walks there, the value will re-evaluate.
    //
    // CRITICAL #1: chain the per-segment `polyline` points, NOT the segment
    // endpoints (`seg.ex/ey`). The primary producer (LongRangePathfinder)
    // emits the ENTIRE winding route as a single PathSegment whose ex/ey is
    // only the final endpoint (the boss) — so chaining endpoints collapses the
    // corridor to a straight bee-line from the bot to the boss. A bee-line
    // sweeps through rooms the real path never enters and flags any closed door
    // that happens to lie near that straight line, even one nowhere on the
    // route. The polyline is the real smoothed corridor geometry.
    //
    // CRITICAL #2: start at the bot's PROGRESS point on the polyline (nearest
    // vertex — the same cursor proxy DistAlongPathToClosedDoor uses), not at
    // the route's origin. The cached path was built from wherever Advance last
    // rebuilt, which can be far BEHIND a moving bot; chaining from the origin
    // both spent the look-ahead budget on the already-walked stretch and
    // synthesized a bot→origin bee-line through walls. Either one flagged
    // doors behind the bot — observed in Stratholme right after opening the
    // Baron-run service gate: a closed gate elsewhere got flagged, the panel
    // showed Blocked at an open gate, and the run wandered off after the
    // phantom blocker.
    //
    // CRITICAL #3: that cursor is picked in 3D (DungeonClearMath::
    // PathProgressCursor). A 2D pick snapped a storey UP in Shadowfang Keep's
    // tower — where the staircase to Nandos climbs directly over the Fenrus
    // room — and re-created exactly the bee-line-through-walls failure #2
    // describes, only vertically: the scan began on the landing overhead and
    // flagged Arugal's Lair 27yd above the tank. See the helper's comment.
    std::vector<G3D::Vector3> pts;
    for (PathSegment const& pathSeg : path.segments)
    {
        if (pathSeg.polyline.empty())
            pts.emplace_back(pathSeg.ex, pathSeg.ey, pathSeg.ez);
        else
            pts.insert(pts.end(), pathSeg.polyline.begin(), pathSeg.polyline.end());
    }
    if (pts.empty())
        return ObjectGuid::Empty;

    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();
    float const botZ = bot->GetPositionZ();
    size_t const cursor = DungeonClearMath::PathProgressCursor(pts, botX, botY, botZ);

    float prevX = botX;
    float prevY = botY;
    float prevZ = botZ;
    float accumulated = 0.0f;

    struct Seg { float ax, ay, az, bx, by, bz; };
    std::vector<Seg> segments;

    for (size_t i = cursor; i < pts.size(); ++i)
    {
        float const dx = pts[i].x - prevX;
        float const dy = pts[i].y - prevY;
        segments.push_back(Seg{prevX, prevY, prevZ, pts[i].x, pts[i].y, pts[i].z});
        accumulated += std::sqrt(dx * dx + dy * dy);
        prevX = pts[i].x;
        prevY = pts[i].y;
        prevZ = pts[i].z;
        if (accumulated >= DOOR_LOOK_AHEAD)
            break;
    }
    if (segments.empty())
        return ObjectGuid::Empty;

    // Generous cull radius: the door's footprint must be within the look-ahead
    // plus room for a large gate's half-extent. Anything past this can't be on
    // the first DOOR_LOOK_AHEAD yards of corridor.
    constexpr float CULL = DOOR_LOOK_AHEAD + 20.0f;
    ObjectGuid best;
    GameObject* bestGo = nullptr;
    float bestDistFromBotSq = std::numeric_limits<float>::max();

    // Iterate the cached door list (DcDoorIndex) rather than the whole GO store;
    // the shut-state check below is still read fresh per door.
    for (ObjectGuid const guid : DcDoorIndex::Get(map))
    {
        GameObject* go = map->GetGameObject(guid);
        // Blocking door currently shut (the "block the corridor" state). The
        // helper folds the type / ignoredByPathing / startOpen-inverted GOState
        // checks; see DcEngageGeometry::IsDoorClosed.
        if (!DcEngageGeometry::IsDoorClosed(go))
            continue;
        // Interact-THROUGH gates (Old Hillsbrad's prison door): the objective is
        // completed from the players' side of the shut door and the event opens
        // it itself — never flag one as blocking, never pause on it.
        if (DcEventDoorRegistry::IsNavigationIgnored(go->GetEntry()))
            continue;
        GameObjectTemplate const* tmpl = go->GetGOInfo();

        float const gx = go->GetPositionX();
        float const gy = go->GetPositionY();
        float const distFromBotSq = (gx - botX) * (gx - botX) + (gy - botY) * (gy - botY);
        if (distFromBotSq > CULL * CULL)
            continue;

        // Resolve the door's real model footprint. Without it we can't tell
        // "through" from "past"; fall back to nothing (don't flag) rather than
        // re-introduce the origin-proximity false positives this replaced.
        GameObjectDisplayInfoEntry const* disp =
            sGameObjectDisplayInfoStore.LookupEntry(tmpl->displayId);
        if (!disp)
        {
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] blocking-door: closed door {} (entry {}) has no display "
                      "footprint -> NOT flagged",
                      bot->GetName(), go->GetObjectGuid().ToString(), tmpl->id);
            continue;
        }

        // Flag ONLY when a route leg actually transits the doorway. A door the
        // corridor merely runs past is left alone, so the tank keeps moving
        // toward the boss / event that opens it instead of parking beside a door
        // it never enters.
        //
        // Two detectors, OR'd. (1) The GeoBox footprint (oriented AABB) — fast,
        // and the long-standing test. (2) GameObject-only LOS against the door's
        // REAL M2 collision — the footprint's modeled box can be rotated/offset
        // from where the navmesh actually threads the opening (SM Cathedral's
        // Chapel Door: the bot walked clean through because the footprint never
        // matched), but the real mesh blocks a route leg that crosses the shut
        // door. The navmesh is door-blind, so a leg that clears the static VMAP
        // yet is GameObject-LOS-blocked near THIS door is transiting its shut
        // collision. Needs CheckGameObjectLoS=1; an OPEN door has no collision so
        // it never flags. Proximity-gated to this door so a stray solid GO can't
        // attribute a block here — and that proximity gate is 2D, so it carries
        // the SAME floor test detector (1) applies. Without it, a leg on the
        // storey below a door (10yd of Z in Shadowfang's tower, well inside the
        // 12yd 2D band) that any unrelated solid GO happened to block got the
        // block attributed to a door it ran nowhere near.
        bool crosses = false;
        for (auto const& seg : segments)
        {
            if (PathLegCrossesDoor(go, disp, seg.ax, seg.ay, seg.az,
                                   seg.bx, seg.by, seg.bz))
            {
                crosses = true;
                break;
            }
        }
        if (!crosses)
        {
            constexpr float NEAR_SQ = 12.0f * 12.0f;
            float doorLoZ, doorHiZ;
            DoorZSpan(go, disp, doorLoZ, doorHiZ);
            for (auto const& seg : segments)
            {
                if (!LegSharesDoorFloor(doorLoZ, doorHiZ, seg.az, seg.bz))
                    continue;
                if (DungeonClearMath::DistSqToSegment2D(gx, gy, seg.ax, seg.ay,
                                                        seg.bx, seg.by) > NEAR_SQ)
                    continue;
                if (!map->isInLineOfSight(seg.ax, seg.ay, seg.az + 1.5f,
                                          seg.bx, seg.by, seg.bz + 1.5f,
                                          bot->GetPhaseMask(),
                                          LINEOFSIGHT_CHECK_GOBJECT,
                                          VMAP::ModelIgnoreFlags::Nothing))
                {
                    crosses = true;
                    break;
                }
            }
        }
        if (!crosses)
            continue;

        if (distFromBotSq < bestDistFromBotSq)
        {
            bestDistFromBotSq = distFromBotSq;
            best = go->GetObjectGuid();
            bestGo = go;
        }
    }

    // Announce at INFO whenever the flagged blocker CHANGES (including door →
    // different door, the signature of a phantom flag) so field reports can
    // name the exact GO without enabling debug logging. Steady-state repeats
    // stay at DEBUG.
    if (best != _lastFlagged)
    {
        _lastFlagged = best;
        // The blocker CHANGED — the corridor cleared, or a different door is on
        // it now. Either way the door-blocked action's blocked-state watchdog is
        // measuring a stall that has ended, so end its window here: the next
        // arrival park arms a fresh one. This is the guaranteed half of the fix
        // (the action's own reset only fires if the 500ms value cache happens to
        // still name a door the tick after it swings open); without it an
        // auto-closing gate accumulates its successful opens into one long
        // "stall" and auto-pauses the run. See DcApproachState::ClearDoorStall.
        context->GetValue<DcApproachState&>(DcKey::ApproachState)
            ->Get()
            .ClearDoorStall();
        if (bestGo)
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] blocking-door: flagged {} '{}' (entry {}) "
                     "{:.1f}yd from bot as corridor-blocking",
                     bot->GetName(), best.ToString(), bestGo->GetName(),
                     bestGo->GetEntry(), std::sqrt(bestDistFromBotSq));
        else
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] blocking-door: corridor clear (no blocking door)",
                     bot->GetName());
    }
    else if (!best.IsEmpty())
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] blocking-door: flagged {} as corridor-blocking",
                  bot->GetName(), best.ToString());
    return best;
}
