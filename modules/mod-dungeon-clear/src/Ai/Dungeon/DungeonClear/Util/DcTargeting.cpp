/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include "DungeonClearUtil.h"   // DcEngageGeometry:: cross-unit calls + DC_PULL_* macros

#include "DcDoorIndex.h"
#include "DungeonClearMath.h"
#include "DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "AttackersValue.h"
#include "CellImpl.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "ItemTemplate.h"
#include "LootMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "InstanceScript.h"
#include "LootObjectStack.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "Chat.h"
#include "ServerFacade.h"
#include "Timer.h"
#include "World.h"
#include "Ai/Dungeon/DungeonClear/Data/BossPullbackRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DcNeverTargetRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/RoomAggroRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearLiveBossValue.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

namespace
{
    // A line segment of the lookahead corridor (a→b). The band test is 2D (XY
    // projection) but the endpoint Z is carried so PickBlockingTrash can reject
    // a candidate on a different vertical level than the leg it matched — a mob
    // at the bottom of a ledge sits under the route in plan view but is NOT
    // blocking the legs that run along the top. Both corridor-trash scanners
    // build a flat list of these and hand it to PickBlockingTrash.
    struct Seg2D { float ax, ay, az, bx, by, bz; };

    // DC_DOOR_BAND, DC_DOOR_Z_BAND, DC_Z_LEVEL_TOLERANCE now live in
    // DungeonClearTuning.h (shared across the split util units).

    // A closed door reduced to its GO-origin point, carried with Z so the
    // corridor tests can reject doors on another floor.
    struct DoorPt { float x, y, z; };

    // Pull-target scan envelope. kPullLookAhead is the same look-ahead the
    // blocking-trash trigger uses, so the pull aims at the very pack the normal
    // flow would otherwise walk in to. kPullStickySlack is the extra distance a
    // STICKY pull target (DungeonClearPullTargetValue) may drift past the
    // look-ahead before it is released for a fresh scan — packs patrol while
    // approached, and releasing right on the scan boundary would reintroduce
    // the target flapping the sticky latch exists to remove.
    constexpr float kPullLookAhead = 35.0f;
    constexpr float kPullStickySlack = 10.0f;

    // Closed `GAMEOBJECT_TYPE_DOOR`s on the map, culled to within `radius` of
    // (cx,cy). Used to truncate the corridor trash scan at the first door so the
    // tank never picks a pack on the FAR side of a shut door — some doors aren't
    // modeled as solid in the navmesh, so the corridor runs straight through
    // them. Mirrors the closed-state logic in DungeonClearBlockingDoorValue.
    std::vector<DoorPt> CollectClosedDoors(Map* map, float cx, float cy,
                                           float radius)
    {
        std::vector<DoorPt> doors;
        if (!map)
            return doors;
        float const r2 = radius * radius;
        // Walk the cached door-GUID list (DcDoorIndex), not the entire map
        // GameObject store. The store holds hundreds-to-thousands of GOs
        // (chests/traps/doodads) and this runs on every path-trash scan tick; the
        // index is the dozen-or-two door GOs, rebuilt only when it ages out or the
        // store size changes. Shut-state is still read FRESH per door via
        // IsDoorClosed (GOState), so door freshness is identical to the full-store
        // walk — the index caches only "which GOs are doors". Mirrors the
        // DcEngageGeometry door predicates, which already consume the index.
        for (ObjectGuid const guid : DcDoorIndex::Get(map))
        {
            GameObject* go = map->GetGameObject(guid);
            if (!DcEngageGeometry::IsDoorClosed(go))
                continue;
            float const gx = go->GetPositionX();
            float const gy = go->GetPositionY();
            if ((gx - cx) * (gx - cx) + (gy - cy) * (gy - cy) > r2)
                continue;
            doors.push_back(DoorPt{gx, gy, go->GetPositionZ()});
        }
        return doors;
    }

    // True if any door lies within DC_DOOR_BAND (2D) of segment a→b AND on the
    // segment's floor (door Z within DC_DOOR_Z_BAND of the segment's Z span).
    // The Z gate stops a door one deck up/down — near in plan-view but not on
    // this leg of the route — from truncating the scan.
    bool SegmentHitsClosedDoor(Seg2D const& s, std::vector<DoorPt> const& doors)
    {
        float const bandSq = DC_DOOR_BAND * DC_DOOR_BAND;
        float const loZ = std::min(s.az, s.bz) - DC_DOOR_Z_BAND;
        float const hiZ = std::max(s.az, s.bz) + DC_DOOR_Z_BAND;
        for (auto const& d : doors)
        {
            if (d.z < loZ || d.z > hiZ)
                continue;
            if (DungeonClearMath::DistSqToSegment2D(d.x, d.y,
                                                    s.ax, s.ay, s.bx, s.by) <= bandSq)
                return true;
        }
        return false;
    }

