/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "Creature.h"
#include "InstanceScript.h"
#include "Player.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"

// --- Stratholme (map 329) — live (Scarlet) side: Dathrohan -> Balnazzar -----
// The Balnazzar encounter (DBC bit 6) credits creature 10813 (Balnazzar), which
// has NO creature.sql spawn: it exists only via Grand Crusader Dathrohan's
// (10812) SmartAI, which at <=40% HP casts Balnazzar Transform and UpdateEntry's
// the SAME creature in place (10812 -> 10813, GUID preserved). BossSpawnIndex
// matches credit entries to spawns, so the spawn-less 10813 is dropped and the
// clear skips from Archivist Galford (bit 5) straight to the dead side, never
// fighting the live boss. (Same shape as Ramstein, also a spawn-less credit.)
//
// Fix: a BossRosterRegistry OBJECTIVE at Dathrohan's static spawn (bit 6, right
// after Galford), carrying anchored event 5. The objective only travels the tank
// in; an Objective completes on arrival WITHOUT engaging, so the event does the
// fighting: KillCreatureEngage(10812) seeks and pulls Dathrohan, then
// KillCreatureEngage(10813) finishes the transformed Balnazzar.
//
// Two steps, not one: a lone KillCreatureEngage(10812) would report Done the
// instant he transforms at 40% (no live 10812 remains) while Balnazzar is still
// up — the tank would latch the objective and walk off mid-fight. Step 2 holds
// until 10813 is actually dead. Non-persistent: both steps are idempotent
// kill-gates (no WaitForSpawn to false-complete across a combat gap, unlike the
// Slaughterhouse), and the whole fight is one continuous combat at a fixed spot,
// so a restart-from-0 after any combat lull just re-evaluates correctly.
//
// --- Stratholme (map 329) — the dead side / "Baron run" --------------------
// The Undead-side set-piece, verified from instance_stratholme.cpp. Two
// independent instance counters gate the whole thing; without orchestration the
// dungeon-clear tank pulls the open-air bosses but never finishes the gates and
// wedges. The five DBC bosses (Baroness, Nerub'enkan, Maleki, Ramstein, Baron)
// keep their natural anchors — we only add the bits the engage pipeline cannot
// reach on its own:
//
//   ZIGGURATS. Each of the three ziggurat bosses (Baroness 10436 / TYPE_ZIGGURAT1,
//   Nerub'enkan 10437 / TYPE_ZIGGURAT2, Maleki 10438 / TYPE_ZIGGURAT3) sits
//   OUTSIDE its ziggurat and is pulled normally. Its death sets TYPE_ZIGGURATx=1,
//   which OPENS the ziggurat door. INSIDE are Thuzadin Acolytes (10399); killing
//   them topples the Ash'ari Crystal (10415), whose SmartAI sets TYPE_ZIGGURATx=2
//   ("cleared"). The tank won't walk into the just-opened door for that trash on
//   its own — so a CONDITIONAL event per ziggurat fires the instant the door
//   opens (GetData==1) and ClearRadius-clears the chamber; it reads false again
//   at state 2 and never re-fires. With all three at 2 the core opens the
//   gauntlet + slaughter gates to the Slaughter Square.
//
//   SLAUGHTERHOUSE. The Slaughter Square holds pre-spawned abominations (Bile
//   Spewer 10416 / Venom Belcher 10417 — trash, not bosses). Killing them all
//   summons Ramstein the Gorger (10439); Ramstein's death spawns 33 Mindless
//   Undead (11030, wave 1); clearing those spawns 5 Black Guards (10394, wave 2,
//   ~20s later); clearing those opens the door to Baron Rivendare (10440). The
//   Mindless charge the party, but the Black Guards walk to fixed posts and idle
//   (they only aggro by proximity), so wave 2 uses KillCreatureEngage to actively
//   SEEK and engage them — otherwise a wave-1 fight that drifted the party off down
//   the hall leaves the guards standing unaggroed and the Baron door never opens (a
//   centre ClearRadius can't see them and a MoveTo hold can't haul the tank back the
//   length of the hall). This whole chain is ONE
//   persistent anchored event (eventId 4) tied to a Slaughter-Square
//   objective anchor (BossRosterRegistry OBJ(1), ordered at key 12 so the tank
//   arrives after the ziggurats + Barthilas and before Baron). Persistent so
//   the many separate fights (each a >1s combat tick-gap that would otherwise
//   rewind the chain) don't reset progress.
//
// IMPORTANT — no instance-data gate for the slaughter phase. The instance
// exposes only TYPE_ZIGGURAT1/2/3 via GetData(); _slaughterProgress is private.
// So unlike ZulFarrak's temple (MoveToHoldUntilInstanceData) every slaughter
// gate here is a LIVE creature / GO-state read, which survives the combat gaps
// and is observable at any time. Each WaitForSpawn carries a finite timeout and
// the following kill/clear step no-ops cleanly when nothing is alive, so a
// fight-straight-through can never wedge the run.

