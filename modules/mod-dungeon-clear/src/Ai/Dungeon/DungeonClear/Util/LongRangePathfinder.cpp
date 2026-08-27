/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "LongRangePathfinder.h"

#include "Ai/Dungeon/DungeonClear/Util/CorridorCenter.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRouteFilter.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "DetourCommon.h"
#include "DetourExtended.h"   // dtQueryFilterExt
#include "Log.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "PathGenerator.h"    // dt navmesh/query headers, NAV_* flags, VERTEX_SIZE, INVALID_POLYREF, G3D::Vector3
#include "Player.h"

namespace
{
    // ---- buffer / pool sizing -------------------------------------------
    // The whole point of this pathfinder: sizes large enough that a full
    // dungeon route fits in one call, so distance stops mattering. The
    // engine PathGenerator caps these at 74 (≈296yd); we don't.

    // Poly corridor output of findPath. A winding dungeon route is typically
    // a couple hundred polys; 1024 is comfortable headroom.
    constexpr uint32 LR_MAX_POLYS = 1024;

    // Smoothed point output. At SMOOTH_STEP=4yd, 4096 points ≈ 16,000yd —
    // far beyond any single dungeon route. Truncation here just yields a
    // partial route (Advance rebuilds from the new position next tick).
    constexpr uint32 LR_MAX_POINTS = 4096;

    // A* search-node pool for our OWN query. The shared engine query uses
    // 1024 (MMapMgr.cpp) — too few for a dungeon-length search. 65535 is
    // Detour's per-query ceiling (node indices are 16-bit).
    constexpr int LR_NODE_POOL = 65535;

    // Match PathGenerator's smoothing cadence so the polyline geometry is
    // identical to what the follower is tuned for.
    constexpr float LR_SMOOTH_STEP = 4.0f;   // SMOOTH_PATH_STEP_SIZE
    constexpr float LR_SMOOTH_SLOP = 0.3f;   // SMOOTH_PATH_SLOP

    // Arrive radii match StridedPathfinder so Advance/at-boss handoff is
    // unchanged regardless of which producer built the route.
    constexpr float ARRIVE_RADIUS     = 8.0f;
    constexpr float HOP_ARRIVE_RADIUS = 6.0f;

    // ---- LOS screen ------------------------------------------------------
    // The graze-bridged clean-prefix walk now lives in DungeonClearGeometry
    // (shared with StridedPathfinder / CorridorCenter). pts[0] is the start and
    // the return value includes it, same contract as before.
    using DungeonClearGeometry::LosCleanPrefixCount;

