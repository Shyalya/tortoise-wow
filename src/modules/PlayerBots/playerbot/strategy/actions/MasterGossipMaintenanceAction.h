#pragma once
#include "GenericActions.h"

namespace ai
{
    class MasterGossipMaintenanceAction : public Action
    {
    public:
        MasterGossipMaintenanceAction(PlayerbotAI* ai) : Action(ai, "master gossip maintenance") {}

        bool Execute(Event& event) override;
        bool isUseful() override;

    private:
        bool IsEligibleBot(Player* master) const;
        bool IsDuplicateInteraction(ObjectGuid npcGuid) const;
        void RememberInteraction(ObjectGuid npcGuid) const;
    };
}
