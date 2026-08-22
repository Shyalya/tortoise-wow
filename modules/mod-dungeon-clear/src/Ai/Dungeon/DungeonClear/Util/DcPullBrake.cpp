/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcPullBrake.h"

#include "DcMovement.h"
#include "DungeonClearUtil.h"   // DC_PULL_* log macros
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "StringFormat.h"
#include "Unit.h"

#include <string>

void DcPullBrake::OnEnterCombat(Player* bot)
{
    if (!bot)
        return;

    // Cheapest gate first, and the one that keeps this off the world's back. This
    // hook fires for every player on the realm on every 0->1 combat transition;
    // outside a dungeon there is no scripted pull to brake, and skipping here means
    // an open-world bot never even materialises its pull context.
    Map const* map = bot->FindMap();
    if (!map || !map->IsDungeon())
        return;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;   // a human driving their own character — never brake them

    AiObjectContext* ctx = botAI->GetAiObjectContext();
    if (!ctx)
        return;

    DcPullContext const& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();

    // Advancing is the ONLY phase with an inbound leg to kill, and it is
    // leader-owned: a follower's own copy of this context never leaves Idle, so the
    // phase test is also the "is this the tank running the pull" test. Returning and
    // Engage are already past the flag; Forming has not started walking at the pack.
    if (pull.phase != DcPullPhase::Advancing)
        return;

    // HardPin rather than Hold, for the one difference between them: Hold early-outs
    // on a bot that is not currently moving, and this can land on exactly that bot —
    // a scripted crossing walks in short steps and spends a think standing between
    // them, with the next step already queued behind a MOVEMENT_COMBAT wait. That is
    // the case where a "stop" that no-ops is worse than useless, because the queued
    // leg resumes a moment later with the pack already on the tank.
    //
    // Still a one-shot: nothing here holds the bot, and the very next tick's action
    // is free to move it. The drag-back does exactly that.
    DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
    DcMovement::ClearMovementWait(bot);

    // Logged at INFO with the range, because this is the measurement that settles
    // where the remaining overshoot comes from. Whatever the tank has travelled
    // between THIS distance and the drag-back's "aggro confirmed at Xyd from camp"
    // was travelled after the brake — i.e. by something that moved it on the combat
    // engine, not by the walk-in coasting.
    float dist = -1.0f;
    if (!pull.pullTarget.IsEmpty())
        if (Unit const* target = ObjectAccessor::GetUnit(*bot, pull.pullTarget))
            dist = bot->GetExactDist2d(target);

    DC_PULL_INFO("[DC:{}] pull brake: combat flag raised mid walk-in ({}) -> inbound "
                 "leg killed at the flag", bot->GetName(),
                 dist >= 0.0f ? Acore::StringFormat("{:.1f}yd to target", dist)
                              : std::string("no pull target"));
}
