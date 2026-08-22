/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- Blackrock Depths (map 230) — the RING OF LAW, ANCHORED + PERSISTENT ---
// A sealed arena gauntlet that gates the way between Houndmaster Grebmar and
// Pyromancer Loregrain. A party member steps onto the centre (area trigger
// 1526), the entrance gate slams shut, Grimstone (10096) summons two random
// trash waves then one random boss, and on the boss's death the forward gate
// opens. Nothing the party fights is in the spawn tables — the waves and boss
// are all SUMMONED and randomised — so the event is purely position/state based.
//
// Navigation is done by the boss list: the Ring of Law is added as an OBJECTIVE
// anchor at the arena centre (BossRosterRegistry, encounterIndex 3, between
// Grebmar at 2 and Loregrain at 4). Boss-nav drives the tank into the arena
// exactly as to any boss; the event then runs in place.
//
// The arena is COME-TO-YOU: every summon AttackStart()s a random player within
// 100yd, so the party never pulls — it holds the centre and the combat engine
// fights reactively as the waves and boss arrive. We therefore do NOT use
// ClearRadius (a "done when the band is empty" gate would false-complete during
// the ~12s Grimstone intro and the ~10s wave-2->boss gap, both of which leave
// the floor momentarily empty while the arena is still sealed). It would also
// risk driving an outward engage UP to the hostile spectator gallery (z ~ -35)
// or DOWN toward Grebmar's prison (z ~ -84), which flank the floor (z ~ -54).
//
// Instead the completion gate is the instance state reaching DONE
// (TYPE_RING_OF_LAW == DONE): it can't be missed across a combat tick-gap and
// can't fire during the empty-floor windows. The tank garrisons dead-centre,
// re-centring between fights (the MoveTo re-checks distance every tick), until
// the whole gauntlet is done — at which point the objective latches and the
// clear proceeds out the now-open north gate toward Loregrain.
//
// TYPE_RING_OF_LAW IS NOT MONOTONIC, unlike the phase counters the instance-data
// gate is usually pointed at. npc_grimstone runs its own watchdog (updateReset):
// once a wave or the boss is out and 30s pass with no summon holding a victim, it
// SetData(FAIL)s — which despawns Grimstone and every summon, puts the gates back
// as they were, and drops the state to NOT_STARTED. Nothing in the core restarts
// it, and nothing on our side used to either: the Custom step that fired the
// areatrigger had latched Done, and the test-run areatrigger relay is EDGE-
// triggered on a volume the party is standing in. The party then held an empty
// arena for the whole 600s timeout. That is what tr-20260808-150405-10 did.
// .WhileHolding re-runs the start hook from inside the garrison, so a reset is
// re-fired (the core's own 2-minute post-fail cooldown decides when it takes).
//
// PERSISTENT because a multi-combat arena event sees a >1s Drive gap after each
// wave/boss fight (the bot is on the combat engine); a non-persistent event
// would rewind to step 0 each time.

namespace
{
    // Instance-data accessor + state (mirrors instance_blackrock_depths.cpp's
    // TYPE_RING_OF_LAW / EncounterState; kept local so this TU needn't pull the
    // core BRD header). GetData(TYPE_RING_OF_LAW) returns the live state.
    constexpr uint32 BRD_TYPE_RING_OF_LAW = 1;  // DataTypes::TYPE_RING_OF_LAW
    constexpr uint32 BRD_RING_DONE = 3;          // EncounterState::DONE

    // Arena centre = area trigger 1526 (x,y from AreaTrigger.dbc; z on the floor
    // where the waves/boss spawn, ~3.9yd below the 8yd trigger sphere's centre —
    // well inside it, so arriving here both crosses the trigger and lets the
    // EnsureRingStarted fallback fire it).
    constexpr float BRD_ARENA_X = 596.432f;
    constexpr float BRD_ARENA_Y = -188.498f;
    constexpr float BRD_ARENA_Z = -53.9f;

    // Garrison tolerance, used for BOTH the walk-in and the hold so the two steps
    // agree on where "on the spot" is. Tight on purpose: this is a 4yd leash, not
    // a dead band. It was 10yd on the hold, which is wide enough that the tank
    // simply kept wherever the last wave ended — tr-20260808-150405-10 finished
    // the waves at (599.0, -197.2), 9.7yd out toward the mob gate, and never
    // re-centred because 9.7 < 10. The arena is come-to-you and its script picks
    // targets and spawns the spoils chest around the centre; standing off it is
    // drift, not positioning.
    constexpr float BRD_ARENA_RADIUS = 4.0f;

