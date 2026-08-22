/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"

#include "Creature.h"
#include "GameObject.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"

#include <atomic>

// --- Shadowfang Keep (map 33) — CONDITIONAL, FACTION-SPECIFIC -------------
// The Courtyard Door (GO 18895) gates the keep past the entry rooms and is
// opened only by a freed prisoner, not by the party. The real mechanic
// (verified from the SFK SmartAI, 2026-06-11) is faction-specific — each side
// has its OWN lever + prisoner:
//   Alliance: pull lever 18901 (opens cell gate 18936) -> gossip Sorcerer
//             Ashcrombe (3850, menu 21213) option 0.
//   Horde:    pull lever 18900 (opens cell gate 18934) -> gossip Deathstalker
//             Adamant (3849, menu 21214) option 0.
// Picking the option fires the prisoner's GOSSIP_SELECT SmartAI: he walks to
// the courtyard door and, ~35s later (5s + 30s waypoint pauses), opens it. Two
// events, one per faction, each gated by a team-aware condition (1 = Alliance,
// 2 = Horde) so the right lever + prisoner drive; only one is ever due for a
// given party. The step list is lever -> gossip -> wait-for-door (an earlier
// version skipped the lever, so the bot could never reach the caged prisoner
// and the gossip was a no-op). Relevance 31 (DungeonClearEventDue) preempts the
// boss pull / door-blocked stall. Optional so a non-firing script degrades to
// the normal door-blocked stall.

// --- Shadowfang Keep (map 33) — the Sorcerer's Gate voidwalker set-piece ---
// The gate out of Fenrus' room toward Wolf Master Nandos (GO 18972, guid 33785)
// is NOT opened by the party either. Verified from the SFK SmartAI + world DB,
// 2026-08-01:
//   Fenrus the Devourer (4274) dies
//     -> his script sets data 1/1 on Archmage Arugal (4275, the final boss,
//        standing a floor up)
//     -> that starts timed actionlist 427500: +2s Arugal speaks, +5s anim,
//        +6s SUMMON CREATURE GROUP 1 — four Arugal's Voidwalkers (4627) at the
//        WEST end of Fenrus' room, ringed ~7.7yd around (-146.75, 2173.21,
//        128.45)
//     -> 'Arugal's Voidwalker - On Just Died - Set GO State' opens the gate the
//        instant the FIRST of them dies.
// The voidwalkers spawn REACT-aggressive (SMART_EVENT_RESET -> Attack Start on
// the closest player within 40yd) and are TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT
// at 60s — so if nothing engages them within a minute they vanish and the gate
// stays shut for the rest of the session (the instance script only re-opens it
// from TYPE_FENRUS_THE_DEVOURER == DONE on GO *create*, i.e. after a grid
// reload). Standing in the room and fighting is the only in-session path
// through.
//
// Which is exactly what the bug was: the gate wears the same empty-lock-85
// template as a plain clickable door, so the door-blocked action rated itself
// entitled and Use()d it open — permanently, since door.autoCloseTime is 0 —
// within the ~6s window between Fenrus dying and the adds existing. The party
// then walked east through a gate that "should be based on the voidwalker
// event", the four voidwalkers materialised BEHIND it, and the run wedged
// between advancing up to Nandos and turning back for the adds. Both halves are
// fixed here: DcEventDoorRegistry::IsScriptOnly now refuses the click, and this
// event holds the party on the room's spawn ring until the adds arrive, kills
// them, and lets the gate open itself.
namespace
{
    bool SfkCourtyardAlliance(Player* bot, AiObjectContext* context);
    bool SfkCourtyardHorde(Player* bot, AiObjectContext* context);
    bool SfkSorcererGate(Player* bot, AiObjectContext* context);

    constexpr uint32 SFK_SORCERER_GATE = 18972;
    constexpr uint32 SFK_FENRUS = 4274;
    constexpr uint32 SFK_VOIDWALKER = 4627;

    // Centroid of the four creature_summon_groups spawn points (summonerId 4275,
    // groupId 1) — every one of them is 7.4-7.8yd from here, so the sweep below
    // is judged from inside DC_EVENT_CLEAR_JUDGE_RADIUS (12) with the whole ring
    // in view. 12.0yd from Fenrus' own spot, so the party barely moves to take
    // it up, and 19.1yd back from the gate.
    constexpr float SFK_VOIDWALKER_RING_X = -146.75f;
    constexpr float SFK_VOIDWALKER_RING_Y = 2173.21f;
    constexpr float SFK_VOIDWALKER_RING_Z = 128.45f;

