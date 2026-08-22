/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCNEVERTARGETREGISTRY_H
#define _PLAYERBOT_DCNEVERTARGETREGISTRY_H

#include "Define.h"

// Static registry of creatures the CLEAR must never pick as a target.
//
// The clear's target selection — the corridor blocking-trash scan, the en-route
// pack sweep, the room-aggro pre-clear, a ClearRadius volume and the map-wide
// fallback pull — all gate their candidates on
// `AttackersValue::IsPossibleTarget`. That predicate answers "can this bot
// legally attack this unit right now", which is not the same question as "is
// killing this unit progress". A mob that is legally attackable but SCRIPTED
// NEVER TO DIE passes it every time, so the clear picks it, pulls it, fights it,
// watches it become invulnerable, and picks it again — a livelock with nothing
// for the pull FSM to key on: the target never dies and never goes away.
//
// This table is the "is killing it progress" half. A listed (mapId, entry) is
// dropped from every clear-side candidate set. It is NOT a pacifism switch: the
// stock combat engine's own targeting is untouched, so a bot these mobs attack
// still fights back normally. All this removes is the clear's decision to go
// LOOKING for them.
//
// Keep it small and keep the justification in the table. A mob that merely
// resists, heals, or respawns does not belong here — only one that cannot be
// killed at all in the window the clear is walking through.
//
// Mirrors FightInPlaceRegistry / RoomAggroRegistry: adding a fix is a single
// table edit inside DungeonClear/, never a core change.
struct DcNeverTargetRow
{
    uint32 mapId{0};
    uint32 entry{0};   // the NORMAL creature entry; heroic spawns keep it
                       // (Creature::InitEntry swaps m_creatureInfo, not the entry)
};

class DcNeverTargetRegistry
{
public:
    // True when the clear must not select `entry` on `mapId` as a target. Pure
    // (no game state) so it is unit-testable on its own. Linear scan; the table
    // is tiny.
    static bool IsNeverTarget(uint32 mapId, uint32 entry);
};

#endif  // _PLAYERBOT_DCNEVERTARGETREGISTRY_H
