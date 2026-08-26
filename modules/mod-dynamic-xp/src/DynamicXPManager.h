#ifndef MOD_DYNAMIC_XP_MANAGER_H
#define MOD_DYNAMIC_XP_MANAGER_H

#include "Common.h"

struct DynamicXPSettings
{
    bool enabled = false;
    float percentile = 0.75f;
    uint32 activeAccountDays = 14;
    float bonusPerLevelBelow = 0.05f;
    float penaltyPerLevelAbove = 0.02f;
    float minRate = 0.80f;
    float maxRate = 2.00f;
    uint32 refreshMinutes = 30;
};

class DynamicXPManager
{
public:
    static DynamicXPManager& Instance();

    void ReloadConfig();
    void RefreshProgressionLevel();
    void Update(uint32 diff);

    bool IsEnabled() const { return _settings.enabled; }
    uint8 GetProgressionLevel() const { return _progressionLevel; }
    float GetRate(uint32 characterLevel) const;
    DynamicXPSettings const& GetSettings() const { return _settings; }

private:
    DynamicXPManager() = default;

    bool CalculateProgressionLevel(uint8& progressionLevel) const;

    DynamicXPSettings _settings;
    uint8 _progressionLevel = 1;
    uint64 _elapsedMs = 0;
    uint64 _refreshIntervalMs = 30 * 60 * 1000;
};

#endif
