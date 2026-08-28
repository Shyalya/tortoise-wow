
#include "playerbot/playerbot.h"
#include "RogueActions.h"
#include "playerbot/strategy/values/GroupCcTargetReservation.h"

using namespace ai;

bool CastSapAction::Execute(Event& event)
{
    Unit* target = GetTarget();
    if (!GroupCcTargetReservation::PrepareFallbackCast(ai, target))
        return false;

    if (GroupCcTargetReservation::IsInFlight(bot, target->GetObjectGuid()))
        return true;

    bool executed = CastSpellAction::Execute(event);
    GroupCcTargetReservation::RecordCast(bot, target, executed);
    return executed;
}
