/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- Sethekk Halls (map 556) — force-summon & kill Anzu (Raven God) ---------
//
// Anzu is an OPTIONAL bonus boss with NO static spawn. In normal play he is
// summoned only by a DRUID using the Essence-Infused Moonstone (item 32449,
// heroic quest 11001) at the ante-chamber before Talon King Ikiss. Verified
// from instance_sethekk_halls.cpp / boss_anzu.cpp / smart_scripts:
//
//   * The moonstone casts a spell whose effect is SPELL_EFFECT_SEND_EVENT with
//     MiscValue 14797. The core then calls instance->ProcessEvent(target,14797).
//   * instance_sethekk_halls::ProcessEvent has NO IsHeroic()/difficulty gate:
//         if (eventId == 14797)
//             if (!GetCreature(DATA_VOICE_OF_THE_RAVEN_GOD) && GetBossState(DATA_ANZU) != DONE)
//                 instance->SummonCreature(NPC_VOICE_OF_THE_RAVEN_GOD, (-88.02,288.18,75.2));
//     At the CORE level "heroic-only" is enforced purely by item/quest
//     availability, NOT by the encounter, so a direct ProcessEvent(14797) would
//     summon Anzu on any difficulty. WE restore the blizzlike gate ourselves:
//     the event carries .HeroicOnly() (and its roster anchor gate HeroicOnly), so
//     the DriveAnzuSummon hook (id 7) only ever pokes send-event 14797 on a
//     HEROIC run — no druid, no item, no quest, no heroic key needed there. On a
//     normal run the event never fires and Anzu never surfaces. The Voice's
//     SmartAI (rows 24769-24770, event_flags 513/512 — NO DIFFICULTY_0..3 bits
//     set) would run on all difficulties, but we simply never trigger it on
//     normal.
//   * Voice (21851, TempSummon) runs action list 2185100: ~40s of theatrics
//     (the bulk is a 24s camera-shake at id6), then id9 summons Anzu (23035) at
//     (-87.61,287.84,26.5), then despawns the portals and itself.
//   * Anzu (23035, TempSummon, boss_anzu : BossAI(DATA_ANZU)) spawns
//     NON_ATTACKABLE + Shadowform; its ~16s intro then clears the flags and
//     calls SetInCombatWithZone() — which force-pulls the whole party into the
//     fight with no explicit pull needed. So ~56s poke -> fightable Anzu.
//
// WHY THE ARCATRAZ (Skyriss) SHAPE: Anzu is a TempSummon, so the spawn-store
// scans behind BossSpawnIndex/FindLiveCreatureOnMap cannot see him — adding him
// as a boss row would deadlock the engage gate on live==0. He is therefore NOT
// a boss row; the event reaches him with FindNearestCreature (a grid scan that
// DOES see TempSummons), and the roster carries only an Objective anchor for
// ordering. Cf. ArcatrazEvents.cpp (Harbinger Skyriss) and the Hellfire /
// Vazruden pattern.
//
// PRE-CLEAR: the room is swept trash-free (ClearRadius) BEFORE the poke. Anzu
// SetInCombatWithZone()s the instant it goes live, so any Sethekk trash left
// standing in the ante-chamber would pile straight into the boss fight. That
// sweep is the ONLY rung that clears the far half of this room: the ordinary
// trash rung (DungeonClearBlockingTrashTrigger) scans a CORRIDOR along the path
// to the next anchor, so it only ever touches what is between the party and the
// statue — everything BEYOND the statue is the sweep's job, and once the tank is
// standing on the anchor the corridor rung finds nothing at all (and the
// objective action outranks it anyway). See SH_ARRIVE_RADIUS / SH_CLEAR_RADIUS
// for the three ways that combination went wrong on live heroic runs.
//
// WHY Persistent + Optional:
//   * Persistent — the ~40s summon and the fight span several combat/non-combat
//     Drive gaps; without it the executor rewinds stepIndex to 0 on every gap
//     and re-pokes forever.
//   * Optional — Anzu is a BONUS boss; Ikiss is the real objective. A summon
//     that fails or a fight that times out must skip straight to Ikiss rather
//     than stall the clear. (This is the one departure from Arcatraz, where
//     Mellichar IS the final boss and is not optional.)
//
// WIPE: BossAI::_Reset despawns Anzu on a wipe. As with Skyriss, the
// KillCreatureEngage gate then reads "no live Anzu" and could latch complete
// without the kill — but the encounter bit is not set, so a corpse-run back
// re-drives the objective, the hook re-pokes (no Voice / no Anzu / not DONE),
// and Anzu re-summons. Optional means even a persistent failure just advances
// to Ikiss. Acceptable.

