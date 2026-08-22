/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DC_MOVEMENT_H
#define _PLAYERBOT_DC_MOVEMENT_H

#include "MoveSplineInitArgs.h"
#include "Ai/Base/Value/LastMovementValue.h"

class Player;
class PlayerbotAI;

// DcMovement is the single funnel through which all DungeonClear code issues or
// stops movement. It owns the two facts that otherwise have to be re-remembered
// at every call site (see the nav/pull review, finding #4):
//
//   1. A plain Unit::StopMoving() does NOT cancel a launched escort-spline
//      glide (the EscortMovementGenerator keeps driving the bot down the old
//      route). Cancelling one needs the escort-aware StopActiveSplineGlide
//      incantation (kill the ESCORT generator + zero the LastMovement wait), and
//      a hold against an in-flight glide additionally needs StopMovingOnCurrentPos.
//   2. The engine queues actions from triggers evaluated at tick start, so an
//      action can execute AFTER the state that should have gated it changed (the
//      pause race). Movement issuance re-checks the paused flag HERE so the
//      whole queued-action race class is killed in one place.
//
// Call sites state intent (Soft / Hold / HardPin, or "move there"); the
// mechanism lives here. The MoveTo funnel proper lives on DcMovementAction (a
// thin MovementAction subclass) because MovementAction::MoveTo is protected;
// the free functions below cover everything that does not need that protected
// method.
namespace DcMovement
{
    // Stop strengths, by intent.
    enum class Stop
    {
        // Settle in place; no-op if already standing. Replaces the bare
        // "if (isMoving()) StopMoving()" dwell stops. NOT escort-aware: only
        // use where no escort glide can be in flight.
        Soft,

        // Hold position against an in-flight escort glide (= the old HaltForHold):
        // kill the ESCORT spline, zero the LastMovement wait, and
        // StopMovingOnCurrentPos. Also tears down a persistent FOLLOW generator
        // for followers (the selfbot-stale-MoveFollow class). Idempotent per
        // tick, so per-tick hold calls do not spam stop packets.
        Hold,

        // Hold + unconditionally pin a queued point-move sitting under an active
        // CC / higher-priority generator (= the CC-abort / turn-and-plant
        // incantation). Unlike Hold it does not early-out on a standing bot,
        // because under CC the bot is not "moving" yet a MOVEMENT_COMBAT MoveTo
        // is still queued and would resume the instant the impairment clears.
        HardPin,
    };

    // Stop the bot at the requested strength.
    void StopBot(Player* bot, Stop strength);

    // Drop the "I am still travelling" wait recorded by the last movement, WITHOUT
    // stopping the bot or touching any generator.
    //
    // MovementAction::MoveTo records a LastMovement delay sized to the leg's travel
    // time, and MovementAction::IsWaitingForLastMove refuses any later move whose
    // priority is not STRICTLY GREATER than the recorded one. Everything the pull
    // issues is MOVEMENT_COMBAT, so one leg's leftover wait silently refuses the
    // NEXT leg for the remainder of its budget — the tank stands in the pack it
    // just aggroed instead of turning and running home. Stop::Hold / Stop::HardPin
    // already zero it as a side effect, but they also stop the bot, and Hold
    // early-outs on a bot that is standing still, which is exactly the case that
    // needs clearing. This is the seam for "I am replacing the leg, not halting
    // it": call it immediately before issuing the replacement move.
    void ClearMovementWait(Player* bot);

    // Issue the upcoming polyline as ONE EscortMovementGenerator spline (the
    // continuous glide that replaces per-point stops). Absorbs the issuance
    // ritual that was hand-duplicated at the advance, swim, and pull-maneuver
    // sites: stand up, interrupt any cast, MoveSplinePath, then record a
    // NORMAL-priority LastMovement sized to the window travel time (for priority
    // arbitration only — the re-issue guards key off splineRunning, not this
    // delay). `pts` must hold the live position at [0] and at least one more
    // point. Returns true iff the spline was issued (pts.size() >= 2).
    bool SplinePath(PlayerbotAI* botAI, Movement::PointsArray& pts,
                    MovementPriority recordPrio = MovementPriority::MOVEMENT_NORMAL);

    // The pause/teardown gate. False once the run is paused, so movement issuance
    // bails (the queued-action race fix). Exposed for the MoveTo funnel and for
    // the few actions that bail before doing non-movement work too.
    bool DcMovementAllowed(PlayerbotAI* botAI);

    // Cancel a stale escort-spline glide. No-op unless an ESCORT glide is
    // actually in flight (so it never perturbs the LastMovement wait at sites
    // with no glide). Folded into DcMovementAction::DcMoveTo (so a point move is
    // never left coasting under an old glide); also called directly at the bare
    // glide-kill sites that drop a stale glide before a non-move (swim hand-back,
    // posStuck rebuild, off-path resnap).
    void ResolveEscortConflict(Player* bot);
}

#endif
