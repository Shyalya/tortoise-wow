/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- The Mechanar (map 554) -------------------------------------------------
//
// Verified from the core (instance_mechanar.cpp / mechanar.h / the boss scripts)
// and the world DB. Three set-pieces sit between the five real DBC bosses and
// are expressed with EXISTING primitives — no net-new EventStepKind:
//
//   1. LOOT THE CACHE OF THE LEGION (floor 1). A locked chest (GO 184465 normal /
//      184849 heroic, Lock.dbc 1706 = a LOCK_KEY_ITEM lock) that requires the
//      Cache of the Legion Key (item 30438). Blizzard forms that key by combining
//      the two Jagged Crystals the Gatewatchers drop (Gyro-Kill 19218 -> Blue
//      30436, Iron-Hand 19710 -> Red 30437) via the crystals' on-use spell 36565.
//      A bot never right-clicks a crystal and the two crystals can split across
//      bags, so (per the design decision) we GRANT the key to the leader at the
//      cache and then USE it ON the chest (the Deadmines-cannon / Durnholde-barrel
//      item-use path): only the key ITEM as the cast item satisfies the KEY_ITEM
//      lock (core Spell::CanOpenLock), and the resulting Player::SendLoot fires the
//      loot-response that the stock "store loot" handler auto-drains. The stock
//      loot pipeline can NOT do this itself (IgnoreChests blacklists the chest and
//      OpenLootAction has no LOCK_KEY_ITEM handling), so the hook owns the open.
//      The chest is consumable (leaves GO_READY once used). All of that is hook 4
//      (GrantCacheKeyAndLoot, ObjectiveHookRegistry).
//
//   2. RIDE THE FACTORY ELEVATOR (floor 1 -> floor 2). The elevator (GO 183788,
//      a type-11 timer-driven transport) lifts the party ~25 yd from the Mo'arg
//      doors (~z1.6) to the Nethermancer door (267.9, 52.3, 27). The framework has
//      no moving-transport support and the navmesh does not bridge the floors, so
//      we FAKE the ride with TeleportParty (the Old Hillsbrad drake pattern): reach
//      the boarding checkpoint past the Mo'arg doors, blink the whole party to the
//      top landing. It cannot fire early — the boarding spot sits behind the twin
//      Mo'arg passage doors, which the core only opens once BOTH Gatewatchers die
//      (DOOR_TYPE_PASSAGE) — and it is ordered after Capacitus besides.
//
//   3. HOLD THE BRIDGE GAUNTLET (floor 2, before Pathaleon). This is a DEFENSIVE
//      set-piece, not a traversal: the gauntlet is three scripted waves that run
//      DOWN the bridge at the party. Walking up the bridge to meet them is the one
//      thing that must not happen — it feeds the party into the next wave's spawn
//      point and ends at the boss's feet. So the event parks a CAMP at the bridge
//      mouth, fights every wave where it stands, and only then walks to Pathaleon.
//
//      The chain (all of it SmartAI on the SPAWN GUIDS, not the entries — see the
//      smart_scripts rows for -138819/-138864/-138820 et al, plus actionlists
//      1922000/1922001/1922002). Each wave's trigger is the PREVIOUS wave's last
//      death, so the waves are strictly one at a time and nothing can be skipped:
//        wave 1, bridge mouth y37..41 — Sunseeker Astromage (guid 138819),
//          Sunseeker Engineer (138878), Bloodwarder Physician (138892). All three
//          spawn under Greater Invisibility (34426). The Astromage drops its own
//          invis and DoZoneInCombat()s on an OOC-LOS row (0-35yd, i.e. from the
//          bridge foot); the Engineer and Physician run the same actionlist on
//          their OWN aggro, so the party has to come within their aggro radius to
//          activate them — which is why the camp sits past them and not short.
//        wave 2, y53.2 — Tempest-Forge Destroyer (138864). Every wave-1 death
//          SET_COUNTERs it; on 3/3 it runs actionlist 1922001 (clear
//          IMMUNE_TO_PC|IMMUNE_TO_NPC, drop invis, DoZoneInCombat).
//        wave 3, y100..112 — Astromage (138820), Sunseeker Netherbinder (138869),
//          Physician (138893), Engineer (138879). The Destroyer's death
//          SET_DATA(1,1)s all four, each of which runs the same 1922001.
//
//      ONLY the four wave-3 deaths DoAction(ACTION_BRIDGE_MOB_DEATH) on Pathaleon
//      the Calculator (19220), and that action is the ONLY writer of
//      DATA_BRIDGE_MOB_DEATH_COUNT. So the boss's CanAIAttack()/visibility gate of
//      >= 4 means exactly "the last wave is dead" — not "any four adds died", which
//      is what an earlier revision of this file assumed. He then ethereal-teleports
//      in and DoZoneInCombat()s the party ~25s later on his own.
//
//      Once a wave is up, nothing has to be walked into: CreatureAI::DoZoneInCombat
//      engages every player within 250yd (the whole bridge) and instanced creatures
//      never leash, so each wave crosses the bridge to the party under its own
//      power. The camp is therefore a single garrison MoveTo gated on the persistent
//      death counter reaching 4 — the same "monotonic counter, safe to observe late"
//      contract the ZulFarrak ramp uses, which matters here because the gauntlet is
//      CONTINUOUS combat and the event engine is dormant in combat.
//
//      TWO THINGS MUST NOT BE TOUCHED WITHOUT RE-READING THE COMMENTS BELOW, because
//      each of them cost a 10-run plan (tp-20260816-105517-2, 6/10 with 4 wipes to
//      bridge trash, against 8/10 the run before):
//        * the camp must sit PAST the wave-1 cluster, or the party is in combat
//          before it can arrive and the event never starts at all; and
//        * the Pathaleon engage step must be EngageOnlyWhenActive with a tight seek
//          radius, or the combat-side stealth-breaker treats his scripted
//          invisibility as a stuck sapper and sprints the tank at him mid-fight.
//
// The five bosses are ordinary DBC encounters (auto-derived); the roster patch
// below only REORDERS them onto the travel path and interleaves the three
// objectives (mirrors the ZulFarrak reorder+objectives shape).