namespace
{
    constexpr uint32 SH_MAP            = 556;
    constexpr uint32 SH_ANZU           = 23035;  // TempSummon boss (grid-search only)
    constexpr uint32 SH_ANZU_ENCOUNTER = 1;      // DATA_ANZU — sorts Syth(0) < Anzu < Ikiss(2)
    constexpr uint32 SH_ANZU_SUMMON_HOOK = 7;    // ObjectiveHookRegistry id (DriveAnzuSummon)

    // The ante-chamber floor where Anzu lands and the fight happens; the party
    // parks here to summon. Anzu itself SetInCombatWithZone()s once it's live,
    // so exact placement only matters for being present when the theatrics fire.
    constexpr float SH_CHAMBER_X = -88.0f;
    constexpr float SH_CHAMBER_Y = 288.0f;
    constexpr float SH_CHAMBER_Z = 26.5f;

    // The room MUST be trash-free before the summon: once Anzu goes live it
    // SetInCombatWithZone()s, so any surviving Sethekk trash would pile into the
    // boss fight — and a live-fire heroic run showed worse, with the summoned
    // Anzu chasing the party the ~80yd back down the hall and then a further
    // 120yd into Ikiss's chamber, so the party fought Anzu, Ikiss and the
    // uncleared trash at once.
    //
    // GEOMETRY (measured off the map-556 `creature` spawn table; note WoW's
    // compass — +X is NORTH, +Y is WEST, so this hall runs SOUTH->NORTH, not
    // east-west). The ante-chamber is ONE open room — no doors anywhere in it —
    // running from the south doorway trio (Ravenguards 18322 + Cobalt Serpent
    // 19428, x ~-141) all the way to the north landing at the mouth of Ikiss's
    // chamber (Avian Warhawk 21904 / Talon Lord 18321 / Shadowmage 18320 /
    // Shaman 18326 / Prophet 18325, x ~-15..-1). All of it is one flat floor at
    // z ~26-27.5. So the room is ~140yd long and the SUMMON STATUE IS NOT ITS
    // CENTRE: it sits 53yd from the south end and 87yd from the north end.
    //
    // That asymmetry is what the previous radius got wrong. 60yd from the statue
    // covers everything from the south doorway up to the north-east Talon Lord
    // (43.3yd) but STOPS 13yd SHORT of the north-landing pack, which the old
    // note dismissed as "Ikiss-corridor packs at 73-87yd". They are not in
    // Ikiss's corridor: Ikiss stands at (44.7,287), a further 45-60yd on, with
    // nothing between. They are the last five elites of this room, and the sweep
    // certified the room clear with all five standing.
    //   Live evidence, heroic run tr-20260726-112544-3 (tank Zeeron): the gate
    //   certified from botDistToCentre=0.0 (so the S1276 vantage fix works and
    //   everything inside 60yd really was dead), the summon fired, and the party
    //   only met that pack after Anzu died, on the walk to Ikiss. Run ...-4 shows
    //   the same shape as a post-Anzu pull of entry 21904 with 5 observed.
    //
    // So the sweep is CENTRED ON THE ROOM, not on the statue. From the room's
    // midpoint both ends sit at ~70yd (south Ravenguard 70.3, north Prophet
    // 70.4) and the next hostile in either direction is a long way out: the
    // Time-Lost Scryer on the Syth approach at 99.7yd (behind the party, dead on
    // the way in) and Ikiss at 116yd. Radius 80 therefore covers the whole room
    // with ~10yd of margin — the north Warhawk (guid 138757) and the NE Talon
    // Lord (138660) both PATROL — while leaving ~20yd of dead space before it
    // could reach anything that is not this room's problem.
    //
    // The centre being ~16yd off the statue is deliberate and costs one hop: the
    // executor refuses to certify a ClearRadius from further out than
    // DC_EVENT_CLEAR_JUDGE_RADIUS, so it walks the tank to the centre first. That
    // is the point — judged from the middle of the room, the strict per-candidate
    // reachability probe only ever has to reach ~70yd, instead of the ~87yd it
    // would need from the statue. Step 3 walks the tank back to the statue before
    // the poke.
    //
    // zBand 12 (z 14.7..38.7) keeps the clear on the floor and OFF the Avian
    // Flyers 21931 (z 41-76) / Invis Raven God ring 23057-23058 (z 49-84) —
    // unreachable, and the triggers aren't hostile anyway — so ClearRadius can't
    // stall chasing a flyer.
    constexpr float SH_CLEAR_X      = -71.5f;
    constexpr float SH_CLEAR_Y      = 288.0f;
    constexpr float SH_CLEAR_Z      = 26.7f;
    constexpr float SH_CLEAR_RADIUS = 80.0f;
    constexpr float SH_CLEAR_ZBAND  = 12.0f;

