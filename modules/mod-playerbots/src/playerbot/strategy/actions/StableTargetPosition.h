#pragma once

#include <cmath>
#include <cstdint>

namespace ai
{
    struct StableTargetOffset
    {
        float angle;
        float scale;
    };

    // Return a deterministic point around a destination for one bot. Movement
    // actions are evaluated repeatedly; drawing a new random point on every
    // evaluation makes a bot reverse direction around the same destination.
    inline StableTargetOffset GetStableTargetOffset(std::uint32_t botGuidLow,
        std::uint32_t targetKey, float x, float y)
    {
        auto mix = [](std::uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        };

        // Quarter-yard precision distinguishes nearby destination points while
        // keeping insignificant floating-point variation from changing a route.
        std::uint32_t const qx = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(std::floor(x * 4.0f)));
        std::uint32_t const qy = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(std::floor(y * 4.0f)));
        std::uint32_t const seed = mix(botGuidLow) ^ mix(targetKey + 0x9e3779b9u) ^
            mix(qx + 0x85ebca6bu) ^ mix(qy + 0xc2b2ae35u);

        constexpr float twoPi = 6.28318530717958647692f;
        float const angle = twoPi * static_cast<float>(seed % 4096u) / 4096.0f;
        float const scale = 0.55f +
            0.40f * static_cast<float>((seed >> 12) % 4096u) / 4095.0f;
        return {angle, scale};
    }
}
