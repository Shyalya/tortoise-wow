/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- Azjol-Nerub (map 601) ------------------------------------------------
//
// Three encounters, DungeonEncounter bits 0/1/2 already in travel order
// (Krik'thir 28684, Hadronox 28921, Anub'arak 29120), so no boss is reordered
// for ordering's sake. Two things need authoring, and both are structural:
//
//   1. HADRONOX IS FOUGHT ON THE UPPER PLATFORM, NOT WHERE SHE SPAWNS, and the
//      party has to kill three specific mobs before she will come up.
//   2. THE WAY ON FROM HER IS A HOLE IN THE FLOOR with a 360yd drop and no
//      navmesh across it.
//
// --- 1. Hadronox: the crusher packs are the swarm's off-switch --------------
//
// The chamber is a vertical shaft. Measured against the live 601 mmtiles:
//
//     z 777   Krik'thir's gate (the party enters here)
//     z 760-767  two upper ledges, west (493,603) and east (567,603)
//     z 730-735  THE PLATFORM — one flat deck ~50yd across
//     z 674   Hadronox's static spawn ledge (522.5, 544.9)
//     z 646-660  the pit floor, with the hole in it (see part 2)
//
// The whole shaft is ONE navmesh component, so nothing stops boss navigation
// walking the tank all the way down to her spawn — 500yd of spiral by Dijkstra
// over the poly graph, ending 10yd from (532.8, 535.2, 681.1), which is the
// last node of every add-swarm waypoint path. That is the worst place in the
// dungeon to stand.
//
// What the encounter actually does (boss_hadronox.cpp + creature_summon_groups):
//
//   * Reset() summons CRUSHER PACK 1 onto the platform (Anub'ar Crusher 28922 +
//     Champion 29117 + Crypt Fiend 29118) and three WORLD TRIGGERS (23472) at
//     (583.1,617.4,771.6), (477.0,618.4,771.5) and (581.4,608.8,739.4) — the
//     three doors. All three trigger positions are on mesh islands DISCONNECTED
//     from the party's component, so the swarm's source can never be reached.
//   * Each trigger carries three periodic summon auras (53035/53036/53037,
//     champion/necromancer/crypt fiend, every 15s/10s/5s). Their spell script
//     keeps firing FOR AS LONG AS THE BOSS IS NOT DONE, so the swarm is
//     INFINITE by construction. The adds walk DB paths 3000012/13/14, all of
//     which converge on (538,530,686) -> (532.8,535.2,681.1), i.e. onto
//     Hadronox, and she eats them.
//   * Every add she kills that is carrying her own Leech Poison (53030) heals
//     her 10% of MAX HP (boss_hadronox::KilledUnit). An infinite swarm is an
//     infinite heal: she is not killable while the doors are open.
//   * Engaging pack 1's Anub'ar Crusher fires ACTION_CRUSHER_ENGAGED, which
//     sets the encounter IN_PROGRESS, summons CRUSHER PACKS 2 and 3 onto the
//     two upper ledges, and schedules MOVE1 (0s), MOVE2 (45s), MOVE3 (70s).
//   * MOVE3 is the off-switch. It re-arms itself every 2s while `_crushersLeft
//     > 0`, so it only fires once EVERY ANUB'AR CRUSHER (28922 — one per pack,
//     three in the dungeon) IS DEAD. On arrival at (530.4, 560.0, 733.2) —
//     ON THE PLATFORM — MovementInform casts Web Front Doors (53177) and the
//     swarm stops.
//   * The packs come to the party: npc_hadronox_crusherPackAI's ACTION_PACK_WALK
//     MovePoints every member of packs 2 and 3 down onto the SAME platform
//     (crusherWaypoints z 731.9-734.0). Nobody has to climb to the ledges.
//
// So the correct clear is: stand on the platform, kill the three Anub'ar
// Crushers as their packs walk in, wait for Hadronox to climb up and web the
// doors, then fight her there. That is exactly what the objective below does,
// and it is why her roster anchor is MOVED from her spawn ledge to her own
// script's MOVE3 destination.
//
// A note on what is NOT the problem, because it is the first thing to suspect:
// THERE IS NO CLIENT PACKET TO RELAY HERE. AreaTrigger.dbc has three rows on map
// 601 — 5113 and 5115 (the two instance portals) and 5292 at (549.2,433.3,286.0)
// — and none of them has an areatrigger_scripts, areatrigger_teleport,
// areatrigger_involvedrelation or smart_scripts source_type 2 row. There is no
// gossip, no lever and no clickable gameobject on the whole map. The swarm is
// driven entirely server-side off periodic auras on summoned world triggers, so
// DcTestAreaTriggers has nothing to send here (contrast the Underbog's Ghaz'an
// and the Shattered Halls' Nethekurse, which really are missing-packet bugs).
// The swarm "not working" is the crusher gate never being satisfied.
//
// --- 2. The drop into the lower kingdom ------------------------------------
//
// Flood-filling the four 601 mmtiles and unioning polys by dtPoly.neis gives two
// large components that do not touch:
//
//     comp1  z 645.5 .. 833.3   the entrance, Krik'thir, the whole Hadronox shaft
//     comp0  z 195.4 .. 310.0   the lower kingdom, out to Anub'arak and the exit
//
// The join is a HOLE in the pit floor, ~11 x 13yd at x 528-539 / y 543-556.
// Column-probing the mesh either side of it:
//
//     (522, 548)  ... 674.85, 648.87, 288.62      <- pit floor present
//     (533, 550)  ... 674.85,         288.62      <- open shaft, no floor
//     (535, 550)  ... 681.02,         288.62      <- open shaft, no floor
//     (541, 550)  ... 681.02, 647.89, 288.62      <- pit floor present
//
// One surface under the whole hole, at z 288.62, and from there Dijkstra walks
// 347yd to Anub'arak with no further break. That is a ~360yd fall, so this is a
// TeleportParty and not a DropInHole: the leader would have to survive the fall
// for the drop primitive, and nothing in the module makes that safe.
//
// The checkpoint is on the pit floor 6yd west of the hole's rim; the landing is
// directly beneath the hole's centre. Both probed against the live mmaps.
//
// --- also authored elsewhere for this map ---------------------------------
//   * DcHazardRegistry — Acid Cloud (53400 / heroic 59419), the 90-second 5yd
//     poison pool Hadronox drops on a random party member.
//   * FightInPlaceRegistry — the whole shaft, so the advanced pull cannot drag a
//     crusher pack off its home and trip SummonedCreatureEvade (which resets the
//     entire encounter).
//   * SealedEncounterRegistry — Anub'arak's arena, whose three DOOR_TYPE_ROOM
//     doors shut 5s after the pull.

