#include "Settings/DcSettings.h"
#include "Config/Config.h"
#include "Log.h"
#include <algorithm>

namespace
{
    float ClampPercent(float value, float fallback)
    {
        if (value < 0.0f || value > 100.0f)
            return fallback;
        return value;
    }
}

DcSettings& DcSettings::Instance()
{
    static DcSettings s;
    return s;
}

void DcSettings::Load()
{
    moduleEnabled = sConfig.GetBoolDefault("DungeonClear.Enabled", true);
    engageRange = sConfig.GetFloatDefault("DungeonClear.EngageRange", 25.0f);
    trashEngageRange = sConfig.GetFloatDefault("DungeonClear.TrashEngageRange", 18.0f);
    advanceArriveRange = sConfig.GetFloatDefault("DungeonClear.AdvanceArriveRange", 12.0f);
    int32 const configuredPullMode = sConfig.GetIntDefault("DungeonClear.PullMode", 0);
    defaultPullMode = configuredPullMode < 0 ? 0 : static_cast<uint8>(configuredPullMode % 3);
    int32 const configuredMaxPull = sConfig.GetIntDefault("DungeonClear.PullDynamicMaxLeeroyMobs", 3);
    pullDynamicMaxLeeroyMobs = configuredMaxPull < 1 ? 1
        : std::min<uint32>(20u, static_cast<uint32>(configuredMaxPull));
    int32 const configuredLootQuality = sConfig.GetIntDefault("DungeonClear.LootQualityMin", 2);
    lootQualityMin = configuredLootQuality < 0 ? 0
        : std::min<uint32>(6u, static_cast<uint32>(configuredLootQuality));
    restHealth = ClampPercent(sConfig.GetFloatDefault("DungeonClear.RestHealth", 60.0f), 60.0f);
    restMana = ClampPercent(sConfig.GetFloatDefault("DungeonClear.RestMana", 50.0f), 50.0f);
    postCombatRez = sConfig.GetBoolDefault("DungeonClear.PostCombatRez", true);
    int32 const configuredSweep = sConfig.GetIntDefault("DungeonClear.StrategyGateSweepMs", 3000);
    strategyGateSweepMs = configuredSweep < 250 ? 250u : static_cast<uint32>(configuredSweep);
    preventBotRelease = sConfig.GetBoolDefault("DungeonClear.PreventBotRelease", true);
    objectiveArriveRadius = sConfig.GetFloatDefault("DungeonClear.ObjectiveArriveRadius", 10.0f);
    partyMaxSpread = sConfig.GetFloatDefault("DungeonClear.PartyMaxSpread", 25.0f);
    if (partyMaxSpread < 10.0f || partyMaxSpread > 60.0f)
        partyMaxSpread = 25.0f;
    sLog.outString("DungeonClear: settings loaded (enabled=%u pullMode=%u)", moduleEnabled ? 1 : 0, defaultPullMode);
}
