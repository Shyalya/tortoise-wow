#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonStrategies.h"
#include "DungeonMultipliers.h"
#include "MoltenCoreMultipliers.h"
#include "playerbot/strategy/Strategy.h"

using namespace ai;

void MoltenCoreDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "start magmadar fight",
        NextAction::array(0, new NextAction("enable magmadar fight strategy", 100.0f), NULL)));

    // Lucifron
    triggers.push_back(new TriggerNode(
        "mc lucifron shadow resistance",
        NextAction::array(0, new NextAction("mc lucifron shadow resistance", ACTION_RAID), NULL)));

    // Magmadar
    triggers.push_back(new TriggerNode(
        "mc magmadar fire resistance",
        NextAction::array(0, new NextAction("mc magmadar fire resistance", ACTION_RAID), NULL)));

    // Gehennas
    triggers.push_back(new TriggerNode(
        "mc gehennas shadow resistance",
        NextAction::array(0, new NextAction("mc gehennas shadow resistance", ACTION_RAID), NULL)));

    // Garr
    triggers.push_back(new TriggerNode(
        "mc garr fire resistance",
        NextAction::array(0, new NextAction("mc garr fire resistance", ACTION_RAID), NULL)));
    triggers.push_back(new TriggerNode(
        "mc garr mark boss",
        NextAction::array(0, new NextAction("mc garr mark boss", ACTION_RAID), NULL)));
    triggers.push_back(new TriggerNode(
        "mc garr is main tank",
        NextAction::array(0, new NextAction("mc garr main tank attack garr", ACTION_RAID), NULL)));
    triggers.push_back(new TriggerNode(
        "mc garr is assist tank",
        NextAction::array(0, new NextAction("mc garr assist tank attack firesworn", ACTION_RAID + 1), NULL)));
    triggers.push_back(new TriggerNode(
        "mc garr dps focus garr",
        NextAction::array(0, new NextAction("mc garr dps attack garr", ACTION_RAID), NULL)));
    triggers.push_back(new TriggerNode(
        "mc garr mark firesworn",
        NextAction::array(0, new NextAction("mc garr mark firesworn", ACTION_RAID), NULL)));

    // Baron Geddon
    triggers.push_back(new TriggerNode(
        "mc baron geddon fire resistance",
        NextAction::array(0, new NextAction("mc baron geddon fire resistance", ACTION_RAID), NULL)));
    triggers.push_back(new TriggerNode(
        "mc living bomb debuff",
        NextAction::array(0, new NextAction("mc move from group", ACTION_RAID), NULL)));
    triggers.push_back(new TriggerNode(
        "mc baron geddon inferno",
        NextAction::array(0, new NextAction("mc move from baron geddon", ACTION_RAID), NULL)));

    // Shazzrah
    triggers.push_back(new TriggerNode(
        "mc shazzrah ranged",
        NextAction::array(0, new NextAction("mc shazzrah move away", ACTION_RAID), NULL)));

    // Sulfuron
    triggers.push_back(new TriggerNode(
        "mc sulfuron harbinger fire resistance",
        NextAction::array(0, new NextAction("mc sulfuron harbinger fire resistance", ACTION_RAID), NULL)));

    // Golemagg
    triggers.push_back(new TriggerNode(
        "mc golemagg fire resistance",
        NextAction::array(0, new NextAction("mc golemagg fire resistance", ACTION_RAID), NULL)));
    triggers.push_back(new TriggerNode(
        "mc golemagg mark boss",
        NextAction::array(0, new NextAction("mc golemagg mark boss", ACTION_RAID), NULL)));
    triggers.push_back(new TriggerNode(
        "mc golemagg is main tank",
        NextAction::array(0, new NextAction("mc golemagg main tank attack golemagg", ACTION_RAID), NULL)));
    triggers.push_back(new TriggerNode(
        "mc golemagg is assist tank",
        NextAction::array(0, new NextAction("mc golemagg assist tank attack core rager", ACTION_RAID), NULL)));

    // Majordomo
    triggers.push_back(new TriggerNode(
        "mc majordomo shadow resistance",
        NextAction::array(0, new NextAction("mc majordomo shadow resistance", ACTION_RAID), NULL)));

    // Ragnaros
    triggers.push_back(new TriggerNode(
        "mc ragnaros fire resistance",
        NextAction::array(0, new NextAction("mc ragnaros fire resistance", ACTION_RAID), NULL)));

    // Trash
    triggers.push_back(new TriggerNode(
        "mc core hound mark",
        NextAction::array(0, new NextAction("mc core hound mark", ACTION_RAID), NULL)));

    triggers.push_back(new TriggerNode(
        "fire protection potion ready",
        NextAction::array(0, new NextAction("fire protection potion", 100.0f), NULL)));
}

void MoltenCoreDungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "mc rune in sight",
        NextAction::array(0, new NextAction("move to mc rune", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "mc rune close",
        NextAction::array(0,
            new NextAction("douse mc rune eternal", 2.0f),
            new NextAction("douse mc rune aqual", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "fire protection potion ready",
        NextAction::array(0, new NextAction("fire protection potion", 100.0f), NULL)));
}

void MoltenCoreDungeonStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new GarrMultiplier(ai));
    multipliers.push_back(new BaronGeddonAbilityMultiplier(ai));
    multipliers.push_back(new GolemaggMultiplier(ai));
}

void MagmadarFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        triggers.push_back(new TriggerNode(
            "magmadar too close",
            NextAction::array(0, new NextAction("move away from magmadar", 100.0f), NULL)));
    }

    triggers.push_back(new TriggerNode(
        "fire protection potion ready",
        NextAction::array(0, new NextAction("fire protection potion", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end magmadar fight",
        NextAction::array(0, new NextAction("disable magmadar fight strategy", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end magmadar fight",
        NextAction::array(0, new NextAction("disable magmadar fight strategy", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "magmadar lava bomb",
        NextAction::array(0, new NextAction("move away from hazard", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        multipliers.push_back(new PreventMoveAwayFromCreatureOnReachToCastMultiplier(ai));
    }
}