namespace
{
    // --- floor-1 bosses (reorder targets) --------------------------------
    constexpr uint32 MECH_GYROKILL  = 19218;  // Gatewatcher Gyro-Kill  (drops Jagged Blue Crystal 30436)
    constexpr uint32 MECH_IRONHAND  = 19710;  // Gatewatcher Iron-Hand  (drops Jagged Red Crystal 30437)
    constexpr uint32 MECH_CAPACITUS = 19219;  // Mechano-Lord Capacitus
    constexpr uint32 MECH_SEPETHREA = 19221;  // Nethermancer Sepethrea (floor 2)
    constexpr uint32 MECH_PATHALEON = 19220;  // Pathaleon the Calculator (bridge end)

    // --- (1) Cache of the Legion -----------------------------------------
    // Anchored ON the chest so boss-nav delivers the tank into interaction range;
    // the Custom step (hook 4) grants the key and holds until the consumable chest
    // is looted + gone. Chest spawn (both difficulty rows share the spot):
    constexpr float MECH_CACHE_X = 222.54f;
    constexpr float MECH_CACHE_Y = 70.61f;
    constexpr float MECH_CACHE_Z = 0.0f;
    constexpr uint32 MECH_HOOK_GRANT_CACHE_KEY = 4;  // ObjectiveHookRegistry id
    // Generous: the loot pipeline may take a few seconds after the key is granted;
    // only a genuinely stuck loot escalates to a stall for the human.
    constexpr uint32 MECH_CACHE_TIMEOUT = 120000;  // 2 min

    // --- (2) Factory elevator (fake the ride) ----------------------------
    // Boarding checkpoint: past the Mo'arg doors (236.5/242.9, 52, floor z~0.6), on
    // the shaft base. Top landing: on the upper platform ~2.6yd short of the
    // Nethermancer door (267.9, 52.3, ~26). Both are NAVMESH-VALIDATED against the
    // real map-554 mmaps (S921 DcNavHarness probe, TestMechanarElevatorProbe):
    //   * boarding snaps on-mesh at (249.07, 52.0, 0.60) and Capacitus->boarding
    //     routes reachable+complete (the tank walks there on floor 1);
    //   * the landing snaps on-mesh at (265.33, 52.0, 26.17) and landing->Sepethrea
    //     routes reachable+complete (len ~73yd, maxStepZ 1.48 — clean floor-2 path
    //     onward), so the teleported party is never stranded off-mesh.
    constexpr float MECH_BOARD_X = 249.0f;
    constexpr float MECH_BOARD_Y = 52.0f;
    constexpr float MECH_BOARD_Z = 0.6f;
    constexpr float MECH_TOP_X = 265.3f;
    constexpr float MECH_TOP_Y = 52.0f;
    constexpr float MECH_TOP_Z = 26.2f;

