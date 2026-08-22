/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARROOMTRASHVALUE_H
#define _PLAYERBOT_DUNGEONCLEARROOMTRASHVALUE_H

#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;

// "dungeon clear room trash remaining": the alive, attackable hostiles that
// must be cleared BEFORE pulling a room-aggro boss (RoomAggroRegistry). The set
// is everything within the boss's scripted room radius — minus the boss, any
// other encounter boss, and anything glued so tightly to the boss it can't be
// pulled without waking it (the boss-aggro sphere) — that the tank can actually
// reach with no closed door between. Empty when the next boss isn't flagged, the
// boss isn't loaded, the room is clear, or the room-clear was given up on
// (RoomClearTimeout). The driving gate (DungeonClearAtBossTrigger) holds the
// boss pull while this is non-empty; the pull pipeline / room-clear action
// target the nearest member.
//
// 500ms poll (mirrors "dungeon clear far targets", whose result it filters). The
// give-up timeout state lives as members on the long-lived value object and
// self-resets when the boss changes or the run ends, so it needs no external
// reset wiring.
class DungeonClearRoomTrashValue : public CalculatedValue<GuidVector>
{
public:
    DungeonClearRoomTrashValue(PlayerbotAI* botAI)
        : CalculatedValue<GuidVector>(botAI, DcKey::RoomTrashRemaining, 500)
    {
    }

protected:
    GuidVector Calculate() override;

private:
    // No-progress give-up bookkeeping (see Calculate). bossEntry detects a boss
    // change so the latch self-resets across bosses/runs without external wiring.
    uint32 trackedBoss = 0;
    uint32 lastRemaining = 0;
    uint32 lastProgressMs = 0;
    bool   gaveUp = false;
    bool   noteSent = false;
    // Last kept-count we emitted the exclusion-breakdown diagnostic for; the log
    // fires only on a change. SIZE_MAX so the first computation always logs.
    size_t lastLoggedKept = static_cast<size_t>(-1);
};

#endif
