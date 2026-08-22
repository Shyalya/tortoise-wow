/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONWINGREGISTRY_H
#define _PLAYERBOT_DUNGEONWINGREGISTRY_H

#include <string>
#include <vector>

#include "Common.h"

// Some 3.3.5 dungeons pack several mutually-inaccessible "wings" onto a single
// instance map. Dire Maul (map 429) is the canonical case: East, West and North
// share one map id but you enter each through its own portal and cannot reach
// the others from inside. DungeonEncounter.dbc lists all wings' bosses under
// that one map id, so the stock (mapId, difficulty) boss list mixes every
// wing's bosses together — the bot then targets bosses it can never reach and
// the dungeon never reads as "cleared".
//
// This registry records, per split map, which boss credit-entries belong to
// which wing. How DungeonBossesValue uses that depends on the wing topology:
//
//   isolated == true  (Dire Maul, Scarlet Monastery): the wings are physically
//     disconnected — separate portals, no in-instance route between them — so
//     the boss list is filtered down to the wing the bot is standing in (picked
//     by proximity, since the wings sit far apart in world space). Bosses in
//     other wings can never be reached and would otherwise wedge the clear.
//
//   isolated == false (Maraudon): the "wings" share one connected interior —
//     orange, purple and the inner Pristine Waters all link up, so every boss
//     stays reachable from any entrance. Here the wing data is NOT a filter; it
//     is a display/label only. All bosses remain in the list and clearable, and
//     the wing name is just surfaced in status/UI. Filtering here would make the
//     bot clear one wing and falsely read the dungeon as done.
//
// Maps NOT listed here are single-wing and pass through unfiltered. The lists
// are static game data (vanilla dungeon layouts never change), so they live in
// code rather than the DB.
struct DungeonWing
{
    std::string name;                  // human-readable, for logs/chat
    std::vector<uint32> bossEntries;   // creature credit-entries in this wing
};

// A split map's full wing layout: the wings plus whether they are physically
// isolated (filter the clear to one wing) or merely labelled regions of one
// connected interior (keep every boss, label only). See the header note above.
struct DungeonWingLayout
{
    bool isolated;                     // true = filter to current wing
    std::vector<DungeonWing> wings;
};

class DungeonWingRegistry
{
public:
    // Returns the wing layout for a split map, or nullptr if the map is not
    // split into wings (the common case — caller then keeps the full list).
    static DungeonWingLayout const* Get(uint32 mapId);

    // Human-readable wing label owning `bossEntry` on `mapId`, or "" when the
    // map has no wing split or the entry isn't registered. For status/UI.
    static std::string WingName(uint32 mapId, uint32 bossEntry);
};

#endif