    // Shared candidate evaluation for the corridor-style trash scans. Returns
    // the closest hostile, alive, level-reachable, in-LOS unit whose 2D
    // position lands within its OWN effective aggro band of any segment in
    // `segs`. Nullptr if none.
    //
    // `corridorWidth` is the band used for every candidate when
    // DungeonClear.DynamicAggroRange is off (legacy fixed-width behaviour).
    // With it on, each candidate's band is its real aggro range (clamped to
    // [DungeonClear.TrashWidthFloor, DungeonClear.TrashWidthCap] via
    // AggroRangeOf) and the AABB cull is padded by that cap: a giant elite with
    // a large aggro radius now counts as blocking even when it sits several
    // yards off the route line, while a low-aggro ambient mob just off the line
    // no longer does — the two failure modes the old single fixed band had.
    //
    // LOS (and level-reachability) are checked PER CANDIDATE rather than once
    // on the final pick: a geometrically closer mob behind a wall must not
    // shadow a visible blocker further along the corridor — otherwise the tank
    // walks right past the visible pack and aggros it onto the healers.
    Unit* PickBlockingTrash(Player* bot,
                            std::vector<Seg2D> const& segs,
                            float corridorWidth,
                            GuidVector const& candidates)
    {
        if (!bot || segs.empty() || candidates.empty())
            return nullptr;

        bool const dynamic =
            DcSettings::GetBool(bot, "DynamicAggroRange");
        float const widthFloor =
            DcSettings::GetFloat(bot, "TrashWidthFloor");
        float const widthCap =
            DcSettings::GetFloat(bot, "TrashWidthCap");

        // Broad-phase band: the widest per-candidate band we might use. With
        // dynamic aggro on it must reach the cap so a giant elite's large band
        // isn't culled by the AABB before its per-candidate test runs; off, it
        // stays at the caller's legacy fixed width.
        float const broadWidth = dynamic ? std::max(corridorWidth, widthCap) : corridorWidth;

        // 2D AABB around the lookahead corridor, padded by the broad-phase
        // width. Units nowhere near the route get rejected with four float
        // compares before any per-segment distance math runs.
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        for (Seg2D const& s : segs)
        {
            minX = std::min({minX, s.ax, s.bx});
            maxX = std::max({maxX, s.ax, s.bx});
            minY = std::min({minY, s.ay, s.by});
            maxY = std::max({maxY, s.ay, s.by});
        }
        minX -= broadWidth; maxX += broadWidth;
        minY -= broadWidth; maxY += broadWidth;

        Unit* best = nullptr;
        float bestDistFromBot = std::numeric_limits<float>::max();
        // Scan-cost telemetry for the heroic band widening (plan §C.4): how many
        // candidates actually landed in a corridor band this scan (i.e. survived
        // the AABB pre-filter AND the per-candidate band test — the ones that
        // cost real work). Logged with the winner below.
        uint32 inBandCount = 0;

        for (ObjectGuid guid : candidates)
        {
            Unit* u = ObjectAccessor::GetUnit(*bot, guid);
            if (!u || !u->IsAlive())
                continue;
            if (!bot->IsHostileTo(u))
                continue;
            // A body on the floor is not a blocker: it never notices us and never
            // fights, so walking over to melee it is pure detour. See
            // DcEngageGeometry::IsDisplayedDead.
            if (DcEngageGeometry::IsDisplayedDead(u))
                continue;
            // A scripted never-dies mob is not a blocker either: pulling it is a
            // fight that can never end in a kill, and the corridor scan would
            // re-pick it every tick. The FarTargets set already drops these, but
            // this scan falls back to the stock `possible targets` value when
            // FarTargets is empty, so the check is repeated here.
            if (DcNeverTargetRegistry::IsNeverTarget(bot->GetMapId(), u->GetEntry()))
                continue;

            float const ux = u->GetPositionX();
            float const uy = u->GetPositionY();

            // Trivial AABB reject before any sqrt / segment math.
            if (ux < minX || ux > maxX || uy < minY || uy > maxY)
                continue;

            // We only want the closest blocker; once we have one, a farther
            // candidate can't win, so skip the corridor/LOS/path work for it.
            float const distFromBot = bot->GetDistance2d(u);
            if (distFromBot >= bestDistFromBot)
                continue;

            // Per-candidate band: its real aggro range, clamped to
            // [widthFloor, widthCap] so a tiny-aggro mob right on the line
            // still counts and a giant elite is caught out to the cap. Falls
            // back to the caller's fixed corridorWidth when dynamic-aggro is
            // off (AggroRangeOf returns the fallback then).
            float const band =
                DcEngageGeometry::AggroRangeOf(bot, u, corridorWidth, widthFloor, widthCap);
            float const widthSq = band * band;

            // A candidate may only match a leg on its OWN vertical level. The
            // band test is 2D, so without this a mob at the bottom of a ledge
            // matches the legs running along the top — plan-view close, but its
            // real along-path distance is way past the lookahead. Restricted to
            // its own floor's legs, the lookahead defers it correctly until the
            // route actually descends to it.
            float const uz = u->GetPositionZ();
            bool inCorridor = false;
            for (Seg2D const& s : segs)
            {
                if (uz < std::min(s.az, s.bz) - DC_CORRIDOR_Z_BAND ||
                    uz > std::max(s.az, s.bz) + DC_CORRIDOR_Z_BAND)
                    continue;
                if (DungeonClearMath::DistSqToSegment2D(ux, uy, s.ax, s.ay, s.bx, s.by) <= widthSq)
                {
                    inCorridor = true;
                    break;  // threshold test — first segment in range is enough
                }
            }
            if (!inCorridor)
                continue;
            ++inBandCount;

            // Expensive gates last, and per-candidate so a closer out-of-LOS
            // mob can't shadow a visible one further along the corridor.
            if (!DcEngageGeometry::IsLevelReachable(bot, u))
                continue;
            if (!bot->IsWithinLOSInMap(u))
                continue;

            best = u;
            bestDistFromBot = distFromBot;
        }
        if (best)
            DC_PULL_DEBUG("[DC:{}] blocking-trash: {} candidate(s) in band -> {} "
                          "at {:.1f}yd",
                          bot->GetName(), inBandCount, best->GetObjectGuid().ToString(),
                          bestDistFromBot);
        return best;
    }

