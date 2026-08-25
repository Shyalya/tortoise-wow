#pragma once
#include "playerbot/strategy/NamedObjectContext.h"
#include "Strategy/DungeonClearStrategy.h"

namespace ai
{
    class DungeonClearStrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        DungeonClearStrategyContext()
        {
            creators["dungeon clear"] = [](PlayerbotAI* ai) { return new DungeonClearStrategy(ai); };
            creators["dungeon clear combat"] = [](PlayerbotAI* ai) { return new DungeonClearCombatStrategy(ai); };
        }
    };
}