    // ---- our own big-pool query, reused across calls ---------------------
    // The query carries a ~2MB 65535-node A* pool. Re-allocating it on every
    // Build (boss change / 15s TTL / stuck rebuild / reachability probe) is
    // pure churn, so we keep ONE query per thread and re-init() it each call.
    // dtNavMeshQuery::init() rebinds the mesh and merely clears the pool when
    // it is already >= LR_NODE_POOL (it only reallocs when too small), so the
    // re-init reuses the existing pool memory — and rebinding the mesh every
    // call is exactly what makes this safe against the "unloaded instance
    // replaced by a new map's mesh at the same address" hazard the per-call
    // alloc was guarding against. thread_local because map updates run across a
    // worker pool and Detour mutates the per-query node pool during search; the
    // unique_ptr frees it at thread exit.
    using ManagedQuery = std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)>;

    dtNavMeshQuery* AcquireQuery(dtNavMesh const* mesh)
    {
        thread_local ManagedQuery tlsQuery(nullptr, &dtFreeNavMeshQuery);
        if (!tlsQuery)
            tlsQuery.reset(dtAllocNavMeshQuery());
        if (!tlsQuery || dtStatusFailed(tlsQuery->init(mesh, LR_NODE_POOL)))
            return nullptr;
        return tlsQuery.get();
    }

    // ---- ports of PathGenerator's smooth-path internals ------------------
    // Identical math to PathGenerator (so polyline geometry matches), but
    // parameterised on our query and sized to LR_MAX_POLYS instead of the
    // hardcoded 74. The slope check is dropped — routing never set it.

    bool InRangeYZX(float const* v1, float const* v2, float r, float h)
    {
        float const dx = v2[0] - v1[0];
        float const dy = v2[1] - v1[1];  // elevation
        float const dz = v2[2] - v1[2];
        return (dx * dx + dz * dz) < r * r && std::fabs(dy) < h;
    }

    uint32 FixupCorridor(dtPolyRef* path, uint32 npath, uint32 maxPath,
                         dtPolyRef const* visited, uint32 nvisited)
    {
        int32 furthestPath = -1;
        int32 furthestVisited = -1;

        for (int32 i = npath - 1; i >= 0; --i)
        {
            bool found = false;
            for (int32 j = nvisited - 1; j >= 0; --j)
            {
                if (path[i] == visited[j])
                {
                    furthestPath = i;
                    furthestVisited = j;
                    found = true;
                }
            }
            if (found)
                break;
        }

        if (furthestPath == -1 || furthestVisited == -1)
            return npath;

        uint32 req = nvisited - furthestVisited;
        uint32 orig = uint32(furthestPath + 1) < npath ? furthestPath + 1 : npath;
        uint32 size = npath > orig ? npath - orig : 0;
        if (req + size > maxPath)
            size = maxPath - req;

        if (size)
            memmove(path + req, path + orig, size * sizeof(dtPolyRef));

        for (uint32 i = 0; i < req; ++i)
            path[i] = visited[(nvisited - 1) - i];

        return req + size;
    }

    bool GetSteerTarget(dtNavMeshQuery const* query, float const* startPos, float const* endPos,
                        float minTargetDist, dtPolyRef const* path, uint32 pathSize,
                        float* steerPos, unsigned char& steerPosFlag, dtPolyRef& steerPosRef)
    {
        static const uint32 MAX_STEER_POINTS = 3;
        float steerPath[MAX_STEER_POINTS * VERTEX_SIZE];
        unsigned char steerPathFlags[MAX_STEER_POINTS];
        dtPolyRef steerPathPolys[MAX_STEER_POINTS];
        uint32 nsteerPath = 0;
        dtStatus dtResult = query->findStraightPath(startPos, endPos, path, pathSize,
            steerPath, steerPathFlags, steerPathPolys, (int*)&nsteerPath, MAX_STEER_POINTS);
        if (!nsteerPath || dtStatusFailed(dtResult))
            return false;

        uint32 ns = 0;
        while (ns < nsteerPath)
        {
            if ((steerPathFlags[ns] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ||
                !InRangeYZX(&steerPath[ns * VERTEX_SIZE], startPos, minTargetDist, 1000.0f))
                break;
            ns++;
        }
        if (ns >= nsteerPath)
            return false;

        dtVcopy(steerPos, &steerPath[ns * VERTEX_SIZE]);
        steerPos[1] = startPos[1];  // keep Z value
        steerPosFlag = steerPathFlags[ns];
        steerPosRef = steerPathPolys[ns];
        return true;
    }

    dtStatus FindSmoothPath(dtNavMeshQuery const* query, dtNavMesh const* navMesh,
                            dtQueryFilter const* filter,
                            float const* startPos, float const* endPos,
                            dtPolyRef const* polyPath, uint32 polyPathSize,
                            float* smoothPath, int* smoothPathSize, uint32 maxSmoothPathSize)
    {
        *smoothPathSize = 0;
        uint32 nsmoothPath = 0;

        // Reused across calls (sized once per thread); the entries we read are
        // overwritten below before use, so no per-call zero-fill is needed.
        thread_local std::vector<dtPolyRef> polys(LR_MAX_POLYS);
        polyPathSize = std::min<uint32>(polyPathSize, LR_MAX_POLYS);
        memcpy(polys.data(), polyPath, sizeof(dtPolyRef) * polyPathSize);
        uint32 npolys = polyPathSize;

        float iterPos[VERTEX_SIZE], targetPos[VERTEX_SIZE];

        if (polyPathSize > 1)
        {
            if (dtStatusFailed(query->closestPointOnPolyBoundary(polys[0], startPos, iterPos)))
                return DT_FAILURE;
            if (dtStatusFailed(query->closestPointOnPolyBoundary(polys[npolys - 1], endPos, targetPos)))
                return DT_FAILURE;
        }
        else
        {
            dtVcopy(iterPos, startPos);
            dtVcopy(targetPos, endPos);
        }

        dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
        nsmoothPath++;

        while (npolys && nsmoothPath < maxSmoothPathSize)
        {
            float steerPos[VERTEX_SIZE];
            unsigned char steerPosFlag;
            dtPolyRef steerPosRef = INVALID_POLYREF;

            if (!GetSteerTarget(query, iterPos, targetPos, LR_SMOOTH_SLOP, polys.data(), npolys,
                                steerPos, steerPosFlag, steerPosRef))
                break;

            bool endOfPath = (steerPosFlag & DT_STRAIGHTPATH_END) != 0;
            bool offMeshConnection = (steerPosFlag & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0;

            float delta[VERTEX_SIZE];
            dtVsub(delta, steerPos, iterPos);
            float len = dtMathSqrtf(dtVdot(delta, delta));
            if ((endOfPath || offMeshConnection) && len < LR_SMOOTH_STEP)
                len = 1.0f;
            else
                len = LR_SMOOTH_STEP / len;

            float moveTgt[VERTEX_SIZE];
            dtVmad(moveTgt, iterPos, delta, len);

            float result[VERTEX_SIZE];
            const static uint32 MAX_VISIT_POLY = 16;
            dtPolyRef visited[MAX_VISIT_POLY];
            uint32 nvisited = 0;
            if (dtStatusFailed(query->moveAlongSurface(polys[0], iterPos, moveTgt, filter, result,
                                                       visited, (int*)&nvisited, MAX_VISIT_POLY)))
                return DT_FAILURE;
            npolys = FixupCorridor(polys.data(), npolys, LR_MAX_POLYS, visited, nvisited);

            query->getPolyHeight(polys[0], result, &result[1]);  // best-effort, like the engine
            result[1] += 0.5f;
            dtVcopy(iterPos, result);

            if (endOfPath && InRangeYZX(iterPos, steerPos, LR_SMOOTH_SLOP, 1.0f))
            {
                dtVcopy(iterPos, targetPos);
                if (nsmoothPath < maxSmoothPathSize)
                {
                    dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
                    nsmoothPath++;
                }
                break;
            }
            else if (offMeshConnection && InRangeYZX(iterPos, steerPos, LR_SMOOTH_SLOP, 1.0f))
            {
                dtPolyRef prevRef = INVALID_POLYREF;
                dtPolyRef polyRef = polys[0];
                uint32 npos = 0;
                while (npos < npolys && polyRef != steerPosRef)
                {
                    prevRef = polyRef;
                    polyRef = polys[npos];
                    npos++;
                }

                for (uint32 i = npos; i < npolys; ++i)
                    polys[i - npos] = polys[i];
                npolys -= npos;

                float connectionStartPos[VERTEX_SIZE], connectionEndPos[VERTEX_SIZE];
                if (dtStatusSucceed(navMesh->getOffMeshConnectionPolyEndPoints(prevRef, polyRef,
                                                                              connectionStartPos, connectionEndPos)))
                {
                    if (nsmoothPath < maxSmoothPathSize)
                    {
                        dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], connectionStartPos);
                        nsmoothPath++;
                    }
                    dtVcopy(iterPos, connectionEndPos);
                    if (dtStatusFailed(query->getPolyHeight(polys[0], iterPos, &iterPos[1])))
                        return DT_FAILURE;
                    iterPos[1] += 0.5f;
                }
            }

            if (nsmoothPath < maxSmoothPathSize)
            {
                dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
                nsmoothPath++;
            }
        }

        *smoothPathSize = nsmoothPath;
        return DT_SUCCESS;
    }
}