    // --- (3) Bridge gauntlet ---------------------------------------------
    // THE CAMP, at the bridge mouth (138, 45). The bridge deck runs x130..146
    // (16yd wide) from y~12 up to y~122, where it opens onto Pathaleon's platform;
    // he stands at (139.5, 149.3, 25.7).
    //
    // y45 is chosen for ARRIVABILITY, which is the property that matters and the
    // one that is easy to get wrong. An anchored event only ever runs on the
    // NON-COMBAT engine, so its steps can drive only in an out-of-combat tick — and
    // the gauntlet has essentially none between wave 1 and wave 3 (each wave's
    // trigger fires on the previous wave's last death). If the anchor sits SOUTH of
    // the wave-1 spawns the party is dragged into combat before it ever gets within
    // arriveRadius, the event never starts at all, and every gate below it is inert:
    // live in tr-20260816-105518-10 with the anchor at y26 — "objective 'Hold the
    // bridge camp': dist=30.0 > arriveRadius=10.0 (NOT arrived; event not started)",
    // followed by a wipe. y45 sits just PAST the wave-1 cluster, so the walk-in is
    // itself the last out-of-combat leg, and arriving there is what aggros the
    // Engineer (4.6yd) and Physician (4.9yd) whose deaths the counter chain needs.
    //
    // Waves 1 and 2 are therefore fought by the ordinary pull/combat pipeline on the
    // way in, exactly as they were before this event grew a camp. What the camp adds
    // is the HOLD afterwards: the tank stays on the spot while wave 3 runs the ~55yd
    // down the deck, instead of advancing up the bridge to meet it.
    //
    // NAVMESH-VALIDATED against the real map-554 mmaps (DcNavHarness probe,
    // TestMechanarGauntletProbe): the camp snaps on-mesh, and Sepethrea->camp,
    // camp->Pathaleon and (in reverse, the leg the far wave has to walk)
    // wave-3 spawn->camp all route reachable+complete.
    constexpr float MECH_CAMP_X = 138.0f;
    constexpr float MECH_CAMP_Y = 45.0f;
    constexpr float MECH_CAMP_Z = 25.4f;
    // Arrive tightly (walk properly onto the spot), then hold with a little slack
    // so ordinary combat shuffling doesn't yo-yo the tank back every tick.
    constexpr float MECH_CAMP_ARRIVE = 4.0f;
    constexpr float MECH_CAMP_HOLD = 6.0f;
    // The anchor radius only has to be loose enough for the objective to trigger;
    // step 0 does the precise walk-in.
    constexpr float MECH_CAMP_ANCHOR_RADIUS = 10.0f;

    // instance_mechanar's DataIndex (mechanar.h): the ONLY persistent-data slot the
    // map declares. Pathaleon's DoAction(ACTION_BRIDGE_MOB_DEATH) increments it once
    // per wave-3 death, and he is untargetable until it reaches 4.
    constexpr uint32 MECH_DATA_BRIDGE_MOB_DEATHS = 0;
    constexpr uint32 MECH_BRIDGE_DEATHS_TO_CLEAR = 4;

    // Seek radius for the Pathaleon engage. He is 105yd from the camp, so this is
    // "just enough" ON PURPOSE — see the .EngageOnlyWhenActive() note below. A wide
    // radius here is not free.
    constexpr float MECH_PATHALEON_SEEK = 130.0f;

    // Three waves back-to-back is minutes of continuous combat for a bot party, and
    // the camp holds through all of it on one step; don't let a slow but healthy
    // gauntlet escalate to a stall for the human.
    constexpr uint32 MECH_CAMP_TIMEOUT = 600000;      // 10 min for the whole gauntlet
    constexpr uint32 MECH_GAUNTLET_TIMEOUT = 300000;  // 5 min for the boss kill
}

