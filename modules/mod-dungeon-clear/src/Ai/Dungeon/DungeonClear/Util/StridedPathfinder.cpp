/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "StridedPathfinder.h"

#include <algorithm>
#include <cmath>

#include "Log.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Ai/Dungeon/DungeonClear/Data/DcNavPenaltyRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonSpawnGraph.h"
#include "Ai/Dungeon/DungeonClear/Util/CorridorCenter.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearGeometry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"  // DC_PI
#include "Ai/Dungeon/DungeonClear/Util/LongRangePathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"

namespace
{
    // Sub-180yd strides keep every PathGenerator request comfortably inside
    // Detour's 74-poly / 74-point safe-output envelope. The v1 builder asked
    // for the full route (often >400yd) and hit PATHFIND_INCOMPLETE /* this finder reports a trimmed path as incomplete */, which silently
    // replaced the path with a straight-line shortcut.
    constexpr float STRIDE_LEN = 180.0f;

    // Final hop must land within this distance of the (snapped) boss target
    // to count as "complete".
    constexpr float ARRIVE_RADIUS = 8.0f;

    // Per-segment arrive radius. Advance picks the first segment whose
    // endpoint is more than this away as its current hop.
    constexpr float HOP_ARRIVE_RADIUS = 6.0f;

    // Minimum forward progress per stride. Anything below this means we're
    // pinned against geometry and should bail.
    constexpr float MIN_STRIDE_PROGRESS = 1.5f;

    // Snap radius for stride probe points. 25yd is wide enough to accept
    // probes that land in the middle of a wall (snapped sideways onto the
    // adjacent corridor) without straying so far that the snapped point is
    // in a different room.
    constexpr float STRIDE_SNAP_RADIUS = 25.0f;

    // Lateral arc retries when the bee-line stride fails. Try ±30° and
    // ±60° of the bee-line direction; if none produce a usable path, give
    // up the stride.
    constexpr float ARC_ANGLES_RAD[] = {
        DC_PI / 6.0f,    //  30°
        -DC_PI / 6.0f,   // -30°
        DC_PI / 3.0f,    //  60°
        -DC_PI / 3.0f,   // -60°
    };

    // Fraction of STRIDE_LEN each fallback direction probes, tried far-first.
    // On a long winding corridor the full-length (1.0) bee-line/arc point
    // lands past a bend — across the inside wall, or snapped onto the wrong
    // arm — and fails to path. A shorter probe point stays on the near arm of
    // the bend and still yields forward progress, which the next stride builds
    // on. Far-first means we take the longest reach that produces a usable
    // chunk, so straight corridors keep striding in big steps.
    constexpr float REACH_FRACTIONS[] = {1.0f, 0.5f, 0.25f};

    // Reject path types that don't represent a real chunked corridor.
    //  - PATHFIND_INCOMPLETE /* this finder reports a trimmed path as incomplete */: BuildShortcut overwrote _pathPoints to [start, target].
    //                    Endpoint is the original target, so chunked stepping
    //                    would jump there in one shot. (The v1 bug.)
    //  - PATHFIND_SHORTCUT: same shape, different origin (start/end far
    //                       from any poly + flying/swimming detected).
    //  - PATHFIND_NOPATH: no route at all.
    bool PathTypeIsBogusChunk(uint32 type)
    {
        return (type & PATHFIND_NOPATH) || (type & PATHFIND_INCOMPLETE /* this finder reports a trimmed path as incomplete */) || (type & PATHFIND_SHORTCUT);
    }

    // LOS screen, distance helpers and the graze-bridged clean-prefix walk now
    // live in DungeonClearGeometry (shared with LongRangePathfinder and
    // CorridorCenter). Pull the names into this TU so the call sites below stay
    // unchanged; LosCleanPrefixCount replaces the old local LOSCleanPrefixCount.
    using DungeonClearGeometry::Dist2D;
    using DungeonClearGeometry::Dist3D;
    using DungeonClearGeometry::LosCleanPrefixCount;

