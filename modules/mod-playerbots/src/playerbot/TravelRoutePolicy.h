#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ai
{
    // This is deliberately a route preference, not the spline's physical
    // speed. The original Turtle implementation and current upstream
    // playerbots both divide taxi-path length by (450 * 8): long-distance bots
    // should strongly prefer an available taxi over walking or ocean swimming.
    constexpr float PLAYERBOT_TAXI_ROUTE_DIVISOR = 450.0f * 8.0f;

    inline float GetTaxiRouteCost(float pathDistance)
    {
        return pathDistance > 0.0f ? pathDistance / PLAYERBOT_TAXI_ROUTE_DIVISOR : 0.0f;
    }

    inline float GetWalkTravelTime(float runDistance, float swimDistance,
        float runSpeed, float swimSpeed)
    {
        float const runTime = runDistance > 0.0f && runSpeed > 0.0f
            ? runDistance / runSpeed : 0.0f;
        if (swimDistance <= 0.0f || swimSpeed <= 0.0f)
            return runTime;

        // Short river crossings remain ordinary travel. Sustained swimming is
        // unsafe and highly impractical compared with roads, taxis and boats,
        // but remains available when the graph has no alternative.
        constexpr float SAFE_SWIM_DISTANCE = 120.0f;
        constexpr float LONG_SWIM_COST_MULTIPLIER = 4.0f;
        float const safeSwimDistance = std::min(swimDistance, SAFE_SWIM_DISTANCE);
        float const longSwimDistance = std::max(0.0f, swimDistance - SAFE_SWIM_DISTANCE);
        float const swimTime = safeSwimDistance / swimSpeed +
            longSwimDistance / swimSpeed * LONG_SWIM_COST_MULTIPLIER;
        return runTime + swimTime;
    }

    inline std::uint32_t MixTravelRouteSeed(std::uint32_t value)
    {
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return value;
    }

    inline std::uint32_t GetStableTravelSelectionSeed(std::uint32_t partySeed,
        std::uint32_t purpose, std::uint32_t mapId, float x, float y)
    {
        auto quantize = [](float coordinate)
        {
            return static_cast<std::uint32_t>(
                static_cast<std::int32_t>(std::floor(coordinate / 50.0f)));
        };

        std::uint32_t seed = MixTravelRouteSeed(partySeed);
        seed ^= MixTravelRouteSeed(purpose + 0x9e3779b9u);
        seed ^= MixTravelRouteSeed(mapId + 0x85ebca6bu);
        seed ^= MixTravelRouteSeed(quantize(x) + 0xc2b2ae35u);
        seed ^= MixTravelRouteSeed(quantize(y) + 0x27d4eb2fu);
        return MixTravelRouteSeed(seed);
    }

    // Give independent parties different preferences between otherwise
    // comparable graph edges. A 25% ceiling keeps clearly inferior routes out
    // while allowing alternate roads and hubs to win for some parties instead
    // of converging the entire random-bot population on one corridor.
    inline float GetStableRouteCostMultiplier(std::uint32_t partySeed,
        std::uint32_t fromMap, float fromX, float fromY,
        std::uint32_t toMap, float toX, float toY)
    {
        auto quantize = [](float coordinate)
        {
            return static_cast<std::uint32_t>(
                static_cast<std::int32_t>(std::floor(coordinate)));
        };

        std::uint32_t seed = MixTravelRouteSeed(partySeed);
        seed ^= MixTravelRouteSeed(fromMap + 0x9e3779b9u);
        seed ^= MixTravelRouteSeed(quantize(fromX) + 0x85ebca6bu);
        seed ^= MixTravelRouteSeed(quantize(fromY) + 0xc2b2ae35u);
        seed ^= MixTravelRouteSeed(toMap + 0x27d4eb2fu);
        seed ^= MixTravelRouteSeed(quantize(toX) + 0x165667b1u);
        seed ^= MixTravelRouteSeed(quantize(toY) + 0xd3a2646cu);

        return 1.0f + 0.25f * static_cast<float>(seed % 1024u) / 1023.0f;
    }
}
