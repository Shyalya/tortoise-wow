
#include "playerbot/playerbot.h"
#include "LootRollAction.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/LootValues.h"
#include "Group/Group.h"
#include "Maps/Map.h"

using namespace ai;

bool LootStartRollAction::Execute(Event& event)
{
    WorldPacket p(event.getPacket()); //WorldPacket packet for CMSG_LOOT_ROLL, (8+4+1)
    ObjectGuid creatureGuid;
    uint32 itemSlot;
    uint32 itemId;
    uint32 randomSuffix;
    int32 randomPropertyId;
#ifdef MANGOSBOT_TWO
    uint32 mapId;
    uint32 count;
#endif 
    uint32 timeout;

    p.rpos(0); //reset packet pointer
    p >> creatureGuid; //creature guid what we're looting
#ifdef MANGOSBOT_TWO
    p >> mapId; /// 3.3.3 mapid
#endif 
    p >> itemSlot; // the itemEntryId for the item that shall be rolled for
    p >> itemId; // the itemEntryId for the item that shall be rolled for
    p >> randomSuffix; // randomSuffix
    p >> randomPropertyId; // item random property ID
#ifdef MANGOSBOT_TWO
    p >> count; // items in stack
#endif 
    p >> timeout;  // the countdown time to choose "need" or "greed"

    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");

    if (!bot->GetGroup())
        return false;

    // One packet is sent per item, so remember exactly this slot. The old
    // code walked every loot slot through Loot::GetRollForSlot and found
    // nothing, and bailed out entirely once the creature was known - which
    // would have dropped the second item of any corpse anyway.
    bool known = false;
    auto range = lootRolls.equal_range(creatureGuid);
    for (auto itr = range.first; itr != range.second; ++itr)
    {
        if (itr->second == itemSlot)
        {
            known = true;
            break;
        }
    }

    if (!known)
        lootRolls.insert({ creatureGuid, itemSlot });

    ActiveRolls::CleanUp(bot, lootRolls);

    SET_AI_VALUE(LootRollMap, "active rolls", lootRolls);

    return false;
}

bool RollAction::Execute(Event& event)
{      
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();

    if (text.empty())
    {
        ai->TellPlayerNoFacing(requester, "Please give a roll type or item. See " + ChatHelper::formatValue("help", "action:roll", "roll help") + " for more information.");
        return false;
    }

    ItemIds ids = ChatHelper::parseItems(text);

    std::string type = "auto";
    if (ids.empty())
        type = text;
    else
        type = text.substr(0, text.find(" "));

    if (type == "emote")
    {
        std::vector<std::string> args = ChatHelper::splitString(text, " ");

        if (args.size() == 2)
            args = { args[0], "1", args[1] };
        if (args.size() == 1)
            args = { args[0], "1", "100" };

        for (char& d : args[1]) //Check if itemId contains only numbers
            if (!isdigit(d))
                return false;

        for (char& d : args[2]) //Check if itemId contains only numbers
            if (!isdigit(d))
                return false;

        WorldPacket data(MSG_RANDOM_ROLL);
        data << stoi(args[1]);
        data << stoi(args[2]);
        bot->GetSession()->HandleRandomRollOpcode(data);

        return true;
    }

    bool rollFeedback = AI_VALUE2(bool, "manual bool", "roll feedback");

    if (type == "feedback")
    {
        rollFeedback = !rollFeedback;

        if (!rollFeedback)
            ai->TellPlayerNoFacing(requester, "Roll feedback disabled.");
        else
            ai->TellPlayerNoFacing(requester, "Roll feedback enalbed.");

        SET_AI_VALUE2(bool, "manual bool", "roll feedback", rollFeedback);

        return true;
    }

    if (!bot->GetGroup())
        return false;

    if (AI_VALUE(LootRollMap, "active rolls").empty())
        return false;

    if (AI_VALUE(uint8, "bag space") >= 100)
        return false;

    if (type != "need" && type != "greed" && type != "pass" && type != "auto")
    {
        ai->TellPlayerNoFacing(requester, "Please give a correct roll type. need, greed, pass or auto. See " + ChatHelper::formatValue("help", "action:roll", "roll help") + " for more information.");
        return false;
    }

    RollVote vote = ROLL_NOT_VALID;

    if (type.find("need") == 0)
        vote = ROLL_NEED;
    else if (type.find("greed") == 0)
        vote = ROLL_GREED;
    else if (type.find("pass") == 0)
        vote = ROLL_PASS;

    uint32 rolledItems = 0;

    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");

    for (auto roll : lootRolls)
    {
        ItemQualifier itemQualifier = GetRollItem(roll.first, roll.second);

        if (!itemQualifier.GetId())
            continue;

        if (!ids.empty() && ids.find(itemQualifier.GetId()) == ids.end())
            continue;

        RollVote doVote = vote;
        if (doVote == ROLL_NOT_VALID) //Auto
            doVote = CalculateRollVote(itemQualifier);

        rolledItems += RollOnItemInSlot(doVote, roll.first, roll.second);     
    }

    return rolledItems;
}

