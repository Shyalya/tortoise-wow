#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonActions.h"
#include "playerbot/strategy/values/RtiTargetValue.h"

#include <algorithm>
#include <cfloat>
#include <vector>

using namespace ai;
using namespace MoltenCoreHelpers;

static constexpr float LIVING_BOMB_DISTANCE = 20.0f;
static constexpr float INFERNO_DISTANCE = 20.0f;
static constexpr float ARCANE_EXPLOSION_DISTANCE = 26.0f;
static constexpr float GARR_ADD_TANK_RAID_DISTANCE = 30.0f;
static constexpr float GARR_SEPARATION_ANXIETY_SAFE = 38.0f;
static constexpr float GARR_ADD_TANK_STEP_DISTANCE = 5.0f;
static constexpr float GOLEMAGG_TANK_X = 795.7308f;
static constexpr float GOLEMAGG_TANK_Y = -994.8848f;
static constexpr float CORE_RAGER_TANK_X = 846.6453f;
static constexpr float CORE_RAGER_TANK_Y = -1019.0639f;
static constexpr float GOLEMAGGS_TRUST_DISTANCE = 30.0f;
static constexpr float CORE_RAGER_STEP_DISTANCE = 5.0f;

static bool MarkWithSkull(Player* bot, Unit* target)
{
    Group* group = bot->GetGroup();
    if (!group || !target)
        return false;

    int index = RtiTargetValue::GetRtiIndex("skull");
    ObjectGuid currentGuid = group->GetTargetIcon(index);
    if (currentGuid == target->GetObjectGuid())
        return false;

#ifndef MANGOSBOT_TWO
    group->SetTargetIcon(index, target->GetObjectGuid());
#else
    group->SetTargetIcon(index, bot->GetObjectGuid(), target->GetObjectGuid());
#endif
    return true;
}

static void SetSkullRti(PlayerbotAI* ai)
{
    ai->GetAiObjectContext()->GetValue<std::string>("rti")->Set("skull");
}

static bool TryTaunt(PlayerbotAI* ai, Event& event)
{
    switch (ai->GetBot()->getClass())
    {
        case CLASS_WARRIOR:
            return ai->DoSpecificAction("taunt", event, true);
        case CLASS_DRUID:
            return ai->DoSpecificAction("growl", event, true);
        default:
            return false;
    }
}

static std::vector<Unit*> FindAliveFiresworns(PlayerbotAI* ai)
{
    std::vector<Unit*> firesworns;
    std::list<ObjectGuid> const& possible =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible targets no los")->Get();
    for (ObjectGuid const& guid : possible)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == NPC_FIRESWORN)
            firesworns.push_back(unit);
    }
    return firesworns;
}

static float GetClosestOtherPlayerDistance(Player* bot)
{
    float closestDist = FLT_MAX;
    Group* group = bot->GetGroup();
    if (!group)
        return closestDist;

    uint32 mapId = bot->GetMapId();
    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* player = gref->getSource();
        if (!player || player == bot || !player->IsAlive() || player->GetMapId() != mapId)
            continue;

        float dist = bot->GetDistance2d(player);
        if (dist < closestDist)
            closestDist = dist;
    }

    return closestDist;
}

bool McMoveFromGroupAction::Execute(Event& /*event*/)
{
    return MoveFromGroup(LIVING_BOMB_DISTANCE);
}

bool McMoveFromBaronGeddonAction::Execute(Event& /*event*/)
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "baron geddon"))
    {
        float distToTravel = INFERNO_DISTANCE - bot->GetDistance2d(boss);
        if (distToTravel > 0)
        {
            bot->AttackStop(true);
            bot->InterruptNonMeleeSpells(false);
            return MoveAway(boss, distToTravel);
        }
    }
    return false;
}

bool McShazzrahMoveAwayAction::Execute(Event& /*event*/)
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "shazzrah"))
    {
        float distToTravel = ARCANE_EXPLOSION_DISTANCE - bot->GetDistance2d(boss);
        if (distToTravel > 0)
            return MoveAway(boss, distToTravel);
    }
    return false;
}

bool McGarrMarkBossAction::Execute(Event& /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "garr");
    if (!boss)
        return false;

    SetSkullRti(ai);
    return MarkWithSkull(bot, boss);
}

bool McGarrMainTankAttackGarrAction::Execute(Event& event)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "garr");
    if (!boss)
        return false;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    if (bot->GetVictim() != boss)
        return Attack(requester, boss);

    if (boss->GetVictim() != bot)
        return TryTaunt(ai, event);

    return false;
}

