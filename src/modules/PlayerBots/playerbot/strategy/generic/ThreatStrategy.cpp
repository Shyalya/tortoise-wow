
#include "playerbot/playerbot.h"
#include "ThreatStrategy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"

using namespace ai;

float ThreatMultiplier::GetValue(Action* action)
{
    if (!action || action->getThreatType() == ActionThreatType::ACTION_THREAT_NONE)
        return 1.0f;

    // Tanks must never throttle threat-generating abilities.
    if (ai->IsTank(bot))
        return 1.0f;

    // Heal threat is reported as AoE; never block healing because of it.
    if (dynamic_cast<CastHealingSpellAction*>(action))
        return 1.0f;

    if (!AI_VALUE(bool, "group"))
        return 1.0f;

    // Without a living tank, threat avoidance only causes idle GCDs.
    bool hasTank = false;
    for (Player* member : ai->GetPlayersInGroup())
    {
        if (member && member->IsAlive() && ai->IsTank(member))
        {
            hasTank = true;
            break;
        }
    }
    if (!hasTank)
        return 1.0f;

    if (action->getThreatType() == ActionThreatType::ACTION_THREAT_AOE)
    {
        uint8 threat = AI_VALUE2(uint8, "threat", "aoe");
        if (threat >= 50)
            return 0.0f;
    }

    if (ai->HasStrategy("debug threat", BotState::BOT_STATE_COMBAT))
    {
        if (ai->GetMaster())
        {
            if (AI_VALUE2(bool, "trigger active", "high threat"))
                ai->GetMaster()->GetSession()->SendPlaySpellVisual(ai->GetBot()->GetObjectGuid(), 6372);
            else if (AI_VALUE2(bool, "trigger active", "medium threat"))
                ai->GetMaster()->GetSession()->SendPlaySpellVisual(ai->GetBot()->GetObjectGuid(), 5036);
        }
    }

    uint8 threat = AI_VALUE2(uint8, "threat", "current target");

    // Soft throttle: keep auto-attack and low-threat fillers, drop hard hitters
    // before they rip aggro. Hard-stop only when already critically high.
    if (threat >= 90)
        return 0.0f;

    if (threat >= 80)
        return 0.5f;

    if (threat >= 60)
        return 0.75f;

    return 1.0f;
}

void ThreatStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new ThreatMultiplier(ai));
}
