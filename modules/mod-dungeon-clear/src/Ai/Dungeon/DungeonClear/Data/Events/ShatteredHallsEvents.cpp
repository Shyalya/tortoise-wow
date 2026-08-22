/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "Creature.h"
#include "Player.h"

// --- The Shattered Halls (map 540) — drop past the door, before the first boss -
//
// On the way to the first boss, Grand Warlock Nethekurse (16807, bit 0), the
// route crosses a BREAK in the navmesh at a door: the party stands at the door
// lip (122.78, 249.59, -15) and must DROP ~33yd down and ~40yd forward to
// (132.78, 209.68, -47.84) to continue. The drop is DIAGONAL and off-mesh — the
// two surfaces sit on disconnected mesh islands — so neither a pure-vertical
// DropInHole (it would fall in the wrong column) nor a ballistic Jump (a big
// diagonal drop clips/overshoots) is reliable. Per the user this door must NOT
// block or stop progress, so we TELEPORT the whole party across the instant the
// tank reaches the door lip, exactly like The Slave Pens' drop past Mennu.
//
// A roster OBJECTIVE anchor (RegisterShatteredHallsRoster) sits at the door lip
// and borrows encounterIndex 0 (Nethekurse's bit): the Objective-before-Boss
// tie-break in Apply() sorts it AHEAD of Nethekurse at the shared key, so on a
// fresh clear the tank visits this objective FIRST, teleports past the door, and
// only then heads to Nethekurse. Boss-nav drives the walk to the lip.
//
// Anchored (not Conditional): the drop is purely positional and on the critical
// path, so it rides the objective like Slave Pens / Wailing Caverns' drops. Not
// Persistent: TeleportParty is a single synchronous step that completes the same
// tick it fires (and is idempotent on a restart — Done immediately if the leader
// is already on the landing), so there is no multi-tick fall for a tick-gap to
// rewind.

