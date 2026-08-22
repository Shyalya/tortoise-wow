/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCRELEVANCE_H
#define _PLAYERBOT_DCRELEVANCE_H

// The dungeon-clear trigger relevance ladder, as named constants shared by BOTH
// strategies (DungeonClearStrategy = non-combat, DungeonClearCombatStrategy =
// combat) instead of float literals scattered across the InitTriggers bodies
// with the invariants living only in prose. t/TestRelevanceLadder.cpp pins the
// ordering and documents every same-value tie with the partition (role / engine /
// map / anchor-kind) that legitimizes it, so a future edit that reorders a rung —
// or lands a new feature on an occupied one — trips a red test instead of
// silently depending on trigger registration order.
//
// TIES THAT ARE REAL (measured, arch-review F3) and how they are handled:
//   - Chat == PartyDied (100): distinct trigger conditions (keyword vs death),
//     both terminal — kept equal, asserted.
//   - AtBoss == AtObjective (30): mutually exclusive by the anchor-kind check in
//     each trigger — kept equal, asserted.
//   - BlockingTrash == FollowTank (25): leader-only vs follower-only — kept
//     equal, asserted.
//   - PullManeuver == StayAtCamp (60, combat): leader-only vs follower-only —
//     kept equal, asserted.
// TIES THAT WERE NOT PARTITIONED (both armable on the same leader) — BROKEN here
// so ordering is deterministic:
//   - HakkarFlame was 35, tying Pull (35). Now 35.5 (still below the 36
//     suppressor, above the 34 loot-blood): on the Sunken Temple carrier a douse
//     outranks starting a fresh pull.
//   - NeedsRest (drink/eat) was 26, tying RoomTrash (26). Now 26.5: a leader
//     below its rest target tops up before committing to a room pre-clear,
//     matching the rest trigger's "top up before pulling" intent.
namespace DcRel
{
    // ===== shared top band =====
    inline constexpr float Chat            = 100.0f; // chat keyword commands (dc on/off/…)
    inline constexpr float PartyDied       = 100.0f; // death bailout (non-combat only)
    inline constexpr float LootRollPending = 95.0f;  // resolve an open loot-roll window at once
    inline constexpr float DoorReopened    = 90.0f;  // auto-resume when a player opens the door
    inline constexpr float AllCleared      = 50.0f;  // congratulate + disable
    // Stranded-member recovery failsafe (leader-only, non-combat). When the run
    // has frozen for StrandedRecoveryNoProgressSecs with a bot member stuck out of
    // range, teleport the strays to the tank. Sits ABOVE the whole leader driving
    // ladder (which has, by definition, been failing to make progress for minutes)
    // so the recovery wins the tick it arms; its narrow trigger keeps it inert
    // otherwise. Below AllCleared (a finished run should congratulate + disable,
    // not teleport). See Util/DcStrandedRecovery.
    inline constexpr float StrandedRecovery = 42.0f; // leader: rescue a stuck member
    inline constexpr float HealReposition  = 41.0f;  // healer-only; both engines (see note below)

