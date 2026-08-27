#include "playerbot/playerbot.h"
#include "ForceRebuffStrategy.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"

using namespace ai;

namespace
{
    class ForceRebuffMultiplier : public Multiplier
    {
    public:
        ForceRebuffMultiplier(PlayerbotAI* ai) : Multiplier(ai, "force rebuff") {}

        float GetValue(Action* action) override
        {
            if (!action || !ai->IsForceRebuffPending() || ai->IsForceRebuffExpired() || bot->IsInCombat())
                return 1.0f;

            CastHealingSpellAction* healing = dynamic_cast<CastHealingSpellAction*>(action);
            if (!healing)
                return 1.0f;

            Unit* target = healing->GetActionTarget();
            if (target && target->GetHealthPercent() <= sPlayerbotAIConfig.criticalHealth)
                return 1.0f;

            return ai->HasForceRebuffBuffWorkThisCycle() ? 0.0f : 1.0f;
        }
    };
}

void ForceRebuffStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "force rebuff pending",
        NextAction::array(0, new NextAction("ready reply", ACTION_IDLE), NULL)));
}

void ForceRebuffStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new ForceRebuffMultiplier(ai));
}