// --- The Shattered Halls (map 540) — fight down the flame gauntlet -----------
//
// Past Nethekurse the route climbs onto a raised corridor (z~2) that runs EAST
// along y~315, from the entry (~x300) to the archers' ledge (~x515). This is the
// flame gauntlet. Every number below is read out of boss_porung.cpp,
// spell_tsh_shoot_flame_arrow, DungeonEncounter.dbc and the world DB — not
// guessed — because the whole encounter is a set of interlocking timers and the
// wrong mental model produces exactly the wrong tactic.
//
// WHAT ARMS IT. A Shattered Hand Scout (17693) stands at (341.3, 314.9, 2). Its
// MoveInLineOfSight fires the instant ANY player is within 50yd 2D of it at
// corridor level (z > -3) — which our own tank does just by walking to the
// corridor mouth at x~291. It yells, goes NOT_SELECTABLE + REACT_PASSIVE (it can
// never be killed: DamageTaken clamps it to 1 HP), and runs waypoint path
// 176930 east to (488.6, 315.7, 1.9), where it turns INVISIBLE and stays for the
// rest of the encounter. Everything the gauntlet does afterwards is scheduled off
// that invisible scout.
//
// WHAT IT THEN RUNS, all on the scout's TaskScheduler:
//   * the three zealots parked at x332-336 SetInCombatWithZone immediately;
//   * the far-end boss (Shattered Hand Blood Guard 17461 on normal, Blood Guard
//     Porung 20923 on heroic — same spot, 512.7/315.7/2, split by spawnMask)
//     casts 30976 "Summon Zealots" at once and every 45s thereafter. 30976 is a
//     SPELL_EFFECT_SUMMON of creature 17462 at spell_target_position
//     (520.06, 255.49, 2.03) with DurationIndex 21 = -1, i.e. the summons NEVER
//     expire. Each one runs SmartAI waypoint set 1746200 — (518,255) ->
//     (522,254) -> (512,287) -> (497,316) -> (485,316) — and on reaching the end
//     SetInCombatWithZone()s. So a fresh wave arrives at the corridor mouth every
//     45 seconds, from BEHIND the far pack, and flags the whole party into combat
//     from wherever it happens to be standing;
//   * ~6s in, both Shattered Hand Archers (17427, at 514.5/319.7 and 514.8/312.0)
//     start casting 30952 "Shoot Flame Arrow" on a 2-9.75s repeat.
//
// WHERE THE FIRE COMES FROM, exactly. 30952 is a 2s cast whose implicit target
// TARGET_UNIT_SRC_AREA_ENTRY is narrowed by `conditions` to creature 17687
// "Flame Arrow" — 20 invisible trigger spawns strung along the corridor from
// x290.3 to x468.7, each with MovementType 1 and wander_distance 12-17, so their
// live positions roam roughly x277..482. spell_tsh_shoot_flame_arrow::
// FilterTargets then drops every anchor with NO PLAYER INSIDE 15yd, every anchor
// that already has a Blaze within 6yd, and the anchor used last, and
// RandomResizes the survivors to ONE. That one anchor casts 30953 "Explosion" on
// itself: 657-844 fire in a 10yd radius immediately, plus GameObject 181915
// "Blaze" for 60 seconds. The Blaze is a GAMEOBJECT_TYPE_TRAP — 2yd trigger
// circle, casts 30979 "Flames" for 875-1126 in 3yd, re-arming every 2s.
//
// Three consequences follow from that filter and they drive the whole design:
//   1. THE FIRE FOLLOWS THE PARTY. An anchor only qualifies with a player inside
//      15yd, so standing still does not make you safe — it makes you the target.
//   2. THERE IS NO SAFE SPOT IN THE CORRIDOR. Anchor spawns plus their wander
//      plus the 15yd trigger cover an unbroken x~261..497 band. Waiting out the
//      gauntlet mid-corridor is not an available tactic at any speed.
//   3. THE LEDGE IS FIRE-FREE. Past x~482 no anchor can reach, which is why the
//      scout's own waypoint parks it at x488.6 — that spot is Blizzard's marker
//      for "out of the fire, short of the pack" and it is where we stage.
//
// HOW IT ENDS. Two separate off-switches, and the file used to have both wrong:
//   * KILLING THE ARCHERS STOPS THE FIRE. The repeat re-arms only while
//     FireArrows() finds an archer, so with both dead the arrow loop terminates
//     permanently. (Merely ENGAGING them does not: Shoot 16100/22907 has a 0ms
//     cast, so an archer in melee is almost never in UNIT_STATE_CASTING when
//     DoCastAOE fires. Engage is the means; the kill is the mechanism.)
//   * KILLING THE FAR-END BOSS STOPS EVERYTHING. On normal, 17461 carries
//     smart_scripts (entryorguid 17461, event 6 JUST_DIED) -> action 45 SET_DATA
//     2 on target 19 (closest 17693) — and the scout's SetData handler is
//     `_scheduler.CancelAll()`. On heroic, boss_porung::JustDied does the same
//     call by hand. Either way the summons and the arrows stop together. An
//     earlier revision of this comment claimed the normal-mode Blood Guard "has
//     no script" and that the gauntlet only wound down at 250yd; both were wrong,
//     and the 250yd wind-down never fires because the scout sits mid-instance.
//
// THE TACTIC. Fight down the hall in BOUNDS: walk to a bound, kill whatever is
// standing on it, walk to the next. Not one 190-yard sprint (what this event used
// to do — tr-20260816-144504-8 ran entry->ledge in 2m30s and cost three deaths
// with the party strung out over 100yd), and not a held camp either (the Mechanar
// bridge answer, which is wrong here: nothing forces the party forward on that
// bridge, whereas here the fire is unavoidable and only exists while the archers
// live, so holding is strictly worse the longer it lasts).
//
// A bound is a ClearRadius, and that is doing real work even where the volume is
// usually empty: an anchored event only drives on the NON-COMBAT engine, so the
// party physically cannot advance to the next bound while a wave is on it, and
// the ClearRadius gate additionally refuses to advance while anything is left
// standing inside the bound. That pair IS "stop and fight the waves as they come,
// then push further" — it needs no new primitive, only bounds short enough
// (~40yd) that the party never strings out across one.
//
// The last three steps are the off-switches, in order: stage on the fire-free
// ledge, KILL THE ARCHERS (fire off), then clear the pack — which on normal
// includes the Blood Guard, so the waves stop too. On HEROIC Porung is a real
// DungeonEncounter, and DcTargeting::NearestHostileNearPoint deliberately skips
// boss entries, so the ClearRadius leaves him alone and the roster's own Porung
// anchor takes him immediately afterwards. See the heroic re-key in the roster
// patch below, which exists because that ordering was previously wrong.
//
// Persistent so (a) the pull pipeline stays down through the whole corridor and
// (b) the many wave/combat gaps don't rewind the step list. Step 0 (MoveTo entry)
// bumps stepIndex so the persistence sticky-trigger engages and the tank may work
// the whole corridor from the anchor.
//
// "Never stand in the fire" is NOT part of this event — it is a DcHazardRegistry
// row (map 540, GO 181915) driven by DungeonClearHazardVacate{Trigger,Action}.
// That row is what makes the sentence true; before it existed the Blaze was
// invisible to every hazard predicate, because a GAMEOBJECT_TYPE_TRAP is neither
// a Unit (DungeonClearHazardsValue) nor a DynamicObject
// (DungeonClearGroundHazardsValue) and both resolvers returned nullptr on it.
//
// COORDS: the bound centres are on the corridor axis interpolated between real
// spawn rows (the zealot cluster at x332-336, the scout at x341.3, the flame
// anchors from x290 to x469, the far pack at x499-515) and the staging point is
// the scout's own waypoint terminus (488.6, 315.7, 1.9) read out of
// waypoint_data path 176930. They have NOT been probed against the map-540
// mmaps; the corridor is a straight open hall at a single z, so the risk is low,
// but a DcNavHarness probe (as Mechanar's did) is the way to certify them.

