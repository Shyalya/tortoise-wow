#pragma once

#include "Platform/Define.h"
#include <string>
#include <vector>

class Player;

namespace ai
{
    // A split-map wing is described by the same creature credit entries used by
    // the boss roster.  Objectives may use synthetic entries from ObjectiveEntry
    // so they survive the same filtering pass as real bosses.
    struct DungeonWing
    {
        std::string name;
        std::vector<uint32> entries;
        float anchorX = 0.0f;
        float anchorY = 0.0f;
        float anchorZ = 0.0f;
    };

    struct DungeonWingLayout
    {
        bool isolated = false;
        std::vector<DungeonWing> wings;
    };

    class DungeonWingRegistry
    {
    public:
        // Synthetic entries occupy a range that cannot collide with creature
        // entries.  This is important for objectives on shared map ids.
        static constexpr uint32 ObjectiveEntry(uint32 sequence)
        {
            return 0x4F000000u | sequence;
        }

        static DungeonWingLayout const* Get(uint32 mapId);
        static int32 FindNearestWing(Player* bot);
        static bool Contains(uint32 mapId, uint8 wing, uint32 entry);
        static std::string WingName(uint32 mapId, uint32 entry);
    };
}
