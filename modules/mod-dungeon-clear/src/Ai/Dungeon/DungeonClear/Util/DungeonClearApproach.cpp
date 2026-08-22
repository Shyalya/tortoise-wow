/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearApproach.h"

namespace DungeonClearApproach
{
    Verdict DecideApproach(Observation const& o)
    {
        // The ladder, in the exact order DungeonClearAdvanceAction::Execute walks
        // its tail. Each guard mirrors one Try* phase's entry condition.

        // 1. Position-stuck recovery. Wedged in place for stuckTickLimit ticks.
        if (o.posStuckTicks >= o.stuckTickLimit)
            return Verdict::StuckRecover;

        // 2. Direct pursuit of a live, visible boss — unless the give-up latch
        //    has tripped (then we let the long-path drive uninterrupted).
        if (o.canPursue && o.pursuitFailTicks < o.pursuitFailLimit)
            return Verdict::Pursue;

        // 3. No navigable long-path. An async build still in flight is EXPECTED
        //    (hold quietly); else try an off-mesh nudge, then a swim, then stall.
        if (!o.pathReachable)
        {
            if (o.asyncPending)
                return Verdict::PlanRouteWait;
            if (o.allowRecoveryMoves && o.startFarFromPoly)
                return Verdict::FarFromPolyRecover;
            if (o.waterBetween)
                return Verdict::Swim;
            return Verdict::Stall;
        }

        // 4. Drifted off the corridor past the tick budget — resnap/rebuild.
        if (o.offPath)
            return Verdict::OffPathRebuild;

        // (The action computes NextHop here; the fields below describe its result.)

        // 5. The route completed. Benign if already in engage range (rebuild and
        //    yield); else escalate dead-end handling: final-approach within the
        //    budget, then a swim, then a stall.
        if (o.hopDone)
        {
            if (o.engageDist < o.engageRange)
                return Verdict::RebuildAndYield;
            if (o.doneNotEngagedTicks < o.doneNotEngagedLimit)
                return Verdict::FinalApproach;
            if (o.waterBetween)
                return Verdict::Swim;
            return Verdict::Stall;
        }

        // 6. Anchor-declared jump leg.
        if (o.hopIsJump)
            return Verdict::JumpLeg;

        // 7. A healthy escort spline is already gliding — leave it alone.
        if (o.splineRunning)
            return Verdict::RideLiveGlide;

        // 8. Physically off the line — rejoin via a generated (wall-following)
        //    path before the straight escort spline can cut a corner.
        if (o.offLine)
            return Verdict::OffLineRejoin;

        // 9. Normal case: launch the upcoming polyline window as one spline.
        if (o.haveSplineWindow)
            return Verdict::IssueSplineWindow;

        // 10. Terminal: window < 2 points — single per-point MoveTo.
        return Verdict::MoveToFallback;
    }
}
