#include "Data/DungeonWingRegistry.h"
#include "Objects/Player.h"

#include <cmath>
#include <unordered_map>

namespace
{
    std::unordered_map<uint32, ai::DungeonWingLayout> const& Store()
    {
        static std::unordered_map<uint32, ai::DungeonWingLayout> const store = []
        {
            std::unordered_map<uint32, ai::DungeonWingLayout> result;

            // Scarlet Monastery: each wing has its own outdoor portal and the
            // four interiors are not connected to one another.
            result[189] = {
                true,
                {
                    {"Scarlet Monastery (Graveyard)", {3983, 4543}, 1786.55f, 1124.44f, 7.57f},
                    {"Scarlet Monastery (Library)", {3974, 6487}, 119.94f, -260.87f, 18.63f},
                    {"Scarlet Monastery (Armory)", {3975}, 1965.09f, -431.61f, 6.26f},
                    // 3977 is retained for detection even when a roster patch
                    // replaces its route anchor with Mograine (3976).
                    {"Scarlet Monastery (Cathedral)", {4542, 3976, 3977, 104591}, 1153.87f, 1398.39f, 32.61f}
                }
            };

            // Dire Maul: the wings are isolated.  Pusillin is deliberately not
            // in the main clear roster; it is an optional event NPC rather than
            // a required encounter credit in the upstream data model.
            result[429] = {
                true,
                {
                    {"Dire Maul (East)", {11490, 13280, 14327, 11492, ai::DungeonWingRegistry::ObjectiveEntry(1)}, 86.19f, -197.89f, -4.06f},
                    {"Dire Maul (West)", {11489, 11488, 11487, 11496, 11486,
                        ai::DungeonWingRegistry::ObjectiveEntry(2), ai::DungeonWingRegistry::ObjectiveEntry(3),
                        ai::DungeonWingRegistry::ObjectiveEntry(4), ai::DungeonWingRegistry::ObjectiveEntry(5),
                        ai::DungeonWingRegistry::ObjectiveEntry(6), ai::DungeonWingRegistry::ObjectiveEntry(7),
                        ai::DungeonWingRegistry::ObjectiveEntry(8), ai::DungeonWingRegistry::ObjectiveEntry(9),
                        ai::DungeonWingRegistry::ObjectiveEntry(10), ai::DungeonWingRegistry::ObjectiveEntry(11),
                        ai::DungeonWingRegistry::ObjectiveEntry(12), ai::DungeonWingRegistry::ObjectiveEntry(13),
                        ai::DungeonWingRegistry::ObjectiveEntry(14)}, 14.39f, 475.85f, -23.30f},
                    {"Dire Maul (North)", {14326, 14322, 14321, 14323, 14325, 11501}, 410.71f, -3.15f, -24.56f}
                }
            };

            // Maraudon is connected internally.  Keep the wing labels available
            // for status output, but never filter its roster to one label.
            result[349] = {
                false,
                {
                    {"Maraudon (Orange)", {13282, 12258}, 550.0f, 620.0f, -75.0f},
                    {"Maraudon (Purple)", {12236, 12225}, 520.0f, 590.0f, -75.0f},
                    {"Maraudon (Pristine Waters)", {13601, 12203, 13596, 12201}, 370.0f, 440.0f, -75.0f}
                }
            };

            // Blackrock Spire uses one map for both wings, but a run must
            // select one wing or it will route through the other wing's
            // encounters.  The nearest anchor is selected when the run starts;
            // the test driver also exposes explicit lower/upper start points.
            result[229] = {
                true,
                {
                    {"Lower Blackrock Spire", {9196, 9736, 9236, 9237, 9218, 10584, 10220, 10268, 9568},
                        -22.65f, -299.75f, 31.70f},
                    {"Upper Blackrock Spire", {9816, 10264, 10429, 10339, 10430, 10363},
                        144.44f, -258.03f, 96.41f}
                }
            };

            return result;
        }();
        return store;
    }
}

ai::DungeonWingLayout const* ai::DungeonWingRegistry::Get(uint32 mapId)
{
    auto const& store = Store();
    auto it = store.find(mapId);
    return it == store.end() ? nullptr : &it->second;
}

int32 ai::DungeonWingRegistry::FindNearestWing(Player* bot)
{
    if (!bot)
        return -1;

    DungeonWingLayout const* layout = Get(bot->GetMapId());
    if (!layout || layout->wings.empty())
        return -1;

    float bestDistance = 1.0e30f;
    int32 bestWing = -1;
    for (size_t i = 0; i < layout->wings.size(); ++i)
    {
        DungeonWing const& wing = layout->wings[i];
        float const dx = bot->GetPositionX() - wing.anchorX;
        float const dy = bot->GetPositionY() - wing.anchorY;
        float const dz = bot->GetPositionZ() - wing.anchorZ;
        float const distance = dx * dx + dy * dy + dz * dz;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestWing = static_cast<int32>(i);
        }
    }
    return bestWing;
}

bool ai::DungeonWingRegistry::Contains(uint32 mapId, uint8 wing, uint32 entry)
{
    DungeonWingLayout const* layout = Get(mapId);
    if (!layout || wing >= layout->wings.size())
        return false;

    DungeonWing const& selected = layout->wings[wing];
    for (uint32 candidate : selected.entries)
    {
        if (candidate == entry)
            return true;
    }
    return false;
}

std::string ai::DungeonWingRegistry::WingName(uint32 mapId, uint32 entry)
{
    DungeonWingLayout const* layout = Get(mapId);
    if (!layout)
        return {};

    for (DungeonWing const& wing : layout->wings)
    {
        for (uint32 candidate : wing.entries)
        {
            if (candidate == entry)
                return wing.name;
        }
    }
    return {};
}
