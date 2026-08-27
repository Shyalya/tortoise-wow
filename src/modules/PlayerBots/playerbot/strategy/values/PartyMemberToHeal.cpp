
#include "playerbot/playerbot.h"
#include "PartyMemberToHeal.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

class IsTargetOfHealingSpell : public SpellEntryPredicate
{
public:
    virtual bool Check(SpellEntry const* spell) 
    {
        return PlayerbotAI::IsHealSpell(spell);
    }
};

uint32 getIncomingdamage(Unit const* pTarget)
{
    uint32 damage = 0;
    for (auto const& pAttacker : pTarget->getAttackers())
        if (pAttacker->CanReachWithMeleeAttack(pTarget))
            damage += uint32((pAttacker->GetFloatValue(UNIT_FIELD_MINDAMAGE) + pAttacker->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2);

    return damage;
}

bool compareByHealth(const Unit *u1, const Unit *u2)
{
    return u1->GetHealthPercent() < u2->GetHealthPercent();
}

static float HealTriageScore(PlayerbotAI* ai, const Unit* unit, bool incomingDamage)
{
    uint32 hp = unit->GetHealth();
    if (incomingDamage)
    {
        uint32 incoming = getIncomingdamage(unit);
        if (incoming >= hp)
            hp = 0;
        else
            hp -= incoming;
    }

    uint32 const hpMax = unit->GetMaxHealth();
    if (!hpMax)
        return 0.0f;

    float missing = float(hpMax - hp);

    // Prefer tanks when injured: they take the most damage and drop the party if they die.
    if (unit->IsPlayer() && ai->IsTank((Player*)unit))
        missing *= 1.35f;

    return missing;
}

Unit* PartyMemberToHeal::Calculate()
{
    std::vector<Unit*> needHeals;
    if (bot->GetSelectionGuid())
    {
        Unit* target = ai->GetUnit(bot->GetSelectionGuid());
        if (target &&
            target->GetObjectGuid() != bot->GetObjectGuid() && 
            sServerFacade.IsFriendlyTo(bot, target) &&
            target->GetHealthPercent() < 100 && 
            Check(target))
        {
            needHeals.push_back(target);
        }
    }

    if (GuidPosition rpgTarget = AI_VALUE(GuidPosition, "rpg target"))
    {
        Unit* target = rpgTarget.GetCreature(bot->GetInstanceId());
        if (target && sServerFacade.IsFriendlyTo(bot, target) && target->GetHealthPercent() < 100)
        {
            needHeals.push_back(target);
        }
    }

    const std::vector<Player*> partyMembers = GetPartyMembers();
    if (partyMembers.empty() && needHeals.empty())
    {
        return nullptr;
    }

    if (!partyMembers.empty() || !needHeals.empty())
    {
        IsTargetOfHealingSpell predicate;
        bool const preHealing = ai->HasStrategy("preheal", BotState::BOT_STATE_COMBAT);
        for (Player* player : partyMembers)
        {
            if (!Check(player) || !sServerFacade.IsAlive(player))
            {
                continue;
            }

            // do not heal dueling members
            if (player->m_duel && player->m_duel->opponent)
            {
                continue;
            }

            uint32 incomingDamage = 0;
            if (preHealing)
                incomingDamage = getIncomingdamage(player);

            int32 effectiveHp = int32(player->GetHealth()) - int32(incomingDamage);
            if (effectiveHp < 0)
                effectiveHp = 0;
            uint8 health = uint8((float(effectiveHp) * 100.0f) / float(player->GetMaxHealth()));

            bool isTank = ai->IsTank(player);

            // Only queue people who actually need a heal. Full-health tanks used
            // to always enter the list and then steal healer index slots, leaving
            // injured DPS/healers unhealed while a second healer idled.
            if (health < sPlayerbotAIConfig.almostFullHealth && !IsTargetOfSpellCast(player, predicate))
            {
                needHeals.push_back(player);
            }
            else if (isTank && preHealing && incomingDamage > 0 && health < 100)
            {
                needHeals.push_back(player);
            }

            Pet* pet = player->GetPet();
            if (pet && CanHealPet(pet) && pet->GetHealthPercent() < sPlayerbotAIConfig.almostFullHealth)
            {
                needHeals.push_back(pet);
            }
        }
    }

    if (needHeals.empty())
    {
        return nullptr;
    }

    bool preHealing = ai->HasStrategy("preheal", BotState::BOT_STATE_COMBAT);
    PlayerbotAI* healAi = ai;
    sort(needHeals.begin(), needHeals.end(), [healAi, preHealing](const Unit* u1, const Unit* u2)
    {
        return HealTriageScore(healAi, u1, preHealing) > HealTriageScore(healAi, u2, preHealing);
    });

    int healerIndex = 0;
    if (!partyMembers.empty())
    {
        for (Player* player : partyMembers)
        {
            if (!ai->IsSafe(player))
            {
                continue;
            }
            else if (player == bot)
            {
                break;
            }
            else if (ai->IsHeal(player) && player->GetPlayerbotAI())
            {
                float percent = (float)player->GetPower(POWER_MANA) / (float)player->GetMaxPower(POWER_MANA) * 100.0;
                if (percent > sPlayerbotAIConfig.lowMana)
                {
                    healerIndex++;
                }
            }
        }
    }
    else
    {
        healerIndex = 1;
    }

    healerIndex = healerIndex % needHeals.size();

    // Spreading the targets over several healers is only worth doing while there
    // is more than one worth spreading. With a single injured member the second
    // healer's index used to land on somebody at full health. Fall back to the
    // worst-off target.
    Unit* chosen = needHeals[healerIndex];
    if (chosen && chosen->GetHealthPercent() >= sPlayerbotAIConfig.almostFullHealth)
        chosen = needHeals[0];

    return chosen;
}

bool PartyMemberToHeal::CanHealPet(Pet* pet)
{
    return MINI_PET != pet->getPetType();
}

bool PartyMemberToHeal::Check(Unit* player)
{
    bool isBg = bot->InBattleGround();

    // Battlegrounds get their own figure rather than a blanket halving: the two
    // situations want different distances, and halving whatever the open-world
    // value happens to be ties them together for no reason.
    float maxDist = isBg ? sPlayerbotAIConfig.healDistanceBg : ai->GetRange("heal");

    if (!player)
        return false;

    if (player->GetObjectGuid() == bot->GetObjectGuid())
        return false;

    if (player->GetMapId() != bot->GetMapId())
        return false;

    if (!player->IsInWorld())
        return false;
                                                     
    if (sServerFacade.GetDistance2d(bot, player) > maxDist)
        return false;

    return true;
}

std::vector<Player*> PartyMemberToHeal::GetPartyMembers()
{
    std::vector<Player*> partyMembers;
    if (ai->HasStrategy("focus heal targets", BotState::BOT_STATE_COMBAT))
    {
        Unit* player = nullptr;
        const std::list<ObjectGuid> focusHealTargets = AI_VALUE(std::list<ObjectGuid>, "focus heal targets");
        for(const ObjectGuid& focusHealTarget : focusHealTargets)
        {
            Player* player = (Player*)ai->GetUnit(focusHealTarget);
            if (player && player->IsInGroup(bot) && ai->IsSafe(player))
            {
                partyMembers.push_back(player);
            }
        }
    }
    else
    {
        Group* group = bot->GetGroup();
        if (group)
        {
            for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
            {
                Player* player = gref->getSource();
                if (player && ai->IsSafe(player))
                {
                    partyMembers.push_back(player);
                }
            }
        }
    }

    return partyMembers;
}

Unit* PartyMemberToProtect::Calculate()
{
    Group* group = bot->GetGroup();
    if (!group)
        return NULL;

    std::vector<Unit*> needProtect;

    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit)
            continue;

        bool isRanged = false;
        if (unit->AI())
        {
            if (unit->AI()->IsRangedUnit())
                isRanged = true;
        }

        Unit* pVictim = unit->GetVictim();
        if (!pVictim || !pVictim->IsPlayer())
            continue;

        if (pVictim == bot)
            continue;

        if (sServerFacade.GetDistance2d(pVictim, bot) > 30.0f)
            continue;

        float attackDistance = isRanged ? 30.0f : 10.0f;
        if (sServerFacade.GetDistance2d(pVictim, unit) > attackDistance)
            continue;

        if (ai->IsTank((Player*)pVictim) && pVictim->GetHealthPercent() > 10)
            continue;
        else if (pVictim->GetHealthPercent() > 30)
            continue;

        if (find(needProtect.begin(), needProtect.end(), pVictim) == needProtect.end())
        needProtect.push_back(pVictim);
    }

    if (needProtect.empty())
        return NULL;

    sort(needProtect.begin(), needProtect.end(), compareByHealth);

    return needProtect[0];
}

Unit* PartyMemberToRemoveRoots::Calculate()
{
    Unit* target = nullptr;
    Group* group = bot->GetGroup();
    if(group)
    {
        for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player* player = gref->getSource();
            if (sServerFacade.IsAlive(player))
            {
                if (player->m_duel && player->m_duel->opponent)
                    continue;

                if (player->HasAuraType(SPELL_AURA_MOD_ROOT) || player->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED))
                {
                    if (!ai->HasAura("stealth", player) && !ai->HasAura("prowl", player))
                    {
                        target = player;
                        break;
                    }
                }
            }
        }
    }

    return target;
}