ItemQualifier RollAction::GetRollItem(ObjectGuid lootGuid, uint32 slot)
{
    if (Loot* loot = sLootMgr.GetLoot(bot, lootGuid))
        if (LootItem* item = loot->GetLootItemInSlot(slot))
            return ItemQualifier(item);

    // The loot object is only reachable while the bot has the corpse open.
    // The group's roll carries the same item and lasts the whole countdown.
    if (Group* group = bot->GetGroup())
        if (Roll const* roll = group->GetActiveRoll(lootGuid, slot))
            return ItemQualifier(roll->itemid, roll->itemRandomPropId);

    return ItemQualifier();
}

RollVote RollAction::CalculateRollVote(ItemQualifier& itemQualifier)
{
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemQualifier.GetQualifier());

    RollVote needVote = ROLL_PASS;
    switch (usage)
    {
    case ItemUsage::ITEM_USAGE_EQUIP:
    case ItemUsage::ITEM_USAGE_GUILD_TASK:
    case ItemUsage::ITEM_USAGE_FORCE_NEED:
        needVote = ROLL_NEED;
        break;
    case ItemUsage::ITEM_USAGE_SKILL:
    case ItemUsage::ITEM_USAGE_USE:
    case ItemUsage::ITEM_USAGE_AH:
    case ItemUsage::ITEM_USAGE_BROKEN_AH:
    case ItemUsage::ITEM_USAGE_VENDOR:
    case ItemUsage::ITEM_USAGE_FORCE_GREED:
        needVote = ROLL_GREED;
        break;
    case ItemUsage::ITEM_USAGE_DISENCHANT:
#ifndef MANGOSBOT_TWO
        needVote = ROLL_GREED;
#else
        needVote = ROLL_DISENCHANT;
#endif
        break;
    }

    // special case for bad equip
    if (usage == ItemUsage::ITEM_USAGE_BAD_EQUIP)
    {
        bool shouldEquipBadItems = sPlayerbotAIConfig.rollBadItemsWithPlayer || !ai->HasRealPlayerMaster();
        if (shouldEquipBadItems)
            needVote = ROLL_NEED;
        else
            needVote = ROLL_GREED;
    }

    bool canLoot = StoreLootAction::IsLootAllowed(itemQualifier, GetBotAI(bot));

    if (AI_VALUE2(bool, "manual bool", "roll feedback"))
    {
        std::string reason = "because it can not be looted.";
        std::string vote = "Passing";
        if(canLoot)
            reason = ItemUsageValue::ReasonForNeed(usage, itemQualifier, 1, bot);

        if (needVote == ROLL_GREED)
            vote = "Rolling greed";
        else if (needVote == ROLL_NEED)
            vote = "Rolling need";
        else if (needVote == ROLL_DISENCHANT)
            vote = "Rolling disenchant";

         ai->TellPlayerNoFacing(ai->GetMaster(), vote + " on " + ChatHelper::formatItem(itemQualifier) + " " + reason);
    }

    return canLoot ? needVote : ROLL_PASS;
}