namespace
{
    // --- live side: Dathrohan -> Balnazzar -------------------------------
    constexpr uint32 STR_DATHROHAN = 10812;  // Grand Crusader Dathrohan (spawned)
    constexpr uint32 STR_BALNAZZAR = 10813;  // Balnazzar (UpdateEntry, no spawn)
    constexpr float STR_DATH_SEEK = 60.0f;   // engage-seek radius from the anchor
    constexpr uint32 STR_DATH_TIMEOUT = 120000;  // 2 min per phase

    // --- ziggurat acolyte chambers ---------------------------------------
    constexpr uint32 STR_ACOLYTE = 10399;  // Thuzadin Acolyte (inside, documentary)

    // Ziggurat boss entries — used only for panel placement: each acolyte clear
    // sorts in the `dc bosses` panel just BEFORE the NEXT anchor in clear order
    // (PanelBeforeBoss), so it renders right after the boss whose death opened
    // its door instead of being dumped at the end as a generic off-path event.
    constexpr uint32 STR_NERUBENKAN = 10437;  // after Baroness's clear
    constexpr uint32 STR_MALEKI = 10438;      // after Nerub'enkan's clear

    // Mirror of instance_stratholme.cpp DataTypes (TYPE_ZIGGURAT1/2/3). The core
    // script header isn't reachable from the module, so the values are duplicated
    // here; they are stable vanilla instance-data ids.
    constexpr uint32 STR_TYPE_ZIGGURAT1 = 1;
    constexpr uint32 STR_TYPE_ZIGGURAT2 = 2;
    constexpr uint32 STR_TYPE_ZIGGURAT3 = 3;

    // Acolyte spawn-cluster centroids per ziggurat (creature.sql, map 329). Each
    // ClearRadius is anchored INSIDE the chamber; the conditional event's engage
    // pipeline drives the tank through the now-open door to clear it.
    constexpr float STR_ZIG1_X = 3847.4f, STR_ZIG1_Y = -3749.7f, STR_ZIG1_Z = 145.2f;  // Baroness
    constexpr float STR_ZIG2_X = 3838.8f, STR_ZIG2_Y = -3498.3f, STR_ZIG2_Z = 141.5f;  // Nerub'enkan
    constexpr float STR_ZIG3_X = 4059.0f, STR_ZIG3_Y = -3668.0f, STR_ZIG3_Z = 133.0f;  // Maleki

    constexpr float STR_ZIG_RADIUS = 18.0f;   // covers the inner chamber
    constexpr float STR_ZIG_ZBAND = 12.0f;    // keep the multi-level floors out

    // --- slaughterhouse --------------------------------------------------
    constexpr uint32 STR_BILE_SPEWER = 10416;    // pre-spawned abomination
    constexpr uint32 STR_VENOM_BELCHER = 10417;  // pre-spawned abomination
    constexpr uint32 STR_RAMSTEIN = 10439;     // summoned once the abominations die
    constexpr uint32 STR_MINDLESS = 11030;     // wave 1 (33x mindless undead)
    constexpr uint32 STR_BLACK_GUARD = 10394;  // wave 2 (5x black guard)
    // Active-seek radius for the abomination / Ramstein mop-up (issue #5). The
    // 13 abominations span ~68yd around the hall centre; the seek must cover the
    // whole hall from wherever the tank ended up mid-clear.
    constexpr float STR_ABOM_SEEK = 150.0f;

