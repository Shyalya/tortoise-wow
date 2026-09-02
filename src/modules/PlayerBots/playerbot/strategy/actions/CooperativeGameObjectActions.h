#pragma once

#include "MovementActions.h"
#include "playerbot/strategy/CooperativeObjectPolicy.h"

namespace ai
{
class UseCooperativeGameObjectAction : public MovementAction
{
public:
    explicit UseCooperativeGameObjectAction(PlayerbotAI* ai) : MovementAction(ai, "use cooperative game object") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
    bool HasPendingIntent();
    ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_NONE; }

private:
    ObjectGuid targetGuid;
    time_t intentAt = 0;
    std::map<ObjectGuid, std::pair<time_t, uint8> > attempts;
    std::map<ObjectGuid, time_t> completed;
    bool IsEligible(GameObject* go) const;
};
}