namespace
{
    // --- Hadronox ---------------------------------------------------------

    constexpr uint32 AN_HADRONOX = 28921;
    // Anub'ar Crusher. One per crusher pack, three on the map, and the ONLY
    // entry `_crushersLeft` counts (ACTION_CRUSHER_DIED is sent from
    // npc_anub_ar_crusher::JustDied alone). Killing all three is what lets
    // EVENT_HADRONOX_MOVE3 fire.
    constexpr uint32 AN_ANUBAR_CRUSHER = 28922;

    // The platform, and Hadronox's own MOVE3 destination
    // (boss_hadronox.cpp hadronoxSteps[2] = 530.420, 560.003, 733.22473).
    // Mesh probed at (530.4, 560.0): a single walkable surface at z 733.82, the
    // same deck the three crusherWaypoints (z 731.9-734.0) sit on.
    constexpr float AN_PLATFORM_X = 530.4f;
    constexpr float AN_PLATFORM_Y = 560.0f;
    constexpr float AN_PLATFORM_Z = 733.8f;

    // Arrival radius for the platform objective. Deliberately loose: the deck is
    // ~50yd across, the three pack parking spots are 13-16yd from the anchor, and
    // the point of the objective is "be up here", not "be on this exact tile".
    constexpr float AN_PLATFORM_ARRIVE = 10.0f;

    // Step timeouts. The 30s EventStepTimeout default is far short of both.
    //   crushers: pack 1 fight + packs 2/3 walking ~65yd down from the ledges +
    //     two more fights, all under a swarm that never stops.
    //   web: MOVE3 is scheduled at 70s from ACTION_CRUSHER_ENGAGED and only
    //     re-checks every 2s, then she still has to walk up from wherever MOVE1 /
    //     MOVE2 left her (z ~695, up to ~120yd of path).
    constexpr uint32 AN_CRUSHERS_TIMEOUT = 300000;
    constexpr uint32 AN_WEB_TIMEOUT = 240000;

    // ObjectiveHookRegistry id — "Hadronox has webbed the front doors".
    // 13, not 11: id 11 is the retired Black Morass BmPullDrainers hook and is
    // deliberately left unused so an old log line naming it stays legible
    // (asserted in t/TestEventRegistry.cpp).
    constexpr uint32 AN_HOOK_HADRONOX_WEBBED_DOORS = 13;

