#pragma once
#include "playerbot/strategy/Value.h"

namespace ai
{
    class ActiveSpellValue : public MillisecondCalculatedValue<uint32>
	{
	public:
        ActiveSpellValue(PlayerbotAI* ai, std::string name = "active spell") : MillisecondCalculatedValue<uint32>(ai, name, 100) {}

        virtual uint32 Calculate() override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "active spell"; } //Must equal iternal name
        virtual std::string GetHelpTypeName() { return "spell"; }
        virtual std::string GetHelpDescription() { return "This value contains the spell id of the spell that the bot is currently casting."; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif 
    };
}
