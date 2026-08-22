/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "Creature.h"
#include "Player.h"
#include "Playerbots.h"

// --- ZulFarrak (map 209) — the TEMPLE (pyramid) event --------------------
// The scripted set-piece between Witch Doctor Zum'rah and the final boss Chief
// Ukorz Sandscalp. Verified from the core scripts (instance_zulfarrak.cpp /
// zulfarrak.cpp): the door to Ukorz stays sealed until the party plays this out,
// so without it the tank bee-lines to Ukorz, hits the closed door and stalls.
//
// The event is ONE persistent anchored step list (eventId 1) tied to the temple
// objective anchor (BossRosterRegistry, at the Sandfury Executioner's spot atop
// the great staircase, ordered at Ukorz's bit 7 so the tank goes up FIRST). It is
// Persistent + the at-objective trigger is sticky, so the SAME progress survives
// the many wave/boss fights (each a >1s gap on the combat engine that would
// otherwise rewind the chain) and the tank may roam far from the anchor — down
// the stairs to the bosses and back to the NPCs — while the event drives.
//
// The scripted phases (instance DATA_PYRAMID), all driven by the steps below:
//   1. Kill the Sandfury Executioner (7274) guarding the summit, then open a
//      Troll Cage (GO 141070-141074). Using one cage (go->Use cheats the lock —
//      no key needed) fires go_troll_cageAI::GossipHello, which frees Bly's band
//      (passive in cages) and starts the assault.
//   2. The cages / executioner sit BACK from the staircase, but the waves charge
//      UP the ramp and hit the freed NPC crew (who stage at the ramp head) first —
//      if the party loiters at the summit it never aggros the waves and the NPCs
//      die. So we move forward onto the ramp head (the crew's staging line,
//      verified from initBlyCrewMember) and hold there to tank the waves off the
//      NPCs.
//   3. Wave 3 spawns at the BOTTOM of the stairs together with the temple bosses
//      Nekrum Gutchewer (7796) and Shadowpriest Sezz'ziz (7275); the NPC crew
//      moves down to engage. We descend (engage steps drive the long walk-in) and
//      help kill them.
//   4. With the trolls dead the crew settles at the bottom. We talk to the goblin
//      Weegli Blastfuse (7607) FIRST — his gossip sends him to blow open the door
//      to Ukorz — then dwell ~10s before provoking Bly. (Per live experience: a
//      ~10s gap is enough; Bly's faction-flip of the crew only fires ~10s after
//      HIS gossip, by which point Weegli is well clear, so the door still opens. A
//      full wait-for-Weegli-despawn is unnecessary.)
//   5. Then talk to Sergeant Bly (7604, human) — his band turns hostile; killing
//      Bly ends the event (DATA_PYRAMID = DONE, door to Ukorz open). The clear
//      then latches the objective and proceeds to Chief Ukorz naturally.
//
// Gossip uses the executor's opcode-forgery path (proven on Shadowfang Keep), and
// the engage steps reuse the normal engage pipeline (long-range path + combat +
// follower assist) so the party fights alongside the NPC helpers.

namespace
{
    constexpr uint32 ZF_EXECUTIONER = 7274;
    // Any one cage starts the event; 141073 sits ~9yd off the objective anchor.
    constexpr uint32 ZF_TROLL_CAGE = 141073;
    constexpr uint32 ZF_NEKRUM = 7796;   // temple boss (wave 3, no static spawn)
    constexpr uint32 ZF_SEZZIZ = 7275;   // temple boss (wave 3, no static spawn)
    constexpr uint32 ZF_WEEGLI = 7607;   // goblin — blows the door to Ukorz
    constexpr uint32 ZF_BLY = 7604;      // human — final fight ends the event

