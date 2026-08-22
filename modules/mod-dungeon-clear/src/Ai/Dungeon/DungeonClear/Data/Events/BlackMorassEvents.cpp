/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "Creature.h"
#include "Player.h"
#include "Playerbots.h"

// --- The Black Morass (map 269) — CoT: Opening of the Dark Portal -----------
//
// A FULLY event-driven dungeon (core: instance_the_black_morass.cpp +
// the_black_morass.cpp): no static bosses, no doors. A player within 20yd of
// Medivh (15608) starts the event (npc_medivh_bm::MoveInLineOfSight ->
// SetData(DATA_MEDIVH)); 18 "time rift" waves then open one at a time at 4
// fixed swamp positions (next rift auto-queues at 90s, 2min from rift 13 — so
// slow kills OVERLAP waves). Each rift summons a keeper 6s in (waves 6/12: the
// bosses Chrono Lord Deja / Temporus, which fight at the rift; wave 18: AEONUS,
// which does NOT — see kBmDrainEntries; others: a random rift keeper/lord) and
// trash every 15s that walks to a ring 14yd around Medivh and
// channels Corrupt, draining his shield (GetData(DATA_SHIELD_PERCENT), 100->0;
// 0 = failure, everything despawns and the instance self-recovers after 300s).
// KILLING THE KEEPER DESPAWNS ITS RIFT and stops its trash. Aeonus (wave 18)
// dying wins: GetBossState(DATA_AEONUS) == DONE, quest credit, outro.
//
// All three bosses are TempSummons (spawnId 0), invisible to the spawn store,
// so there is NO derived roster — the Old Hillsbrad model: the objectives ARE
// the clear.
//
//   (1) Sa'at            — entrance NPC gossip (Chrono-Beacon). Quest-gated:
//                          unreachable for a questless bot, so it is a short
//                          best-effort step for a quest-carrying party member
//                          (see event 1).
//   (2) Portal grounds   — pre-clear the hostile swamp beasts (crocolisks /
//                          jaguars / tarantulas, faction 16) off the 4 rift
//                          spawn sites before starting, so they never add
//                          mid-wave.
//   (3) Defend Medivh    — walk into Medivh's 20yd start trigger, then hold the
//                          Medivh-side hold point (hook 8, the Arcatraz
//                          DriveMellicharWaves pattern — also restarts after a
//                          shield-failure reset). Completes on Aeonus DONE,
//                          which ends the run; fails it on shield 0.
//
// The actual WAVE COMBAT is a separate CONDITIONAL + REPEATABLE event (id 4)
// whose single Custom step (hook 12) IS the strategy, re-decided every tick:
//
//      hold ON Medivh — every add walks to a home 14yd around him, so his ring
//      is the one spot that intercepts all four rifts with zero travel, and the
//      adds are REACT_DEFENSIVE so the driver force-pulls whatever stands next
//      to the party -> when the ring is clean, run to the open rift and kill its
//      KEEPER, the only thing whose death closes a rift and stops its pump,
//      picking up the adds that spawn on top of you -> rift closes -> run back,
//      clean up whatever leaked -> next rift.
//
// Two properties of that event are load-bearing and easy to lose:
//   * DrivesInCombat() — this is a continuous wave fight, and the ordinary
//     conditional-event rung stands down on bot->IsInCombat(). Without the flag
//     the driver only ran between waves and stopped running entirely once the
//     party fell behind and combat stopped dropping. That gate alone is why runs
//     collapsed whenever two rifts were open at once.
//   * ONE step, not a list — the priority above is a standing preference, not a
//     sequence. See event 4's own comment for the three failures a step list
//     structurally could not avoid.
// Repeatable => it re-fires for all 18 waves; Optional => a timed-out step
// (wipe, corpse-run) SKIPS and the repeat re-fires fresh, so it never stalls the
// run for a human.

namespace
{
    // --- creature entries (the_black_morass.h) ----------------------------
    constexpr uint32 BM_NPC_SAAT              = 20201;