// --- The Shattered Halls (map 540) — sweep the stealth-assassin L-hallway ------
//
// Between Warbringer O'mrogg (16809, at 375,57,-7, bit 1) and Warchief Kargath
// Bladefist (16808, at 231,-83,5, bit 2) the route runs an L-SHAPED hallway lined
// with STEALTHED Shattered Hand Assassins (17695; creature_template_addon aura
// 30991 + SmartAI "On Create - Cast Stealth"). User-probed endpoints:
//
//     start (374.83, 0.31, 1.73)  --  N-S leg down x~375-382 (assassins at
//                                     382,-32 / 383,-53 / 381,-75) ...
//     ... corner ~ (382,-85) ...  --  W leg along y~-85..-92 (assassins at
//                                     368,-88 / 325,-92 / 291,-91) ...
//     end (293.67, -83.02, 1.91)  --  mouth of Kargath's arena.
//   (A seventh static spawn sits off to the NE at 482,55 — a lone sentinel on the
//    O'MROGG APPROACH, before that fight; it is handled by its own event/objective
//    below, ordered ahead of O'mrogg, NOT by this post-O'mrogg hallway sweep.)
//
// Left to the default pull this DEADLOCKS, and — the key correction over the
// first cut — a ClearRadius push does NOT fix it. An assassin flags the party
// into combat but STAYS STEALTHED and doesn't melee; the bots can't engage it and
// the run wedges "in combat, nothing to hit". The reason a ClearRadius sweep also
// failed: it engages via DcTargeting::NearestHostileNearPoint ->
// AttackersValue::IsPossibleTarget, which HARD-GATES on `bot->CanSeeOrDetect()`.
// A stealthed assassin the tank hasn't detected fails that test, so the gate
// found "no hostile", reported the zone clear, and NEVER engaged. (The grid-scan
// FarTargets set DOES contain the stealthed unit, but IsPossibleTarget filters it
// back out — my earlier "sees through stealth" claim was wrong.)
//
// Real fix: a PERSISTENT KillCreatureEngage BY ENTRY. FindNearestCreature(17695)
// is a grid scan with NO stealth/visibility filter, and Unit::Attack has no
// visibility gate — so EngageDirect walks the tank to the assassin's exact
// position and swings; the first point of damage breaks stealth. One step sweeps
// all six in-hallway assassins ("any alive 17695 within the seek radius" keeps it
// Running, engaging the nearest each tick) down the N-S leg then the W leg.
//
// Its roster OBJECTIVE borrows encounterIndex 2 (Kargath's bit): the
// Objective-before-Boss tie-break sorts it AHEAD of Kargath, so the tank sweeps
// the assassins and only then engages him. (Map-540 DBC bit order matches the
// script enum: Nethekurse 0, O'mrogg 1, Kargath 2, Porung 3 — the heroic-only
// Porung takes the top bit, NOT Kargath.) The sweep stays east of x=293, clear of
// Kargath's 42yd room (kargathRespawnPos 231,-83), so it can't wake him.
// --- The Shattered Halls (map 540) — wake Grand Warlock Nethekurse ------------
//
// Nethekurse (16807, at 172.66/289.61/-8.11) spawns SetImmuneToAll: his AI's
// Reset() re-immunes him whenever `_canAggro` is false (boss_nethekurse.cpp),
// and the ONLY things that flip it are ACTION_START_INTRO fired from
//   (a) area trigger 4347 (at_rp_nethekurse) — which exists only as a
//       CMSG_AREATRIGGER handler, a packet a bot never sends (same class as the
//       BRD Ring of Law and ZulFarrak Zum'rah bugs), or
//   (b) his own UpdateAI door-watch: chamber door 182539 read at GoState ACTIVE
//       — i.e. LOCKPICKED open, since as a DOOR_TYPE_PASSAGE it otherwise only
//       opens on the boss's own state change.
// Our route teleports past that door's navmesh break (event 1) without ever
// touching it, so in a full-bot run neither path fires: the tank walks into the
// chamber and parks against a permanently immune, passive boss. A human in the
// party fixes it by accident (their client fires AT 4347 on the way in), which
// is why this only deadlocks `dc test` / all-bot runs.
//
// Fix = the Zum'rah pattern verbatim: a Conditional + Repeatable event whose
// predicate reads the exact deadlock signature — Nethekurse alive, within 60yd,
// and still IsImmuneToPC (the very flag ACTION_START_INTRO clears) — and whose
// Custom hook (id 9, StartNethekurseIntro) calls DoAction(ACTION_START_INTRO)
// on his AI: precisely what the area-trigger script does, reusing its own
// `_introStarted` idempotence guard. Once immunity drops the predicate reads
// false forever (fight, RP, or DONE all clear it), so the repeat can never
// spin; Repeatable covers a full despawn/respawn, which reconstructs the AI
// with `_canAggro = false` and re-arms the deadlock. Firing the intro is
// enough on its own: the RP runs, and either he finishes killing his four
// peons and AttackStarts the nearest player himself, or the tank engages him
// first and JustEngagedWith cancels the RP — both end in a normal fight.
namespace
{
    // --- wake Nethekurse (first boss immune until his client-only intro) ---
    constexpr uint32 SH_NETHEKURSE = 16807;             // Grand Warlock Nethekurse
    // Comfortably beyond boss engage range so the event fires as soon as
    // boss-nav parks the tank at him, before the engage loop settles into its
    // deadlock — but near enough that it can never fire from across the dungeon
    // (mirrors ZF_ZUMRAH_SCAN).
    constexpr float SH_NETHEKURSE_SCAN = 60.0f;
    constexpr uint32 SH_HOOK_START_NETHEKURSE_INTRO = 9;  // ObjectiveHookRegistry id

