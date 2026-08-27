#include "Action/DungeonClearActions.h"
#include "AI/ScriptDevAI/ScriptDevAIMgr.h"
#include "playerbot/playerbot.h"
#include "Util/DungeonClearUtil.h"
#include "Util/DcAddonComm.h"
#include "Settings/DcSettings.h"
#include "Data/DungeonEventRegistry.h"
#include "DcValueKeys.h"
#include "DcRunState.h"
#include "Group/Group.h"
#include "Objects/GameObject.h"
#include "Maps/GridSearchers.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"
#include "LootMgr.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>

using namespace ai;

namespace
{
    uint32 EventStaleGapMs()
    {
        uint32 const slowestTick = std::max<uint32>(sPlayerbotAIConfig.passiveDelay,
            sPlayerbotAIConfig.reactDelay * 30u);
        return std::max<uint32>(3000u, slowestTick * 2u);
    }

    uint32 EventSearchRadius(DcEventStep const& step, uint32 fallback)
    {
        return step.searchRadius ? step.searchRadius : fallback;
    }

    Creature* FindAliveCreature(Player* bot, uint32 entry, float radius)
    {
        if (!bot || !entry)
            return nullptr;

        std::list<Creature*> list;
        GetCreatureListWithEntryInGrid(list, bot, entry, radius);
        for (Creature* creature : list)
        {
            if (creature && creature->IsAlive())
                return creature;
        }
        return nullptr;
    }

    uint32 CountAliveCreatures(Player* bot, uint32 entry, float radius)
    {
        if (!bot || !entry)
            return 0;

        std::list<Creature*> list;
        GetCreatureListWithEntryInGrid(list, bot, entry, radius);
        uint32 count = 0;
        for (Creature* creature : list)
        {
            if (creature && creature->IsAlive())
                ++count;
        }
        return count;
    }

    bool HasGossipOption(Player* bot, int32 option)
    {
        return bot && bot->GetPlayerMenu() && option >= 0
            && static_cast<uint32>(option) < bot->GetPlayerMenu()->GetGossipMenu().MenuItemCount();
    }

    Unit* FindHostileForClearStep(Player* bot, DcEventStep const& step, float radius)
    {
        Map* map = bot ? bot->GetMap() : nullptr;
        if (!bot || !map)
            return nullptr;

        bool const hasCenter = step.x || step.y || step.z;
        // The visitor is centered on the objective rather than on the bot.
        // Cell::VisitAllObjects keeps its no-create behavior, so an event only
        // examines grids already visible to the run; it cannot load unrelated
        // dungeon space merely because a large clear radius was configured.
        float const centerDistance = hasCenter
            ? bot->GetDistance(step.x, step.y, step.z) : 0.0f;
        float const verticalReach = step.zBand > 0.0f ? step.zBand : 0.0f;
        float const areaReach = std::sqrt(radius * radius + verticalReach * verticalReach);
        float const searchRadius = areaReach + centerDistance + 10.0f;
        std::list<Unit*> list;
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(bot, searchRadius);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(list, check);
        if (hasCenter)
            Cell::VisitAllObjects(step.x, step.y, map, searcher, radius, true);
        else
            Cell::VisitAllObjects(bot, searcher, searchRadius, true);

        Unit* best = nullptr;
        float bestDistance = 1.0e30f;
        for (Unit* unit : list)
        {
            if (!unit || unit->GetTypeId() != TYPEID_UNIT || !unit->IsAlive())
                continue;
            if (!unit->IsVisibleForOrDetect(bot, bot, false))
                continue;
            Creature* creature = static_cast<Creature*>(unit);
            if (creature->IsCivilian() || creature->IsCritter())
                continue;
            if (step.x || step.y || step.z)
            {
                if (unit->GetDistance2d(step.x, step.y) > radius)
                    continue;
                if (step.zBand > 0.0f && std::abs(unit->GetPositionZ() - step.z) > step.zBand)
                    continue;
            }
            if (std::find(step.excludeEntries.begin(), step.excludeEntries.end(), creature->GetEntry())
                != step.excludeEntries.end())
                continue;

            float const distance = bot->GetDistance(unit);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = unit;
            }
        }
        return best;
    }
}

bool DungeonClearAdvanceAction::Execute(Event& /*event*/)
{
    auto next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next)
        return false;
    return MoveTo(next->mapId, next->x, next->y, next->z, false, false, false, true);
}