bool McGarrAssistTankAttackFireswornAction::Execute(Event& event)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "garr");
    if (!boss)
        return false;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    if (!PlayerbotAI::IsAssistTankOfIndex(bot, 0, true))
        return Attack(requester, boss);

    std::vector<Unit*> firesworns = FindAliveFiresworns(ai);
    if (firesworns.empty())
        return Attack(requester, boss);

    uint32 looseAdds = 0;
    Unit* firstLooseAdd = nullptr;
    for (Unit* add : firesworns)
    {
        if (add->GetVictim() != bot)
        {
            ++looseAdds;
            if (!firstLooseAdd)
                firstLooseAdd = add;
        }
    }

    if (looseAdds >= 2)
    {
        if (ai->DoSpecificAction("challenging shout", event, true))
            return true;
        if (ai->DoSpecificAction("challenging roar", event, true))
            return true;
    }

    if (firstLooseAdd)
    {
        if (bot->GetVictim() != firstLooseAdd)
            return Attack(requester, firstLooseAdd);
        return TryTaunt(ai, event);
    }

    Unit* currentVictim = bot->GetVictim();
    if (!currentVictim || currentVictim->GetEntry() != NPC_FIRESWORN || !currentVictim->IsAlive())
        return Attack(requester, firesworns.front());

    float distToBoss = bot->GetDistance2d(boss);
    if (distToBoss >= GARR_SEPARATION_ANXIETY_SAFE)
        return false;

    float closestPlayerDist = GetClosestOtherPlayerDistance(bot);
    if (closestPlayerDist < GARR_ADD_TANK_RAID_DISTANCE)
    {
        float step = std::min(GARR_ADD_TANK_STEP_DISTANCE, GARR_SEPARATION_ANXIETY_SAFE - distToBoss);
        if (step > 0.5f)
            return MoveAway(boss, step);
    }

    return false;
}

bool McGarrDpsAttackGarrAction::Execute(Event& event)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "garr");
    if (!boss)
        return false;

    SetSkullRti(ai);

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (bot->GetVictim() == boss && currentTarget == boss)
        return false;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, boss);
}

Unit* McGarrMarkFireswornAction::GetTarget()
{
    Unit* lowestHealthAdd = nullptr;
    for (Unit* unit : FindAliveFiresworns(ai))
    {
        if (!lowestHealthAdd || unit->GetHealth() < lowestHealthAdd->GetHealth())
            lowestHealthAdd = unit;
    }

    if (!lowestHealthAdd)
        return nullptr;

    Group* group = bot->GetGroup();
    int skullIndex = RtiTargetValue::GetRtiIndex("skull");
    ObjectGuid currentSkullGuid = group ? group->GetTargetIcon(skullIndex) : ObjectGuid();
    if (currentSkullGuid && currentSkullGuid != lowestHealthAdd->GetObjectGuid())
    {
        if (Unit* currentSkullUnit = ai->GetUnit(currentSkullGuid))
            if (currentSkullUnit->IsAlive() && currentSkullUnit->GetEntry() == NPC_FIRESWORN)
                return nullptr;
    }

    if (!currentSkullGuid || currentSkullGuid != lowestHealthAdd->GetObjectGuid())
        return lowestHealthAdd;

    return nullptr;
}

bool McGarrMarkFireswornAction::Execute(Event& /*event*/)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    SetSkullRti(ai);
    return MarkWithSkull(bot, target);
}

bool McGolemaggMarkBossAction::Execute(Event& /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "golemagg the incinerator");
    if (!boss)
        return false;

    SetSkullRti(ai);
    return MarkWithSkull(bot, boss);
}

bool McGolemaggTankAction::MoveUnitToPosition(Unit* target, float tankX, float tankY, float maxDistance, float stepDistance)
{
    Player* requester = GetMaster();
    if (bot->GetVictim() != target)
        return Attack(requester, target);

    if (target->GetVictim() == bot)
    {
        float distanceToTankPosition = bot->GetDistance2d(tankX, tankY, SizeFactor::None);
        if (distanceToTankPosition > maxDistance)
        {
            float dX = tankX - bot->GetPositionX();
            float dY = tankY - bot->GetPositionY();
            float dist = sqrt(dX * dX + dY * dY);
            if (dist < 0.1f)
                return false;
            float moveX = bot->GetPositionX() + (dX / dist) * stepDistance;
            float moveY = bot->GetPositionY() + (dY / dist) * stepDistance;
            float moveZ = bot->GetPositionZ();
            bot->UpdateAllowedPositionZ(moveX, moveY, moveZ);
            return MoveTo(bot->GetMapId(), moveX, moveY, moveZ, false, false, false, true);
        }
    }
    else
    {
        Event tauntEvent;
        if (TryTaunt(ai, tauntEvent))
            return true;
    }

    return false;
}

bool McGolemaggTankAction::FindCoreRagers(Unit*& coreRager1, Unit*& coreRager2) const
{
    coreRager1 = coreRager2 = nullptr;
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets no los"))
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == NPC_CORE_RAGER)
        {
            if (!coreRager1)
                coreRager1 = unit;
            else if (!coreRager2)
            {
                coreRager2 = unit;
                break;
            }
        }
    }
    return coreRager1 && coreRager2;
}

