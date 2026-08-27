#pragma once
#include "playerbot/strategy/Multiplier.h"

namespace ai
{
    class DungeonClearMultiplier : public Multiplier
    {
    public:
        DungeonClearMultiplier(PlayerbotAI* ai) : Multiplier(ai, "dungeon clear") {}
        float GetValue(Action* action) override;
    };

    class DungeonClearCombatMultiplier : public Multiplier
    {
    public:
        DungeonClearCombatMultiplier(PlayerbotAI* ai) : Multiplier(ai, "dungeon clear combat") {}
        float GetValue(Action* action) override;
    };
}
