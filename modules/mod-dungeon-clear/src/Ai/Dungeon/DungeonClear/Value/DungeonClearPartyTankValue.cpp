/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearPartyTankValue.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include "Player.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

Player* DungeonClearPartyTankValue::Calculate()
{
    if (!bot)
        return nullptr;

    // The single elected leader for the whole group (party or raid). For the
    // leader bot itself this resolves to `bot`; for every follower — non-tanks
    // and non-leader (off-)tanks alike — it is the tank they should trail.
    Player* leader = DcLeaderSignal::FindLeaderTank(bot);
    if (!leader)
        return nullptr;

    // Only bot tanks can be elected, so this always resolves; guard anyway.
    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return nullptr;

    // Expose the leader only while its clear is actively running and unpaused.
    // A paused/disabled leader returns null so followers stop following it and
    // revert to the player — matching the leader's own paused/off behavior.
    AiObjectContext* leaderCtx = leaderAI->GetAiObjectContext();
    if (DcRun::Of(leaderCtx).enabled &&
        !DcRun::Of(leaderCtx).paused)
        return leader;
    return nullptr;
}
