/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARBLOCKINGDOORVALUE_H
#define _PLAYERBOT_DUNGEONCLEARBLOCKINGDOORVALUE_H

#include "ObjectGuid.h"
#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;

// Returns the GUID of a closed `GAMEOBJECT_TYPE_DOOR` whose geometry sits
// within ~5yd of the next leg of the cached `dungeon clear long path`,
// looking ~80yd ahead from the bot's current position. Empty GUID means
// "no closed door blocking the corridor".
//
// Used by the door-blocked trigger to stall Advance with a specific reason
// in party chat instead of having the bot ram a closed door until the
// position-stuck detector trips.
class DungeonClearBlockingDoorValue : public CalculatedValue<ObjectGuid>
{
public:
    // checkInterval 500ms — door state changes are player- or boss-driven
    // events, not per-tick. The grid walk over m_gameobjectBySpawnIdStore
    // is cheap (small dungeon maps) but cheap × 50ms is still wasted CPU.
    DungeonClearBlockingDoorValue(PlayerbotAI* botAI)
        : CalculatedValue<ObjectGuid>(botAI, DcKey::BlockingDoor, 500)
    {
    }

protected:
    ObjectGuid Calculate() override;

private:
    // Last GUID this value flagged, for change-only INFO logging in
    // Calculate (a flag switching door→door mid-run is the signature of a
    // phantom blocker and must be visible without debug logs).
    ObjectGuid _lastFlagged;
};

#endif
