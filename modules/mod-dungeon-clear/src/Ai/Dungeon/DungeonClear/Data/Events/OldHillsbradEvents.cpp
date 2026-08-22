/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- Old Hillsbrad Foothills / Escape from Durnholde (map 560) --------------
//
// The whole zone is one long scripted chain (core: old_hillsbrad.cpp +
// instance_old_hillsbrad.cpp + boss_captain_skarloc.cpp). The instance exposes a
// MONOTONIC progress counter, GetData(DATA_ESCORT_PROGRESS) (data id 0), which is
// the single source of truth we gate on:
//
//   0 NONE · 1 BARRELS (5 bombs planted, Lt. Drake summoned) · 3 THRALL_ARMORED ·
//   4 AMBUSHES_1 · 5 SKARLOC_KILLED · 6 TARREN_MILL · 7 TARETHA_MEET ·
//   9 FINISHED (Epoch Hunter dead).
//
// Three anchored objectives sequence the run:
//   (1) Ride to the keep      — gossip Brazen, then relocate the party there.
//   (2) Distraction + Drake   — bomb 5 barrels (item-on-GO), kill Lt. Drake.
//   (3) Free & escort Thrall  — ONE EscortCreature step that runs from freeing
//       Thrall all the way to Epoch Hunter's death, driven entirely off Thrall's
//       gossip flag (release / mount-up / start-waves / Epoch cutscene) and the
//       instance progress counter. The escort driver assists every ambush, the
//       Skarloc fight, the barn/church/inn waves, the Epoch waves and Epoch
//       himself, and speed-matches Thrall on the mounted Tarren Mill ride.
//
// The three bosses (Lt. Drake 17848, Capt. Skarloc 17862, Epoch Hunter 18096) are
// TempSummons (spawnId 0), invisible to the spawn store, so there is no derived
// roster — the objectives ARE the clear. Drake is engaged by objective (2)'s
// KillCreatureEngage; Skarloc and Epoch are engaged by objective (3)'s escort
// threat-assist (both attack Thrall). No standalone boss anchors — a boss anchor
// would have DC navigate to them independently and fight the escort.

namespace
{
    // Instance progress counter (old_hillsbrad.h DATA_ESCORT_PROGRESS) + the
    // milestone values we gate the escort completion on.
    constexpr uint32 OH_DATA_ESCORT_PROGRESS = 0;
    constexpr uint32 OH_PROGRESS_FINISHED    = 9;  // ENCOUNTER_PROGRESS_FINISHED

    // Entrance / Brazen (flight-master drake) — where the party spawns in.
    constexpr float OH_ENTRANCE_X = 2381.79f;
    constexpr float OH_ENTRANCE_Y = 1170.31f;
    constexpr float OH_ENTRANCE_Z = 66.54f;
    constexpr uint32 NPC_BRAZEN   = 18725;

    // Erozion — hands out the Pack of Incendiary Bombs (item 25853). His grant
    // gossip is quest-gated (quest 10283 taken/rewarded), which a bot never has,
    // AND Brazen's ride gossip (menu 7959) only appears once the player HOLDS item
    // 25853 — so the bot must acquire the pack before Brazen will transport it.
    // Since Erozion's option is unreachable for a questless bot, hook 3 grants the
    // pack directly (mechanically identical to Erozion's AddItem). See
    // ObjectiveHookRegistry GrantIncendiaryBombs. Erozion (18723) sits at:
    constexpr float OH_EROZION_X       = 2375.50f;
    constexpr float OH_EROZION_Y       = 1175.50f;
    constexpr float OH_EROZION_Z       = 65.30f;
    constexpr uint32 OH_HOOK_GRANT_BOMBS = 3;  // ObjectiveHookRegistry id

    // Brazen's drake DROP-OFF — OUTSIDE the keep's west wall (in-game reading), on
    // the road above the courtyard. The party teleports HERE and then WALKS in to
    // the barrels (never teleporting onto them), matching the intended flow.
    constexpr float OH_DROP_X = 2023.73f;
    constexpr float OH_DROP_Y = 241.40f;
    constexpr float OH_DROP_Z = 68.38f;

    // Keep courtyard — central to the barrel cluster; the OBJ(2) anchor the tank
    // walks in to from the drop before bombing (instancePositions[1] gather point).
    constexpr float OH_KEEP_X = 2103.23f;
    constexpr float OH_KEEP_Y = 93.55f;
    constexpr float OH_KEEP_Z = 53.10f;