    // Monotonic phase doors (instance_stratholme.cpp). _slaughterProgress is NOT
    // exposed via GetData, but these doors are: each opens once and stays open, so
    // they are the bulletproof phase signal that survives the long continuous
    // combat (the event engine is dormant in combat) and bridges the multi-second
    // inter-wave lulls that would otherwise let a creature gate complete early.
    constexpr uint32 STR_GO_BARON_DOOR = 175796;   // opens when the 5 guards die
    constexpr uint32 STR_GO_OPEN = 0;              // GO_STATE_ACTIVE (open)
    // The Baron door sits ~51yd north of the hall centre; the default GO search is
    // only 20yd, so the wait needs an explicit reach that finds it from anywhere
    // in the hall.
    constexpr float STR_DOOR_SEARCH = 120.0f;

    // Slaughterhouse-hall centre. NOT the old SlaughterPos (4032,-3378): that sits
    // at the north end RIGHT AT the still-closed Baron door (175796 @ -3364) and
    // doors4 (175405 @ -3389), so the approach hit the door and the tank deadlocked
    // ("closed door blocking path"). This centre is pulled ~37yd SOUTH into the
    // abomination field (they span Y -3380..-3444), reachable from the south
    // entrance gate (175373 @ -3469) without crossing any closed door. The radius
    // covers the whole hall; the zBand keeps the ramps/Baron landing out.
    constexpr float STR_SLAUGHTER_X = 4032.0f;
    constexpr float STR_SLAUGHTER_Y = -3415.0f;
    constexpr float STR_SLAUGHTER_Z = 118.0f;
    // Covers the whole hall with margin (farthest abomination spawn ~68yd) so a
    // patrolling straggler can't drift outside and false-complete the gate while
    // it's still alive (the undead wave spawns only once EVERY abomination dies,
    // so a missed one would deadlock the next step). Bosses (Baron) are excluded
    // from ClearRadius, and the gauntlet behind is already cleared, so a wide
    // radius is safe.
    constexpr float STR_SLAUGHTER_RADIUS = 80.0f;
    constexpr float STR_SLAUGHTER_ZBAND = 15.0f;

    // The whole slaughter chain runs minutes through continuous combat, so every
    // step gets a generous wall-clock timeout (the step timer is not paused in
    // combat — cf. ZulFarrak's 15-min wave step) to keep a long fight from
    // escalating to a stall.
    constexpr uint32 STR_PHASE_TIMEOUT = 300000;   // 5 min per phase
    constexpr uint32 STR_WAVE_GAP_TIMEOUT = 120000;  // 2 min to bridge a wave gap

    // DUE while a ziggurat boss is dead (door open, GetData==1) but the chamber
    // is not yet cleared (GetData==2). Reads false at 0 (boss still alive, door
    // shut) and at 2 (acolytes dead), so the event fires exactly in the
    // "acolytes still up" window and never re-fires — the instance state is the
    // latch, no ConditionalLatchKey needed.
    bool ZigguratAcolytes(Player* bot, uint32 dataType)
    {
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        return inst && inst->GetData(dataType) == 1;
    }

    bool StrZig1(Player* bot, AiObjectContext* /*context*/)
    {
        return ZigguratAcolytes(bot, STR_TYPE_ZIGGURAT1);
    }

    bool StrZig2(Player* bot, AiObjectContext* /*context*/)
    {
        return ZigguratAcolytes(bot, STR_TYPE_ZIGGURAT2);
    }

    bool StrZig3(Player* bot, AiObjectContext* /*context*/)
    {
        return ZigguratAcolytes(bot, STR_TYPE_ZIGGURAT3);
    }

