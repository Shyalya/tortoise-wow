
#include "playerbot/playerbot.h"
#include "CcTargetValue.h"
#include "GroupCcTargetReservation.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Action.h"

using namespace ai;

namespace
{
    bool IsBossCcTarget(Unit* creature)
    {
        if (Creature* boss = creature->ToCreature())
            return boss->IsWorldBoss() || boss->HasExtraFlag(CREATURE_FLAG_EXTRA_INSTANCE_BIND);

        return false;
    }

    bool IsAlreadyControlled(Unit* creature)
    {
        return creature->HasBreakableByDamageCrowdControlAura() ||
               creature->HasAuraType(SPELL_AURA_MOD_FEAR) ||
               creature->HasAuraType(SPELL_AURA_MOD_ROOT) ||
               creature->HasAuraType(SPELL_AURA_MOD_STUN) ||
               creature->HasAuraType(SPELL_AURA_MOD_CHARM) ||
               creature->HasAuraType(SPELL_AURA_MOD_POSSESS) ||
               creature->HasAuraType(SPELL_AURA_MOD_PACIFY) ||
               creature->HasAuraType(SPELL_AURA_MOD_PACIFY_SILENCE);
    }

    bool IsCurrentTankTarget(PlayerbotAI* ai, Unit* creature)
    {
        Player* victim = dynamic_cast<Player*>(creature->GetVictim());
        return victim && ai->IsTank(victim);
    }
}

class FindTargetForCcStrategy : public FindTargetStrategy
{
public:
    FindTargetForCcStrategy(PlayerbotAI* ai, std::string spell) : FindTargetStrategy(ai)
    {
        this->spell = spell;
        maxDistance = 0;
        rtiLocked = false;
        reservationLocked = false;

        AiObjectContext* context = ai->GetAiObjectContext();
        rtiCcTarget = AI_VALUE(Unit*, "rti cc target");
        rtiTarget = AI_VALUE(Unit*, "rti target");
        currentTarget = AI_VALUE(Unit*, "current target");
    }

    Unit* GetRtiCcTarget() const { return rtiCcTarget; }

public:
    virtual void CheckAttacker(Unit* creature, ThreatManager* /*threatManager*/)
    {
        Player* bot = ai->GetBot();
        AiObjectContext* context = ai->GetAiObjectContext();

        if (rtiLocked)
            return;

        if (rtiCcTarget == creature)
        {
            result = ai->CanCastSpell(spell, creature, true, nullptr, false, true) ? creature : nullptr;
            rtiLocked = true;
            return;
        }

        if (reservationLocked)
            return;

        ObjectGuid creatureGuid = creature->GetObjectGuid();
        // Keep a live in-flight claim even if a fresh cast is no longer legal,
        // so later selection cannot drop it while the aura is still pending.
        if (GroupCcTargetReservation::IsInFlight(bot, creatureGuid))
        {
            result = creature;
            reservationLocked = true;
            return;
        }

        if (!ai->CanCastSpell(spell, creature, true, nullptr, false, true))
            return;

        if (currentTarget == creature)
            return;

        if (rtiTarget == creature)
            return;

        if (IsBossCcTarget(creature) || IsCurrentTankTarget(ai, creature) || IsAlreadyControlled(creature))
            return;

        // Ownership outranks skip for a still-valid selection claim. Safety
        // gates above still apply so a stale non-in-flight claim can be freed.
        if (!GroupCcTargetReservation::IsOwnedBy(bot, creatureGuid) &&
            (GroupCcTargetReservation::IsSkipped(bot, creatureGuid) ||
             GroupCcTargetReservation::IsClaimedByOther(bot, creatureGuid)))
            return;

        uint8 health = creature->GetHealthPercent();
        if (health < sPlayerbotAIConfig.mediumHealth)
            return;

        float minDistance = ai->GetRange("spell");
        Group* group = bot->GetGroup();
        if (!group)
            return;

        if (AI_VALUE(uint8,"aoe count") > 2)
        {
            WorldLocation aoe = AI_VALUE(WorldLocation,"aoe position");
            if (sServerFacade.IsDistanceLessOrEqualThan(sServerFacade.GetDistance2d(creature, aoe.coord_x, aoe.coord_y), sPlayerbotAIConfig.aoeRadius))
                return;
        }

        if (creature->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) && !(spell == "fear" || spell == "banish"))
            return;

