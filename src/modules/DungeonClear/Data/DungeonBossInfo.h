#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "Platform/Define.h"

enum class DungeonAnchorKind : uint8
{
    Boss = 0,
    Objective = 1
};

struct DungeonBossInfo
{
    uint32 entry = 0;
    uint32 encounterIndex = 0;
    std::string name;
    uint32 mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    DungeonAnchorKind kind = DungeonAnchorKind::Boss;
    float arriveRadius = 0.0f;
    uint32 gateEntry = 0;
    uint32 eventId = 0;
    int32 orderOverride = -1;
    bool spawnOnApproach = false;
    std::vector<uint32> alternateEntries;
};

inline uint32 BossOrderKey(DungeonBossInfo const& b)
{
    return b.orderOverride >= 0 ? static_cast<uint32>(b.orderOverride) : b.encounterIndex;
}

inline bool DungeonBossMatchesEntry(DungeonBossInfo const& boss, uint32 entry)
{
    if (boss.entry == entry)
        return true;
    return std::find(boss.alternateEntries.begin(), boss.alternateEntries.end(), entry)
        != boss.alternateEntries.end();
}

// Entries are not unique in a few vanilla encounters (and event bosses can
// use a different runtime creature entry), so state must be keyed by the
// encounter rather than by creature entry alone.
inline uint32 DungeonBossStateKey(DungeonBossInfo const& boss)
{
    uint32 hash = 2166136261u;
    auto mix = [&hash](uint32 value)
    {
        for (uint32 shift = 0; shift < 32; shift += 8)
        {
            hash ^= (value >> shift) & 0xffu;
            hash *= 16777619u;
        }
    };

    mix(boss.entry);
    mix(boss.encounterIndex);
    mix(static_cast<uint32>(boss.orderOverride + 1));
    return 0x80000000u | (hash & 0x7fffffffu);
}