bool DungeonClearEngageTrashAction::Execute(Event& event)
{
    Unit* target = DcUtil::FindHostileNear(bot, sDcSettings.trashEngageRange);
    if (!target)
        return false;
    return Attack(event.getOwner() ? event.getOwner() : bot, target);
}

bool DungeonClearEngageBossAction::Execute(Event& event)
{
    auto next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next || next->kind != DungeonAnchorKind::Boss)
        return false;

    std::list<Creature*> list;
    for (uint32 entry : next->alternateEntries)
        GetCreatureListWithEntryInGrid(list, bot, entry, sDcSettings.engageRange + 15.0f);
    GetCreatureListWithEntryInGrid(list, bot, next->entry, sDcSettings.engageRange + 15.0f);
    Creature* boss = nullptr;
    for (Creature* c : list)
    {
        if (c && c->IsAlive())
        {
            boss = c;
            break;
        }
    }
    if (!boss)
    {
        // Walk in and hope spawn is there.
        return MoveTo(next->mapId, next->x, next->y, next->z, false, false, false, true);
    }

    // Persist the encounter observation together with the concrete creature
    // we are attacking.  NextDungeonBossValue can then confirm its corpse or
    // death state even if the party has moved beyond the anchor by the time
    // the cached value is recalculated.
    DcRunState& state = AI_VALUE(DcRunState&, DcKey::RunState);
    state.observedBosses.insert(DungeonBossStateKey(*next));
    state.activeBossGuid = boss->GetObjectGuid();
    state.activeBossStateKey = DungeonBossStateKey(*next);
    state.activeBossInstanceId = bot->GetInstanceId();
    return Attack(event.getOwner() ? event.getOwner() : bot, boss);
}

bool DungeonClearObjectiveArriveAction::Execute(Event& /*event*/)
{
    auto next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next || next->kind != DungeonAnchorKind::Objective)
        return false;

    if (next->eventId)
    {
        // Hand off to event runner; do not latch yet.
        return false;
    }

    auto& cleared = AI_VALUE(std::unordered_set<uint32>&, DcKey::ClearedAnchors);
    uint32 const anchorKey = DungeonBossStateKey(*next);
    cleared.insert(anchorKey);
    DcUtil::TellGroup(ai, bot, std::string("Objective reached: ") + next->name);
    return true;
}

