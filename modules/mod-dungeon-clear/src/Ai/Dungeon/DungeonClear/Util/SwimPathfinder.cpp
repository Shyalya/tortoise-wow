/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "SwimPathfinder.h"

#include <array>
#include <cmath>

#include "Optional.h"          // Optional<> used (unguarded) by GridTerrainData.h
#include "GridTerrainData.h"   // ZLiquidStatus (LIQUID_MAP_*); std::array<> used (unguarded) too
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "Player.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"  // DC_PI

namespace
{
    // Greedy step geometry.
    constexpr float SWIM_STEP_LEN     = 4.0f;   // yd advanced per accepted hop
    constexpr int   SWIM_MAX_STEPS    = 220;    // hard budget (≈880yd of travel)
    constexpr float SWIM_ARRIVE       = 4.0f;   // within this of goal => done
    constexpr float SWIM_EXIT_RADIUS  = 12.0f;  // allow leaving water this close to goal (surfacing onto a dry platform)
    constexpr int   SWIM_MAX_NO_PROGRESS = 12;  // consecutive non-progress hops before bailing to (future) Tier B

    // Deflection fan, in degrees, off the straight-to-goal bearing. Tried only
    // when the straight hop is blocked. Ordered small->large so the least
    // deviating clear hop tends to win on score ties.
    constexpr float YAW_FAN[]   = { 0.0f, 20.0f, -20.0f, 40.0f, -40.0f, 60.0f, -60.0f, 85.0f, -85.0f, 110.0f, -110.0f };
    constexpr float PITCH_FAN[] = { 0.0f, 20.0f, -20.0f, 40.0f, -40.0f, 60.0f, -60.0f };

    constexpr float DEG2RAD = DC_PI / 180.0f;

