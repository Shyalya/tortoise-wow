/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_WAYPOINTHINT_H
#define _PLAYERBOT_WAYPOINTHINT_H

#include "Common.h"

// Per-anchor behavior bits for a registered dungeon route. Bitmasked into
// `WaypointHint::flags`. Unused bits are reserved for future hints.
enum class AnchorFlag : uint16
{
    NONE        = 0,
    JUMP_DOWN   = 1 << 0,  // Advance uses JumpTo to reach this anchor.
    JUMP_GAP    = 1 << 1,  // Off-mesh gap — same MoveJump path, distinguishes ledge-jump from drop-down for telemetry.
    DOOR_AHEAD  = 1 << 2,  // Anchor sits in front of a door (entry in `doorGoEntry`); Advance stalls if door is closed.
    PIVOT_TIGHT = 1 << 3,  // Tight pivot — followers cluster closer here. Hint only; doesn't affect Advance directly.
};

inline constexpr uint16 ToFlag(AnchorFlag f) { return static_cast<uint16>(f); }

inline constexpr uint16 operator|(AnchorFlag a, AnchorFlag b)
{
    return static_cast<uint16>(a) | static_cast<uint16>(b);
}

inline constexpr uint16 operator|(uint16 a, AnchorFlag b)
{
    return a | static_cast<uint16>(b);
}

inline constexpr bool HasFlag(uint16 mask, AnchorFlag f)
{
    return (mask & static_cast<uint16>(f)) != 0;
}

struct WaypointHint
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    uint32 doorGoEntry{0};       // GameObject template entry of an associated door, when DOOR_AHEAD is set
    uint16 flags{0};             // Bitmask of AnchorFlag
    float arriveRadius{6.0f};    // Distance from the hint at which Advance considers it reached
};

#endif