    // Where to hold during the waves: PARTWAY DOWN the ramp, clearly IN FRONT of
    // Bly's crew (they stage at the head, ~y1263 z41.5), facing the waves coming
    // up the stairs. This is essential — the party does NOT auto-assist the NPCs,
    // so unless a party member takes the first aggro nobody engages and the crew
    // dies. Sitting down-ramp of the crew puts the tank in the waves' path so it
    // grabs them first. Coords interpolate the staircase (head y1263/z41.5 ->
    // bottom y1228/z~10, verified from initBlyCrewMember + the PRE_WAVE_3 NPC
    // moves); a generous arrive radius tolerates the per-step z of the stairs.
    constexpr float ZF_RAMP_X = 1886.0f;
    constexpr float ZF_RAMP_Y = 1250.0f;
    constexpr float ZF_RAMP_Z = 30.0f;

    // The wave sequence (cages open -> wave 3 / bosses spawn) runs several
    // minutes; a generous timeout keeps the long survive-the-waves wait from
    // escalating to a stall. The NPC gossips / door-blow likewise take a while.
    constexpr uint32 ZF_WAVES_TIMEOUT = 900000;  // 15 min
    constexpr uint32 ZF_NPC_TIMEOUT = 180000;    // 3 min

    // Instance phase gate for the ramp garrison. DATA_PYRAMID (instance GetData
    // type 0) climbs NOT_STARTED(0) -> CAGES_OPEN(1) -> ... -> WAVE_3(7) ->
    // KILLED_ALL_TROLLS(8) -> MOVED_DOWNSTAIRS(9) -> ... -> DONE(12). We garrison
    // the ramp until it reaches WAVE_3 (bosses spawned at the bottom), then
    // descend. Monotonic, so even if the party fights straight through wave 3 in
    // continuous combat (the event engine is dormant in combat) and the bosses are
    // already dead by the time it drops combat, the gate still reads "past WAVE_3"
    // and releases — the kill steps then no-op and the run reaches the gossips.
    // (Mirrors enums ZFPyramidData::DATA_PYRAMID / ZFPyramidPhases in zulfarrak.h.)
    constexpr uint32 ZF_DATA_PYRAMID = 0;
    constexpr uint32 ZF_PHASE_WAVE_3 = 7;
    // Lead time between gossiping Weegli (door) and Bly (fight) — enough that
    // Bly's faction-flip of the crew can't catch Weegli mid-walk (live-verified).
    constexpr uint32 ZF_WEEGLI_LEAD_MS = 10000;  // 10 s

    // --- The Sacred Pool (Gahz'rilla gong) — the OPTIONAL final boss --------
    // The Gong of Zul'Farrak (GO 141832) sits by the sacred pool near the
    // entrance. USING it (GameObject::Use -> SmartGameObjectAI GOSSIP_HELLO with
    // reportUse=false, which the gong's filter==1 passes) summons Gahz'rilla
    // (7273) from the pool, gated by the smart event's condition instance
    // GetData(1)==0 and its NOT_REPEATABLE flag — so it fires exactly once on a
    // fresh instance. No Mallet of Zul'Farrak is needed: go->Use() runs the
    // GossipHello BEFORE the GOOBER lock check (lock 99 / LockType 14), exactly
    // as the troll cage above and the Razorfen Downs gong cheat their locks.
    //
    // Gahz'rilla emerges (a brief non-attackable scripted walk via its own
    // SmartAI) but does NOT auto-aggro the party, so we must PULL it. On its
    // death its SmartAI sets instance GetData(1)=3 (DONE). We summon, wait for it
    // to materialise, then engage and kill it via the normal engage pipeline.
    constexpr uint32 ZF_GONG = 141832;        // Gong of Zul'Farrak — summons Gahz'rilla
    constexpr uint32 ZF_GAHZRILLA = 7273;     // the summoned pool boss
    // Comfortably covers the objective anchor's arrive radius so a tank parked at
    // the edge of the pool still finds the gong and HopTos the last yards.
    constexpr float ZF_GONG_SEARCH = 30.0f;
    // Gahz'rilla's emerge-from-pool walk takes a few seconds; a generous wait
    // bridges the summon delay without escalating to a stall.
    constexpr uint32 ZF_GAHZRILLA_SPAWN_TIMEOUT = 60000;  // 1 min

