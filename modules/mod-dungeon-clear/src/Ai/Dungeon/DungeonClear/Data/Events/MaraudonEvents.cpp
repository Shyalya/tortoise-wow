/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonWingRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"

#include <unordered_map>

// Maraudon (map 349) has no scripted events — its clear-data is the wing
// LABELS (isolated == false: display only, not a filter) plus one roster
// removal (Rotgrip, who sits in open water). This file is Maraudon's
// definition unit so that data lives with its dungeon like every other. The TU
// stays linked because DungeonWingRegistry's aggregator calls
// RegisterMaraudonWings explicitly (and BossRosterRegistry's calls
// RegisterMaraudonRoster).

// --- wing layout (relocated from DungeonWingRegistry) --------------------
void RegisterMaraudonWings(std::unordered_map<uint32, DungeonWingLayout>& store)
{
    // --- Maraudon (map 349) --------------------------------------
    // Three named regions — Orange (Foulspore Cavern), Purple (Wicked
    // Grotto) and the inner Pristine Waters (Earth Song Falls) — but
    // UNLIKE Dire Maul they share one connected interior: orange and
    // purple converge at the Celebras seal, which opens into the inner
    // waters, and the Earth Song Falls surface portal drops straight
    // into that same inner area. So every boss is reachable from any
    // entrance.
    //
    // isolated == false: this is a LABEL only. Do NOT filter — all
    // eight bosses stay in the clear list; the wing name is surfaced in
    // status/UI so the player can see which region each boss sits in.
    //
    // Entries are the kill-creature credit-entries from
    // instance_encounters (what BossSpawnIndex emits). Celebras the
    // Cursed sits at the purple-side seal he unlocks, so he is grouped
    // with Purple.
    store[349] = {false, {
        {"Maraudon (Orange)", {
            13282,  // Noxxion
            12258,  // Razorlash
        }},
        {"Maraudon (Purple)", {
            12236,  // Lord Vyletongue
            12225,  // Celebras the Cursed
        }},
        {"Maraudon (Pristine Waters)", {
            13601,  // Tinkerer Gizlock
            12203,  // Landslide
            13596,  // Rotgrip — dropped from the clear (see the roster patch
                    // below); the label is kept so anything that resolves his
                    // entry to a region still names the right one.
            12201,  // Princess Theradras
        }},
    }};
}

// --- roster patch: drop Rotgrip ------------------------------------------
void RegisterMaraudonRoster(std::vector<BossRosterPatch>& t)
{
    // --- Maraudon (map 349) --------------------------------------
    // ROTGRIP (13596, DBC bit 6) is SKIPPED — removed from the clear list, not
    // relocated.
    //
    // He is a crocolisk who spawns and lives in the Pristine Waters lake at
    // (42.08, -65.95, -199.55) — open water, not a shoreline platform. That is
    // the same navmesh shape as the Underbog basin the Ghaz'an anchor had to be
    // moved off (see UnderbogEvents.cpp): every mesh column over that lake is a
    // water SURFACE sheet, so boss-nav either routes the party into a swim or
    // fails outright. Unlike Ghaz'an there is no scripted event that walks him
    // out onto dry ground and no dry anchor within his leash, so there is
    // nothing to re-anchor to — remove + re-add can't fix a boss whose whole
    // encounter is in the water.
    //
    // Consequence: the party clears Gizlock -> Landslide -> Princess Theradras
    // and reports the dungeon done with Rotgrip's kill-bit unset. That is
    // intentional; he is an optional side boss off the main path to the
    // Princess, so skipping him costs no progression. Removing him (rather than
    // leaving him in for the run to stall on) is what keeps the clear moving:
    // an unreachable boss otherwise parks the tank at the water's edge until the
    // stalled-boss fallback fires, once per attempt.
    BossRosterPatch p;
    p.mapId = 349;
    p.remove = {13596};  // Rotgrip
    t.push_back(std::move(p));
}
