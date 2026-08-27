// DERIVED from the navmesh itself, not from a live clear.
// Map 36, boss 645 (Cookie), 28 anchors over 260yd.
//
// The mesh carries this path - a plain Detour query walks it in 55
// polygons using the very filter the core queries with - but the
// module's own chunked builder returns an incomplete route and the
// party ends up in the water beside the ship. These anchors are the
// corner points of that Detour corridor, so following them walks the
// same ramp a player walks. A live clear that does better replaces
// this file through the ordinary shortest-wins rule.
#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"

void RegisterRecordedRoute36_645()
{
    DungeonClearRouteRegistry::Register(36, DUNGEON_DIFFICULTY_NORMAL, 645,
        {
            { -87.00f, -820.00f, 39.33f },
            { -76.27f, -818.13f, 40.16f },
            { -75.20f, -817.87f, 40.26f },
            { -64.00f, -810.67f, 41.26f },
            { -48.53f, -804.00f, 42.76f },
            { -45.07f, -798.40f, 39.56f },
            { -45.87f, -797.07f, 39.36f },
            { -50.93f, -794.93f, 38.76f },
            { -58.67f, -793.07f, 39.06f },
            { -96.80f, -800.00f, 32.06f },
            { -97.60f, -799.47f, 30.76f },
            { -100.53f, -794.67f, 28.06f },
            { -88.80f, -781.87f, 26.86f },
            { -88.53f, -781.07f, 26.86f },
            { -88.80f, -780.27f, 26.96f },
            { -91.73f, -780.00f, 24.56f },
            { -100.27f, -782.67f, 22.06f },
            { -106.67f, -786.13f, 19.46f },
            { -128.00f, -810.67f, 16.96f },
            { -128.00f, -814.40f, 16.86f },
            { -126.13f, -820.00f, 16.86f },
            { -119.47f, -830.67f, 18.46f },
            { -118.13f, -832.00f, 18.46f },
            { -112.00f, -837.60f, 18.46f },
            { -106.67f, -841.33f, 18.56f },
            { -96.53f, -847.47f, 17.16f },
            { -85.33f, -853.33f, 17.36f },
            { -68.00f, -854.00f, 17.16f },
        });
}
