#include "Multiplier/DungeonClearMultiplier.h"
#include "playerbot/playerbot.h"
#include "Util/DungeonClearUtil.h"
#include "DcValueKeys.h"

using namespace ai;

float DungeonClearMultiplier::GetValue(Action* action)
{
    if (!action || !DcUtil::IsEnabledRun(bot))
        return 1.0f;
    if (DcUtil::IsPausedRun(bot))
    {
        // While paused, suppress DC driving actions but allow chat / follow.
        std::string n = action->getName();
        if (n.find("dungeon clear") == 0
            && n.find("follow") == std::string::npos
            && n != "dungeon clear recover")
            return 0.0f;
    }
    return 1.0f;
}

float DungeonClearCombatMultiplier::GetValue(Action* action)
{
    if (!action || !DcUtil::IsEnabledRun(bot))
        return 1.0f;
    // Soften "drop target" so assist can hold.
    if (action->getName() == "drop target")
        return 0.5f;
    return 1.0f;
}