bool DungeonClearRunEventAction::Execute(Event& /*event*/)
{
    Map* map = bot->GetMap();
    if (!map)
        return false;

    auto next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    uint32 eventId = next && next->eventId ? next->eventId : 0;
    DungeonEvent const* ev = nullptr;
    if (eventId)
        ev = DungeonEventRegistry::Instance().FindEvent(map->GetId(), eventId);
    if (!ev && next)
        ev = DungeonEventRegistry::Instance().FindDueEvent(bot, context, map->GetId(), next->encounterIndex);
    if (!ev || ev->steps.empty())
        return false;

    DcRunState& state = AI_VALUE(DcRunState&, DcKey::RunState);
    uint32& stepIdx = AI_VALUE(uint32&, DcKey::EventProgress);
    uint32& eventStartedAt = AI_VALUE(uint32&, DcKey::EventStartedAt);
    uint32& stepStartedAt = AI_VALUE(uint32&, DcKey::EventStepStartedAt);
    uint32 now = WorldTimer::getMSTime();
    uint32 instanceId = map->GetInstanceId();

    auto clearEventProgress = [&]()
    {
        state.eventId = 0;
        state.eventInstanceId = 0;
        state.eventLastDriveAt = 0;
        state.eventMaxStep = 0;
        state.eventProgressAt = 0;
        state.eventActionSent = false;
        state.eventActionMenuId = 0;
        stepIdx = 0;
        eventStartedAt = 0;
        stepStartedAt = 0;
    };

    auto latchEvent = [&](bool optionalFailure, char const* reason)
    {
        bool const markCleared = optionalFailure || !reason;
        auto& cleared = AI_VALUE(std::unordered_set<uint32>&, DcKey::ClearedAnchors);
        if (markCleared && next && next->kind == DungeonAnchorKind::Objective)
        {
            uint32 const anchorKey = DungeonBossStateKey(*next);
            cleared.insert(anchorKey);
        }
        if (markCleared)
            cleared.insert(ev->eventId);
        if (optionalFailure)
            DcUtil::TellGroup(ai, bot, std::string("Optional event skipped: ") + ev->name);
        else if (reason)
            DcUtil::TellGroup(ai, bot, std::string("Dungeon clear paused: ") + reason);
        clearEventProgress();
        return true;
    };

    auto failEvent = [&](char const* reason)
    {
        if (!ev->required)
            return latchEvent(true, nullptr);

        state.paused = true;
        state.pauseReason = std::string(reason) + ev->name;
        return latchEvent(false, state.pauseReason.c_str());
    };

    // An event progress record belongs to one event and one instance.  A
    // stale record from another objective or a new instance must never resume
    // in the middle of an unrelated step.
    if (state.eventId != ev->eventId || state.eventInstanceId != instanceId)
    {
        state.eventId = ev->eventId;
        state.eventInstanceId = instanceId;
        state.eventMaxStep = 0;
        state.eventProgressAt = now;
        eventStartedAt = now;
        stepStartedAt = now;
        state.eventActionSent = false;
        stepIdx = 0;
    }
    else if (!ev->persistent && state.eventLastDriveAt
        && WorldTimer::getMSTimeDiff(state.eventLastDriveAt, now) > EventStaleGapMs())
    {
        // Non-persistent events are short room interactions.  A long gap is a
        // combat/reload boundary; restart them from a clean, idempotent step 0.
        state.eventMaxStep = 0;
        state.eventProgressAt = now;
        eventStartedAt = now;
        stepStartedAt = now;
        state.eventActionSent = false;
        stepIdx = 0;
    }
    state.eventLastDriveAt = now;

    if (!eventStartedAt)
        eventStartedAt = now;

    if (ev->timeoutMs && WorldTimer::getMSTimeDiff(eventStartedAt, now) >= ev->timeoutMs)
        return failEvent("event timeout: ");

    if (!stepStartedAt)
        stepStartedAt = now;

    auto advanceStep = [&]()
    {
        ++stepIdx;
        stepStartedAt = now;
        state.eventActionSent = false;
        if (stepIdx > state.eventMaxStep)
        {
            state.eventMaxStep = stepIdx;
            state.eventProgressAt = now;
        }
        return true;
    };

    if (stepIdx >= ev->steps.size())
    {
        latchEvent(false, nullptr);
        DcUtil::TellGroup(ai, bot, std::string("Event done: ") + ev->name);
        return true;
    }

    DcEventStep const& step = ev->steps[stepIdx];
    uint32 const stepTimeout = step.timeoutMs ? step.timeoutMs : 0;
    if (stepTimeout && WorldTimer::getMSTimeDiff(stepStartedAt, now) >= stepTimeout)
        return failEvent("event step timeout: ");

    switch (step.type)
    {
        case DcEventStepType::MoveTo:
            if (bot->GetDistance(step.x, step.y, step.z) <= step.radius)
                return advanceStep();
            return MoveTo(map->GetId(), step.x, step.y, step.z, false, false, false, true);

        case DcEventStepType::Jump:
            if (bot->GetDistance(step.x, step.y, step.z) <= step.radius)
                return advanceStep();
            bot->GetMotionMaster()->MoveJump(step.x, step.y, step.z, 10.0f, 5.0f);
            return true;

        case DcEventStepType::UseGO:
        {
            GameObject* go = DcUtil::FindGONear(bot, step.entry, EventSearchRadius(step, 40));
            if (!go)
                return false;
            float const interactRadius = step.radius > 0.0f ? step.radius : INTERACTION_DISTANCE;
            if (bot->GetDistance(go) > interactRadius)
                return MoveNear(go->GetMapId(), go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
            bot->GetMotionMaster()->Clear();
            bot->SetFacingToObject(go);
            go->Use(bot);
            return advanceStep();
        }

        case DcEventStepType::TalkNpc:
        {
            std::list<Creature*> list;
            GetCreatureListWithEntryInGrid(list, bot, step.entry, EventSearchRadius(step, 40));
            Creature* npc = list.empty() ? nullptr : list.front();
            if (!npc)
                return false;
            float const interactRadius = step.radius > 0.0f ? step.radius : INTERACTION_DISTANCE;
            if (bot->GetDistance(npc) > interactRadius)
                return MoveNear(npc->GetMapId(), npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ());

            // Gossip is a request/response exchange.  Do not send the same
            // interaction on every AI tick, and do not advance until the
            // server has either closed the menu or changed its contents.
            if (state.eventActionSent)
            {
                uint32 const menuItems = bot->GetPlayerMenu()
                    ? bot->GetPlayerMenu()->GetGossipMenu().MenuItemCount() : 0;
                uint32 const menuId = bot->GetPlayerMenu()
                    ? bot->GetPlayerMenu()->GetGossipMenu().GetMenuId() : 0;
                if (step.gossipOption < 0)
                    return menuItems ? advanceStep() : true;
                if (!menuItems || menuId != state.eventActionMenuId)
                    return advanceStep();
                return true;
            }

            if (!sScriptDevAIMgr.OnGossipHello(bot, npc))
                bot->PrepareGossipMenu(npc, npc->GetDefaultGossipMenuId());
            bot->SendPreparedGossip(npc);

            if (step.gossipOption >= 0)
            {
                if (!HasGossipOption(bot, step.gossipOption))
                    return false;

                state.eventActionMenuId = bot->GetPlayerMenu()->GetGossipMenu().GetMenuId();

                WorldPacket packet;
                packet << npc->GetObjectGuid();
#ifdef MANGOSBOT_ZERO
                packet << static_cast<uint32>(step.gossipOption);
#else
                packet << bot->GetPlayerMenu()->GetGossipMenu().GetMenuId();
                packet << static_cast<uint32>(step.gossipOption);
#endif
                packet << std::string();
                bot->GetSession()->HandleGossipSelectOptionOpcode(packet);
            }
            state.eventActionSent = true;
            return true;
        }

        case DcEventStepType::WaitForSpawn:
        {
            uint32 const radius = EventSearchRadius(step, 80);
            uint32 const alive = CountAliveCreatures(bot, step.entry, radius);
            bool const satisfied = step.wantAlive ? alive > 0 : alive == 0;
            return satisfied ? advanceStep() : true;
        }

        case DcEventStepType::WaitGOState:
        {
            GameObject* go = DcUtil::FindGONear(bot, step.entry, EventSearchRadius(step, 80));
            if (!go)
                return false;
            return go->GetGoState() == step.goState ? advanceStep() : true;
        }

        case DcEventStepType::KillCreature:
        {
            uint32 const radius = EventSearchRadius(step, static_cast<uint32>(step.radius));
            if (!step.entry)
            {
                Unit* target = DcUtil::FindHostileNear(bot, static_cast<float>(radius));
                return target ? Attack(bot, target) : advanceStep();
            }

            uint32 const alive = CountAliveCreatures(bot, step.entry, static_cast<float>(radius));
            if (alive < step.count)
                return advanceStep();
            Creature* target = FindAliveCreature(bot, step.entry, static_cast<float>(radius));
            return target ? Attack(bot, target) : true;
        }

        case DcEventStepType::ClearRadius:
        {
            float const radius = step.radius > 0.0f ? step.radius : 40.0f;
            Unit* target = FindHostileForClearStep(bot, step, radius);
            if (target)
                return Attack(bot, target);

            if (!(step.x || step.y || step.z))
                return advanceStep();

            // Once the objective grid is loaded, the centered scan above is
            // authoritative for the whole circle.  First settle the party at
            // the anchor, though: this loads the relevant grid when the event
            // was entered from an adjacent cell and removes the old 75%-of-
            // radius blind spot.  The event/step timeout bounds an unreachable
            // anchor instead of waiting forever for an unloaded area.
            float const centerDistance = bot->GetDistance(step.x, step.y, step.z);
            bool const anchorLoaded = map->IsLoaded(step.x, step.y);
            float const settleRadius = std::max(5.0f,
                std::min(radius * 0.25f, sDcSettings.objectiveArriveRadius));
            if (!anchorLoaded || centerDistance > settleRadius)
                return MoveTo(map->GetId(), step.x, step.y, step.z, false, false, false, true);

            return advanceStep();
        }

        case DcEventStepType::WaitMs:
            return WorldTimer::getMSTimeDiff(stepStartedAt, now) >= step.waitMs
                ? advanceStep() : true;

        case DcEventStepType::Custom:
            if (step.customFn)
                return step.customFn(ai, bot) ? advanceStep() : false;
            // Keep the historical Deadmines cannon retry as an explicit
            // compatibility hook.  Unknown custom labels must fail visibly;
            // silently advancing a missing script is worse than pausing.
            if (step.text == "cannon volley")
            {
                if (GameObject* go = DcUtil::FindGONear(bot, 16398, 20.0f))
                {
                    go->Use(bot);
                    return advanceStep();
                }
            }
            return failEvent("unsupported custom event step: ");
    }
    return failEvent("unsupported event step: ");
}

bool DungeonClearFollowTankAction::Execute(Event& /*event*/)
{
    ObjectGuid tankGuid = AI_VALUE(ObjectGuid, DcKey::PartyTank);
    Player* tank = ObjectAccessor::FindPlayer(tankGuid);
    if (!tank || tank == bot)
        return false;
    return MoveTo(tank, ai->GetRange("follow"));
}

bool DungeonClearAssistTankAction::Execute(Event& event)
{
    Player* tank = DcUtil::FindEnabledTank(bot);
    if (!tank)
        return false;
    Unit* target = tank->GetVictim();
    if (!target)
        target = DcUtil::FindHostileNear(bot, sDcSettings.trashEngageRange);
    if (!target)
        return false;
    return Attack(event.getOwner() ? event.getOwner() : bot, target);
}

bool DungeonClearDisableOnDeathAction::Execute(Event& /*event*/)
{
    DcUtil::DisableDungeonClear(ai, bot, "party wiped");
    return true;
}

bool DungeonClearRecoverAction::Execute(Event& event)
{
    DcRunState& state = AI_VALUE(DcRunState&, DcKey::RunState);
    if (!state.enabled)
        return false;

    bool allDead = !bot->IsAlive();
    if (Group* group = bot->GetGroup())
    {
        uint32 const mapId = bot->GetMapId();
        uint32 const instanceId = bot->GetInstanceId();
        allDead = true;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (member && member->IsInWorld() && member->GetMapId() == mapId
                && member->GetInstanceId() == instanceId && member->IsAlive())
            {
                allDead = false;
                break;
            }
        }
    }

    if (allDead && !state.paused)
    {
        DcUtil::CancelDungeonClearEvent(ai);
        state.paused = true;
        state.pauseReason = "party wiped; waiting for recovery";
        bool const preventRelease = DcUtil::EffectivePreventBotRelease(bot);

        // Releasing is deliberately opt-in because a real player may be
        // about to resurrect the group.  When enabled, use the existing
        // PlayerBots manual release path so this explicit DungeonClear
        // choice is honored even when the stock action's master-distance
        // safety gate would otherwise defer the release.
        bool released = false;
        if (!preventRelease)
        {
            auto releaseMember = [&](Player* member)
            {
                if (!member || member->IsAlive())
                    return;
                if (PlayerbotAI* memberAi = member->GetPlayerbotAI())
                    released = memberAi->DoSpecificAction("release", event, true) || released;
            };

            if (Group* group = bot->GetGroup())
            {
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    releaseMember(ref->getSource());
            }
            else
                releaseMember(bot);
        }

        DcUtil::TellGroup(ai, bot, preventRelease
            ? "Party wiped; waiting for resurrection."
            : (released ? "Party wiped; releasing and waiting for recovery."
                       : "Party wiped; waiting for recovery."));
        DcAddonComm::PushStatus(ai, bot);
        return true;
    }

    Map* map = bot->GetMap();
    if (state.paused && map && (map->IsDungeon() || map->IsRaid())
        && DcUtil::PartyReadyToResume(bot))
    {
        state.OnResume();
        DcUtil::TellGroup(ai, bot, "Party recovered; dungeon clear resumed.");
        DcAddonComm::PushStatus(ai, bot);
        return true;
    }

    return false;
}

