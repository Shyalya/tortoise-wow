#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonTriggers.h"

using namespace ai;
using namespace MoltenCoreHelpers;

bool McGarrMarkBossTrigger::IsActive()
{
    return AI_VALUE2(Unit*, "find target", "garr") && PlayerbotAI::IsTank(bot);
}

bool McGarrIsMainTankTrigger::IsActive()
{
    return AI_VALUE2(Unit*, "find target", "garr") && PlayerbotAI::IsMainTank(bot);
}

bool McGarrIsAssistTankTrigger::IsActive()
{
    return AI_VALUE2(Unit*, "find target", "garr") && PlayerbotAI::IsAssistTank(bot);
}

bool McGarrDpsFocusGarrTrigger::IsActive()
{
    if (!AI_VALUE2(Unit*, "find target", "garr"))
        return false;

    if (PlayerbotAI::IsAssistTankOfIndex(bot, 0, true))
        return false;

    return PlayerbotAI::IsDps(bot) || PlayerbotAI::IsTank(bot);
}

bool McGarrMarkFireswornTrigger::IsActive()
{
    if (AI_VALUE2(Unit*, "find target", "garr"))
        return false;

    return PlayerbotAI::IsTank(bot) && AI_VALUE2(Unit*, "find target", "firesworn");
}

bool McLivingBombDebuffTrigger::IsActive()
{
    return ai->HasAura(SPELL_LIVING_BOMB, bot);
}

bool McBaronGeddonInfernoTrigger::IsActive()
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "baron geddon"))
        return ai->HasAura(SPELL_INFERNO, boss);
    return false;
}

bool McShazzrahRangedTrigger::IsActive()
{
    return AI_VALUE2(Unit*, "find target", "shazzrah") && ai->IsRanged(bot);
}

bool McGolemaggMarkBossTrigger::IsActive()
{
    return AI_VALUE2(Unit*, "find target", "golemagg the incinerator") && PlayerbotAI::IsTank(bot);
}

bool McGolemaggIsMainTankTrigger::IsActive()
{
    return AI_VALUE2(Unit*, "find target", "golemagg the incinerator") && PlayerbotAI::IsMainTank(bot);
}

bool McGolemaggIsAssistTankTrigger::IsActive()
{
    return AI_VALUE2(Unit*, "find target", "golemagg the incinerator") && PlayerbotAI::IsAssistTank(bot);
}

bool McCoreHoundMarkTrigger::IsActive()
{
    return PlayerbotAI::IsMainTank(bot) && AI_VALUE2(Unit*, "find target", "core hound");
}
