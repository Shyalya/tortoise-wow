/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARPARTYTANKVALUE_H
#define _PLAYERBOT_DUNGEONCLEARPARTYTANKVALUE_H

#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;
class Player;

// Returns the elected leader tank (DcLeaderSignal::FindLeaderTank) while its
// dungeon-clear mode is enabled and unpaused, or nullptr otherwise. Used by all
// non-leader bots — non-tanks and non-leader (off-)tanks in a raid alike — to
// redirect their follow target to the leader for the duration of the clear
// instead of trailing the player master. For the leader bot itself it resolves
// to the bot, which is how the driving ladder and follow-tank trigger tell the
// single leader apart from the followers it leads.
//
// UNCACHED (checkInterval 1) ON PURPOSE — do not "optimise" it back to a TTL.
// CalculatedValue stores the raw `Player*` it last computed, so any interval > 1
// hands out a pointer nobody revalidates for the length of the TTL. A tank that
// logs out, is despawned, or leaves the map inside that window leaves every
// follower holding a dangling or foreign-thread pointer, and the follow-tank rung
// then WRITES through it (MotionMaster::MoveFollow -> Unit::FollowerAdded, which
// checks only for null) — a SIGSEGV in the follow target's m_followingMe, reported
// as issue #20. Calculate() re-resolves through ObjectAccessor every read, and
// FindLeaderTank keeps its own 250 ms election memo, so the recompute is a couple
// of hash lookups, not a group walk.
class DungeonClearPartyTankValue : public CalculatedValue<Player*>
{
public:
    DungeonClearPartyTankValue(PlayerbotAI* botAI)
        : CalculatedValue<Player*>(botAI, DcKey::PartyTank, 1)
    {
    }

protected:
    Player* Calculate() override;
};

#endif