bool DungeonClearRestPartyAction::Execute(Event& event)
{
    if (!DcUtil::PartyNeedsRest(bot) || !bot->IsAlive() || bot->IsInCombat())
        return false;

    bool acted = false;
    bool resting = false;
    auto restMember = [&](Player* member)
    {
        if (!member || !member->IsAlive() || member->IsInCombat())
            return;
        PlayerbotAI* memberAi = member->GetPlayerbotAI();
        if (!memberAi)
            return;

        resting = resting || member->IsSitState() || member->IsNonMeleeSpellCasted(true);

        if (member->GetHealthPercent() < DcUtil::EffectiveRestHealth(bot))
            acted = memberAi->DoSpecificAction("food", event, true) || acted;

        uint32 const maxMana = member->GetMaxPower(POWER_MANA);
        if (member->GetPowerType() == POWER_MANA && maxMana
            && (100.0f * member->GetPower(POWER_MANA) / maxMana) < DcUtil::EffectiveRestMana(bot))
            acted = memberAi->DoSpecificAction("drink", event, true) || acted;
    };

    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            restMember(ref->getSource());
    }
    else
        restMember(bot);

    // Keep the tank and the rest of the party parked while another member is
    // eating or drinking.  This prevents the normal advance/pull strategies
    // from dragging the group into the next pack prematurely.
    if (!acted && !resting)
        return false; // No usable consumable: do not deadlock the route forever.
    SetDuration(1000);
    return acted || DcUtil::PartyNeedsRest(bot);
}

