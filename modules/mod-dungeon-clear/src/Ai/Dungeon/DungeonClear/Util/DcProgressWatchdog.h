/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCPROGRESSWATCHDOG_H
#define _PLAYERBOT_DCPROGRESSWATCHDOG_H

#include <cstdint>

// One reusable "am I making progress toward my objective; escalate after a
// budget" detector, replacing the per-movement-mode stuck counters that had
// accreted one at a time (route-glide posStuck, its byte-identical door-walk-in
// twin, the swim closing-distance timer — nav review F11). Each site had grown
// its own copy of the metric + threshold + reset; folding them onto one tested
// primitive gives them a single implementation and a single Reset so they can't
// drift or be forgotten by a reset — the same win DcApproachState brought to the
// latch/counter sprawl, and it makes the state struct the honest home the review
// noted it already was.
//
// Two progress signals, because the sites legitimately differ:
//
//   * Displacement — "did the bot physically move this tick". The route glide and
//     its door-walk-in twin watch a continuous escort spline grinding against
//     geometry, where the wedge shows up as ~0 displacement WHILE isMoving(). A
//     momentary isMoving()==false (a spline micro-stop) is not a wedge, so it
//     resets. Blind to a fully stalled bot by design — that case belongs to other
//     rungs (direct-pursuit / dead-end) — matching the original posStuck exactly.
//
//   * ClosingDistance — "am I getting nearer the target". Used by the swim leg,
//     the direct-pursuit shortcut, and the dead-end final approach: progress is
//     the straight-line distance to the objective dropping below the closest seen
//     so far. Works whether or not the bot is moving, so it ALSO catches a fully
//     stalled bot — the non-moving blind spot displacement can't see, which is
//     exactly why pursuit and final-approach had grown their own MoveTo-refusal /
//     tick counters. Each no-progress tick both increments stuckTicks (a TICK
//     budget, for pursuit/final-approach) and leaves lastProgressMs unstamped (a
//     wall-clock TIME budget, for the swim leg, checked by the caller with the
//     wrap-safe getMSTimeDiff); a progress tick resets both.
struct DcProgressWatchdog
{
    std::uint32_t stuckTicks     = 0;      // consecutive no-progress ticks
    float         bestDist       = -1.0f;  // closest approach to the active target (<0 = unset)
    std::uint32_t lastProgressMs = 0;      // wall-clock of the last progress (time-budget sites)

    void Reset() { *this = DcProgressWatchdog{}; }

    // Displacement tick. moving = bot->isMoving(); moved = distance travelled
    // since the previous sample. A no-progress tick is one that is moving yet
    // barely displaced (moved < minMove); any not-moving OR real-movement tick
    // clears the count. Returns the updated counter — compare it against the
    // caller's tick budget. (The caller passes moving=false on the first,
    // un-sampled tick so it reads as "no wedge yet", exactly as the sentinel
    // lastPos did before.)
    std::uint32_t TickDisplacement(bool moving, float moved, float minMove)
    {
        if (moving && moved < minMove)
            ++stuckTicks;
        else
            stuckTicks = 0;
        return stuckTicks;
    }

    // Closing-distance tick. distToTarget = current straight-line distance to the
    // objective point; nowMs = getMSTime(). Progress = the distance improved by
    // >= minClose versus the closest seen so far (bestDist); on progress it
    // re-arms bestDist, stamps lastProgressMs and zeroes stuckTicks; otherwise it
    // increments stuckTicks. The first sample (bestDist < 0) arms the tracker and
    // counts as progress. Returns true iff progress was made this tick. A tick-
    // budgeted caller (pursuit / final approach) compares stuckTicks to its
    // budget; a time-budgeted caller (swim) compares getMSTimeDiff(lastProgressMs,
    // nowMs) to its stale limit.
    bool TickClosing(float distToTarget, float minClose, std::uint32_t nowMs)
    {
        if (bestDist < 0.0f || distToTarget < bestDist - minClose)
        {
            bestDist = distToTarget;
            lastProgressMs = nowMs;
            stuckTicks = 0;
            return true;
        }
        ++stuckTicks;
        return false;
    }
};

#endif
