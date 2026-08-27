
#include "playerbot/playerbot.h"
#include "CcTargetValue.h"
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
    }

public:
    virtual void CheckAttacker(Unit* creature, ThreatManager* threatManager)
    {
        Player* bot = ai->GetBot();
        AiObjectContext* context = ai->GetAiObjectContext();

        if (!ai->CanCastSpell(spell, creature, true, nullptr, false, true))
            return;

        if (AI_VALUE(Unit*,"rti cc target") == creature)
        {
            result = creature;
            return;
        }

        if (AI_VALUE(Unit*,"current target") == creature)
            return;

        if (AI_VALUE(Unit*,"rti target") == creature)
            return;

        if (IsBossCcTarget(creature) || IsCurrentTankTarget(ai, creature) || IsAlreadyControlled(creature))
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

        if (ai->HasMyAura(qualifier, add))
            return NULL;

        if (qualifier == "polymorph")
        {
            if (ai->HasMyAura("polymorph: pig", add))
                return NULL;
            if (ai->HasMyAura("polymorph: turtle", add))
                return NULL;
        }
    }

    FindTargetForCcStrategy strategy(ai, qualifier);
    return FindTarget(&strategy);
}