bool DungeonClearRegroupAction::Execute(Event& /*event*/)
{
    if (!DcUtil::PartyNeedsRegroup(bot) || bot->IsInCombat())
        return false;

    // Let followers close the gap before the tank commits to another anchor
    // or pack.  The normal follower strategy then supplies the movement; the
    // tank only needs to stop briefly, avoiding a permanent manual regroup.
    ai->StopMoving();
    SetDuration(1000);
    return true;
}

bool DungeonClearDisableOnClearedAction::Execute(Event& /*event*/)
{
    DcUtil::DisableDungeonClear(ai, bot, "all bosses cleared");
    return true;
}

bool DungeonClearRezPartyAction::Execute(Event& /*event*/)
{
    Group* g = bot->GetGroup();
    if (!g)
        return false;
    for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
    {
        Player* m = ref->getSource();
        if (!m || m->IsAlive() || m->GetMapId() != bot->GetMapId())
            continue;
        if (bot->GetDistance(m) > 30.0f)
            return MoveNear(m->GetMapId(), m->GetPositionX(), m->GetPositionY(), m->GetPositionZ());
        return DcUtil::CastRezOn(ai, bot, m);
    }
    return false;
}

bool DungeonClearFilterLootAction::Execute(Event& /*event*/)
{
    // Opening and item-by-item filtering stay in the stock loot pipeline.  It
    // reads the optional DungeonClear loot-quality value when the loot window
    // is actually available, so quest/upgrade/consumable exceptions remain
    // centralized in StoreLootAction.
    LootObjectStack* stack = AI_VALUE(LootObjectStack*, "available loot");
    if (!stack)
        return false;
    return ai->DoSpecificAction("loot", Event(), true);
}

