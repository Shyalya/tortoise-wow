#include "playerbot/playerbot.h"
#include "MoltenCoreMultipliers.h"
#include "playerbot/strategy/actions/MoltenCoreDungeonActions.h"
#include "playerbot/strategy/actions/ChooseTargetActions.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"
#include "playerbot/strategy/generic/MoltenCoreHelpers.h"

using namespace ai;
using namespace MoltenCoreHelpers;

static bool IsAoeDamageAction(Action* action)
{
    if (!action)
        return false;

    // Heals report AoE threat (heal aggro) but are not damage AoE.
    if (dynamic_cast<CastHealingSpellAction*>(action))
        return false;

    if (dynamic_cast<DpsAoeAction*>(action))
        return true;

    if (auto* castSpellAction = dynamic_cast<CastSpellAction*>(action))
    {
        if (castSpellAction->getThreatType() == ActionThreatType::ACTION_THREAT_AOE)
            return true;
    }

    // Many damage AoE actions still report Single threat — match by action name.
    // Avoid including per-class action headers here (shared class names collide).
    std::string const& name = action->getName();
    return name == "whirlwind" ||
           name == "cleave" ||
           name == "blizzard" ||
           name == "hurricane" ||
           name == "rain of fire" ||
           name == "consecration" ||
           name == "thunder clap" ||
           name == "volley" ||
           name == "multi-shot" ||
           name == "magma totem" ||
           name == "explosive trap" ||
           name == "swipe" ||
           name == "swipe (bear)" ||
           name == "swipe (cat)";
}

static bool IsSingleLivingTankInGroup(Player* bot)
{
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->getSource();
            if (!member || !member->IsAlive() || member == bot)
                continue;
            if (PlayerbotAI::IsTank(member))
                return false;
        }
    }
    return true;
}

static bool IsGarrAddTank(Player* bot)
{
    return PlayerbotAI::IsAssistTankOfIndex(bot, 0, true);
}

static bool IsFireswornUnit(Unit* unit)
{
    return unit && unit->IsAlive() && unit->GetEntry() == NPC_FIRESWORN;
}

float GarrMultiplier::GetValue(Action* action)
{
    Unit* garr = AI_VALUE2(Unit*, "find target", "garr");
    Unit* firesworn = AI_VALUE2(Unit*, "find target", "firesworn");
    if (!garr && !firesworn)
        return 1.0f;

    if (garr && PlayerbotAI::IsTank(bot) && IsSingleLivingTankInGroup(bot))
    {
        if (dynamic_cast<McGarrMainTankAttackGarrAction*>(action) ||
            dynamic_cast<McGarrAssistTankAttackFireswornAction*>(action) ||
            dynamic_cast<McGarrDpsAttackGarrAction*>(action))
            return 0.0f;
    }

    if (garr && IsGarrAddTank(bot))
    {
        if (dynamic_cast<TankAssistAction*>(action) || dynamic_cast<McGarrDpsAttackGarrAction*>(action))
            return 0.0f;
        return 1.0f;
    }

    if ((garr || firesworn) && PlayerbotAI::IsDps(bot) && IsAoeDamageAction(action))
        return 0.0f;

    if (garr)
    {
        if (PlayerbotAI::IsDps(bot) && dynamic_cast<DpsAssistAction*>(action))
            return 0.0f;

        if (IsFireswornUnit(action->GetTarget()))
        {
            if (dynamic_cast<McGarrDpsAttackGarrAction*>(action) ||
                dynamic_cast<McGarrMainTankAttackGarrAction*>(action))
                return 1.0f;

            if (dynamic_cast<AttackAction*>(action))
                return 0.0f;

            if (auto* castSpellAction = dynamic_cast<CastSpellAction*>(action))
            {
                if (!dynamic_cast<CastHealingSpellAction*>(action) &&
                    castSpellAction->getThreatType() != ActionThreatType::ACTION_THREAT_NONE)
                    return 0.0f;
            }
        }
    }
    else if (firesworn)
    {
        if (ai->IsMelee(bot) && PlayerbotAI::IsDps(bot))
        {
            if (IsFireswornUnit(action->GetTarget()) &&
                (dynamic_cast<AttackAction*>(action) || dynamic_cast<DpsAssistAction*>(action)))
                return 0.0f;
        }
    }

    return 1.0f;
}

static bool IsAllowedGeddonMovementAction(Action* action)
{
    if (dynamic_cast<MovementAction*>(action) &&
        !dynamic_cast<McMoveFromGroupAction*>(action) &&
        !dynamic_cast<McMoveFromBaronGeddonAction*>(action))
        return false;

    if (dynamic_cast<CastReachTargetSpellAction*>(action))
        return false;

    return true;
}

float BaronGeddonAbilityMultiplier::GetValue(Action* action)
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "baron geddon"))
    {
        if (ai->HasAura(SPELL_INFERNO, boss) && !IsAllowedGeddonMovementAction(action))
            return 0.0f;
    }

    if (ai->HasAura(SPELL_LIVING_BOMB, bot) && !IsAllowedGeddonMovementAction(action))
        return 0.0f;

    return 1.0f;
}

float GolemaggMultiplier::GetValue(Action* action)
{
    if (AI_VALUE2(Unit*, "find target", "golemagg the incinerator"))
    {
        if (PlayerbotAI::IsTank(bot) && IsSingleLivingTankInGroup(bot))
        {
            if (dynamic_cast<McGolemaggMainTankAttackGolemaggAction*>(action) ||
                dynamic_cast<McGolemaggAssistTankAttackCoreRagerAction*>(action))
                return 0.0f;
        }
        if (PlayerbotAI::IsAssistTank(bot) && dynamic_cast<TankAssistAction*>(action))
            return 0.0f;
        if (PlayerbotAI::IsDps(bot) && IsAoeDamageAction(action))
            return 0.0f;
    }
    return 1.0f;
}