    constexpr uint32 BM_NPC_AEONUS            = 17881;
    constexpr uint32 BM_NPC_CHRONO_LORD_DEJA  = 17879;
    constexpr uint32 BM_NPC_INF_CHRONO_LORD   = 21697;  // heroic re-kill wave 6
    constexpr uint32 BM_NPC_TEMPORUS          = 17880;
    constexpr uint32 BM_NPC_INF_TIMEREAVER    = 21698;  // heroic re-kill wave 12

    constexpr uint32 BM_NPC_RIFT_LORD         = 17839;
    constexpr uint32 BM_NPC_RIFT_LORD_2       = 21140;
    constexpr uint32 BM_NPC_KEEPER_WARLOCK    = 21104;
    constexpr uint32 BM_NPC_KEEPER_MAGE       = 21148;

    constexpr uint32 BM_NPC_CHRONOMANCER      = 17892;
    constexpr uint32 BM_NPC_CHRONOMANCER_2    = 21136;
    constexpr uint32 BM_NPC_ASSASSIN          = 17835;
    constexpr uint32 BM_NPC_ASSASSIN_2        = 21137;
    constexpr uint32 BM_NPC_WHELP             = 21818;
    constexpr uint32 BM_NPC_EXECUTIONER       = 18994;
    constexpr uint32 BM_NPC_EXECUTIONER_2     = 21138;
    constexpr uint32 BM_NPC_VANQUISHER        = 18995;
    constexpr uint32 BM_NPC_VANQUISHER_2      = 21139;

    // Every hostile a rift can field — the Infinite Dragonflight and its rift
    // keepers, and NOTHING else. It is the union of kBmKeeperEntries and
    // kBmDrainEntries below, and its one consumer is the wave event's activation
    // predicate, which scans it (after the rift itself) to decide "something is
    // still alive" and so keep the driver in charge for the post-close sweep.
    // It must stay COMPLETE (order is irrelevant): an entry missing here reads as
    // "the arena is quiet" and hands the tick back to the plain garrison while
    // that mob walks into Medivh.
    //
    // It must also stay EXCLUSIVE. The arena is open swamp full of ambient fauna
    // (crocolisks, jaguars, tarantulas), and admitting any of them would leave
    // the driver due — and the tank steering — for a beast wandering 200yd away.
    // Ambient beasts are still fought when they aggro the party; that is the
    // combat engine's job, not the driver's.
    constexpr uint32 kBmWaveEntries[] =
    {
        BM_NPC_AEONUS,
        BM_NPC_CHRONO_LORD_DEJA, BM_NPC_INF_CHRONO_LORD,
        BM_NPC_TEMPORUS,         BM_NPC_INF_TIMEREAVER,
        BM_NPC_RIFT_LORD,        BM_NPC_RIFT_LORD_2,
        BM_NPC_KEEPER_WARLOCK,   BM_NPC_KEEPER_MAGE,
        BM_NPC_CHRONOMANCER,     BM_NPC_CHRONOMANCER_2,
        BM_NPC_ASSASSIN,         BM_NPC_ASSASSIN_2,
        BM_NPC_WHELP,
        BM_NPC_EXECUTIONER,      BM_NPC_EXECUTIONER_2,
        BM_NPC_VANQUISHER,       BM_NPC_VANQUISHER_2,
    };

