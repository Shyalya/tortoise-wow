/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CorridorCenter.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "DetourCommon.h"
#include "DetourExtended.h"   // dtQueryFilterExt
#include "DetourNavMeshQuery.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "PathGenerator.h"    // NAV_* flags, VERTEX_SIZE, INVALID_POLYREF
#include "Player.h"
#include "Timer.h"            // getMSTime
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearGeometry.h"

namespace
{
    // findNearestPoly search box, matching the producers' own snap extents.
    float const POLY_EXTENTS[VERTEX_SIZE] = { 3.0f, 5.0f, 3.0f };

    // A vertical step larger than this between adjacent polyline points marks a
    // stair seam / off-mesh jump / ledge drop — a transition we must not nudge
    // sideways (it would shear the link off its endpoints). Horizontal hugging
    // along a ledge has no Z delta, so it is still centered normally.
    constexpr float JUMP_DZ = 1.8f;

    // LOS-preservation constants, kept in lockstep with the producers'
    // post-centering VMAP screen (StridedPathfinder / LongRangePathfinder).
    // findDistanceToWall / OnMesh validate a push against the *navmesh*, which
    // near a doorway is more permissive than the static door-frame geometry in
    // the VMAP. So a push that stays on-mesh can still make the chord to a
    // neighbour clip the jamb — and the producer's LOS screen then truncates
    // the whole route just short of the door. We therefore re-check every
    // accepted push against the VMAP and revert any that would break a
    // sightline, so centering can never cause that truncation.
    // The static-VMAP chord check is shared with the route producers — see
    // DungeonClearGeometry::ChordClear (short hops clear, null map fails open,
    // ~eye-height raycast bump). Pull it in so the call sites stay unchanged.
    using DungeonClearGeometry::ChordClear;

    // Modest A* pool for the convenience overload's own query. findDistanceTo
    // Wall / findNearestPoly are local searches — they do not need the
    // dungeon-length pool the primary producer allocates.
    constexpr int CC_NODE_POOL = 2048;

    // A point counts as "moved" by the centering pass (and so a smoothing
    // candidate) when its accepted position differs from its original by more
    // than this. Below it the push is visually nil and can't seed a sawtooth,
    // so smoothing it would spend raycasts for no change. Squared, in yd^2.
    constexpr float CC_MOVED_EPS_SQ = 0.05f * 0.05f;

