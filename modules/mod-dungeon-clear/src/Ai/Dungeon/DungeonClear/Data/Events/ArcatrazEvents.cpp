/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "Player.h"
#include "Playerbots.h"

// --- The Arcatraz (map 552) — Warden Mellichar's stasis-pod finale ---------
//
// The last encounter is a fixed multi-wave set-piece with no static boss spawn
// at the end of it. Verified from arcatraz.cpp / instance_arcatraz.cpp:
//
//   * Warden Mellichar (20904) IS a normal spawn, on his dais at
//     (445.803, -169.007, 43.6442). He is SetImmuneToAll, zeroes all incoming
//     damage, and overrides MoveInLineOfSight / AttackStart / JustEngagedWith to
//     empty — he never fights and never loses HP.
//   * The encounter starts when a PLAYER DAMAGES HIM (DamageTaken). There is no
//     gossip, no area trigger, no clickable object. See the hook, below.
//   * Five stasis pods then open on a fixed timer, each releasing a wave:
//       wave 1  (478.3,-148.5)  RAND(20905 Blazing Trickster, 20906 Phase-Hunter)
//       wave 2  (413.3,-148.4)  20977 Millhouse Manastorm  — COSMETIC, not gated
//       wave 3  (420.2,-174.4)  RAND(20908 Akkiris Lightning-Waker,
//                                    20909 Sulfuron Magma-Thrower)
//       wave 4  (471.8,-174.6)  RAND(20910 Twilight Drakonaar,
//                                    20911 Blackwing Drakonaar)
//       wave 5  (445.8,-191.6)  20912 Harbinger Skyriss — the real boss
//     Waves 1/3/4 gate on their own death (SmartAI JustDied -> SetInstanceData);
//     wave 2 is pure theatre and the sequence does not wait for Millhouse.
//   * Killing Skyriss makes Mellichar KillSelf() and latches boss bit 3.
//
// WHY A GARRISON RATHER THAN AN INSTANCE-DATA GATE: instance_arcatraz does NOT
// implement GetData at all — it overrides only OnGameObjectCreate / SetData /
// GetGuidData / SetBossState, so GetData falls through to ZoneScript's base
// `return 0` for every id, forever. MoveToHoldUntilInstanceData (the BRD Ring of
// Law / ZulFarrak pattern) would read 0 and either never clear or clear
// instantly. The pod SetData values are forwarded straight to the pod GO and to
// Mellichar's AI and are never stored. So the wave phase is gated on Skyriss
// EXISTING instead — WaitForSpawn/garrison uses FindNearestCreature, which sees
// a TempSummon (the spawn-store scans do not).
//
// WHERE THE PARTY STANDS: (445.9, -161.5, 42.56), the centroid of the four wave
// spawn corners. Each corner is ~30-36yd away, so no wave lands on top of the
// party, and it is well inside the encounter's 100yd leash
// (EVENT_WARDEN_CHECK_PLAYERS evades the whole fight within 1s if no player is
// within 100yd of Mellichar). It is also ~11yd off Mellichar's dais, so the
// party is not stacked underneath him during the intro.
//
// WIPE HANDLING: BossAI::_Reset despawns every wave mob and Skyriss, re-closes
// all five pods, re-raises the shield and puts the boss state back to
// NOT_STARTED — but does NOT despawn Mellichar. A wipe DURING THE WAVES recovers
// cleanly: the hook keeps the poke live for the whole phase, so a party that
// corpse-runs back sees NOT_STARTED and restarts the sequence.
//
// KNOWN GAP — a wipe during the SKYRISS FIGHT does not. Step 3's KillCreature
// gate completes when no live Skyriss remains in range, and a reset despawns
// him, so the gate reads Done and the event latches complete without the kill.
// Mellichar's boss row is still unfinished, so the run then drives the party
// back to his dais and the boss-engage machinery re-pokes him there (his
// DamageTaken is the trigger, and engaging him IS damage) — the waves rerun,
// fought from the dais rather than the centroid. Sloppy but not stuck. Closing
// it properly needs a completion gate on encounter bit 3 rather than on the
// creature's absence, which the KillCreature step does not offer today.
//
// The two Containment Core security fields (184318 / 184319, at x~199.9) need no
// event rows: they are plain DOOR_TYPE_PASSAGE gates that open permanently when
// Soccothrates and Dalliah die, which ordinary boss ordering already handles.
//
// SEPARATELY: the Arcatraz Sentinels (20869) are handled by DcHazardRegistry (not
// an event). They wake AGGRESSIVE at 40% HP and are fought normally; the danger is
// what happens AFTER. (1) A dormant one pulses 563-937/s in 15yd; pull/camp/route
// avoidance keeps bots from loitering in it. (2) The run-wiper: on death the
// Sentinel summons the "Destroyed Sentinel" (21761) at the corpse — NOT_SELECTABLE,
// so unkillable, carrying the same 15yd/1s pulse until it despawns. The party is
// standing right on it after the kill; DungeonClearHazardVacate drives every bot
// out of the pulse (both engines, since combat usually drops). The live one's <=10%
// "Explode" (36719) needs no handling — it self-stuns for the wind-up, so the party
// bursts it down before it detonates. See DcHazardRegistry.h.