    // Project (tx, ty, tz) from (cx, cy, cz) at the given 2D rotation about
    // the bee-line. zHint is the requested target z — we use it to hold
    // vertical aim roughly correct before the snap step shifts to whatever
    // poly z actually lives there.
    struct Probe
    {
        float x, y, z;
    };

    Probe AimProbe(float cx, float cy, float cz, float tx, float ty, float tz, float angleOffset,
                   float reach = 1.0f)
    {
        float const dx = tx - cx;
        float const dy = ty - cy;
        float const dist = Dist2D(cx, cy, tx, ty);
        float const stride = std::min(STRIDE_LEN * reach, dist);
        if (dist <= 0.001f)
            return Probe{tx, ty, tz};

        float const dirX = dx / dist;
        float const dirY = dy / dist;
        float const cosA = std::cos(angleOffset);
        float const sinA = std::sin(angleOffset);
        float const rx = dirX * cosA - dirY * sinA;
        float const ry = dirX * sinA + dirY * cosA;
        // For the vertical, interpolate linearly toward the target — the
        // snap step will adjust if the actual poly z differs.
        float const tInterp = (dist > 0.0f) ? (stride / dist) : 0.0f;
        return Probe{cx + rx * stride, cy + ry * stride, cz + (tz - cz) * tInterp};
    }

