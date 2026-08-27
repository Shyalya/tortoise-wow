// Resolves the group's authoritative tank for follow/assist purposes:
// delegates to DcUtil::FindEnabledTank (the tank-role party member currently
// running an enabled DungeonClear) and exposes its guid, or an empty guid
// when there isn't one yet.
#pragma once

#include "playerbot/strategy/Value.h"
#include "DcValueKeys.h"
#include "ObjectGuid.h"

class Player;

namespace ai
{
    class DungeonClearPartyTankValue : public CalculatedValue<ObjectGuid>
    {
    public:
        DungeonClearPartyTankValue(PlayerbotAI* ai) : CalculatedValue<ObjectGuid>(ai, DcKey::PartyTank, 2) {}

        std::string Format() override;

    protected:
        ObjectGuid Calculate() override;
    };
}