    // ===== non-combat leader driving ladder =====
    inline constexpr float HakkarSuppressor = 36.0f; // ST Hakkar: silence a resetting suppressor
    inline constexpr float HakkarFlame      = 35.5f; // ST Hakkar: douse (tie-broken above Pull)
    inline constexpr float Pull             = 35.0f; // advanced/dynamic pull-to-camp maneuver
    inline constexpr float HakkarLootBlood  = 34.0f; // ST Hakkar: grab blood (below flame)
    // Post-combat rez driver. Runs on ALL bots (the elected rezzer may be a
    // follower OR the leader — a prot paladin raising its healer), so it must
    // outrank BOTH ladders it can land on: on the leader, EventDue (31) and
    // AtBoss/AtObjective (30) — recover the party before running an event gate
    // or pulling the next boss; on a follower, every follower rung (assist 29 /
    // hold-at-camp 28 / follow-tank 25). 31.5 (not the plan's proposed 31,
    // which EventDue already occupies with no role/engine partition — both are
    // leader-armable on the same tick). Kept BELOW Pull (35): the pull trigger
    // gates on the between-pulls readiness, which the rez IsPending hold makes
    // false, so Pull is inert during a recovery anyway; and below the Hakkar
    // band (34-36, map-partitioned mid-encounter orchestration).
    inline constexpr float RezParty         = 31.5f; // elected rezzer: raise the corpse
    inline constexpr float EventDue         = 31.0f; // off-path conditional event gate due
    inline constexpr float AtBoss           = 30.0f; // engage the next boss
    inline constexpr float AtObjective      = 30.0f; // arrive at a travel objective (anchor-kind peer)
    inline constexpr float AssistCamp       = 29.0f; // follower: pile into the leader's fight
    inline constexpr float HoldAtCamp       = 28.0f; // follower: hold passive at camp mid-pull
    inline constexpr float NeedsRest        = 26.5f; // rest to target HP/mana (tie-broken above RoomTrash)
    inline constexpr float RoomTrash        = 26.0f; // room-aggro boss pre-clear
    inline constexpr float BlockingTrash    = 25.0f; // leader: engage a pack blocking the path
    inline constexpr float FollowTank       = 25.0f; // follower: redirect follow to the tank
    inline constexpr float LeaderAssist     = 24.0f; // leader: help a fight it never saw
    inline constexpr float DoorBlocked      = 22.0f; // stall at a shut door
    inline constexpr float Stalled          = 20.0f; // stalled-no-path fallback
    inline constexpr float RoomPreclearHold = 16.0f; // hold the room-aggro standoff
    inline constexpr float Advance          = 15.0f; // default: walk toward the next boss
    inline constexpr float FilterLoot       = 9.0f;  // enforce loot policy while paused

