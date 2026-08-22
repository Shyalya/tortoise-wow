/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- The Slave Pens (map 547) — drop-down past Mennu, ANCHORED -------------
//
// The route from Mennu the Betrayer (first boss, bit 0) onward to Rokmar the
// Crackler (bit 1) and Quagmirran (bit 2) crosses a BREAK in the navmesh: the
// party climbs a ramp to a ledge at (-186.52, -412.79, 55.32) and must DROP
// ~47yd down to (-209.02, -384.32, 8.53) to continue. The two surfaces sit on
// disconnected mesh islands and the drop is DIAGONAL — the landing is ~36yd
// horizontally offset from the ledge — so neither a pure-vertical DropInHole
// (it would fall straight down into the wrong column) nor a ballistic Jump (big
// diagonal drops clip the wall / overshoot, the source of past drop-down grief)
// is reliable here.
//
// Per the user, the robust fix is to TELEPORT the whole party across the instant
// the tank reaches the ledge. A roster OBJECTIVE anchor (BossRosterRegistry map
// 547) sits at the ledge, ordered between Mennu and Rokmar (encounterIndex 1, the
// same bit as Rokmar — the Objective-before-Boss tie-break in Apply() sorts it
// ahead of him); boss-nav drives the long ramp climb to it. This one-step event
// then relocates the leader and every party bot down to the landing, after which
// boss-nav resumes toward Rokmar from solid, connected mesh.
//
// Anchored (not Conditional): the drop is purely positional and on the critical
// path, so it rides the objective exactly like Wailing Caverns' DropInHole. Not
// Persistent: TeleportParty is a single synchronous step that completes the same
// tick it fires (and is idempotent on a restart), so there is no multi-tick fall
// for a tick-gap to rewind.

void RegisterSlavePensEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(547, 1, "Drop down past Mennu")
                      .Anchored(/*orderIndex, doc-only*/ 1)
                      .TeleportParty(/*ledge*/ -186.52f, -412.79f, 55.32f,
                                     /*landing*/ -209.02f, -384.32f, 8.53f)
                      .Build());
}

// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterSlavePensRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- The Slave Pens (map 547) --------------------------------
    // The auto-roster derives all three bosses (Mennu 17941 / bit 0,
    // Rokmar 17991 / bit 1, Quagmirran 17942 / bit 2) from their static
    // spawns, so no boss surgery is needed. But the path from Mennu to
    // Rokmar crosses a navmesh BREAK: the party climbs a ramp to a ledge
    // (-186.52, -412.79, 55.32) and must drop ~47yd down to
    // (-209.02, -384.32, 8.53), which sits on a disconnected mesh island
    // boss-nav can't route to.
    //
    // Add a travel OBJECTIVE at the ledge, ordered between Mennu and
    // Rokmar. It borrows encounterIndex 1 (Rokmar's bit): the
    // Objective-before-Boss tie-break in Apply() sorts it AHEAD of Rokmar
    // at the shared key, so after Mennu (bit 0) the tank visits this
    // objective and only then Rokmar. Sharing the bit is safe — an
    // objective is filtered by the cleared-anchor latch, never the
    // completion mask (NextDungeonBossValue keys the mask to Boss anchors
    // only), so Rokmar's eventual kill can't retro-complete it. Its
    // eventId 1 (SlavePensEvents.cpp) teleports the whole party across the
    // break the instant the tank reaches the ledge.
    {
        BossRosterPatch p;
        p.mapId = 547;
        p.add = {
            MakeObjective(OBJ(1), /*encounterIndex*/ 1, 547,
                          "Drop down past Mennu",
                          -186.52f, -412.79f, 55.32f,
                          /*arriveRadius*/ 6.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 1),
        };
        t.push_back(std::move(p));
    }
}