    // Run a single PathGenerator call from (cx,cy,cz) to (px,py,pz). On
    // success populates outPoints with the smoothed-corridor points
    // (excluding the starting point — it equals (cx,cy,cz) and would
    // double-up when segments are concatenated) and returns true. On
    // failure outPoints is cleared; outType carries the raw path type for
    // the caller's diagnostics.
    bool TryProbe(Player* bot, uint32 mapId, float cx, float cy, float cz, float px, float py, float pz,
                  std::vector<G3D::Vector3>& outPoints, uint32& outType)
    {
        outPoints.clear();
        PathGenerator gen(bot);
        gen.CalculatePath(cx, cy, cz, px, py, pz, /*forceDest*/ false);
        outType = gen.GetPathType();

        if (PathTypeIsBogusChunk(outType))
            return false;

        // PATHFIND_NOT_USING_PATH happens on maps without mmap, or flying/
        // swimming. The shortcut spline goes straight to (px,py,pz); accept
        // it as a single hop — there's no chunking to do anyway.
        if (outType & PATHFIND_NOT_USING_PATH)
        {
            outPoints.push_back(G3D::Vector3(px, py, pz));
            return true;
        }

        auto const& pts = gen.GetPath();
        if (pts.size() < 2)
            return false;

        // LOS sanity check on the smoothed corridor. The leading point of the
        // input equals (cx,cy,cz) — the previous segment's endpoint already
        // represents it — and the rest form this stride's smoothed corridor.
        std::vector<G3D::Vector3> losInput;
        losInput.reserve(pts.size());
        losInput.push_back(G3D::Vector3(cx, cy, cz));
        losInput.insert(losInput.end(), pts.begin() + 1, pts.end());

        // NOTE: corridor centering is DEFERRED to Build's accept path, which
        // centres only the one stride it actually keeps instead of every
        // rejected tier's probe (the direct/bee-line/arc/spawn-graph cascade
        // could otherwise run findDistanceToWall + VMAP raycasts on a dozen
        // throwaway probes). So the LOS screen below verifies the raw
        // engine-smoothed line; Build re-screens after centring the winner.
        // The candidate corridor is everything past the (pinned) start point.
        std::vector<G3D::Vector3> candidate(losInput.begin() + 1, losInput.end());

        // Count the LOS-clean leading points (corner grazes bridged; a real
        // wall-bridge truncates the prefix). cleanPts includes the leading
        // start point, so the usable candidate points are cleanPts - 1.
        size_t const cleanPts = LosCleanPrefixCount(bot, losInput);
        if (cleanPts < 2)
        {
            // Not even the first hop is clean — the corridor heads straight
            // into static geometry from here. Log it so a leg that
            // consistently fails LOS (a railing, a steep multi-level seam) is
            // distinguishable from a genuine NOPATH when diagnosing stalls.
            LOG_DEBUG("playerbots.dungeonclear",
                      "[dungeon-clear] probe rejected by LOS check (type=0x{:x}, {} pts) — "
                      "no clean corridor from start; trying next tier",
                      outType, losInput.size());
            return false;
        }

        size_t const keep = cleanPts - 1;  // usable candidate points
        if (keep < candidate.size())
        {
            // A sharp bend or a wall sat partway along this probe. Keep the
            // verified prefix as this stride's chunk; the stride loop re-probes
            // the boss from the clean end next iteration, so the bend ends up
            // early in the next (shorter) probe where it's easier to clear.
            LOG_DEBUG("playerbots.dungeonclear",
                      "[dungeon-clear] probe LOS-truncated to {}/{} corridor points "
                      "(type=0x{:x}) — re-probing from the clean end next stride",
                      keep, candidate.size(), outType);
            candidate.resize(keep);
        }

        // No-go volume screen (review Finding B). This strided fallback builds its
        // corridor with the stock engine PathGenerator, which is BLIND to the
        // DcRouteFilter route-cost discouragements that the primary long-range
        // producer uses — so on its own it would happily path straight up a
        // navmesh shortcut a real player can't follow (e.g. the LBRS chasm wall).
        // Reject any probe whose corridor enters a registered no-go volume; the
        // caller then tries the lateral arc / spawn-graph tiers (which route
        // around it) or, failing those, reports the target unreachable — Advance
        // rebuilds next tick (the primary normally succeeds), which is far better
        // than stranding the party up a wall. HasVolumes gates the per-point loop
        // so maps without a volume — i.e. almost all — pay nothing.
        //
        // The screen rejects a corridor that ENTERS a region, never one that is
        // LEAVING one. A party can legitimately be standing inside a fence — the
        // Hellfire Ramparts wall covers ordinary room floor a few yards from the
        // zone-in point — and rejecting every probe from in there strands them:
        // each tier fails the same screen, so the party is walled into the spot
        // the fence was only ever meant to keep routes out of. So when this
        // stride's own start is inside a region, its leading points are allowed to
        // be too; the screen arms the moment the corridor first reaches clear
        // ground, and any point that goes back in after that is still rejected.
        // Strides chain from the previous clean end, so a stride that starts
        // outside is screened exactly as before.
        if (DcNavPenaltyRegistry::HasVolumes(mapId))
        {
            bool walkingOut = DcNavPenaltyRegistry::IsInsideRegion(mapId, cx, cy, cz);
            for (G3D::Vector3 const& p : candidate)
            {
                if (!DcNavPenaltyRegistry::IsInsideRegion(mapId, p.x, p.y, p.z))
                {
                    walkingOut = false;   // clear ground reached — the screen is live
                    continue;
                }
                if (walkingOut)
                    continue;             // still on the way out of the start region

                LOG_DEBUG("playerbots.dungeonclear",
                          "[dungeon-clear] probe rejected: corridor enters a no-go volume on "
                          "map {} (type=0x{:x}); trying next tier",
                          mapId, outType);
                return false;
            }
        }

        outPoints = std::move(candidate);
        return true;
    }
}

