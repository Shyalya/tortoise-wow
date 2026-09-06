
#include "playerbot/playerbot.h"
#include "RacialsStrategy.h"

using namespace ai;

void RacialsStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    if (!ai || !ai->GetBot()) return;
    // ManTech capability pruning, expressed through learned spells so Turtle's
    // custom races are not classified using TBC/WotLK numeric race constants.
    auto add = [&](char const* trigger, char const* spell, float priority = 71.0f)
    {
        if (ai->HasSpell(std::string(spell)))
            triggers.push_back(new TriggerNode(trigger,
                NextAction::array(0, new NextAction(spell, priority), NULL)));
    };
    add("low health", "gift of the naaru");
    add("melee medium aoe", "war stomp");
    add("war stomp", "war stomp");
    add("cannibalize", "cannibalize");
    add("perception", "perception");
    add("rooted", "escape artist");
    add("will of the forsaken", "will of the forsaken");
    add("berserking", "berserking", 58.0f);
    add("blood fury", "blood fury");
    add("stoneform", "stoneform");
    add("mana tap", "mana tap");
    add("arcane torrent", "arcane torrent");
}

void RacialsStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    InitNonCombatTriggers(triggers);
}
