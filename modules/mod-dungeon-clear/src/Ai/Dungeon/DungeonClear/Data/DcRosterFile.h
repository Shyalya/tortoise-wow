/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef DC_ROSTER_FILE_H
#define DC_ROSTER_FILE_H

#include "DcBossEntries1121.h"

#include <string>
#include <vector>

// The boss credit list and the encounter order, as DATA on disk rather than as
// a compiled table.
//
// Why: every dungeon taken onto the ladder so far needed the same two
// corrections - bosses the auto-derived roster never saw (Wailing Caverns 5,
// Shadowfang Keep 5, Blackfathom Deeps 4, The Stockade 2, one of them the END
// boss), plus an encounter order. A sweep across the fourteen remaining rungs
// found the same hole in every one of them, with Blackrock Depths missing 26
// bosses - Emperor Dagran Thaurissan among them. Carried in
// DcBossEntries1121.h, each of those corrections costs a full rebuild, about
// forty minutes, thirteen more times, for what is a two-line data edit.
//
// So DcBossEntries1121.h stays as the compiled baseline - a realm without the
// file behaves exactly as before - and a text file overlays it.
//
// Format, one directive per line, '#' starts a comment:
//
//     credit <entry> [<entry> ...]     add creature entries to the credit list
//     order  <mapId> <entry> <index>   place an entry in that map's order
//     drop   <entry> [<entry> ...]     take entries OUT of the roster
//
// `drop` exists for the rares. Seven creatures in the compiled credit list
// come out of a spawn pool, six of them genuine rares - Earthcaller Halmgar
// at 30%, Fallen Champion at 2%. Credited, each becomes a required kill,
// and the group then waits for something that usually is not in the
// instance at all. A rare is never an objective. drop wins over everything
// else, including an order line in the same file.
//     drop   <entry> [<entry> ...]     take entries OUT of the roster
//
// `drop` exists for the rares. Seven creatures in the compiled credit list
// come out of a spawn pool, six of them genuine rares - Earthcaller Halmgar
// at 30%, Fallen Champion at 2%. Credited, each becomes a required kill,
// and the group then waits for something that usually is not in the
// instance at all. A rare is never an objective. drop wins over everything
// else, including an order line in the same file.
//
// An `order` line credits its entry as well, so a boss the curated list never
// carried needs nothing but the order line.
//
// Order is replaced PER MAP: if the file names any order for a map, that map's
// compiled rows are dropped entirely. A file order is therefore always read as
// written, and can never end up half-merged with a stale compiled one - which
// would silently renumber encounters and is the kind of thing that only shows
// up as a boss being skipped three hours into a run.
//
// Path comes from `DungeonClear.RosterFile`; `.reload config` re-reads it.
namespace DcRosterFile
{
    // Compiled baseline plus whatever the file adds. Returned BY VALUE on
    // purpose: these are read while the boss index is being built, and handing
    // out a reference to a static that Reload() may resize underneath a reader
    // is precisely the shared-mutable-state pattern that cost us a SIGSEGV in
    // the strategy map. They are called once per index build, so the copy is
    // free in any sense that matters.
    std::vector<uint32> CreditEntries();
    std::vector<DcBossOrderRow> OrderRows();

    // Re-read the file. Returns the number of accepted directives; `error`, if
    // given, receives the first complaint.
    uint32 Reload(std::string* error = nullptr);
}

#endif