    bool ShNethekurseDormant(Player* bot, AiObjectContext* context);

    // --- flame gauntlet -------------------------------------------------
    // The only entry this event names. Everything else on the ledge is taken
    // positionally by the closing ClearRadius: the 12 Shattered Hand Zealots
    // (17462) and, on normal, the Shattered Hand Blood Guard (17461) whose death
    // cancels the wave scheduler. Blood Guard Porung (20923) is deliberately NOT
    // named — on heroic he is a real DungeonEncounter, so NearestHostileNearPoint
    // skips him and the roster's own Porung anchor takes him next.
    constexpr uint32 SH_ARCHER = 17427;     // Shattered Hand Archer (the fire's source)

    // Corridor axis is y~315, z~2, x increasing east. Bound centres are ~40yd
    // apart: short enough that the party never strings out across one leg (the
    // 90yd hop this event used to make is what produced the strung-out near-wipe
    // in tr-20260816-144504-8), long enough that the walk is not a stutter.
    //
    // ENTRY. Arms the gauntlet by itself: the scout at (341.3, 314.9) triggers on
    // any player within 50yd 2D at z > -3, and this anchor is 41yd from it. Also
    // the last out-of-combat spot on the way in, so it is where the party is
    // assembled when the encounter starts.
    constexpr float SH_ENTRY_X = 300.0f, SH_ENTRY_Y = 314.0f, SH_ENTRY_Z = 2.0f;
    // B1 — the three static zealots at x332.3..335.7 that SetInCombatWithZone the
    // moment the scout yells. r30 covers x306..366, i.e. the pack plus the mouth
    // behind it, so a charger that runs past the tank is still inside the bound.
    constexpr float SH_B1_X = 336.0f, SH_B1_Y = 315.0f, SH_B1_Z = 2.0f;
    constexpr float SH_B1_RADIUS = 30.0f;
    // B2/B3/B4 — the open middle. No static spawns live here at all; these bounds
    // exist to break the advance into legs the summoned waves are met on, one at
    // a time, with the party together. Their gate is usually satisfied on arrival,
    // which is the point: they cost nothing when the corridor is empty and they
    // refuse to advance when it is not.
    constexpr float SH_B2_X = 378.0f, SH_B2_Y = 316.0f, SH_B2_Z = 2.0f;
    constexpr float SH_B2_RADIUS = 32.0f;
    constexpr float SH_B3_X = 420.0f, SH_B3_Y = 316.0f, SH_B3_Z = 2.0f;
    constexpr float SH_B3_RADIUS = 32.0f;
    constexpr float SH_B4_X = 458.0f, SH_B4_Y = 316.0f, SH_B4_Z = 2.0f;
    constexpr float SH_B4_RADIUS = 30.0f;   // x428..488 — stops short of the far pack
    // STAGING — the scout's own waypoint terminus (waypoint_data path 176930,
    // point 4). Past every flame anchor's reach (the last two spawn at x467.5 and
    // x468.7 with 13yd wander, so x481.7 worst case) and ~12yd short of the
    // nearest far-pack zealot at (498.9, 309.1). Fire-free ground within aggro
    // reach of the pack: exactly the spot to fight the last stage from.
    constexpr float SH_STAGE_X = 488.6f, SH_STAGE_Y = 315.7f, SH_STAGE_Z = 1.9f;
    // The far ledge: 12 zealots spread x498.9..515.1 / y292.4..340.4, the two
    // archers at x514.5, and the Blood Guard / Porung at (512.7, 315.7). r32 from
    // the centre covers all of it.
    constexpr float SH_LEDGE_X = 508.0f, SH_LEDGE_Y = 316.0f, SH_LEDGE_Z = 2.0f;
    constexpr float SH_LEDGE_RADIUS = 32.0f;
    // Seek radius for the archer kill, measured from the staging point: the
    // archers are 26yd away, so 60 is generous without reaching anything else
    // (the next 17427 on the map does not exist — both spawns are here).
    constexpr float SH_ARCHER_SEEK = 60.0f;
    constexpr float SH_GAUNTLET_ZBAND = 12.0f;
    // A bound can legitimately sit through several 45s wave cycles; the ledge
    // fight is the whole far pack plus the archers. Keep the long holds from
    // escalating into a stall for the human, and remember Drive's separate
    // no-progress backstop is 3x whatever is set here.
    constexpr uint32 SH_BOUND_TIMEOUT = 240000;    // 4 min per bound
    constexpr uint32 SH_ARCHER_TIMEOUT = 240000;   // 4 min to reach + drop both archers
    constexpr uint32 SH_LEDGE_TIMEOUT = 420000;    // 7 min for the far pack