LongRangePathfinder::RawResult LongRangePathfinder::BuildCoreFromMesh(
    dtNavMesh const* navMesh, uint32 mapId, float sx, float sy, float sz, float tx, float ty, float tz)
{
    // WORKER-SAFE: no Player*/Map*/VMAP here — only the navmesh + plain floats.
    RawResult result;
    result.tx = tx;
    result.ty = ty;
    result.tz = tz;

    if (!navMesh)
    {
        result.failureReason = "no navmesh on map";
        return result;
    }

    dtNavMeshQuery* query = AcquireQuery(navMesh);
    if (!query)
    {
        result.failureReason = "navmesh query alloc/init failed";
        return result;
    }

    // Player filter — matches PathGenerator's "assume Player" branch. DcRouteFilter
    // adds the per-map no-go volume penalty that steers the A* corridor off navmesh
    // shortcuts a real player can't follow; it derives from dtQueryFilterExt, so
    // the include/exclude flags and the liquid area costs below apply exactly as
    // before, and on a map with no volume the cost is identical to stock.
    // The start goes in so a route that BEGINS inside a no-go region can leave it:
    // the fence exists to keep routes out of a bad spot, not to strand a party
    // that is already standing in one (see DcRouteFilter's header).
    DcRouteFilter filter(mapId, sx, sy, sz);
    filter.setIncludeFlags(static_cast<uint16>(NAV_GROUND | NAV_WATER | NAV_MAGMA));
    filter.setExcludeFlags(0);
    // Prefer land: water/magma stay traversable but cost more, so the A* corridor
    // only dips into liquid when no near-as-short dry route exists.
    DungeonClearGeometry::ApplyLiquidAreaCosts(filter);

    // Detour coordinate order is {y, z, x}.
    float const startPt[VERTEX_SIZE] = { sy, sz, sx };
    float const endPt[VERTEX_SIZE]   = { ty, tz, tx };

    float extents[VERTEX_SIZE]    = { 3.0f, 5.0f, 3.0f };
    float bigExtents[VERTEX_SIZE] = { 3.0f, 50.0f, 3.0f };  // taller box, like GetPolyByLocation's retry

    dtPolyRef startRef = INVALID_POLYREF;
    dtPolyRef endRef = INVALID_POLYREF;
    float startNearest[VERTEX_SIZE] = { 0, 0, 0 };
    float endNearest[VERTEX_SIZE]   = { 0, 0, 0 };

    // Start poly. Failure here means the bot is off the navmesh — surface it
    // so Advance can run a FARFROMPOLY recovery hop (same as the strided path).
    if (dtStatusFailed(query->findNearestPoly(startPt, extents, &filter, &startRef, startNearest)) ||
        startRef == INVALID_POLYREF)
    {
        if (dtStatusFailed(query->findNearestPoly(startPt, bigExtents, &filter, &startRef, startNearest)) ||
            startRef == INVALID_POLYREF)
        {
            result.startFarFromPoly = true;
            result.failureReason = "bot off navmesh (no start poly)";
            // Kept as a permanent diagnostic: a silent start-poly failure is
            // always a positional anomaly worth a log line (live it exposed a
            // tank standing at Z=203 over the Deadmines - overworld height
            // inside the instance map - which no amount of path-log reading
            // would have revealed).
            {
                int loaded = 0;
                for (int i = 0; i < navMesh->getMaxTiles(); ++i)
                    if (navMesh->getTile(i) && navMesh->getTile(i)->header)
                        ++loaded;
                sLog.outString("[DC-DIAG] no start poly: map=%u pos=(%.1f,%.1f,%.1f) maxTiles=%d loadedTiles=%d",
                               mapId, sx, sy, sz, navMesh->getMaxTiles(), loaded);
            }
            return result;
        }
    }

    // End poly. Failure → let the strided fallback try its snap/arc tiers.
    if (dtStatusFailed(query->findNearestPoly(endPt, extents, &filter, &endRef, endNearest)) ||
        endRef == INVALID_POLYREF)
    {
        if (dtStatusFailed(query->findNearestPoly(endPt, bigExtents, &filter, &endRef, endNearest)) ||
            endRef == INVALID_POLYREF)
        {
            result.failureReason = "target off navmesh (no end poly)";
            return result;
        }
    }

    // Full poly corridor in one call — the big node pool + buffer is what
    // lifts the 74-poly cap. Scratch is reused per thread (findPath fills
    // [0..npolys] before we read it, so no per-call zero-fill is needed).
    thread_local std::vector<dtPolyRef> corridor(LR_MAX_POLYS);
    int npolys = 0;
    dtStatus const pathStatus = query->findPath(startRef, endRef, startNearest, endNearest, &filter,
                                                corridor.data(), &npolys, static_cast<int>(LR_MAX_POLYS));
    if (dtStatusFailed(pathStatus) || npolys <= 0)
    {
        result.failureReason = "findPath returned no corridor";
        return result;
    }
    result.corridorComplete = (corridor[npolys - 1] == endRef);

    // Smooth the whole corridor into a polyline. Scratch reused per thread;
    // FindSmoothPath writes [0..nsmooth) before we read it back.
    thread_local std::vector<float> smooth(LR_MAX_POINTS * VERTEX_SIZE);
    int nsmooth = 0;
    dtStatus const smoothStatus = FindSmoothPath(query, navMesh, &filter, startNearest, endNearest,
                                                 corridor.data(), static_cast<uint32>(npolys),
                                                 smooth.data(), &nsmooth, LR_MAX_POINTS);

    // Did the smoothing actually ARRIVE? FindSmoothPath walks the corridor
    // with moveAlongSurface, i.e. sliding across the polygons, and a climb
    // that gains twenty yards over very little ground defeats that walk: the
    // Deadmines ship ramp stopped it every time. What came back was either a
    // hard failure or - worse - a stub ending halfway, and `reachable` was
    // set for the stub because nothing here ever checked the far end. Both
    // ways the party gave up beside the ship and ended in the harbour water,
    // and [DC-MESH] reported the boss unreachable although a plain Detour
    // query walks that corridor in 55 polygons (measured with
    // tools/meshprobe.cpp against the very tiles the core loads).
    bool arrived = !dtStatusFailed(smoothStatus) && nsmooth >= 2;
    if (arrived)
    {
        float const* last = &smooth[(nsmooth - 1) * VERTEX_SIZE];
        float const dx = last[0] - endNearest[0];
        float const dy = last[1] - endNearest[1];
        float const dz = last[2] - endNearest[2];
        arrived = (dx * dx + dy * dy + dz * dz) <= (6.0f * 6.0f);
    }

    if (!arrived)
    {
        // The corridor is sound; only the way we turned it into points was
        // not. findStraightPath gives the corner points of that same
        // corridor - the canonical line through it - and it has no surface
        // walk to get stuck on. Coarser than the smoothed polyline (corners
        // can sit 30-40yd apart), but every corner lies on the corridor, so
        // walking corner to corner stays on the ground the corridor covers.
        thread_local std::vector<float> corners(LR_MAX_POLYS * VERTEX_SIZE);
        thread_local std::vector<unsigned char> cornerFlags(LR_MAX_POLYS);
        thread_local std::vector<dtPolyRef> cornerRefs(LR_MAX_POLYS);
        int ncorners = 0;
        dtStatus const cornerStatus =
            query->findStraightPath(startNearest, endNearest, corridor.data(), npolys,
                                    corners.data(), cornerFlags.data(), cornerRefs.data(),
                                    &ncorners, static_cast<int>(LR_MAX_POLYS), 0);
        if (dtStatusFailed(cornerStatus) || ncorners < 2)
        {
            result.failureReason = "smooth-path build failed and no corner path either";
            return result;
        }
        result.rawPts.reserve(ncorners);
        for (int i = 0; i < ncorners; ++i)
            result.rawPts.emplace_back(corners[i * VERTEX_SIZE + 2], corners[i * VERTEX_SIZE + 0],
                                       corners[i * VERTEX_SIZE + 1]);
        result.reachable = true;
        return result;
    }

    // Convert to world-space G3D points. Z here is the navmesh getPolyHeight
    // estimate from FindSmoothPath; the live-state refinement
    // (Player::UpdateAllowedPositionZ) is deferred to Finalize on the map
    // thread, so this loop stays free of any Player/Map access. rawPts[0] is
    // the (snapped) start point.
    result.rawPts.reserve(nsmooth);
    for (int i = 0; i < nsmooth; ++i)
        result.rawPts.emplace_back(smooth[i * VERTEX_SIZE + 2], smooth[i * VERTEX_SIZE + 0],
                                   smooth[i * VERTEX_SIZE + 1]);

    result.reachable = true;
    return result;
}

