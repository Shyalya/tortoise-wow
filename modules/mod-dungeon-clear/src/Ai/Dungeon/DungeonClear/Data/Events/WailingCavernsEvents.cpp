/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- Wailing Caverns (map 43) — the DROP to LORD SERPENTIS, ANCHORED + PERSISTENT ---
// Lord Serpentis (3673) sits on an upper-ground navmesh component that the party
// reaches only by running to a ledge and DROPPING DOWN onto it. Recast bakes no
// off-mesh connection across that vertical gap (the island dumper shows the
// Serpentis shelf as a disconnected component), so stock boss-nav declares him
// unreachable and the clear stalls at the lip.
//
// Navigation is by the boss list: an OBJECTIVE anchor is added at the LIP
// (BossRosterRegistry, encounterIndex 5 — shared with Serpentis; the
// objective-before-boss tie-break runs it first). Boss-nav drives the tank to
// the lip exactly as to any boss — the lip is on the approach-side mesh, so it
// IS reachable — and the event then jumps the one off-mesh leg. Once landed the
// bot is on Serpentis's island and stock nav walks it the rest of the way; the
// objective latches and the clear advances to Serpentis (bit 5) and Verdan
// (bit 6).
//
// Coords are from in-game observation (a GM stood at the jump): the lip is the
// edge the player leaps from, the landing a few yards onto the lower shelf
// (~5.6yd drop, ~9.4yd across — a clean run-speed MoveJump).
//
// PERSISTENT because the drop is ONE-WAY: the bot cannot path back up across the
// gap. A non-persistent anchored event REWINDS to step 0 after any >1s Drive gap
// (e.g. the bot lands among trash and the combat engine takes over for a few
// seconds); that rewind would re-run MoveTo(lip) and walk the bot BACK toward
// the now-unreachable lip. Persistence keeps the step progress across the gap so
// the jump fires exactly once and never regresses. The Jump step is itself
// idempotent (Done once within radius of the landing), a second guard against
// re-firing.

