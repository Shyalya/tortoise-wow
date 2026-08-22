/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_BOSSSPAWNINDEX_H
#define _PLAYERBOT_BOSSSPAWNINDEX_H

#include <unordered_map>
#include <vector>

#include "Common.h"
#include "DBCEnums.h"
#include "DungeonBossInfo.h"

class BossSpawnIndex
{
public:
    static std::vector<DungeonBossInfo> const& Get(uint32 mapId, Difficulty difficulty);

private:
    static void EnsureBuilt();
    static void Build();

    static std::unordered_map<uint64, std::vector<DungeonBossInfo>> _store;
    static bool _built;

    static uint64 MakeKey(uint32 mapId, Difficulty difficulty)
    {
        return (static_cast<uint64>(mapId) << 32) | static_cast<uint32>(difficulty);
    }
};

#endif