    // Wide enough to cover the ring plus wherever the adds close to (they run at
    // the party, so the fight happens on top of the anchor), and harmless at any
    // width because the sweep is entry-filtered to the voidwalkers alone.
    constexpr float SFK_VOIDWALKER_SWEEP_RADIUS = 40.0f;
    constexpr float SFK_VOIDWALKER_SWEEP_ZBAND = 15.0f;
    constexpr float SFK_VOIDWALKER_SCAN = 60.0f;

    // The summon lands 6s after Fenrus dies; 45s is a wide margin on that and
    // still expires INSIDE the 60s out-of-combat despawn, so a wipe-and-return
    // (adds long gone, gate shut forever) fails the step promptly instead of
    // parking the party in an empty room for minutes. Optional() then degrades
    // it to the ordinary door-blocked pause, which auto-resumes via
    // DungeonClearDoorReopenedTrigger if a human opens the gate.
    constexpr uint32 SFK_VOIDWALKER_SPAWN_TIMEOUT = 45000;
    // Four level-20 adds; generous but bounded.
    constexpr uint32 SFK_VOIDWALKER_FIGHT_TIMEOUT = 180000;
    // The gate is already open by the time the last add falls (the FIRST death
    // opens it) — this is a confirmation gate, not a wait.
    constexpr uint32 SFK_SORCERER_GATE_TIMEOUT = 30000;
}