    // --- Witch Doctor Zum'rah — the area-trigger wake-up ------------------
    // Zum'rah (7271) is a normal roster boss (order key 3), but he spawns FRIENDLY:
    // creature_template faction 35, no NOT_SELECTABLE / IMMUNE_TO_PC flag. He turns
    // hostile only when a party member crosses area trigger 962 (3yd off his spawn),
    // whose SmartAI row sets his faction to 37. That row runs off CMSG_AREATRIGGER,
    // which bots never send for a non-teleport trigger — so with a human in the party
    // he flips as a side effect of the human walking in, and in an all-bot party
    // (every `dc test` run) he never does. The engage gate is attackability-blind
    // (GetLiveBoss only asks IsAlive), so the tank walks up to a friendly Zum'rah and
    // the run deadlocks on him forever with no stall message.
    //
    // A conditional event fixes it entirely module-side: hook 5 (WakeZumrah) sets the
    // faction the trigger's SmartAI row would have set. An earlier attempt forged the
    // CMSG_AREATRIGGER packet instead, to run the genuine script — it did not work in
    // a live all-bot run, so the packet path (and the MoveTo onto the trigger centre
    // that existed only to satisfy the core's range check) is gone. See
    // ObjectiveHookRegistry for the full reasoning.
    //
    // Travel to him is NOT this event's job — he is a roster boss, so boss-nav
    // delivers the tank; the scan range below both gates the event on the party
    // having arrived and doubles as this conditional's proximity check (it has no
    // arrival step, so it is listed in the integrity test's near-gated whitelist).
    //
    // REPEATABLE, like the Razorfen Downs gong: a wipe runs his "On Reset - Restore
    // faction" row, putting him back to 35, and a one-shot latch would leave the
    // second attempt deadlocked exactly as before. The condition ends the loop by
    // itself the moment he is hostile (or dead), so the repeat is self-limiting.
    constexpr uint32 ZF_ZUMRAH = 7271;
    // Comfortably beyond boss engage range so the event fires as soon as boss-nav
    // parks the tank at him, before the engage loop settles into its deadlock —
    // but near enough that it can never fire from across the dungeon.
    constexpr float ZF_ZUMRAH_SCAN = 60.0f;
    constexpr uint32 ZF_ZUMRAH_FACTION_FRIENDLY = 35;  // creature_template default
    constexpr uint32 ZF_HOOK_WAKE_ZUMRAH = 5;  // ObjectiveHookRegistry id

    bool ZfZumrahAsleep(Player* bot, AiObjectContext* context);
}

void RegisterZulFarrakEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(209, 1, "Temple (Executioner / Bly's Band event)")
                      .Anchored(7)
                      .Persistent()
                      // 1. Clear the summit guard, then crack a cage to begin.
                      .KillCreatureEngage(ZF_EXECUTIONER)
                      .UseGO(ZF_TROLL_CAGE, /*searchRadius*/ 25.0f)
                      // 2. GARRISON down the ramp, IN FRONT of the NPC crew, so the
                      //    tank takes the waves' first aggro (the party doesn't
                      //    auto-assist the NPCs) AND returns to this spot between
                      //    waves instead of resting wherever the last fight ended —
                      //    down on the wave spawn, where the next wave spawns on top
                      //    of the party. Holds here until the instance reaches
                      //    WAVE_3 (bosses spawned); gated on the monotonic phase
                      //    rather than "boss alive" so a fight straight through
                      //    wave 3 can't leave it waiting on an already-dead boss.
                      .MoveToHoldUntilInstanceData(ZF_RAMP_X, ZF_RAMP_Y, ZF_RAMP_Z, /*radius*/ 10.0f,
                                                   ZF_DATA_PYRAMID, ZF_PHASE_WAVE_3)
                          .Timeout(ZF_WAVES_TIMEOUT)
                      // 3. Descend and help the crew kill the temple bosses.
                      .KillCreatureEngage(ZF_NEKRUM, /*count*/ 1, /*searchRadius*/ 250.0f)
                      .KillCreatureEngage(ZF_SEZZIZ, /*count*/ 1, /*searchRadius*/ 250.0f)
                      // 4. Goblin FIRST — opens the door — then a short dwell so
                      //    Bly's later faction-flip can't catch Weegli mid-walk.
                      //    WaitTargetStill: the crew's gossip is offered while they
                      //    are still walking DOWN to the temple floor — wait until
                      //    Weegli settles at his final spot before talking, or he is
                      //    interrupted mid-descent. SkipIfTargetMissing: if Weegli
                      //    died the boss is unreachable, but don't deadlock — skip
                      //    and still take Bly (the human can re-open / `dc skip`).
                      .Gossip(ZF_WEEGLI, /*option*/ 0, /*searchRadius*/ 40.0f)
                          .Timeout(ZF_NPC_TIMEOUT).WaitTargetStill().SkipIfTargetMissing()
                      .Wait(ZF_WEEGLI_LEAD_MS)
                      // 5. Human — starts the fight; killing Bly ends the event.
                      //    WaitTargetStill so we don't provoke Bly mid-descent;
                      //    SkipIfTargetMissing on the gossip + the engage gate's
                      //    own dead-check mean a dead Bly just completes the event
                      //    (all helpers gone => the clear continues to Ukorz).
                      .Gossip(ZF_BLY, /*option*/ 0, /*searchRadius*/ 40.0f)
                          .Timeout(ZF_NPC_TIMEOUT).WaitTargetStill().SkipIfTargetMissing()
                      .KillCreatureEngage(ZF_BLY, /*count*/ 1, /*searchRadius*/ 80.0f)
                      .Build());

    // The Sacred Pool (Gahz'rilla), ordered LAST (objective at encounterIndex 8,
    // after Chief Ukorz's bit 7 — see BossRosterRegistry). Boss-nav travels the
    // tank to the gong anchor, then this anchored event rings the gong and kills
    // the summoned boss. Persistent so the kill (a >1s combat tick-gap on the
    // engine) doesn't rewind the step list; WaitForSpawn is essential between the
    // ring and the kill, or KillCreatureEngage would read "no live Gahz'rilla"
    // before the summon and false-complete.
    out.push_back(EventBuilder(209, 2, "Sacred Pool (Gahz'rilla)")
                      .Anchored(8)
                      .Persistent()
                      .UseGO(ZF_GONG, ZF_GONG_SEARCH)
                      .WaitForSpawn(ZF_GAHZRILLA, /*alive*/ true)
                          .Timeout(ZF_GAHZRILLA_SPAWN_TIMEOUT)
                      .KillCreatureEngage(ZF_GAHZRILLA, /*count*/ 1, /*searchRadius*/ 100.0f)
                      .Build());

    // Wake Zum'rah: flip him hostile once the party reaches him, so he fights an
    // all-bot party the same way he fights a human who walked in. Folded under
    // Zum'rah in the panel — it is his pull, not a step of its own.
    out.push_back(EventBuilder(209, 3, "Wake Witch Doctor Zum'rah")
                      .Conditional(&ZfZumrahAsleep)
                      .Repeatable()
                      .PanelBeforeBoss(ZF_ZUMRAH)
                      .Custom(ZF_HOOK_WAKE_ZUMRAH)
                      .Build());
}

// --- the wake-up gate (event 3, repeatable) ------------------------------
// DUE while Zum'rah is alive, within scan range, and still carrying his spawn
// faction. Reads false the instant the flip lands (the boss flow takes him from
// there) and once he is dead or out of range, so the repeat can never spin: the
// only state that keeps it true is the exact deadlock it fixes.
//
// Tests faction 35 DIRECTLY rather than asking IsValidAttackTarget. The first
// version used the latter and the live run never woke him — with no log we could
// not tell whether the predicate or the packet was at fault, so this reads the
// one value the whole bug is about, which is also the value the hook writes.
namespace
{
    bool ZfZumrahAsleep(Player* bot, AiObjectContext* /*context*/)
    {
        Creature* zumrah = bot->FindNearestCreature(ZF_ZUMRAH, ZF_ZUMRAH_SCAN);
        if (!zumrah || !zumrah->IsAlive())
            return false;

        // Still on his spawn faction => nobody has tripped area trigger 962.
        return zumrah->GetFaction() == ZF_ZUMRAH_FACTION_FRIENDLY;
    }
}

// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterZulFarrakRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- ZulFarrak (map 209) -------------------------------------
    // ORDER FIX. The DBC encounter order does NOT match the travel path:
    // Hydromancer Velratha carries bit 0, so the auto path sent the tank
    // straight to the far sacred pool FIRST, before anything else. The
    // sensible (and Blizzard-intended) clear sweeps the dungeon front to
    // back and saves the pool for last:
    //   1. Theka the Martyr      (DBC bit 3)
    //   2. Antu'sul              (DBC bit 2)
    //   3. Witch Doctor Zum'rah  (DBC bit 4)
    //   4. Temple Summit         (objective — the Executioner event)
    //   5. Chief Ukorz Sandscalp (DBC bit 7 — opens after the event)
    //   6. Hydromancer Velratha  (DBC bit 0 — at the sacred pool)
    //   7. Sacred Pool           (objective — the Gahz'rilla gong)
    // Reorder the five auto-derived bosses in place (orderOverride keys
    // 1..6 on a contiguous scale shared with the two objectives) so each
    // sorts into its path slot while its real DBC kill-bit is untouched.
    //
    // The temple event (Executioner / Bly's Band) at the top of the great
    // staircase opens the door to Chief Ukorz; the tank must trigger it
    // before heading to Ukorz or it bee-lines into his closed door and
    // stalls. The Temple Summit objective (key 4) sits just before Ukorz
    // (key 5). eventId 1 is the full PERSISTENT temple step list (see
    // ZulFarrakEvents.cpp): kill the executioner, crack a cage, survive
    // the waves, descend to help kill the temple bosses, gossip Weegli
    // (door) and Bly (final fight). Only when Bly dies does the event
    // complete and latch this objective, after which the clear proceeds
    // to Ukorz with the door open.
    {
        BossRosterPatch p;
        p.mapId = 209;
        p.reorder = {
            { 7272, 1 },  // Theka the Martyr      (DBC bit 3)
            { 8127, 2 },  // Antu'sul              (DBC bit 2)
            { 7271, 3 },  // Witch Doctor Zum'rah  (DBC bit 4)
            { 7267, 5 },  // Chief Ukorz Sandscalp (DBC bit 7)
            { 7795, 6 },  // Hydromancer Velratha  (DBC bit 0)
        };
        p.add = {
            MakeObjective(OBJ(1), /*encounterIndex*/ 7, 209,
                          "Temple Summit (Executioner event)",
                          1886.8f, 1289.9f, 46.0f,
                          /*arriveRadius*/ 12.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 1, /*orderOverride*/ 4),
            // The optional Gahz'rilla gong, ordered LAST (key 7): the
            // strictly-ordinal picker only routes the tank to the sacred
            // pool once every real boss is dead. Anchor sits ON the gong
            // (GO 141832 spawn coords) so boss-nav delivers the tank right
            // to it; eventId 2 (see ZulFarrakEvents.cpp) rings it and kills
            // the summoned boss. gateEntry 0: the event owns completion
            // (Gahz'rilla dead), not "boss alive". The objective carries no
            // real kill-bit, so its key can't collide with a set encounter
            // bit.
            MakeObjective(OBJ(2), /*encounterIndex*/ 8, 209,
                          "Sacred Pool (Gahz'rilla gong)",
                          1650.91f, 1171.88f, 10.901f,
                          /*arriveRadius*/ 12.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 2, /*orderOverride*/ 7),
        };
        t.push_back(std::move(p));
    }
}
