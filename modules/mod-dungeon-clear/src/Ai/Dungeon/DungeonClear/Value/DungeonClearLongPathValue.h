/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARLONGPATHVALUE_H
#define _PLAYERBOT_DUNGEONCLEARLONGPATHVALUE_H

#include "Value.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;

// Cached chunked path from the bot to the next boss. Advance reads this
// every tick to pick a hop; corridor scans walk its polyline.
//
// Lifetime is bot-owned via ManualSetValue's `data` field. The strategy
// invalidates the cache by setting `dungeon clear long path target` to 0
// (boss-change, dc-skip, dc-on) or by calling Reset() on this value
// (dc-off, party death). Advance also invalidates on cache TTL expiry.
class DungeonClearLongPathValue : public ManualSetValue<ChunkedPathfinder::Result&>
{
public:
    DungeonClearLongPathValue(PlayerbotAI* botAI)
        : ManualSetValue<ChunkedPathfinder::Result&>(botAI, data, DcKey::LongPath)
    {
    }

    void Reset() override
    {
        data = ChunkedPathfinder::Result{};
    }

private:
    ChunkedPathfinder::Result data;
};

#endif