    // The sweep needs a MUCH longer budget than the 30s EventStepTimeout default.
    // A step that times out is Failed, and because this event is Optional, Failed
    // means the executor skips the REST of the event — the summon included. With
    // the room genuinely uncleared the sweep has to walk the length of the hall
    // and fight what it finds, which is minutes, not seconds; on the default it
    // would silently drop Anzu exactly on the runs where the pre-clear matters
    // most. 5 minutes is generous enough to cover a full-room sweep with a wipe
    // recovery in it, and still bounded so a sweep that can never finish (an
    // unreachable straggler inside the volume) gives up rather than hanging.
    constexpr uint32 SH_CLEAR_TIMEOUT = 300000;

    // Arrive radius: SMALL, and deliberately NOT tied to the ClearRadius.
    //
    // The old 55 came from "the arrive radius must be >= the ClearRadius, or
    // boss-nav decides the tank has left the anchor and hauls it back". That is
    // not true for a PERSISTENT anchored event: once its first step completes,
    // DungeonEventExecutor::IsPersistentAnchoredEventActive latches the
    // at-objective trigger ON regardless of distance, precisely so the tank can
    // roam far from the anchor while the event drives it. Step 0 here is a
    // MoveTo with an 8yd radius, so the latch engages the moment the tank parks
    // on the anchor and the sweep is free to range the full clear radius — which
    // it needs, since the sweep's centre is itself 16yd off this anchor.
    //
    // What 55 DID buy was the bug: DcRel::AtObjective (30) outranks
    // DcRel::BlockingTrash (25), so crossing the arrive radius hands the tick to
    // the objective action, which StopBot(Hold)s and starts driving steps. At 55
    // that happened a whole room-length out, at the hall's south doorway —
    // killing the ordinary corridor trash-clear for the entire approach and
    // letting the pre-clear be judged (and the summon poked) from ~80yd away.
    // 12 keeps the corridor clear and the pull pipeline in charge of the walk in,
    // exactly as they are for every other anchor, and hands over only once the
    // tank is genuinely at the statue. It sits just above step 0's 8yd MoveTo
    // radius so arrival and the first step agree.
    constexpr float SH_ARRIVE_RADIUS = 12.0f;

    // Poke -> Anzu-spawn is ~40s of theatrics; 120s covers it with margin for a
    // post-wipe re-poke without hanging a genuinely broken summon (Optional then
    // skips to Ikiss).
    constexpr uint32 SH_SUMMON_TIMEOUT = 120000;
    // The fight itself — generous but bounded so a wedged Anzu surfaces to the
    // human rather than holding the (Optional) event open indefinitely.
    constexpr uint32 SH_KILL_TIMEOUT = 180000;
}

void RegisterSethekkHallsEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(SH_MAP, /*eventId*/ 1, "Anzu (forced Raven God summon)")
                      .Anchored(/*orderIndex (doc)*/ SH_ANZU_ENCOUNTER)
                      .HeroicOnly()  // Anzu is a HEROIC-ONLY bonus boss (real WoW gates
                                     // him via the heroic quest/moonstone). Never poke
                                     // send-event 14797 on a normal run.
                      .Optional()    // bonus boss — never stall the clear to Ikiss on it
                      .Persistent()  // survives the combat/non-combat Drive gaps
                      // 1. Settle on the chamber floor before the ~40s theatrics.
                      .MoveTo(SH_CHAMBER_X, SH_CHAMBER_Y, SH_CHAMBER_Z, /*radius*/ 8.0f)
                      // 2. CLEAR THE WHOLE ROOM FIRST. Anzu's SetInCombatWithZone
                      //    would otherwise drag any surviving trash into the boss
                      //    fight. Position-based (any reachable hostile in the
                      //    band), so it engages whatever the walk-in left standing
                      //    — including the north-landing pack the pull pipeline
                      //    never reaches, because the tank stops at the statue.
                      //    Centred on the ROOM, not the statue: the executor
                      //    refuses to certify from further out than
                      //    DC_EVENT_CLEAR_JUDGE_RADIUS, so it walks the tank to
                      //    the centre and every reachability probe is a ~70yd
                      //    room half rather than an 87yd room length.
                      .ClearRadius(SH_CLEAR_X, SH_CLEAR_Y, SH_CLEAR_Z,
                                   SH_CLEAR_RADIUS, SH_CLEAR_ZBAND)
                          .Timeout(SH_CLEAR_TIMEOUT)
                      // 3. Re-settle on the statue. The sweep leaves the tank at
                      //    the room's centre (or wherever the last straggler
                      //    died); Anzu lands at the statue, so gather the party
                      //    back on it before the theatrics start rather than
                      //    letting the ~40s cinematic run with the party spread
                      //    down the hall.
                      .MoveTo(SH_CHAMBER_X, SH_CHAMBER_Y, SH_CHAMBER_Z, /*radius*/ 8.0f)
                      // 4. Fire send-event 14797 and hold through the theatrics
                      //    until Anzu is on the field. The hook stops poking the
                      //    instant Anzu exists, so it never spawns a second Voice.
                      .Custom(SH_ANZU_SUMMON_HOOK)
                          .Timeout(SH_SUMMON_TIMEOUT)
                      // 5. Engage & kill. FindNearestCreature sees the TempSummon;
                      //    Anzu's own SetInCombatWithZone pulls the party in.
                      .KillCreatureEngage(SH_ANZU, /*count*/ 1, /*searchRadius*/ 100.0f)
                          .Timeout(SH_KILL_TIMEOUT)
                      .Build());
}

void RegisterSethekkHallsRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = SH_MAP;
    // Anzu is heroic-only (see the event's .HeroicOnly() gate); keep his ordering
    // anchor off the normal-mode roster too so nothing surfaces on a normal run.
    p.gate = DcDifficultyGate::HeroicOnly;
    p.add = {
        // Objective anchor only — Anzu is NOT added as a boss row (TempSummon,
        // invisible to BossSpawnIndex). encounterIndex = DATA_ANZU (1) sorts the
        // anchor after Syth (0) and before Ikiss (2), so boss-nav delivers the
        // tank to the chamber before it heads on to Ikiss. eventId 1 links this
        // anchor to the event row above.
        MakeObjective(OBJ(1), SH_ANZU_ENCOUNTER, SH_MAP,
                      "Anzu (forced Raven God summon)",
                      SH_CHAMBER_X, SH_CHAMBER_Y, SH_CHAMBER_Z,
                      /*arriveRadius*/ SH_ARRIVE_RADIUS, /*gateEntry*/ 0,
                      /*hook*/ 0, /*eventId*/ 1),
    };
    t.push_back(std::move(p));
}