    // THE SHIELD DRAINERS — everything that parks on Medivh's ring and channels
    // Corrupt instead of fighting the party. NOT the Rift Lords (17839/21140) or
    // Rift Keepers (21104/21148), which fight normally.
    //
    // The nine trash entries carry the smart_scripts row
    //
    //     event_type 25 (SMART_EVENT_RESET), action 11 (CAST),
    //     spell 31326 'Corrupt Medivh', target_type 1 (SELF)
    //
    // They do NOT attack Medivh. DoSummonAtRift gives each one a home 14yd from
    // him and sends it there with MoveTargetedHome; on arrival it RESETS and
    // casts Corrupt on ITSELF, and spell_black_morass_corrupt_medivh's periodic
    // tick then calls SetData(DATA_DAMAGE_SHIELD) every third tick. So the thing
    // that loses the run is a stationary, never-aggroed mob standing at its home
    // with a self-buff — no victim, no combat, nothing pointed at the party.
    //
    // That is exactly the shape the normal targeting cannot see. Its selector
    // (DcTargeting::NearestHostileNearPoint) runs every candidate through
    // AttackersValue::IsPossibleTarget, which hard-gates a creature parked in
    // that state, so a position sweep reports the volume EMPTY while the party
    // stands on top of the mob draining the shield — observed live, 2026-07-25.
    //
    // The fix is a deterministic FORCE-PULL by entry, in the wave driver
    // (ObjectiveHookRegistry hook 12, BmForcePull): a raw grid scan with no
    // visibility/evade/tap filter, then EngageWithTarget + AttackStart, which
    // drags the mob out of its reset idle and hands it to the ordinary combat
    // engine. Deliberately NOT fixed by loosening IsPossibleTarget or the
    // `!bot->IsHostileTo()` gate inside EngageDirect: both are shared by every
    // dungeon's engage path and their strictness is load-bearing elsewhere.
    //
    // This list is also the SWEEP COUNT — how many of these are alive inside
    // Medivh's ring is what tells the driver his shield is being drained and it
    // is time to come back and clean up.
    //
    // AEONUS IS ONE OF THESE, not a keeper — the single most important entry in
    // the list. boss_aeonus::IsSummonedBy does verbatim what DoSummonAtRift does
    // to the trash:
    //
    //     me->SetReactState(REACT_DEFENSIVE);
    //     me->SetHomePosition(medivh + 14yd);
    //     me->GetMotionMaster()->MoveTargetedHome();
    //
    // and JustReachedHome then DoCastAOE(37853) once inside 20yd of Medivh —
    // which spell_black_morass_corrupt_medivh drains at 2 per tick, DOUBLE the
    // trash rate. So the final boss never aggros the party, walks past it to
    // Medivh, and quietly drains the run out from under them.
    //
    // Deja and Temporus are NOT like this (checked: plain BossAI, no
    // IsSummonedBy override) — they stay aggressive and fight at their rift, so
    // they are keepers. Aeonus is the lone exception.
    constexpr uint32 kBmDrainEntries[] =
    {
        BM_NPC_ASSASSIN,     BM_NPC_ASSASSIN_2,
        BM_NPC_CHRONOMANCER, BM_NPC_CHRONOMANCER_2,
        BM_NPC_EXECUTIONER,  BM_NPC_EXECUTIONER_2,
        BM_NPC_VANQUISHER,   BM_NPC_VANQUISHER_2,
        BM_NPC_WHELP,
        BM_NPC_AEONUS,
    };

    // THE RIFT KEEPERS — everything npc_time_rift's EVENT_SUMMON_BOSS can field
    // 6s after a rift opens: a random Rift Keeper / Rift Lord on an ordinary
    // wave, and the three real bosses on waves 6 / 12 / 18 (plus their heroic
    // re-kill variants). Killing one DESPAWNS ITS RIFT and stops that rift's 15s
    // add pump — the only shutoff the encounter has — which is why the wave
    // driver selects and pulls by this list rather than by "nearest hostile".
    //
    // Deliberately disjoint from kBmDrainEntries: these carry no Corrupt cast, so
    // they never drain the shield and are never sweep targets.
    //
    // AEONUS IS DELIBERATELY ABSENT even though it is the wave-18 boss, because it
    // fails BOTH jobs this list has:
    //
    //  * it is never AT the rift to be found. boss_aeonus::IsSummonedBy sends it
    //    to Medivh the instant it spawns, so BmKeeperOf's 40yd band loses it
    //    within seconds and rift selection would be steering on a ghost;
    //  * killing it does not even close its rift. npc_time_rift::JustSummoned sets
    //    _riftKeeperGUID only for the first NON-Aeonus summon, so
    //    SummonedCreatureDies never matches Aeonus and rift 18 keeps pumping.
    //
    // It lives in kBmDrainEntries instead, which is where its actual behaviour
    // belongs. Winning is still detected the only way it ever was — the instance's
    // own GetBossState(DATA_AEONUS) == DONE.
    constexpr uint32 kBmKeeperEntries[] =
    {
        BM_NPC_RIFT_LORD,        BM_NPC_RIFT_LORD_2,
        BM_NPC_KEEPER_WARLOCK,   BM_NPC_KEEPER_MAGE,
        BM_NPC_CHRONO_LORD_DEJA, BM_NPC_INF_CHRONO_LORD,
        BM_NPC_TEMPORUS,         BM_NPC_INF_TIMEREAVER,
    };

