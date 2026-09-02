/*
 * GroupQuestLoot shares quest-relevant loot with the group while leaving
 * normal loot generation and per-player eligibility checks in the core.
 */

#include "LootMgr.h"
#include "ObjectMgr.h"

namespace
{
bool IsGroupQuestLoot(LootStoreItem const& item)
{
    if (item.needs_quest)
        return true;

    ItemPrototype const* itemPrototype = sObjectMgr.GetItemPrototype(item.itemid);
    return itemPrototype && itemPrototype->StartQuest != 0 &&
        sObjectMgr.GetQuestTemplate(itemPrototype->StartQuest) != nullptr;
}

struct GroupQuestLootRegistration
{
    GroupQuestLootRegistration()
    {
        RegisterQuestLootSharingPolicy(&IsGroupQuestLoot);
    }
};

GroupQuestLootRegistration const groupQuestLootRegistration;
}