    // EnsureRingStarted hook (ObjectiveHookRegistry id 1): fires the real area
    // trigger if arrival alone didn't start the encounter (all-bot party / no
    // human on the trigger).
    constexpr uint32 BRD_ENSURE_RING_STARTED_HOOK = 1;

    // --- Shadowforge Lock (event 2) ---------------------------------------
    // The lever in the East Garrison, and the Giant Doors it drives. GOState
    // values are spelled out locally so this TU needn't pull SharedDefines:
    // GO_STATE_ACTIVE = 0, GO_STATE_READY = 1.
    constexpr uint32 BRD_GO_SHADOWFORGE_LOCK = 161460;
    constexpr uint32 BRD_GO_GIANT_DOORS = 157923;
    constexpr uint32 BRD_GO_STATE_READY = 1;

    // The lever's own spawn point, dropped onto the garrison floor (the mmaps
    // surface under it is z -59.82; the GO sits at -60.06 on the wall). The
    // objective anchor delivers the tank here and the UseGO step's own 5yd
    // approach closes whatever is left.
    constexpr float BRD_LOCK_X = 615.61f;
    constexpr float BRD_LOCK_Y = -49.78f;
    constexpr float BRD_LOCK_Z = -59.82f;
    constexpr float BRD_LOCK_RADIUS = 4.0f;

    // Search radius for the Giant Doors from the lever. They are 113yd away at
    // (723.1,-105.9,-71.5) but in the SAME map grid as the lever (both fall in
    // grid 30/32), so the cell visit always has them loaded — this is a state
    // read, not a proximity gate.
    constexpr float BRD_GIANT_DOOR_SCAN = 200.0f;
    constexpr uint32 BRD_GIANT_DOOR_TIMEOUT_MS = 30000;
}

void RegisterBlackrockDepthsEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(
        EventBuilder(230, 1, "Ring of Law")
            .Anchored(/*encounterIndex*/ 3)
            .Persistent()
            // 1. Settle on the trigger spot. Arrival crosses area trigger 1526
            //    (you, or a self-bot relayed from the master) -> IN_PROGRESS.
            .MoveTo(BRD_ARENA_X, BRD_ARENA_Y, BRD_ARENA_Z, BRD_ARENA_RADIUS)
            // 2. Make sure the encounter actually started; if not (autonomous /
            //    no human on the trigger) the hook fires the real trigger. Done
            //    once IN_PROGRESS.
            .Custom(BRD_ENSURE_RING_STARTED_HOOK)
            // 3. Hold dead-centre, re-centring between fights, until the whole
            //    gauntlet is DONE. Combat AI fights the waves + random boss as
            //    they arrive. Generous timeout: the boss fight can be long. The
            //    same hook runs from inside the hold so a Grimstone-side reset
            //    (see the header) restarts the arena instead of stalling it.
            .MoveToHoldUntilInstanceData(BRD_ARENA_X, BRD_ARENA_Y, BRD_ARENA_Z,
                                         BRD_ARENA_RADIUS, BRD_TYPE_RING_OF_LAW,
                                         /*minValue*/ BRD_RING_DONE)
            .WhileHolding(BRD_ENSURE_RING_STARTED_HOOK)
            .Timeout(600000)
            .Build());

    // --- the SHADOWFORGE LOCK, ANCHORED between Bael'Gar and Angerforge ---
    // The Giant Doors at (723.1,-105.9,-71.5) spawn OPEN, and the lower passage
    // they stand in is the way north to Bael'Gar. Closing them is a separate,
    // deliberate act: you walk back to the East Garrison — through the East
    // Garrison Door (170570, lock 680, waived in DcEventDoorRegistry) — and pull
    // the lever at the far end of the room. That is the whole event.
    //
    // OFF-PATH OPENER, so it is ANCHORED rather than Conditional: the lever is
    // 113yd from the doors it moves and a floor above them, so nothing about
    // standing at the doors can bring the party to it (see the door-event-type
    // rule). Boss-nav has to deliver the tank to the lever, which is what an
    // objective anchor is for.
    //
    // The lever needs no key logic here: EventStepKind::UseGameObject calls
    // GameObject::Use() directly, and the DOOR branch of Use() has no lock check
    // at all (locks are adjudicated client-side and, for bots, by
    // BotCanOpenDoorLikePlayer — which the exemption covers for the walk-in
    // case). Use() -> UseDoorOrButton -> SetLootState(GO_ACTIVATED) fires the
    // lever's SmartAI chain SYNCHRONOUSLY, which activates the mechanism, the
    // Giant Doors, and the two collision hulls in one go.
    //
    // NOT persistent, and it does not need to be: every step is idempotent under
    // a rewind. MoveTo re-arrives, UseGO early-returns Done on a lever that is no
    // longer GO_STATE_READY (so a second click can never re-open the doors), and
    // the state check is a plain read.
    out.push_back(
        EventBuilder(230, 2, "Shadowforge Lock")
            .Anchored(/*orderIndex*/ 9)
            // 1. Settle at the lever inside the East Garrison.
            .MoveTo(BRD_LOCK_X, BRD_LOCK_Y, BRD_LOCK_Z, BRD_LOCK_RADIUS)
            // 2. Pull it.
            .UseGO(BRD_GO_SHADOWFORGE_LOCK, /*searchRadius*/ 15.0f)
            // 3. Confirm the machine actually moved. Gating on the DOORS rather
            //    than on the lever is the point: the lever flipping only proves
            //    the click landed, while GO_STATE_READY on 157923 proves the
            //    SmartAI chain ran and the path is open. If the world DB is
            //    missing that chain this step burns its timeout and stalls
            //    visibly instead of latching the objective on a no-op.
            .WaitForGOState(BRD_GO_GIANT_DOORS, BRD_GO_STATE_READY,
                            BRD_GIANT_DOOR_TIMEOUT_MS, BRD_GIANT_DOOR_SCAN)
            .Build());
}

// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterBlackrockDepthsRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- Blackrock Depths (map 230) — Ring of Law arena gauntlet --
    // The Ring of Law is its OWN DungeonEncounter (bit 3, between
    // Houndmaster Grebmar at 2 and Pyromancer Loregrain at 4), credited
    // to Grimstone (10096) — but Grimstone has NO static spawn (he is
    // summoned only when the centre area trigger fires), so BossSpawnIndex
    // can't emit him, exactly like Razorfen Downs' Tuten'kash. Add the
    // Ring of Law as an OBJECTIVE anchor at the arena centre (area trigger
    // 1526, x/y from AreaTrigger.dbc; floor z) so boss-nav drives the tank
    // into the sealed arena and the event (eventId 1) runs the gauntlet:
    // walk in -> the trigger fires -> hold dead-centre until DONE while
    // the random waves + boss are fought reactively. encounterIndex 3
    // slots it after Grebmar and before Loregrain; the objective-before-
    // boss tie-break + the picker's strictly-greater advance order it
    // correctly. No gateEntry (the boss is random; the event owns
    // completion via TYPE_RING_OF_LAW == DONE, see BlackrockDepthsEvents).
    {
        BossRosterPatch p;
        p.mapId = 230;
        p.add = {
            MakeObjective(OBJ(1), /*encounterIndex*/ 3, 230, "Ring of Law",
                          596.432f, -188.498f, -53.9f, /*arriveRadius*/ 12.0f,
                          /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 1),
            // --- Blackrock Depths — the Shadowforge Lock ------------------
            // The lever that closes the Giant Doors, carrying map-230 event 2.
            // It has no DungeonEncounter of its own; encounterIndex 9 is an
            // ordering hint only (NextDungeonBossValue consults the completion
            // mask for Boss anchors ONLY, precisely so an objective sharing a
            // boss's bit can't vanish when that bit flips), and 9 is General
            // Angerforge's bit. Sharing it is what puts the lever between
            // Bael'Gar (bit 8) and Angerforge: on Bael'Gar's death the picker
            // advances to the lowest key strictly greater than 8, and the
            // roster sort's objective-before-boss tie-break hands over the
            // lever first. arriveRadius 8 is the garrison room, not the wall —
            // the event's own MoveTo (radius 4) plus the UseGO 5yd approach do
            // the fine positioning.
            MakeObjective(OBJ(2), /*encounterIndex*/ 9, 230, "Shadowforge Lock",
                          615.61f, -49.78f, -59.82f, /*arriveRadius*/ 8.0f,
                          /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 2),
        };
        t.push_back(std::move(p));
    }
}