void RegisterShadowfangKeepEvents(std::vector<DungeonEvent>& out)
{
    // After freeing the prisoner, walk up to the courtyard door and wait THERE
    // (not in the cell) for it to open — the closed door stops the approach a
    // few yards short, so the tank is parked ready to walk through the instant
    // the prisoner opens it.
    // Panel placement: the courtyard door is opened right after the first boss
    // (Rethilgore, 3914) and gates the rest of the keep, so it sorts as its own
    // row immediately AFTER Rethilgore (#2) rather than last. Each row is shown
    // only to the faction that actually performs it — the activation predicate
    // already team-gates execution; .PanelTeam mirrors that on the panel so the
    // other faction's row is hidden.
    out.push_back(EventBuilder(33, 1, "Free Ashcrombe (Courtyard Door, Alliance)")
                      .Conditional(&SfkCourtyardAlliance)
                      .MoveTo(-248.0f, 2122.0f, 81.3f, /*radius*/ 6.0f)
                      .UseGO(/*lever*/ 18901, /*searchRadius*/ 14.0f)
                      .Gossip(/*Sorcerer Ashcrombe*/ 3850, /*option*/ 0, /*searchRadius*/ 16.0f)
                      .MoveTo(/*courtyard door*/ -242.58f, 2159.05f, 90.62f, /*radius*/ 9.0f)
                      .WaitForGOState(/*courtyard door*/ 18895, /*GO_STATE_ACTIVE*/ 0,
                                      /*timeout*/ 60000)
                      .PanelAfterBoss(/*Rethilgore*/ 3914)
                      .PanelTeam(TEAM_ALLIANCE)
                      .Optional()
                      .Build());

    out.push_back(EventBuilder(33, 2, "Free Adamant (Courtyard Door, Horde)")
                      .Conditional(&SfkCourtyardHorde)
                      .MoveTo(-251.0f, 2115.0f, 81.3f, /*radius*/ 6.0f)
                      .UseGO(/*lever*/ 18900, /*searchRadius*/ 14.0f)
                      .Gossip(/*Deathstalker Adamant*/ 3849, /*option*/ 0, /*searchRadius*/ 16.0f)
                      .MoveTo(/*courtyard door*/ -242.58f, 2159.05f, 90.62f, /*radius*/ 9.0f)
                      .WaitForGOState(/*courtyard door*/ 18895, /*GO_STATE_ACTIVE*/ 0,
                                      /*timeout*/ 60000)
                      .PanelAfterBoss(/*Rethilgore*/ 3914)
                      .PanelTeam(TEAM_HORDE)
                      .Optional()
                      .Build());

    // Kill Arugal's voidwalkers so the Sorcerer's Gate opens itself.
    //
    // CONDITIONAL, not Anchored: the voidwalkers are trash, not an encounter, so
    // they earn no roster objective and no encounter slot. The condition (see
    // SfkSorcererGate) is its own proximity gate — it needs the gate GO within
    // SFK_VOIDWALKER_SCAN, which is only true from inside Fenrus' room and the
    // stairs above it — and its own completion test: the gate reading open is
    // the mechanic's definition of done, so the event latches the moment the
    // first voidwalker dies.
    //
    // PERSISTENT because the sweep is a fight: the party is in REAL combat with
    // the adds (unlike the Arcatraz Eredar room, which is only combat-FLAGGED by
    // an aura), the event rung is non-combat-only, and a mid-fight Drive gap
    // would rewind a non-persistent event back to step 0 — re-running the
    // WaitForSpawn after the adds have already been pulled. Nothing is lost by
    // not driving mid-fight: DC's ordinary combat machinery fights the adds, and
    // the advance rung is non-combat-only too, so the party cannot leave for
    // Nandos while the fight is on. Between fights this rung (EventDue 31)
    // outranks advance (15), so it re-takes the tick and finishes the sweep.
    //
    // Panel: its own row right after Fenrus, whose death starts the sequence.
    out.push_back(EventBuilder(33, 3, "Arugal's Voidwalkers (Sorcerer's Gate)")
                      .Conditional(&SfkSorcererGate)
                      .Persistent()
                      .PanelAfterBoss(/*Fenrus the Devourer*/ SFK_FENRUS)
                      // 1. Take up the spawn ring instead of walking on to the
                      //    gate. This is the step that actually fixes the report:
                      //    it keeps the party inside the adds' 40yd Attack-Start
                      //    radius for the ~6s until they exist, so they aggro
                      //    into a waiting party rather than materialising behind
                      //    one already on the stairs.
                      .MoveTo(SFK_VOIDWALKER_RING_X, SFK_VOIDWALKER_RING_Y,
                              SFK_VOIDWALKER_RING_Z, /*radius*/ 8.0f)
                      // 2. Wait for the summon. Without this gate the sweep below
                      //    would evaluate an empty room in the 6s window, certify
                      //    it clear and latch the event done having killed
                      //    nothing — the Stratholme #5 false-latch, on a timer.
                      .WaitForSpawn(SFK_VOIDWALKER, /*wantAlive*/ true,
                                    SFK_VOIDWALKER_SPAWN_TIMEOUT)
                      // 3. Clear the ring. Entry-filtered: nothing else in the
                      //    room is this event's business, and Fenrus' own trash
                      //    is already dead by the time it runs.
                      .ClearRadius(SFK_VOIDWALKER_RING_X, SFK_VOIDWALKER_RING_Y,
                                   SFK_VOIDWALKER_RING_Z, SFK_VOIDWALKER_SWEEP_RADIUS,
                                   SFK_VOIDWALKER_SWEEP_ZBAND)
                          .OnlyEntries({ SFK_VOIDWALKER })
                          .Timeout(SFK_VOIDWALKER_FIGHT_TIMEOUT)
                      // 4. BY-ENTRY BACKSTOP (the Shattered Halls assassin
                      //    lesson). A ClearRadius resolves targets through
                      //    NearestHostileNearPoint, which drops anything
                      //    CanSeeOrDetect or the strict reachability probe
                      //    rejects — and a voidwalker the sweep cannot see is a
                      //    voidwalker whose death never opens the gate.
                      //    KillCreatureEngage resolves by entry through a plain
                      //    grid scan with the looser reachability probe, and is
                      //    an instant no-op when they really are all dead.
                      .KillCreatureEngage(SFK_VOIDWALKER,
                                          /*count (doc; "any alive")*/ 4,
                                          /*searchRadius*/ SFK_VOIDWALKER_SCAN)
                          .Timeout(SFK_VOIDWALKER_FIGHT_TIMEOUT)
                      // 5. Confirm the gate actually swung. If the SmartAI hook
                      //    somehow did not fire we hold here rather than walking
                      //    the party into a shut gate and calling it navigation.
                      .WaitForGOState(SFK_SORCERER_GATE, /*GO_STATE_ACTIVE*/ 0,
                                      SFK_SORCERER_GATE_TIMEOUT)
                      // Degrade, don't stall: if the adds are gone (a wipe burned
                      // the 60s despawn) no amount of waiting brings them back, so
                      // fall through to the door-blocked pause and let the human
                      // open the gate — that pause auto-resumes the run the
                      // instant it opens.
                      .Optional()
                      .Build());
}