bool RollAction::RollOnItemInSlot(RollVote vote, ObjectGuid lootGuid, uint32 slot)
{
    Group* group = bot->GetGroup();
    if (!group || !group->GetActiveRoll(lootGuid, slot))
        return false;

    if (vote != ROLL_NEED && vote != ROLL_GREED && vote != ROLL_PASS)
        vote = ROLL_PASS;

    // Same entry point the client uses via CMSG_LOOT_ROLL. The core checks
    // eligibility and resolves the roll once everyone has voted.
    group->CountRollVote(bot, lootGuid, slot, vote);

    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");

    ActiveRolls::CleanUp(bot, lootRolls, lootGuid, slot);

    SET_AI_VALUE(LootRollMap, "active rolls", lootRolls);

    return true;
}

// Do any real players still have the roll dialog open, and did one of them
// ask for need? Bots are ignored - they are expected to sort themselves out.
bool RollAction::HumansStillDeciding(ObjectGuid lootGuid, uint32 slot, bool& humanNeeds)
{
    humanNeeds = false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    Roll const* roll = group->GetActiveRoll(lootGuid, slot);
    if (!roll)
        return false;

    bool waiting = false;
    for (auto const& vote : roll->playerVote)
    {
        Player* voter = sObjectMgr.GetPlayer(vote.first);
        if (!voter || GetBotAI(voter))
            continue;

        if (vote.second == ROLL_NEED)
            humanNeeds = true;
        else if (vote.second == ROLL_NOT_EMITED_YET)
            waiting = true;
    }

    return waiting;
}

// Half the countdown is gone. Past that the bot votes regardless, so one
// player who walked away cannot make the whole group wait out the timer.
bool RollAction::RollAboutToExpire(ObjectGuid lootGuid)
{
    Creature* target = bot->GetMap() ? bot->GetMap()->GetCreature(lootGuid) : nullptr;
    if (!target)
        return true;

    uint32 const left = target->GetGroupLootTimer();
    return left == 0 || left < 30 * IN_MILLISECONDS;
}

// Vote on one item the polite way.
bool RollAction::RollWithEtiquette(ObjectGuid lootGuid, uint32 slot)
{
    bool humanNeeds = false;
    if (HumansStillDeciding(lootGuid, slot, humanNeeds) && !RollAboutToExpire(lootGuid))
        return false;

    ItemQualifier itemQualifier = GetRollItem(lootGuid, slot);
    if (!itemQualifier.GetId())
        return false;

    RollVote vote = CalculateRollVote(itemQualifier);

    // A player asked for it. Never roll need against them.
    if (humanNeeds && vote == ROLL_NEED)
        vote = ROLL_GREED;

    return RollOnItemInSlot(vote, lootGuid, slot);
}

bool LootRollAction::Execute(Event& event)
{
    Player* bot = QueryItemUsageAction::ai->GetBot();

    WorldPacket p(event.getPacket()); //WorldPacket packet for CMSG_LOOT_ROLL, (8+4+1)
    ObjectGuid guid;
    uint32 slot;
    uint8 rollType;
    p.rpos(0); //reset packet pointer
    p >> guid; //guid of the item rolled
    p >> slot; //number of players invited to roll
    p >> rollType; //need,greed or pass on roll

    return RollWithEtiquette(guid, slot);
}

bool AutoLootRollAction::Execute(Event& event)
{
    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");
    if (lootRolls.empty())
        return false;

    auto currentRoll = std::next(lootRolls.begin(), urand(0, lootRolls.size() - 1));

    return RollWithEtiquette(currentRoll->first, currentRoll->second);
}

bool AutoLootRollAction::isPossible()
{
    return bot->GetGroup() && !AI_VALUE(LootRollMap, "active rolls").empty() && AI_VALUE(uint8, "bag space") < 100;
}