    // --- Timmy the Cruel (live side, Crusaders' Square) ------------------
    // Timmy (10808) is a STATIC spawn his SmartAI keeps INVISIBLE + passive
    // (faction 35) until no Crimson trash — Crimson Guardsman 10418 / Crimson
    // Conjuror 10419 / Crimson Gallant 10424 — remains within 40yd (conditions.sql
    // source_type 22, condition 29). Only then does he flip visible + hostile and
    // yell "TIMMY!". The dungeon-clear tank, walking straight to his boss anchor,
    // reached his spot before the pack was cleared and stalled on an un-engageable
    // boss (issue #5). A conditional ClearRadius — same shape as the ziggurat
    // acolyte clears — drives the tank to clear his pack first, then he activates.
    constexpr uint32 STR_TIMMY = 10808;
    constexpr uint32 STR_CRIMSON_GUARDSMAN = 10418;
    constexpr uint32 STR_CRIMSON_CONJUROR = 10419;
    constexpr uint32 STR_CRIMSON_GALLANT = 10424;
    constexpr float STR_TIMMY_X = 3651.62f;
    constexpr float STR_TIMMY_Y = -3190.08f;
    constexpr float STR_TIMMY_Z = 126.96f;
    constexpr float STR_TIMMY_TEST_RADIUS = 40.0f;   // matches the SmartAI gate
    constexpr float STR_TIMMY_CLEAR_RADIUS = 44.0f;  // clear a touch wider than tested
    constexpr float STR_TIMMY_ZBAND = 12.0f;
    // Only ARM the event once the tank is in Timmy's corner of the live side.
    // 60yd is well outside the boss engage range (so the pre-clear owns the
    // approach before the tank tries to pull passive Timmy) yet close enough that
    // the near edge of his pack is engage-reachable when it arms. Measured to his
    // static spawn, so it needs no loaded creature. WHY the gate exists: unlike
    // the ziggurat conditions (monotonic instance data that only turns true once
    // the tank is already at the chamber), this is a CREATURE-PRESENCE test that
    // reads true from the moment the map's grids load — i.e. with the tank still
    // at the instance entrance. A due conditional event drives immediately, and
    // the executor's ClearRadius step, evaluated from that far position, finds no
    // reachable hostile and reports Done — latching the event "complete" before
    // the tank has moved (the "Timmy room already cleared at the entrance" bug).
    // Proximity-gating the condition keeps it un-due until the tank is actually
    // there, exactly like the ziggurats.
    constexpr float STR_TIMMY_NEAR = 60.0f;

    // DUE while the tank is near Timmy AND he is still gated: any Crimson pack
    // member alive within 40yd of him. Reads false once they're cleared (he
    // activates), he's gone, or the tank is not yet in his corner — so the event
    // fires exactly in the "at Timmy, pack still up" window and latches on clear.
    bool StrTimmyGated(Player* bot, AiObjectContext* /*context*/)
    {
        if (!bot ||
            bot->GetDistance(STR_TIMMY_X, STR_TIMMY_Y, STR_TIMMY_Z) > STR_TIMMY_NEAR)
            return false;  // not in Timmy's corner yet — don't arm from afar
        Creature* timmy = DcTargeting::FindLiveCreatureOnMap(bot, STR_TIMMY);
        if (!timmy)
            return false;  // grid not streamed in yet, or Timmy already dead
        return timmy->FindNearestCreature(STR_CRIMSON_GUARDSMAN, STR_TIMMY_TEST_RADIUS, /*alive*/ true) ||
               timmy->FindNearestCreature(STR_CRIMSON_CONJUROR, STR_TIMMY_TEST_RADIUS, /*alive*/ true) ||
               timmy->FindNearestCreature(STR_CRIMSON_GALLANT, STR_TIMMY_TEST_RADIUS, /*alive*/ true);
    }
}

namespace
{
    bool StrZig1(Player* bot, AiObjectContext* context);
    bool StrZig2(Player* bot, AiObjectContext* context);
    bool StrZig3(Player* bot, AiObjectContext* context);
    bool StrTimmyGated(Player* bot, AiObjectContext* context);
}

