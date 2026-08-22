/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcFirstContact.h"

#include "DungeonClearUtil.h"   // DC_PULL_* log macros
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include "CombatManager.h"
#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "StringFormat.h"
#include "Unit.h"

#include <optional>
#include <string>

namespace
{
    // How many party members were ALREADY flagged when this one was grabbed. 0 is
    // the signature of a fresh pull; anything else means this contact joined a fight
    // that was already running, which is the case the pull record cannot show.
    uint32 CountPartyAlreadyInCombat(Player* bot)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return 0;
        uint32 count = 0;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != bot && member->IsAlive() &&
                member->GetMapId() == bot->GetMapId() && member->IsInCombat())
                ++count;
        }
        return count;
    }
}

void DcFirstContact::OnEnterCombat(Player* bot, Unit* enemy)
{
    if (!bot || !enemy)
        return;

    // Same cheapest-gate-first discipline as DcPullBrake, and for the same reason:
    // this hook fires for every player on the realm on every 0->1 combat transition.
    Map const* map = bot->FindMap();
    if (!map || !map->IsDungeon())
        return;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    // Only inside a live, unpaused run — a paused run is a deliberate hand-off to
    // the player and their fights are not the harness's business.
    Player* owner = DcLeaderSignal::FindRunOwner(bot);
    PlayerbotAI* ownerAI = owner ? GET_PLAYERBOT_AI(owner) : nullptr;
    if (!ownerAI)
        return;
    DcRunState const& run = DcRun::Of(ownerAI);
    if (!run.enabled || run.paused)
        return;

    AiObjectContext* ctx = botAI->GetAiObjectContext();
    if (!ctx)
        return;

    Creature const* asCreature = enemy->ToCreature();
    uint32 const entry = enemy->GetEntry();

    // Is this the thing the run is currently walking towards? The single most
    // load-bearing field: an objective appearing here at all means it was NOT
    // pulled, because a pulled one goes through the pull FSM and gets a pull row.
    bool isObjective = false;
    std::optional<DungeonBossInfo> const next =
        ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (next.has_value() && next->entry == entry)
        isObjective = true;

    // How far the enemy is from where it SPAWNED. A mob standing on its home
    // position was reached by us; one a long way off came to us, or was dragged.
    float homeDist = -1.0f;
    if (asCreature)
        homeDist = asCreature->GetDistance(asCreature->GetHomePosition());

    DcPullContext const& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    // Tortoise port: the count of units this enemy is fighting is its threat
    // list size here - same number the combat-ref count gave upstream.
    uint32 const refs =
        static_cast<uint32>(enemy->GetThreatMgr().GetThreatList().size());
    uint32 const alreadyFighting = CountPartyAlreadyInCombat(bot);

    // INFO, and on the pull logger so dc_test_run.py's slice and SIGNALS section
    // pick it up next to the pull rows this is the missing half of. One line per
    // party member per fight — a handful per pack, not a per-tick stream.
    DC_PULL_INFO("[DC:{}] first contact: {}{} (entry {}) at {:.1f}yd, {} from its "
                 "spawn, refs={} | party already fighting: {} | pull phase {} camp {} "
                 "stage {}",
                 bot->GetName(), enemy->GetName(), isObjective ? " [OBJECTIVE]" : "",
                 entry, bot->GetExactDist(enemy),
                 homeDist >= 0.0f ? Acore::StringFormat("{:.1f}yd", homeDist)
                                  : std::string("n/a"),
                 refs, alreadyFighting,
                 static_cast<uint32>(pull.phase), pull.HasCamp() ? "set" : "none",
                 pull.scriptedStage);

    // An OBJECTIVE arriving with the party already fighting is the shape that has
    // no other record anywhere, so it gets its own greppable line at WARN. This is
    // the one that answers the Delrissa question: it fires, or it does not, and
    // either answer eliminates half the hypotheses.
    if (isObjective && alreadyFighting > 0 && pull.phase == DcPullPhase::Idle)
        LOG_WARN("playerbots.dungeonclear",
                 "[DC:{}] OBJECTIVE JOINED AN ONGOING FIGHT: {} (entry {}) at "
                 "{:.1f}yd with no pull in flight — {} other member(s) already "
                 "in combat", bot->GetName(), enemy->GetName(), entry,
                 bot->GetExactDist(enemy), alreadyFighting);
}
