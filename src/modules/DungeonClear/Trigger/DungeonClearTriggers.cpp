#include "Trigger/DungeonClearTriggers.h"
#include "playerbot/playerbot.h"
#include "Util/DungeonClearUtil.h"
#include "Settings/DcSettings.h"
#include "Data/DungeonEventRegistry.h"
#include "Data/DungeonBossInfo.h"
#include "DcValueKeys.h"
#include "DcRunState.h"
#include "Group/Group.h"
#include "Maps/Map.h"
#include <optional>

using namespace ai;

bool DungeonClearEnabledTrigger::LeaderEnabledUnpaused() const
{
    if (!sDcSettings.moduleEnabled)
        return false;
    DcRunState* st = DcUtil::LeaderRunState(bot);
    return st && st->enabled && !st->paused;
}

bool DungeonClearPartyDiedTrigger::IsActive()
{
    if (!LeaderEnabledUnpaused())
        return false;
    if (!DcUtil::IsDungeonClearLeader(ai, bot))
        return false;
    Group* g = bot->GetGroup();
    if (!g)
        return bot->IsDead();
    bool anyAlive = false;
    bool anyDead = false;
    for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
    {
        Player* m = ref->getSource();
        if (!m || m->GetMapId() != bot->GetMapId())
            continue;
        if (m->IsAlive())
            anyAlive = true;
        else
            anyDead = true;
    }
    return anyDead && !anyAlive; // full wipe
}

bool DungeonClearAllClearedTrigger::IsActive()
{
    if (!LeaderEnabledUnpaused() || !DcUtil::IsDungeonClearLeader(ai, bot))
        return false;
    auto next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    return !next.has_value();
}

bool DungeonClearAtBossTrigger::IsActive()
{
    if (!LeaderEnabledUnpaused() || !DcUtil::IsDungeonClearLeader(ai, bot))
        return false;
    auto next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next || next->kind != DungeonAnchorKind::Boss)
        return false;
    float d = bot->GetDistance(next->x, next->y, next->z);
    return d <= sDcSettings.engageRange;
}

bool DungeonClearAtObjectiveTrigger::IsActive()
{
    if (!LeaderEnabledUnpaused() || !DcUtil::IsDungeonClearLeader(ai, bot))
        return false;
    auto next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next || next->kind != DungeonAnchorKind::Objective)
        return false;
    float radius = next->arriveRadius > 0 ? next->arriveRadius : sDcSettings.objectiveArriveRadius;
    return bot->GetDistance(next->x, next->y, next->z) <= radius;
}

bool DungeonClearBlockingTrashTrigger::IsActive()
{
    if (!LeaderEnabledUnpaused() || !DcUtil::IsDungeonClearLeader(ai, bot))
        return false;
    return DcUtil::FindHostileNear(bot, sDcSettings.trashEngageRange) != nullptr;
}

bool DungeonClearNeedAdvanceTrigger::IsActive()
{
    if (!LeaderEnabledUnpaused() || !DcUtil::IsDungeonClearLeader(ai, bot))
        return false;
    auto next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next)
        return false;
    float d = bot->GetDistance(next->x, next->y, next->z);
    float arrive = next->kind == DungeonAnchorKind::Objective
        ? (next->arriveRadius > 0 ? next->arriveRadius : sDcSettings.objectiveArriveRadius)
        : sDcSettings.advanceArriveRange;
    return d > arrive;
}

bool DungeonClearFollowTankTrigger::IsActive()
{
    if (!DcUtil::IsEnabledRun(bot) || DcUtil::IsPausedRun(bot))
        return false;
    if (DcUtil::IsDungeonClearLeader(ai, bot))
        return false;
    ObjectGuid tankGuid = AI_VALUE(ObjectGuid, DcKey::PartyTank);
    if (!tankGuid)
        return false;
    return tankGuid != bot->GetObjectGuid();
}

bool DungeonClearAssistTankTrigger::IsActive()
{
    if (!DcUtil::IsEnabledRun(bot))
        return false;
    if (DcUtil::IsDungeonClearLeader(ai, bot))
        return false;
    Player* tank = DcUtil::FindEnabledTank(bot);
    return tank && tank->IsInCombat();
}

bool DungeonClearPullTrigger::IsActive()
{
    if (!LeaderEnabledUnpaused() || !DcUtil::IsDungeonClearLeader(ai, bot))
        return false;
    if (bot->IsInCombat())
        return false;
    auto next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (next && next->kind == DungeonAnchorKind::Boss
        && bot->GetDistance(next->x, next->y, next->z) <= sDcSettings.engageRange)
        return false; // let engage-boss own it
    return DcUtil::FindHostileNear(bot, sDcSettings.trashEngageRange + 5.0f) != nullptr;
}

bool DungeonClearEventDueTrigger::IsActive()
{
    if (!LeaderEnabledUnpaused() || !DcUtil::IsDungeonClearLeader(ai, bot))
        return false;
    Map* map = bot->GetMap();
    if (!map || (!map->IsDungeon() && !map->IsRaid()))
        return false;
    auto next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next)
        return false;
    DungeonEvent const* event = nullptr;
    if (next->kind == DungeonAnchorKind::Objective && next->eventId)
    {
        event = DungeonEventRegistry::Instance().FindEvent(map->GetId(), next->eventId);
        DcRunState& runState = AI_VALUE(DcRunState&, DcKey::RunState);
        if (event && event->persistent && runState.eventId == event->eventId
            && runState.eventInstanceId == map->GetInstanceId())
            return true;
        return event && bot->GetDistance(next->x, next->y, next->z)
            <= (next->arriveRadius > 0 ? next->arriveRadius : sDcSettings.objectiveArriveRadius) + 5.0f;
    }

    event = DungeonEventRegistry::Instance().FindDueEvent(bot, context, map->GetId(), next->encounterIndex);
    if (!event)
        return false;
    if (event->conditional)
        return true;
    return bot->GetDistance(next->x, next->y, next->z) <= sDcSettings.engageRange + 10.0f;
}

bool DungeonClearRezPartyTrigger::IsActive()
{
    if (!sDcSettings.postCombatRez || !LeaderEnabledUnpaused())
        return false;
    if (bot->IsInCombat() || !bot->IsAlive())
        return false;
    Group* g = bot->GetGroup();
    if (!g)
        return false;
    bool canRez = bot->getClass() == CLASS_PRIEST || bot->getClass() == CLASS_PALADIN
        || bot->getClass() == CLASS_SHAMAN || bot->getClass() == CLASS_DRUID;
    if (!canRez)
        return false;
    for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
    {
        Player* m = ref->getSource();
        if (m && !m->IsAlive() && m->GetMapId() == bot->GetMapId())
            return true;
    }
    return false;
}

bool DungeonClearFilterLootTrigger::IsActive()
{
    if (!LeaderEnabledUnpaused())
        return false;
    return AI_VALUE(bool, DcKey::Stock::HasAvailableLoot);
}