    // --- the drop ---------------------------------------------------------

    // Pit floor, 6yd west of the hole's rim. Column probe: 878.42 / 734.55 /
    // 674.85 / 648.87 / 288.62 — one pit-floor surface, unambiguous.
    constexpr float AN_DROP_CHECKPOINT_X = 522.0f;
    constexpr float AN_DROP_CHECKPOINT_Y = 548.0f;
    constexpr float AN_DROP_CHECKPOINT_Z = 648.9f;

    // NOT under the hole — 68yd south of it, on the dry bank past the lake.
    // Hand-picked in game and column-probed against the live 601 mmtiles: one
    // walkable surface at z 289.53, NAV_GROUND, nothing else in the column
    // within 400 yards. TeleportParty exists precisely for a "big DIAGONAL
    // drop", so nothing requires the landing to sit under the checkpoint.
    //
    // WHY NOT UNDER THE HOLE. Two independent traps live directly beneath it,
    // and this one coordinate steps past both:
    //
    //   * THE LAKE. The z 288.6 sheet under the whole hole is NAV_WATER — the
    //     mmap generator meshes the liquid SURFACE — with the pool floor 8-12yd
    //     below at z 276-281. The lake runs x 470-641 / y 404-587, so a party
    //     landing in it swims for the first 145yd of the lower kingdom no matter
    //     where inside the hole it comes down; the nearest dry ground is a ledge
    //     17yd south of the hole's centre. Landing at y 481 is past the whole
    //     thing.
    //   * THE MMTILE SEAM at x = 533.3333. Map 601's tiles meet there, and at
    //     y 512 the mesh beside the seam is a fan of three sliver triangles all
    //     pinned to one shared vertex (533.33, 512.00, 290.95) — 0.02, 0.09 and
    //     0.007 yards thick. LongRangePathfinder's smoothing walk cannot
    //     reliably thread that; five times in six it burns all 4096 of its
    //     points doubling back inside an 8yd box and hands the follower a route
    //     that reverses on itself (tr-20260818-182556-1). x 544 is 11yd clear.
    //
    // The checkpoint is unchanged: the party still musters on the pit floor at
    // the hole's rim and is relocated from there, so the walk up to the drop
    // reads the same. Only where they come down moved.
    constexpr float AN_DROP_LANDING_X = 544.18f;
    constexpr float AN_DROP_LANDING_Y = 481.26f;
    constexpr float AN_DROP_LANDING_Z = 288.98f;

    // Arrival radius for the drop objective. 6yd, matching The Underbog's ledge:
    // the checkpoint is 6yd from a 360yd drop, so "arrived" has to mean arrived.
    constexpr float AN_DROP_ARRIVE = 6.0f;

    // --- clear-order keys -------------------------------------------------
    // Real kill-bits (0/1/2) are untouched; this is only the travel sequence.
    constexpr int32 AN_ORDER_KRIKTHIR = 1;
    constexpr int32 AN_ORDER_CRUSHERS = 2;
    constexpr int32 AN_ORDER_HADRONOX = 3;
    constexpr int32 AN_ORDER_DROP     = 4;
    constexpr int32 AN_ORDER_ANUBARAK = 5;

    constexpr uint32 AN_KRIKTHIR = 28684;
    constexpr uint32 AN_ANUBARAK = 29120;
}

