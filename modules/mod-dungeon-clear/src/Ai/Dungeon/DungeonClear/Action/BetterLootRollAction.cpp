/*
 * mod-dungeon-clear — BetterLootRollAction.cpp  (Tortoise port)
 *
 * Rewritten against this tree's roll plumbing rather than transliterated from
 * the AzerothCore original, because the two are shaped differently in a way
 * that matters.
 *
 * Upstream overrides Execute() and walks Group::GetRolls() itself, deciding
 * every pending roll and voting afterwards. It has to: mod-playerbots offers
 * no hook between "a roll is pending" and "here is my vote".
 *
 * This tree does. RollAction::CalculateRollVote(ItemQualifier&) is exactly
 * that hook - it is asked once per pending item and its answer is the vote.
 * So improvement #2 is an override of that one method, with the base class
 * left to iterate, read the loot, cast the votes and destroy the rolls. The
 * comment upstream carries about "no Roll* may be read after any vote has been
 * cast" is not a hazard here for the same reason: this code never holds one.
 *
 * Improvement #1 (a self-bot must not vote, because bot and human share a GUID
 * and the bot's vote pre-empts the player's dialog) is unchanged in intent and
 * stays in isUseful(), plus the same repeat inside the decision - a queued
 * basket outlives the trigger that filled it, so a gate that exists only in
 * isUseful() is not a gate.
 *
 * Dropped in the port, both because this core has no equivalent:
 *   - the PvP-spec weighting (RandomPlayerbotMgr has no IsSpecPvp here)
 *   - random suffixes (1.12 items carry a random property id and nothing else)
 */

#include "BetterLootRollAction.h"

#include "playerbot/playerbot.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPlayerbotCompat.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "StatsWeightCalculator.h"

using namespace ai;

bool DungeonClearBetterLootRollAction::isUseful()
{
    // Same mapless guard as StayDeadAction::isUseful - the stock base reads
    // through the bot's map, and GetMap throws on a mid-teleport bot here.
    if (!bot || !bot->FindMap())
        return false;

    // Only intercept self-bots (master == bot). A bot driven for a separate
    // human master keeps stock rolling — its vote is its own GUID, no conflict.
    if (DcPlayerbotCompat::IsSelfBot(bot) && DcSettings::GetBool(bot, "BetterLootRolling"))
        return false;  // bot-self: cast no vote so the human gets to roll

    return LootRollAction::isUseful();
}

RollVote DungeonClearBetterLootRollAction::CalculateRollVote(ItemQualifier& itemQualifier)
{
    if (!DcSettings::GetBool(bot, "BetterLootRolling"))
        return LootRollAction::CalculateRollVote(itemQualifier);

    // See the header comment: isUseful() alone does not gate a queued basket.
    if (DcPlayerbotCompat::IsSelfBot(bot))
        return ROLL_PASS;

    ItemPrototype const* proto = itemQualifier.GetProto();

    // Anything that is not the over-level case is stock's to answer.
    if (!proto || !IsFutureWearable(proto))
        return LootRollAction::CalculateRollVote(itemQualifier);

    return CalculateFutureVote(proto, itemQualifier.GetRandomPropertyId());
}

bool DungeonClearBetterLootRollAction::IsFutureWearable(ItemPrototype const* proto) const
{
    if (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR)
        return false;

    if (proto->RequiredLevel <= bot->GetLevel())
        return false;

    // CanUseItem checks faction, class/race, skill and spell BEFORE level, so
    // this exact error means the level requirement is the only blocker.
    return bot->CanUseItem(proto) == EQUIP_ERR_CANT_EQUIP_LEVEL_I;
}

RollVote DungeonClearBetterLootRollAction::CalculateFutureVote(ItemPrototype const* proto, int32 randomProperty)
{
    // Proficiency judged at the item's required level, not the bot's current
    // one — a 35 warrior WILL wear level-42 plate (plate unlocks at 40).
    //
    // The upstream SFINAE dispatch over two CanEquip* parameter orders is gone:
    // it existed to build against two mod-playerbots branches, and there is
    // only one signature here. Armor takes a spec argument this core cannot
    // supply at roll time; 0 is the unspecialised weighting.
    bool const proficient = proto->Class == ITEM_CLASS_WEAPON
        ? sRandomItemMgr.CanEquipWeapon(bot->getClass(), proto)
        : sRandomItemMgr.CanEquipArmor(bot->getClass(), 0, proto->RequiredLevel, proto);

    if (!proficient)
        return ROLL_GREED;  // never their gear, but still vendor value

    StatsWeightCalculator calculator(bot);
    calculator.SetItemSetBonus(false);
    calculator.SetOverflowPenalty(false);

    return calculator.CalculateItem(proto->ItemId, randomProperty) > 0 ? ROLL_NEED : ROLL_GREED;
}
