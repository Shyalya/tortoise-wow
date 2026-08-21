#include "playerbot/playerbot.h"
#include "BossAuraTriggers.h"

using namespace ai;

static bool IsFirstAlivePaladinInRaid(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group || !group->isRaidGroup())
        return false;

    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* member = gref->getSource();
        if (!member || !member->IsAlive())
            continue;

        if (member->getClass() == CLASS_PALADIN)
            return member == bot;
    }

    return false;
}

bool BossFireResistanceTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", bossName);
    if (!boss || !boss->IsAlive() || boss->IsFriendlyTo(bot))
        return false;

    if (bot->getClass() != CLASS_PALADIN)
        return false;

    if (ai->HasAura("fire resistance aura", bot))
        return false;

    if (ai->HasStrategy("aura fire", BotState::BOT_STATE_COMBAT))
        return false;

    if (!ai->HasSpell(SPELL_FIRE_RESISTANCE_AURA_RANK_3) &&
        !ai->HasSpell(SPELL_FIRE_RESISTANCE_AURA_RANK_2) &&
        !ai->HasSpell(SPELL_FIRE_RESISTANCE_AURA_RANK_1))
        return false;

    return IsFirstAlivePaladinInRaid(bot);
}

bool BossShadowResistanceTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", bossName);
    if (!boss || !boss->IsAlive() || boss->IsFriendlyTo(bot))
        return false;

    if (bot->getClass() != CLASS_PALADIN)
        return false;

    if (ai->HasAura("shadow resistance aura", bot))
        return false;

    if (ai->HasStrategy("aura shadow", BotState::BOT_STATE_COMBAT))
        return false;

    if (!ai->HasSpell(SPELL_SHADOW_RESISTANCE_AURA_RANK_3) &&
        !ai->HasSpell(SPELL_SHADOW_RESISTANCE_AURA_RANK_2) &&
        !ai->HasSpell(SPELL_SHADOW_RESISTANCE_AURA_RANK_1))
        return false;

    return IsFirstAlivePaladinInRaid(bot);
}
