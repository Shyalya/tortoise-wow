#pragma once

#include "playerbot/strategy/Multiplier.h"

namespace ai
{
    class GarrMultiplier : public Multiplier
    {
    public:
        GarrMultiplier(PlayerbotAI* ai) : Multiplier(ai, "garr multiplier") {}
        float GetValue(Action* action) override;
    };

    class BaronGeddonAbilityMultiplier : public Multiplier
    {
    public:
        BaronGeddonAbilityMultiplier(PlayerbotAI* ai) : Multiplier(ai, "baron geddon ability multiplier") {}
        float GetValue(Action* action) override;
    };

    class GolemaggMultiplier : public Multiplier
    {
    public:
        GolemaggMultiplier(PlayerbotAI* ai) : Multiplier(ai, "golemagg multiplier") {}
        float GetValue(Action* action) override;
    };
}