StridedPathfinder::Result StridedPathfinder::Build(Player* bot, uint32 mapId, uint32 bossEntry, float tx, float ty,
                                                   float tz, uint32 maxStrides, bool skipLongRange)
{
    Result result;
    if (!bot || !bot->IsInWorld())
    {
        result.failureReason = "no bot in world";
        return result;
    }

    Map* map = bot->GetMap();
    if (!map)
    {
        result.failureReason = "no map";
        return result;
    }

    // Snap the target so its position is guaranteed on-mesh. The boss list
    // values already snap, but Build() is also called for non-boss targets
    // (Advance recovery, IsReachable for arbitrary hostile pulls) where the
    // caller may pass an unsnapped coord.
    {
        NavmeshSnap::Result const tgt = NavmeshSnap::Snap(map, tx, ty, tz, STRIDE_SNAP_RADIUS);
        if (tgt.ok)
        {
            tx = tgt.x;
            ty = tgt.y;
            tz = tgt.z;
        }
        // Snap failure is OK — we still try to path to the original coord.
        // The final stride's failure mode will surface as failureReason.
    }

    // Anchor route fast-path: trust the hand-tuned data, walk anchors in
    // order. Each anchor is snapped to the navmesh before we hand it off;
    // anchors that won't snap are dropped with a log so the route author
    // sees the typo at server start instead of getting a silent stall.
    if (bossEntry)
    {
        Difficulty const difficulty = map->GetDifficulty();
        if (std::vector<WaypointHint> const* hints = DungeonClearRouteRegistry::Get(mapId, difficulty, bossEntry))
        {
            for (WaypointHint const& h : *hints)
            {
                NavmeshSnap::Result const snapped = NavmeshSnap::Snap(map, h.x, h.y, h.z, STRIDE_SNAP_RADIUS);
                if (!snapped.ok)
                {
                    // Off-mesh anchor → drop it. Pathing to the boss still
                    // works via the next anchor (or the anchor-free stride
                    // fallback if every anchor is bad).
                    LOG_ERROR("playerbots",
                              "[dungeon-clear] Anchor for map {} boss {} at ({:.1f}, {:.1f}, {:.1f}) "
                              "is off the navmesh; dropping. Edit the override route or widen "
                              "STRIDE_SNAP_RADIUS.",
                              mapId, bossEntry, h.x, h.y, h.z);
                    continue;
                }
                PathSegment seg;
                seg.ex = snapped.x;
                seg.ey = snapped.y;
                seg.ez = snapped.z;
                seg.arriveRadius = h.arriveRadius;
                seg.anchored = true;
                seg.jumpDown = HasFlag(h.flags, AnchorFlag::JUMP_DOWN);
                seg.jumpGap = HasFlag(h.flags, AnchorFlag::JUMP_GAP);
                seg.doorGoEntry = HasFlag(h.flags, AnchorFlag::DOOR_AHEAD) ? h.doorGoEntry : 0u;
                // Anchored segments collapse to a single polyline point —
                // no PathGenerator ran for this leg. The follower walks
                // straight to the anchor.
                seg.polyline.push_back(G3D::Vector3(snapped.x, snapped.y, snapped.z));
                result.segments.push_back(seg);
            }
            // If every anchor was off-mesh the route is unusable — fall
            // through to the anchor-free stride loop below.
            if (!result.segments.empty())
            {
                PathSegment goal;
                goal.ex = tx;
                goal.ey = ty;
                goal.ez = tz;
                goal.arriveRadius = ARRIVE_RADIUS;
                goal.polyline.push_back(G3D::Vector3(tx, ty, tz));
                result.segments.push_back(goal);
                result.reachable = true;
                result.complete = true;
                return result;
            }
        }
    }

    // PRIMARY producer: a single long-range Detour query (own big node pool +
    // dungeon-sized buffers) that returns the WHOLE smoothed corridor in one
    // shot, bypassing PathGenerator's 74-poly/74-point caps. When it yields a
    // reachable route we're done — no striding needed, distance irrelevant.
    // The target is already snapped above; LongRangePathfinder re-snaps
    // internally too, which is harmless. On failure (no navmesh, target
    // off-mesh, immediate static obstruction at the start) we fall through to
    // the hardened bee-line / arc / spawn-graph stride tiers below.
    //
    // skipLongRange: the async caller already ran this exact LongRange build on
    // the worker thread and got "unreachable", so re-running it here would just
    // pay the heavy A* again on the map thread. Skip straight to the fallback.
    if (!skipLongRange)
    {
        LongRangePathfinder::Result lr = LongRangePathfinder::Build(bot, tx, ty, tz);
        if (lr.reachable && !lr.segments.empty())
            return lr;
        // The bot being off the navmesh is a definitive answer the strided
        // tiers can't improve on — surface it directly for FARFROMPOLY recovery.
        if (lr.startFarFromPoly)
        {
            result.startFarFromPoly = true;
            result.failureReason = lr.failureReason;
            return result;
        }
        LOG_DEBUG("playerbots.dungeonclear",
                  "[dungeon-clear] long-range producer didn't reach boss {} ({}); "
                  "falling through to strided tiers",
                  bossEntry, lr.failureReason);
    }

    // Anchor-free stride loop. Each stride asks PathGenerator for the real
    // corridor toward the boss (see the per-stride comment below). A
    // PATHFIND_INCOMPLETE result is a truncated-but-valid partial corridor;
    // we advance to its end and pick up the rest on the next stride.
    float cx = bot->GetPositionX();
    float cy = bot->GetPositionY();
    float cz = bot->GetPositionZ();

    for (uint32 stride = 0; stride < maxStrides; ++stride)
    {
        float const distToTarget = Dist3D(cx, cy, cz, tx, ty, tz);
        if (distToTarget <= ARRIVE_RADIUS)
        {
            result.reachable = true;
            result.complete = true;
            return result;
        }

        std::vector<G3D::Vector3> probePoints;
        uint32 outType = 0;
        bool stridedOk = false;
        char const* tier = "none";

        // PRIMARY probe: aim straight at the boss target, every stride,
        // regardless of distance. PathGenerator (Detour) follows the real
        // navmesh corridor around every bend; when the route is too long for
        // one call it returns PATHFIND_INCOMPLETE with a valid *partial*
        // corridor (truncated at Detour's 74-poly cap) whose endpoint is
        // on-mesh and headed at the goal — TryProbe accepts that. We then
        // chunk along THAT corridor, advancing `cur` to the partial's end and
        // re-probing the boss from the new (closer) position next iteration.
        //
        // This is the fix for "almost never finds a path": the old loop only
        // aimed at the boss on the final (<=~188yd) stride and otherwise
        // strided toward a straight-line bee-line point, which walls
        // invalidate the moment a corridor bends. The bee-line / arc /
        // spawn-graph tiers below are now fallbacks for the cases the direct
        // probe genuinely can't satisfy in one call (PATHFIND_INCOMPLETE /* this finder reports a trimmed path as incomplete */ in a huge
        // open room, NOPATH, or an LOS reject across multi-level geometry).
        if (TryProbe(bot, mapId, cx, cy, cz, tx, ty, tz, probePoints, outType))
        {
            stridedOk = true;
            tier = "direct";
        }

        // Probe one fallback direction at decreasing reaches (far-first).
        // Returns true and fills probePoints on the first usable hit. Caps the
        // PathGenerator call at STRIDE_LEN * reach so its partial comes back
        // NORMAL/INCOMPLETE instead of PATHFIND_INCOMPLETE /* this finder reports a trimmed path as incomplete */; the probe point may land
        // on a wall, so the snapped point is what we actually path to.
        auto tryDirection = [&](float angle, char const* label) -> bool
        {
            for (float reach : REACH_FRACTIONS)
            {
                Probe const pr = AimProbe(cx, cy, cz, tx, ty, tz, angle, reach);
                NavmeshSnap::Result const snapped =
                    NavmeshSnap::Snap(map, pr.x, pr.y, pr.z, STRIDE_SNAP_RADIUS);
                if (!snapped.ok)
                    continue;
                if (TryProbe(bot, mapId, cx, cy, cz, snapped.x, snapped.y, snapped.z,
                             probePoints, outType))
                {
                    tier = label;
                    return true;
                }
            }
            return false;
        };

        // FALLBACK 1: straight bee-line toward the boss.
        if (!stridedOk && tryDirection(0.0f, "beeline"))
            stridedOk = true;

        // FALLBACK 2: lateral arcs in order of increasing deviation from the
        // bee-line, for when a wall sits squarely on the straight line.
        if (!stridedOk)
        {
            for (float angle : ARC_ANGLES_RAD)
            {
                if (tryDirection(angle, "arc"))
                {
                    stridedOk = true;
                    break;
                }
            }
        }

        // FALLBACK 3: ask the dungeon's creature spawn graph for a corridor of
        // on-mesh waypoints between us and the target — every spawn is, by
        // definition, a player-walkable point. Pick the first reachable node.
        if (!stridedOk)
        {
            std::vector<SpawnNode> const corridor =
                DungeonSpawnGraph::FindCorridor(map, mapId, cx, cy, cz, tx, ty, tz);
            for (SpawnNode const& node : corridor)
            {
                if (TryProbe(bot, mapId, cx, cy, cz, node.x, node.y, node.z, probePoints, outType))
                {
                    stridedOk = true;
                    tier = "spawngraph";
                    break;
                }
            }
        }

        if (stride == 0 && !stridedOk && (outType & PATHFIND_FARFROMPOLY_START))
        {
            // The bot itself is off the navmesh. Surface this to Advance so
            // it can run a FARFROMPOLY recovery hop.
            result.startFarFromPoly = true;
            result.failureReason = "I'm off the navmesh — need a recovery move";
            LOG_DEBUG("playerbots.dungeonclear",
                      "[dungeon-clear] pathfind boss {} stride 0: bot off navmesh (FARFROMPOLY_START)",
                      bossEntry);
            return result;
        }

        if (!stridedOk)
        {
            if (result.segments.empty())
            {
                result.failureReason = "no navigable route";
                LOG_DEBUG("playerbots.dungeonclear",
                          "[dungeon-clear] pathfind boss {} stride {}: every tier failed "
                          "(lastType=0x{:x}, dist={:.0f}) and no segments yet — giving up",
                          bossEntry, stride, outType, distToTarget);
                return result;
            }
            // We made some progress earlier but this stride is blocked.
            // Return a partial route — Advance walks what we have and
            // rebuilds from the new position next tick.
            result.reachable = true;
            result.complete = false;
            LOG_DEBUG("playerbots.dungeonclear",
                      "[dungeon-clear] pathfind boss {} stride {}: blocked after {} segment(s) "
                      "(dist remaining {:.0f}); returning partial route",
                      bossEntry, stride, result.segments.size(), distToTarget);
            return result;
        }

        // Centre the CHOSEN stride's wall-hugging engine smoothing toward the
        // corridor middle — deferred here from TryProbe so the raycast-heavy
        // findDistanceToWall / VMAP work runs once for the stride we keep, not
        // for every rejected tier. Pin the start (cx,cy,cz) and the stride
        // endpoint; CorridorCenter never moves index 0 or the last point. Done
        // BEFORE probeEnd/stepLen are read so any LOS-truncation from a push is
        // reflected in the endpoint — identical ordering to the old inline
        // centre-then-truncate. If centering somehow breaks even the first hop
        // (it self-validates each push, so this is a backstop), keep the already
        // LOS-clean un-centred probe so the stride is never worse than before.
        {
            std::vector<G3D::Vector3> centred;
            centred.reserve(probePoints.size() + 1);
            centred.push_back(G3D::Vector3(cx, cy, cz));
            centred.insert(centred.end(), probePoints.begin(), probePoints.end());
            CorridorCenter::Center(bot, centred, CorridorCenter::LoadParams());
            size_t const cleanPts = LosCleanPrefixCount(bot, centred);
            if (cleanPts >= 2)
                probePoints.assign(centred.begin() + 1, centred.begin() + cleanPts);
        }

        // TryProbe guarantees at least one point on success.
        G3D::Vector3 const& probeEnd = probePoints.back();
        float const ex = probeEnd.x;
        float const ey = probeEnd.y;
        float const ez = probeEnd.z;

        // Did the stride advance us forward? If not, we're stuck against
        // geometry — bail to avoid an infinite loop of zero-progress probes.
        float const stepLen = Dist3D(cx, cy, cz, ex, ey, ez);
        if (stepLen < MIN_STRIDE_PROGRESS)
        {
            if (result.segments.empty())
            {
                result.failureReason = "path didn't make forward progress";
                LOG_DEBUG("playerbots.dungeonclear",
                          "[dungeon-clear] pathfind boss {} stride {}: tier={} produced "
                          "stepLen={:.1f} < {:.1f}; no forward progress",
                          bossEntry, stride, tier, stepLen, MIN_STRIDE_PROGRESS);
                return result;
            }
            result.reachable = true;
            result.complete = false;
            return result;
        }

        PathSegment seg;
        seg.ex = ex;
        seg.ey = ey;
        seg.ez = ez;
        seg.arriveRadius = HOP_ARRIVE_RADIUS;
        seg.polyline = std::move(probePoints);
        result.segments.push_back(seg);

        // Reached (or near enough to) the target this stride? Tighten the
        // final hop's arrive radius so Advance hands off smoothly to the
        // at-boss trigger. The direct probe can land on the boss from any
        // distance, so this check is no longer gated on a "final stride" flag.
        if (Dist3D(ex, ey, ez, tx, ty, tz) <= ARRIVE_RADIUS)
        {
            result.segments.back().arriveRadius = ARRIVE_RADIUS;
            result.reachable = true;
            result.complete = true;
            LOG_DEBUG("playerbots.dungeonclear",
                      "[dungeon-clear] pathfind boss {}: complete in {} segment(s), last tier={}",
                      bossEntry, result.segments.size(), tier);
            return result;
        }

        cx = ex;
        cy = ey;
        cz = ez;
    }

    // Exhausted maxStrides. Whatever segments we collected let Advance
    // keep walking; the cache will be rebuilt from the new position.
    if (result.segments.empty())
    {
        result.failureReason = "exceeded stride budget without progress";
        LOG_DEBUG("playerbots.dungeonclear",
                  "[dungeon-clear] pathfind boss {}: exhausted {} strides with no segments",
                  bossEntry, maxStrides);
        return result;
    }
    result.reachable = true;
    result.complete = false;
    LOG_DEBUG("playerbots.dungeonclear",
              "[dungeon-clear] pathfind boss {}: exhausted {} strides, {} segment(s) (partial route)",
              bossEntry, maxStrides, result.segments.size());
    return result;
}