void RegisterAzjolNerubEvents(std::vector<DungeonEvent>& out)
{
    // Hold the platform until the swarm's off-switch has been thrown.
    //
    // Step 1 is a GARRISON, not a sweep: the crusher packs walk to the party
    // (ACTION_PACK_WALK MovePoints all nine members onto this deck), so seeking
    // them would only march the tank up a ramp into the add stream to meet mobs
    // that were already on their way down. The gate is entry-keyed on the
    // Anub'ar Crusher because that is precisely what `_crushersLeft` counts —
    // the champions / crypt fiends / necromancers die on the way but do not move
    // the counter. DC_EVENT_CREATURE_SCAN is 250yd, so the two ledge spawns 67yd
    // away are inside the gate's reach from the moment they appear.
    //
    // Step 2 waits for the web. Killing the crushers only makes MOVE3 ELIGIBLE;
    // it is still scheduled at 70s and she still has to walk up. Releasing the
    // objective the instant the last crusher dies would hand her to boss
    // navigation while she is at z ~695 — the tank would walk down to meet her,
    // and the swarm would keep pouring in the whole time. The hook reads the
    // encounter's own `_doorsWebbed` through the 'Hadronox Denied' achievement
    // probe (boss_hadronox::GetData(28921), 0 once the doors are webbed), which
    // is the only exposure that flag has.
    //
    // OPTIONAL, so a wedged crusher (a pack that never finishes its walk, an
    // evade that eats the counter) degrades into "fight her wherever she is"
    // rather than stalling the run for the human. Same call as the Utgarde Keep
    // forges.
    //
    // PERSISTENT because both steps span combat gaps: the party is fighting a
    // continuous swarm through the whole event, and a >1s Drive gap rewinding to
    // step 0 would re-arm the crusher gate. Both steps are idempotent, so the
    // rewind would be survivable — persistence is what stops the at-objective
    // trigger from going false while the tank is pushed around the deck.
    out.push_back(EventBuilder(601, 1, "Hadronox: web the doors")
                      .Anchored(/*orderIndex (doc)*/ AN_ORDER_CRUSHERS)
                      .Optional()
                      .Persistent()
                      .MoveToHoldUntilSpawn(AN_PLATFORM_X, AN_PLATFORM_Y, AN_PLATFORM_Z,
                                            AN_PLATFORM_ARRIVE,
                                            AN_ANUBAR_CRUSHER, /*wantAlive*/ false)
                          .Timeout(AN_CRUSHERS_TIMEOUT)
                      .Custom(AN_HOOK_HADRONOX_WEBBED_DOORS)
                          .Timeout(AN_WEB_TIMEOUT)
                      .Build());

    // The hole in the pit floor. One hop, so NOT persistent — the single
    // TeleportParty step is idempotent (Done immediately once the leader is on
    // the landing), so a Drive gap restarting at step 0 re-evaluates correctly.
    // Required: there is no other way into the lower kingdom.
    out.push_back(EventBuilder(601, 2, "Drop into the lower kingdom")
                      .Anchored(/*orderIndex (doc)*/ AN_ORDER_DROP)
                      .TeleportParty(AN_DROP_CHECKPOINT_X, AN_DROP_CHECKPOINT_Y,
                                     AN_DROP_CHECKPOINT_Z,
                                     AN_DROP_LANDING_X, AN_DROP_LANDING_Y,
                                     AN_DROP_LANDING_Z)
                      .Build());
}

// --- roster patch ---------------------------------------------------------
void RegisterAzjolNerubRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = 601;

    // HADRONOX MOVES. The auto-roster anchors her on her static spawn
    // (522.5, 544.9, 674.7) — a ledge 60yd below the platform, 10yd from where
    // every add-swarm waypoint path ends, and 500yd of spiral from the party's
    // side of the chamber. Her own script walks her to (530.4, 560.0, 733.2) to
    // web the doors, and that is where the fight is meant to happen, so that is
    // her anchor. Probed: one walkable surface at z 733.82, the deck the three
    // crusherWaypoints share.
    //
    // remove + re-add rather than `reorder` because only remove+add can carry
    // hand-authored coordinates; completionFrom = her own entry keeps her real
    // DungeonEncounter kill-bit (1), resolved off the base list before the
    // removal takes effect (ApplyOne step 1). orderOverride has to travel on the
    // MakeBoss call for the same reason — p.reorder only touches entries that
    // survived the removal.
    p.remove = { AN_HADRONOX };
    p.add = {
        MakeBoss(AN_HADRONOX, 601, "Hadronox",
                 AN_PLATFORM_X, AN_PLATFORM_Y, AN_PLATFORM_Z,
                 /*completionFrom*/ AN_HADRONOX,
                 /*orderOverride*/ AN_ORDER_HADRONOX),

        // The crusher objective sits ON the same spot, one key ahead of her, so
        // boss navigation walks the tank up here once and the event holds the
        // party there through the whole swarm. encounterIndex is left 0: an
        // objective has no kill-bit and NextDungeonBossValue never tests the
        // completion mask for one, so ordering is entirely orderOverride's job.
        MakeObjective(OBJ(1), /*encounterIndex*/ 0, 601, "Hadronox: web the doors",
                      AN_PLATFORM_X, AN_PLATFORM_Y, AN_PLATFORM_Z,
                      AN_PLATFORM_ARRIVE, /*gateEntry*/ 0, /*hook*/ 0,
                      /*eventId*/ 1, /*orderOverride*/ AN_ORDER_CRUSHERS),

        // The drop, between Hadronox and Anub'arak.
        MakeObjective(OBJ(2), /*encounterIndex*/ 0, 601, "Drop into the lower kingdom",
                      AN_DROP_CHECKPOINT_X, AN_DROP_CHECKPOINT_Y, AN_DROP_CHECKPOINT_Z,
                      AN_DROP_ARRIVE, /*gateEntry*/ 0, /*hook*/ 0,
                      /*eventId*/ 2, /*orderOverride*/ AN_ORDER_DROP),
    };

    // Put the two untouched bosses on the same 1..5 key scale so the objectives
    // have somewhere to sit. Their DBC kill-bits (0 and 2) are unchanged, and
    // their relative order already matched the travel path.
    p.reorder = {
        { AN_KRIKTHIR, AN_ORDER_KRIKTHIR },
        { AN_ANUBARAK, AN_ORDER_ANUBARAK },
    };

    t.push_back(std::move(p));
}

