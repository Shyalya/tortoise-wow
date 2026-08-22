/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearHealTargetValue.h"

#include "Creature.h"
#include "Group.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

ObjectGuid DungeonClearHealTargetValue::Calculate()
{
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    if (!bot)
        return ObjectGuid::Empty;

    Group* group = bot->GetGroup();
    if (!group)
        return ObjectGuid::Empty;

    // The elected leader tank (non-null only on an active, unpaused run) is the
    // one we bias toward — it is the member being dragged out of sight.
    Player* tank = context->GetValue<Player*>(DcKey::PartyTank)->Get();

    float const hpFloor = DcSettings::GetFloat(bot, "HealRepositionHpFloor");
    float const tankBias = DcSettings::GetFloat(bot, "HealRepositionTankBias");
    // Mirror the stock heal-candidate radius (healDistance * 2) but WITHOUT the
    // LOS filter — keeping the out-of-sight member in candidacy is the whole point.
    float const maxDist = sPlayerbotAIConfig.healDistance * 2.0f;

    std::vector<DungeonClearMath::HealCandidate> candidates;
    std::vector<ObjectGuid> guids;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || member->IsGameMaster())
            continue;
        if (member->GetMapId() != bot->GetMapId() || member->IsCharmed())
            continue;
        if (bot->GetDistance2d(member) > maxDist)
            continue;

        candidates.push_back({ member->GetHealthPct(), tank && member == tank });
        guids.push_back(member->GetObjectGuid());
    }

    // Fold in the DC escortee (Thrall / the Disciple / ...) so the healer also
    // repositions to keep IT in range and LOS — the reposition counterpart to the
    // `party member to heal` decorator that puts the escortee in the heal rotation.
    // Same LOS-blind maxDist gate as the members above (keeping an out-of-sight
    // escortee in candidacy is the whole point); never tank-biased.
    if (Creature* escortee = DcLeaderSignal::GetLeaderEscortee(bot))
    {
        if (escortee->IsAlive() && !escortee->IsCharmed() &&
            bot->GetDistance2d(escortee) <= maxDist)
        {
            candidates.push_back({ escortee->GetHealthPct(), false });
            guids.push_back(escortee->GetObjectGuid());
        }
    }

    std::size_t const idx =
        DungeonClearMath::SelectHealTarget(candidates, hpFloor, tankBias);
    if (idx == DungeonClearMath::HealTargetNone)
        return ObjectGuid::Empty;

    return guids[idx];
}
