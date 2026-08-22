#pragma once
#include <string>

#ifndef GenerateBotHelp
//#define GenerateBotHelp //Enable only for help generation
#endif

#ifndef GenerateBotTests
//#define GenerateBotTests //Enable only for test generation
#endif

class PlayerbotAI;

namespace ai
{
    class PlayerbotAIAware 
    {
    public:
        PlayerbotAIAware(PlayerbotAI* const ai) : ai(ai) { }
        virtual ~PlayerbotAIAware() = default;
        virtual std::string getName() { return std::string(); }
    protected:
        PlayerbotAI* ai;

        // Second name for the member above, for code ported from
        // mod-playerbots, which spells it botAI. A reference rather than a
        // macro: a #define would also rewrite the local variables of that name
        // that ported files declare for themselves. Those locals shadow this,
        // which is what should happen.
        PlayerbotAI* const& botAI = ai;
    };
}