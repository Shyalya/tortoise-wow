#pragma once
#include <string>
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
};

inline uint32 BossOrderKey(DungeonBossInfo const& b)
{
    return b.orderOverride >= 0 ? static_cast<uint32>(b.orderOverride) : b.encounterIndex;
}