    // --- geometry ---------------------------------------------------------
    // Sa'at's spawn at the instance entrance.
    constexpr float BM_SAAT_X = -1547.06f, BM_SAAT_Y = 7113.63f, BM_SAAT_Z = 32.82f;

    // The 4 fixed rift spawn positions (instance_the_black_morass.cpp
    // PortalLocation[]). Visit order below is nearest-first from the entrance:
    // P3 sits on the way in, P4 north, then south across the arena P2 -> P1.
    // P1 ends the sweep ~95yd from Medivh — the route never crosses his 20yd
    // start trigger, so the event cannot fire before the grounds are clear.
    constexpr float BM_P1_X = -2030.83f, BM_P1_Y = 7024.94f, BM_P1_Z = 23.07f;
    constexpr float BM_P2_X = -1961.73f, BM_P2_Y = 7029.53f, BM_P2_Z = 21.81f;
    constexpr float BM_P3_X = -1887.70f, BM_P3_Y = 7106.56f, BM_P3_Z = 22.05f;
    constexpr float BM_P4_X = -1930.91f, BM_P4_Y = 7183.60f, BM_P4_Z = 23.01f;

    // Each site's pre-clear radius. The rift's own summons appear <=10yd from
    // it; 35 clears the ambient beasts whose wander could add into that.
    constexpr float BM_PORTAL_CLEAR_RADIUS = 35.0f;

    // THE HOLD POINT the tank garrisons whenever nothing better is due: 12yd
    // from Medivh's spawn, on the bearing toward the arena centre. Mirrored in
    // ObjectiveHookRegistry (hooks 8 and 12) — the two must agree or the
    // objective garrison and the wave driver fight over the tank.
    //
    // This used to be a "mid-arena stand point" 38yd out, chosen to sit OUTSIDE
    // Medivh's 20yd start trigger. Both halves of that were wrong. Every rift add
    // walks to a home 14yd around MEDIVH, so his ring is the one spot in the
    // arena that intercepts all four rifts for free — parking 38yd away means
    // each add gets to establish its Corrupt channel before anyone is near it.
    // And by the time the party is at this objective the grounds are pre-cleared
    // and starting the event is exactly what we want (npc_medivh_bm::
    // MoveInLineOfSight guards itself with `!events.Empty()`, so a later arrival
    // is a no-op).
    constexpr float BM_HOLD_X = -2014.4f, BM_HOLD_Y = 7114.6f, BM_HOLD_Z = 22.8f;

    // Arena centroid (mean of the 4 portals), used only by the wave event's
    // proximity gate. 250 matches the engage-gate scan default: the event is due
    // only where its driver can actually SEE the rifts, so a far-away leader
    // (corpse-run) can never spin — it walks back on the objective machinery
    // first.
    constexpr float BM_ARENA_X = -1952.8f, BM_ARENA_Y = 7086.2f;
    constexpr float BM_EVENT_DUE_RANGE = 250.0f;
    constexpr float BM_WAVE_SCAN = 250.0f;

