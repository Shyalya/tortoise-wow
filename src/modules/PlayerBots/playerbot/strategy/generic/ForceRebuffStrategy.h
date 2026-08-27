#pragma once

#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class ForceRebuffStrategy : public Strategy
    {
    public:
        ForceRebuffStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "force rebuff"; }

    protected:
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}
