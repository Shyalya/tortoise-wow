#include "playerbot/playerbot.h"
#include "playerbot/AiContextAugment.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/BotSlots.h"

#include <vector>

static std::vector<AiContextAugmenter>& Augmenters()
{
    static std::vector<AiContextAugmenter> list;
    return list;
}

void RegisterAiContextAugmenter(AiContextAugmenter fn)
{
    if (!fn)
        return;

    Augmenters().push_back(fn);

    // Catch up on bots born before this module registered - see the header.
    sRandomPlayerbotMgr.ForEachPlayerbot([fn](Player* bot)
    {
        if (PlayerbotAI* ai = GetBotAI(bot))
            if (ai::AiObjectContext* context = ai->GetAiObjectContext())
                fn(ai, context);
    });
}

void RunAiContextAugmenters(PlayerbotAI* ai, ai::AiObjectContext* context)
{
    if (!ai || !context)
        return;

    for (AiContextAugmenter fn : Augmenters())
        fn(ai, context);
}