    // The next `maxLookAhead` yards of the smoothed route as a polyline whose
    // [0] IS the bot's live position. Truncated at the first closed door, for
    // the reason FindBlockingTrashOnPath documents at length: the mesh is baked
    // without doors, so the corridor otherwise runs straight through them.
    //
    // CRITICAL: chain each segment's `polyline` points, NOT the segment endpoints
    // (`seg.ex/ey`). The primary producer (LongRangePathfinder) emits the ENTIRE
    // winding route as ONE PathSegment whose ex/ey is only the final endpoint, so
    // chaining endpoints collapses the window to a bee-line through walls that
    // ignores maxLookAhead entirely.
    //
    // The last leg may overshoot maxLookAhead — the accumulator is checked after
    // the point is appended, so the window always ends on a real route vertex
    // rather than mid-leg. Both consumers treat the window as a threshold test,
    // so a few yards of overshoot cost nothing and a truncated final leg would
    // cost a whole point of geometry.
    std::vector<G3D::Vector3> BuildRouteWindow(Player* bot,
                                               std::vector<PathSegment> const& segments,
                                               float maxLookAhead)
    {
        std::vector<G3D::Vector3> window;
        if (!bot || segments.empty())
            return window;

        auto const doors =
            CollectClosedDoors(bot->FindMap(), bot->GetPositionX(), bot->GetPositionY(),
                               maxLookAhead + DC_DOOR_BAND);

        window.reserve(segments.size() + 1);
        window.emplace_back(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

        float accumulated = 0.0f;
        bool stop = false;
        for (PathSegment const& seg : segments)
        {
            // Anchored segments collapse to a single polyline point; non-anchored
            // segments carry the full smoothed corridor. Fall back to the endpoint
            // only if a segment somehow has no polyline at all — built lazily so
            // the common (populated) case allocates nothing per segment.
            std::vector<G3D::Vector3> fallback;
            if (seg.polyline.empty())
                fallback.emplace_back(seg.ex, seg.ey, seg.ez);
            std::vector<G3D::Vector3> const& pts = seg.polyline.empty() ? fallback : seg.polyline;

            for (G3D::Vector3 const& pt : pts)
            {
                // By VALUE: push_back below reallocates, and a reference into
                // the vector would dangle the moment it does.
                G3D::Vector3 const prev = window.back();
                Seg2D const s{prev.x, prev.y, prev.z, pt.x, pt.y, pt.z};
                if (!doors.empty() && SegmentHitsClosedDoor(s, doors))
                {
                    stop = true;
                    break;
                }
                float const dx = pt.x - prev.x;
                float const dy = pt.y - prev.y;
                accumulated += std::sqrt(dx * dx + dy * dy);
                window.push_back(pt);
                if (accumulated >= maxLookAhead)
                {
                    stop = true;
                    break;
                }
            }
            if (stop)
                break;
        }
        return window;
    }

    // Consecutive polyline points as corridor legs. Trivial, but it keeps the
    // "window[i] -> window[i+1]" pairing in one place now that two scanners
    // consume the same window.
    std::vector<Seg2D> WindowSegments(std::vector<G3D::Vector3> const& window)
    {
        std::vector<Seg2D> segs;
        if (window.size() < 2)
            return segs;
        segs.reserve(window.size() - 1);
        for (size_t i = 0; i + 1 < window.size(); ++i)
            segs.push_back(Seg2D{window[i].x, window[i].y, window[i].z,
                                 window[i + 1].x, window[i + 1].y, window[i + 1].z});
        return segs;
    }

    // How many swept candidates may be probed (reachability + LOS) in one scan
    // before the sweep gives up and hands the tick to the corridor band. Each
    // rejected candidate costs a PathGenerator probe and a VMap ray, so this is
    // the scan's real cost ceiling. Four is well past the observed need — in a
    // hall with rooms the FIRST violated sphere is the answer, and the retries
    // only exist so one unreachable straggler cannot blind the sweep entirely.
    constexpr int kSweepProbeBudget = 4;

    // Would `u` actually notice us walking this window? The sphere set is
    // LOS-blind (right for avoidance, wrong for targeting — see
    // DcTargeting::FindEnRouteAggroPack), so this is the veto that keeps the
    // sweep from walking off to pull a room on the far side of a wall that would
    // never have woken. One ray, from the mob to the point on the walk it comes
    // closest to: the sphere is violated, so that point is inside its aggro
    // reach by construction.
    //
    // The +2yd on Z is the walker's eye height. A ray to the floor point clips
    // the lip of a doorway or a ramp crest that the mob can plainly see over,
    // and a false "it can't see us" here is the exact over-pull this whole
    // change exists to remove.
    bool SeesTheWalk(Unit* u, std::vector<G3D::Vector3> const& window)
    {
        // NOT named `near` — windef.h defines that as a macro (see
        // tools/check_msvc_portability.py).
        G3D::Vector3 nearest;
        if (!DungeonClearMath::NearestPointOnPolyline2D(window, u->GetPositionX(),
                                                        u->GetPositionY(), nearest))
            return false;
        return u->IsWithinLOS(nearest.x, nearest.y, nearest.z + 2.0f);
    }
}

Unit* DcTargeting::FindBlockingTrash(Player* bot,
                                          DungeonBossInfo const& boss,
                                          float range,
                                          float halfAngle,
                                          GuidVector const& possibleTargets)
{
    if (!bot || possibleTargets.empty())
        return nullptr;

    float const bossDx = boss.x - bot->GetPositionX();
    float const bossDy = boss.y - bot->GetPositionY();
    float const bossAngle = std::atan2(bossDy, bossDx);

    Unit* best = nullptr;
    float bestDist = range;

    for (ObjectGuid guid : possibleTargets)
    {
        Unit* u = ObjectAccessor::GetUnit(*bot, guid);
        if (!u || !u->IsAlive())
            continue;
        if (!bot->IsHostileTo(u))
            continue;
        // …but DO skip a unit displayed as a corpse: it cannot be the thing
        // clawing at the healer. See DcEngageGeometry::IsDisplayedDead.
        if (DcEngageGeometry::IsDisplayedDead(u))
            continue;
        // Deliberately do NOT skip in-combat units. A hostile in our forward
        // corridor that's already engaged on another party member is *more*
        // reason to engage, not less — otherwise the tank walks past while
        // the healer gets clawed.

        float const dx = u->GetPositionX() - bot->GetPositionX();
        float const dy = u->GetPositionY() - bot->GetPositionY();
        float const dist = std::hypot(dx, dy);
        if (dist > range)
            continue;

        float const ang = std::atan2(dy, dx);
        float delta = std::fabs(ang - bossAngle);
        if (delta > DC_PI)
            delta = 2.0f * DC_PI - delta;
        if (delta > halfAngle)
            continue;

        if (dist < bestDist && DcEngageGeometry::IsLevelReachable(bot, u))
        {
            best = u;
            bestDist = dist;
        }
    }
    return best;
}
Unit* DcTargeting::FindBlockingTrashCorridor(Player* bot,
                                                  Movement::PointsArray const& corridor,
                                                  float maxLookAhead,
                                                  float corridorWidth,
                                                  GuidVector const& possibleTargets)
{
    if (!bot || corridor.size() < 2 || possibleTargets.empty())
        return nullptr;

    // Walk the polyline only as far as `maxLookAhead` yards from the bot's
    // current position. Beyond that the trigger doesn't care — we only
    // engage things that actually block the next leg of the route.
    // Truncate at the first closed door (see FindBlockingTrashOnPath) so a pack
    // on the far side of a shut, navmesh-passable door is never picked.
    auto const doors =
        CollectClosedDoors(bot->FindMap(), bot->GetPositionX(), bot->GetPositionY(),
                           maxLookAhead + DC_DOOR_BAND);

    std::vector<Seg2D> segments;
    segments.reserve(corridor.size());
    float accumulated = 0.0f;
    for (size_t i = 0; i + 1 < corridor.size(); ++i)
    {
        G3D::Vector3 const& a = corridor[i];
        G3D::Vector3 const& b = corridor[i + 1];
        Seg2D const s{a.x, a.y, a.z, b.x, b.y, b.z};
        if (!doors.empty() && SegmentHitsClosedDoor(s, doors))
            break;
        segments.push_back(s);
        float const dx = b.x - a.x;
        float const dy = b.y - a.y;
        accumulated += std::sqrt(dx * dx + dy * dy);
        if (accumulated >= maxLookAhead)
            break;
    }

    // Per-candidate LOS gate inside PickBlockingTrash drops "pack on the other
    // side of a wall" false positives without letting a closer out-of-LOS mob
    // shadow a visible blocker further along the corridor.
    return PickBlockingTrash(bot, segments, corridorWidth, possibleTargets);
}
Unit* DcTargeting::FindBlockingTrashOnPath(Player* bot,
                                                std::vector<PathSegment> const& segments,
                                                float maxLookAhead,
                                                float corridorWidth,
                                                GuidVector const& candidates)
{
    if (!bot || segments.empty() || candidates.empty())
        return nullptr;

    // The scanned corridor is the first `maxLookAhead` yards of the smoothed
    // route, truncated at the first closed door — see BuildRouteWindow for why
    // both of those matter and why it chains polyline points rather than
    // segment endpoints.
    return PickBlockingTrash(bot, WindowSegments(BuildRouteWindow(bot, segments, maxLookAhead)),
                             corridorWidth, candidates);
}
Unit* DcTargeting::FindEnRouteAggroPack(Player* bot, AiObjectContext* ctx,
                                        std::vector<PathSegment> const& segments,
                                        float maxLookAhead)
{
    if (!bot || !ctx || segments.empty())
        return nullptr;
    if (!DcEngageGeometry::EnRouteSweepApplies(bot))
        return nullptr;

    std::vector<G3D::Vector3> const window = BuildRouteWindow(bot, segments, maxLookAhead);
    if (window.size() < 2)
        return nullptr;

    // The leg the spheres are gathered around. BystanderSpheres searches out to
    // this endpoint's distance plus a full aggro reach, so a pack beside the far
    // end of the window is still collected.
    G3D::Vector3 const& end = window.back();
    Position const legEnd(end.x, end.y, end.z, 0.0f);
    std::vector<DcEngageGeometry::AvoidSphere> spheres =
        DcEngageGeometry::BystanderSpheres(bot, legEnd, /*exclude*/ nullptr);
    if (spheres.empty())
        return nullptr;

    // Cheap vetoes first, over the whole set, so the route-order loop below never
    // spends a probe on a candidate that was never eligible.
    //
    // A boss is not sweepable in either sense: the encounter bosses have their own
    // engage path (range gate, anchor checks, party-ready gate), and a room-aggro
    // boss is precisely the thing the pre-clear works AROUND. BystanderSpheres has
    // no reason to exclude either — it only ever walks around them — so this
    // mirrors what NearestHostileNearPoint already does for the point-radius clear.
    // IsPossibleTarget comes along for the same reason: the sphere set only has to
    // decide what to keep AWAY from, and an unattackable trigger creature is worth
    // avoiding but can never be pulled.
    //
    // The distance cap is the load-bearing one. A sphere is violated when the mob
    // is within its OWN reach of the walk, so a candidate can legitimately sit a
    // full aggro radius beyond the far end of the window — ~64yd out. Everything
    // downstream promises otherwise: the pull action's Idle branch is written
    // around "the scan already caps the look-ahead at ~35yd", and
    // IsStickyPullTargetValid releases a latch past 45yd, so an over-range pick
    // would re-scan every single tick instead of latching. Capping the swept pack
    // at the same look-ahead the corridor scan uses keeps one envelope for both,
    // and costs nothing: a room further up the route is swept as soon as the tank
    // has walked into range of it.
    uint32 const mapId = bot->GetMapId();
    spheres.erase(std::remove_if(spheres.begin(), spheres.end(),
                                 [&](DcEngageGeometry::AvoidSphere const& s)
                                 {
                                     Unit* const u = ObjectAccessor::GetUnit(*bot, s.guid);
                                     if (!u || !u->IsAlive())
                                         return true;
                                     if (bot->GetExactDist2d(u) > maxLookAhead)
                                         return true;
                                     if (!AttackersValue::IsPossibleTarget(u, bot))
                                         return true;
                                     if (DcNeverTargetRegistry::IsNeverTarget(mapId, u->GetEntry()))
                                         return true;
                                     if (IsDungeonBossEntry(ctx, u->GetEntry()))
                                         return true;
                                     return RoomAggroRegistry::Find(mapId, u->GetEntry()) != nullptr;
                                 }),
                  spheres.end());
    if (spheres.empty())
        return nullptr;

    // Route order, not nearest-the-bot: the pack to pull is the one the WALK
    // reaches first, which on a route that doubles back is not the one closest in
    // straight-line terms. Each rejected candidate is dropped and the question
    // re-asked, so one unreachable straggler beside the corridor cannot blind the
    // sweep to the room behind it — bounded by kSweepProbeBudget because every
    // rejection costs a pathfinder probe and a VMap ray.
    for (int probes = 0; probes < kSweepProbeBudget && !spheres.empty(); ++probes)
    {
        size_t leg = 0;
        int const idx = DcEngageGeometry::FirstViolatedSphereOnPolyline(window, spheres, leg);
        if (idx < 0)
            return nullptr;                 // the whole walk is clear of aggro

        DcEngageGeometry::AvoidSphere const violated = spheres[idx];
        spheres.erase(spheres.begin() + idx);

        Unit* const u = ObjectAccessor::GetUnit(*bot, violated.guid);
        if (!u || !u->IsAlive())
            continue;
        // Does it actually see the walk? The sphere set is LOS-blind on purpose;
        // without this the sweep trades "rooms we wake in passing" for "rooms we
        // never would have woken at all".
        if (!SeesTheWalk(u, window))
            continue;
        if (!DcEngageGeometry::IsLevelReachable(bot, u))
            continue;
        if (DcEngageGeometry::ClosedDoorBetween(bot, u->GetPositionX(),
                                                u->GetPositionY(), u->GetPositionZ()))
            continue;

        DC_PULL_DEBUG("[DC:{}] en-route sweep: {} at {:.1f}yd — route enters its "
                      "{:.1f}yd aggro sphere on leg {}",
                      bot->GetName(), u->GetObjectGuid().ToString(), bot->GetExactDist2d(u),
                      violated.r, static_cast<uint32>(leg));
        return u;
    }
    return nullptr;
}
Unit* DcTargeting::FindPullTarget(PlayerbotAI* botAI, DungeonBossInfo const& next)
{
    if (!botAI)
        return nullptr;
    Player* bot = botAI->GetBot();
    if (!bot)
        return nullptr;
    AiObjectContext* context = botAI->GetAiObjectContext();

    // PULL-BACK boss (BossPullbackRegistry): the BOSS is the pull target, ahead of
    // any corridor scan. This is the one case where the pull pipeline is aimed at a
    // boss on purpose — the boss veto further down exists to stop a boss being
    // camp-dragged by accident, and this is the deliberate opposite. Handing him to
    // the pull FSM is what buys the whole maneuver for free: the party parks and
    // goes passive at the camp (Forming), the tank goes out ALONE to tag
    // (Advancing), and the drag-back (Returning) hauls him to the anchor before
    // anyone else is in the fight.
    //
    // ARMING is gated on the tank actually being AT the anchor: for a pull-back
    // boss IsAtBossEngage measures the ANCHOR, not the boss, so the pull commits
    // exactly when boss navigation has finished walking the party onto the safe
    // ground and not a step before. Without that gate the pull would commit from
    // halfway across the dungeon and drag the boss to wherever the party happened
    // to be standing.
    //
    // Once the maneuver IS in flight (`bossPullback`), the gate has to drop, and
    // this is load-bearing rather than cosmetic: the tag leg may walk the tank off
    // the anchor to close on a boss that wandered, which takes it outside the
    // anchor's engage radius. Still requiring the gate would resolve the target to
    // null mid-leg, the Advancing branch would read that as "target gone", reset
    // the FSM to Idle, and the maneuver would restart from scratch every time the
    // tank stepped away — a livelock with the party parked at camp forever.
    DcPullContext const& pullCtx =
        context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    if (next.kind == DungeonAnchorKind::Boss &&
        BossPullbackRegistry::Find(bot->GetMapId(), next.entry) &&
        (pullCtx.bossPullback || DcTickMemoAccess::AtBossEngage(bot, context, next)))
    {
        Creature* const pullbackBoss = GetLiveBoss(bot, context, next.entry);
        if (pullbackBoss && pullbackBoss->IsAlive())
            return pullbackBoss;
        // Not loaded / already dead: fall through. The corridor scan below vetoes
        // bosses anyway, so this degrades to "no pull target", and the at-boss
        // engage's own not-present handling reports it.
    }

    // Same look-ahead / band the blocking-trash trigger uses; keep them aligned
    // so the pull aims at the very pack the normal flow would otherwise pull.
    constexpr float kLookAhead = kPullLookAhead;
    constexpr float kWidth = 18.0f;
    constexpr float kConeRange = 35.0f;
    float const kConeHalfAngle = DC_PI / 3.0f;  // 60°

    // Room-wide-aggro pre-clear: while the tank is at a flagged boss with room
    // trash still up, the pull pipeline targets that ROOM trash (nearest-first),
    // not the corridor to the boss. This is what makes advanced/dynamic pull
    // honour the player's chosen pull type for the room clear — the same
    // pull-to-camp machinery, just aimed at the room instead of the corridor —
    // while the boss gate (DungeonClearAtBossTrigger) holds the boss pull. Off /
    // Leeroy clears the same nearest unit via the room-clear engage action.
    if (IsRoomClearActive(bot, context))
    {
        if (Unit* room = NearestRoomTrash(bot, context))
            return room;
        // Nothing reachable this tick — fall through; the corridor scan below
        // returns null at the boss, so the gate simply keeps holding.
    }

    GuidVector const& farTargets = context->GetValue<GuidVector>(DcKey::FarTargets)->Get();
    GuidVector const& possibleTargets = context->GetValue<GuidVector>(DcKey::Stock::PossibleTargets)->Get();
    GuidVector const& candidates = farTargets.empty() ? possibleTargets : farTargets;

    Unit* trash = nullptr;
    ChunkedPathfinder::Result const& path =
        context->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Get();
    if (path.reachable && !path.segments.empty())
    {
        trash = FindBlockingTrashOnPath(bot, path.segments, kLookAhead, kWidth, candidates);
        // En-route sweep: prefer the pack the route's aggro reaches FIRST over the
        // one merely standing on the centre line. Returns null in heroics, so
        // this is the historical pick unless swept.
        //
        // A corridor pick that is ALREADY IN COMBAT keeps the tick, though: the
        // corridor scan deliberately does not skip in-combat units ("a hostile in
        // our forward corridor already engaged on another party member is MORE
        // reason to engage, not less"), while BystanderSpheres deliberately does
        // (its aggro is spent — nothing to avoid). Letting a fresh sweep pick
        // outrank it would walk the tank off toward a quiet room while a follower
        // is being clawed behind it.
        if (Unit* const swept = FindEnRouteAggroPack(bot, context, path.segments, kLookAhead))
            if (!trash || !trash->IsInCombat())
                trash = swept;
    }
    else
    {
        Movement::PointsArray corridor;
        if (DcEngageGeometry::ComputeCorridor(bot, next.x, next.y, next.z, corridor))
            trash = FindBlockingTrashCorridor(bot, corridor, kLookAhead, kWidth, candidates);
        else
            trash = FindBlockingTrash(bot, next, kConeRange, kConeHalfAngle, candidates);
    }

    if (!trash)
        return nullptr;

    // Never pull a boss: the dedicated at-boss path (engage-range gate, anchor
    // checks, door veto, party-ready gate) owns boss engagements. Without this,
    // a boss that becomes the nearest corridor blocker in the band between
    // engage range and the look-ahead gets committed as a camp drag — scripted
    // bosses (RP gossip starts, threshold adds, room-bound mechanics) misbehave
    // when dragged, and players don't camp-pull bosses through corridors.
    // Checked before the door veto (a tiny vector scan vs. a map-wide door
    // collection). Deliberately NOT applied to the FindBlockingTrash* scans:
    // in plain mode, walking in on a boss in the corridor band degenerates to
    // a normal engage, which is acceptable — only the camp-drag is wrong.
    if (IsDungeonBossEntry(context, trash->GetEntry()))
    {
        DC_PULL_DEBUG("[DC:{}] pull target vetoed — {} ({}) is a dungeon boss, "
                      "at-boss path owns it",
                      bot->GetName(), trash->GetName(), trash->GetObjectGuid().ToString());
        return nullptr;
    }

    // Never pull a pack on the far side of a closed door: doors are navmesh-
    // passable (the mesh is baked without them), so the tank would otherwise run
    // through and drag the pack back through the doorway. ClosedDoorBetween is a
    // GameObject-LOS test against the door's real mesh — accurate for wide doors
    // (SM Cathedral's Chapel Door) and never fires on a route merely running PAST
    // a door (SFK's courtyard gate).
    if (DcEngageGeometry::ClosedDoorBetween(
            bot, trash->GetPositionX(), trash->GetPositionY(), trash->GetPositionZ()))
    {
        DC_PULL_DEBUG("[DC:{}] pull target {} vetoed — closed door (GO-LOS) between us and it",
                      bot->GetName(), trash->GetObjectGuid().ToString());
        return nullptr;
    }

    return trash;
}
bool DcTargeting::IsDungeonBossEntry(AiObjectContext* ctx, uint32 entry)
{
    if (!ctx || !entry)
        return false;
    std::vector<DungeonBossInfo> const& bosses =
        ctx->GetValue<std::vector<DungeonBossInfo>>(DcKey::DungeonBosses)->Get();
    for (DungeonBossInfo const& boss : bosses)
        if (boss.entry == entry)
            return true;
    return false;
}
Unit* DcTargeting::GetPullTarget(PlayerbotAI* botAI)
{
    if (!botAI)
        return nullptr;
    Player* bot = botAI->GetBot();
    if (!bot)
        return nullptr;
    AiObjectContext* ctx = botAI->GetAiObjectContext();
    if (!ctx)
        return nullptr;

    Value<ObjectGuid>* value = ctx->GetValue<ObjectGuid>(DcKey::PullTarget);
    ObjectGuid guid = value->Get();
    if (guid.IsEmpty())
        return nullptr;

    // Re-resolve the cached GUID every call so the position stays live and we
    // never touch a pointer that may have been freed since the scan.
    if (Unit* u = ObjectAccessor::GetUnit(*bot, guid))
        if (u->IsAlive())
            return u;

    // Cached GUID went stale within the interval (the pack died / despawned).
    // Force one recompute now instead of reporting "no target" for the rest of
    // the interval: a commit must never aim at a corpse, and the governor must
    // see the NEXT pack, not a phantom null. Steady state never reaches this.
    value->Reset();
    guid = value->Get();
    if (guid.IsEmpty())
        return nullptr;
    Unit* fresh = ObjectAccessor::GetUnit(*bot, guid);
    return (fresh && fresh->IsAlive()) ? fresh : nullptr;
}
bool DcTargeting::IsStickyPullTargetValid(Player* bot, AiObjectContext* ctx, Unit* u)
{
    if (!bot || !ctx || !u)
        return false;
    if (!u->IsAlive() || !bot->IsHostileTo(u))
        return false;

    // Mirror the fresh scan's boss veto. The governor can only latch what the
    // scan returned, so a boss should never be sticky — but the invariant
    // (a boss is NEVER a pull target) is cheap to keep airtight locally.
    if (IsDungeonBossEntry(ctx, u->GetEntry()))
        return false;

    // A pack a prior pull gave up on is handed to the normal walk-in engage;
    // keeping it sticky would pin the scan on the very pack the pipeline is
    // trying to move past.
    DcPullContext const& pull =
        ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    if (!pull.abortTarget.IsEmpty() && u->GetObjectGuid() == pull.abortTarget)
        return false;

    if (bot->GetExactDist2d(u) > kPullLookAhead + kPullStickySlack)
        return false;
    if (!DcEngageGeometry::IsLevelReachable(bot, u))
        return false;
    // Mirrors the fresh scan's veto: a closed door coming between us and the
    // latched pack releases it. GameObject-LOS against the door's real mesh —
    // stable (a shut door doesn't flicker, so this never flaps the latch the way
    // a distance/corner re-probe would) yet correct for wide/offset doors.
    if (DcEngageGeometry::ClosedDoorBetween(bot, u->GetPositionX(),
                                            u->GetPositionY(), u->GetPositionZ()))
        return false;
    return true;
}
Unit* DcTargeting::FindNearestReachableHostile(Player* bot)
{
    if (!bot)
        return nullptr;
    Map* map = bot->FindMap();
    if (!map)
        return nullptr;

    // Match the cap PullRequestAction enforces — anything past this gets
    // silently rejected by the pull pipeline, so don't even consider it.
    float const maxPullDistance = sPlayerbotAIConfig.reactDistance * 3.0f;

    // Collect candidate hostiles from every loaded creature on the map, sort
    // by distance, then take the first one we can actually path to AND that
    // the pull pipeline will accept. Cheap pre-sort means we usually only
    // run 1–2 PathGenerator calls.
    std::vector<std::pair<float, Creature*>> candidates;
    for (auto const& kv : map->GetCreatureBySpawnIdStore())
    {
        Creature* c = kv.second;
        if (!c || !c->IsAlive())
            continue;
        if (!bot->IsHostileTo(c))
            continue;
        if (c->IsInCombat())
            continue;
        float const dist = bot->GetDistance(c);
        if (dist > maxPullDistance)
            continue;
        // Apply the same target validity gates the pull pipeline uses
        // (visibility, faction edge cases, non-attackable flags, etc.).
        // Without this we'd hand RequestPull a target it silently rejects
        // downstream and the tank would just stand there.
        if (!AttackersValue::IsPossibleTarget(c, bot))
            continue;
        // A scripted never-dies mob would be picked here forever (see
        // DcNeverTargetRegistry) — this scan builds its own candidate list from
        // the creature store, so the FarTargets filter never sees it.
        if (DcNeverTargetRegistry::IsNeverTarget(bot->GetMapId(), c->GetEntry()))
            continue;
        candidates.emplace_back(dist, c);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](auto const& a, auto const& b) { return a.first < b.first; });

