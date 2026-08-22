#ifndef DC_COMPAT_PLAYERBOTS_H
#define DC_COMPAT_PLAYERBOTS_H

// mod-playerbots ships one umbrella header that pulls in the AI framework and
// defines GET_PLAYERBOT_AI. The vendored ike3 tree here has the same classes
// under different paths and no umbrella, so this is it.
//
// GET_PLAYERBOT_AI maps onto GetBotAI(), the free accessor this fork grew when
// the bot pointers moved out of Player into module slots. Upstream the macro
// exists to paper over a mod-playerbots branch difference; here it happens to
// be exactly the shape our own accessor already has.

#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotMgr.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/AiFactory.h"
#include "playerbot/BotSlots.h"
#include "playerbot/strategy/AiObject.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/NamedObjectContext.h"

#ifndef GET_PLAYERBOT_AI
#define GET_PLAYERBOT_AI(player) GetBotAI(player)
#endif

#ifndef GET_PLAYERBOT_MGR
#define GET_PLAYERBOT_MGR(player) GetBotMgr(player)
#endif

#endif
