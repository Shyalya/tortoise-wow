/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearLiveBossValue.h"

#include <optional>

#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

DungeonClearLiveBoss DungeonClearLiveBossValue::Calculate()
{
    DungeonClearLiveBoss out;
    if (!bot || !bot->IsInWorld())
        return out;

    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next.has_value())
        return out;
    out.entry = next->entry;

    Map* map = bot->GetMap();
    if (!map)
        return out;

    // Single store scan, bounded by the 250ms cache interval. Callers turn the
    // stored GUID back into a live creature in O(1).
    for (auto const& kv : map->GetCreatureBySpawnIdStore())
    {
        Creature* c = kv.second;
        if (c && c->GetEntry() == next->entry && c->IsAlive())
        {
            out.guid = c->GetObjectGuid();
            break;
        }
    }
    return out;
}
