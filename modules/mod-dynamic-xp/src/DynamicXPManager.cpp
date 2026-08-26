#include "DynamicXPManager.h"

#include "Database/DatabaseEnv.h"
#include "Config/Config.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>
#include <vector>

namespace
{
    float ReadNonNegativeFloat(char const* key, float defaultValue)
    {
        float value = sConfig.GetFloatDefault(key, defaultValue);
        return std::isfinite(value) && value >= 0.0f ? value : defaultValue;
    }

    float ReadClampedFloat(char const* key, float defaultValue, float minValue, float maxValue)
    {
        float value = sConfig.GetFloatDefault(key, defaultValue);
        if (!std::isfinite(value))
            value = defaultValue;
        return std::clamp(value, minValue, maxValue);
    }

    uint32 ReadNonNegativeInt(char const* key, uint32 defaultValue)
    {
        int32 value = sConfig.GetIntDefault(key, static_cast<int32>(defaultValue));
        return value < 0 ? 0 : static_cast<uint32>(value);
    }
}

DynamicXPManager& DynamicXPManager::Instance()
{
    static DynamicXPManager manager;
    return manager;
}

void DynamicXPManager::ReloadConfig()
{
    _settings.enabled = sConfig.GetBoolDefault("DynamicXP.Enable", true);
    _settings.percentile = ReadClampedFloat("DynamicXP.Percentile", 0.75f, 0.0f, 1.0f);
    _settings.activeAccountDays = ReadNonNegativeInt("DynamicXP.ActiveAccountDays", 14);
    _settings.bonusPerLevelBelow = ReadNonNegativeFloat("DynamicXP.BonusPerLevelBelow", 0.05f);
    _settings.penaltyPerLevelAbove = ReadNonNegativeFloat("DynamicXP.PenaltyPerLevelAbove", 0.02f);
    // Keep the neutral rate inside the clamp interval so a character exactly
    // at the progression level always remains at 1.0x.
    _settings.minRate = std::min(ReadNonNegativeFloat("DynamicXP.MinRate", 0.80f), 1.0f);
    _settings.maxRate = std::max(ReadNonNegativeFloat("DynamicXP.MaxRate", 2.00f), 1.0f);
    _settings.refreshMinutes = ReadNonNegativeInt("DynamicXP.RefreshMinutes", 30);

    if (_settings.minRate > _settings.maxRate)
        std::swap(_settings.minRate, _settings.maxRate);

    if (_settings.refreshMinutes == 0)
        _settings.refreshMinutes = 1;

    _refreshIntervalMs = static_cast<uint64>(_settings.refreshMinutes) * 60 * 1000;
    _elapsedMs = _refreshIntervalMs;

    if (!_settings.enabled)
        _progressionLevel = 1;
}

void DynamicXPManager::Update(uint32 diff)
{
    if (!_settings.enabled)
        return;

    _elapsedMs += diff;
    if (_elapsedMs < _refreshIntervalMs)
        return;

    _elapsedMs %= _refreshIntervalMs;
    RefreshProgressionLevel();
}

float DynamicXPManager::GetRate(uint32 characterLevel) const
{
    int32 const difference = static_cast<int32>(_progressionLevel) - static_cast<int32>(characterLevel);
    float rate = 1.0f;

    if (difference > 0)
        rate += static_cast<float>(difference) * _settings.bonusPerLevelBelow;
    else if (difference < 0)
        rate -= static_cast<float>(-difference) * _settings.penaltyPerLevelAbove;

    return std::clamp(rate, _settings.minRate, _settings.maxRate);
}

bool DynamicXPManager::CalculateProgressionLevel(uint8& progressionLevel) const
{
    std::unordered_set<uint32> activeAccounts;
    // Random playerbots use the RNDBOT account prefix; %% emits SQL's % in PQuery.
    std::unique_ptr<QueryResult> activeAccountResult(LoginDatabase.PQuery(
        "SELECT `id` FROM `account` "
        "WHERE `last_login` >= NOW() - INTERVAL %u DAY "
        "AND UPPER(`username`) NOT LIKE 'RNDBOT%%'",
        _settings.activeAccountDays));

    if (activeAccountResult)
    {
        do
        {
            activeAccounts.insert(activeAccountResult->Fetch()[0].GetUInt32());
        } while (activeAccountResult->NextRow());
    }

    if (activeAccounts.empty())
    {
        progressionLevel = 1;
        return true;
    }

    std::unique_ptr<QueryResult> characterResult(CharacterDatabase.Query(
        "SELECT `account`, MAX(`level`) FROM `characters` "
        "WHERE `deleteDate` IS NULL GROUP BY `account`"));

    if (!characterResult)
    {
        progressionLevel = 1;
        return true;
    }

    std::vector<uint32> levels;
    do
    {
        Field* fields = characterResult->Fetch();
        uint32 const accountId = fields[0].GetUInt32();
        uint32 const level = fields[1].GetUInt32();

        if (activeAccounts.find(accountId) != activeAccounts.end() && level > 0)
            levels.push_back(level);
    } while (characterResult->NextRow());

    if (levels.empty())
    {
        progressionLevel = 1;
        return true;
    }

    std::sort(levels.begin(), levels.end());

    double const position = static_cast<double>(levels.size() - 1) * _settings.percentile;
    size_t const lowerIndex = static_cast<size_t>(std::floor(position));
    size_t const upperIndex = std::min(lowerIndex + 1, levels.size() - 1);
    double const fraction = position - static_cast<double>(lowerIndex);
    double const percentileLevel = static_cast<double>(levels[lowerIndex]) +
        (static_cast<double>(levels[upperIndex]) - static_cast<double>(levels[lowerIndex])) * fraction;

    progressionLevel = static_cast<uint8>(std::clamp<int64>(
        static_cast<int64>(std::lround(percentileLevel)), 1, 255));
    return true;
}

void DynamicXPManager::RefreshProgressionLevel()
{
    if (!_settings.enabled)
        return;

    uint8 progressionLevel = 1;
    if (CalculateProgressionLevel(progressionLevel))
        _progressionLevel = progressionLevel;

    _elapsedMs = 0;
}
