/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONROSTERBUILDERS_H
#define _PLAYERBOT_DUNGEONROSTERBUILDERS_H

#include <utility>

#include "Common.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"

// Shared builders for the per-dungeon roster patches (BossRosterPatch).
//
// Each Data/Events/<Dungeon>Events.cpp owns its roster patch as a
// Register<Dungeon>Roster appender (declared in DungeonEventTables.h) — the
// same one-file-per-dungeon "definition unit" the event rows already use. These
// helpers used to live in an anonymous namespace inside BossRosterRegistry.cpp;
// exposing them here (as inline free functions in namespace DcRoster) lets the
// patch data move next to the matching event rows while the central
// BossRosterRegistry keeps its Apply/HasPatch API and just aggregates the
// appenders. Open the namespace with `using namespace DcRoster;` at the top of
// each Register<Dungeon>Roster body.
namespace DcRoster
{
    // Synthetic entry for the seq-th non-creature objective on a map. Thin alias
    // of the shared BossRosterRegistry::ObjectiveEntry so the moved patch tables
    // keep their OBJ(n) shorthand.
    inline constexpr uint32 OBJ(uint32 seq) { return BossRosterRegistry::ObjectiveEntry(seq); }

    // Build a boss anchor with an inherited completion bit. `completionFrom` is
    // the auto-derived entry whose encounterIndex (kill-bit) this boss borrows
    // — used when the real boss has no DungeonEncounter row of its own.
    // `orderOverride` (default -1) reorders the boss in the clear sequence
    // independently of its kill-bit — see DungeonBossInfo::orderOverride. Use it
    // when re-adding a real boss whose DBC bit doesn't match the desired path
    // (pass completionFrom = the boss's own entry to keep its real kill-bit).
    inline DungeonBossInfo MakeBoss(uint32 entry, uint32 mapId, char const* name,
                                    float x, float y, float z, uint32 completionFrom,
                                    int32 orderOverride = -1, int32 doneBossStateIndex = -1)
    {
        DungeonBossInfo b;
        b.entry = entry;
        b.name = name;
        b.mapId = mapId;
        b.x = x;
        b.y = y;
        b.z = z;
        b.kind = DungeonAnchorKind::Boss;
        b.inheritCompletionFrom = completionFrom;
        b.orderOverride = orderOverride;
        b.doneBossStateIndex = doneBossStateIndex;
        // A boss completed via the instance boss-state slot has NO DungeonEncounter
        // bit, so park its encounterIndex past bit 31 — the completedMask check
        // (guarded by `encounterIndex < 32`) then never matches a real boss's bit
        // (default 0 would collide with DBC bit 0). Ordering still uses the
        // explicit orderOverride; completion uses doneBossStateIndex.
        if (doneBossStateIndex >= 0)
            b.encounterIndex = 64;
        return b;
    }

    // Build a travel-objective anchor. `encounterIndex` slots it into the
    // encounter ordering; `gateEntry` (optional) is a creature whose live
    // presence also satisfies the objective; `hook` (optional) is an
    // ObjectiveHookRegistry id. `orderOverride` (default -1) reorders the
    // objective in the clear sequence independently of `encounterIndex` (which,
    // for an objective, is only an ordering hint — objectives carry no real
    // kill-bit), so it can share a 1..N key scale with reordered real bosses.
    inline DungeonBossInfo MakeObjective(uint32 entry, uint32 encounterIndex, uint32 mapId,
                                         char const* name, float x, float y, float z,
                                         float arriveRadius = 0.0f, uint32 gateEntry = 0,
                                         uint32 hook = 0, uint32 eventId = 0,
                                         int32 orderOverride = -1)
    {
        DungeonBossInfo o;
        o.entry = entry;
        o.encounterIndex = encounterIndex;
        o.name = name;
        o.mapId = mapId;
        o.x = x;
        o.y = y;
        o.z = z;
        o.kind = DungeonAnchorKind::Objective;
        o.arriveRadius = arriveRadius;
        o.gateEntry = gateEntry;
        o.onArriveHook = hook;
        o.eventId = eventId;
        o.orderOverride = orderOverride;
        return o;
    }
}

#endif