void RegisterMechanarEvents(std::vector<DungeonEvent>& out)
{
    // (1) LOOT THE CACHE OF THE LEGION.
    // Non-persistent anchored: the objective navigates the tank onto the chest,
    // then hook 4 grants key 30438 and holds Running until the stock loot pipeline
    // consumes the chest (gone from range) — see GrantCacheKeyAndLoot. A combat gap
    // that rewinds a non-persistent event just re-runs the idempotent grant.
    out.push_back(
        EventBuilder(554, 1, "Loot the Cache of the Legion")
            .Anchored(/*orderIndex (doc)*/ 10)
            .Custom(MECH_HOOK_GRANT_CACHE_KEY)
                .Timeout(MECH_CACHE_TIMEOUT)
            .Build());

    // (2) RIDE THE FACTORY ELEVATOR (faked with TeleportParty).
    // PERSISTENT with a MoveTo step 0 so the at-objective trigger goes sticky
    // (stepIndex > 0) before the teleport — matching the OH ride. TeleportParty is
    // synchronous + idempotent and pulls stranded followers across, so a tick-gap
    // restart never double-teleports (the leader is no longer at the bottom
    // checkpoint once lifted).
    out.push_back(
        EventBuilder(554, 2, "Ride the Factory Elevator")
            .Anchored(/*orderIndex (doc)*/ 11)
            .Persistent()
            .MoveTo(MECH_BOARD_X, MECH_BOARD_Y, MECH_BOARD_Z, /*radius*/ 8.0f)
            .TeleportParty(/*checkpoint*/ MECH_BOARD_X, MECH_BOARD_Y, MECH_BOARD_Z,
                           /*landing*/ MECH_TOP_X, MECH_TOP_Y, MECH_TOP_Z,
                           /*radius*/ 12.0f)
            .Build());

    // (3) HOLD THE BRIDGE CAMP, THEN SLAY PATHALEON.
    // PERSISTENT so it stands down the pull pipeline (no premature engage of the
    // invisible boss) and its progress survives the wave combat gaps.
    //
    // Step 0 walks the tank onto the camp spot; it also bumps stepIndex to 1, which
    // is what makes the persistence sticky-trigger latch (see
    // DungeonEventExecutor::IsPersistentAnchoredEventActive) so the event keeps
    // driving even when a fight has dragged the tank off the anchor.
    //
    // Step 1 IS the camp: a garrison hold that re-walks the tank back onto the spot
    // whenever combat has displaced it, gated on the wave-3 death counter hitting 4.
    // There is deliberately NO advance step and NO ClearRadius — the waves come to
    // the camp, and the previous shape's "advance to y90 to trip the far cluster,
    // then clear it" walked the tank into the oncoming wave and on toward the boss.
    // Killing is left to the ordinary combat engine, which owns the tick anyway for
    // as long as the party is in combat; the event's job here is only to decide
    // WHERE the party stands and WHEN the gauntlet is over.
    //
    // The gate is the persistent counter rather than "no hostile left in a volume"
    // because the whole gauntlet is one unbroken fight: wave 2 engages on wave 1's
    // last death and wave 3 on wave 2's, so there is no out-of-combat moment in
    // which a position-based gate could be read, and the first such moment after
    // the last wave dies must not be mistaken for "cluster clear, advance".
    //
    // Step 2 takes Pathaleon once the counter has made him attackable. He also
    // DoZoneInCombat()s the party himself ~25s after the 4th death, so the party is
    // often already pulled in before the walk finishes.
    //
    // EngageOnlyWhenActive is NOT optional here. The combat-side stealth-breaker
    // (DungeonClearObjectiveEngageCombatTrigger, relevance 34 — above the stock
    // combat movers) arms off the event's first engage step even while an earlier
    // step is active, and fires when a live creature of that entry is reachable but
    // UNDETECTABLE. Pathaleon is greater-invisible until the 4th bridge death, so he
    // matches that signature for the entire gauntlet: the tank abandons whatever
    // wave it is tanking and sprints the length of the bridge at him. That is the
    // tp-20260816-105517-2 regression (4 wipes to bridge trash, 6/10 vs the previous
    // 8/10). The seek radius above being tight is the second belt on the same
    // trousers — widening it to 250 armed the rung from Sepethrea's room, 198yd out.
    out.push_back(
        EventBuilder(554, 3, "Hold the bridge camp and slay Pathaleon")
            .Anchored(/*orderIndex (doc)*/ 12)
            .Persistent()
            .MoveTo(MECH_CAMP_X, MECH_CAMP_Y, MECH_CAMP_Z, MECH_CAMP_ARRIVE)
            .MoveToHoldUntilPersistentData(MECH_CAMP_X, MECH_CAMP_Y, MECH_CAMP_Z,
                                           MECH_CAMP_HOLD, MECH_DATA_BRIDGE_MOB_DEATHS,
                                           MECH_BRIDGE_DEATHS_TO_CLEAR)
                .Timeout(MECH_CAMP_TIMEOUT)
            .KillCreatureEngage(MECH_PATHALEON, /*count*/ 1, MECH_PATHALEON_SEEK)
                .EngageOnlyWhenActive()
                .Timeout(MECH_GAUNTLET_TIMEOUT)
            .Build());
}