    // Per-step budgets. A portal hop is up to ~160yd of open swamp with beast
    // fights on the way. The whole 18-wave event runs >=27min wall-clock (the
    // rift cadence is fixed), so both driver Custom steps get a full hour — they
    // are Running-until-the-encounter-ends by construction, and a timeout on
    // either is a bug report, not a control-flow mechanism.
    // Sa'at's gossip option is quest-gated and unreachable for a questless bot
    // (see event 1) — cap the guaranteed loss instead of burning the 30s
    // EventStepTimeout at the instance door on every run.
    constexpr uint32 BM_SAAT_TIMEOUT_MS  = 5000;
    constexpr uint32 BM_HOP_TIMEOUT_MS   = 180000;
    constexpr uint32 BM_CLEAR_TIMEOUT_MS = 180000;
    constexpr uint32 BM_EVENT_TIMEOUT_MS = 3600000;

    constexpr uint32 BM_HOOK_DRIVE_EVENT = 8;   // ObjectiveHookRegistry id
    constexpr uint32 BM_HOOK_DRIVE_WAVE  = 12;  // ObjectiveHookRegistry id
    constexpr uint32 BM_NPC_TIME_RIFT    = 17838;

    bool BmWaveHostilesActive(Player* bot, AiObjectContext* context);
}

// Shared with the wave driver (ObjectiveHookRegistry hook 12) — the force-pull
// and the Medivh-ring sweep count.
std::vector<uint32> const& BlackMorassDrainEntries()
{
    static std::vector<uint32> const entries(std::begin(kBmDrainEntries),
                                             std::end(kBmDrainEntries));
    return entries;
}

// Shared with the wave driver (ObjectiveHookRegistry hook 12) — rift selection
// and the keeper pull. See the header declaration.
std::vector<uint32> const& BlackMorassKeeperEntries()
{
    static std::vector<uint32> const entries(std::begin(kBmKeeperEntries),
                                             std::end(kBmKeeperEntries));
    return entries;
}

