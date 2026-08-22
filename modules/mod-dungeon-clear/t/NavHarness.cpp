/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "NavHarness.h"

#include "DetourAlloc.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourStatus.h"
#include "MapDefines.h"
#include "Ai/Dungeon/DungeonClear/Util/LongRangePathfinder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace DcNavHarness
{
    namespace
    {
        namespace fs = std::filesystem;

        // Read one .mmtile and add it to the navmesh. Mirrors MMapMgr::LoadTile
        // but takes an explicit path (the harness loads from a sliced data dir,
        // not the config DataDir) and derives the tile coords from the tile data
        // header rather than the filename.
        bool AddTileFromFile(dtNavMesh* navMesh, fs::path const& path)
        {
            FILE* file = std::fopen(path.string().c_str(), "rb");
            if (!file)
                return false;

            MmapTileHeader fileHeader;
            if (std::fread(&fileHeader, sizeof(MmapTileHeader), 1, file) != 1 ||
                fileHeader.mmapMagic != MMAP_MAGIC ||
                fileHeader.mmapVersion != MMAP_VERSION)
            {
                std::fclose(file);
                return false;
            }

            unsigned char* data =
                static_cast<unsigned char*>(dtAlloc(fileHeader.size, DT_ALLOC_PERM));
            if (!data)
            {
                std::fclose(file);
                return false;
            }
            if (std::fread(data, fileHeader.size, 1, file) != 1)
            {
                dtFree(data);
                std::fclose(file);
                return false;
            }
            std::fclose(file);

            dtTileRef ref = 0;
            // DT_TILE_FREE_DATA: detour now owns `data` and frees it on removeTile.
            if (dtStatusSucceed(navMesh->addTile(data, fileHeader.size,
                                                 DT_TILE_FREE_DATA, 0, &ref)))
                return true;

            dtFree(data);
            return false;
        }
    }

    std::shared_ptr<dtNavMesh> LoadMap(std::string const& mapDataDir, uint32_t mapId)
    {
        fs::path const mmapsDir = fs::path(mapDataDir) / "mmaps";
        if (!fs::exists(mmapsDir))
            return nullptr;

        char mapName[16];
        std::snprintf(mapName, sizeof(mapName), "%03u.mmap", mapId);
        fs::path const paramsFile = mmapsDir / mapName;

        FILE* file = std::fopen(paramsFile.string().c_str(), "rb");
        if (!file)
            return nullptr;
        dtNavMeshParams params;
        bool const ok = std::fread(&params, sizeof(dtNavMeshParams), 1, file) == 1;
        std::fclose(file);
        if (!ok)
            return nullptr;

        dtNavMesh* mesh = dtAllocNavMesh();
        if (!mesh)
            return nullptr;
        if (mesh->init(&params) != DT_SUCCESS)
        {
            dtFreeNavMesh(mesh);
            return nullptr;
        }

        std::shared_ptr<dtNavMesh> navMesh(mesh,
                                           [](dtNavMesh* m) { dtFreeNavMesh(m); });

        // Add every tile present for this map: "{mapId:03}XXYY.mmtile".
        char prefix[8];
        std::snprintf(prefix, sizeof(prefix), "%03u", mapId);
        uint32_t loaded = 0;
        for (auto const& entry : fs::directory_iterator(mmapsDir))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".mmtile")
                continue;
            std::string const stem = entry.path().stem().string();
            if (stem.rfind(prefix, 0) != 0)  // not this map's tile
                continue;
            if (AddTileFromFile(navMesh.get(), entry.path()))
                ++loaded;
        }

        if (loaded == 0)
            return nullptr;
        return navMesh;
    }

    RouteResult Route(dtNavMesh const* mesh, uint32_t mapId,
                      float sx, float sy, float sz,
                      float tx, float ty, float tz)
    {
        RouteResult out;
        if (!mesh)
            return out;
        out.built = true;

        LongRangePathfinder::RawResult const raw =
            LongRangePathfinder::BuildCoreFromMesh(mesh, mapId, sx, sy, sz, tx, ty, tz);

        out.reachable = raw.reachable;
        out.corridorComplete = raw.corridorComplete;
        out.startFarFromPoly = raw.startFarFromPoly;
        out.failureReason = raw.failureReason;
        out.pointCount = static_cast<uint32_t>(raw.rawPts.size());

        for (std::size_t i = 1; i < raw.rawPts.size(); ++i)
        {
            G3D::Vector3 const& a = raw.rawPts[i - 1];
            G3D::Vector3 const& b = raw.rawPts[i];
            float const dx = b.x - a.x;
            float const dy = b.y - a.y;
            out.routeLength2d += std::sqrt(dx * dx + dy * dy);
            out.maxStepZ = std::max(out.maxStepZ, std::fabs(b.z - a.z));
        }
        return out;
    }

    bool NearestPoint(dtNavMesh const* mesh,
                      float x, float y, float z,
                      float hExtent, float vExtent, G3D::Vector3& out,
                      unsigned short includeFlags)
    {
        if (!mesh)
            return false;

        dtNavMeshQuery* query = dtAllocNavMeshQuery();
        if (!query)
            return false;

        bool found = false;
        if (dtStatusSucceed(query->init(mesh, 4096)))
        {
            dtQueryFilter filter;
            filter.setIncludeFlags(includeFlags);  // default: any navigable poly
            filter.setExcludeFlags(0);

            // Detour coordinate order is {y, z, x} (see BuildCoreFromMesh).
            float const pt[3]  = { y, z, x };
            float const ext[3] = { hExtent, vExtent, hExtent };
            dtPolyRef ref = 0;
            float nearest[3] = { 0, 0, 0 };
            if (dtStatusSucceed(query->findNearestPoly(pt, ext, &filter, &ref, nearest)) && ref)
            {
                out = G3D::Vector3(nearest[2], nearest[0], nearest[1]);  // back to WoW {x,y,z}
                found = true;
            }
        }

        dtFreeNavMeshQuery(query);
        return found;
    }
}
