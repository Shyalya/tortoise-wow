#pragma once

#include "playerbot/strategy/Action.h"

namespace ai
{
    class BossFireResistanceAction : public Action
    {
    public:
        BossFireResistanceAction(PlayerbotAI* ai, std::string const& bossName)
            : Action(ai, "mc " + bossName + " fire resistance"), bossName(bossName) {}
        bool Execute(Event& event) override;
        bool isUseful() override;

    private:
        std::string bossName;
    };

    class BossShadowResistanceAction : public Action
    {
    public:
        BossShadowResistanceAction(PlayerbotAI* ai, std::string const& bossName)
            : Action(ai, "mc " + bossName + " shadow resistance"), bossName(bossName) {}
        bool Execute(Event& event) override;
        bool isUseful() override;

    private:
        std::string bossName;
    };
}
