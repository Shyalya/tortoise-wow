#pragma once

#include "GenericTriggers.h"

namespace ai
{
    // Classic ranks only (Turtle / MANGOSBOT_ZERO).
    enum BossAuraSpellIds
    {
        SPELL_SHADOW_RESISTANCE_AURA_RANK_1 = 19876,
        SPELL_FROST_RESISTANCE_AURA_RANK_1 = 19888,
        SPELL_FIRE_RESISTANCE_AURA_RANK_1 = 19891,
        SPELL_SHADOW_RESISTANCE_AURA_RANK_2 = 19895,
        SPELL_SHADOW_RESISTANCE_AURA_RANK_3 = 19896,
        SPELL_FROST_RESISTANCE_AURA_RANK_2 = 19897,
        SPELL_FROST_RESISTANCE_AURA_RANK_3 = 19898,
        SPELL_FIRE_RESISTANCE_AURA_RANK_2 = 19899,
        SPELL_FIRE_RESISTANCE_AURA_RANK_3 = 19900,
    };

    class BossFireResistanceTrigger : public Trigger
    {
    public:
        BossFireResistanceTrigger(PlayerbotAI* ai, std::string const& bossName)
            : Trigger(ai, bossName + " fire resistance trigger"), bossName(bossName) {}
        bool IsActive() override;

    private:
        std::string bossName;
    };

    class BossShadowResistanceTrigger : public Trigger
    {
    public:
        BossShadowResistanceTrigger(PlayerbotAI* ai, std::string const& bossName)
            : Trigger(ai, bossName + " shadow resistance trigger"), bossName(bossName) {}
        bool IsActive() override;

    private:
        std::string bossName;
    };
}