LongRangePathfinder::Result LongRangePathfinder::Finalize(Player* bot, RawResult const& raw)
{
    // MAP-THREAD ONLY: from here on we touch the live Player and VMAP.
    Result result;
    if (!raw.reachable)
    {
        // Propagate the core's verdict so Advance/the strided fallback see the
        // same failure they would have from the synchronous build.
        result.startFarFromPoly = raw.startFarFromPoly;
        result.failureReason = raw.failureReason;
        return result;
    }
    if (!bot || !bot->IsInWorld())
    {
        result.failureReason = "no bot in world";
        return result;
    }
    if (raw.rawPts.size() < 2)
    {
        result.failureReason = "smooth-path build failed";
        return result;
    }

    // Pin each point to a walkable Z, exactly as PathGenerator::NormalizePath
    // does. (Was inline in the convert loop pre-split; moved here because it
    // reads terrain/VMAP.) pts[0] is the (snapped) start.
    std::vector<G3D::Vector3> pts = raw.rawPts;
    for (G3D::Vector3& p : pts)
        bot->UpdateAllowedPositionZ(p.x, p.y, p.z);

    // Nudge the taut, wall-hugging smoothed line toward the corridor centre so
    // the bot stops grazing walls/ledges and clipping wall-lining props. The
    // start and target points are pinned; every nudged point is re-validated
    // on-mesh inside Center(). The LOS screen below then verifies the CENTRED
    // line, so any push that would cross geometry is still truncated away.
    CorridorCenter::Center(bot, pts, CorridorCenter::LoadParams());

    // LOS-screen the smoothed corridor (corner grazes bridged; a sustained
    // wall-crossing truncates to the verified prefix). cleanPts counts from
    // index 0 and includes the leading start point.
    size_t const cleanPts = LosCleanPrefixCount(bot, pts);
    if (cleanPts < 2)
    {
        // Corridor heads straight into static geometry from the start — hand
        // off to the strided fallback, which has bee-line/arc tiers for this.
        result.failureReason = "LOS: no clean corridor from start";
        LOG_DEBUG("playerbots.dungeonclear",
                  "[dungeon-clear] long-range route rejected by LOS at start ({} smoothed pts)",
                  raw.rawPts.size());
        return result;
    }

    // Drop the leading start point (== bot position) per the PathSegment
    // convention; the remaining points are this segment's walkable polyline.
    std::vector<G3D::Vector3> polyline(pts.begin() + 1, pts.begin() + cleanPts);
    if (polyline.empty())
    {
        result.failureReason = "no usable corridor after start point";
        return result;
    }

    bool const losTruncated = (cleanPts < pts.size());

    G3D::Vector3 const& last = polyline.back();
    float const dx = raw.tx - last.x;
    float const dy = raw.ty - last.y;
    float const dz = raw.tz - last.z;
    bool const nearTarget = (dx * dx + dy * dy + dz * dz) <= (ARRIVE_RADIUS * ARRIVE_RADIUS);
    bool const complete = raw.corridorComplete && !losTruncated && nearTarget;

    PathSegment seg;
    seg.ex = last.x;
    seg.ey = last.y;
    seg.ez = last.z;
    seg.arriveRadius = complete ? ARRIVE_RADIUS : HOP_ARRIVE_RADIUS;
    seg.anchored = false;
    seg.polyline = std::move(polyline);
    result.segments.push_back(std::move(seg));

    result.reachable = true;
    result.complete = complete;

    LOG_DEBUG("playerbots.dungeonclear",
              "[dungeon-clear] long-range route: {} smoothed pts, {} usable ({}), complete={}",
              raw.rawPts.size(), result.segments.back().polyline.size(),
              losTruncated ? "LOS-truncated" : "full", complete);
    return result;
}

LongRangePathfinder::Result LongRangePathfinder::Build(Player* bot, float tx, float ty, float tz)
{
    Result result;
    if (!bot || !bot->IsInWorld())
    {
        result.failureReason = "no bot in world";
        return result;
    }

    Map* map = bot->FindMap();
    if (!map)
    {
        result.failureReason = "no map";
        return result;
    }

    dtNavMesh const* navMesh = map->GetMapCollisionData().GetMMapData().GetNavMesh();
    if (!navMesh)
    {
        result.failureReason = "no navmesh on map";
        return result;
    }

    RawResult const raw = BuildCoreFromMesh(navMesh, map->GetId(), bot->GetPositionX(),
                                            bot->GetPositionY(), bot->GetPositionZ(), tx, ty, tz);
    return Finalize(bot, raw);
}
