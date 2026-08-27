#include "playerbot/playerbot.h"
#include "MasterGossipMaintenanceAction.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/values/ItemUsageValue.h"

using namespace ai;

namespace
{
    static std::string const LAST_MASTER_GOSSIP_NPC = "last master gossip maintenance npc";
    static std::string const LAST_MASTER_GOSSIP_TIME = "last master gossip maintenance time";
    static time_t const MASTER_GOSSIP_DEBOUNCE_SECONDS = 2;
}

bool MasterGossipMaintenanceAction::Execute(Event& event)
{
    Player* master = GetMaster();
    if (!IsEligibleBot(master))
        return false;

    ObjectGuid npcGuid = event.getObject();
    if (!npcGuid || IsDuplicateInteraction(npcGuid))
        return false;

    bool canSell = false;
    bool canRepair = false;

    if (bot->GetNPCIfCanInteractWith(npcGuid, UNIT_NPC_FLAG_VENDOR))
        canSell = AI_VALUE2(uint32, "item count", "usage " + std::to_string((uint8)ItemUsage::ITEM_USAGE_VENDOR)) > 0;

    if (bot->GetNPCIfCanInteractWith(npcGuid, UNIT_NPC_FLAG_REPAIR))
        canRepair = AI_VALUE(uint8, "durability inventory") < 100;

    if (!canSell && !canRepair)
        return false;

    RememberInteraction(npcGuid);

    bool didSomething = false;

    if (canSell)
        didSomething |= ai->DoSpecificAction("sell", Event(getName(), npcGuid), true);

    if (canRepair)
        didSomething |= ai->DoSpecificAction("repair", Event(getName(), npcGuid), true);

    return didSomething;
}

bool MasterGossipMaintenanceAction::isUseful()
{
    return sPlayerbotAIConfig.autoMaintenanceOnMasterVendor && ai->HasRealPlayerMaster();
}

bool MasterGossipMaintenanceAction::IsEligibleBot(Player* master) const
{
    if (!sPlayerbotAIConfig.autoMaintenanceOnMasterVendor || !master || !ai->HasRealPlayerMaster())
        return false;

    if (!bot->GetGroup() || !bot->GetGroup()->IsMember(master->GetObjectGuid()))
        return false;

    if (!ai->IsSafe(master) || !bot->IsAlive() || bot->IsInCombat() || bot->IsTaxiFlying() || bot->IsBeingTeleported())
        return false;

    if (bot->GetTradeData() || bot->GetTrader())
        return false;

    return true;
}

bool MasterGossipMaintenanceAction::IsDuplicateInteraction(ObjectGuid npcGuid) const
{
    uint32 lastNpcGuid = AI_VALUE2(uint32, "manual int", LAST_MASTER_GOSSIP_NPC);
    time_t nextAllowedTime = AI_VALUE2(time_t, "manual time", LAST_MASTER_GOSSIP_TIME);

    return lastNpcGuid == npcGuid.GetCounter() && nextAllowedTime > time(0);
}

void MasterGossipMaintenanceAction::RememberInteraction(ObjectGuid npcGuid) const
{
    SET_AI_VALUE2(uint32, "manual int", LAST_MASTER_GOSSIP_NPC, npcGuid.GetCounter());
    SET_AI_VALUE2(time_t, "manual time", LAST_MASTER_GOSSIP_TIME, time(0) + MASTER_GOSSIP_DEBOUNCE_SECONDS);
}
