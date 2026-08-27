
#include "playerbot/playerbot.h"
#include "ConserveManaStrategy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/strategy/values/LastSpellCastValue.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

float ConserveManaMultiplier::GetValue(Action* action)
{
    if (action == NULL) return 1.0f;

    uint8 health = AI_VALUE2(uint8, "health", "self target");
    uint8 mana = AI_VALUE2(uint8, "mana", "self target");
    bool hasMana = AI_VALUE2(bool, "has mana", "self target");
    bool mediumMana = hasMana && mana < sPlayerbotAIConfig.mediumMana;
    bool lowMana = hasMana && mana < sPlayerbotAIConfig.lowMana;

    if (health < sPlayerbotAIConfig.lowHealth)
        return 1.0f;

    // Never throttle heals or emergency utility through mana conservation.
    // (HealerAutoSaveManaMultiplier handles inefficient heals separately.)
    if (dynamic_cast<CastHealingSpellAction*>(action))
        return 1.0f;

    CastSpellAction* spellAction = dynamic_cast<CastSpellAction*>(action);
    if (!spellAction)
        return 1.0f;

    std::string spell = spellAction->getName();
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    const SpellEntry* const spellInfo = sServerFacade.LookupSpellInfo(spellId);
    if (!spellInfo || spellInfo->powerType != POWER_MANA)
        return 1.0f;

    // Skip combat buffs once mana is no longer comfortable.
    if (mediumMana && dynamic_cast<CastBuffSpellAction*>(action))
        return 0.0f;

    // Healers: drop off-DPS when mana is low, or when someone actually needs a heal.
    if (ai->ContainsStrategy(STRATEGY_TYPE_HEAL) &&
        action->getThreatType() != ActionThreatType::ACTION_THREAT_NONE)
    {
        if (lowMana)
            return 0.0f;

        Unit* toHeal = AI_VALUE(Unit*, "party member to heal");
        if (toHeal && toHeal->GetHealthPercent() < sPlayerbotAIConfig.mediumHealth)
            return 0.0f;
    }

    Unit* target = AI_VALUE(Unit*, "current target");
    if (action->GetTarget() != target)
        return 1.0f;

    if (target)
    {
        if (((int)target->GetLevel() - (int)bot->GetLevel()) >= 0)
            return 1.0f;
    }

    return 1.0f;
}

float SaveManaMultiplier::GetValue(Action* action)
{
    if (action == NULL)
        return 1.0f;

    if (dynamic_cast<CastHealingSpellAction*>(action))
        return 1.0f;

    if (action->GetTarget() != AI_VALUE(Unit*, "current target"))
        return 1.0f;

    double saveLevel = AI_VALUE(double, "mana save level");
    if (saveLevel <= 1.0)
        return 1.0f;

    CastSpellAction* spellAction = dynamic_cast<CastSpellAction*>(action);
    if (!spellAction)
        return 1.0f;

    std::string spell = spellAction->getName();
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    const SpellEntry* const spellInfo = sServerFacade.LookupSpellInfo(spellId);
    if (!spellInfo || spellInfo->powerType != POWER_MANA)
        return 1.0f;

    int32 cost = spellInfo->manaCost;
    if (!cost)
        return 1.0f;

    time_t lastCastTime = AI_VALUE2(time_t, "last spell cast time", spell);
    if (!lastCastTime)
        return 1.0f;

    time_t elapsed = time(0) - lastCastTime;
    if ((double)elapsed < 10 * saveLevel)
        return 0.0f;

    return 1.0f;
}

float HealerAutoSaveManaMultiplier::GetValue(Action* action)
{
    if (!action || !ai->ContainsStrategy(STRATEGY_TYPE_HEAL))
        return 1.0f;

    if (!AI_VALUE2(bool, "has mana", "self target"))
        return 1.0f;

    uint8 mana = AI_VALUE2(uint8, "mana", "self target");
    if (mana > sPlayerbotAIConfig.saveManaThreshold)
        return 1.0f;

    CastHealingSpellAction* healingAction = dynamic_cast<CastHealingSpellAction*>(action);
    if (!healingAction)
        return 1.0f;

    Unit* target = healingAction->GetActionTarget();
    if (!target)
        return 1.0f;

    bool isTank = target->IsPlayer() && ai->IsTank((Player*)target);
    uint8 health = target->GetHealthPercent();
    HealingManaEfficiency manaEfficiency = healingAction->manaEfficiency;
    uint8 estAmount = healingAction->estAmount;
    uint8 lossAmount = health >= 100 ? 0 : (100 - health);

    if (isTank)
    {
        // Tanks have larger health pools; treat estimated heal as smaller relative hit.
        estAmount = uint8(float(estAmount) / 1.5f);
        if (health >= sPlayerbotAIConfig.mediumHealth &&
            (lossAmount < estAmount || manaEfficiency <= HealingManaEfficiency::MEDIUM))
            return 0.0f;
        if (health >= sPlayerbotAIConfig.lowHealth &&
            (lossAmount < estAmount || manaEfficiency <= HealingManaEfficiency::LOW))
            return 0.0f;
    }
    else
    {
        if (health >= sPlayerbotAIConfig.mediumHealth &&
            (lossAmount < estAmount || manaEfficiency <= HealingManaEfficiency::MEDIUM))
            return 0.0f;
        if (lossAmount < estAmount || manaEfficiency <= HealingManaEfficiency::LOW)
            return 0.0f;
    }

    return 1.0f;
}

void ConserveManaStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new ConserveManaMultiplier(ai));
    multipliers.push_back(new SaveManaMultiplier(ai));
}

void HealerAutoSaveManaStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new HealerAutoSaveManaMultiplier(ai));
}

void HealerAutoSaveManaStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new HealerAutoSaveManaMultiplier(ai));
}