// --- route to Anub'arak ---------------------------------------------------
//
// From the drop landing (544.18, 481.26) south to Anub'arak, ~233yd, entirely
// on dry ground. WHY ANCHORS AND NOT THE PATHFINDER: see AN_DROP_LANDING_X
// above — the mmtile seam at x = 533.3333 defeats the long-range smoothing
// walk, and while the new landing starts the party 11yd clear of it, a route
// built by LongRangePathfinder is free to wander back onto it. The anchors take
// the decision away.
//
// EVERY ANCHOR MUST BE ON DRY GROUND, and that has to be checked explicitly.
// The lower kingdom's first 145yd are a lake (NAV_WATER polys meshed at the
// liquid surface, x 470-641 / y 404-587) and anchor 1 sits only 25yd past its
// southern shore. A plain shortest-path string-pull will happily cut a corner
// through it: the first cut of this route did exactly that and the party swam
// ~80yd across the drop chamber (tp-20260818-200553-1). The stock pathfinder
// never makes that mistake because DungeonClearGeometry::ApplyLiquidAreaCosts
// charges NAV_WATER edges DungeonClear.WaterPathCost (default 3x), but the
// anchor fast-path bypasses the filter entirely — a route authored here has to
// price the water itself. t/TestAzjolNerubRouteProbe is the guard.
//
// StridedPathfinder::Build takes the anchor list as a fast path: each anchor
// becomes a one-point segment the follower walks to IN A STRAIGHT LINE, the
// boss position is appended as the goal, and LongRangePathfinder is never
// called. So every leg below has to be a straight walk, and every leg was
// validated against the live 601 mmtiles: sampled every 0.25yd, each sample on
// a NAV_GROUND poly of the lower-kingdom component and at least 1.75yd from the
// nearest wall edge — the mmap's own walkableRadius is 0.53.
//
// SPACING IS LOAD-BEARING: no leg is longer than 25.1yd, comfortably inside
// DungeonPathFollower::RESNAP_RADIUS (45). InstallLongPath resets the follower
// cursor to segment 0 on every rebuild, and after a trash fight 150yd down the
// route that cursor points at anchor 1 — it is Resnap's forward search finding
// the anchor the tank is actually standing next to that keeps the party from
// walking back north. Anchors farther apart than the resnap radius would
// reintroduce exactly that backtrack.
//
// And note the OTHER thing a registered route arms: both DcAdvanceAction's
// atBoss handoff and DungeonClearAtBossTrigger::IsActive gate the engage on
// "anchored hops still pending", counting from the follower's cursor. They must
// agree; when the trigger's copy counted from segment 0 instead, the party
// walked this whole route and then held 29yd from Anub'arak forever.
void RegisterAzjolNerubRoute()
{
    DungeonClearRouteRegistry::Register(601, DUNGEON_DIFFICULTY_NORMAL, AN_ANUBARAK,
        {
            // south off the landing, hugging the middle of the bank
            { 545.66f, 456.24f, 287.34f },
            // the bank narrows to ~8yd here and the eastern strip is the one
            // that carries on; the two western strips at y 440-450 dead-end
            { 549.00f, 439.72f, 285.35f },
            { 548.82f, 415.27f, 283.70f },
            // the long descent: z 284 -> 224 over ~150yd
            { 547.65f, 390.80f, 267.76f },
            { 548.22f, 366.37f, 244.52f },
            { 548.80f, 341.95f, 240.83f },
            { 549.37f, 317.52f, 235.45f },
            { 549.95f, 293.09f, 227.83f },
            // last anchor before the arena; the goal segment (Anub'arak's own
            // spawn) is appended by StridedPathfinder, 20yd further south.
            { 550.52f, 268.66f, 224.37f },
        });
}
