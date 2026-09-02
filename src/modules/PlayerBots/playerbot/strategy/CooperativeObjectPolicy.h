#pragma once

#include "playerbot/PlayerbotAIConfig.h"
class PlayerbotAI;

namespace ai
{
// Policy is deliberately small: strict never performs unsolicited cooperative
// interactions, assist performs group-safe rituals/events, and independent is
// reserved for bots without a real player controller.
enum class CooperativeObjectPolicy
{
    Strict,
    Assist,
    Independent
};

CooperativeObjectPolicy GetCooperativeObjectPolicy(PlayerbotAI* ai);
bool AllowsCooperativeObjectUse(PlayerbotAI* ai);
}
