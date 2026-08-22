/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcTickMemo.h"

#include "DcEngageGeometry.h"
#include "DcPartyState.h"
#include "DungeonClearTuning.h"   // DC_ENGAGE_RANGE
#include "Ai/Dungeon/DungeonClear/Data/ScriptedPullRegistry.h"
#include "Player.h"
#include "Timer.h"
#include "AiObjectContext.h"
#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

bool DcTickMemo::MemoValid(std::uint32_t stampMs, std::uint32_t now)
{
    if (stampMs == 0)
        return false;
    return getMSTimeDiff(stampMs, now) <= kMemoWindowMs;
}

void DcTickMemo::EnsureFresh(std::uint32_t now)
{
    if (MemoValid(stampMs, now))
        return;
    *this = DcTickMemo{};       // clear all cached fields
    stampMs = now ? now : 1;    // never stamp 0 (== "never filled")
}

namespace
{
    DcTickMemo& Memo(AiObjectContext* ctx)
    {
        DcTickMemo& m =
            ctx->GetValue<DcTickMemo&>(DcKey::TickMemo)->Get();
        m.EnsureFresh(getMSTime());
        return m;
    }
}

bool DcTickMemoAccess::AtBossEngage(Player* bot, AiObjectContext* ctx,
                                    DungeonBossInfo const& next)
{
    if (!bot || !ctx)
        return DcEngageGeometry::IsAtBossEngage(bot, ctx, next, DC_ENGAGE_RANGE);

    DcTickMemo& m = Memo(ctx);
    if (m.atBossEngage < 0)
        m.atBossEngage =
            DcEngageGeometry::IsAtBossEngage(bot, ctx, next, DC_ENGAGE_RANGE) ? 1 : 0;
    return m.atBossEngage == 1;
}

bool DcTickMemoAccess::BetweenPullsReady(Player* bot, AiObjectContext* ctx,
                                         bool requireNoLoot)
{
    if (!bot || !ctx)
        return DcPartyState::IsBetweenPullsReady(bot, ctx, requireNoLoot);

    DcTickMemo& m = Memo(ctx);
    std::int8_t& slot =
        requireNoLoot ? m.betweenPullsReadyStrict : m.betweenPullsReadyLoose;
    if (slot < 0)
        slot = DcPartyState::IsBetweenPullsReady(bot, ctx, requireNoLoot) ? 1 : 0;
    return slot == 1;
}

ScriptedPullStage const* DcTickMemoAccess::ScriptedStage(Player* bot, AiObjectContext* ctx)
{
    if (!bot || !ctx)
        return nullptr;

    DcTickMemo& m = Memo(ctx);
    if (m.scriptedStage == -2)
    {
        ScriptedPullStage const* const due = ScriptedPullRegistry::DueStage(bot, ctx);
        m.scriptedStage = due ? static_cast<std::int32_t>(due->order) : -1;
        return due;
    }
    // Only the ORDER is memoised (the pointer would be just as stable, but storing
    // an int keeps the memo a plain trivially-copyable POD that EnsureFresh can
    // reset by assignment). Re-resolving it is a two-row table walk.
    return ScriptedPullRegistry::Find(bot->GetMapId(), m.scriptedStage);
}
