#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class DungeonClearStrategy : public Strategy
    {
    public:
        DungeonClearStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "dungeon clear"; }
        int GetType() override { return STRATEGY_TYPE_NONCOMBAT; }
    protected:
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class DungeonClearCombatStrategy : public Strategy
    {
    public:
        DungeonClearCombatStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "dungeon clear combat"; }
        int GetType() override { return STRATEGY_TYPE_COMBAT; }
    protected:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}
