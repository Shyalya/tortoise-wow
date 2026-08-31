#pragma once
#include "playerbot/strategy/Value.h"
#include "TargetValue.h"

namespace ai
{
    class SpellCastUsefulValue : public MillisecondCalculatedValue<bool>, public Qualified
	{
	public:
        SpellCastUsefulValue(PlayerbotAI* ai, std::string name = "spell cast useful") : MillisecondCalculatedValue<bool>(ai, name), Qualified() {}
        virtual bool Calculate() override;

        virtual std::string Format() override
        {
            return Calculate() ? "true" : "false";
        }
    };

    class SpellReadyValue : public BoolCalculatedValue, public Qualified
    {
    public:
        SpellReadyValue(PlayerbotAI* ai, std::string name = "spell ready") : BoolCalculatedValue(ai, name), Qualified() {}
        virtual bool Calculate() override;
    };
}
