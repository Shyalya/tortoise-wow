#include "playerbot/playerbot.h"
#include "BossAuraActions.h"
#include "playerbot/strategy/triggers/BossAuraTriggers.h"

using namespace ai;

bool BossFireResistanceAction::isUseful()
{
    BossFireResistanceTrigger trigger(ai, bossName);
    return trigger.IsActive();
}

bool BossFireResistanceAction::Execute(Event& /*event*/)
{
    ai->ChangeStrategy("+aura fire", BotState::BOT_STATE_COMBAT);
    return ai->DoSpecificAction("fire resistance aura", Event(), true);
}

bool BossShadowResistanceAction::isUseful()
{
    BossShadowResistanceTrigger trigger(ai, bossName);
    return trigger.IsActive();
}

bool BossShadowResistanceAction::Execute(Event& /*event*/)
{
    ai->ChangeStrategy("+aura shadow", BotState::BOT_STATE_COMBAT);
    return ai->DoSpecificAction("shadow resistance aura", Event(), true);
}