namespace
{
    // The wave arena floor centroid — where the whole finale is fought.
    constexpr float ARC_ARENA_X = 445.9f;
    constexpr float ARC_ARENA_Y = -161.5f;
    constexpr float ARC_ARENA_Z = 42.56f;

    constexpr uint32 ARC_SKYRISS = 20912;

    // ObjectiveHookRegistry id 6 — pokes Mellichar and holds through the waves.
    constexpr uint32 ARC_MELLICHAR_WAVES_HOOK = 6;

    // Mellichar's DBC encounter slot; the objective shares it so the roster's
    // Objective-before-Boss tie-break sorts it immediately ahead of him.
    constexpr uint32 ARC_MELLICHAR_ENCOUNTER = 3;

    // The scripted intro alone is ~2 minutes of hard-coded delays before Skyriss
    // is released, and three of the four waves gate on a kill in between. 300s is
    // ~2.5x the realistic worst case without being so loose that a genuinely
    // wedged run sits here instead of surfacing to the human.
    constexpr uint32 ARC_WAVES_TIMEOUT = 300000;

    // --- The Eredar room (event 2) -----------------------------------------
    //
    // TWO entries, because all three spawn points are MULTISPAWN. Each of the
    // guids 138942 / 138943 / 138944 carries a `creature_multispawn` row for
    // 20880, so `CreatureData::id2` is set and Creature::LoadFromDB rolls
    // GetRandomId(id, id2, id3) at spawn — an even coin flip per point between
    // Eredar Soul-Eater (20879) and Eredar Deathbringer (20880). The roll is
    // REDONE on every respawn (Creature::Update's `if (data->id2)` branch calls
    // UpdateEntry with a fresh roll), so the room's composition is not stable
    // across a wipe, let alone across runs: anywhere from 3 Soul-Eaters to 3
    // Deathbringers.
    //
    // This is why `SELECT * FROM creature WHERE id=20880` returns nothing on any
    // map while the harness still records `wipeOpponentEntry 20880` — the base
    // spawn rows all say 20879. Do not conclude from an empty `creature` query
    // that an entry never appears; check `creature_multispawn` too.
    //
    // Both are the NORMAL entries and cover heroic as well: Creature::InitEntry
    // does `SetEntry(Entry); // normal entry always` and swaps only
    // m_creatureInfo, so GetEntry() never returns the heroic templates (21595 /
    // 21594) and adding them here would be rows that can never match.
    constexpr uint32 ARC_SOUL_EATER = 20879;
    constexpr uint32 ARC_DEATHBRINGER = 20880;

    // Centroid of the three spawns, dropped onto the room floor. Navmesh-probed:
    // a single surface at z 22.32, and each spawn sits on mesh too (the NE one on
    // a 24.88 step). Every spawn is within 13.8yd of this point, so a tank parked
    // here is inside the executor's DC_EVENT_CLEAR_JUDGE_RADIUS and its strict
    // reachability probe covers the whole volume.
    constexpr float ARC_SOUL_EATER_X = 297.7f;
    constexpr float ARC_SOUL_EATER_Y = 140.6f;
    constexpr float ARC_SOUL_EATER_Z = 22.3f;