void RegisterBlackMorassEvents(std::vector<DungeonEvent>& out)
{
    // (1) TALK TO SA'AT AT THE ENTRANCE — the Chrono-Beacon handout.
    // Menu 8088 option 0 ("I have lost the chrono-beacon...") casts 34975 on
    // the selecting player. QUEST-GATED, not merely flavour-gated: re-reading
    // `conditions` (SourceType 15, SourceGroup 8088) the option carries
    // CONDITION_QUEST_NONE(10297) with NegativeCondition=1 — i.e. it needs the
    // quest to NOT be status-NONE, so the talker must be CARRYING quest 10297.
    // Option 1 needs it REWARDED. A DC tank bot is questless, so BOTH options
    // are filtered and the menu opens EMPTY — SelectGossip can never latch and
    // the step spins Running until it times out.
    //
    // The earlier note here read condition 14 backwards ("never taken … all
    // true for a questless bot"), so the step carried no timeout and burned the
    // full 30s EventStepTimeout at the instance door on EVERY run (live
    // 2026-07-24: 74s and 71s between the Sa'at and portal-grounds objectives
    // on two consecutive runs). Explicit short timeout: still best-effort for a
    // party member who DOES hold the quest, ~5s of loss for one who doesn't.
    // The objective itself stays — it is encounterIndex 1, and dropping it
    // would shift the index space of everything after it.
    out.push_back(
        EventBuilder(269, 1, "Sa'at (Chrono-Beacon)")
            .Anchored(/*orderIndex*/ 1)
            .Optional()
            .Gossip(BM_NPC_SAAT, /*option*/ 0, /*searchRadius*/ 20.0f)
                .SkipIfTargetMissing()
                .Timeout(BM_SAAT_TIMEOUT_MS)
            .Build());

    // (2) CLEAR THE PORTAL GROUNDS.
    // Sweep the 4 rift sites free of the swamp beasts before starting the
    // event. Step 0 is the lenient arrival bump at the anchor (P3) so the
    // persistent event goes sticky (OH pattern) and the later hops can roam.
    // PERSISTENT: every ClearRadius is a combat gap that would rewind a
    // normal event's step list.
    out.push_back(
        EventBuilder(269, 2, "Clear the portal grounds")
            .Anchored(/*orderIndex*/ 2)
            .Persistent()
            .MoveTo(BM_P3_X, BM_P3_Y, BM_P3_Z, /*radius*/ 30.0f)
            .ClearRadius(BM_P3_X, BM_P3_Y, BM_P3_Z, BM_PORTAL_CLEAR_RADIUS)
                .Timeout(BM_CLEAR_TIMEOUT_MS)
            .MoveTo(BM_P4_X, BM_P4_Y, BM_P4_Z, /*radius*/ 8.0f)
                .Timeout(BM_HOP_TIMEOUT_MS)
            .ClearRadius(BM_P4_X, BM_P4_Y, BM_P4_Z, BM_PORTAL_CLEAR_RADIUS)
                .Timeout(BM_CLEAR_TIMEOUT_MS)
            .MoveTo(BM_P2_X, BM_P2_Y, BM_P2_Z, /*radius*/ 8.0f)
                .Timeout(BM_HOP_TIMEOUT_MS)
            .ClearRadius(BM_P2_X, BM_P2_Y, BM_P2_Z, BM_PORTAL_CLEAR_RADIUS)
                .Timeout(BM_CLEAR_TIMEOUT_MS)
            .MoveTo(BM_P1_X, BM_P1_Y, BM_P1_Z, /*radius*/ 8.0f)
                .Timeout(BM_HOP_TIMEOUT_MS)
            .ClearRadius(BM_P1_X, BM_P1_Y, BM_P1_Z, BM_PORTAL_CLEAR_RADIUS)
                .Timeout(BM_CLEAR_TIMEOUT_MS)
            .Build());

    // (3) DEFEND MEDIVH — start the event, garrison through all 18 waves.
    // Step 0 bumps the persistent event sticky at the hold-point anchor; the
    // Custom step (hook 8) then owns the START and the END: walk into Medivh's
    // 20yd proximity trigger to start (and to RESTART after a shield-failure
    // reset — the hook re-nudges whenever the rift counter reads 0 with Medivh
    // alive), hold the point between waves, Done when Aeonus is DONE, and fail
    // the run outright if the shield hits 0.
    //
    // The wave fights themselves are event 4's job — its EventDue rung (31)
    // preempts this garrison (30) exactly while anything is up.
    // PERSISTENT: the event spans every wave fight of the run.
    out.push_back(
        EventBuilder(269, 3, "Defend Medivh (Opening of the Dark Portal)")
            .Anchored(/*orderIndex*/ 3)
            .Persistent()
            // Hook 8 walks the tank into Medivh's 20yd trigger and holds the hold
            // point, both through the long-haul spline funnel. Without this the
            // at-objective StopBot(Hold) cancels that glide every tick before the
            // hook runs — the Old Hillsbrad barrel trap. See
            // DungeonEvent::stepsOwnMovement.
            .StepsOwnMovement()
            .MoveTo(BM_HOLD_X, BM_HOLD_Y, BM_HOLD_Z, /*radius*/ 8.0f)
            .Custom(BM_HOOK_DRIVE_EVENT)
                .Timeout(BM_EVENT_TIMEOUT_MS)
            .Build());

    // (4) CLOSE THE TIME RIFT — the wave driver.
    //
    // ONE Custom step (hook 12), and that is the whole point. Every earlier cut
    // of this event was a SEQUENTIAL STEP LIST — camp, then nine per-entry
    // drainer kills, then four portal volumes, then Medivh, then the arena — and
    // each cut failed for the same structural reason: a step list can only say
    // "do these in order and block on each", and what this encounter needs is a
    // standing PREFERENCE re-evaluated every tick as rifts open and adds arrive.
    //
    // Concretely, what the list could not express (all three observed live):
    //   * The rift KEEPER is the only thing whose death closes a rift and stops
    //     its 15s add pump, so it has to be reachable FIRST, always. In the list
    //     it sat behind nine drainer-kill gates that a live rift re-blocked every
    //     15s, so it was never reached and no rift ever closed — a spiral with no
    //     floor once the party fell behind.
    //   * The portal volumes engaged the NEAREST hostile, which is whichever add
    //     just spawned 10yd away, never the keeper. Killing adds at a portal does
    //     not close it.
    //   * With two or more rifts open, "nearest rift" flips as the tank moves, so
    //     the party oscillated between portals and closed neither.
    // See BmDriveWave's header (ObjectiveHookRegistry) for the priority it runs.
    //
    // DRIVES IN COMBAT — the load-bearing flag. The conditional-event rung stands
    // down on bot->IsInCombat(), which is right for a lever or a gossip and fatal
    // here: this is a continuous wave fight, so with two rifts pumping the party
    // never leaves combat, the driver never runs, and the tank simply stands where
    // its last fight ended. That one gate is why runs "fell apart any time two
    // portals were open". See DungeonEvent::drivesInCombat.
    //
    // REPEATABLE (18 waves; the condition going false — no rift, nothing alive —
    // is the only "done") + OPTIONAL (a timeout skips and the repeat re-fires
    // fresh, so a wipe or a corpse-run never hard-stalls the run).
    out.push_back(
        EventBuilder(269, 4, "Close the time rift")
            .Conditional(&BmWaveHostilesActive)
            .Repeatable()
            .Optional()
            .DrivesInCombat()
            .StepsOwnMovement()
            .Custom(BM_HOOK_DRIVE_WAVE)
                .Timeout(BM_EVENT_TIMEOUT_MS)
            .Build());
}

