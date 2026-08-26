#pragma once
#include "playerbot/strategy/NamedObjectContext.h"
#include "Value/DungeonClearStateValues.h"
#include "Value/DungeonBossesValue.h"
#include "Value/NextDungeonBossValue.h"
#include "Value/DungeonClearPartyTankValue.h"

namespace ai
{
    class DungeonClearValueContext : public NamedObjectContext<UntypedValue>
    {
    public:
        DungeonClearValueContext()
        {
            creators[DcKey::RunState] = [](PlayerbotAI* ai) { return new DungeonClearRunStateValue(ai); };
            creators[DcKey::Skipped] = [](PlayerbotAI* ai) { return new DungeonClearSkippedValue(ai); };
            creators[DcKey::PullMode] = [](PlayerbotAI* ai) { return new DungeonClearPullModeValue(ai); };
            creators[DcKey::StallReason] = [](PlayerbotAI* ai) { return new DungeonClearStallReasonValue(ai); };
            creators[DcKey::ClearedAnchors] = [](PlayerbotAI* ai) { return new DungeonClearClearedAnchorsValue(ai); };
            creators[DcKey::EventProgress] = [](PlayerbotAI* ai) { return new DungeonClearEventProgressValue(ai); };
            creators[DcKey::EventStartedAt] = [](PlayerbotAI* ai) { return new DungeonClearEventStartedAtValue(ai); };
            creators[DcKey::EventStepStartedAt] = [](PlayerbotAI* ai) { return new DungeonClearEventStepStartedAtValue(ai); };
            creators[DcKey::DungeonBosses] = [](PlayerbotAI* ai) { return new DungeonBossesValue(ai); };
            creators[DcKey::NextDungeonBoss] = [](PlayerbotAI* ai) { return new NextDungeonBossValue(ai); };
            creators[DcKey::PartyTank] = [](PlayerbotAI* ai) { return new DungeonClearPartyTankValue(ai); };
        }
    };
}