// --- Courtyard-door activation conditions (1 = Alliance, 2 = Horde) -------
// The Courtyard Door (GO 18895) blocks progression past the entry rooms and is
// opened not by the party but by a freed prisoner. The mechanic is
// FACTION-SPECIFIC: Alliance pulls the lever by Sorcerer Ashcrombe's cell
// (3850) and gossips him; Horde pulls a different lever by Deathstalker
// Adamant's cell (3849) and gossips him. Both NPCs spawn in AC, but each party
// uses its own — so the condition is gated on team so the right event (right
// lever + right prisoner) drives. DUE while the door is still shut
// (GO_STATE_READY) AND the faction's prisoner is alive to free; once the door
// opens this reads false and the event latches done.
namespace
{
    constexpr uint32 SFK_COURTYARD_DOOR = 18895;
    constexpr uint32 SFK_PRISONER_ASHCROMBE = 3850;  // Alliance
    constexpr uint32 SFK_PRISONER_ADAMANT = 3849;    // Horde
    // Rethilgore (3914) is the first boss and stands AMONG the prison cells, so
    // the courtyard event must wait until he is dead — otherwise the party would
    // detour to the prison the instant DC is enabled at the zone entrance (the
    // bug this gate fixes). His grid is co-loaded with the prisoners', so the
    // prisoner-alive check below guarantees we are not reading a false "dead"
    // from an unloaded grid.
    constexpr uint32 SFK_FIRST_BOSS_RETHILGORE = 3914;
    // Door / prisoner sit near the entry; scan generously so the condition is
    // true from the moment the party is anywhere in the early keep.
    constexpr float SFK_SCAN = 200.0f;

    bool SfkCourtyard(Player* bot, TeamId team, uint32 prisonerEntry, char const* who)
    {
        if (bot->GetTeamId() != team)
            return false;

        GameObject* door = bot->FindNearestGameObject(SFK_COURTYARD_DOOR, SFK_SCAN);
        Creature* prisoner = bot->FindNearestCreature(prisonerEntry, SFK_SCAN, /*alive*/ true);
        bool const firstBossDead =
            DcTargeting::FindLiveCreatureOnMap(bot, SFK_FIRST_BOSS_RETHILGORE) == nullptr;

        // Throttled diagnostic: one line / 5s per faction so a live run shows WHY
        // the event is or isn't due (door missing/open, no prisoner, first boss
        // still up). Lands in DungeonClear.log. atomic because bot AI ticks run on
        // the MapUpdate.Threads pool — the throttle stamp is read/written from
        // multiple map threads (the check-then-set race is benign: at worst two
        // lines in a window).
        static std::atomic<uint32> lastLog{0};
        uint32 const now = getMSTime();
        if (getMSTimeDiff(lastLog, now) >= 5000)
        {
            lastLog = now;
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] SFK courtyard cond ({}): door={} state={} prisoner={} rethilgore={}",
                      bot->GetName(), who, door ? "found" : "MISSING",
                      door ? static_cast<int>(door->GetGoState()) : -1,
                      prisoner ? "alive" : "no", firstBossDead ? "dead" : "ALIVE");
        }

        if (!firstBossDead)
            return false;  // wait until the first boss (Rethilgore) is down
        if (!door || door->GetGoState() != GO_STATE_READY)
            return false;  // no door found, or already open
        return prisoner != nullptr;
    }

    bool SfkCourtyardAlliance(Player* bot, AiObjectContext* /*context*/)
    {
        return SfkCourtyard(bot, TEAM_ALLIANCE, SFK_PRISONER_ASHCROMBE, "A");
    }

    bool SfkCourtyardHorde(Player* bot, AiObjectContext* /*context*/)
    {
        return SfkCourtyard(bot, TEAM_HORDE, SFK_PRISONER_ADAMANT, "H");
    }

    // --- Sorcerer's Gate activation condition -----------------------------
    // DUE while Fenrus is dead (his death is what starts the summon script) and
    // the gate is still SHUT. Both halves matter:
    //   - the gate lookup is the PROXIMITY GATE. SFK_VOIDWALKER_SCAN is 60yd and
    //     the gate is unique on the map, so this cannot read true anywhere before
    //     Fenrus' room — Odo, the nearest earlier boss, is 107yd from it.
    //   - the dead-Fenrus test keeps the event from preempting his pull. Read
    //     off the whole map (not a grid scan) so a party that arrives after the
    //     gate has already been dealt with, or that pulled him out of the room,
    //     is judged on the encounter and not on what happens to be near.
    // Once the first voidwalker dies the gate reads GO_STATE_ACTIVE, this goes
    // false and the event latches done — which is also how a saved instance
    // whose gate is already open never re-runs it.
    bool SfkSorcererGate(Player* bot, AiObjectContext* /*context*/)
    {
        GameObject* gate = bot->FindNearestGameObject(SFK_SORCERER_GATE, SFK_VOIDWALKER_SCAN);
        if (!gate || gate->GetGoState() != GO_STATE_READY)
            return false;  // not in the room yet, or the gate is already open
        return DcTargeting::FindLiveCreatureOnMap(bot, SFK_FENRUS) == nullptr;
    }
}