    float Dist3D(G3D::Vector3 const& a, G3D::Vector3 const& b)
    {
        float const dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // VMAP line-of-sight only (liquid is transparent to VMAP), so this tests
    // the swim line against rock / WMO geometry exactly like the route LOS
    // screen, but at the true submerged Z with no eye-height bump.
    bool LineClear(Map* map, uint32 phase, G3D::Vector3 const& a, G3D::Vector3 const& b)
    {
        return map->isInLineOfSight(a.x, a.y, a.z, b.x, b.y, b.z, phase,
                                    LINEOFSIGHT_CHECK_VMAP, VMAP::ModelIgnoreFlags::Nothing);
    }

    bool InWater(Map* map, uint32 /*phase*/, G3D::Vector3 const& p, float /*coll*/)
    {
        // Tortoise port: liquid lives on the terrain here, one phase, and the
        // collision height plays no part in the status test.
        if (!map->GetTerrain())
            return false;
        GridMapLiquidData ld{};
        GridMapLiquidStatus const st = const_cast<TerrainInfo*>(map->GetTerrain())->getLiquidStatus(p.x, p.y, p.z, MAP_ALL_LIQUIDS, &ld);
        return (st & (LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER)) != 0;
    }

    // Unit direction from `base` azimuth/elevation, deflected by yaw (around
    // world Z) and pitch (elevation), returned as a world-space unit vector.
    G3D::Vector3 DeflectedDir(float az, float el, float yawDeg, float pitchDeg)
    {
        float const a = az + yawDeg * DEG2RAD;
        float e = el + pitchDeg * DEG2RAD;
        float const lim = DC_PI * 0.5f - 0.01f;
        if (e > lim) e = lim;
        if (e < -lim) e = -lim;
        float const ce = std::cos(e);
        return G3D::Vector3(ce * std::cos(a), ce * std::sin(a), std::sin(e));
    }
}

namespace SwimPathfinder
{
    Result Build(Player* bot, G3D::Vector3 const& start, G3D::Vector3 const& goal)
    {
        Result r;
        if (!bot)
        {
            r.failureReason = "no bot";
            return r;
        }
        Map* map = bot->FindMap();
        if (!map)
        {
            r.failureReason = "no map";
            return r;
        }
        uint32 const phase = bot->GetPhaseMask();
        float const coll = bot->GetCollisionHeight();

        G3D::Vector3 cur = start;
        float bestToGoal = Dist3D(cur, goal);
        int noProgress = 0;

        for (int step = 0; step < SWIM_MAX_STEPS; ++step)
        {
            float const d = Dist3D(cur, goal);
            // Close enough to dart straight in?
            if (d <= SWIM_ARRIVE || (d <= SWIM_STEP_LEN * 2.0f && LineClear(map, phase, cur, goal)))
            {
                r.points.push_back(goal);
                r.ok = true;
                return r;
            }

            G3D::Vector3 const delta = goal - cur;
            float const len = delta.length();
            if (len < 0.001f)
            {
                r.points.push_back(goal);
                r.ok = true;
                return r;
            }
            float const az = std::atan2(delta.y, delta.x);
            float el = std::asin(std::max(-1.0f, std::min(1.0f, delta.z / len)));

            // Fast path: straight toward the goal. Most of a tubular tunnel is
            // taken one cheap raycast per step this way; only bends pay the fan.
            G3D::Vector3 const straight = cur + DeflectedDir(az, el, 0.0f, 0.0f) * SWIM_STEP_LEN;
            bool const straightOk = LineClear(map, phase, cur, straight) &&
                (InWater(map, phase, straight, coll) || Dist3D(straight, goal) <= SWIM_EXIT_RADIUS);

            G3D::Vector3 chosen;
            bool found = false;
            if (straightOk)
            {
                chosen = straight;
                found = true;
            }
            else
            {
                // Probe the deflection fan; keep the clear, water-staying,
                // progressing candidate with the best (progress − deviation) score.
                float bestScore = -1.0e9f;
                for (float yaw : YAW_FAN)
                {
                    for (float pitch : PITCH_FAN)
                    {
                        if (yaw == 0.0f && pitch == 0.0f)
                            continue;  // already tried as the straight hop
                        G3D::Vector3 const cand = cur + DeflectedDir(az, el, yaw, pitch) * SWIM_STEP_LEN;
                        if (!LineClear(map, phase, cur, cand))
                            continue;
                        float const candToGoal = Dist3D(cand, goal);
                        if (!InWater(map, phase, cand, coll) && candToGoal > SWIM_EXIT_RADIUS)
                            continue;
                        float const progress = d - candToGoal;
                        float const deviation = std::abs(yaw) + std::abs(pitch);
                        float const score = progress - 0.01f * deviation;
                        if (score > bestScore)
                        {
                            bestScore = score;
                            chosen = cand;
                            found = true;
                        }
                    }
                }
            }

            if (!found)
            {
                // Boxed in — no clear hop in any probed direction. Tier B (lattice
                // A*) is the designed escape; until then the caller stalls.
                r.failureReason = "greedy swim: no clear hop (concave geometry?)";
                return r;
            }

            r.points.push_back(chosen);

            float const newToGoal = Dist3D(chosen, goal);
            if (newToGoal >= bestToGoal - 0.1f)
            {
                if (++noProgress > SWIM_MAX_NO_PROGRESS)
                {
                    r.failureReason = "greedy swim: stopped making progress";
                    return r;
                }
            }
            else
            {
                noProgress = 0;
                bestToGoal = newToGoal;
            }
            cur = chosen;
        }

        r.failureReason = "greedy swim: step budget exhausted";
        return r;
    }

    bool WaterBetween(Player* bot, G3D::Vector3 const& a, G3D::Vector3 const& b)
    {
        if (!bot)
            return false;
        Map* map = bot->FindMap();
        if (!map)
            return false;
        uint32 const phase = bot->GetPhaseMask();
        float const coll = bot->GetCollisionHeight();

        constexpr int SAMPLES = 10;
        for (int i = 0; i <= SAMPLES; ++i)
        {
            float const t = float(i) / float(SAMPLES);
            G3D::Vector3 const p(a.x + (b.x - a.x) * t,
                                 a.y + (b.y - a.y) * t,
                                 a.z + (b.z - a.z) * t);
            if (InWater(map, phase, p, coll))
                return true;
        }
        return false;
    }
}
