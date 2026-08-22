/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_PULL_BRAKE_H
#define _DC_PULL_BRAKE_H

class Player;

// THE INBOUND LEG DIES AT THE FLAG, NOT AT THE NEXT THINK.
//
// Every other brake on a pull walk-in is an AI action, and an AI action is
// necessarily late. The sequence a tank actually experiences when its pack notices
// it is:
//
//   1. the mob's MoveInLineOfSight fires and it engages the tank;
//   2. CombatManager::UpdateOwnerCombatState sets UNIT_FLAG_IN_COMBAT on the tank
//      and calls sScriptMgr->OnPlayerEnterCombat — synchronously, inside the same
//      call, before anything has ticked;
//   3. ...somewhere between 200ms and a second later, the bot THINKS. It is still
//      on the non-combat engine, so the first thing that has to happen is an
//      engine flip, and only after that can DungeonClearPullManeuverAction run and
//      do the thing it has always done first: kill the inbound leg and turn around.
//
// Nothing in the module owns step 2, and the MotionMaster spends the whole of step
// 3 executing whatever destination it was last handed. At ~7.5yd/s that is several
// yards of walking INTO the formation after it has already been noticed, which is
// the "it still runs forward when it enters combat" the player sees. The walk-in's
// own step cap bounds how much leg is in flight; it cannot bound how long the bot
// takes to notice that it should stop.
//
// So take step 2. OnPlayerEnterCombat is the earliest signal that exists — the
// same statement that raises the combat flag — and a stop issued there lands before
// the bot's own AI is aware there is anything to stop.
namespace DcPullBrake
{
    // Kill an in-flight pull walk-in the instant `bot` is flagged in combat.
    //
    // A ONE-SHOT STOP, never a pin. It cancels the leg that nobody is left to
    // cancel; it does not hold the bot still. Any action that wants to move on the
    // next tick — the drag-back, a re-plan, stock combat — simply issues a move, and
    // this is not called again until the bot leaves combat and re-enters it.
    //
    // Scoped to bots whose OWN pull FSM is mid-walk-in (DcPullPhase::Advancing),
    // which is leader-owned state, so a follower, a bot on another job, and a human
    // driving their own character all fall out without a special case.
    void OnEnterCombat(Player* bot);
}

#endif  // _DC_PULL_BRAKE_H
