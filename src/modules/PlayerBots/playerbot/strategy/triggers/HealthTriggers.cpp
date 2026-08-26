
#include "playerbot/playerbot.h"
#include "HealthTriggers.h"

using namespace ai;

float HealthInRangeTrigger::GetValue()
{
    return AI_VALUE2(uint8, "health", GetTargetName());
}

bool HealthInRangeTrigger::IsActive()
{
    bool healthCheck = false;
    if (ai->HasStrategy("preheal", BotState::BOT_STATE_COMBAT))
    {
        Unit* target = GetTarget();
        if (target)
        {
            uint32 incDmg = AI_VALUE2(uint32, "incoming damage", GetTargetName());
            if (incDmg)
            {
                float healthPercent = GetValue();
                float incDmgPercent = float(float(incDmg) / float(target->GetMaxHealth())) * 100;
                float healthPredict = healthPercent;
                if (incDmgPercent >= healthPercent)
                    healthPredict = 0.f;
                else
                    healthPredict = healthPercent - incDmgPercent;

                healthCheck = healthPredict < maxValue && healthPredict >= minValue;
                if (healthCheck && ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
                {
                    std::string msg = GetTargetName() + " hp: " + std::to_string(healthPercent) + ", predicted: " + std::to_string(healthPredict);
                    ai->TellPlayerNoFacing(GetMaster(), msg);
                }
            }
            else
                healthCheck = ValueInRangeTrigger::IsActive();
        }
    }
    else
        healthCheck = ValueInRangeTrigger::IsActive();

    return healthCheck
        && !AI_VALUE2(bool, "dead", GetTargetName())
        && (!isTankRequired || (GetTarget()->IsPlayer() && ai->IsTank((Player*)GetTarget(), false)));
}

// This is the top-up tier - a party member between mediumHealth and
// almostFullHealth, so between 70 and 90 percent by default. It is the tier that
// fires most often by a wide margin, and a heal cast at 85 percent costs the same
// mana as the one that saves somebody at 20. Once the healer's own mana is no
// longer comfortable, that trade stops being worth making: skip the tier and keep
// what is left for the ones below it, which are deliberately untouched and will
// still fire at any mana level.
bool PartyMemberAlmostFullHealthTrigger::IsActive()
{
    if (AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.mediumMana)
        return false;

    return PartyMemberLowHealthTrigger::IsActive();
}

// Medium band (default 50-70%). At low mana skip the expensive mid-tier heals
// and leave GCDs for low/critical casts that keep people alive.
bool PartyMemberMediumHealthTrigger::IsActive()
{
    if (AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.lowMana)
        return false;

    return PartyMemberLowHealthTrigger::IsActive();
}

bool PartyMemberDeadTrigger::IsActive()
{
	return GetTarget();
}

bool DeadTrigger::IsActive()
{
    return AI_VALUE2(bool, "dead", GetTargetName());
}

bool AoeHealTrigger::IsActive()
{
    return AI_VALUE2(uint8, "aoe heal", type) >= count;
}

bool HealTargetFullHealthTrigger::IsActive()
{
    Spell* currentSpell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (currentSpell && (currentSpell->getState() == SPELL_STATE_CASTING) && (currentSpell->GetCastedTime() > 0U))
    {
        // Interrupt pre casted heals if target is not injured.
        if (PlayerbotAI::IsHealSpell(currentSpell->m_spellInfo))
        {
            std::string status = "fullhp";
            if (Unit* pTarget = currentSpell->m_targets.getUnitTarget())
            {
                bool hpFull = pTarget->GetHealth() == pTarget->GetMaxHealth();
                if (!hpFull && (pTarget->GetHealthPercent() > 90.f))
                {
                    uint32 healValue = currentSpell->GetDamage();
                    uint32 needHeal = pTarget->GetMaxHealth() - pTarget->GetHealth();
                    if (healValue > needHeal && float((needHeal * 100.0f) / healValue) < 50.0f)
                    {
                        status = "almost fullhp";
                        hpFull = true;
                    }
                }
                if (hpFull)
                {
                    uint32 manaCost = currentSpell->GetPowerCost();
                    if (ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
                    {
                        std::string msg = "target " + status + ", can save " + std::to_string(manaCost) + " mana, cast left : " + std::to_string(currentSpell->GetCastedTime()) + "ms";
                        ai->TellPlayerNoFacing(GetMaster(), msg);
                    }
                    return true;
                }
            }
        }
    }
    return false;
}