    using ManagedQuery = std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)>;

    // Distance to the nearest wall from a G3D world point, plus the unit normal
    // pointing AWAY from that wall (in Detour {y,z,x} order). Returns false when
    // the point isn't on a poly or the query fails. On success `outDist` is the
    // wall distance and outNormal carries the (Detour-space) escape direction.
    bool WallProbe(dtNavMeshQuery const* query, dtQueryFilter const* filter,
                   G3D::Vector3 const& p, float searchRadius,
                   float& outDist, float outNormal[VERTEX_SIZE])
    {
        float const pos[VERTEX_SIZE] = { p.y, p.z, p.x };  // Detour order
        dtPolyRef ref = INVALID_POLYREF;
        float nearest[VERTEX_SIZE] = { 0, 0, 0 };
        if (dtStatusFailed(query->findNearestPoly(pos, POLY_EXTENTS, filter, &ref, nearest)) ||
            ref == INVALID_POLYREF)
            return false;

        float hitPos[VERTEX_SIZE];
        if (dtStatusFailed(query->findDistanceToWall(ref, pos, searchRadius, filter,
                                                     &outDist, hitPos, outNormal)))
            return false;
        return true;
    }

    // True iff a G3D world point still sits on a walkable poly.
    bool OnMesh(dtNavMeshQuery const* query, dtQueryFilter const* filter, G3D::Vector3 const& p)
    {
        float const pos[VERTEX_SIZE] = { p.y, p.z, p.x };
        dtPolyRef ref = INVALID_POLYREF;
        float nearest[VERTEX_SIZE] = { 0, 0, 0 };
        return dtStatusSucceed(query->findNearestPoly(pos, POLY_EXTENTS, filter, &ref, nearest)) &&
               ref != INVALID_POLYREF;
    }

    // Laplacian de-kink. The per-point centering push is independent and the
    // nearest wall can flip sides between adjacent points, leaving a sawtooth
    // the spline follower renders as stutter. Each pass replaces every interior
    // point with a 1-2-1 weighted average of itself and its neighbours, pinning
    // the endpoints and skipping vertical transitions. Jacobi (reads a stable
    // snapshot per pass) so the smoothing is symmetric, not direction-biased.
    // Every averaged point is re-pinned to a walkable Z and reverted if it would
    // leave the mesh; the producer's LOS screen still runs afterwards.
    //
    // Only points the centering pass actually pushed (`moved`) can carry the
    // sawtooth this exists to remove; untouched points are the raw taut
    // polyline (collinear runs → the 1-2-1 average is identically the point
    // itself), and re-validating them ran the bulk of Finalize's per-install
    // map-thread cost — UpdateAllowedPositionZ + OnMesh + 2 raycasts per point,
    // every install — for a guaranteed no-op. So restrict the pass to the moved
    // points and their neighbourhood. Each Jacobi pass reads the previous
    // pass's snapshot, so a push at index m ripples at most one point further
    // out per pass; dilating the mask by `iterations` on each side makes the
    // restricted pass bit-for-bit identical to the old unconditional pass on
    // every point it still touches (points outside can provably never change).
    void SmoothPolyline(dtNavMeshQuery const* query, dtQueryFilter const* filter,
                        Player* bot, std::vector<G3D::Vector3>& pts, int iterations,
                        std::vector<bool> const& moved)
    {
        if (iterations <= 0 || pts.size() < 3)
            return;

        std::vector<bool> active(pts.size(), false);
        size_t const reach = static_cast<size_t>(iterations);
        for (size_t i = 0; i < moved.size() && i < pts.size(); ++i)
        {
            if (!moved[i])
                continue;
            size_t const lo = i > reach ? i - reach : 0;
            size_t const hi = std::min(pts.size() - 1, i + reach);
            for (size_t j = lo; j <= hi; ++j)
                active[j] = true;
        }

        for (int it = 0; it < iterations; ++it)
        {
            std::vector<G3D::Vector3> const src = pts;  // stable snapshot (Jacobi)
            for (size_t i = 1; i + 1 < src.size(); ++i)
            {
                if (!active[i])
                    continue;
                if (std::fabs(src[i].z - src[i - 1].z) > JUMP_DZ ||
                    std::fabs(src[i + 1].z - src[i].z) > JUMP_DZ)
                    continue;

                G3D::Vector3 avg = src[i - 1] * 0.25f + src[i] * 0.5f + src[i + 1] * 0.25f;
                bot->UpdateAllowedPositionZ(avg.x, avg.y, avg.z);
                // On-mesh AND sightline-preserving: an average that drifts the
                // point across a doorjamb stays on the navmesh but would clip
                // the frame in the VMAP, so screen it against both neighbours
                // (pts[i-1] is this pass's already-committed value).
                if (OnMesh(query, filter, avg) &&
                    ChordClear(bot, pts[i - 1], avg) && ChordClear(bot, avg, src[i + 1]))
                    pts[i] = avg;
            }
        }
    }
}

CorridorCenter::Params CorridorCenter::LoadParams()
{
    // These four options change only on a manual `.reload config`, yet the
    // strided fallback calls this once per probe per stride — each call doing
    // four string-keyed config lookups. Cache for a second so the lookups don't
    // repeat in a hot loop while a live reload still takes effect promptly.
    // thread_local keeps it lock-free across the map-update worker pool.
    thread_local Params cached;
    thread_local uint32 cachedAt = 0;
    uint32 const now = getMSTime();
    if (cachedAt != 0 && now - cachedAt < 1000)
        return cached;

    // Server-only keys: read with an empty run owner so the override store is
    // never consulted (this runs on the map-update worker threads).
    Params p;
    p.enable      = DcSettings::GetBool(ObjectGuid::Empty, "PathCenterEnable");
    p.clearance   = DcSettings::GetFloat(ObjectGuid::Empty, "PathWallClearance");
    p.maxPush     = DcSettings::GetFloat(ObjectGuid::Empty, "PathCenterMaxPush");
    p.smoothIters = DcSettings::GetInt(ObjectGuid::Empty, "PathCenterSmoothIters");
    p.clearance   = std::max(0.0f, p.clearance);
    p.maxPush     = std::max(0.0f, p.maxPush);
    p.smoothIters = std::max(0, p.smoothIters);

    cached = p;
    cachedAt = now;
    return p;
}