    // The distraction: item 25853 (Pack of Incendiary Bombs) casts spell 32744
    // (Planting Incendiary Bomb) which the barrel's SmartAI counts (the core has
    // no positional/order check — any 5 spellhits count).
    //
    // THE BARRELS ARE POOLED (acore_world pool_template 1163-1167, "Barrel Group
    // 1-5", each max_limit=1 over 3 candidate gameobject rows): each of the FIVE
    // HOUSES spawns exactly ONE barrel per instance, at a RANDOM one of its 3
    // candidate spots. So a step can never anchor a specific gameobject row —
    // each step anchors its HOUSE's candidate CENTROID instead, and the executor
    // matches the goEntry GO within 25yd of the anchor (candidates sit <=21yd
    // from their centroid; the nearest NEIGHBOUR house's barrel is >=38yd away).
    // Houses 5/4/3 line the SW courtyard; houses 2/1 sit NE by the prison yard —
    // visit order below is nearest-first from the courtyard anchor.
    constexpr uint32 OH_ITEM_BOMBS   = 25853;
    constexpr uint32 OH_SPELL_PLANT  = 32744;
    constexpr uint32 OH_GO_BARREL    = 182589;
    constexpr uint32 NPC_LT_DRAKE    = 17848;

    // House candidate centroids (mean of each pool group's 3 gameobject rows).
    constexpr float OH_HOUSE5_X = 2109.12f, OH_HOUSE5_Y = 46.99f,  OH_HOUSE5_Z = 53.66f;  // pool 1167
    constexpr float OH_HOUSE4_X = 2074.34f, OH_HOUSE4_Y = 71.89f,  OH_HOUSE4_Z = 53.84f;  // pool 1166
    constexpr float OH_HOUSE3_X = 2067.37f, OH_HOUSE3_Y = 116.14f, OH_HOUSE3_Z = 54.50f;  // pool 1165
    constexpr float OH_HOUSE2_X = 2165.47f, OH_HOUSE2_Y = 252.75f, OH_HOUSE2_Z = 53.75f;  // pool 1164
    constexpr float OH_HOUSE1_X = 2213.15f, OH_HOUSE1_Y = 257.25f, OH_HOUSE1_Z = 53.81f;  // pool 1163

    // Post-barrel muster point — OUTSIDE the last (house-1) building. Planting the
    // 5th bomb sets that house ablaze; the party was parking ON the last barrel
    // inside it, where the fire ground-damage both hurt them and suppressed
    // food/drink (rest can't tick while taking damage), deadlocking the run. A
    // MoveTo here walks everyone clear of the fire into the open yard before Drake
    // spawns — Drake is then engaged from this safe spot.
    constexpr float OH_MUSTER_X = 2174.63f;
    constexpr float OH_MUSTER_Y = 220.60f;
    constexpr float OH_MUSTER_Z = 52.88f;

    // Objective (3) anchor: OUTSIDE Thrall's prison gate, on the players' side.
    // Thrall spawns INSIDE the cell (2231.90, 120.00, 82.30) behind the closed
    // Prison Door (GO 184393 at 2230.56, 118.12, 83.05), and the intended flow is
    // to gossip him THROUGH the gate — his script then opens the door itself
    // (EVENT_OPEN_DOORS) and he walks out. Anchoring on his spawn point instead
    // routed navigation INTO the closed gate, and the door-blocked machinery
    // (which correctly can't open a script-only door) paused the whole run. This
    // spot sits ~2.5yd outside the door on the walkway, ~4.8yd from Thrall —
    // inside gossip range through the bars, with the gate never on-path.
    constexpr uint32 NPC_THRALL   = 17876;
    constexpr float OH_CELL_GATE_X = 2229.11f;
    constexpr float OH_CELL_GATE_Y = 116.09f;
    constexpr float OH_CELL_GATE_Z = 83.05f;
}