        if (GroupCcTargetReservation::IsOwnedBy(bot, creatureGuid))
        {
            result = creature;
            reservationLocked = true;
            return;
        }

        if (!creature->IsPlayer())
        {
            int tankCount, dpsCount;
            GetPlayerCount(creature, &tankCount, &dpsCount);

            // Prefer free adds, but don't repeatedly CC something the current tank
            // is already holding by themselves.
            if (tankCount && !dpsCount)
                return;

            if (!tankCount && !dpsCount && !result)
            {
                result = creature;
                maxDistance = minDistance;
            }
        }

        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player *member = sObjectMgr.GetPlayer(itr->guid);
            if(!member || !sServerFacade.IsAlive(member) || member == bot || bot->GetMapId() != member->GetMapId())
                continue;

            if (!ai->IsTank(member))
                continue;

            float distance = sServerFacade.GetDistance2d(member, creature);
            if (distance < minDistance)
                minDistance = distance;
        }

        if ((!result && !creature->IsPlayer()) || minDistance > maxDistance)
        {
            result = creature;
            maxDistance = minDistance;
        }
    }

private:
    std::string spell;
    float maxDistance;
    bool rtiLocked;
    bool reservationLocked;
    Unit* rtiCcTarget;
    Unit* rtiTarget;
    Unit* currentTarget;
};

Unit* CcTargetValue::Calculate()
{
    std::list<ObjectGuid> possible = AI_VALUE(std::list<ObjectGuid>,"possible targets no los");

    for (std::list<ObjectGuid>::iterator i = possible.begin(); i != possible.end(); ++i)
    {
        ObjectGuid guid = *i;
        Unit* add = ai->GetUnit(guid);
        if (!add)
            continue;

        if (!ai->IsSafe(add))
            continue;

        if (ai->HasMyAura(qualifier, add) ||
            (qualifier == "polymorph" && (ai->HasMyAura("polymorph: pig", add) || ai->HasMyAura("polymorph: turtle", add))))
        {
            Player* bot = ai->GetBot();
            ObjectGuid ownedGuid = GroupCcTargetReservation::GetOwnedTarget(bot);
            if (!ownedGuid.IsEmpty())
                GroupCcTargetReservation::Release(bot, ownedGuid);
            return NULL;
        }
    }

    Player* bot = ai->GetBot();
    ObjectGuid ownedGuid = GroupCcTargetReservation::GetOwnedTarget(bot);
    if (!ownedGuid.IsEmpty())
    {
        Unit* owned = ai->GetUnit(ownedGuid);
        if (!owned || !sServerFacade.IsAlive(owned) || !ai->IsSafe(owned) || IsAlreadyControlled(owned))
            GroupCcTargetReservation::Release(bot, ownedGuid);
    }

    FindTargetForCcStrategy strategy(ai, qualifier);
    Unit* selected = FindTarget(&strategy);
    Unit* rtiCcTarget = strategy.GetRtiCcTarget();
    ownedGuid = GroupCcTargetReservation::GetOwnedTarget(bot);

    if (rtiCcTarget)
    {
        if (!ownedGuid.IsEmpty())
            GroupCcTargetReservation::Release(bot, ownedGuid);
        return selected == rtiCcTarget ? selected : nullptr;
    }

    // Selection uses a hard 3s lease. Do not refresh or reacquire here;
    // PrepareFallbackCast reacquires at real cast time if the add is free.
    if (selected)
        GroupCcTargetReservation::Claim(bot, selected->GetObjectGuid());
    else if (!ownedGuid.IsEmpty() && !GroupCcTargetReservation::IsInFlight(bot, ownedGuid))
        GroupCcTargetReservation::Release(bot, ownedGuid);

    ObjectGuid liveOwned = GroupCcTargetReservation::GetOwnedTarget(bot);
    if (!liveOwned.IsEmpty() && (!selected || selected->GetObjectGuid() != liveOwned))
    {
        Unit* owned = ai->GetUnit(liveOwned);
        if (owned)
            return owned;
    }

    return selected;
}
