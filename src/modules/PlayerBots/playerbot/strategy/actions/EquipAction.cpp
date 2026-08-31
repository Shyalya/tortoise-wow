
#include "playerbot/playerbot.h"
#include "EquipAction.h"

#include "playerbot/RandomItemMgr.h"
#include "playerbot/strategy/values/ItemCountValue.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "UnequipAction.h"

using namespace ai;

namespace
{
    InventoryResult CanEquipOffhandAfterTwoHand(Player* bot, Item* item)
    {
        uint16 dest;
        InventoryResult result = bot->CanEquipItem(EQUIPMENT_SLOT_OFFHAND, dest, item, true);
        if (result != EQUIP_ERR_OK && result != EQUIP_ERR_CANT_EQUIP_WITH_TWOHANDED)
            return result;

        // CanEquipItem checks the current 2H restriction after all generic item,
        // proficiency, uniqueness, combat, and dual-wield checks. Reaching only
        // that error proves the offhand is otherwise valid for the post-2H state.

        // A legal two-handed setup has no offhand item. Reject an inconsistent
        // state rather than relying on two sequential displacements sharing space.
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
            return EQUIP_ERR_ITEMS_CANT_BE_SWAPPED;

        uint16 mainhandPos = ((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_MAINHAND);
        Item* mainhand = bot->GetItemByPos(mainhandPos);
        if (!mainhand || mainhand->GetProto()->InventoryType != INVTYPE_2HWEAPON)
            return EQUIP_ERR_ITEM_NOT_FOUND;

        result = bot->CanUnequipItem(mainhandPos, false);
        if (result != EQUIP_ERR_OK)
            return result;

        ItemPosCountVec mainhandDest;
        return bot->CanStoreItem(NULL_BAG, NULL_SLOT, mainhandDest, mainhand, false);
    }
}

bool EquipAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();
    if (text == "?")
    {
        ListItems(requester);
        return true;
    }

    uint8 targetSlot = NULL_SLOT;
    if (text.find("mh ") == 0)
    {
        targetSlot = EQUIPMENT_SLOT_MAINHAND;
        text = text.substr(3);
    }
    else if (text.find("oh ") == 0)
    {
        targetSlot = EQUIPMENT_SLOT_OFFHAND;
        text = text.substr(3);
    }

    ItemIds ids = chat->parseItems(text);
    if (ids.empty())
    {
        //Get items based on text.
        std::list<Item*> found = ai->InventoryParseItems(text, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);

        //Sort items on itemLevel descending.
        found.sort([](Item* i, Item* j) {return i->GetProto()->ItemLevel > j->GetProto()->ItemLevel; });

        std::vector< uint16> dests;
        for (auto& item : found)
        {
            uint32 itemId = item->GetProto()->ItemId;
            if (std::find(ids.begin(), ids.end(), itemId) != ids.end())
            {
                continue;
            }

            uint16 dest;
            InventoryResult msg;
            if (targetSlot == NULL_SLOT && item->GetProto()->InventoryType == INVTYPE_AMMO)
            {
                msg = bot->CanUseAmmo(itemId);
                dest = uint16(-1);
            }
            else if ((targetSlot == NULL_SLOT || targetSlot == EQUIPMENT_SLOT_OFFHAND) &&
                     item->GetProto()->IsOffHandItem())
            {
                Item* mainhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
                if (mainhand && mainhand->GetProto()->InventoryType == INVTYPE_2HWEAPON)
                {
                    msg = CanEquipOffhandAfterTwoHand(bot, item);
                    dest = ((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_OFFHAND);
                }
                else
                    msg = bot->CanEquipItem(targetSlot, dest, item, true);
            }
            else
                msg = bot->CanEquipItem(targetSlot, dest, item, true);

            if (msg != EQUIP_ERR_OK)
            {
                continue;
            }

            if (std::find(dests.begin(), dests.end(), dest) != dests.end())
            {
                continue;
            }

            dests.push_back(dest);
            ids.insert(itemId);
        }
    }

    if (targetSlot != NULL_SLOT)
        return EquipItemsToSlot(requester, ids, targetSlot);

    return EquipItems(requester, ids);
}

void EquipAction::ListItems(Player* requester)
{
    ai->TellPlayer(requester, "=== Equip ===");

    std::map<uint32, int> items;
    std::map<uint32, bool> soulbound;
    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (pItem)
            {
                items[pItem->GetProto()->ItemId] += pItem->GetCount();
            }
        }
    }

    ai->InventoryTellItems(requester, items, soulbound);
}