void CorridorCenter::Center(dtNavMeshQuery const* query, dtQueryFilter const* filter,
                            Player* bot, std::vector<G3D::Vector3>& pts, Params const& params)
{
    if (!params.enable || !query || !filter || !bot || pts.size() < 3 || params.clearance <= 0.0f)
        return;

    // Wide enough to see the OPPOSITE wall of a corridor up to ~2*clearance
    // wide, so the midpoint correction below can converge to the medial axis.
    float const searchRadius = params.clearance * 2.0f + 2.0f;

    // Which interior points this pass actually pushed off the raw line — the
    // only ones the following smoothing pass needs to touch (see SmoothPolyline).
    std::vector<bool> moved(pts.size(), false);

    // Never move the start (index 0) or the target (last point).
    for (size_t i = 1; i + 1 < pts.size(); ++i)
    {
        // Skip vertical transitions (jumps / stairs / ledge drops).
        if (std::fabs(pts[i].z - pts[i - 1].z) > JUMP_DZ ||
            std::fabs(pts[i + 1].z - pts[i].z) > JUMP_DZ)
            continue;

        float dist1 = 0.0f;
        float normal[VERTEX_SIZE] = { 0, 0, 0 };
        if (!WallProbe(query, filter, pts[i], searchRadius, dist1, normal))
            continue;
        if (dist1 >= params.clearance)
            continue;  // already clear of every wall — open room or wide hall

        // Escape direction back in G3D world order (normal is Detour {y,z,x}).
        G3D::Vector3 dir(normal[2], normal[0], normal[1]);
        float const dlen = dir.length();
        if (dlen < 0.5f)            // degenerate normal — skip rather than guess
            continue;
        dir /= dlen;

        float push = std::min(params.clearance - dist1, params.maxPush);

        // One-sided nudge off the nearest wall.
        G3D::Vector3 cand = pts[i] + dir * push;

        // In a narrow corridor that push heads toward the far wall; measure it
        // and, if it's close, settle on the midpoint of the two walls instead.
        float dist2 = 0.0f;
        float n2[VERTEX_SIZE] = { 0, 0, 0 };
        if (WallProbe(query, filter, cand, searchRadius, dist2, n2) && dist2 < params.clearance)
        {
            // Walls are (dist1) behind and (push + dist2) ahead along dir; the
            // centered offset from pts[i] is half their separation.
            float centered = std::clamp((push + dist2 - dist1) * 0.5f, 0.0f, params.maxPush);
            cand = pts[i] + dir * centered;
        }

        // Re-pin to a walkable Z and re-validate on-mesh; halve the push twice
        // before giving up, so a slightly-too-far nudge degrades gracefully.
        // The push must ALSO keep the chords to both neighbours clear in the
        // VMAP — otherwise a navmesh-legal nudge near a doorway shoves the
        // approach off-axis, the chord clips the jamb, and the producer's LOS
        // screen truncates the route right before the door (the stutter-step-
        // up-to-the-door bug). pts[i-1] is already centred; pts[i+1] is still
        // its original taut value (processed next iteration). Halving the push
        // on a blocked chord lets a slightly-too-far nudge settle to a clean
        // offset instead of reverting all the way to the wall-hugging point.
        G3D::Vector3 accepted = pts[i];
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            G3D::Vector3 test = cand;
            bot->UpdateAllowedPositionZ(test.x, test.y, test.z);
            if (OnMesh(query, filter, test) &&
                ChordClear(bot, pts[i - 1], test) &&
                ChordClear(bot, test, pts[i + 1]))
            {
                accepted = test;
                break;
            }
            // Pull halfway back toward the original point and retry.
            cand = (cand + pts[i]) * 0.5f;
        }
        if ((accepted - pts[i]).squaredLength() > CC_MOVED_EPS_SQ)
            moved[i] = true;
        pts[i] = accepted;
    }

    // Pull the per-point sawtooth back into a flowing line — only around the
    // points the loop above actually pushed (moved); the rest is untouched raw
    // corridor that the 1-2-1 average leaves exactly where it is.
    SmoothPolyline(query, filter, bot, pts, params.smoothIters, moved);
}

void CorridorCenter::Center(Player* bot, std::vector<G3D::Vector3>& pts, Params const& params)
{
    if (!params.enable || !bot || pts.size() < 3 || params.clearance <= 0.0f)
        return;

    Map* map = bot->GetMap();
    if (!map)
        return;
    dtNavMesh const* navMesh = map->GetMapCollisionData().GetMMapData().GetNavMesh();
    if (!navMesh)
        return;

    // Reuse one query per thread instead of allocating a fresh node pool on
    // every probe/stride. init() rebinds the mesh and clears (reuses) the pool
    // when it is already >= CC_NODE_POOL, so this is both cheaper and safe
    // against a mesh being replaced at the same address. thread_local because
    // map updates run across a worker pool and Detour mutates the node pool
    // during search; the unique_ptr frees it at thread exit.
    thread_local ManagedQuery query(nullptr, &dtFreeNavMeshQuery);
    if (!query)
        query.reset(dtAllocNavMeshQuery());
    if (!query || dtStatusFailed(query->init(navMesh, CC_NODE_POOL)))
        return;

    dtQueryFilterExt filter;
    filter.setIncludeFlags(static_cast<uint16>(NAV_GROUND | NAV_WATER | NAV_MAGMA));
    filter.setExcludeFlags(0);
    // Match the route producers' liquid preference so the centering wall-probe /
    // smoothing pass weighs polys the same way the corridor was routed.
    DungeonClearGeometry::ApplyLiquidAreaCosts(filter);

    Center(query.get(), &filter, bot, pts, params);
}
