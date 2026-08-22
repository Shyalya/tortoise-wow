/*
 * mod-dungeon-clear — DungeonClearDispatch.h
 *
 * Shared helper that dispatches DungeonClear subcommands to a player's tank
 * bot(s). Used by both the `.dc` chat command (DungeonClearCommand.cpp) and
 * the addon-message hook (DungeonClearAddonHook.cpp).
 */

#ifndef _DUNGEON_CLEAR_DISPATCH_H
#define _DUNGEON_CLEAR_DISPATCH_H

#include <string>
#include "Event.h"
#include "Group.h"
#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"

namespace DungeonClearDispatch
{
    // Dispatch "dc <sub>" to the issuer's group's elected leader tank — the
    // single bot that owns the run for the whole party or raid (see
    // DcLeaderSignal::FindLeaderTank). All DC state lives on the leader, so
    // every subcommand (on/off/skip/pause/status/bosses/go) goes there alone;
    // followers react to the leader's flags on their own. Returns the number of
    // bots that handled it (0 or 1); 0 means "no tank bot found in the group".
    inline uint32 DispatchToTankBots(Player* issuer, std::string const& action, std::string const& param = "")
    {
        if (!issuer)
            return 0;

        Player* leader = DcLeaderSignal::FindLeaderTank(issuer);
        if (!leader)
            return 0;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(leader);
        if (!botAI)
            return 0;

        botAI->DoSpecificAction(action, Event("dc", param, issuer), true);
        return 1;
    }
}

#endif
