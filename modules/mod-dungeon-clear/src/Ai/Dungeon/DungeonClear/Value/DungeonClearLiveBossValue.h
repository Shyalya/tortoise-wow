/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARLIVEBOSSVALUE_H
#define _PLAYERBOT_DUNGEONCLEARLIVEBOSSVALUE_H

#include "ObjectGuid.h"
#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;

// Cached identity of the live boss creature for the CURRENT "next dungeon
// boss" entry, refreshed on a short interval. Resolving the boss creature
// otherwise costs a full linear scan of the map's creature store, and the
// trigger ladder plus the advance action each do that several times per tick
// for the same boss. Caching only the GUID (not a raw Creature*) lets callers
// re-resolve it in O(1) via ObjectAccessor every call (see
// DcTargeting::GetLiveBoss): the returned position stays live and we never
// hold a pointer that could dangle if the creature despawns between ticks.
struct DungeonClearLiveBoss
{
    uint32 entry{0};   // the next-boss entry this lookup was computed for
    ObjectGuid guid;   // an alive creature of that entry; empty if none loaded
};

class DungeonClearLiveBossValue : public CalculatedValue<DungeonClearLiveBoss>
{
public:
    DungeonClearLiveBossValue(PlayerbotAI* botAI)
        // 250ms: long enough that the store scan doesn't run every tick, short
        // enough that engage gating and final-approach pursuit track a moving
        // boss well within DC_ENGAGE_RANGE (a patrolling boss covers <2yd in
        // 250ms). Callers re-resolve the GUID to a live position each tick, so
        // this interval bounds only how often the scan re-runs.
        : CalculatedValue<DungeonClearLiveBoss>(botAI, DcKey::LiveBoss, 250)
    {
    }

protected:
    DungeonClearLiveBoss Calculate() override;
};

#endif
