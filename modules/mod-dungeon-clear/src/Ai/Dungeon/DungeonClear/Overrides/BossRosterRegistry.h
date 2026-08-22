/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_BOSSROSTERREGISTRY_H
#define _PLAYERBOT_BOSSROSTERREGISTRY_H

#include <utility>
#include <vector>

#include "Common.h"
#include "Ai/Dungeon/DungeonClear/Data/DcDifficultyGate.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"

// Per-dungeon corrections to the auto-derived boss list (BossSpawnIndex).
//
// Many dungeons spawn a boss only after an event, gate a boss behind an event
// the party must travel through, or simply carry the wrong creature in their
// DungeonEncounter.dbc data. This registry lets each such dungeon express a
// small PATCH against the derived list — remove the wrong/locked entries, add
// the real boss (with hand-authored navmesh coords), and/or add travel
// "objective" anchors the tank leads the party to.
//
// Authoring is one row per dungeon in a hardcoded table (see
// BossRosterRegistry.cpp), mirroring RoomAggroRegistry. This is link-safe in
// the static-lib build (a hardcoded table in a GLOBed .cpp), unlike
// self-registering static initializers which the linker strips.
struct BossRosterPatch
{
    uint32 mapId{0};
    // Entries to drop from the auto-derived list (wrong / event-locked bosses).
    std::vector<uint32> remove;
    // Bosses / objectives to add. Coords are hand-authored; objectives use
    // DungeonAnchorKind::Objective. A boss with inheritCompletionFrom set
    // borrows that (to-be-removed) entry's encounterIndex for its kill-bit.
    std::vector<DungeonBossInfo> add;
    // Reorder auto-derived bosses IN PLACE: {entry, orderOverride}. Sets
    // DungeonBossInfo::orderOverride on the matching kept entry so it sorts by
    // the given clear-path key while its real DBC kill-bit (encounterIndex) is
    // left untouched. This is the low-boilerplate way to fix a dungeon whose
    // DBC encounter order doesn't match the sensible travel path WITHOUT
    // remove+re-add (no hand-authored coords needed — the auto-derived spawn
    // coords are kept). Use a contiguous 1..N key sequence and slot any
    // travel objectives into the same scale via MakeObjective's orderOverride.
    // Entries not listed keep their DBC encounterIndex order.
    std::vector<std::pair<uint32, int32>> reorder;
    // Difficulty gate. Most patches are Any (5-man rosters mostly match across
    // normal/heroic); a heroic-only correction (an added heroic bonus boss /
    // anchor, or a remove that only applies on heroic) goes in a SECOND patch
    // for the same map with gate HeroicOnly — Apply applies every patch whose
    // gate matches the run's difficulty, in registration order.
    DcDifficultyGate gate{DcDifficultyGate::Any};
};

class BossRosterRegistry
{
public:
    // Synthetic entry for the seq-th non-creature objective anchor on a map.
    // Real creature entries never reach this range, so an objective gets a
    // unique nonzero entry that flows through the entry-keyed machinery (skip /
    // sticky / cleared-anchor latch / panel) without colliding with a spawn.
    // Exposed so an event file can name the objective it sorts relative to
    // in the `dc bosses` panel (DungeonEvent::panelGatesBossEntry).
    static constexpr uint32 ObjectiveEntry(uint32 seq) { return 0x4F000000u | seq; }

    // The full patch table. Exposed as a seam for the registry-integrity gtests
    // (t/TestEventRegistry.cpp): the added objectives carry the eventId /
    // onArriveHook / wing cross-references validated there. Not used by runtime
    // callers, which go through Apply/HasPatch.
    static std::vector<BossRosterPatch> const& AllPatches();

    // Returns the patched boss list for `mapId` at `difficulty`. Every patch
    // whose mapId matches AND whose gate matches the difficulty is applied, in
    // registration order (a map typically has one Any patch, optionally plus a
    // HeroicOnly one). If none match, the base list is returned unchanged.
    static std::vector<DungeonBossInfo> Apply(uint32 mapId, Difficulty difficulty,
                                              std::vector<DungeonBossInfo> base);

    // True if any patch exists for the map (cheap gate for callers).
    static bool HasPatch(uint32 mapId);
};

#endif
