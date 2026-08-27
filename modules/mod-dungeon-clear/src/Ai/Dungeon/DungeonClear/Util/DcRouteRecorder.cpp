/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRouteRecorder.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"

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
        // Whose walk this is. A leg must be ONE body's path; two bodies
        // sampling into the same vector produce a route that zigzags between
        // them by however far apart the party happens to walk.
        ObjectGuid owner;
        // Set while the owner is dead, cleared when it is back at the point
        // where the recording broke off. See Sample().
        bool awaitingReturn = false;
        // Last position seen for the owner, ALIVE or DEAD. Used at boss time
        // to tell a recording that ran to the kill from one that stopped
        // halfway.
        Sample3 lastSeen{};
        bool haveLastSeen = false;
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
    // How close the resurrected owner has to get to the last recorded point
    // before recording resumes. The corpse run retraces ground that is
    // already in the leg, so nothing is lost by ignoring it.
    constexpr float kRejoinRadius = 20.0f;
    // Two samples this close together are the same spot; everything walked
    // between them was a loop. Twice the sample step, so ordinary corridor
    // wobble is not mistaken for a return.
    constexpr float kLoopRadius = 8.0f;
    // ...and the same FLOOR. Wailing Caverns stacks its tunnels: two points
    // eight yards apart seen from above can be two different tubes, one over
    // the other. Cutting between those splices a route that walks into rock,
    // and since anchors are walked in a straight line with no pathfinding in
    // between, the party simply grinds against the wall (live: 319 "stuck
    // ladder" reports, nine of ten groups frozen at 0/8). Four yards allows a
    // ramp or a step, not a storey.
    constexpr float kLoopRise = 4.0f;
    // How close the recording has to end to where the boss actually died for
    // the leg to count as complete.
    constexpr float kFinishRadius = 40.0f;
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
    // Collapse loops. A recorded leg holds every yard the leader walked,
    // including the ones it walked twice - back for a drink, back for a
    // straggler, back around a pull that went sideways. Those repeats are
    // what pushed the leg to Jared Voss to 1300-1900yd for 130yd of
    // distance and got every single recording thrown out as wandering.
    //
    // The rule is purely geometric: if a later sample stands within
    // kLoopRadius of an earlier one, the leader was back where it had been,
    // so everything in between was a detour and can go. Both ends are ground
    // it actually stood on, so nothing is invented - only the going-around
    // is dropped. Always cut to the LAST such return, or a route that passes
    // one junction three times keeps two of the three passes.
    std::vector<Sample3> CutLoops(std::vector<Sample3> const& pts)
    {
        std::vector<Sample3> out;
        out.reserve(pts.size());
        std::size_t i = 0;
        while (i < pts.size())
        {
            out.push_back(pts[i]);
            std::size_t jump = i;
            for (std::size_t j = pts.size(); j > i + 1; --j)
            {
                if (Dist2D(pts[i], pts[j - 1]) <= kLoopRadius &&
                    std::fabs(pts[i].z - pts[j - 1].z) <= kLoopRise)
                {
                    jump = j - 1;
                    break;
                }
            }
            i = (jump > i) ? jump + 1 : i + 1;
        }
        return out;
    }

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

        // One body per leg (see Leg::owner).
        if (leg.owner.IsEmpty())
            leg.owner = leader->GetObjectGuid();
        else if (leg.owner != leader->GetObjectGuid())
            return;

        leg.lastSeen = now;
        leg.haveLastSeen = true;

        // A dead leader is not walking. Releasing the spirit puts it at the
        // instance graveyard - in the Deadmines that is 249yd from the
        // foundry floor, and every single group's leg to Gilnid was thrown
        // out for a "sideways jump" of 237-267yd because the release was
        // recorded as a step. Stop sampling until it is alive AND back where
        // the recording broke off; the corpse run retraces ground the leg
        // already holds.
        if (!leader->IsAlive())
        {
            leg.awaitingReturn = true;
            return;
        }
        if (leg.awaitingReturn)
        {
            // Height counts here too: standing directly above the point where
            // the recording broke off is not standing on it.
            if (!leg.pts.empty() &&
                (Dist2D(leg.pts.back(), now) > kRejoinRadius ||
                 std::fabs(leg.pts.back().z - now.z) > kLoopRise))
                return;
            leg.awaitingReturn = false;
        }

        if (leg.pts.empty() || Dist2D(leg.pts.back(), now) >= kSampleStep)
            leg.pts.push_back(now);
    }

    void DiscardRoute(uint32 mapId, uint32 bossEntry)
    {
        std::string const base = OutputDir() + "/Route_" + std::to_string(mapId) + "_" +
                                 std::to_string(bossEntry);
        for (char const* ext : { ".route", ".cpp" })
        {
            std::string const from = base + ext;
            std::string const to = from + ".bad";
            std::rename(from.c_str(), to.c_str());   // absent file: nothing happens
        }
    }

    void OnBossKilled(Map* map, uint32 bossEntry, std::string const& bossName)
    {
        if (!map)
            return;

        std::vector<Sample3> pts;
        uint32 mapId = map->GetId();
        Sample3 lastSeen{};
        bool haveLastSeen = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_legs.find(map->GetInstanceId());
            if (it == g_legs.end())
                return;
            pts.swap(it->second.pts);          // leg closed; next boss starts fresh
            mapId = it->second.mapId ? it->second.mapId : mapId;
            lastSeen = it->second.lastSeen;
            haveLastSeen = it->second.haveLastSeen;
            it->second.owner.Clear();          // next leg is up for grabs again
            it->second.awaitingReturn = false;
        }

        // Reject legs that contain a TELEPORT. Sampling is every ~4yd, so a
        // gap far beyond that is not walking - it is the distance fence, the
        // stranded recovery or a corpse run moving somebody instantly. Such a
        // leg looks SHORT (which is exactly what the shortest-wins rule
        // prefers) while containing a segment that walks through walls, and
        // once loaded at runtime it strands every later group on that jump.
        // This is the regression behind Deadmines falling from 7/10 to 2-4/10
        // after runtime loading went live.
        for (std::size_t i = 1; i < pts.size(); ++i)
        {
            float const dx = pts[i].x - pts[i - 1].x;
            float const dy = pts[i].y - pts[i - 1].y;
            float const dz = pts[i].z - pts[i - 1].z;
            // Falling is not teleporting. A drop covers a lot of ground
            // between two samples but stays over the same spot - the plunge
            // into the Deadmines foundry reads as a 35yd jump and had three
            // perfectly good legs thrown away. A teleport moves you ACROSS
            // the map, so judge on the horizontal component alone.
            float const flatJump = std::sqrt(dx * dx + dy * dy);
            // 60yd, not 25: sampling rides the world tick, and with ten
            // groups running the tick stretches - a gliding bot covers 25-28yd
            // between two samples without anything unusual happening. Those
            // values clustered just above the old threshold and were throwing
            // away sound legs. A real displacement is much larger (a corpse
            // run or a rescue moves you across the instance), so judge well
            // above what walking can produce.
            if (flatJump > 60.0f)
            {
                LOG_INFO("playerbots.dungeonclear",
                         "[DC-ROUTE] discarded a teleported leg for {} (sideways jump of {}yd)",
                         bossName, static_cast<uint32>(flatJump));
                return;
            }
        }

        std::vector<Sample3> const anchors = Thin(CutLoops(pts));
        if (anchors.size() < 3)
            return;

        // The recording has to reach the kill. If the leader died on the way
        // and never came back, the leg ends somewhere in the middle - and
        // since Build() appends the boss as a final straight-line goal after
        // the last anchor, adopting such a route would send every later group
        // walking from that midpoint into whatever wall lies between.
        if (haveLastSeen && Dist2D(anchors.back(), lastSeen) > kFinishRadius)
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[DC-ROUTE] discarded an incomplete leg for {} (recording stops {}yd "
                     "short of the kill)",
                     bossName, static_cast<uint32>(Dist2D(anchors.back(), lastSeen)));
            return;
        }

        float length = 0.0f;
        for (size_t i = 1; i < anchors.size(); ++i)
            length += Dist2D(anchors[i - 1], anchors[i]);
        if (length < kMinLegLength)
            return;

        std::ostringstream path;
        path << OutputDir() << "/Route_" << mapId << "_" << bossEntry << ".cpp";
        bool haveRouteAlready = false;
        {
            std::ifstream probe(path.str().c_str());
            haveRouteAlready = probe.is_open();
        }

        // Reject wandering. A leg is only worth keeping if it roughly tracks
        // the way to the boss; a party that searched half the dungeon
        // produces a technically valid but useless route - and since Advance
        // PREFERS registered routes, adopting one actively sends later groups
        // on that detour. Live: the leg to Jared Voss was captured at 2404yd
        // for a boss ~150yd from where the party started, and every group
        // that loaded it walked the long way round. Six times the straight
        // line is generous for real corridors and still cuts the strays.
        {
            float const straight = Dist2D(anchors.front(), anchors.back());
            if (straight > 1.0f && length > straight * 6.0f)
            {
                // ... but only when there is something better to fall back
                // on. With no route on disk the choice is not "detour or
                // short way", it is "detour or search the dungeon again",
                // and the detour wins: every anchor in it is ground the
                // leader actually walked. Live: the Masterpiece Harvester
                // sits 53yd from where its leg starts and takes 805yd of
                // real corridor to reach, so the guard alone would have left
                // that boss without a route forever. Shortest-wins replaces
                // this the moment a group does better.
                if (haveRouteAlready)
                {
                    LOG_INFO("playerbots.dungeonclear",
                             "[DC-ROUTE] discarded a wandering leg for {}: {}yd walked for {}yd of distance",
                             bossName, static_cast<uint32>(length), static_cast<uint32>(straight));
                    return;
                }
                LOG_INFO("playerbots.dungeonclear",
                         "[DC-ROUTE] taking a long leg for {} for now ({}yd walked for {}yd of "
                         "distance) — nothing better exists yet",
                         bossName, static_cast<uint32>(length), static_cast<uint32>(straight));
            }
        }

        // One appender per (map, boss). Written as an ordinary C++ source file
        // in the same shape as the authored routes, so committing it is all it
        // takes to ship the route with the module.
        std::string const ident = SanitizeIdent(bossName);

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

        // Sofort eintragen, nicht erst beim naechsten Hochfahren. Zehn
        // Gruppen laufen denselben Dungeon; was eine findet, sollen die
        // anderen neun in derselben Minute benutzen. Live in Wailing
        // Caverns: eine Gruppe schrieb den Weg zu Verdan auf, und elfmal
        // meldeten andere im selben Zeitraum "Verdan unerreichbar", weil
        // die Datei zwar dalag, aber niemand sie las.
        {
            std::vector<WaypointHint> hints;
            hints.reserve(anchors.size());
            for (Sample3 const& a : anchors)
                hints.push_back(WaypointHint{a.x, a.y, a.z, 0, 0, 6.0f});
            DungeonClearRouteRegistry::Register(mapId, DUNGEON_DIFFICULTY_NORMAL, bossEntry,
                                                std::move(hints));
        }

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
