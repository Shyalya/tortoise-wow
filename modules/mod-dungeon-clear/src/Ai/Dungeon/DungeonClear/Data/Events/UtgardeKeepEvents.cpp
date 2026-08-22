/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- Utgarde Keep (map 574) — the forge masters must be killed IN ORDER ----
//
// UK is the first Wrath dungeon on the clear. It is one monotonic ~2275yd climb
// from the zone-in (154,-93,13) to Ingvar (246,-332,180) with no branch to
// choose between, and DungeonEncounter.dbc already lists its three encounters in
// travel order (Keleseth bit 0, "Skarvold & Dalronn" bit 1, Ingvar bit 2). So
// the derived roster's BOSSES are already right and none is removed or re-added
// here. The whole patch exists for one thing: the forge.
//
// THE GATE. `npc_dragonflayer_forge_master` (24079, three spawns) refuses to be
// fought out of order — utgarde_keep.cpp:
//
//     void JustEngagedWith(Unit*) override
//     {
//         if (prevDataId && !pInstance->GetData(prevDataId))
//         {
//             EnterEvadeMode();      // <-- attacked out of order
//             return;
//         }
//         pInstance->SetData(dataId, IN_PROGRESS);
//     }
//     void Reset()  { pInstance->SetData(dataId, NOT_STARTED); }   // clears the bit
//
// Each master's forge id is derived from its HOME POSITION in the constructor,
// so the order is fixed by geometry:
//
//     DATA_FORGE_1  home x 344-357, y -44..-35   spawn (349.6, -39.3, 24.8)  no prereq
//     DATA_FORGE_2  home x 380-389, y -21..-12   spawn (385.8, -16.2, 24.8)  needs forge 1
//     DATA_FORGE_3  everything else              spawn (347.6,   4.6, 24.7)  needs forge 2
//
// `GetData` reads a bit that `SetData` sets for ANY state other than
// NOT_STARTED, so merely ENGAGING forge 1 unlocks forge 2 — but a forge that
// evades and resets clears its own bit again. Touch forge 2 or 3 first and it
// evades on the spot; a puller that re-acquires it re-aggros it and it evades
// again. That is an evade livelock with nothing for the pull FSM to key on: the
// target never engages, never dies, and never goes away.
//
// AND THE GEOMETRY MAKES IT LIKELY. The main line north out of the forge hall
// runs (307,-12,24) -> (314,20,25) -> (342,33,24), whose closest approach to
// FORGE MASTER 3 is ~29yd — outside a level-70 aggro bubble but inside the
// room-trash and en-route-pack scan ranges — and the two Dragonflayer
// Metalworkers at (331.6,0.8) and (334.3,18.6) sit 10-18yd from him DIRECTLY on
// that line, so an ordinary corridor pull can call him in.
//
// THE FIX: three ordered travel objectives, one per forge, each running a
// one-step entry-filtered ClearRadius. Boss navigation walks the tank to each
// forge in turn and the OBJECTIVE ORDER is what enforces the script's order.
//
// Why ClearRadius and not KillCreature: all three masters share entry 24079, and
// KillCreature/KillCreatureEngage resolve by ENTRY — they would seek the nearest
// one, which is exactly the out-of-order engage this event exists to prevent.
// ClearRadius is POSITION-anchored, so a 12yd volume names one specific master;
// the three are 41-44yd apart and the nearest non-forge-master spawn to any of
// them is 14.7yd, so each volume holds exactly its own mob.
//
// NO BY-ENTRY BACKSTOP, deliberately — and this is the one place the house
// idiom (ClearRadius then a KillCreatureEngage of the same entry, the Shattered
// Halls assassin lesson) must NOT be copied. A backstop keyed on 24079 seeks the
// nearest live one, which past forge 1 is frequently the WRONG forge, and one
// out-of-order engage undoes the ordering the objectives just bought. The
// masters are plain visible elites standing in an open lit hall with no stealth,
// no invisibility and no untargetable phase, so the sight/reachability filtering
// that motivated the backstop elsewhere has nothing to drop here.
//
// OPTIONAL, so a wedged forge degrades instead of stalling: the forge is not a
// prerequisite for any boss (nothing gates on ForgeEventMask but the masters
// themselves), so a step that times out should let the clear carry on to
// Keleseth rather than hold the run for the human. Same call as Shadowfang
// Keep's voidwalker sweep.
//
// NOT persistent: one step, so a Drive gap restarting at step 0 re-evaluates the
// same volume — idempotent. (The F1 persistence lint skips single-step events
// for the same reason.)
//
// NOT in the roster: the three riders/ghosts/hazards this dungeon also needs.
// Ingvar's thrown axe is a DcHazardRegistry row (entry 23997), and the airborne
// Proto-Drake Riders are a targeting question, not a clear-order one.