bool McGolemaggMainTankAttackGolemaggAction::Execute(Event& /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "golemagg the incinerator");
    if (!boss)
        return false;

    Unit* coreRager1 = nullptr;
    Unit* coreRager2 = nullptr;
    if (!FindCoreRagers(coreRager1, coreRager2))
        return false;

    if (ai->HasAura(SPELL_GOLEMAGGS_TRUST, coreRager1) || ai->HasAura(SPELL_GOLEMAGGS_TRUST, coreRager2))
        return MoveUnitToPosition(boss, GOLEMAGG_TANK_X, GOLEMAGG_TANK_Y, boss->GetCombatReach());

    return false;
}

bool McGolemaggAssistTankAttackCoreRagerAction::Execute(Event& event)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "golemagg the incinerator");
    if (!boss)
        return false;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    bool isFirstAssistTank = PlayerbotAI::IsAssistTankOfIndex(bot, 0, true);
    bool isSecondAssistTank = PlayerbotAI::IsAssistTankOfIndex(bot, 1, true);
    if (!isFirstAssistTank && !isSecondAssistTank)
        return Attack(requester, boss);

    Unit* coreRager1 = nullptr;
    Unit* coreRager2 = nullptr;
    if (!FindCoreRagers(coreRager1, coreRager2))
        return false;

    Unit* myCoreRager = isFirstAssistTank ? coreRager1 : coreRager2;
    Unit* otherCoreRager = isFirstAssistTank ? coreRager2 : coreRager1;

    if (myCoreRager->GetVictim() != bot)
    {
        if (bot->GetVictim() != myCoreRager)
            return Attack(requester, myCoreRager);
        return TryTaunt(ai, event);
    }

    Unit* otherCoreRagerVictim = otherCoreRager->GetVictim();
    if (otherCoreRagerVictim)
    {
        Player* otherVictimPlayer = otherCoreRagerVictim->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (otherVictimPlayer &&
            !PlayerbotAI::IsAssistTankOfIndex(otherVictimPlayer, 0, true) &&
            !PlayerbotAI::IsAssistTankOfIndex(otherVictimPlayer, 1, true))
        {
            if (bot->GetVictim() != otherCoreRager)
                return Attack(requester, otherCoreRager);
            return TryTaunt(ai, event);
        }
    }

    if (bot->GetVictim() != myCoreRager)
        return Attack(requester, myCoreRager);

    if (ai->HasAura(SPELL_GOLEMAGGS_TRUST, myCoreRager) ||
        (otherCoreRagerVictim == bot && ai->HasAura(SPELL_GOLEMAGGS_TRUST, otherCoreRager)))
    {
        float bossDistanceToCoreRagerTankPosition = boss->GetDistance2d(CORE_RAGER_TANK_X, CORE_RAGER_TANK_Y, SizeFactor::None);
        if (bossDistanceToCoreRagerTankPosition > GOLEMAGGS_TRUST_DISTANCE)
        {
            float distanceToTankPosition = bot->GetDistance2d(CORE_RAGER_TANK_X, CORE_RAGER_TANK_Y, SizeFactor::None);
            if (distanceToTankPosition > CORE_RAGER_STEP_DISTANCE)
                return MoveUnitToPosition(myCoreRager, CORE_RAGER_TANK_X, CORE_RAGER_TANK_Y, CORE_RAGER_STEP_DISTANCE);
        }

        return MoveAway(boss, CORE_RAGER_STEP_DISTANCE);
    }

    return false;
}

Unit* McCoreHoundMarkAction::GetTarget()
{
    Unit* highestHealthHound = nullptr;
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets no los"))
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == NPC_CORE_HOUND)
        {
            if (!highestHealthHound || unit->GetHealth() > highestHealthHound->GetHealth())
                highestHealthHound = unit;
        }
    }

    if (!highestHealthHound)
        return nullptr;

    Group* group = bot->GetGroup();
    int skullIndex = RtiTargetValue::GetRtiIndex("skull");
    ObjectGuid currentSkullGuid = group ? group->GetTargetIcon(skullIndex) : ObjectGuid();
    if (currentSkullGuid && currentSkullGuid != highestHealthHound->GetObjectGuid())
    {
        if (Unit* currentSkullUnit = ai->GetUnit(currentSkullGuid))
            if (currentSkullUnit->IsAlive() && highestHealthHound->GetHealth() <= currentSkullUnit->GetHealth() * 1.10f)
                return nullptr;
    }

    if (!currentSkullGuid || currentSkullGuid != highestHealthHound->GetObjectGuid())
        return highestHealthHound;

    return nullptr;
}

bool McCoreHoundMarkAction::Execute(Event& /*event*/)
{
    Unit* target = GetTarget();
    if (!target || !bot->GetGroup())
        return false;

    SetSkullRti(ai);
    return MarkWithSkull(bot, target);
}