    for (auto const& pair : candidates)
    {
        Creature* c = pair.second;
        if (DcEngageGeometry::IsReachable(bot, c->GetPositionX(), c->GetPositionY(), c->GetPositionZ()))
            return c;
    }
    return nullptr;
}

Unit* DcTargeting::LeaderFightAnchor(Player* bot, Player* leader, Position& anchorPos)
{
    if (!bot || !leader)
        return nullptr;

    // Nearest live unit the leader is meleeing — the pack it is holding. LOS-blind
    // on purpose (the reconnect exists precisely for a fight the bot can't see yet).
    // getAttackers() covers the melee pack; the leader's victim is the fallback for
    // an all-ranged grab. Mirrors DungeonClearAssistCampActionBase's anchor scan.
    Unit* target = nullptr;
    float bestDist = 0.0f;
    for (Unit* a : leader->getAttackers())
    {
        if (!a || !a->IsAlive() || a->GetMapId() != bot->GetMapId())
            continue;
        if (!bot->IsValidAttackTarget(a))
            continue;
        float const d = bot->GetExactDist2d(a);
        if (!target || d < bestDist)
        {
            target = a;
            bestDist = d;
        }
    }
    if (!target)
    {
        Unit* const victim = leader->GetVictim();
        if (victim && victim->IsAlive() && victim->GetMapId() == bot->GetMapId() &&
            bot->IsValidAttackTarget(victim))
            target = victim;
    }

    anchorPos = target ? target->GetPosition() : leader->GetPosition();
    return target;
}
Creature* DcTargeting::FindLiveCreatureOnMap(Player* bot, uint32 entry)
{
    if (!bot)
        return nullptr;
    Map* map = bot->FindMap();
    if (!map)
        return nullptr;

    for (auto const& kv : map->GetCreatureBySpawnIdStore())
    {
        Creature* c = kv.second;
        if (c && c->GetEntry() == entry && c->IsAlive())
            return c;
    }
    return nullptr;
}
Creature* DcTargeting::GetLiveBoss(Player* bot, AiObjectContext* ctx, uint32 entry)
{
    if (!bot || !ctx || !entry)
        return nullptr;

    DungeonClearLiveBoss const cached =
        ctx->GetValue<DungeonClearLiveBoss>(DcKey::LiveBoss)->Get();
    if (cached.entry == entry && !cached.guid.IsEmpty())
    {
        // Re-resolve the cached GUID every call so the position stays live and
        // we never touch a pointer that may have been freed since the scan.
        Creature* c = ObjectAccessor::GetCreature(*bot, cached.guid);
        if (c && c->IsAlive() && c->GetEntry() == entry)
            return c;
        // Cached GUID went stale within the interval (died / despawned). Fall
        // through to a direct scan rather than miss a still-present instance.
    }

    // Cache miss: computed for a different boss (just after a boss change), or
    // a stale GUID — pay one direct scan. Steady state hits the branch above.
    return FindLiveCreatureOnMap(bot, entry);
}
bool DcTargeting::IsCreaturePresentOnMap(Player* bot, uint32 entry)
{
    if (!bot)
        return false;
    Map* map = bot->FindMap();
    if (!map)
        return false;

    for (auto const& kv : map->GetCreatureBySpawnIdStore())
    {
        Creature* c = kv.second;
        if (c && c->GetEntry() == entry)
            return true;
    }
    return false;
}
bool DcTargeting::HasPendingSummonEvent(Player* bot, AiObjectContext* ctx, uint32 bossEntry)
{
    if (!bot || !ctx || !bossEntry)
        return false;
    Map* map = bot->FindMap();
    if (!map)
        return false;

    auto const& cleared =
        ctx->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get();
    for (DungeonEvent const* ev : DungeonEventRegistry::Conditional(map->GetId(), map->GetDifficulty()))
    {
        if (ev->panelGatesBossEntry != bossEntry)
            continue;
        // Retired (skipped via `dc skip` inserts the latch key into cleared). A
        // Repeatable summon event is otherwise never latched, so it stays
        // "pending" for the whole encounter until the boss it gates is live/dead.
        if (cleared.count(DungeonEventExecutor::ConditionalLatchKey(ev->id)))
            continue;
        return true;
    }
    return false;
}