void RegisterOldHillsbradEvents(std::vector<DungeonEvent>& out)
{
    // (1) GET THE BOMBS FROM EROZION, THEN RIDE TO DURNHOLDE KEEP.
    // The player must hold the Pack of Incendiary Bombs (item 25853) before Brazen
    // will offer his drake ride (Brazen menu 7959 opt 0 is condition-gated on the
    // item). Erozion normally grants it, but his gossip is quest-gated (quest 10283)
    // and unreachable for a questless bot — so walk to Erozion and grant the pack
    // directly (hook 3), THEN gossip Brazen to trigger the ride (spell 32892), THEN
    // TeleportParty the WHOLE party to the keep. The taxi flies only the invoker, so
    // a lone-leader flight would desync the followers 1100yd back; the teleport
    // lands everyone together at Brazen's DROP-OFF outside the walls (its
    // MotionMaster Clear cancels the just-started flight). The party then WALKS in
    // to the barrels via the OBJ(2) navigation — never teleported onto them.
    // PERSISTENT so the at-objective trigger stays sticky after the gossip nudges
    // the leader off the anchor — the TeleportParty then still fires; generous
    // checkpoint radius so a tick or two of flight still satisfies "at the checkpoint".
    out.push_back(
        EventBuilder(560, 1, "Ride to Durnholde Keep")
            .Anchored(/*orderIndex*/ 1)
            .Persistent()
            .MoveTo(OH_EROZION_X, OH_EROZION_Y, OH_EROZION_Z, /*radius*/ 6.0f)
            .Custom(OH_HOOK_GRANT_BOMBS)  // hold the pack -> Brazen's ride option appears
            .Gossip(NPC_BRAZEN, /*option*/ 0, /*searchRadius*/ 20.0f)
                .SkipIfTargetMissing()
            .TeleportParty(/*checkpoint*/ OH_ENTRANCE_X, OH_ENTRANCE_Y, OH_ENTRANCE_Z,
                           /*landing (drop-off outside the walls)*/ OH_DROP_X, OH_DROP_Y, OH_DROP_Z,
                           /*radius*/ 200.0f)
            .Build());

    // (2) CREATE A DISTRACTION, THEN SLAY LIEUTENANT DRAKE.
    // Bomb five barrels; the 5th plant fires the instance's summon of Lt. Drake
    // (~18s later) up on the keep platform. WaitForSpawn(Drake) is the real "5
    // counted" check (a same-barrel double-hit never advances the count, so it
    // would time out loudly rather than pass). KillCreatureEngage seeks up to the
    // platform and kills him. PERSISTENT: the Drake fight is a >1s combat gap that
    // would otherwise rewind the step chain and re-bomb the barrels.
    out.push_back(
        EventBuilder(560, 2, "Create a distraction and slay Lieutenant Drake")
            .Anchored(/*orderIndex*/ 2)
            .Persistent()
            // Step 0: a lenient MoveTo at the courtyard anchor. Its only job is to
            // reach stepIndex 1 so the persistent event goes STICKY (roam-enabled)
            // — otherwise driving to the first barrel moves the tank off the anchor
            // while stepIndex is still 0, the at-objective gate closes, and the
            // advance action fights the barrel driver back to the anchor. The radius
            // is generous because the exact anchor spot is blocked ~13yd out (the
            // tank can't stand on it), so we treat "near the courtyard" as arrived.
            .MoveTo(OH_KEEP_X, OH_KEEP_Y, OH_KEEP_Z, /*radius*/ 30.0f)
            // Real item-use per barrel (the pack of bombs is the barrel lock's KEY
            // ITEM: spell 32744 is OPEN_LOCK vs lock 1682, so only the item-use
            // cast hits and fires the SmartAI SPELLHIT — the Deadmines-cannon
            // rule). Tight cast reach (executor default 3.5yd strict + vmap LOS —
            // the GO interact box live-failed at 6.0yd, and a wall between tank
            // and barrel must never count as arrived) forces the tank to walk
            // INTO each house; the step latches Done on the barrel leaving
            // GO_READY (a landed plant activates the goober for 86400s).
            // One step per HOUSE, anchored on the house's pooled-candidate
            // centroid (see the pooling note above) — the executor finds whichever
            // of the 3 candidate spots this instance rolled. 120s per step: the
            // NE houses are a ~170yd walk from the courtyard, with guards to
            // fight through (the 30s default mis-fired even on the courtyard
            // houses — a walk-in + 2.5s plant measured 32-51s live).
            .UseItemOnGO(OH_ITEM_BOMBS, OH_SPELL_PLANT, OH_GO_BARREL, OH_HOUSE5_X, OH_HOUSE5_Y, OH_HOUSE5_Z)
                .Timeout(120000)
            .UseItemOnGO(OH_ITEM_BOMBS, OH_SPELL_PLANT, OH_GO_BARREL, OH_HOUSE4_X, OH_HOUSE4_Y, OH_HOUSE4_Z)
                .Timeout(120000)
            .UseItemOnGO(OH_ITEM_BOMBS, OH_SPELL_PLANT, OH_GO_BARREL, OH_HOUSE3_X, OH_HOUSE3_Y, OH_HOUSE3_Z)
                .Timeout(120000)
            .UseItemOnGO(OH_ITEM_BOMBS, OH_SPELL_PLANT, OH_GO_BARREL, OH_HOUSE2_X, OH_HOUSE2_Y, OH_HOUSE2_Z)
                .Timeout(120000)
            .UseItemOnGO(OH_ITEM_BOMBS, OH_SPELL_PLANT, OH_GO_BARREL, OH_HOUSE1_X, OH_HOUSE1_Y, OH_HOUSE1_Z)
                .Timeout(120000)
            // The 5th plant torched house 1; walk the party out into the open yard
            // (OH_MUSTER) before Drake spawns so nobody parks in the fire (which
            // both damaged them and blocked drinking, deadlocking the run). Drake
            // is engaged from here. Generous radius/timeout: the muster point is a
            // ~50yd walk back from the last barrel, possibly through guard fights.
            .MoveTo(OH_MUSTER_X, OH_MUSTER_Y, OH_MUSTER_Z, /*radius*/ 6.0f)
                .Timeout(60000)
            .WaitForSpawn(NPC_LT_DRAKE, /*wantAlive*/ true, /*timeout*/ 60000)
            .KillCreatureEngage(NPC_LT_DRAKE, /*count*/ 1, /*searchRadius*/ 200.0f)
            .Build());

    // (3) FREE THRALL AND ESCORT HIM TO FREEDOM.
    // ONE EscortCreature step, gated on the progress counter reaching FINISHED (9)
    // — Epoch Hunter dead. The escort driver:
    //   * gossips Thrall whenever he raises his gossip flag (release from the cell,
    //     mount up after Skarloc, start the Tarren Mill waves, the Epoch cutscene,
    //     and to resume after a death-reset) — his single "talk to continue" signal;
    //   * follows + engages every attacker (prison-camp ambushes, Captain Skarloc,
    //     the barn/church/inn guard waves, the three Infinite waves, and Epoch
    //     Hunter himself — all of which attack Thrall);
    //   * speed-matches the party to Thrall's 1.6x mount on the long Tarren Mill
    //     ride so his npc_escortAI never resets him for the party falling >100yd back.
    // Wide escortee scan (150yd) so the mounted ride never loses him off-grid; a
    // slightly wider threat radius (25yd) catches the waves that rush him.
    // PERSISTENT: the escort spans a dozen+ combat gaps that would rewind a normal
    // anchored event. Step 0 (a short MoveTo to the spot outside the prison gate —
    // see OH_CELL_GATE) bumps stepIndex to 1 so the persistence sticky-trigger
    // engages and the tank can roam the whole zone from the anchor while the
    // escort drives. The tight 4yd radius plants the tank AT the gate, within
    // gossip range of Thrall THROUGH it — the escort driver then never has to
    // issue a move toward his inside-the-cell spawn (which would path into the
    // closed door), and his script opens the door after the gossip.
    out.push_back(
        EventBuilder(560, 3, "Free Thrall and escort him to freedom")
            .Anchored(/*orderIndex*/ 3)
            .Persistent()
            .MoveTo(OH_CELL_GATE_X, OH_CELL_GATE_Y, OH_CELL_GATE_Z, /*radius*/ 4.0f)
            .EscortCreature(/*escortee*/ NPC_THRALL, /*startGossipOption*/ 0,
                            /*doneEntry*/ 0, /*doneBit*/ -1,
                            /*standoff*/ 5.0f, /*threatRadius*/ 25.0f,
                            /*threatZBand*/ 20.0f, /*searchRadius*/ 150.0f,
                            /*doneDataId*/ static_cast<int32>(OH_DATA_ESCORT_PROGRESS),
                            /*doneDataMin*/ OH_PROGRESS_FINISHED)
            .Build());
}

// --- roster patch: the three objectives (no derived roster; bosses are summons) --
void RegisterOldHillsbradRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = 560;
    p.add = {
        MakeObjective(OBJ(1), /*encounterIndex*/ 1, 560, "Ride to Durnholde Keep",
                      OH_ENTRANCE_X, OH_ENTRANCE_Y, OH_ENTRANCE_Z, /*arriveRadius*/ 12.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 1, /*orderOverride*/ 1),
        MakeObjective(OBJ(2), /*encounterIndex*/ 2, 560,
                      "Create a distraction and slay Lieutenant Drake",
                      OH_KEEP_X, OH_KEEP_Y, OH_KEEP_Z, /*arriveRadius*/ 25.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 2, /*orderOverride*/ 2),
        MakeObjective(OBJ(3), /*encounterIndex*/ 3, 560, "Free Thrall and escort him to freedom",
                      OH_CELL_GATE_X, OH_CELL_GATE_Y, OH_CELL_GATE_Z, /*arriveRadius*/ 8.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 3, /*orderOverride*/ 3),
    };
    t.push_back(std::move(p));
}
