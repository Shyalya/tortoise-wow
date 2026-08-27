// DERIVED from the navmesh itself, not from a live clear.
// Map 36, boss 639 (Edwin VanCleef), 4 anchors over 23yd.
//
// The mesh carries this path - a plain Detour query walks it in 55
// polygons using the very filter the core queries with - but the
// module's own chunked builder returns an incomplete route and the
// party ends up in the water beside the ship. These anchors are the
// corner points of that Detour corridor, so following them walks the
// same ramp a player walks. A live clear that does better replaces
// this file through the ordinary shortest-wins rule.
#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"

void RegisterRecordedRoute36_639()
{
    DungeonClearRouteRegistry::Register(36, DUNGEON_DIFFICULTY_NORMAL, 639,
        {
            { -69.00f, -808.00f, 40.81f },
            { -75.20f, -817.87f, 40.26f },
            { -76.27f, -818.13f, 40.16f },
            { -87.00f, -820.00f, 39.33f },
        });
}