bool DungeonClearPullAction::Execute(Event& event)
{
    uint8 mode = AI_VALUE(uint8&, DcKey::PullMode);
    Unit* target = DcUtil::FindHostileNear(bot, sDcSettings.trashEngageRange + 8.0f);
    if (!target)
        return false;

    // Size the pack around the prospective pull target.  Counting around the
    // tank made a distant five-mob pack look empty while the tank was scouting
    // it, defeating Dynamic's configured safety ceiling.
    uint32 const nearby = DcUtil::CountHostileNear(bot, target, 15.0f);

    if (mode == static_cast<uint8>(DcPullMode::Dynamic)
        && nearby > DcUtil::EffectivePullMax(bot))
    {
        DcRunState& state = AI_VALUE(DcRunState&, DcKey::RunState);
        state.paused = true;
        state.pauseReason = "dynamic pull blocked: " + std::to_string(nearby) + " hostiles nearby";
        DcUtil::TellGroup(ai, bot, "Dynamic pull paused: too many nearby hostiles.");
        DcAddonComm::PushStatus(ai, bot);
        return true;
    }

    // Leeroy deliberately charges.  Dynamic charges only when the local pack
    // is within the configured safe ceiling; Advanced uses the class-specific
    // pull strategy (ranged pull, judgement, faerie fire, etc.) and falls
    // back to a normal attack when that strategy is unavailable.
    bool const leeroy = mode == static_cast<uint8>(DcPullMode::Leeroy)
        || (mode == static_cast<uint8>(DcPullMode::Dynamic)
            && nearby <= DcUtil::EffectivePullMax(bot));
    if (!leeroy && !target->IsInCombat())
    {
        SET_AI_VALUE(Unit*, "pull target", target);
        if (ai->DoSpecificAction("pull action", event, true))
            return true;
    }

    if (mode == static_cast<uint8>(DcPullMode::Leeroy)
        || mode == static_cast<uint8>(DcPullMode::Dynamic)
        || mode == static_cast<uint8>(DcPullMode::Advanced))
        return Attack(event.getOwner() ? event.getOwner() : bot, target);

    return false;
}
