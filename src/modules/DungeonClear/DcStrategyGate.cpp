#include "DcStrategyGate.h"
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAiExtension.h"
#include "playerbot/BotState.h"
#include "Maps/Map.h"
#include "Settings/DcSettings.h"

namespace DcStrategyGate
{
    void Reconcile(PlayerbotAI* ai, Player* bot)
    {
        if (!ai || !bot || !sDcSettings.moduleEnabled)
            return;

        Map* map = bot->GetMap();
        bool inDungeon = map && map->IsDungeon();

        bool hasNC = ai->HasStrategy("dungeon clear", BotState::BOT_STATE_NON_COMBAT);
        bool hasC = ai->HasStrategy("dungeon clear combat", BotState::BOT_STATE_COMBAT);

        // Strip strays: NC strategy in combat engine / combat strategy in NC engine.
        if (ai->HasStrategy("dungeon clear", BotState::BOT_STATE_COMBAT))
            ai->ChangeStrategy("-dungeon clear", BotState::BOT_STATE_COMBAT);
        if (ai->HasStrategy("dungeon clear combat", BotState::BOT_STATE_NON_COMBAT))
            ai->ChangeStrategy("-dungeon clear combat", BotState::BOT_STATE_NON_COMBAT);

        switch (Decide(inDungeon, hasNC))
        {
            case Action::Install:
                ai->ChangeStrategy("+dungeon clear", BotState::BOT_STATE_NON_COMBAT);
                break;
            case Action::Strip:
                ai->ChangeStrategy("-dungeon clear", BotState::BOT_STATE_NON_COMBAT);
                break;
            default: break;
        }
        switch (Decide(inDungeon, hasC))
        {
            case Action::Install:
                ai->ChangeStrategy("+dungeon clear combat", BotState::BOT_STATE_COMBAT);
                break;
            case Action::Strip:
                ai->ChangeStrategy("-dungeon clear combat", BotState::BOT_STATE_COMBAT);
                break;
            default: break;
        }
    }

    void Register()
    {
        sPlayerbotAiExtension.RegisterStrategyGate(&Reconcile);
    }
}
