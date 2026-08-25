#include "Strategy/DungeonClearStrategy.h"
#include "DcRelevance.h"
#include "Multiplier/DungeonClearMultiplier.h"

using namespace ai;

void DungeonClearStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("dungeon clear party died",
        NextAction::array(0, new NextAction("dungeon clear disable on death", DcRel::PartyDied), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear all cleared",
        NextAction::array(0, new NextAction("dungeon clear disable on cleared", DcRel::AllCleared), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear rez party",
        NextAction::array(0, new NextAction("dungeon clear rez party", DcRel::RezParty), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear pull",
        NextAction::array(0, new NextAction("dungeon clear pull", DcRel::Pull), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear event due",
        NextAction::array(0, new NextAction("dungeon clear run event", DcRel::EventDue), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear at boss",
        NextAction::array(0, new NextAction("dungeon clear engage boss", DcRel::AtBoss), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear at objective",
        NextAction::array(0, new NextAction("dungeon clear objective arrive", DcRel::AtObjective), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear blocking trash",
        NextAction::array(0, new NextAction("dungeon clear engage trash", DcRel::BlockingTrash), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear filter loot",
        NextAction::array(0, new NextAction("dungeon clear filter loot", DcRel::FilterLoot), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear need advance",
        NextAction::array(0, new NextAction("dungeon clear advance", DcRel::Advance), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear follow tank",
        NextAction::array(0, new NextAction("dungeon clear follow tank", DcRel::FollowTank), NULL)));

    // Chat keywords (party chat → action names)
    triggers.push_back(new TriggerNode("dc on",
        NextAction::array(0, new NextAction("dc on", ACTION_PASSTROUGH + 1), NULL)));
    triggers.push_back(new TriggerNode("dc off",
        NextAction::array(0, new NextAction("dc off", ACTION_PASSTROUGH + 1), NULL)));
    triggers.push_back(new TriggerNode("dc pause",
        NextAction::array(0, new NextAction("dc pause", ACTION_PASSTROUGH + 1), NULL)));
    triggers.push_back(new TriggerNode("dc skip",
        NextAction::array(0, new NextAction("dc skip", ACTION_PASSTROUGH + 1), NULL)));
    triggers.push_back(new TriggerNode("dc pull",
        NextAction::array(0, new NextAction("dc pull", ACTION_PASSTROUGH + 1), NULL)));
    triggers.push_back(new TriggerNode("dc status",
        NextAction::array(0, new NextAction("dc status", ACTION_PASSTROUGH + 1), NULL)));
    triggers.push_back(new TriggerNode("dc bosses",
        NextAction::array(0, new NextAction("dc bosses", ACTION_PASSTROUGH + 1), NULL)));
}

void DungeonClearStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new DungeonClearMultiplier(ai));
}

void DungeonClearCombatStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("dungeon clear pull",
        NextAction::array(0, new NextAction("dungeon clear pull", DcRel::Pull), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear at boss",
        NextAction::array(0, new NextAction("dungeon clear engage boss", DcRel::AtBoss), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear follow tank",
        NextAction::array(0, new NextAction("dungeon clear follow tank", DcRel::FollowTank), NULL)));
    triggers.push_back(new TriggerNode("dungeon clear assist tank",
        NextAction::array(0, new NextAction("dungeon clear assist tank", DcRel::FollowTank + 1), NULL)));
}

void DungeonClearCombatStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new DungeonClearCombatMultiplier(ai));
}