bool DcTargeting::IsHoldingForSummonEvent(Player* bot, AiObjectContext* ctx,
                                         DungeonBossInfo const& next)
{
    // How close to the anchor / boss counts as "committed to the event". Must
    // comfortably contain the wave-fight spread (RFD's gong summons reach ~55yd)
    // AND bridge the anchor-to-spawn gap (the gong is ~83yd from Tuten'kash's
    // summon spot) so the hold stays continuous as the tank closes on him.
    constexpr float DC_EVENT_BOSS_HOLD_RADIUS = 80.0f;

    if (!bot || !ctx || next.kind != DungeonAnchorKind::Boss)
        return false;
    if (!HasPendingSummonEvent(bot, ctx, next.entry))
        return false;

    // Hold (suppress the dynamic pull) while parked at the gong to ring AND,
    // crucially, AFTER the boss is summoned while the tank closes to engage it.
    // Releasing the moment it spawned let the pull grab leftover room trash and
    // tow the tank away before it reached the (passive) boss — the "sometimes
    // doesn't aggro" bug. The union of the anchor (gong) and the live-boss radii
    // covers the whole gong->summon-spot corridor, so the pull never re-arms
    // mid-encounter. Released once the boss dies: its credit bit flips and
    // NextDungeonBossValue advances `next` past it (this returns false then).
    if (bot->GetDistance(next.x, next.y, next.z) <= DC_EVENT_BOSS_HOLD_RADIUS)
        return true;
    Creature* live = GetLiveBoss(bot, ctx, next.entry);
    return live && bot->GetDistance(live->GetPositionX(), live->GetPositionY(),
                                    live->GetPositionZ()) <= DC_EVENT_BOSS_HOLD_RADIUS;
}