bool EquipAction::EquipItems(Player* requester, ItemIds ids)
{
    bool didEquip = false;
    for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        didEquip = EquipItem(requester, &visitor) || didEquip;
    }

    return didEquip;
}

bool EquipAction::EquipItemsToSlot(Player* requester, ItemIds ids, uint8 targetSlot)
{
    bool didEquip = false;
    for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        std::list<Item*> items = visitor.GetResult();
        if (!items.empty())
            didEquip = EquipItemToSlot(requester, *items.begin(), targetSlot) || didEquip;
    }

    return didEquip;
}

bool EquipAction::EquipItem(Player* requester, FindItemVisitor* visitor)
{
    ai->InventoryIterateItems(visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    std::list<Item*> items = visitor->GetResult();
    if (items.empty())
        return false;

    return EquipItem(ai, requester, *items.begin());
}

//Return the bag slot with smallest bag
uint8 EquipAction::GetSmallestBagSlot(Player* bot)
{
    int8 curBag = 0;
    uint32 curSlots = 0;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        const Bag* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (pBag)
        {
            if (curBag > 0 && curSlots < pBag->GetBagSize())
            {
                continue;
            }
            
            curBag = bag;
            curSlots = pBag->GetBagSize();
        }
        else
        {
            return bag;
        }
    }

    return curBag;
}

