/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSTRANDEDRECOVERY_H
#define _PLAYERBOT_DCSTRANDEDRECOVERY_H

class Player;

// Engine glue for the stranded-member recovery failsafe (see DcStrandedDecision.h
// for the kernel). This is the only place the feature touches game objects: it
// resolves the leader tank, maintains the run's no-progress clock on the leader's
// DcRunState, reads the settings via DcSettings, runs the pure kernel over a
// same-map group snapshot, and — when the verdict says so — teleports the stuck
// bot member(s) to the tank and re-arms the clock.
//
// LEADER-OWNED, unlike the dead-tolerant rez recovery: the failsafe teleports
// members TO the tank, so the tank (the alive, elected leader) is the natural and
// only owner of the clock. Evaluate is called on every bot by the recovery
// trigger but no-ops on non-leaders, so the leader's DcRunState is the single
// clock-writer site (the same single-threaded cross-bot access the other run-
// state clocks rely on). A dead leader is a wipe/rez situation, not a stranded
// one — the rez recovery owns that.
//
// PROGRESS SIGNAL mirrors the test-harness livelock net (DcTestRunJob): the clock
// re-stamps whenever a boss/objective completes or the tank closes on the next
// anchor, and combat re-arms it wholesale (a fight is progress) so neither a long
// boss fight nor a slow rest ever trips the failsafe. Only a genuine freeze — the
// tank parked at a spread gate it can never satisfy because a member fell under
// the world — lets the clock run out.
namespace DcStrandedRecovery
{
    // DungeonClear.StrandedRecovery, per-run override -> conf -> default.
    bool Enabled(Player* bot);

    // Tick the leader's no-progress clock and evaluate the failsafe for `bot`'s
    // run. Callable by any bot every tick; no-ops (returns false) unless `bot` is
    // the elected leader of an enabled, unpaused run. Maintains the progress clock
    // on the leader's DcRunState as a side effect. Returns true only when a
    // teleport should fire THIS tick — out of combat, the no-progress window
    // elapsed, and at least one bot member stranded out of range.
    bool Evaluate(Player* bot);

    // Teleport every stranded bot member to the leader tank (fanned out so they
    // don't stack), drop their stale follow splines, re-arm the clock, and
    // announce. Called by the action after Evaluate returned true. Safe to call
    // on any bot; no-ops unless `leader` is the elected leader.
    void Recover(Player* leader);
}

#endif  // _PLAYERBOT_DCSTRANDEDRECOVERY_H