InstanceScript* DcTargeting::GetInstanceScript(Player* bot)
{
    if (!bot)
        return nullptr;
    Map* map = bot->FindMap();
    if (!map)
        return nullptr;
    InstanceMap* im = map->ToInstanceMap();
    return im ? im->GetInstanceScript() : nullptr;
}
bool DcTargeting::ResetCompletionLatchesForNewInstance(Player* bot, AiObjectContext* context)
{
    if (!bot || !context)
        return false;
    // instanceId 0 = not in an instance / mid-load; never stamp or reset on it.
    uint32 const instanceId = bot->GetInstanceId();
    if (instanceId == 0)
        return false;
    // The stored run-instance matches => same run, nothing to do. This is the
    // common path every tick. Resetting fires only when the live instance id
    // differs from the stamp, INCLUDING the unstamped 0 case: a latch present the
    // first time we stamp an instance can only be stale (left by an untracked prior
    // run), and on a genuinely fresh run the sets are empty so the wipe is a no-op.
    uint32 const lastRunInstance = context->GetValue<uint32>(DcKey::RunInstance)->Get();
    if (lastRunInstance == instanceId)
        return false;

    auto& clearedSet = context->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get();
    auto& skippedSet = context->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get();

    InstanceScript* inst = GetInstanceScript(bot);
    LOG_INFO("playerbots.dungeonclear",
             "[DC:{}] run-instance transition: last={} now={} clearedAnchors={} skipped={} (mask={:#x})",
             bot->GetName(), lastRunInstance, instanceId,
             static_cast<uint32>(clearedSet.size()), static_cast<uint32>(skippedSet.size()),
             inst ? inst->GetCompletedEncounterMask() : 0u);

    clearedSet.clear();
    skippedSet.clear();
    context->GetValue<std::unordered_set<uint32>&>(DcKey::SeenBosses)->Get().clear();
    context->GetValue<std::unordered_set<uint32>&>(DcKey::SeenDueEvents)->Get().clear();
    context->GetValue<uint32>(DcKey::StickyBoss)->Set(0u);
    DcRun::Of(context).selectedBossEntry = 0u;
    context->GetValue<uint32>(DcKey::RunInstance)->Set(instanceId);
    return true;
}
bool DcTargeting::IsRoomClearActive(Player* bot, AiObjectContext* ctx)
{
    if (!bot || !ctx)
        return false;

    std::optional<DungeonBossInfo> next =
        ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next.has_value())
        return false;
    if (!RoomAggroRegistry::Find(bot->GetMapId(), next->entry))
        return false;

    // Cheap cached read first; the envelope probe (level-reachability) runs only
    // when there is actually room trash left to clear.
    if (ctx->GetValue<GuidVector>(DcKey::RoomTrashRemaining)->Get().empty())
        return false;

    // The room-clear DRIVER stays active across the WHOLE room envelope, not just
    // the tight engage standoff. The skirt orbits the avoid sphere on a ring WIDER
    // than the standoff, so gating on IsAtBossEngage dropped the driver (and the
    // governor) the instant the bot ran back onto the ring to round the sphere —
    // handing the tick to the boss-bound Advance mid-orbit (the live Jammal'an
    // failure: "ran backwards at a large angle, then snapped straight at the boss").
    return DcEngageGeometry::WithinRoomClearWindow(bot, ctx, *next);
}
bool DcTargeting::IsPullbackBossDue(Player* bot, AiObjectContext* ctx)
{
    if (!bot || !ctx)
        return false;

    // Cheapest gate first: most maps have no pull-back boss at all and pay one bool.
    if (!BossPullbackRegistry::HasRows(bot->GetMapId()))
        return false;

    std::optional<DungeonBossInfo> next =
        ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next.has_value() || next->kind != DungeonAnchorKind::Boss)
        return false;
    if (!BossPullbackRegistry::Find(bot->GetMapId(), next->entry))
        return false;

    // At the ANCHOR (IsAtBossEngage measures the anchor for a pull-back boss), and
    // the boss actually there to be fetched.
    if (!DcTickMemoAccess::AtBossEngage(bot, ctx, *next))
        return false;
    Creature* const boss = GetLiveBoss(bot, ctx, next->entry);
    return boss && boss->IsAlive();
}
bool DcTargeting::RoomClearForcesAdvanced(Player* bot, AiObjectContext* ctx)
{
    if (!bot || !ctx)
        return false;

    // Only a room with a pullOutRadius forces the verdict — check that BEFORE the
    // (pricier) room-clear window probe so the ordinary room-aggro bosses pay
    // nothing new. The next boss is already resolved for IsRoomClearActive.
    std::optional<DungeonBossInfo> next =
        ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next.has_value())
        return false;
    RoomAggroBoss const* room = RoomAggroRegistry::Find(bot->GetMapId(), next->entry);
    if (!room || room->pullOutRadius <= 0.0f)
        return false;

    return IsRoomClearActive(bot, ctx);
}
float DcTargeting::ActiveRoomSkirt(Player* bot, AiObjectContext* ctx)
{
    if (!bot || !ctx)
        return 0.0f;

    // Cheap gates first (registry Find), matching RoomClearForcesAdvanced: only a
    // skirt-override boss can widen the camp, so bail before the pricier room-clear
    // window probe for every ordinary room.
    std::optional<DungeonBossInfo> next =
        ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next.has_value())
        return 0.0f;
    float const skirt = RoomAggroRegistry::SkirtOverride(bot->GetMapId(), next->entry);
    if (skirt <= 0.0f)
        return 0.0f;

    return IsRoomClearActive(bot, ctx) ? skirt : 0.0f;
}
Unit* DcTargeting::NearestRoomTrash(Player* bot, AiObjectContext* ctx)
{
    if (!bot || !ctx)
        return nullptr;

    GuidVector const& remaining =
        ctx->GetValue<GuidVector>(DcKey::RoomTrashRemaining)->Get();

    Unit* best = nullptr;
    float bestDist = std::numeric_limits<float>::max();
    for (ObjectGuid const guid : remaining)
    {
        Unit* u = ObjectAccessor::GetUnit(*bot, guid);
        if (!u || !u->IsAlive())
            continue;
        float const d = bot->GetExactDist2d(u);
        if (d < bestDist)
        {
            best = u;
            bestDist = d;
        }
    }
    return best;
}

