/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearStrategy.h"

#include "Ai/Dungeon/DungeonClear/Multiplier/DungeonClearMultiplier.h"
#include "Ai/Dungeon/DungeonClear/Strategy/DcRelevance.h"
#include "Playerbots.h"

void DungeonClearStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Highest priority: bail out on death.
    triggers.push_back(new TriggerNode(
        "dungeon clear party died",
        { NextAction("dungeon clear disable on death", DcRel::PartyDied) }));

    // All bosses cleared — congratulate and disable.
    triggers.push_back(new TriggerNode(
        "dungeon clear all cleared",
        { NextAction("dungeon clear disable on cleared", DcRel::AllCleared) }));

    // Stranded-member recovery failsafe (leader-only, non-combat). When the run
    // has frozen for the configured window with a bot member stuck out of range
    // (fell under the world / wedged), teleport the strays to the tank. Relevance
    // 42 sits above the whole leader driving ladder — which, by definition, has
    // been failing to progress for minutes — so the rescue wins the tick its
    // narrow trigger arms; it is inert otherwise. See DungeonClearRecoverStranded
    // Trigger / DcStrandedRecovery.
    triggers.push_back(new TriggerNode(
        "dungeon clear recover stranded",
        { NextAction("dungeon clear recover stranded", DcRel::StrandedRecovery) }));

    // Post-combat rez driver. Registered for ALL bots (the elected rezzer may
    // be a follower or the leader itself — a prot paladin raising its healer);
    // the trigger fires only on the one deterministically-elected rezzer.
    // Relevance 31.5 outranks the leader's event/boss drivers (31/30) and every
    // follower rung (<= 29) so the rezzer walks to the corpse instead of
    // following/holding/pulling; the between-pulls + event-rest IsPending gates
    // (DcRezRecovery) hold everyone else. See DungeonClearRezPartyTrigger.
    triggers.push_back(new TriggerNode(
        "dungeon clear rez party",
        { NextAction("dungeon clear rez party", DcRel::RezParty) }));

    // Advanced pull (LOS pull-to-camp). Sits ABOVE the engage triggers so, when
    // pull mode is on, the tank runs the pull-to-camp maneuver instead of the
    // normal walk-in — but it is trash-only (the pull-target scan vetoes dungeon
    // bosses outright, and the trigger additionally stands down inside boss
    // engage range), so the at-boss engage below still owns boss pulls. Inert
    // when pull mode is off. See DungeonClearPullTrigger / DungeonClearPullAction.
    triggers.push_back(new TriggerNode(
        "dungeon clear pull",
        { NextAction("dungeon clear pull", DcRel::Pull) }));

    // Off-path CONDITIONAL event due (DungeonEventRegistry): a pre-boss gate the
    // party must perform — pull a lever, talk to a prisoner to open the gate, etc.
    // Relevance 31, just above the at-boss pull (30), so a due gate preempts the
    // boss engage AND the door-blocked stall (22). Inert unless a conditional
    // event's condition is currently true and un-latched. See
    // DungeonClearEventDueTrigger / DcRunEventAction.
    triggers.push_back(new TriggerNode(
        "dungeon clear event due",
        { NextAction("dungeon clear run event", DcRel::EventDue) }));

    // Within engage range of next boss.
    triggers.push_back(new TriggerNode(
        "dungeon clear at boss",
        { NextAction("dungeon clear engage boss", DcRel::AtBoss) }));

    // Sunken Temple (map 109) Avatar of Hakkar encounter handlers. These live in
    // BOTH strategies: here for the brief out-of-combat gaps between waves, and
    // (the important copy) in DungeonClearCombatStrategy so they actually run mid-
    // fight — the whole encounter is a wave fight, so a bot is almost always in
    // combat, and the previous non-combat-only registration was why the flames
    // never got doused (the win path) and the fight just timed out. Inert
    // everywhere but the live Sanctum (the triggers gate on it).
    //
    // Priority: suppressor (36) > flame (35.5) > loot blood (34). Values in DcRel;
    // flame sits at 35.5 so on the carrier a douse also outranks starting a Pull (35).
    //  - Suppressor first: a Nightmare Suppressor left channelling RESETS the
    //    event; merely tagging it (its drain is an OOC channel) silences it.
    //  - Flame ABOVE loot blood: once a bot HOLDS blood, dousing makes progress
    //    toward the 4/4 that spawns the Avatar — it must not keep grabbing more
    //    blood (the old loot>flame order starved the douse, the reported "trouble
    //    getting priority to extinguish the flames"). The flame trigger only fires
    //    when the bot already carries blood, so a bot WITHOUT blood still loots.
    triggers.push_back(new TriggerNode(
        "dungeon clear hakkar suppressor",
        { NextAction("dungeon clear hakkar suppressor", DcRel::HakkarSuppressor) }));
    triggers.push_back(new TriggerNode(
        "dungeon clear hakkar flame",
        { NextAction("dungeon clear hakkar flame", DcRel::HakkarFlame) }));
    triggers.push_back(new TriggerNode(
        "dungeon clear hakkar loot blood",
        { NextAction("dungeon clear hakkar loot blood", DcRel::HakkarLootBlood) }));

    // Arrived at a travel OBJECTIVE (BossRosterRegistry, non-combat anchor).
    // Peer of at-boss (30) — mutually exclusive via the anchor-kind check in
    // each trigger — so the objective is completed and the clear advances
    // instead of trying to engage a non-creature target.
    triggers.push_back(new TriggerNode(
        "dungeon clear at objective",
        { NextAction("dungeon clear objective arrive", DcRel::AtObjective) }));

    // Blocking trash on the path to the next boss.
    triggers.push_back(new TriggerNode(
        "dungeon clear blocking trash",
        { NextAction("dungeon clear engage trash", DcRel::BlockingTrash) }));

    // Room-wide-aggro boss pre-clear (RoomAggroRegistry): at a flagged boss, clear
    // the room before the boss is pulled. Relevance 26 — between engage-trash (25)
    // and engage-boss (30) — so it preempts the (stood-down) corridor scan and the
    // boss pull is held by the at-boss gate until the room is clear. This node is
    // the Off / Dynamic-chose-Leeroy path (walk in and tank in place); when
    // pull-to-camp is in effect the pull pipeline (35) owns the room clear instead.
    triggers.push_back(new TriggerNode(
        "dungeon clear room trash",
        { NextAction("dungeon clear room clear", DcRel::RoomTrash) }));

    // Stalled fallback: only fires when Advance/EngageBoss has set a stall
    // reason because no path to the next boss exists. Sits above the default
    // advance (15) so the fallback kill wins, and below at-boss (30) and
    // blocking-trash (25) so a viable boss/trash pull still preempts.
    triggers.push_back(new TriggerNode(
        "dungeon clear stalled",
        { NextAction("dungeon clear clear stalled", DcRel::Stalled) }));

    // Door blocking the corridor: stall with a specific message. Sits
    // above advance (15) but below the engage triggers so a hostile in the
    // doorway still gets pulled first; otherwise the bot stops and waits
    // for the door to be opened.
    triggers.push_back(new TriggerNode(
        "dungeon clear door blocked",
        { NextAction("dungeon clear door blocked", DcRel::DoorBlocked) }));

    // LEADER-only: a groupmate is fighting a pack the tank never saw (a follower
    // aggroed around a sharp corner, or the tank called the pull done and walked
    // off toward the next objective) — so rather than freezing on the Advance rest
    // gate ("party not ready / resting") while the DPS fight without it, the tank
    // goes back and takes threat. Relevance 24: above advance (15), the stalled
    // fallback (20) and door-blocked (22) — all of which would otherwise leave the
    // tank stranded while the party fights — but BELOW the tank's own engage scans
    // (engage trash 25, room trash 26, engage boss 30), so a deliberate visible
    // pull always wins and this only fills the out-of-sight gap. Inert for
    // followers (their IsLeaderFightAssistWanted path owns them) and the instant
    // the tank sees a target of its own. See DcLeaderSignal::IsLeaderShouldAssistFight.
    triggers.push_back(new TriggerNode(
        "dungeon clear leader assist",
        { NextAction("dungeon clear leader assist", DcRel::LeaderAssist) }));

    // Auto-resume once a player opens the door we auto-paused at. Fires only
    // while paused for that specific door (see DungeonClearDoorReopenedTrigger),
    // when the rest of the driving ladder is inert, so its relevance only has to
    // clear the stock wander/idle actions — keep it high so nothing preempts the
    // resume.
    triggers.push_back(new TriggerNode(
        "dungeon clear door reopened",
        { NextAction("dungeon clear door reopened", DcRel::DoorReopened) }));

    // Room pre-clear OWNER (fix #2). Active for the whole pre-clear window (flagged
    // room-aggro boss, trash still up, tank at the standoff). Relevance 16 — just
    // ABOVE the default Advance (15) and BELOW every real driver (engage/door/stall/
    // assist 20-35) — so whenever no higher driver claims the tick it HOLDS the tank
    // at the standoff instead of letting the room-aggro-blind Advance creep at the
    // boss centre. This is the structural close for the recurring "boss woken
    // mid-clear" failures: the standoff is owned every gap, not just when Advance's
    // own conditional engage-hold rung happens to fire.
    triggers.push_back(new TriggerNode(
        "dungeon clear room preclear hold",
        { NextAction("dungeon clear room preclear hold", DcRel::RoomPreclearHold) }));

    // Default: walk toward the next boss. Lowest of the bunch but above
    // grind (4) / new rpg (11). Wander strategies are also suppressed by
    // DungeonClearMultiplier while enabled.
    triggers.push_back(new TriggerNode(
        "dungeon clear idle",
        { NextAction("dungeon clear advance", DcRel::Advance) }));

    // Non-tank bots in the tank's party redirect their follow target to the
    // tank while DC is on. Relevance above the default follow (1.0) so it
    // preempts the usual master-follow behavior.
    triggers.push_back(new TriggerNode(
        "dungeon clear follow tank",
        { NextAction("dungeon clear follow tank", DcRel::FollowTank) }));

    // While the leader is mid-pull, non-leader followers hold passive at the camp
    // instead of trailing the tank into the pull. Relevance above follow-tank (25)
    // so it preempts the trail for the duration of the maneuver; inert otherwise.
    triggers.push_back(new TriggerNode(
        "dungeon clear hold at camp",
        { NextAction("dungeon clear hold at camp", DcRel::HoldAtCamp) }));

    // Leader-fight assist: while the leader tank is in combat, every follower
    // still OUT of combat is driven into the fight — the advanced-pull camp
    // fight, but also any Leeroy/dynamic/boss pull the tank took around a corner
    // or beyond the follower's natural engage range, where group combat never
    // propagates and the stock target picker (LOS-filtered, and multiplier-
    // suppressed anyway) would never acquire it. Relevance above hold-at-camp
    // (28) so it preempts the camp yield, and above the rest triggers (26.5) so
    // "tank is fighting" outranks topping up. Defers to the camp hold during
    // the passive pull phases. See DungeonClearAssistCampTrigger /
    // DcLeaderSignal::IsLeaderFightAssistWanted.
    triggers.push_back(new TriggerNode(
        "dungeon clear assist camp",
        { NextAction("dungeon clear assist camp", DcRel::AssistCamp) }));

    // Healer LOS reposition, NON-COMBAT side. Covers the gap where the healer
    // healed, dropped combat, and the tank then moved out of line of sight while
    // still hurt: the bot would otherwise just follow/idle. Relevance 41 — above
    // follow-tank (25), hold-at-camp (28) and assist (29) so it preempts trailing
    // the tank. NOTE: 41 is ABOVE the pull/hakkar/at-boss drivers here in the
    // non-combat engine too (unlike the combat engine, where the camp owners sit
    // at 60); it does not contend only because this trigger is HEALER-only and
    // those drivers are LEADER-only — a role partition, asserted in the ladder
    // test. Same trigger as the combat side; it defers during passive camp holds.
    // See DungeonClearHealRepositionTrigger.
    triggers.push_back(new TriggerNode(
        "dungeon clear heal reposition",
        { NextAction("dungeon clear heal reposition", DcRel::HealReposition) }));

    // Phantom-combat escape hatch, NON-COMBAT side — and this is the half that
    // matters, because the state it recovers is one the bot can only be in HERE.
    //
    // Engine transitions are action-driven, not derived from IsInCombat: stock
    // `drop target` (CombatStrategy, relevance 99 — above everything DC owns) fires
    // on an invalid/dead/out-of-LOS target and runs ChangeEngine(BOT_STATE_NON_COMBAT)
    // + AttackStop(), and it does NOT clear the core combat flag. Nothing puts the
    // bot back: PlayerbotAI::DoNextAction only nulls `current target` when it finds a
    // flagged bot on the non-combat engine, and the two ChangeEngine(BOT_STATE_COMBAT)
    // call sites are AttackAction / PullActions — both zeroed by DungeonClearMultiplier
    // for the whole duration of a run.
    //
    // So a flagged bot can sit on the non-combat engine forever, and DC's rungs are
    // partitioned by the FLAG: the non-combat rungs bail on IsInCombat(), the combat
    // rungs live on an engine it has left. Nothing is live on either side — including
    // this hatch, which was registered in the combat strategy only and so was blind
    // in exactly the state it exists for.
    //
    // Live (tr-20260803-154419-18): tank, healer and hunter dropped off the combat
    // engine at Selin's camp fight and stood flagged with no attackers, no victims and
    // full health for seven minutes; the two members that were never flagged kept
    // running normally beside them. The signature (flagged, no victim, pull phase
    // non-Idle >90s) is on 16 of 829 recorded runs across five dungeons, eight of
    // which burned the full no-progress watchdog.
    //
    // Clearing the flag is enough to unwind all of it: IsInCombat() goes false and the
    // ordinary non-combat rungs — including the pull FSM's own Engage cleanup — become
    // reachable again on their existing gates. Same node as the combat side; all the
    // guards (no attackers, no victim, no legitimate holder, sustained
    // StuckCombatTimeout, never in a raid) live in the trigger, so registering it here
    // only widens WHERE it can see, not WHEN it fires.
    triggers.push_back(new TriggerNode(
        "dungeon clear break stuck combat",
        { NextAction("dungeon clear break stuck combat", DcRel::BreakStuckCombat) }));

    // Pull maneuver, NON-combat side — a LIVENESS NET, not a second driver.
    //
    // Every watchdog the maneuver owns — the tag-leg and return-leg timeouts, the
    // CC abort, the turn-and-plant debounce, the arrive-at-camp release — is
    // evaluated INSIDE DungeonClearPullManeuverAction::Execute. So they only exist
    // on ticks where that action actually runs, and while the trigger was
    // combat-only that meant "only while the tank is on the combat engine". The
    // hatch above recovers a bot with NOTHING to fight; this covers the other half,
    // where the fight is entirely real and the tank has simply been moved off the
    // engine that steers it.
    //
    // Which is exactly what an LOS-break pull does to itself: the camp is chosen so
    // the tank cannot see the mob it tagged, and that is InvalidTargetValue's
    // out-of-LOS clause, so stock `drop target` (99) flips the tank to this engine
    // mid-drag. DungeonClearCombatMultiplier now suppresses that drop, but the
    // suppression is a prevention and this is the backstop: ANY future path onto the
    // non-combat engine mid-maneuver would otherwise re-freeze the FSM in a holding
    // phase with no clock running at all. Live before the fix
    // (tp-20260815-162044-2, Deadmines workshop): phase pinned at Returning for
    // 130-215s across three runs, zero log lines emitted, party passive at camp,
    // nobody below 100% HP — a 10s return-leg watchdog sat unreachable the whole time.
    //
    // Contention: none. The non-combat pull DRIVER ("dungeon clear pull", 35) gates
    // on !IsInCombat for every non-Idle phase and this trigger requires
    // IsInCombat(), so the two are partitioned by the combat flag and can never be
    // armed on the same tick. 60 sits above HazardVacate (55) — a maneuver in
    // flight outranks stepping off a pulse — and below BreakStuckCombat (65), which
    // is inert whenever anything is fightable. Same node as the combat side: every
    // gate (leader-only, run enabled, pull mode, holding phase) lives in the
    // trigger, so this widens WHERE the maneuver can be ticked, not WHEN.
    triggers.push_back(new TriggerNode(
        "dungeon clear pull maneuver",
        { NextAction("dungeon clear pull maneuver", DcRel::PullManeuver) }));

    // Hazard vacate, NON-combat side — the essential half. After the party kills
    // an Arcatraz Sentinel, the Destroyed Sentinel (21761) summon pulses 15yd/1s
    // at the corpse and combat usually drops (it is NOT_SELECTABLE, so it does not
    // hold anyone in combat) — so the bot idles ON the corpse in the non-combat
    // engine, taking the pulse, until this walks it clear. Relevance 55 outranks
    // the whole non-combat driving ladder (advance 15, rest, loot, follow) so the
    // bot leaves before it loots/regroups on the death spot. Same node runs in the
    // combat strategy for the case combat is still up. See
    // DungeonClearHazardVacateTrigger.
    triggers.push_back(new TriggerNode(
        "dungeon clear hazard vacate",
        { NextAction("dungeon clear hazard vacate", DcRel::HazardVacate) }));

    // Rest-target override: top up to the run's chosen HP/mana before pulling.
    // Relevance 26.5 (DcRel::NeedsRest) — above advance (15) and follow-tank (25)
    // so a bot below target sits and rests instead of walking, and tie-broken just
    // ABOVE room-trash (26) so a leader tops up before committing to a room
    // pre-clear; safely below the engage triggers, which can't fire anyway while
    // the party is still recovering (the rest gate uses the same target). Only
    // active when the run sets RestHealthPct / RestManaPct; otherwise the triggers
    // are inert and stock rest is unchanged.
    triggers.push_back(new TriggerNode(
        "dungeon clear needs drink",
        { NextAction("drink", DcRel::NeedsRest) }));
    triggers.push_back(new TriggerNode(
        "dungeon clear needs eat",
        { NextAction("food", DcRel::NeedsRest) }));

    // Keep the DC loot policy (quality floor / IgnoreChests) enforced for the
    // WHOLE party while the run is PAUSED — leader and followers alike. The
    // driving ladder above is inert when paused and followers stop trailing the
    // leader, so without this every member reverts to the stock playerbots loot
    // pipeline and loots everything (and follower junk-looting stalls the tank
    // via IsAnyPartyMemberLooting). Relevance 9 sits just above the stock loot
    // actions (open loot is 8) so the filter prunes before they pick up; the
    // action returns false so the surviving loot is still collected this tick.
    // Inert unless paused — when active the same filter runs inline in
    // advance/follow-tank. See DungeonClearFilterLootTrigger.
    triggers.push_back(new TriggerNode(
        "dungeon clear filter loot",
        { NextAction("dungeon clear filter loot", DcRel::FilterLoot) }));

    // BetterLootRolling improvement #3: roll the moment a loot-roll window
    // opens. Stock only reaches "loot roll" off the "very often" RandomTrigger
    // (1-in-3 at most every RepeatDelay), leaving bots staring at an open roll
    // for many seconds. This node drives the same action — the
    // BetterLootRollAction override — every tick a vote is pending. The action
    // is instant and resolves one roll per execute, so the trigger self-clears;
    // relevance above the whole driving ladder (door reopened 90) so the vote
    // never queues behind movement, below chat (100). Inert unless
    // DungeonClear.BetterLootRolling is on (see the trigger).
    triggers.push_back(new TriggerNode(
        "dungeon clear loot roll pending",
        { NextAction("loot roll", DcRel::LootRollPending) }));

    // Chat-keyword triggers (`dc on/off/skip/status/bosses` + long aliases).
    // Folded in here so there is a single "dungeon clear" strategy: one name to
    // apply (via config or the login hook), which is what lets self-bots —
    // built from config when `.playerbots bot self` is toggled — pick up the
    // whole feature, keyword listener included. ChatCommandTrigger latches its
    // fired flag until an engine checks it, so a `dc off` typed mid-combat still
    // fires the moment the bot next ticks the non-combat engine.
    constexpr float chatRel = DcRel::Chat;
    triggers.push_back(new TriggerNode("dc on",             { NextAction("dc on",     chatRel) }));
    triggers.push_back(new TriggerNode("dungeon clear on",  { NextAction("dc on",     chatRel) }));
    triggers.push_back(new TriggerNode("dc off",            { NextAction("dc off",    chatRel) }));
    triggers.push_back(new TriggerNode("dungeon clear off", { NextAction("dc off",    chatRel) }));
    triggers.push_back(new TriggerNode("dc skip",           { NextAction("dc skip",   chatRel) }));
    // Single toggle: pauses when running, resumes when paused.
    triggers.push_back(new TriggerNode("dc pause",            { NextAction("dc pause", chatRel) }));
    triggers.push_back(new TriggerNode("dungeon clear pause", { NextAction("dc pause", chatRel) }));
    // Advanced-pull toggle (`dc pull [on|off]`). The keyword trigger keys differ
    // from the engine "dungeon clear pull" trigger to avoid a creator collision:
    // "dungeon clear pull keyword" listens for the chat phrase "dungeon clear pull".
    triggers.push_back(new TriggerNode("dc pull",                  { NextAction("dc pull", chatRel) }));
    triggers.push_back(new TriggerNode("dungeon clear pull keyword", { NextAction("dc pull", chatRel) }));
    triggers.push_back(new TriggerNode("dc status",         { NextAction("dc status", chatRel) }));
    triggers.push_back(new TriggerNode("dc bosses",         { NextAction("dc bosses", chatRel) }));
}

void DungeonClearStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new DungeonClearMultiplier(botAI));
}

void DungeonClearCombatStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Phantom-combat escape hatch (ANY member). A bot left flagged in combat by a
    // mob that spawned far away / behind a gate — nothing meleeing it, no victim, no
    // reachable holder — force-clears its combat after DungeonClear.StuckCombatTimeout
    // seconds, breaking the deadlock a `dc off`/`on` cannot (the flag lives in the
    // core CombatManager). Relevance 65 — the top of the combat band — so the
    // recovery always wins the tick when it legitimately fires; it is inert whenever
    // anything is fightable, so it never contends with a real fight or a scripted
    // wave. See DungeonClearBreakStuckCombatTrigger.
    triggers.push_back(new TriggerNode(
        "dungeon clear break stuck combat",
        { NextAction("dungeon clear break stuck combat", DcRel::BreakStuckCombat) }));

    // The in-combat half of the advanced pull: once the tank aggros, run it back
    // to the camp before releasing the party. Relevance above the stock combat
    // movement/attack actions (MoveChase ~30, attack lines) so the maneuver owns
    // the tick and the tank doesn't fight at the pack. Inert unless the bot is the
    // leader and mid-pull (see DungeonClearPullManeuverTrigger).
    triggers.push_back(new TriggerNode(
        "dungeon clear pull maneuver",
        { NextAction("dungeon clear pull maneuver", DcRel::PullManeuver) }));

    // Combat-engine hold for held FOLLOWERS. A held follower enters combat the
    // instant the tank aggros (group combat) and switches to this engine, where
    // the non-combat hold-at-camp can't run and PassiveMultiplier explicitly
    // permits stock "follow" while the bot is +passive — so without this the
    // party trails the tank the moment a pull starts (the "passive isn't enough"
    // bug). The action name contains "stay" so PassiveMultiplier's substring
    // whitelist lets it through; relevance above the stock combat movers (follow
    // 1.0, move-from-group 1.0, MoveChase ~30) so it owns the tick and pins the
    // follower at camp. Inert at Engage (not a holding phase), so the released
    // party fights normally. Leader-exempt via the trigger.
    triggers.push_back(new TriggerNode(
        "dungeon clear stay at camp",
        { NextAction("dungeon clear stay at camp", DcRel::StayAtCamp) }));

    // Leader-fight assist, combat-engine side. A follower that was dragged into
    // combat (group combat / stray hit) but has the pack around a corner has an
    // empty LOS attacker list and so idles in the combat engine — stock
    // MoveChase/attack have no target. Drive it onto the leader's pack to
    // regain sight; fires for ANY leader fight, not just the camp fight. Relevance
    // above the stock combat movers (MoveChase ~30) so it owns the tick; inert
    // the instant a valid attacker is visible, handing back to stock combat. Sits
    // below stay-at-camp / pull-maneuver (60), which are inert at Engage anyway.
    triggers.push_back(new TriggerNode(
        "dungeon clear assist camp combat",
        { NextAction("dungeon clear assist camp combat", DcRel::AssistCampCombat) }));

    // LEADER-only: combat-side driver for the KillCreature-engage objective. A
    // stealthed sapper (Shattered Halls' Shattered Hand Assassins) can Sap the tank,
    // flag the party into combat and stay stealthed — stock combat then has no
    // detectable victim and the run wedges. This drives EngageDirect BY ENTRY on the
    // undetected assassin to break stealth. Relevance 34 — above the stock combat
    // movers (MoveChase ~30) so it owns the tick and walks the tank onto the sapper,
    // below the camp owners / assist (35, follower-only) and Hakkar (62-64). Inert
    // the instant the target is detectable (stock combat resumes the kill). See
    // DungeonClearObjectiveEngageCombatTrigger.
    triggers.push_back(new TriggerNode(
        "dungeon clear objective engage combat",
        { NextAction("dungeon clear objective engage combat", DcRel::ObjectiveEngageCombat) }));

    // In-combat regroup for FOLLOWERS (contribution-gated, Option B): reconnect a
    // follower to the fight ONLY when the pure kernel says it can't contribute from
    // where it stands (a DPS with no visible attacker, a healer that can't heal the
    // tank yet), driving it to a role-correct standoff point with LOS. Relevance is
    // now BELOW the stock combat movers (MoveChase ~30) and stock critical heals
    // (30) — the OPPOSITE of the old distance-tether rung: it fires only when stock
    // movement has no target to chase, so anything stock can do legitimately wins
    // the tick. Also below the camp actions (assist 35, stay-at-camp / pull-maneuver
    // 60), which own positioning during an advanced-pull camp where this stands down
    // anyway. 29 ties AssistCamp (29) but that runs only in the NON-combat engine, so
    // they never contend. Inert the instant the follower can contribute again. See
    // DungeonClearRegroupCombatTrigger + DcRegroupDecision.
    triggers.push_back(new TriggerNode(
        "dungeon clear regroup combat",
        { NextAction("dungeon clear regroup combat", DcRel::RegroupCombat) }));

    // Healer LOS reposition, COMBAT side. The real fix for the stranded-healer
    // bug: a healer whose hurt heal target (usually the tank) was dragged out of
    // line of sight walks back into a spot it can heal from. Relevance 41 — above
    // the stock `reach party member to heal` (ACTION_CRITICAL_HEAL+10 = 40, which
    // reads the LOS-filtered `party member to heal` and so can't chase an
    // out-of-sight target), above DC assist (35) / regroup (33), and below the
    // camp owners (stay-at-camp / pull-maneuver 60). Arbitration against in-LOS
    // heals is done in the trigger (it defers to a visible hurt member), not by
    // relevance, so this never steals a tick from a real heal. See
    // DungeonClearHealRepositionTrigger.
    triggers.push_back(new TriggerNode(
        "dungeon clear heal reposition",
        { NextAction("dungeon clear heal reposition", DcRel::HealReposition) }));

    // Survival: move out of an unfightable hazard's pulse — the Arcatraz Destroyed
    // Sentinel (21761) summoned at a Sentinel's corpse, pulsing 15yd/1s until it
    // despawns. Fires for EVERY bot in the pulse (nothing to tank — it is
    // NOT_SELECTABLE). Relevance 55 — above every stock combat mover (MoveChase
    // ~30) and the DC role repositions (heal 41 / assist 35 / regroup 29) so it
    // owns the tick, below the camp owners (60) and Hakkar (62-64) which never
    // contend. Also registered in the NON-combat strategy, because the summon
    // ticks after the kill once combat has dropped. See
    // DungeonClearHazardVacateTrigger.
    triggers.push_back(new TriggerNode(
        "dungeon clear hazard vacate",
        { NextAction("dungeon clear hazard vacate", DcRel::HazardVacate) }));

    // Conditional-event driver, COMBAT side. Fires only for an event that opted in
    // with DrivesInCombat() — a continuous WAVE encounter where the party is in
    // combat from the first pull to the last, so the non-combat copy (relevance 31)
    // runs only in the gaps between waves and stops running at all once the party
    // falls behind and combat stops dropping.
    //
    // Black Morass is why this exists. Its 18 rifts each pump one add every 15s
    // until their KEEPER dies — killing the keeper is the only shutoff — and with
    // two rifts open the party never left combat, so nothing ever walked the tank
    // to a portal and no rift ever closed. Same failure the Hakkar handlers below
    // hit, generalised onto the events framework instead of hand-copied per
    // encounter. See DungeonEvent::drivesInCombat / DcRel::EventDueCombat.
    triggers.push_back(new TriggerNode(
        "dungeon clear event due combat",
        { NextAction("dungeon clear run event combat", DcRel::EventDueCombat) }));

    // Sunken Temple Avatar of Hakkar orchestration, COMBAT side — THE place these
    // run. The encounter is a continuous wave fight, so every member is in combat
    // almost the whole time; with the handlers only in the non-combat strategy
    // (their original home) the win path never executed — the flames stayed 0/4
    // and the WaitForSpawn(Avatar) step just timed out (the "very long fight").
    // Relevance ABOVE every stock combat mover/attack (MoveChase ~30) and the DC
    // camp/regroup rungs so the carrier actually peels mid-fight to silence a
    // resetting suppressor / douse a flame / grab blood. Same suppressor > flame >
    // loot-blood order as the non-combat copy (flame above loot so a blood carrier
    // douses instead of hoarding). All inert outside the live Sanctum.
    triggers.push_back(new TriggerNode(
        "dungeon clear hakkar suppressor",
        { NextAction("dungeon clear hakkar suppressor", DcRel::HakkarSuppressorCombat) }));
    triggers.push_back(new TriggerNode(
        "dungeon clear hakkar flame",
        { NextAction("dungeon clear hakkar flame", DcRel::HakkarFlameCombat) }));
    triggers.push_back(new TriggerNode(
        "dungeon clear hakkar loot blood",
        { NextAction("dungeon clear hakkar loot blood", DcRel::HakkarLootBloodCombat) }));
}

void DungeonClearCombatStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    // The one combat-engine multiplier: it suppresses ONLY the stock "drop target"
    // for a follower closing on the tank's out-of-LOS fight, so the flip-early assist
    // can hold the combat engine instead of ping-ponging back out. See
    // DungeonClearCombatMultiplier.
    multipliers.push_back(new DungeonClearCombatMultiplier(botAI));
}