void RegisterWailingCavernsEvents(std::vector<DungeonEvent>& out)
{
    // The ledge the party leaps FROM (approach-side mesh, where the objective
    // anchor sits) and the shelf it lands ON (Serpentis's island).
    constexpr float WC_LIP_X  = -290.65567f;
    constexpr float WC_LIP_Y  = -3.8297224f;
    constexpr float WC_LIP_Z  = -58.30473f;
    constexpr float WC_LAND_X = -285.45773f;
    constexpr float WC_LAND_Y = 4.021016f;
    constexpr float WC_LAND_Z = -63.919395f;

    out.push_back(
        EventBuilder(43, 1, "Drop to Lord Serpentis")
            .Anchored(/*encounterIndex*/ 5)
            .Persistent()
            // 1. Settle exactly on the lip (the objective's arrive radius may
            //    leave the tank a few yards off — too far off and the leap
            //    misses the shelf).
            .MoveTo(WC_LIP_X, WC_LIP_Y, WC_LIP_Z, /*radius*/ 3.0f)
            // 2. Leap the off-mesh gap onto Serpentis's shelf. Done once landed;
            //    stock boss-nav then carries the bot to Serpentis.
            .Jump(WC_LAND_X, WC_LAND_Y, WC_LAND_Z, /*radius*/ 5.0f)
            .Build());

    // --- The finale: ESCORT the DISCIPLE OF NARALEX, ANCHORED + PERSISTENT ---
    // After the four Fanglords are dead the dungeon's last objective is a scripted
    // ESCORT: talk to the Disciple of Naralex (3678) to start him, then protect him
    // along a 25-waypoint path (three ambushes + a banish ritual) to the ritual
    // chamber, where he summons MUTANUS THE DEVOURER (3654) — the final boss.
    //
    // mod-playerbots will NOT put a bot into combat when only a non-party escortee
    // is attacked (no bot receives a threat event), so aggro propagation alone lets
    // the Disciple die while the party just keeps following. The EscortCreature step
    // exists exactly to bridge that: it actively engages whatever hits him, which
    // pulls the rest of the party in via the leader-fight assist seam.
    //
    // ANCHORED + PERSISTENT, never Conditional+Repeatable: the escort spans four+
    // separate fights (3 ambushes + the final-chamber waves + Mutanus), and the
    // executor REWINDS a non-persistent event to step 0 after any >1s driving-engine
    // gap — which every wave's combat is. Persistence keeps stepIndex across the
    // gaps; the sticky at-objective trigger (stepIndex >= 1) lets the tank roam the
    // whole route from the anchor. Step 0 is a short MoveTo to the Disciple so
    // stepIndex reaches 1 (the persistence sticky); step 1 is the escort proper
    // (its self-heal re-runs the start gossip itself, so a dead Disciple recovers
    // even though a Persistent event won't auto-rewind to a step-0 Gossip).
    //
    // Completion is gated STRICTLY on Mutanus existing (grid scan) / his encounter
    // bit (7) — never on "reached the end" — to avoid the DM-West/RFD premature-
    // completion class of bug. Once it latches, the roster's Mutanus BOSS anchor
    // takes over and the normal kill-bit completes the dungeon.
    constexpr float WC_DISCIPLE_X = -134.97f;
    constexpr float WC_DISCIPLE_Y = 125.40f;
    constexpr float WC_DISCIPLE_Z = -78.09f;

    // --- The RETURN-FALL: drop off Verdan's shelf to the lower caverns --------
    // After Verdan the dungeon's last objective (the Disciple escort) sits back
    // DOWN in the lower caverns, reachable only by dropping through a narrow
    // vertical hole behind Verdan into the water below. Boss-nav can't path the
    // one-way vertical gap (the shelf and the deep water floor are disjoint mesh
    // tiers), so — exactly like the Serpentis drop — an OBJECTIVE anchor at the
    // LIP (reachable approach-side mesh) drives the tank there, then this event
    // drops it the one off-mesh leg.
    //
    // The navmesh (probed offline with tools/probe_navmesh.py) shows the lip
    // column is FOUR stacked tiers: shelf -27.5 / a mid-shaft LEDGE -58 / the deep
    // water floor -105.8 / -111.5. That ledge is the trap that beat the earlier
    // attempts: a plain MoveFall straight off the lip catches it, and a ballistic
    // Jump clips the narrow shaft wall. DropInHole instead glides the leader ~7yd
    // in +X to (-49.5, 47.6) — the one spot whose column is open STRAIGHT to the
    // -105.8 floor — then MoveFall()s PURE-VERTICAL into the water (no horizontal
    // travel => no wall clip; clears the -58 ledge). A polygon-adjacency BFS over
    // the merged mesh confirms the deep floor is ONE connected component with the
    // Disciple's poly, so stock nav routes the party to him after landing.
    //
    // Followers can't reproduce the off-mesh nudge, so for the whole drop they HOLD
    // at the ledge top (DcLeaderSignal::IsLeaderDroppingInHole — a MoveFollow to the
    // far-below tank can't path the shaft and would clip them straight down through
    // its wall) and TELEPORT to the landing once the tank has finished falling (the
    // sanctioned one-way-drop fix, shared with the Serpentis Jump).
    //
    // PERSISTENT + ANCHORED, same one-way reasoning as the Serpentis drop: the bot
    // can't path back UP across the gap, so the step progress must survive any >1s
    // combat gap (the party may land among lower-caverns fauna) rather than
    // rewinding to MoveTo(lip) and walking back toward the now-unreachable lip.
    constexpr float WC_DROP_LIP_X  = -55.89f;
    constexpr float WC_DROP_LIP_Y  =  44.32f;
    constexpr float WC_DROP_LIP_Z  = -29.01f;
    // Over the OPEN shaft mouth (a short +X nudge from the lip), held at shelf Z;
    // its column is clear straight down to the deep floor (no shelf / no -58 ledge).
    constexpr float WC_DROP_OVER_X = -49.5f;
    constexpr float WC_DROP_OVER_Y =  47.6f;
    constexpr float WC_DROP_OVER_Z = -29.0f;
    // The deep water floor directly below the over-hole point (probe centroid;
    // BFS-confirmed connected to the Disciple).
    constexpr float WC_DROP_LAND_X = -49.5f;
    constexpr float WC_DROP_LAND_Y =  47.6f;
    constexpr float WC_DROP_LAND_Z = -105.83f;

    out.push_back(
        EventBuilder(43, 3, "Drop down to the lower caverns")
            .Anchored(/*orderIndex*/ 7)
            .Persistent()
            // Step 0: settle on the lip (the objective arrive radius may leave the
            //         tank a few yards off; the drop needs it at the shaft edge so
            //         stepIndex also reaches 1, the persistence sticky).
            .MoveTo(WC_DROP_LIP_X, WC_DROP_LIP_Y, WC_DROP_LIP_Z, /*radius*/ 4.0f)
            // Step 1: glide over the open shaft mouth, MoveFall into the water, then
            //         teleport the held followers down. Done once the fall finishes.
            .DropInHole(WC_DROP_OVER_X, WC_DROP_OVER_Y, WC_DROP_OVER_Z,
                        WC_DROP_LAND_X, WC_DROP_LAND_Y, WC_DROP_LAND_Z)
            .Build());

    out.push_back(
        EventBuilder(43, 2, "Escort the Disciple of Naralex")
            .Anchored(/*encounterIndex*/ 7)
            .Persistent()
            // Step 0: close to the Disciple so the persistent stepIndex reaches 1
            //         (the escort step's own start branch walks the last yards to
            //         gossip range, so this just needs to get near him).
            .MoveTo(WC_DISCIPLE_X, WC_DISCIPLE_Y, WC_DISCIPLE_Z, /*radius*/ 5.0f)
            // Step 1: start (gossip menu 201 option 0), follow + protect, and
            //         complete on Mutanus (3654, encounter bit 7).
            .EscortCreature(/*escortee*/ 3678, /*startGossipOption*/ 0,
                            /*doneEntry*/ 3654, /*doneBit*/ 7)
            .Build());
}

// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterWailingCavernsRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- Wailing Caverns (map 43) — Serpentis drop + Mutanus escort ---
    // Two roster additions, both backed by WailingCavernsEvents.cpp:
    //
    // (1) Lord Serpentis (3673, encounterIndex 5) sits on an upper-ground
    // navmesh island the party reaches only by running to a ledge and
    // DROPPING onto it; Recast bakes no off-mesh link across the gap, so
    // stock boss-nav calls him unreachable. Add an OBJECTIVE anchor at the
    // LIP (approach-side mesh -> reachable) sharing Serpentis's bit 5; the
    // objective-before-boss tie-break drives the tank there first, the
    // event (eventId 1) jumps the gap, then the clear advances to
    // Serpentis (now on the same island). No gateEntry: the event owns
    // completion via the Jump landing.
    //
    // (2) The finale is a scripted ESCORT of the Disciple of Naralex (3678)
    // to the ritual chamber, where he summons MUTANUS THE DEVOURER (3654) —
    // the dungeon's final boss. Mutanus is a TempSummon (spawnId 0), so
    // BossSpawnIndex never emits him; add him as a BOSS at his summon spot
    // with his real DungeonEncounter bit 7 (instance_encounters credit 3654,
    // the 8th WC encounter after Anacondra/Cobrahn/Kresh/Pythas/Skum 0-4,
    // Serpentis 5, Verdan 6), so his kill flips bit 7 and completes the run.
    // The Disciple/escort OBJECTIVE (eventId 2) sits at the same key 7 and,
    // by the objective-before-boss tie-break, is reached FIRST: the party
    // escorts the Disciple, he summons Mutanus, the escort completion gate
    // latches, and the picker hands over the same-key Mutanus boss next.
    //
    // (3) The RETURN-FALL: after Verdan the escort objective is back DOWN
    // in the lower caverns, reachable only by dropping through a narrow
    // vertical hole behind Verdan into the water. An OBJECTIVE anchor at the
    // LIP (eventId 3, ordering key 7) drives the tank there; its DropInHole
    // event glides the leader over the open shaft mouth and MoveFall()s it
    // pure-vertical into the water (the lip column stacks shelf/-58 ledge/
    // -105.8 floor — a blind drop catches the ledge; see WailingCavernsEvents
    // and tools/probe_navmesh.py). Ordering key 7 with objective-before-boss
    // and insertion-before-escort places it Verdan(6) -> hole-drop -> escort
    // -> Mutanus; it latches by entry on event completion (objectives carry
    // no kill-bit, so sharing key 7 with Mutanus is harmless).
    {
        BossRosterPatch p;
        p.mapId = 43;

        // Mutanus: built (not MakeBoss) so we can stamp his own real
        // encounterIndex 7 (he inherits from no removed entry).
        DungeonBossInfo mutanus =
            MakeBoss(3654, 43, "Mutanus the Devourer",
                     151.27f, 252.26f, -102.82f, /*completionFrom*/ 0);
        mutanus.encounterIndex = 7;

        p.add = {
            MakeObjective(OBJ(1), /*encounterIndex*/ 5, 43, "Drop to Lord Serpentis",
                          -290.65567f, -3.8297224f, -58.30473f, /*arriveRadius*/ 6.0f,
                          /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 1),
            // Return-fall off Verdan's shelf (key 7, BEFORE the escort by
            // insertion order; see the (3) note above). Backed by event 3.
            MakeObjective(OBJ(3), /*encounterIndex*/ 7, 43,
                          "Drop down to the lower caverns",
                          -55.89f, 44.32f, -29.01f, /*arriveRadius*/ 6.0f,
                          /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 3),
            MakeObjective(OBJ(2), /*encounterIndex*/ 7, 43,
                          "Escort the Disciple of Naralex",
                          -134.97f, 125.40f, -78.09f, /*arriveRadius*/ 18.0f,
                          /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 2),
            mutanus,
        };
        t.push_back(std::move(p));
    }
}
