#pragma once
#include "playerbot/strategy/Trigger.h"
namespace ai { class CooperativeGameObjectTrigger : public Trigger { public: explicit CooperativeGameObjectTrigger(PlayerbotAI* ai) : Trigger(ai, "cooperative game object", 1) {} bool IsActive() override; }; }