    // --- stealth-assassin L-hallway (O'mrogg -> Kargath), z~1.8 ---
    // Real endpoints (user-probed). The hall is an L: a N-S leg down x~375-382
    // (y 0.3 -> ~-85) then a W leg along y~-85..-92 (x ~382 -> 293), ending at the
    // mouth of Kargath's arena.
    constexpr uint32 SH_ASSASSIN_ENTRY = 17695;      // Shattered Hand Assassin (stealthed)
    constexpr float SH_ASSN_START_X = 374.83f, SH_ASSN_START_Y = 0.31f, SH_ASSN_START_Z = 1.73f;
    constexpr float SH_ASSN_END_X = 293.67f, SH_ASSN_END_Y = -83.02f, SH_ASSN_END_Z = 1.91f;
    // Seek radius (from the moving tank) for the KillCreatureEngage sweep. Big
    // enough that the L's longest inter-assassin gap never falsely reads "clear",
    // small enough to leave the lone (482,55) sentinel off to the NE out of scope.
    constexpr float SH_ASSN_SEEK_RADIUS = 120.0f;
    // Stealthed rogues die fast, but keep the long seek-and-sweep hold from
    // escalating to a stall for the human while the tank walks the whole L.
    constexpr uint32 SH_ASSN_TIMEOUT = 180000;   // 3 min for the full sweep

    // --- lone stealthed sentinel on the O'MROGG APPROACH (guid 151185) ---
    // Sits NE of O'mrogg, encountered BEFORE the O'mrogg fight — a separate anchor
    // ordered ahead of O'mrogg (bit 1), not part of the post-O'mrogg L-hallway.
    constexpr float SH_SENTINEL_X = 481.99f, SH_SENTINEL_Y = 55.09f, SH_SENTINEL_Z = 1.94f;
    // Tight seek so this event only claims the sentinel: the nearest OTHER assassin
    // (382,-32, the hallway's first) is ~133yd away, well outside.
    constexpr float SH_SENTINEL_SEEK_RADIUS = 60.0f;
    constexpr uint32 SH_SENTINEL_TIMEOUT = 90000;   // 90s to reach + kill the one
}

void RegisterShatteredHallsEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(540, 1, "Drop past the door")
                      .Anchored(/*orderIndex, doc-only*/ 0)
                      .TeleportParty(/*door lip*/ 122.78f, 249.59f, -15.0f,
                                     /*landing*/  132.78f, 209.68f, -47.84f)
                      .Build());

    // FIGHT DOWN THE FLAME GAUNTLET IN BOUNDS, THEN KILL THE ARCHERS AND CLEAR
    // THE LEDGE.
    //
    // PERSISTENT: stands down the pull pipeline for the whole corridor (so the
    // advanced-pull FSM never tries to drag a wave back to a camp that is on
    // fire) and survives the many wave combat gaps without rewinding the step
    // list. Step 0 (MoveTo entry) arms the Scout and bumps stepIndex, which is
    // what makes the persistence sticky-trigger latch so the tank may work the
    // whole 190yd corridor from an anchor it left long ago.
    //
    // Steps 1-4 are the BOUNDS. Each is a ClearRadius, and each does two things
    // the old single 90yd advance did not: it holds the party at the bound until
    // nothing is standing on it, and — because an anchored event only ever drives
    // on the NON-COMBAT engine — it cannot advance at all while a wave is on the
    // party. Fight, then push, one ~40yd leg at a time.
    //
    // Step 5 stages on the scout's own fire-free spot. Step 6 KILLS THE ARCHERS,
    // which is the off-switch for the fire (FireArrows() stops re-arming once no
    // 17427 is alive) — it is a KillCreatureEngage rather than a ClearRadius
    // because it must SEEK a specific pair through a pack of zealots, and
    // EngageOnlyWhenActive keeps the combat-side stealth-breaker from arming off
    // it out of turn (the Mechanar/Pathaleon lesson; archers are never
    // undetectable so it could not fire here, but the guard is free).
    //
    // Step 7 clears the ledge. On NORMAL that includes the Shattered Hand Blood
    // Guard, whose death SetData(2)s the scout and cancels the wave scheduler
    // outright. On HEROIC his heroic-only counterpart Porung is a real
    // DungeonEncounter, so NearestHostileNearPoint skips him and the roster's own
    // Porung anchor — ordered immediately after this objective, see the heroic
    // re-key below — is what ends the waves.
    out.push_back(
        EventBuilder(540, 2, "Fight down the flame gauntlet")
            .Anchored(/*orderIndex (doc)*/ 1)
            .Persistent()
            .MoveTo(SH_ENTRY_X, SH_ENTRY_Y, SH_ENTRY_Z, /*radius*/ 8.0f)
            .ClearRadius(SH_B1_X, SH_B1_Y, SH_B1_Z, SH_B1_RADIUS, SH_GAUNTLET_ZBAND)
                .Timeout(SH_BOUND_TIMEOUT)
            .ClearRadius(SH_B2_X, SH_B2_Y, SH_B2_Z, SH_B2_RADIUS, SH_GAUNTLET_ZBAND)
                .Timeout(SH_BOUND_TIMEOUT)
            .ClearRadius(SH_B3_X, SH_B3_Y, SH_B3_Z, SH_B3_RADIUS, SH_GAUNTLET_ZBAND)
                .Timeout(SH_BOUND_TIMEOUT)
            .ClearRadius(SH_B4_X, SH_B4_Y, SH_B4_Z, SH_B4_RADIUS, SH_GAUNTLET_ZBAND)
                .Timeout(SH_BOUND_TIMEOUT)
            .MoveTo(SH_STAGE_X, SH_STAGE_Y, SH_STAGE_Z, /*radius*/ 6.0f)
            .KillCreatureEngage(SH_ARCHER, /*count (doc; "any alive")*/ 2, SH_ARCHER_SEEK)
                .EngageOnlyWhenActive()
                .Timeout(SH_ARCHER_TIMEOUT)
            .ClearRadius(SH_LEDGE_X, SH_LEDGE_Y, SH_LEDGE_Z, SH_LEDGE_RADIUS, SH_GAUNTLET_ZBAND)
                .Timeout(SH_LEDGE_TIMEOUT)
            .Build());

    // SWEEP THE STEALTH-ASSASSIN L-HALLWAY BEFORE KARGATH.
    // PERSISTENT: stands the pull pipeline down and survives the combat gaps
    // between kills as the tank walks the whole L. Step 0 (MoveTo start) arms the
    // persistence sticky. The sweep is a KillCreatureEngage BY ENTRY — NOT a
    // ClearRadius. This is the whole fix: ClearRadius engages via
    // NearestHostileNearPoint -> AttackersValue::IsPossibleTarget, which hard-gates
    // on `bot->CanSeeOrDetect()` — and a stealthed assassin the tank hasn't
    // detected fails that, so ClearRadius reported "clear" and NEVER engaged (the
    // party sat deadlocked in combat with an un-engaged stealthed rogue).
    // KillCreatureEngage instead finds the assassin by ENTRY via FindNearestCreature
    // (a grid scan with NO stealth/visibility filter) and EngageDirect walks the
    // tank to its exact position and Attack()s it (Unit::Attack has no visibility
    // gate) — the first point of damage breaks stealth. One step sweeps all seven:
    // "any alive 17695 within the seek radius" keeps it Running, engaging the
    // nearest each tick, so the tank clears the N-S leg then the W leg. A trailing
    // MoveTo lands the tank at the hall's Kargath-side mouth for a clean handoff.
    out.push_back(
        EventBuilder(540, 3, "Sweep the assassin hallway")
            .Anchored(/*orderIndex (doc)*/ 2)
            .Persistent()
            .MoveTo(SH_ASSN_START_X, SH_ASSN_START_Y, SH_ASSN_START_Z, /*radius*/ 10.0f)
            .KillCreatureEngage(SH_ASSASSIN_ENTRY, /*count (doc; "any alive")*/ 6,
                                SH_ASSN_SEEK_RADIUS)
                .Timeout(SH_ASSN_TIMEOUT)
            .MoveTo(SH_ASSN_END_X, SH_ASSN_END_Y, SH_ASSN_END_Z, /*radius*/ 10.0f)
            .Build());

    // KILL THE LONE STEALTHED SENTINEL ON THE O'MROGG APPROACH.
    // Same stealth-engage mechanism as the hallway sweep (KillCreatureEngage BY
    // ENTRY — FindNearestCreature has no stealth filter, Unit::Attack no visibility
    // gate), but a SEPARATE objective ordered BEFORE O'mrogg (bit 1). The tight
    // seek radius (60yd) keeps this event to the single sentinel; the hallway's
    // assassins are ~133yd south, so the two 17695 sweeps never overlap.
    out.push_back(
        EventBuilder(540, 4, "Kill the O'mrogg-approach assassin")
            .Anchored(/*orderIndex (doc)*/ 1)
            .Persistent()
            .MoveTo(SH_SENTINEL_X, SH_SENTINEL_Y, SH_SENTINEL_Z, /*radius*/ 10.0f)
            .KillCreatureEngage(SH_ASSASSIN_ENTRY, /*count (doc; "any alive")*/ 1,
                                SH_SENTINEL_SEEK_RADIUS)
                .Timeout(SH_SENTINEL_TIMEOUT)
            .Build());

    // Wake Nethekurse: fire his client-only intro once the party reaches him, so
    // he drops SetImmuneToAll and fights an all-bot party the same way he fights
    // a human whose client tripped area trigger 4347 on the walk in. Folded
    // under Nethekurse in the panel — it is his pull, not a step of its own.
    out.push_back(EventBuilder(540, 5, "Wake Grand Warlock Nethekurse")
                      .Conditional(&ShNethekurseDormant)
                      .Repeatable()
                      .PanelBeforeBoss(SH_NETHEKURSE)
                      .Custom(SH_HOOK_START_NETHEKURSE_INTRO)
                      .Build());
}

