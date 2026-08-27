#pragma once
#include "playerbot/strategy/NamedObjectContext.h"
#include "Action/DungeonClearActions.h"
#include "Action/DungeonClearChatActions.h"

namespace ai
{
    class DungeonClearActionContext : public NamedObjectContext<Action>
    {
    public:
        DungeonClearActionContext()
        {
            creators["dungeon clear advance"] = [](PlayerbotAI* ai) { return new DungeonClearAdvanceAction(ai); };
            creators["dungeon clear engage trash"] = [](PlayerbotAI* ai) { return new DungeonClearEngageTrashAction(ai); };
            creators["dungeon clear engage boss"] = [](PlayerbotAI* ai) { return new DungeonClearEngageBossAction(ai); };
            creators["dungeon clear objective arrive"] = [](PlayerbotAI* ai) { return new DungeonClearObjectiveArriveAction(ai); };
            creators["dungeon clear run event"] = [](PlayerbotAI* ai) { return new DungeonClearRunEventAction(ai); };
            creators["dungeon clear follow tank"] = [](PlayerbotAI* ai) { return new DungeonClearFollowTankAction(ai); };
            creators["dungeon clear assist tank"] = [](PlayerbotAI* ai) { return new DungeonClearAssistTankAction(ai); };
            creators["dungeon clear disable on death"] = [](PlayerbotAI* ai) { return new DungeonClearDisableOnDeathAction(ai); };
            creators["dungeon clear recover"] = [](PlayerbotAI* ai) { return new DungeonClearRecoverAction(ai); };
            creators["dungeon clear rest party"] = [](PlayerbotAI* ai) { return new DungeonClearRestPartyAction(ai); };
            creators["dungeon clear regroup"] = [](PlayerbotAI* ai) { return new DungeonClearRegroupAction(ai); };
            creators["dungeon clear disable on cleared"] = [](PlayerbotAI* ai) { return new DungeonClearDisableOnClearedAction(ai); };
            creators["dungeon clear rez party"] = [](PlayerbotAI* ai) { return new DungeonClearRezPartyAction(ai); };
            creators["dungeon clear filter loot"] = [](PlayerbotAI* ai) { return new DungeonClearFilterLootAction(ai); };
            creators["dungeon clear pull"] = [](PlayerbotAI* ai) { return new DungeonClearPullAction(ai); };
            creators["dc on"] = [](PlayerbotAI* ai) { return new DcOnAction(ai); };
            creators["dc off"] = [](PlayerbotAI* ai) { return new DcOffAction(ai); };
            creators["dc pause"] = [](PlayerbotAI* ai) { return new DcPauseAction(ai); };
            creators["dc skip"] = [](PlayerbotAI* ai) { return new DcSkipAction(ai); };
            creators["dc pull"] = [](PlayerbotAI* ai) { return new DcPullModeAction(ai); };
            creators["dc status"] = [](PlayerbotAI* ai) { return new DcStatusAction(ai); };
            creators["dc bosses"] = [](PlayerbotAI* ai) { return new DcBossesAction(ai); };
            creators["dc go"] = [](PlayerbotAI* ai) { return new DcGoAction(ai); };
        }
    };
}