    // 45 = the 13.8yd worst-case spawn offset plus room for a caster that backs
    // off the tank mid-fight. Nothing else on the map carries either entry, so a
    // generous radius costs nothing and the entry filter keeps the Sentinel at
    // (255.5,158.9) and the three corpse props out of the sweep regardless.
    constexpr float ARC_SOUL_EATER_RADIUS = 45.0f;
    constexpr float ARC_SOUL_EATER_ZBAND = 12.0f;

    // Proximity gate. FindNearestCreature is a grid scan, so this reads true only
    // once the party is in the wing — the nearest boss anchors are 161yd
    // (Dalliah), 164yd (Soccothrates) and 265yd (Zereketh) from the centroid, so
    // even at 80 it cannot fire from any of them.
    //
    // Deliberately WIDE rather than tight-to-the-room. The condition is what keeps
    // the party on the job: the moment it reads false the event stops being
    // driven, and a half-cleared room with live Deathbringers in it is the exact
    // state this event exists to prevent. A 50yd gate un-arms if a chase, a
    // knockback or a corpse-run puts the party ~36yd outside the room, which is
    // easy — 80 keeps it armed through all of that. Firing "early" costs nothing
    // here: the room is directly on the route, so being pulled to it is where the
    // party was going anyway.
    constexpr float ARC_SOUL_EATER_SCAN = 80.0f;

    // Three level-70 elites, engaged one at a time as the sweep walks the tank
    // between them, plus a DC_FLAGGED_NO_ENGAGE_GRACE_MS pause after any kill that
    // leaves nothing aggroed.
    //
    // GENEROUS ON PURPOSE — a timeout here is not a safety valve, it is the
    // failure mode. `required` (below) means a Failed step returns Stalled, and
    // DcRunEventAction answers Stalled by parking the tank with a "sort it out"
    // message. In a room where up to three Unholy Auras are ticking 750 every 2s
    // on the whole party, parking IS the wipe. So the number has to be far past
    // any legitimate fight rather than tuned close to it: ~30-60s per elite for a
    // 5-bot party is 90-180s, and 180 was sitting exactly on that edge.
    //
    // The no-progress backstop (DC_EVENT_NO_PROGRESS_FACTOR, 3x) rides on this at
    // 24 minutes, which the run watchdog beats comfortably — so a genuinely wedged
    // room still surfaces, it just does not surface by standing the party in the
    // damage until they die.
    constexpr uint32 ARC_SOUL_EATER_TIMEOUT = 480000;

    // DUE while any of the trio is alive within grid range. Doubles as the
    // proximity gate (see ARC_SOUL_EATER_SCAN) and as the "still work to do"
    // test, so it goes false on its own the moment the room is clear — the
    // Hellfire Ramparts pattern.
    //
    // BOTH entries must be scanned. A room that rolled three Deathbringers has
    // no 20879 in it at all, and a Soul-Eater-only condition would never fire —
    // leaving exactly the freeze this event exists to prevent, in the one case
    // that matters most (three Deathbringers is also the worst aura stack).
    bool ArcEredarRoom(Player* bot, AiObjectContext* /*context*/)
    {
        if (!bot)
            return false;
        if (bot->FindNearestCreature(ARC_SOUL_EATER, ARC_SOUL_EATER_SCAN, /*alive*/ true))
            return true;
        return bot->FindNearestCreature(ARC_DEATHBRINGER, ARC_SOUL_EATER_SCAN,
                                        /*alive*/ true) != nullptr;
    }
}

void RegisterArcatrazEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(552, 1, "Warden Mellichar (stasis pod waves)")
                      .Anchored(/*orderIndex (doc)*/ 3)
                      // Every wave is a fight, and the executor only Drives from
                      // the NON-COMBAT strategy — so each wave produces a Drive
                      // gap longer than EventStaleGapMs. Without Persistent the
                      // step index rewinds to 0 on every gap and the event
                      // restarts forever. (Also required by the persistence lint:
                      // this event holds two rewind-hazard steps.)
                      .Persistent()
                      // 1. Settle on the arena floor, clear of all four corners.
                      .MoveTo(ARC_ARENA_X, ARC_ARENA_Y, ARC_ARENA_Z, /*radius*/ 6.0f)
                      // 2. Own the whole wave phase: poke Mellichar to open the
                      //    pods, garrison the centroid between waves, and finish
                      //    when Skyriss is released. Combat AI fights the waves
                      //    as they arrive.
                      //
                      //    ONE Custom step rather than Custom(poke) +
                      //    MoveToHoldUntilSpawn(Skyriss), because this event must
                      //    be Persistent and the executor never rewinds a
                      //    persistent event's stepIndex — a poke step that has
                      //    already returned Done could not re-fire after a wipe,
                      //    and the hold would then wait out its whole timeout on
                      //    a Skyriss nobody was left to release. See the hook.
                      .Custom(ARC_MELLICHAR_WAVES_HOOK)
                          .Timeout(ARC_WAVES_TIMEOUT)
                      // 3. Kill him. Mellichar KillSelf()s the instant Skyriss
                      //    dies, so his boss row completes with this step.
                      .KillCreatureEngage(ARC_SKYRISS, /*count*/ 1, /*searchRadius*/ 100.0f)
                      .Build());

    // --- Event 2: the Eredar room -----------------------------------------
    //
    // Three spawn points on the route between Zereketh and the Dalliah /
    // Soccothrates pair, east of the two Containment Core security fields:
    //
    //     138942  (305.7, 148.1, 24.9)
    //     138943  (285.5, 146.2, 22.3)
    //     138944  (301.8, 127.4, 22.3)
    //
    // All three are stationary (wander_distance 0, MovementType 0) and all three
    // are MULTISPAWN — each independently rolls Eredar Soul-Eater (20879) or
    // Eredar Deathbringer (20880) at spawn and again at every respawn. See the
    // ARC_SOUL_EATER / ARC_DEATHBRINGER note above; the composition is not stable
    // across a wipe, so this event must handle 0-3 of each.
    //
    // WHICH ONE ROLLS DECIDES HOW DANGEROUS THE ROOM IS. Both auras are permanent
    // creature_template_addon auras, both 45yd, both running out of combat and
    // forever, read straight out of Spell.dbc:
    //
    //   Soul-Eater  "Entropic Aura" 36784 — effect 129 APPLY_AREA_AURA_ENEMY,
    //     aura 193 HASTE_ALL -25% and aura 33 MOD_DECREASE_SPEED -25%. Annoying,
    //     but NO DAMAGE.
    //   Deathbringer "Unholy Aura" 27987 (heroic 38844) — APPLY_AURA ->
    //     PERIODIC_TRIGGER_SPELL every 2000ms, duration -1, firing 27988 / 38845:
    //     SCHOOL_DAMAGE 450 normal / 750 heroic at radius 45yd against
    //     TARGET_UNIT_SRC_AREA_ENEMY. That is 225 / 375 dps to EVERY party member
    //     per Deathbringer, and up to three can roll.
    //
    // A heroic room that rolls three Deathbringers puts ~1125 dps on every member
    // for as long as the party is anywhere in it, with no position that escapes
    // it — 45yd covers the whole room from any spawn point. That is the shape of
    // the fight this event has to win, and it is why the sweep is a race rather
    // than a normal trash pull.
    //
    // WHAT BROKE THE RUN BEFORE THE FIGHT EVEN STARTED: a 45yd hostile area aura
    // (either one) sets the core
    // combat FLAG on every party member, while a level-70 elite's aggro radius is
    // only ~20yd. That leaves a 25yd annulus where everyone is "in combat" with
    // nothing aggroed — no victim, no attacker, no threat — and playerbots never
    // enters the combat engine on the flag alone. Every DC rung that stood down
    // on the raw flag went inert there. That hole is closed for good in
    // DcCombatFlag (MayDrive / IsPhantomFlag / AnyPartyEngagement) and is what
    // lets THIS event drive at all: DungeonClearEventDueTrigger gates on
    // MayDrive, so without it the party would be flagged on approach and the
    // sweep below would never get a tick.
    //
    // The event is the positive half. Rather than leaving the room to the
    // blocking-trash and room-trash heuristics — which is where the three corpse
    // props at (298.8,151.7) / (283.7,130.2) / (257.3,155.6) kept being read as
    // live packs — it makes clearing the trio an explicit, ordered set-piece:
    // walk to the middle of the room and kill all three before moving on.
    //
    // CONDITIONAL, not Anchored: the room is trash, not an encounter, so it earns
    // no roster objective and no encounter slot. The condition is its own
    // proximity gate and its own completion test (see ArcEredarRoom), and it
    // folds into Dalliah's panel row. Persistent because a ClearRadius is a fight
    // and a mid-fight Drive gap would otherwise rewind the step list — a wipe
    // resumes the sweep rather than restarting it.
    //
    // THE PARTY MUST FINISH ALL THREE — it cannot stop half way, and it cannot
    // wait the room out. Four things enforce that, and they are worth stating
    // together because each is load-bearing:
    //
    //   * IT CANNOT REST. Every rest path runs through RestTargetIfActive, which
    //     returns 0 on a raw bot->IsInCombat(). A 45yd hostile area aura holds the
    //     whole party flagged for as long as any of the three lives, so the drink
    //     / eat rungs (DcRel::NeedsRest) and the Smart Rest latch are all inert in
    //     this room by construction. The between-pulls HP/mana floors would
    //     otherwise deadlock on the same flag, and DcPartyState waives them under
    //     IsPhantomFlag for exactly this case. Resting here is not a decision the
    //     party gets to make.
    //   * IT CANNOT WANDER OFF. The event outranks everything it competes with in
    //     the non-combat ladder (DcRel::EventDue 31 vs NeedsRest 26.5, RoomTrash
    //     26, follow-tank 25, advance 15), so while the condition holds the leader
    //     drives the sweep and nothing else gets the tick.
    //   * IT CANNOT DECLARE VICTORY EARLY. The ClearRadius gate is Running while
    //     any reachable in-filter hostile remains, and the executor refuses to
    //     certify "clear" from further than DC_EVENT_CLEAR_JUDGE_RADIUS — it walks
    //     the tank back to the centre instead. Both multispawn entries are in the
    //     filter, so a Deathbringer cannot be mistaken for an empty room. And
    //     because a position sweep can only fight what IsPossibleTarget /
    //     IsEngageReachable let it see, two by-entry KillCreatureEngage steps
    //     follow it as a backstop (steps 3-4) — the Shattered Halls pattern.
    //   * THE WHOLE GROUP FIGHTS, not just the tank. Followers do not drive this
    //     event (it is leader-only), and while the aura flags them with nothing
    //     aggroed there is deliberately nothing for them to do. The moment the
    //     tank's EngageDirect gives it a victim, PickPartyFightTarget resolves
    //     that mob off the leader and the flip-early assist rung
    //     (DcFollowerActions) puts every follower into the COMBAT engine
    //     immediately, without waiting for LOS or range. So the chain that turns
    //     "flagged by an aura" into "the party is fighting" is: event drives ->
    //     tank attacks -> followers resolve the tank's victim -> whole party in
    //     the combat engine.
    //   * IT CANNOT QUIETLY GIVE UP. `required` is left at its default true, so a
    //     Failed step stalls for the human rather than skipping on with the room
    //     half cleared. That is the right call for correctness and a dangerous one
    //     for survival — see the ARC_SOUL_EATER_TIMEOUT note for why the timeout
    //     is set far past any real fight instead of close to it.
    //
    // A wipe is not an escape either: the event is Persistent, so the party
    // corpse-runs back and resumes at the sweep. The spawns re-roll on respawn, so
    // the room it comes back to may be a different mix.
    out.push_back(EventBuilder(552, 2, "Clear the Eredar room")
                      .Conditional(&ArcEredarRoom)
                      .Persistent()
                      .PanelBeforeBoss(/*Dalliah the Doomsayer*/ 20885)
                      // 1. Settle in the middle of the room. All three spawns are
                      //    within 13.8yd of here, so the sweep below is judged
                      //    from a vantage point that can actually see the room
                      //    (DC_EVENT_CLEAR_JUDGE_RADIUS is 12).
                      .MoveTo(ARC_SOUL_EATER_X, ARC_SOUL_EATER_Y, ARC_SOUL_EATER_Z,
                              /*radius*/ 8.0f)
                      // 2. Kill all three, whichever way they rolled. Both
                      //    multispawn entries are listed: a Deathbringer-only room
                      //    would otherwise read "clear" with three live casters in
                      //    it and latch the event done having killed nothing.
                      //    Entry-filtered so the sweep is exactly the trio — the
                      //    Arcatraz Sentinel on the room's west edge keeps its own
                      //    hazard/pull handling, and the corpse props are not
                      //    fought at all.
                      .ClearRadius(ARC_SOUL_EATER_X, ARC_SOUL_EATER_Y, ARC_SOUL_EATER_Z,
                                   ARC_SOUL_EATER_RADIUS, ARC_SOUL_EATER_ZBAND)
                          .OnlyEntries({ ARC_SOUL_EATER, ARC_DEATHBRINGER })
                          .Timeout(ARC_SOUL_EATER_TIMEOUT)
                      // 3-4. BY-ENTRY BACKSTOP, one per multispawn entry. The
                      //      Shattered Halls assassin lesson: a ClearRadius gate
                      //      resolves targets through DcTargeting::
                      //      NearestHostileNearPoint, which filters every
                      //      candidate through AttackersValue::IsPossibleTarget
                      //      (hard-gated on bot->CanSeeOrDetect) and a STRICT
                      //      IsEngageReachable. Anything those reject is invisible
                      //      to the sweep, so the gate answers "no hostile left",
                      //      certifies the room clear and latches the event done
                      //      with a live 750-per-2s caster still standing — and
                      //      the party then walks on, flagged, with nothing to
                      //      hit. That is the wedge this room already produced
                      //      once.
                      //
                      //      KillCreatureEngage resolves by ENTRY through
                      //      FindNearestCreature, a plain grid scan with no
                      //      visibility or targeting filter, and its reachability
                      //      probe is the looser requireDirect=false. So whatever
                      //      the sweep could not see, these still find and walk
                      //      the tank into. Both are instant no-ops when the room
                      //      really is empty (no live creature of the entry =>
                      //      Done on the first evaluation), so they cost nothing
                      //      in the normal case.
                      //
                      //      Tight search radius: only three spawns of either
                      //      entry exist on the whole map and all three are in
                      //      this room, so 60 cannot pull the tank anywhere else.
                      .KillCreatureEngage(ARC_SOUL_EATER, /*count (doc; "any alive")*/ 3,
                                          /*searchRadius*/ ARC_SOUL_EATER_SCAN)
                          .Timeout(ARC_SOUL_EATER_TIMEOUT)
                      .KillCreatureEngage(ARC_DEATHBRINGER, /*count (doc; "any alive")*/ 3,
                                          /*searchRadius*/ ARC_SOUL_EATER_SCAN)
                          .Timeout(ARC_SOUL_EATER_TIMEOUT)
                      .Build());
}

void RegisterArcatrazRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = 552;
    p.add = {
        // Mellichar's own boss row is KEPT, not removed: he is a real spawn with
        // correct coords and his kill-bit (3) latches on the win. The objective
        // shares his encounter index and sorts ahead of him, so by the time his
        // row is considered the event has already killed Skyriss and he is gone.
        //
        // Skyriss is deliberately NOT added as a boss row. He is a TempSummon, so
        // BossSpawnIndex cannot see him and the spawn-store scans behind
        // FindLiveCreatureOnMap would read live=0 and deadlock the engage gate.
        // The event reaches him with FindNearestCreature instead, which does see
        // TempSummons — the Hellfire Ramparts / Vazruden pattern.
        MakeObjective(OBJ(1), ARC_MELLICHAR_ENCOUNTER, 552,
                      "Warden Mellichar (stasis pod waves)",
                      ARC_ARENA_X, ARC_ARENA_Y, ARC_ARENA_Z,
                      /*arriveRadius*/ 12.0f, /*gateEntry*/ 0,
                      /*hook*/ 0, /*eventId*/ 1),
    };
    t.push_back(std::move(p));
}