Unit* DcTargeting::NearestHostileNearPoint(Player* bot, AiObjectContext* ctx,
                                           float px, float py, float pz,
                                           float radius, float zBand,
                                           std::vector<uint32> const* entryFilter,
                                           Position const* rankFrom,
                                           ObjectGuid exclude,
                                           Position const* awayFrom)
{
    if (!bot || !ctx || radius <= 0.0f)
        return nullptr;

    // An empty list means "no filter" — never "match nothing", so a caller that
    // forwards an unset step field keeps the position-only behaviour.
    bool const filtered = entryFilter && !entryFilter->empty();

    // Reuse the standard candidate set (nearby units the room/trash machinery
    // already gathers); filter to those that sit in the point's 2D radius and
    // floor band, mirroring the room-trash exclusions.
    GuidVector const& candidates =
        ctx->GetValue<GuidVector>(DcKey::FarTargets)->Get();

    float const r2 = radius * radius;
    Unit* best = nullptr;
    float bestDist = std::numeric_limits<float>::max();
    for (ObjectGuid const guid : candidates)
    {
        if (!exclude.IsEmpty() && guid == exclude)
            continue;
        Unit* u = ObjectAccessor::GetUnit(*bot, guid);
        if (!u || !u->IsAlive())
            continue;
        if (!bot->IsHostileTo(u))
            continue;
        // Entry allow-list (cheap, so it runs before the reachability probes).
        if (filtered && std::find(entryFilter->begin(), entryFilter->end(),
                                  u->GetEntry()) == entryFilter->end())
            continue;
        if (!AttackersValue::IsPossibleTarget(u, bot))
            continue;
        if (DcNeverTargetRegistry::IsNeverTarget(bot->GetMapId(), u->GetEntry()))
            continue;
        // Never treat an encounter boss or a room-aggro boss/partner as clearable
        // area trash — those belong to the dedicated boss/at-boss paths.
        if (IsDungeonBossEntry(ctx, u->GetEntry()))
            continue;
        if (RoomAggroRegistry::Find(bot->GetMapId(), u->GetEntry()))
            continue;

        if (zBand > 0.0f && std::fabs(u->GetPositionZ() - pz) > zBand)
            continue;
        float const dx = u->GetPositionX() - px;
        float const dy = u->GetPositionY() - py;
        if (dx * dx + dy * dy > r2)
            continue;

        // Skip what the tank can't actually reach or is behind a closed door, so
        // the clear can't livelock on it. STRICT (IsEngageReachable, no same-level
        // fast path): this is a bare point-radius scan with no corridor/cone 2D
        // pre-filter, so a straight-line-near mob in an ADJACENT room/tunnel — even
        // on the same level — must be probed and rejected, not trusted. Without
        // this a conditional room event firing while the tank is elsewhere
        // EngageDirects through the dividing wall (Uldaman keeper hall).
        if (!DcEngageGeometry::IsEngageReachable(bot, u))
            continue;
        if (DcEngageGeometry::ClosedDoorBetween(bot, u->GetPositionX(),
                                                u->GetPositionY(), u->GetPositionZ()))
            continue;

        // Rank from the caller's reference point when it gave one, else from the
        // bot. Everything above this line — the filters — is unchanged either way;
        // `rankFrom`/`awayFrom` only decide which of the qualifying candidates wins.
        //
        // `awayFrom` wins by being FURTHEST from the anchor, which is the same
        // comparison with the sign flipped — stored negated so one `d < bestDist`
        // serves both and the "no candidate yet" sentinel stays a single max().
        float const d = awayFrom
            ? -awayFrom->GetExactDist2d(u->GetPositionX(), u->GetPositionY())
            : rankFrom
                ? rankFrom->GetExactDist2d(u->GetPositionX(), u->GetPositionY())
                : bot->GetExactDist2d(u);
        if (d < bestDist)
        {
            best = u;
            bestDist = d;
        }
    }
    return best;
}
