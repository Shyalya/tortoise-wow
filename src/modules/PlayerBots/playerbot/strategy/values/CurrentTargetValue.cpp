
#include "playerbot/playerbot.h"
#include "CurrentTargetValue.h"

#include "playerbot/ServerFacade.h"
using namespace ai;

Unit* CurrentTargetValue::Get()
{
    if (selection.IsEmpty())
        return NULL;

    Unit* unit = sObjectAccessor.GetUnit(*bot, selection);
    if (unit && !bot->IsWithinDistInMap(unit, sPlayerbotAIConfig.sightDistance))
        return NULL;

    // Distance was the only test here, so a target kept once was kept for good -
    // a player who stealthed after being targeted stayed targeted. This asks the
    // same question the target list asks before picking anyone: can the bot still
    // see them. It covers stealth, invisibility and visibility state and does not
    // involve line of sight, so a target behind a pillar is not lost.
    if (unit && unit != bot &&
        !unit->IsVisibleForOrDetect(bot, bot->GetCamera().GetBody(), true))
        return NULL;

    return unit;
}

void CurrentTargetValue::Set(Unit* target)
{
    selection = target ? target->GetObjectGuid() : ObjectGuid();
}

void CurrentTargetValue::Reset()
{
    UnitManualSetValue::Reset();
    selection = ObjectGuid();
}