// --- the wake-up gate (event 5, repeatable) ------------------------------
// DUE while Nethekurse is alive, within scan range, and still carrying the
// spawn-time PC-immunity his intro clears. Reads false the instant
// ACTION_START_INTRO lands (hook 9, a human's area trigger, or a lockpicked
// door — SetImmuneToAll(false) is synchronous inside all of them) and once he
// is dead or out of range, so the repeat can never spin: the only state that
// keeps it true is the exact deadlock it fixes.
//
// Tests IsImmuneToPC DIRECTLY rather than asking IsValidAttackTarget — the
// same lesson as ZfZumrahAsleep: read the one value the whole bug is about,
// which is also the value the hook's DoAction writes.
namespace
{
    bool ShNethekurseDormant(Player* bot, AiObjectContext* /*context*/)
    {
        Creature* nethekurse = bot->FindNearestCreature(SH_NETHEKURSE, SH_NETHEKURSE_SCAN);
        if (!nethekurse || !nethekurse->IsAlive())
            return false;

        // Still immune-to-PC => nobody has started his intro.
        return nethekurse->IsImmuneToPC();
    }
}

// --- roster patch --------------------------------------------------------
void RegisterShatteredHallsRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // The auto-roster derives all bosses (Nethekurse 16807 / bit 0, Omrogg,
    // Kargath) from their static spawns, so no boss surgery is needed. But the
    // path to Nethekurse crosses a navmesh BREAK at a door: the party stands at
    // the lip (122.78, 249.59, -15) and must drop ~33yd down to
    // (132.78, 209.68, -47.84), which sits on a disconnected mesh island boss-nav
    // can't route to.
    //
    // Add a travel OBJECTIVE at the door lip, ordered BEFORE Nethekurse. It
    // borrows encounterIndex 0 (Nethekurse's bit): the Objective-before-Boss
    // tie-break in Apply() sorts it AHEAD of Nethekurse at the shared key, so the
    // tank visits this objective and only then Nethekurse. Sharing the bit is
    // safe — an objective is filtered by the cleared-anchor latch, never the
    // completion mask (NextDungeonBossValue keys the mask to Boss anchors only),
    // so Nethekurse's eventual kill can't retro-complete it. Its eventId 1
    // (ShatteredHallsEvents.cpp) teleports the whole party across the break the
    // instant the tank reaches the lip.
    //
    // Second, a travel OBJECTIVE at the flame-gauntlet ENTRY, ordered AFTER
    // Nethekurse and BEFORE O'mrogg. It borrows encounterIndex 1 — which the
    // map-540 NORMAL DBC assigns to O'MROGG (normal bits: Nethekurse 0,
    // O'mrogg 1, Kargath 2 — Porung has no normal row at all; on HEROIC the DBC
    // inserts Porung AT bit 1, shifting O'mrogg to 2 and Kargath to 3 — see the
    // HeroicOnly patch below). So the Objective-before-Boss tie-break sorts
    // this objective AHEAD of O'mrogg at the shared key — the flame gauntlet runs,
    // then O'mrogg. Its eventId 2 (the persistent "Fight down the flame gauntlet"
    // event) drives the whole corridor: arm the scout, fight down the hall in
    // ~40yd bounds, stage on the fire-free ledge, kill the archers, clear the far
    // pack. Sharing bit 1 is safe — an objective is filtered by the cleared-anchor
    // latch, never the completion mask (NextDungeonBossValue keys the mask to Boss
    // anchors only).
    {
        BossRosterPatch p;
        p.mapId = 540;
        p.add = {
            MakeObjective(OBJ(1), /*encounterIndex*/ 0, 540,
                          "Drop past the door",
                          122.78f, 249.59f, -15.0f,
                          /*arriveRadius*/ 6.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 1),
            MakeObjective(OBJ(2), /*encounterIndex*/ 1, 540,
                          "Fight down the flame gauntlet",
                          /*entry anchor*/ SH_ENTRY_X, SH_ENTRY_Y, SH_ENTRY_Z,
                          /*arriveRadius*/ 10.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 2),
            // Third, a travel OBJECTIVE at the stealth-assassin hallway START,
            // ordered AFTER O'mrogg (bit 1) and BEFORE Kargath (bit 2). It borrows
            // encounterIndex 2 — Kargath's own kill-bit — so the Objective-before-
            // Boss tie-break sorts it AHEAD of Kargath at that shared key: the tank
            // walks into the L-hallway and sweeps the stealthed Shattered Hand
            // Assassins (17695) BY ENTRY before engaging Kargath. (Map-540 NORMAL
            // DBC bits are Nethekurse 0, O'mrogg 1, Kargath 2 — an earlier cut used
            // 3 and the objective sorted PAST Kargath to the end of the clear. On
            // HEROIC Kargath shifts to bit 3, so the HeroicOnly patch below re-keys
            // this objective to 3 there.) Sharing
            // bit 2 is safe — an objective is filtered by the cleared-anchor latch,
            // never the completion mask (NextDungeonBossValue keys the mask to Boss
            // anchors only), so Kargath's eventual kill can't retro-complete it. Its
            // eventId 3 is the persistent "Sweep the assassin hallway" event.
            MakeObjective(OBJ(3), /*encounterIndex*/ 2, 540,
                          "Sweep the assassin hallway",
                          SH_ASSN_START_X, SH_ASSN_START_Y, SH_ASSN_START_Z,
                          /*arriveRadius*/ 10.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 3),
            // Fourth, the lone stealthed sentinel on the O'MROGG APPROACH, ordered
            // BEFORE O'mrogg. It borrows encounterIndex 1 — O'mrogg's own NORMAL
            // bit — so the Objective-before-Boss tie-break sorts it AHEAD of
            // O'mrogg. It also shares bit 1 with the flame-gauntlet objective
            // (OBJ 2); at an equal key the stable_sort keeps insertion order, and
            // OBJ(2) is added first, so the normal sequence is
            // gauntlet -> sentinel -> O'mrogg. Its eventId 4 (persistent "Kill the
            // O'mrogg-approach assassin") engages the sentinel by entry. Sharing
            // bit 1 is safe — objectives latch on the cleared anchor, never the
            // completion mask. On HEROIC bit 1 is PORUNG, so this row is re-keyed
            // to 2 by the patch below; see there for why walking south with him
            // still alive is the wrong order.
            MakeObjective(OBJ(4), /*encounterIndex*/ 1, 540,
                          "Kill the O'mrogg-approach assassin",
                          SH_SENTINEL_X, SH_SENTINEL_Y, SH_SENTINEL_Z,
                          /*arriveRadius*/ 10.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 4),
        };
        t.push_back(std::move(p));
    }

    // HEROIC index-shift correction. The heroic DBC INSERTS Blood Guard Porung
    // at bit 1, which shifts O'mrogg to 2 and Kargath to 3. Read straight out of
    // DungeonEncounter.dbc (columns ID, MapID, Difficulty, OrderIndex, Bit):
    //     407 540 diff0 order0    bit0  Grand Warlock Nethekurse
    //     410 540 diff0 order1000 bit1  Warbringer O'mrogg
    //     412 540 diff0 order2000 bit2  Warchief Kargath Bladefist
    //     408 540 diff1 order1000 bit0  Grand Warlock Nethekurse
    //     409 540 diff1 order2000 bit1  Blood Guard Porung
    //     411 540 diff1 order3000 bit2  Warbringer O'mrogg
    //     413 540 diff1 order4000 bit3  Warchief Kargath Bladefist
    // (Note the instance_encounters ROW order — 407/409/410/412 — is the dbcEntry
    // id order and NOT the Bit order; reading it as the latter is what put the
    // assassin objective past Kargath in an earlier cut.)
    //
    // The borrowed keys above then land differently on heroic:
    //   * OBJ(1) key 0 — still before Nethekurse. Fine.
    //   * OBJ(2) key 1 — now sorts before PORUNG, which is exactly right: the
    //     gauntlet event walks the corridor and kills the archers, and Porung is
    //     the very last thing on that ledge. Gauntlet -> Porung.
    //   * OBJ(4) key 1 — the lone sentinel at (482, 55). ALSO sorted before
    //     Porung, and that is WRONG: the sentinel sits ~260yd SOUTH of the ledge,
    //     down through the training yard, so the tank would leave the gauntlet
    //     ledge with Porung alive (and therefore with the 45s zealot summons still
    //     running, since only his death cancels the scout's scheduler), walk the
    //     whole yard with waves chasing, and then walk back north for him. Re-key
    //     it to 2 (O'mrogg's heroic bit) so the order is
    //     gauntlet -> Porung -> sentinel -> O'mrogg, matching normal's
    //     gauntlet -> sentinel -> O'mrogg once Porung is factored out.
    //   * OBJ(3) key 2 — now O'MROGG's bit, which would send the tank to the
    //     stealth L-hallway BEFORE O'mrogg. Wrong: the hallway sits between
    //     O'mrogg and Kargath. Re-key it to 3 (Kargath's heroic bit) so the
    //     Objective-before-Boss tie-break keeps sweep-then-Kargath.
    {
        BossRosterPatch p;
        p.mapId = 540;
        p.gate = DcDifficultyGate::HeroicOnly;
        p.reorder = {{OBJ(3), 3}, {OBJ(4), 2}};
        t.push_back(std::move(p));
    }
}