namespace
{
    // The forge masters. One entry, three spawns, three fixed positions.
    constexpr uint32 UK_FORGE_MASTER = 24079;

    constexpr float UK_FORGE1_X = 349.6f;
    constexpr float UK_FORGE1_Y = -39.3f;
    constexpr float UK_FORGE1_Z = 24.8f;

    constexpr float UK_FORGE2_X = 385.8f;
    constexpr float UK_FORGE2_Y = -16.2f;
    constexpr float UK_FORGE2_Z = 24.8f;

    constexpr float UK_FORGE3_X = 347.6f;
    constexpr float UK_FORGE3_Y = 4.6f;
    constexpr float UK_FORGE3_Z = 24.7f;

    // 12yd names exactly one master: the three are 41-44yd apart and the nearest
    // OTHER spawn to any of them is a Metalworker at 14.7yd. The entry filter
    // then makes the completion gate exact — the neighbouring Metalworkers and
    // Weaponsmiths are still fought when they aggro, which is the combat
    // engine's job, not the clear's.
    constexpr float UK_FORGE_SWEEP_RADIUS = 12.0f;
    // The forge hall is a single floor (masters z 24.7-24.8, anvils z 21.3,
    // bellows z 42.0). 8yd keeps the bellows gantry out without being able to
    // miss a master knocked a yard downhill.
    constexpr float UK_FORGE_SWEEP_ZBAND = 8.0f;

    // Arrival radius for the objective anchor. Matches the sweep volume so
    // "arrived" and "inside the volume the event clears" are the same place.
    constexpr float UK_FORGE_ARRIVE = 12.0f;

    // One elite plus whatever its neighbours contribute. The 30s EventStepTimeout
    // default is a walk-in short of that; 2 minutes is generous but bounded, and
    // the event is Optional so hitting it costs the forge, not the run.
    constexpr uint32 UK_FORGE_FIGHT_TIMEOUT = 120000;

    // Clear-order keys. The three real bosses keep their DBC kill-bits (0/1/2)
    // and are only REORDERED onto this 1..6 scale, so nothing about their
    // completion detection changes.
    constexpr int32 UK_ORDER_FORGE1 = 1;
    constexpr int32 UK_ORDER_FORGE2 = 2;
    constexpr int32 UK_ORDER_FORGE3 = 3;
    constexpr int32 UK_ORDER_KELESETH = 4;
    constexpr int32 UK_ORDER_DALRONN = 5;
    constexpr int32 UK_ORDER_INGVAR = 6;

    constexpr uint32 UK_KELESETH = 23953;
    // The encounter is "Skarvold & Dalronn" but its DungeonEncounter credit
    // entry is DALRONN ALONE (24201) — Skarvald (24200) has no row of his own,
    // and instance_utgarde_keep never calls SetBossState for the pair, so the
    // completed-encounter bit comes from the generic Map::UpdateEncounterState
    // on Dalronn's death. The derived roster therefore anchors on Dalronn; they
    // spawn 7yd apart and each force-aggros the other, so one anchor is one
    // fight either way.
    constexpr uint32 UK_DALRONN = 24201;
    constexpr uint32 UK_INGVAR = 23954;
}

void RegisterUtgardeKeepEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(574, 1, "First forge")
                      .Anchored(/*orderIndex (doc)*/ UK_ORDER_FORGE1)
                      .Optional()
                      .ClearRadius(UK_FORGE1_X, UK_FORGE1_Y, UK_FORGE1_Z,
                                   UK_FORGE_SWEEP_RADIUS, UK_FORGE_SWEEP_ZBAND)
                          .OnlyEntries({ UK_FORGE_MASTER })
                          .Timeout(UK_FORGE_FIGHT_TIMEOUT)
                      .Build());

    out.push_back(EventBuilder(574, 2, "Second forge")
                      .Anchored(/*orderIndex (doc)*/ UK_ORDER_FORGE2)
                      .Optional()
                      .ClearRadius(UK_FORGE2_X, UK_FORGE2_Y, UK_FORGE2_Z,
                                   UK_FORGE_SWEEP_RADIUS, UK_FORGE_SWEEP_ZBAND)
                          .OnlyEntries({ UK_FORGE_MASTER })
                          .Timeout(UK_FORGE_FIGHT_TIMEOUT)
                      .Build());

    out.push_back(EventBuilder(574, 3, "Third forge")
                      .Anchored(/*orderIndex (doc)*/ UK_ORDER_FORGE3)
                      .Optional()
                      .ClearRadius(UK_FORGE3_X, UK_FORGE3_Y, UK_FORGE3_Z,
                                   UK_FORGE_SWEEP_RADIUS, UK_FORGE_SWEEP_ZBAND)
                          .OnlyEntries({ UK_FORGE_MASTER })
                          .Timeout(UK_FORGE_FIGHT_TIMEOUT)
                      .Build());
}

// --- roster patch ---------------------------------------------------------
void RegisterUtgardeKeepRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = 574;

    // Three forge objectives, in script order, all ahead of Keleseth. The
    // objective's `encounterIndex` is an ordering hint only (an objective has no
    // kill-bit and NextDungeonBossValue never tests the completion mask for
    // one), so it is left 0 and the clear orders by orderOverride.
    p.add = {
        MakeObjective(OBJ(1), /*encounterIndex*/ 0, 574, "First forge",
                      UK_FORGE1_X, UK_FORGE1_Y, UK_FORGE1_Z,
                      UK_FORGE_ARRIVE, /*gateEntry*/ 0, /*hook*/ 0,
                      /*eventId*/ 1, /*orderOverride*/ UK_ORDER_FORGE1),
        MakeObjective(OBJ(2), /*encounterIndex*/ 0, 574, "Second forge",
                      UK_FORGE2_X, UK_FORGE2_Y, UK_FORGE2_Z,
                      UK_FORGE_ARRIVE, /*gateEntry*/ 0, /*hook*/ 0,
                      /*eventId*/ 2, /*orderOverride*/ UK_ORDER_FORGE2),
        MakeObjective(OBJ(3), /*encounterIndex*/ 0, 574, "Third forge",
                      UK_FORGE3_X, UK_FORGE3_Y, UK_FORGE3_Z,
                      UK_FORGE_ARRIVE, /*gateEntry*/ 0, /*hook*/ 0,
                      /*eventId*/ 3, /*orderOverride*/ UK_ORDER_FORGE3),
    };

    // Slot the three real bosses onto the same 1..N key scale so the forge
    // objectives sort ahead of them. Their DBC kill-bits (0/1/2) are untouched —
    // orderOverride only moves the clear sequence, and the relative order of the
    // bosses is unchanged (it already matched the travel path).
    p.reorder = {
        { UK_KELESETH, UK_ORDER_KELESETH },
        { UK_DALRONN,  UK_ORDER_DALRONN  },
        { UK_INGVAR,   UK_ORDER_INGVAR   },
    };

    t.push_back(std::move(p));
}