bool StridedPathfinder::IsReachable(Player* bot, float tx, float ty, float tz)
{
    if (!bot || !bot->IsInWorld())
        return false;

    // Lightweight gate: a single engine PathGenerator call on the shared
    // pooled query — NO big-pool alloc, no smoothing, no CorridorCenter
    // raycasts, no LOS screen. This is only a "does a navmesh corridor make
    // forward progress toward the target" probe (used by boss selection and
    // the stalled-fallback's nearest-hostile pick); the real route is built
    // later by the full producer in EnsureLongPath, so routing quality is
    // unchanged. Previously this delegated to Build(), which front-ran the
    // whole LongRangePathfinder (a full-dungeon A* + ~2MB query) just to
    // return a bool.
    //
    // PATHFIND_NORMAL is a complete path; PATHFIND_INCOMPLETE is a valid
    // partial corridor truncated at Detour's 74-poly cap (a far-but-reachable
    // target) — both count as reachable, matching Build's direct-probe tier.
    // NOPATH / SHORT / SHORTCUT are straight-line non-corridors and don't.
    PathGenerator gen(bot);
    gen.CalculatePath(tx, ty, tz, /*forceDest*/ false);
    uint32 const type = gen.GetPathType();
    if (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE /* this finder reports a trimmed path as incomplete */ | PATHFIND_SHORTCUT))
        return false;
    // NOT_USING_PATH (no mmap / flying / swimming) goes straight to the dest;
    // treat as reachable, same as Build's TryProbe.
    if (type & PATHFIND_NOT_USING_PATH)
        return true;
    return gen.GetPath().size() >= 2;
}