bool EquipAction::EquipItemToSlot(Player* requester, Item* item, uint8 targetSlot)
{
    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    uint32 itemId = item->GetProto()->ItemId;

    Item* oldMainhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    const bool handlerUnequipsTwoHand = targetSlot == EQUIPMENT_SLOT_OFFHAND && item->GetProto()->IsOffHandItem() &&
        oldMainhand && oldMainhand->GetProto()->InventoryType == INVTYPE_2HWEAPON;

    Item* oldItem = nullptr;
    Item* otherOldItem = nullptr;
    uint16 itemPos = item->GetPos();
    uint16 oldItemPos = 0;
    uint16 otherOldItemPos = 0;

    if (handlerUnequipsTwoHand)
    {
        InventoryResult msg = CanEquipOffhandAfterTwoHand(bot, item);
        if (msg != EQUIP_ERR_OK)
        {
            bot->SendEquipError(msg, item, oldMainhand);
            return false;
        }

        oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        otherOldItem = oldMainhand;
        oldItemPos = oldItem ? oldItem->GetPos() : 0;
        otherOldItemPos = otherOldItem->GetPos();

        WorldPacket packet(CMSG_AUTOEQUIP_ITEM, 2);
        packet << bagIndex << slot;
        bot->GetSession()->HandleAutoEquipItemOpcode(packet);

        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND) != item)
        {
            if (item->GetPos() != itemPos)
                RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());
            if (oldItem && oldItem->GetPos() != oldItemPos)
                RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldItem).GetQualifier());
            if (otherOldItem->GetPos() != otherOldItemPos)
                RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(otherOldItem).GetQualifier());

            return false;
        }
    }
    else
    {
        uint16 dest;
        InventoryResult msg = bot->CanEquipItem(targetSlot, dest, item, true);
        if (msg != EQUIP_ERR_OK)
        {
            bot->SendEquipError(msg, item, nullptr);
            return false;
        }

        uint8 destSlot = dest & 0xFF;
        if (destSlot != targetSlot)
        {
            ai->TellPlayer(requester, "Cannot equip this item to the specified slot.");
            return false;
        }

        oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, targetSlot);
        if (destSlot == EQUIPMENT_SLOT_MAINHAND && item->GetProto()->InventoryType == INVTYPE_2HWEAPON)
            otherOldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        oldItemPos = oldItem ? oldItem->GetPos() : 0;
        otherOldItemPos = otherOldItem ? otherOldItem->GetPos() : 0;

        uint16 src = ((bagIndex << 8) | slot);
        uint16 dstPos = ((INVENTORY_SLOT_BAG_0 << 8) | targetSlot);

        bot->SwapItem(src, dstPos);

        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, targetSlot) != item)
        {
            if (item->GetPos() != itemPos)
                RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());
            if (oldItem && oldItem->GetPos() != oldItemPos)
                RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldItem).GetQualifier());
            if (otherOldItem && otherOldItem->GetPos() != otherOldItemPos)
                RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(otherOldItem).GetQualifier());

            return false;
        }
    }

    RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());

    if (oldItem)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldItem).GetQualifier());
    if (otherOldItem)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(otherOldItem).GetQualifier());

    sPlayerbotAIConfig.logEvent(ai, "EquipAction", item->GetProto()->Name1, std::to_string(item->GetProto()->ItemId));

    std::map<std::string, std::string> args;
    args["%item"] = chat->formatItem(item);
    ai->TellPlayer(requester, BOT_TEXT2("equip_command", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    return true;
}

bool EquipAction::EquipItem(PlayerbotAI* ai, Player* requester, Item* item, bool silent)
{
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();

    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    uint32 itemId = item->GetProto()->ItemId;

    if (bot->GetItemByPos(bagIndex, slot) != item)
    {
        bot->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, item, nullptr);
        return false;
    }

    Item* oldItem = nullptr;
    Item* otherOldItem = nullptr;
    bool equipped = false;

    if (item->GetProto()->InventoryType == INVTYPE_AMMO)
    {
        InventoryResult result = bot->CanUseAmmo(itemId);
        if (result != EQUIP_ERR_OK)
        {
            bot->SendEquipError(result, nullptr, nullptr, itemId);
            return false;
        }

        bot->SetAmmo(itemId);
        equipped = bot->GetUInt32Value(PLAYER_AMMO_ID) == itemId;
    }
    else if (item->GetProto()->Class == ITEM_CLASS_CONTAINER || item->GetProto()->Class == ITEM_CLASS_QUIVER)
    {
        uint8 newBagSlot = GetSmallestBagSlot(bot);

        // GetSmallestBagSlot hands back a free bag slot when there is one, and
        // otherwise the slot holding the smallest equipped bag. Displacing that
        // second kind only works while it is empty: _CanStoreItem_InSpecificSlot
        // refuses to move a non-empty bag (it is guarded as a dupe exploit) and
        // SwapItem reports nothing back, so the code below used to set
        // equipedBag = true for a swap that never happened - and the same
        // decision then fired again on the very next tick. One bot was seen
        // retrying about six times a second for hours, and every attempt logged
        // an anticheat entry. Leave the smaller bag alone until it empties.
        Item* const oldBag = newBagSlot > 0 ? bot->GetItemByPos(INVENTORY_SLOT_BAG_0, newBagSlot) : nullptr;
        const bool oldBagIsFull = oldBag && oldBag->IsBag() && !((Bag*)oldBag)->IsEmpty();

        if (newBagSlot == 0 || oldBagIsFull)
            return false;

        uint16 dest;
        InventoryResult result = bot->CanEquipItem(newBagSlot, dest, item, true);
        if (result != EQUIP_ERR_OK)
        {
            bot->SendEquipError(result, item, oldBag);
            return false;
        }

        if ((dest & 0xFF) != newBagSlot)
            return false;

        oldItem = oldBag;
        uint16 src = ((bagIndex << 8) | slot);

        if (newBagSlot == item->GetBagSlot()) //The new bag is in the slots of the old bag. Move it to the pack first.
        {
            uint16 packDest = ((INVENTORY_SLOT_BAG_0 << 8) | INVENTORY_SLOT_ITEM_START);
            bot->SwapItem(src, packDest);
            if (bot->GetItemByPos(packDest) != item)
                return false;

            src = packDest;
        }

        bot->SwapItem(src, dest);
        equipped = bot->GetItemByPos(dest) == item;
    }
    else
    {
        Item* oldMainhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        const bool handlerUnequipsTwoHand = item->GetProto()->IsOffHandItem() && oldMainhand &&
            oldMainhand->GetProto()->InventoryType == INVTYPE_2HWEAPON;

        uint16 dest = 0;
        if (handlerUnequipsTwoHand)
        {
            InventoryResult result = CanEquipOffhandAfterTwoHand(bot, item);
            if (result != EQUIP_ERR_OK)
            {
                bot->SendEquipError(result, item, oldMainhand);
                return false;
            }

            oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            otherOldItem = oldMainhand;
        }
        else
        {
            InventoryResult result = bot->CanEquipItem(NULL_SLOT, dest, item, true);
            if (result != EQUIP_ERR_OK)
            {
                bot->SendEquipError(result, item, nullptr);
                return false;
            }

            oldItem = bot->GetItemByPos(dest);
            if ((dest & 0xFF) == EQUIPMENT_SLOT_MAINHAND && item->GetProto()->InventoryType == INVTYPE_2HWEAPON)
                otherOldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        }

        uint16 itemPos = item->GetPos();
        uint16 oldItemPos = oldItem ? oldItem->GetPos() : 0;
        uint16 otherOldItemPos = otherOldItem ? otherOldItem->GetPos() : 0;

        WorldPacket packet(CMSG_AUTOEQUIP_ITEM, 2);
        packet << bagIndex << slot;
        bot->GetSession()->HandleAutoEquipItemOpcode(packet);

        if (handlerUnequipsTwoHand)
            equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND) == item;
        else
            equipped = bot->GetItemByPos(dest) == item;

        if (!equipped)
        {
            if (item->GetPos() != itemPos)
                RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());
            if (oldItem && oldItem->GetPos() != oldItemPos)
                RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldItem).GetQualifier());
            if (otherOldItem && otherOldItem->GetPos() != otherOldItemPos)
                RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(otherOldItem).GetQualifier());

            return false;
        }
    }

    if (!equipped)
        return false;

    RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());

    if (oldItem)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldItem).GetQualifier());
    if (otherOldItem)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(otherOldItem).GetQualifier());

    sPlayerbotAIConfig.logEvent(ai, "EquipAction", item->GetProto()->Name1, std::to_string(item->GetProto()->ItemId));

    if (!silent)
    {
        std::map<std::string, std::string> args;
        args["%item"] = ChatHelper::formatItem(item);
        ai->TellPlayer(requester, BOT_TEXT2("equip_command", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }

    return true;
}

bool EquipUpgradesAction::Execute(Event& event)
{
    if (!sPlayerbotAIConfig.autoEquipUpgradeLoot && !sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;

    if (event.getSource() == "trade status")
    {
        WorldPacket p(event.getPacket());
        p.rpos(0);
        uint32 status;
        p >> status;

        if (status != TRADE_STATUS_TRADE_ACCEPT)
        {
            return false;
        }
    }
    else if (event.getSource() == "item push result")
    {
        bool valid = false;
        WorldPacket& data = event.getPacket();
        if (!data.empty())
        {
            data.rpos(0);

            ObjectGuid guid;
            data >> guid;
            if (guid != bot->GetObjectGuid())
            {
                return false;
            }

            uint32 received, created, isShowChatMessage, slotId, itemId, suffixFactor, count;
            uint32 itemRandomPropertyId;
            //uint32 invCount;
            uint8 bagSlot;

            data >> received;                               // 0=looted, 1=from npc
            data >> created;                                // 0=received, 1=created
            data >> isShowChatMessage;                                      // IsShowChatMessage
            data >> bagSlot;
            // item slot, but when added to stack: 0xFFFFFFFF
            data >> slotId;
            data >> itemId;
            data >> suffixFactor;
            data >> itemRandomPropertyId;
            data >> count;
            // data >> invCount; // [-ZERO] count of items in inventory

            ItemQualifier itemQualifier(itemId, (int32)itemRandomPropertyId);
            const ItemPrototype* itemProto = itemQualifier.GetProto();
            if (itemProto && (itemProto->Class == ItemClass::ITEM_CLASS_WEAPON || 
                              itemProto->Class == ItemClass::ITEM_CLASS_ARMOR ||
                              itemProto->Class == ItemClass::ITEM_CLASS_CONTAINER))
            {
                valid = true;
            }
        }

        if (!valid)
        {
            return false;
        }
    }

    Item* oldMainhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    Item* oldOffhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        
    if (oldMainhand)
        UnequipAction::UnequipItem(ai, bot, oldMainhand, true);
    if (oldOffhand)
        UnequipAction::UnequipItem(ai, bot, oldOffhand, true);

    context->ClearExpiredValues("item usage", 10); //Clear old item usage.

    std::list<Item*> items;

    FindItemUsageVisitor visitor(bot, ItemUsage::ITEM_USAGE_EQUIP);
    ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    visitor.SetUsage(ItemUsage::ITEM_USAGE_BAD_EQUIP);
    ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    items = visitor.GetResult();

    bool didEquip = false;

    items.sort([plr = bot](Item* i, Item* j) {
        bool iMain = i->GetProto()->InventoryType == INVTYPE_WEAPONMAINHAND;
        bool jMain = j->GetProto()->InventoryType == INVTYPE_WEAPONMAINHAND;

        if (iMain != jMain)
            return iMain; // mainhand comes first

        return sRandomItemMgr.ItemStatWeight(plr, i) > sRandomItemMgr.ItemStatWeight(plr, j); });

    for (auto& item : items)
    {
#ifdef MANGOSBOT_TWO
        if (item->GetProto()->Class == ITEM_CLASS_GLYPH)
            continue;
#endif

        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());
        if (usage == ItemUsage::ITEM_USAGE_EQUIP || usage == ItemUsage::ITEM_USAGE_BAD_EQUIP)
        {
            std::string reason = ItemUsageValue::ReasonForNeed(usage, item, 1, bot);
            if (!EquipItem(ai, GetMaster(), item, item == oldMainhand || item == oldOffhand))
                continue;

            sLog.outDetail("Bot #%d <%s> auto equips item %d (%s)", bot->GetGUIDLow(), bot->GetName(), item->GetProto()->ItemId, usage == ItemUsage::ITEM_USAGE_EQUIP ? "better than current" : usage == ItemUsage::ITEM_USAGE_BAD_EQUIP ? "wrong item but empty slot" : "");
            ai->TellDebug(ai->GetMaster(), "Equipped: " + chat->formatItem(item) + " - " + reason, "debug equip");
            didEquip = true;
        }
    }

    // Check if main-hand has higher top-end damage than off-hand
    if (didEquip && bot->CanDualWield())
    {
        Item* mh = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        Item* oh = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);

        if (mh && oh
            && mh->GetProto()->Class == ITEM_CLASS_WEAPON
            && oh->GetProto()->Class == ITEM_CLASS_WEAPON
            && mh->GetProto()->InventoryType != INVTYPE_2HWEAPON)
        {
            float mhMaxDmg = mh->GetProto()->Damage[0].DamageMax;
            float ohMaxDmg = oh->GetProto()->Damage[0].DamageMax;

            if (ohMaxDmg > mhMaxDmg)
            {
                uint16 srcPos = ((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_MAINHAND);
                uint16 dstPos = ((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_OFFHAND);
                bot->SwapItem(srcPos, dstPos);

                sLog.outDetail("Bot #%d <%s> swapped MH/OH weapons to put higher top-end damage (%.1f) in main hand",
                    bot->GetGUIDLow(), bot->GetName(), ohMaxDmg);
            }
        }
    }

    return didEquip;
}