// --- roster patch: reorder the five DBC bosses + interleave three objectives ---
void RegisterMechanarRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // The DBC encounter order does not match the travel path; reorder the three
    // DBC bosses and ADD the two Gatewatchers onto a contiguous 1..8 key scale
    // shared with the three objectives (orderOverride 4/5/7), so the strictly-
    // ordinal picker walks:
    //   1. Gatewatcher Gyro-Kill
    //   2. Mechano-Lord Capacitus
    //   3. Gatewatcher Iron-Hand          (both crystals now looted across 1 & 3)
    //   4. Loot the Cache of the Legion   (objective, eventId 1)  — past the Mo'arg doors
    //   5. Ride the Factory Elevator      (objective, eventId 2)  — up to floor 2
    //   6. Nethermancer Sepethrea
    //   7. Hold the bridge camp           (objective, eventId 3)  — holds the 3 waves + kills Pathaleon
    //   8. Pathaleon the Calculator       (already dead once the gauntlet event completes)
    //
    // Capacitus sits at order 2, BETWEEN the Gatewatchers, on purpose: the tank
    // reaches him from Gyro-Kill (NW) and approaches the pit from the west/north,
    // so the SE Driller pack (y-52..-63) is NOT on the pull approach — it falls on
    // the post-Capacitus walk DOWN to Iron-Hand (SE) and gets handled as ordinary
    // trash-clear once the boss is already dead. That removes the whole reason the
    // Capacitus room-aggro pre-clear existed (it pre-cleared that SE pack when the
    // old order approached him last, up the corridor from the dead Iron-Hand), so
    // the RoomAggroRegistry entry for 19219 is gone.
    //
    // CRITICAL: the two Gatewatchers are NOT auto-derived. instance_encounters has
    // no ENCOUNTER_CREDIT_KILL_CREATURE row for 19218/19710 (they are door-gating
    // mini-bosses, not DungeonEncounter.dbc encounters), so BossSpawnIndex never
    // lists them and a bare `reorder` entry is a no-op — the picker jumped straight
    // to Capacitus and the party never killed them, leaving the Mo'arg passage
    // doors (each opens on its Gatewatcher's death) shut and the elevator blocked.
    // So ADD them as explicit bosses whose completion reads the instance script's
    // own boss-state slots (DATA_GATEWATCHER_GYROKILL=0 / _IRON_HAND=1 from
    // mechanar.h) via MakeBoss's doneBossStateIndex — the one sanctioned use of
    // GetBossState, keyed off the instance header, not a coincidental DBC index.
    //
    // Objective encounterIndex uses 10/11/12 — bits the 5-encounter mask never
    // sets, so an objective only completes via its event, never a stray mask bit.
    constexpr int32 MECH_DATA_GYROKILL = 0;   // DATA_GATEWATCHER_GYROKILL (mechanar.h)
    constexpr int32 MECH_DATA_IRONHAND = 1;   // DATA_GATEWATCHER_IRON_HAND (mechanar.h)
    BossRosterPatch p;
    p.mapId = 554;
    p.reorder = {
        { MECH_CAPACITUS, 2 },
        { MECH_SEPETHREA, 6 },
        { MECH_PATHALEON, 8 },
    };
    p.add = {
        MakeBoss(MECH_GYROKILL, 554, "Gatewatcher Gyro-Kill",
                 85.53f, 20.20f, 15.00f, /*completionFrom*/ 0,
                 /*orderOverride*/ 1, /*doneBossStateIndex*/ MECH_DATA_GYROKILL),
        MakeBoss(MECH_IRONHAND, 554, "Gatewatcher Iron-Hand",
                 181.85f, -77.12f, 0.01f, /*completionFrom*/ 0,
                 /*orderOverride*/ 3, /*doneBossStateIndex*/ MECH_DATA_IRONHAND),
        MakeObjective(OBJ(1), /*encounterIndex*/ 10, 554, "Loot the Cache of the Legion",
                      MECH_CACHE_X, MECH_CACHE_Y, MECH_CACHE_Z, /*arriveRadius*/ 5.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 1, /*orderOverride*/ 4),
        MakeObjective(OBJ(2), /*encounterIndex*/ 11, 554, "Ride the Factory Elevator",
                      MECH_BOARD_X, MECH_BOARD_Y, MECH_BOARD_Z, /*arriveRadius*/ 8.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 2, /*orderOverride*/ 5),
        MakeObjective(OBJ(3), /*encounterIndex*/ 12, 554, "Hold the bridge camp",
                      MECH_CAMP_X, MECH_CAMP_Y, MECH_CAMP_Z,
                      MECH_CAMP_ANCHOR_RADIUS, /*gateEntry*/ 0, /*hook*/ 0,
                      /*eventId*/ 3, /*orderOverride*/ 7),
    };
    t.push_back(std::move(p));
}