// --- the wave gate (event 4, repeatable) ---------------------------------
// DUE while a Time Rift is OPEN — the portal itself is the first probe, so the
// party moves out the moment the swirling light appears, during the quiet 6s
// before its keeper even spawns — or while any wave hostile is still alive, which
// is what keeps the driver in charge for the Medivh-ring sweep after a rift
// closes. Grid scans (NOT the spawn store — the rift and every wave mob are
// TempSummons with spawnId 0; the Arcatraz Skyriss precedent), early-exiting on
// the first hit; the full 18-entry sweep only runs in the quiet between waves.
// The 250yd proximity gate keeps the event not-due for a leader outside
// engage-scan range (corpse-run), where the driver would otherwise steer a bot
// that cannot see the arena.
//
// Note this predicate is the ONLY thing that ends the driver's turn: while it
// reads true the wave event outranks the "Defend Medivh" garrison (31 over 30)
// and, in combat, outranks the stock combat movers (DcRel::EventDueCombat).
namespace
{
    bool BmWaveHostilesActive(Player* bot, AiObjectContext* /*context*/)
    {
        if (bot->GetExactDist2d(BM_ARENA_X, BM_ARENA_Y) > BM_EVENT_DUE_RANGE)
            return false;

        if (bot->FindNearestCreature(BM_NPC_TIME_RIFT, BM_WAVE_SCAN, /*alive*/ true))
            return true;

        for (uint32 entry : kBmWaveEntries)
            if (bot->FindNearestCreature(entry, BM_WAVE_SCAN, /*alive*/ true))
                return true;
        return false;
    }
}

// --- roster patch: the three objectives (no derived roster; all bosses are
// TempSummons summoned by their rift) -------------------------------------
void RegisterBlackMorassRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = 269;
    p.add = {
        MakeObjective(OBJ(1), /*encounterIndex*/ 1, 269, "Sa'at (Chrono-Beacon)",
                      BM_SAAT_X, BM_SAAT_Y, BM_SAAT_Z, /*arriveRadius*/ 10.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 1, /*orderOverride*/ 1),
        MakeObjective(OBJ(2), /*encounterIndex*/ 2, 269, "Clear the portal grounds",
                      BM_P3_X, BM_P3_Y, BM_P3_Z, /*arriveRadius*/ 25.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 2, /*orderOverride*/ 2),
        MakeObjective(OBJ(3), /*encounterIndex*/ 3, 269,
                      "Defend Medivh (Opening of the Dark Portal)",
                      BM_HOLD_X, BM_HOLD_Y, BM_HOLD_Z, /*arriveRadius*/ 12.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 3, /*orderOverride*/ 3),
    };
    t.push_back(std::move(p));
}
