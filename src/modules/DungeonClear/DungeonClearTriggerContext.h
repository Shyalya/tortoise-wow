#pragma once
#include "playerbot/strategy/NamedObjectContext.h"
#include "Trigger/DungeonClearTriggers.h"
#include "playerbot/strategy/triggers/ChatCommandTrigger.h"

namespace ai
{
    class DungeonClearTriggerContext : public NamedObjectContext<Trigger>
    {
    public:
        DungeonClearTriggerContext()
        {
            creators["dungeon clear party died"] = [](PlayerbotAI* ai) { return new DungeonClearPartyDiedTrigger(ai); };
            creators["dungeon clear all cleared"] = [](PlayerbotAI* ai) { return new DungeonClearAllClearedTrigger(ai); };
            creators["dungeon clear at boss"] = [](PlayerbotAI* ai) { return new DungeonClearAtBossTrigger(ai); };
            creators["dungeon clear at objective"] = [](PlayerbotAI* ai) { return new DungeonClearAtObjectiveTrigger(ai); };
            creators["dungeon clear blocking trash"] = [](PlayerbotAI* ai) { return new DungeonClearBlockingTrashTrigger(ai); };
            creators["dungeon clear need advance"] = [](PlayerbotAI* ai) { return new DungeonClearNeedAdvanceTrigger(ai); };
            creators["dungeon clear follow tank"] = [](PlayerbotAI* ai) { return new DungeonClearFollowTankTrigger(ai); };
            creators["dungeon clear assist tank"] = [](PlayerbotAI* ai) { return new DungeonClearAssistTankTrigger(ai); };
            creators["dungeon clear pull"] = [](PlayerbotAI* ai) { return new DungeonClearPullTrigger(ai); };
            creators["dungeon clear event due"] = [](PlayerbotAI* ai) { return new DungeonClearEventDueTrigger(ai); };
            creators["dungeon clear rez party"] = [](PlayerbotAI* ai) { return new DungeonClearRezPartyTrigger(ai); };
            creators["dungeon clear filter loot"] = [](PlayerbotAI* ai) { return new DungeonClearFilterLootTrigger(ai); };
            creators["dc on"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc on"); };
            creators["dc off"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc off"); };
            creators["dc pause"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc pause"); };
            creators["dc skip"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc skip"); };
            creators["dc pull"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc pull"); };
            creators["dc status"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc status"); };
            creators["dc bosses"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc bosses"); };
        }
    };
}
