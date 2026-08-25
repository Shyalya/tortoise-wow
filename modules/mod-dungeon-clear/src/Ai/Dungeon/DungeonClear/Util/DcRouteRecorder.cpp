/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRouteRecorder.h"

#include "Map.h"
#include "Player.h"

#include "Config.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace
{
    struct Sample3
    {
        float x, y, z;
    };

    struct Leg
    {
        uint32 mapId = 0;
        std::vector<Sample3> pts;
    };

    std::mutex g_mutex;
    // instanceId -> leg currently being walked
    std::unordered_map<uint32, Leg> g_legs;

    // Sampling: one point per ~4yd of travel. Fine enough that the thinning
    // below has real geometry to work with, coarse enough that a 20-minute run
    // holds a few hundred points, not tens of thousands.
    constexpr float kSampleStep = 4.0f;
    // Anchor spacing in the emitted route. The authored Azjol-Nerub route sits
    // at ~24yd between anchors; 15 keeps corners in a tighter dungeon.
    constexpr float kAnchorStep = 15.0f;
    // A leg shorter than this is not worth an anchor route (the boss was
    // already next door and the router handles that trivially).
    constexpr float kMinLegLength = 40.0f;
    // Vertical granularity: a stairway anchored every 3y keeps its shape
    // without turning a flat corridor into a chain of stops.
    constexpr float kAnchorRise = 3.0f;

    float Dist2D(Sample3 const& a, Sample3 const& b)
    {
        float const dx = a.x - b.x;
        float const dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Douglas-Peucker-lite: keep a point whenever the running distance since
    // the last kept anchor exceeds kAnchorStep, or the direction turns sharply
    // (so corners survive even when they fall between two spacing marks).
    std::vector<Sample3> Thin(std::vector<Sample3> const& pts)
    {
        std::vector<Sample3> out;
        if (pts.size() < 2)
            return out;
        out.push_back(pts.front());
        float run = 0.0f;
        for (size_t i = 1; i + 1 < pts.size(); ++i)
        {
            run += Dist2D(pts[i - 1], pts[i]);
            // Turn detection on the 2D heading either side of this point.
            float const ax = pts[i].x - pts[i - 1].x, ay = pts[i].y - pts[i - 1].y;
            float const bx = pts[i + 1].x - pts[i].x, by = pts[i + 1].y - pts[i].y;
            float const la = std::sqrt(ax * ax + ay * ay), lb = std::sqrt(bx * bx + by * by);
            bool corner = false;
            if (la > 0.1f && lb > 0.1f)
            {
                float const cosang = (ax * bx + ay * by) / (la * lb);
                corner = cosang < 0.82f;   // ~35 degrees or sharper
            }
            // Height matters as much as heading. The turn test above is 2D,
            // so a straight staircase reads as "no corner" and would only get
            // an anchor every kAnchorStep - losing the climb exactly where a
            // route needs it most (the Deadmines ship deck rises ~39y over a
            // short run). Keep a point whenever we have gained or lost more
            // than kAnchorRise since the last kept anchor. Flat ground is
            // unaffected: extra points on a straight line buy nothing but
            // stop-and-go.
            bool const climbed = std::fabs(pts[i].z - out.back().z) > kAnchorRise;
            if (run >= kAnchorStep || corner || climbed)
            {
                out.push_back(pts[i]);
                run = 0.0f;
            }
        }
        out.push_back(pts.back());
        return out;
    }

    std::string SanitizeIdent(std::string const& in)
    {
        std::string out;
        bool upper = true;
        for (char c : in)
        {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            {
                out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
                upper = false;
            }
            else
                upper = true;
        }
        if (out.empty() || (out[0] >= '0' && out[0] <= '9'))
            out.insert(out.begin(), 'B');
        return out;
    }
}

namespace DcRouteRecorder
{
    std::string OutputDir()
    {
        // Default: the module's own routes/ folder, i.e. repo content. A
        // packaged server can point this at a writable path instead.
        return sConfig.GetStringDefault("DungeonClear.RouteRecorderDir",
                                        "../../modules/mod-dungeon-clear/routes");
    }

    void Sample(Player* leader)
    {
        if (!leader)
            return;
        Map* map = leader->FindMap();
        if (!map || !map->IsDungeon())
            return;

        Sample3 const now{leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ()};
        std::lock_guard<std::mutex> lock(g_mutex);
        Leg& leg = g_legs[map->GetInstanceId()];
        leg.mapId = map->GetId();
        if (leg.pts.empty() || Dist2D(leg.pts.back(), now) >= kSampleStep)
            leg.pts.push_back(now);
    }

    void OnBossKilled(Map* map, uint32 bossEntry, std::string const& bossName)
    {
        if (!map)
            return;

        std::vector<Sample3> pts;
        uint32 mapId = map->GetId();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_legs.find(map->GetInstanceId());
            if (it == g_legs.end())
                return;
            pts.swap(it->second.pts);          // leg closed; next boss starts fresh
            mapId = it->second.mapId ? it->second.mapId : mapId;
        }

        std::vector<Sample3> const anchors = Thin(pts);
        if (anchors.size() < 3)
            return;

        float length = 0.0f;
        for (size_t i = 1; i < anchors.size(); ++i)
            length += Dist2D(anchors[i - 1], anchors[i]);
        if (length < kMinLegLength)
            return;

        // One appender per (map, boss). Written as an ordinary C++ source file
        // in the same shape as the authored routes, so committing it is all it
        // takes to ship the route with the module.
        std::string const ident = SanitizeIdent(bossName);
        std::ostringstream path;
        path << OutputDir() << "/Route_" << mapId << "_" << bossEntry << ".cpp";

        // Keep the SHORTEST route. The generated header carries the leg's
        // length ("... N anchors over Xyd."), so a previous recording can be
        // compared without any side index. Live: the same Gilnid leg was
        // recorded at 460yd and, minutes later, at 1695yd - last-writer-wins
        // threw the good one away. With several groups running the same
        // dungeon at once this decides which attempt survives.
        {
            std::ifstream prev(path.str().c_str());
            if (prev.is_open())
            {
                std::string head;
                std::getline(prev, head);
                std::getline(prev, head);          // second line carries the numbers
                std::size_t const at = head.find(" over ");
                if (at != std::string::npos)
                {
                    uint32 const prevLen =
                        static_cast<uint32>(std::atoi(head.c_str() + at + 6));
                    if (prevLen != 0 && prevLen <= static_cast<uint32>(length))
                    {
                        LOG_INFO("playerbots.dungeonclear",
                                 "[DC-ROUTE] kept the shorter route for {} ({}yd) — this run "
                                 "walked {}yd",
                                 bossName, prevLen, static_cast<uint32>(length));
                        return;
                    }
                }
            }
        }

        // Side file + rename: with several groups running at once two can
        // close the same boss leg within milliseconds, and a direct truncate
        // would let one read the other's half-written file. Rename is atomic
        // on the same filesystem.
        std::string const finalPath = path.str();
        std::string const tmpPath =
            finalPath + ".tmp" + std::to_string(map->GetInstanceId());
        std::ofstream out(tmpPath.c_str(), std::ios::trunc);
        if (!out.is_open())
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[DC-ROUTE] could not write {} — recorder disabled for this leg",
                     finalPath);
            return;
        }

        out << "// GENERATED by DcRouteRecorder from a live clear — safe to edit by hand.\n"
            << "// Map " << mapId << ", boss " << bossEntry << " (" << bossName << "), "
            << anchors.size() << " anchors over " << static_cast<uint32>(length) << "yd.\n"
            << "//\n"
            << "// The recorder samples the run leader every ~4yd and thins the leg to\n"
            << "// ~15yd anchors (corners preserved). Advance prefers a registered anchor\n"
            << "// route over the long-range router, so this file makes the walked path the\n"
            << "// path every later run takes.\n"
            << "#include \"Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h\"\n\n"
            << "void RegisterRecordedRoute" << mapId << "_" << bossEntry << "()\n{\n"
            << "    DungeonClearRouteRegistry::Register(" << mapId
            << ", DUNGEON_DIFFICULTY_NORMAL, " << bossEntry << ",\n        {\n";
        for (Sample3 const& a : anchors)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "            { %.2ff, %.2ff, %.2ff },\n", a.x, a.y, a.z);
            out << buf;
        }
        out << "        });\n}\n";
        out.close();
        std::rename(tmpPath.c_str(), finalPath.c_str());

        // Runtime twin: same anchors, one "x y z" per line, plus the leg
        // length in the header so the shortest-wins comparison works on it
        // too. The .cpp above is what the repo ships; THIS is what a running
        // server reads at startup, so a better route is live after a restart
        // instead of after a rebuild.
        {
            std::string const datPath = finalPath.substr(0, finalPath.size() - 4) + ".route";
            std::string const datTmp = datPath + ".tmp" + std::to_string(map->GetInstanceId());
            std::ofstream dat(datTmp.c_str(), std::ios::trunc);
            if (dat.is_open())
            {
                dat << "# map " << mapId << " boss " << bossEntry << " len "
                    << static_cast<uint32>(length) << "\n";
                for (Sample3 const& a : anchors)
                    dat << a.x << ' ' << a.y << ' ' << a.z << "\n";
                dat.close();
                std::rename(datTmp.c_str(), datPath.c_str());
            }
        }

        LOG_INFO("playerbots.dungeonclear",
                 "[DC-ROUTE] recorded {} anchors ({}yd) for {} (entry {}) -> {}",
                 anchors.size(), static_cast<uint32>(length), bossName, bossEntry, finalPath);
    }

    void Forget(uint32 instanceId)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_legs.erase(instanceId);
    }
}
