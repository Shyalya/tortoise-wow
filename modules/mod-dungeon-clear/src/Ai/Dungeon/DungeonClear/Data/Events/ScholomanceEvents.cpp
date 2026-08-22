/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- Scholomance (map 289) — Marduk & Vectus ROOM-AGGRO PRE-CLEAR ----------
// Marduk Blackpool (10433) and Vectus (10432) are a linked pair (pulling either
// pulls both, see their SmartAI) collapsed into ONE boss anchored on Vectus by
// BossRosterRegistry (map 289). They sit in a chamber of ~32 Scholomance
// Students; though they do not pre-aggro, an AoE that wakes one wakes the pair
// while the room is still up. Re-use the shared room-aggro pre-clear model
// (condition 3 — DUE while DungeonClearRoomTrashValue still has room trash, with
// the RoomAggroRegistry geometry around the LIVE boss minus the boss aggro
// sphere / unreachable / door-blocked): the lone KillCreature(0 = room-trash
// mode) step gates the boss pull until the students are cleared, and
// DcRunEventAction drives the engage (nearest room trash first). Required (hold
// the pull until clear or the value gives up) and NEVER latched — the same row
// re-fires for the merged boss. Both 10432 and 10433 are in RoomAggroRegistry so
// the tracked boss's keep-away sphere protects the close trash and Marduk (off
// the roster) is excluded from being chased as a faction-15 student.

void RegisterScholomanceEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(289, 1, "Clear the room (Marduk & Vectus)")
                      .Conditional(&DcRoomAggroPreClearCondition)
                      .KillCreature(/*room trash*/ 0)
                      .Build());
}

// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterScholomanceRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- Scholomance (map 289) — Marduk & Vectus, one merged boss ---
    // Marduk Blackpool (10433) and Vectus (10432) are two separate
    // DungeonEncounters that share one room ~18yd apart and are wired
    // together in SmartAI: each "On Aggro -> Set Data 3" and "On Data Set
    // 3 -> Attack Start", so pulling EITHER pulls BOTH. They do not
    // pre-aggro, but they stand in a chamber of ~32 Scholomance Students;
    // engaging them before the room is clear (or AoE-waking one while
    // clearing) drags the whole pile in.
    //
    // Collapse the pair into ONE boss, mirroring SM Cathedral
    // (Mograine/Whitemane): drop BOTH derived entries and re-add a single
    // "Marduk & Vectus" anchored on VECTUS's spawn (the boss nearest the
    // student cluster — the close trash at ~14yd then falls inside the
    // tracked boss's keep-away sphere and is left "coming with the boss").
    // The merged boss re-uses Vectus's real entry (10432) so the engage
    // pipeline drives a real creature, and inherits Vectus's own kill-bit
    // via inheritCompletionFrom (resolved from the base list before the
    // remove). Killing the linked pair flips that bit -> encounter done.
    // Marduk stays in RoomAggroRegistry (partner exclusion) but off the
    // roster, so the tank never routes to him separately. The room-aggro
    // pre-clear event (ScholomanceEvents.cpp, condition 3) clears the
    // students first.
    {
        BossRosterPatch p;
        p.mapId = 289;
        p.remove = { 10432, 10433 };
        p.add = {
            MakeBoss(10432, 289, "Marduk & Vectus",
                     143.5f, 99.1f, 104.7f, /*completionFrom*/ 10432),
        };
        t.push_back(std::move(p));
    }
}
