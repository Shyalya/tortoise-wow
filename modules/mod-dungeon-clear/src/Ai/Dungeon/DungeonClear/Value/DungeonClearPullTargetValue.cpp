/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearPullTargetValue.h"

#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonBossesValue.h"
#include "Ai/Dungeon/DungeonClear/Data/ScriptedPullRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

ObjectGuid DungeonClearPullTargetValue::Calculate()
{
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    if (!bot)
        return ObjectGuid::Empty;

    // SCRIPTED PULL STAGE (ScriptedPullRegistry): a hand-authored plan is running,
    // so the pack it names IS the pull target — ahead of the sticky latch and ahead
    // of the corridor scan.
    //
    // It has to be resolved HERE, at the one value every consumer reads, rather
    // than inside the pull action: the plan's whole content is an ORDER, and the
    // scan's own answer is "the nearest blocker". In Selin's room those disagree —
    // the west pack is 17yd from the approach and the east pack 26yd, and the plan
    // pulls east first because that is the one the tank has a safe sight-line to
    // while the party is still walking up. Overriding only the action would leave
    // the Dynamic governor, the pull trigger, the blocking-trash stand-down and the
    // advance camp-trail gate all aimed at the OTHER pack, which is the same class
    // of split-brain that made the earlier attempts at this room thrash.
    if (ScriptedPullStage const* stage = DcTickMemoAccess::ScriptedStage(bot, context))
        if (Unit* member = ScriptedPullRegistry::NearestPackMember(bot, context, *stage))
            return member->GetObjectGuid();

    // Sticky fast path: the governor's per-pack latch IS the target while it
    // still resolves to a valid pull. Deliberately does NOT require it to still
    // be the closest blocker nor to still sit inside the scan corridor — packs
    // drift while patrolling, and re-running corridor membership is exactly the
    // instability being removed. Death/abort/door/distance release it.
    DcPullContext const& pull =
        context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    if (!pull.decisionTarget.IsEmpty())
    {
        Unit* sticky = ObjectAccessor::GetUnit(*bot, pull.decisionTarget);
        if (sticky && DcTargeting::IsStickyPullTargetValid(bot, context, sticky))
            return pull.decisionTarget;
    }

    std::optional<DungeonBossInfo> next =
        context->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next.has_value())
        return ObjectGuid::Empty;

    Unit* fresh = DcTargeting::FindPullTarget(botAI, *next);
    return fresh ? fresh->GetObjectGuid() : ObjectGuid::Empty;
}