    // ===== combat engine =====
    // Phantom-combat escape hatch. A DC bot flagged in combat but with nothing it
    // can fight (no attacker, no victim, no reachable holder — e.g. a mob spawned
    // far across the map / behind a gate tagged it) force-clears its combat after a
    // long timeout. Highest combat rung so the recovery always wins the tick when it
    // legitimately fires; it never contends with real content because the trigger is
    // inert whenever anything is actually fightable. See
    // DungeonClearBreakStuckCombatTrigger / DungeonClearMath::ShouldBreakStuckCombat.
    //
    // Registered in BOTH engines (like HazardVacate), and listed here rather than in
    // the non-combat block only because the flag it clears is a combat one. The
    // non-combat half is the load-bearing one: `drop target` (stock, relevance 99)
    // can move a still-flagged bot onto the NON-combat engine, where every DC rung
    // bails on IsInCombat() and every combat rung — including this hatch, while it
    // was combat-only — is out of reach. 65 also tops the non-combat ladder
    // (HealReposition 41, HazardVacate 55), which is what it needs: the recovery
    // must beat the driving rungs it is unblocking. See DungeonClearStrategy.
    inline constexpr float BreakStuckCombat       = 65.0f; // phantom-combat force-clear
    inline constexpr float HakkarSuppressorCombat = 64.0f; // ST Hakkar combat side
    inline constexpr float HakkarFlameCombat      = 63.0f;
    inline constexpr float HakkarLootBloodCombat  = 62.0f;
    // Leader-only. COMBAT copy of the conditional-event rung, restricted to events
    // flagged DungeonEvent::drivesInCombat — a continuous WAVE encounter where the
    // event IS the fight and the driver must keep steering the tank under fire.
    //
    // 61 sits deliberately high: the driver's whole job in that shape is to move
    // the tank somewhere the stock combat engine would never take it (off the pack
    // it is tanking, across the arena, to the next rift), so it has to outrank
    // MoveChase (~30), the DC role repositions (assist 35 / regroup 29) and the
    // camp owners (60). It stays BELOW Hakkar (62-64) and the phantom-combat
    // hatch (65), neither of which can contend — Hakkar is another map, and the
    // hatch only fires when nothing is fightable at all.
    //
    // Inert on every map without a drivesInCombat event, which is all of them bar
    // Black Morass — the trigger resolves the flag before it fires.
    inline constexpr float EventDueCombat         = 61.0f; // leader: wave-encounter event driver
    // Registered in BOTH engines (like BreakStuckCombat / HazardVacate). The combat
    // registration is the working one; the NON-combat registration is a liveness net.
    // Every watchdog the maneuver owns — tag-leg and return-leg timeouts, the CC
    // abort, the arrive-at-camp release — is evaluated inside the action, so they
    // only tick while the action runs. An LOS-break pull ends with the tank unable to
    // see its own target, which is InvalidTargetValue's out-of-LOS clause, so stock
    // `drop target` (99) can move the tank to the non-combat engine mid-drag and
    // freeze the FSM in a holding phase with no clock running (Deadmines workshop,
    // tp-20260815-162044-2: Returning pinned 130-215s). 60 also clears the non-combat
    // ladder it lands in — above HazardVacate (55), below BreakStuckCombat (65).
    inline constexpr float PullManeuver           = 60.0f; // leader: drag the pack back to camp
    inline constexpr float StayAtCamp             = 60.0f; // follower: pin at camp (role peer of PullManeuver)
    // Survival: move OUT of an active-vacate hazard's pulse. Two shapes, one rung.
    // The Arcatraz "Destroyed Sentinel" (21761) is summoned on a Sentinel's death
    // at the corpse, is NOT_SELECTABLE (can't be fought), and pulses ~563-937 every
    // second in 15yd until it despawns. Scholomance's "Cloud of Disease" (17742) is
    // the same problem one tier down and with no creature at all — a persistent
    // area aura dropped where a Diseased Ghoul dies, 350/s in 5yd for 20s.
    // Either way the party is standing right on it after the kill and
    // nothing else moves them off, so they die where they stand. This drives EVERY
    // bot (no tank exemption — there is nothing to tank) out of the pulse, after
    // which normal driving advances them past it. Registered in BOTH engines because the summon
    // ticks after the kill, often once combat has already dropped. Outranks stock
    // MoveChase (~30), the DC role repositions (heal 41 / assist 35 / regroup 29),
    // and the whole non-combat driving ladder (advance 15, rest, loot). Kept BELOW
    // the combat camp owners (60) and Hakkar (62-64), which never contend (the
    // summon isn't a fight; different map than Hakkar), and BELOW the terminal
    // death/chat bailouts (100). See DungeonClearHazardVacate{Trigger,Action}.
    inline constexpr float HazardVacate           = 55.0f; // any role: clear an unfightable hazard's pulse
    inline constexpr float AssistCampCombat       = 35.0f; // follower: onto the leader's pack
    // Leader-only, combat side of the KillCreature-engage objective. A stealthed
    // sapper (Shattered Halls' Shattered Hand Assassins) flags the party into combat
    // and re-stealths — stock combat then has no detectable victim and the run
    // wedges. This drives EngageDirect BY ENTRY on the undetected creature to break
    // stealth. Above the stock combat movers (MoveChase ~30) so it owns the tick and
    // walks the tank onto the sapper; below the camp owners / assist (35) and Hakkar
    // (62-64) which never contend (role/zone partitioned). Inert the instant the
    // target is detectable — stock combat then owns the kill.
    inline constexpr float ObjectiveEngageCombat  = 34.0f; // leader: break a stealthed sapper's combat
    // Contribution-gated combat regroup (Option B). Fires ONLY when the pure kernel
    // says a follower can't contribute from where it stands (see DcRegroupDecision),
    // so it no longer needs to out-shout the stock movers — it sits BELOW them
    // (ACTION_MOVE / MoveChase ~30) and the stock critical heals (30): anything stock
    // *can* do legitimately wins the tick, and this is the fallback when it can't.
    // 29 collides numerically with AssistCamp (29, NON-combat engine) — an engine-
    // partitioned tie (this rung is combat-only), same class as the other asserted
    // ties; pinned in t/TestRelevanceLadder.cpp. Kept at 29 (not lower) so it stays
    // above idle/default rungs and above rotation casts that isUseful might mis-report
    // during an LOS gap.
    inline constexpr float RegroupCombat          = 29.0f; // follower: contribution-gated reconnect
    // HealReposition (41) also registers in the combat engine, ABOVE AssistCampCombat
    // (35) / RegroupCombat (29) and the stock reach-heal (40), BELOW the camp owners
    // (60). Healer-only, so it never contends with the leader-only camp owners.
}

#endif