void RegisterStratholmeEvents(std::vector<DungeonEvent>& out)
{
    // --- three ziggurat acolyte clears (conditional, fire on door-open) ----
    // ClearRadius (position-based) rather than KillCreature(10399): it drives
    // the tank into the chamber and clears whatever guards the crystal without
    // depending on the exact acolyte count, and the zBand keeps the chamber's
    // adjacent levels out of the clear. (void STR_ACOLYTE — documentary entry.)
    (void) STR_ACOLYTE;

    // --- live side: Grand Crusader Dathrohan -> Balnazzar (anchored, id 5) --
    // Anchored at the Dathrohan objective (BossRosterRegistry OBJ(2), bit 6).
    // Step 1 seeks + engages Dathrohan; at 40% he UpdateEntry's to Balnazzar
    // (same GUID, already in combat), so no live 10812 remains -> step Done.
    // Step 2 holds until Balnazzar himself is dead, then the objective latches.
    out.push_back(EventBuilder(329, 5, "Grand Crusader Dathrohan (Balnazzar)")
                      .Anchored(6)
                      .KillCreatureEngage(STR_DATHROHAN, /*count*/ 1, STR_DATH_SEEK)
                          .Timeout(STR_DATH_TIMEOUT)
                      .KillCreatureEngage(STR_BALNAZZAR, /*count*/ 1, STR_DATH_SEEK)
                          .Timeout(STR_DATH_TIMEOUT)
                      .Build());

    // --- Timmy the Cruel pre-clear (conditional, id 6) ---------------------
    // Fires while his Crimson pack is still up within 40yd; the ClearRadius drives
    // the tank to clear it so his SmartAI flips him visible + hostile, then the
    // boss anchor engages him. See the StrTimmyGated note.
    out.push_back(EventBuilder(329, 6, "Timmy the Cruel (clear his pack)")
                      .Conditional(&StrTimmyGated)
                      .PanelBeforeBoss(STR_TIMMY)
                      .ClearRadius(STR_TIMMY_X, STR_TIMMY_Y, STR_TIMMY_Z,
                                   STR_TIMMY_CLEAR_RADIUS, STR_TIMMY_ZBAND)
                      .Build());

    out.push_back(EventBuilder(329, 1, "Ziggurat 1 acolytes (Baroness)")
                      .Conditional(&StrZig1)
                      .PanelBeforeBoss(STR_NERUBENKAN)
                      .ClearRadius(STR_ZIG1_X, STR_ZIG1_Y, STR_ZIG1_Z,
                                   STR_ZIG_RADIUS, STR_ZIG_ZBAND)
                      .Build());

    out.push_back(EventBuilder(329, 2, "Ziggurat 2 acolytes (Nerub'enkan)")
                      .Conditional(&StrZig2)
                      .PanelBeforeBoss(STR_MALEKI)
                      .ClearRadius(STR_ZIG2_X, STR_ZIG2_Y, STR_ZIG2_Z,
                                   STR_ZIG_RADIUS, STR_ZIG_ZBAND)
                      .Build());

    out.push_back(EventBuilder(329, 3, "Ziggurat 3 acolytes (Maleki)")
                      .Conditional(&StrZig3)
                      // After Maleki's clear the next anchor is the Slaughterhouse
                      // objective; sort just before it (all three ziggurats must be
                      // cleared before that gate opens).
                      .PanelBeforeBoss(BossRosterRegistry::ObjectiveEntry(1))
                      .ClearRadius(STR_ZIG3_X, STR_ZIG3_Y, STR_ZIG3_Z,
                                   STR_ZIG_RADIUS, STR_ZIG_ZBAND)
                      .Build());

    // --- the slaughterhouse chain (persistent anchored, eventId 1) ---------
    // Anchored at the Slaughter-Square objective (BossRosterRegistry, OBJ(1),
    // encounterIndex 11). Boss-nav travels the tank in; the steps then run the
    // abomination -> Ramstein -> wave1 -> wave2 chain. Persistent so the kills
    // (each a >1s combat tick-gap) don't rewind the step list.
    out.push_back(EventBuilder(329, 4, "Slaughterhouse (Baron run)")
                      .Anchored(11)
                      .Persistent()
                      // 1. Clear the hall of the pre-spawned abominations (13x Bile
                      //    Spewer 10416 / Venom Belcher 10417). Ramstein is summoned
                      //    ONLY when EVERY abomination dies, so a single one left alive
                      //    wedges the run. The point-anchored ClearRadius is bot-centred
                      //    and reachability-filtered (NearestHostileNearPoint), so a few
                      //    far-corner abominations can be left behind — exactly the
                      //    "stragglers before Ramstein" report (issue #5).
                      .ClearRadius(STR_SLAUGHTER_X, STR_SLAUGHTER_Y, STR_SLAUGHTER_Z,
                                   STR_SLAUGHTER_RADIUS, STR_SLAUGHTER_ZBAND)
                          .Timeout(STR_PHASE_TIMEOUT)
                      // 1b/1c. Mop up any straggler abominations the centre clear
                      //    couldn't reach: KillCreatureEngage actively SEEKS each entry
                      //    via the engage pipeline (long-range walk-in), same fix as the
                      //    Black Guards below. Each no-ops instantly when none of its
                      //    entry is alive (the kill gate reports Done when the scan finds
                      //    nothing), so when the centre clear already took them all these
                      //    pass through in one tick. Both entries are covered because the
                      //    last-to-die (which triggers Ramstein) can be either type.
                      .KillCreatureEngage(STR_BILE_SPEWER, /*count*/ 1, STR_ABOM_SEEK)
                          .Timeout(STR_PHASE_TIMEOUT)
                      .KillCreatureEngage(STR_VENOM_BELCHER, /*count*/ 1, STR_ABOM_SEEK)
                          .Timeout(STR_PHASE_TIMEOUT)
                      // 1d. All abominations dead -> Ramstein the Gorger (10439) is now
                      //    summoned at SlaughterPos. Seek + kill him explicitly rather
                      //    than relying on the centre clear still running when he spawns
                      //    (it may have completed on the mop-up steps above). His death
                      //    starts wave 1. He has no creature.sql spawn / boss slot, so
                      //    KillCreatureEngage owns his kill-bit flip.
                      .KillCreatureEngage(STR_RAMSTEIN, /*count*/ 1, STR_ABOM_SEEK)
                          .Timeout(STR_PHASE_TIMEOUT)
                      // 2. Wave 1: Ramstein's death spawns 33 Mindless Undead (11030)
                      //    after a ~5s lull. Wait for the first to appear (a genuine
                      //    out-of-combat lull, so the spawn can't be missed), then
                      //    ClearRadius them — they charge the hall, so the radius +
                      //    reactive combat reap them; count-tolerant (TEMPSUMMON).
                      .WaitForSpawn(STR_MINDLESS, /*alive*/ true)
                          .Timeout(STR_WAVE_GAP_TIMEOUT)
                      .ClearRadius(STR_SLAUGHTER_X, STR_SLAUGHTER_Y, STR_SLAUGHTER_Z,
                                   STR_SLAUGHTER_RADIUS, STR_SLAUGHTER_ZBAND)
                          .Timeout(STR_PHASE_TIMEOUT)
                      // 3. Wave 2: ~20s after the undead die, 5 Black Guards (10394)
                      //    spawn at the north door, WALK to fixed posts ~8yd north of
                      //    the hall centre, then idle there (SetHomePosition). Unlike the
                      //    charging Mindless they never seek the party — they only aggro
                      //    by proximity, and their deaths are what open the Baron door.
                      //    If the wave-1 fight dragged the party off down the hall, the
                      //    guards reach their posts and stand there unaggroed and the run
                      //    wedges. A point-anchored ClearRadius can't recover this: its
                      //    engage scan is bot-centred (so far-off guards are invisible)
                      //    and a MoveTo hold can't haul the tank the length of the hall
                      //    (intra-room hop only — it just sat off-position till timeout).
                      //    So WaitForSpawn first (the kill gate can't false-complete
                      //    before they appear), then KillCreatureEngage: the engage
                      //    pipeline (EngageDirect, long-range walk-in) actively SEEKS the
                      //    guards wherever the party ended up and fights through all five.
                      //    Same mechanism ZulFarrak uses to walk the tank to its bosses.
                      .WaitForSpawn(STR_BLACK_GUARD, /*alive*/ true)
                          .Timeout(STR_WAVE_GAP_TIMEOUT)
                      .KillCreatureEngage(STR_BLACK_GUARD, /*count*/ 1, /*searchRadius*/ 250.0f)
                          .Timeout(STR_PHASE_TIMEOUT)
                      // 4. The guards' death opens the Baron door (175796). Gate on
                      //    that monotonic, combat-gap-proof signal — the authoritative
                      //    "slaughterhouse done" — not a transient creature check.
                      //    Baron (DBC bit 12) is then a normal boss anchor.
                      .WaitForGOState(STR_GO_BARON_DOOR, STR_GO_OPEN,
                                      /*timeoutMs*/ STR_WAVE_GAP_TIMEOUT,
                                      /*searchRadius*/ STR_DOOR_SEARCH)
                      .Build());
}


// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterStratholmeRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- Stratholme (map 329) — the Slaughterhouse / Baron run --------
    // Ramstein the Gorger (DBC bit 11) is SUMMONED once the Slaughter
    // Square abominations die — he has no creature.sql spawn, so
    // BossSpawnIndex (which walks spawns) never lists him and the tank
    // can't anchor on him. Add ONE objective at the Slaughter Square
    // (SlaughterPos from instance_stratholme.cpp), ordered at key 12
    // (after the ziggurats + Barthilas, before Baron 13), carrying the
    // persistent slaughter event (eventId 4): clear the abominations ->
    // Ramstein -> 33 Mindless Undead -> 5 Black Guards, which opens Baron's
    // door. The event's KillCreatureEngage(Ramstein) flips his real bit when
    // he dies (objectives skip the completion-mask check, so the order key is
    // a pure ordering hint that can't collide).
    //
    // The three ziggurat bosses and Baron need NO remove/re-add — they have
    // static spawns and real bits and are pulled normally; their in-ziggurat
    // acolyte follow-ups are conditional events (1/2/3). They ARE reordered in
    // place (p.reorder) onto the shared 1..13 scale below.
    //
    // ORDER (issue #5). The full explicit clear path is stamped in p.reorder +
    // the objective/Barthilas orderOverrides below. The two structural fixes
    // it encodes: (a) the DBC bits put the ziggurats (Baroness 7, Nerub'enkan
    // 8, Maleki 9) BEFORE Magistrate Barthilas (10), but the dead-side path
    // runs Barthilas FIRST (he flees the entrance to warn the Baron), then the
    // ziggurats, then the slaughterhouse, then Baron; (b) the DBC lists The
    // Unforgiven / Hearthsinger Forresten first (bits 0/1), which forced a full
    // circle before the live side — they now clear after it. Every reorder
    // keeps each boss's real kill-bit (encounterIndex) untouched; only the
    // travel order moves.
    //
    // Both the live (Scarlet) and dead (Undead) sides stay in the list so
    // one run does both. Barthilas is remove+re-added (not p.reorder) because
    // his spawn coords are hand-authored to his static creature.sql spawn (he
    // relocates at run-time, but the anchor and engage pipeline handle his
    // flee); completionFrom = his own entry keeps kill-bit 10. See
    // StratholmeEvents.
    //
    // LIVE-SIDE BALNAZZAR. The Balnazzar encounter (DBC bit 6) credits
    // creature 10813 (Balnazzar), which has NO creature.sql spawn — it
    // exists only via Grand Crusader Dathrohan's (10812) on-aggro SmartAI,
    // which at <=40% HP casts Balnazzar Transform and UpdateEntry's the
    // SAME creature 10812 -> 10813. So BossSpawnIndex (which matches credit
    // entries to spawns) drops the encounter and the clear jumps straight
    // from Archivist Galford (bit 5) to the dead side — the live boss is
    // never fought. Same class as Ramstein. Fix: an OBJECTIVE at Dathrohan's
    // static spawn, ordered at key 5 (right after Galford at 4), carrying
    // event 5 (KillCreatureEngage 10812 -> 10813). Completion is the event
    // ("Balnazzar dead"), not the mask, so the objective's order key carries
    // no real kill-bit and can't collide with the real bit-6 the encounter
    // sets. See StratholmeEvents.
    {
        BossRosterPatch p;
        p.mapId = 329;
        p.remove = { 10435 };  // Barthilas — re-added below, reordered

        // Full explicit clear-path order (issue #5). orderOverride keys on a
        // contiguous 1..13 scale shared with the two objectives + Barthilas; each
        // auto-derived boss keeps its real DBC kill-bit (encounterIndex) untouched,
        // so completion detection is unaffected — only the travel order moves. The
        // two moved bosses are The Unforgiven (10516) and Hearthsinger Forresten
        // (10558): the DBC lists them first (bits 0/1, ahead of Timmy's 2), which
        // sent the tank on a full circle to Unforgiven -> Hearthsinger -> Timmy
        // before it ever reached the live side. They now clear AFTER the live side
        // (keys 6/7), on the way to the dead side.
        //
        //   1  Timmy the Cruel        (10808, DBC bit 2)  — first, live side
        //   2  Malor the Zealous      (11032, bit 4)
        //   3  Cannon Master Willey   (10997, bit 3)
        //   4  Archivist Galford      (10811, bit 5)
        //   5  Dathrohan/Balnazzar    (OBJ(2), event 5)   — live-side final
        //   6  The Unforgiven         (10516, bit 0)      — moved here
        //   7  Hearthsinger Forresten (10558, bit 1)      — moved here
        //   8  Magistrate Barthilas   (10435, bit 10)     — dead side begins
        //   9  Baroness Anastari      (10436, bit 7)
        //   10 Nerub'enkan            (10437, bit 8)
        //   11 Maleki the Pallid      (10438, bit 9)
        //   12 Slaughterhouse         (OBJ(1), event 4)
        //   13 Baron Rivendare        (10440, bit 12)
        p.reorder = {
            { 10808, 1 },   // Timmy the Cruel
            { 11032, 2 },   // Malor the Zealous     — before the Cannon Master
            { 10997, 3 },   // Cannon Master Willey
            { 10811, 4 },   // Archivist Galford
            { 10516, 6 },   // The Unforgiven         — after the live side
            { 10558, 7 },   // Hearthsinger Forresten — after the live side
            { 10436, 9 },   // Baroness Anastari
            { 10437, 10 },  // Nerub'enkan
            { 10438, 11 },  // Maleki the Pallid
            { 10440, 13 },  // Baron Rivendare
        };

        p.add = {
            MakeBoss(10435, 329, "Magistrate Barthilas",
                     3663.23f, -3619.14f, 137.98f,
                     /*completionFrom*/ 10435, /*orderOverride*/ 8),
            // Grand Crusader Dathrohan -> Balnazzar (live side). Objective
            // at his static spawn (creature.sql, map 329); event 5 seeks +
            // engages him and finishes the transformed Balnazzar. orderOverride
            // 5 = live-side final, just after Galford (4). See the LIVE-SIDE
            // BALNAZZAR note.
            MakeObjective(OBJ(2), /*encounterIndex*/ 6, 329,
                          "Grand Crusader Dathrohan",
                          3415.8f, -3044.5f, 136.8f, /*arriveRadius*/ 30.0f,
                          /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 5,
                          /*orderOverride*/ 5),
            // Anchor pulled ~37yd SOUTH of SlaughterPos into the abomination
            // hall: SlaughterPos (4032,-3378) sits at the still-closed Baron
            // door (175796 @ -3364), so the approach hit the door and stalled
            // ("closed door blocking path"). This spot is in the open hall,
            // reachable from the south entrance gate without crossing a closed
            // door; the event's ClearRadius (r80) covers the whole hall.
            // orderOverride 12 = after the ziggurats (11), before Baron (13).
            MakeObjective(OBJ(1), /*encounterIndex*/ 11, 329,
                          "Slaughterhouse (Baron run)",
                          4032.0f, -3415.0f, 118.0f, /*arriveRadius*/ 30.0f,
                          /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 4,
                          /*orderOverride*/ 12),
        };
        t.push_back(std::move(p));
    }
}
