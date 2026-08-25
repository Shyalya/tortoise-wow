#include "Settings/DcSettings.h"
#include "Config/Config.h"
#include "Log.h"

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
    defaultPullMode = static_cast<uint8>(sConfig.GetIntDefault("DungeonClear.PullMode", 0));
    pullDynamicMaxLeeroyMobs = static_cast<uint32>(sConfig.GetIntDefault("DungeonClear.PullDynamicMaxLeeroyMobs", 3));
    lootQualityMin = static_cast<uint32>(sConfig.GetIntDefault("DungeonClear.LootQualityMin", 2));
    restHealth = sConfig.GetFloatDefault("DungeonClear.RestHealth", 60.0f);
    restMana = sConfig.GetFloatDefault("DungeonClear.RestMana", 50.0f);
    postCombatRez = sConfig.GetBoolDefault("DungeonClear.PostCombatRez", true);
    strategyGateSweepMs = static_cast<uint32>(sConfig.GetIntDefault("DungeonClear.StrategyGateSweepMs", 3000));
    preventBotRelease = sConfig.GetBoolDefault("DungeonClear.PreventBotRelease", true);
    objectiveArriveRadius = sConfig.GetFloatDefault("DungeonClear.ObjectiveArriveRadius", 10.0f);
    sLog.outString("DungeonClear: settings loaded (enabled=%u pullMode=%u)", moduleEnabled ? 1 : 0, defaultPullMode);
}
