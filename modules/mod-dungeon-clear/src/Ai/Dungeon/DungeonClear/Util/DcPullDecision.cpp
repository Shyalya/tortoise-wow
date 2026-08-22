/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcPullDecision.h"

namespace DcPullDecision
{
    PullVerdict DecidePull(PullObservation const& o)
    {
        // 1. No pull target this tick. A standing verdict survives a TRANSIENT
        //    no-target read (door-veto flicker, long-path mid-rebuild, far-targets
        //    poll boundary): the planner's ShouldDropPullVerdict latch decides
        //    when a continuous loss is real. No standing verdict -> nothing to
        //    protect, keep the Leeroy ladder down.
        if (!o.hasTarget)
        {
            if (!o.hasStandingVerdict)
                return PullVerdict::DropToLeeroy;
            if (o.verdictGraceExpired)
                return PullVerdict::DropToLeeroy;
            return PullVerdict::HoldNoTarget;
        }

        // 2. Never flip the verdict mid-engagement: in combat or any non-Idle pull
        //    phase the standing decision is latched until the fight resolves.
        if (o.inCombat || !o.phaseIdle)
            return PullVerdict::NoOp;

        // 3. Same-pack latch. An Advanced commit is locked for the rest of the
        //    approach (never downgraded). A standing Leeroy / patrol-hold re-checks
        //    only on the throttle window, so the verdict can UPGRADE to Advanced
        //    (room changed) without flapping every tick.
        if (o.sameTarget)
        {
            if (o.currentlyAdvanced)
                return PullVerdict::NoOp;       // committed Advanced — locked
            if (!o.recheckElapsed)
                return PullVerdict::NoOp;        // throttled — hold standing decision
        }

        // 4. Resolve a fresh classification into a verdict, layering the patrol-wait
        //    gate on top of the plain Leeroy/Advanced decision. Patrol-wait fires
        //    only when ADVANCED is the verdict AND the feature is on: when the ONLY
        //    reason a pack reads ADVANCED is a lone patroller in chain range
        //    (`patrolContended`), a human holds at commit range and waits it out,
        //    or — while still approaching — walks in as a provisional Leeroy rather
        //    than locking the heavier maneuver 35yd out.
        if (o.advanced && o.patrolWaitEnabled)
        {
            if (o.atCommitRange)
            {
                // At the decision point: hold while still contended and the wait
                // budget has not run out; otherwise fall through and commit.
                if (o.patrolContended && !o.patrolWaitExpired)
                    return PullVerdict::PatrolWaitHold;
            }
            else if (o.patrolContended)
            {
                // Still approaching a patrol-contended pack: stay provisional Leeroy
                // and walk in; don't run the wait clock until the decision point.
                return PullVerdict::ApproachAsLeeroy;
            }
        }

        // 5. Commit the classification.
        return o.advanced ? PullVerdict::Advanced : PullVerdict::Leeroy;
    }
}
