/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearMath.h"

#include "DungeonClearTuning.h"  // DC_PI

#include <cmath>
#include <cstddef>
#include <limits>

std::uint32_t DungeonClearMath::EstimateAggroCount(std::vector<DynPullMob> const& mobs,
                                                   std::size_t targetIdx,
                                                   float combatSpread,
                                                   float assistRadius,
                                                   float zTolerance,
                                                   bool excludeLonePatrollers,
                                                   std::vector<std::size_t>* countedOut,
                                                   std::uint32_t* weightThirdsOut)
{
    if (weightThirdsOut)
        *weightThirdsOut = 0;
    std::size_t const n = mobs.size();
    if (n == 0 || targetIdx >= n)
        return 0;

    // Patrol-wait reduced pass: a lone patroller (no atomic pack) is treated as if
    // it had walked out of range — not eligible to seed, assist, or be assisted.
    // Formation/linked patrollers (packId != 0) are unaffected.
    auto eligible = [&](std::size_t i)
    {
        return mobs[i].chainEligible &&
               !(excludeLonePatrollers && mobs[i].patroller && mobs[i].packId == 0);
    };

    auto dist2d = [&](std::size_t a, std::size_t b)
    {
        float const dx = mobs[a].x - mobs[b].x;
        float const dy = mobs[a].y - mobs[b].y;
        return std::sqrt(dx * dx + dy * dy);
    };
    // Same floor: the inter-mob height gap is within tolerance. WotLK floor-to-
    // floor gaps comfortably exceed it, so this separates a ledge/ramp mob
    // overhead from one that shares the camp's floor.
    auto sameLevel = [&](std::size_t a, std::size_t b)
    {
        return std::fabs(mobs[a].z - mobs[b].z) <= zTolerance;
    };

    // Engine-pack identity is atomic: members sharing a non-zero packId pull as
    // one unit regardless of distance or height (a formation / linked respawn).
    // Union them so a partially-counted pack can be closed to its full size.
    // n is tiny, so O(n^2) union-find is fine.
    std::vector<std::size_t> parent(n);
    for (std::size_t i = 0; i < n; ++i)
        parent[i] = i;
    auto find = [&parent](std::size_t a)
    {
        while (parent[a] != a)
        {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    };
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j)
            if (mobs[i].packId != 0 && mobs[i].packId == mobs[j].packId)
                parent[find(i)] = find(j);

    std::vector<char> inSet(n, 0);

    // Seed: the camp spot is the target's position. A mob proximity-aggros when a
    // player enters ITS aggro radius, and in a Leeroy the whole party piles onto
    // the target, so a mob counts when it sits within its own reach (+ combat
    // drift) of the target, on the same level and reachable. The target's own
    // atomic pack pulls regardless of distance/height.
    std::size_t const targetRoot = find(targetIdx);
    for (std::size_t i = 0; i < n; ++i)
    {
        if (find(i) == targetRoot)
        {
            inSet[i] = 1;  // target + its atomic pack
            continue;
        }
        if (eligible(i) && sameLevel(i, targetIdx)
            && dist2d(i, targetIdx) <= mobs[i].aggroReach + combatSpread)
            inSet[i] = 1;
    }

    // One assist hop (no transitivity). A chainEligible mob within assistRadius of
    // a SEED mob joins via CallForHelp, but proximity aggro from a fixed camp does
    // not chain, so an assisted mob does not seed further proximity or assist.
    // Iterate over a SNAPSHOT of the seed so newly-added mobs can't propagate.
    std::vector<char> const seed = inSet;
    for (std::size_t j = 0; j < n; ++j)
    {
        if (seed[j] || !eligible(j))
            continue;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (!seed[i])
                continue;
            if (sameLevel(i, j) && dist2d(i, j) <= assistRadius)
            {
                inSet[j] = 1;
                break;
            }
        }
    }

    // Formation closure: a counted member drags its whole pack in — you cannot
    // pull half a formation. This completes packs; it does not re-seed proximity
    // or assist, so it cannot cascade.
    for (std::size_t i = 0; i < n; ++i)
    {
        if (!inSet[i] || mobs[i].packId == 0)
            continue;
        std::size_t const r = find(i);
        for (std::size_t k = 0; k < n; ++k)
            if (find(k) == r)
                inSet[k] = 1;
    }

    if (countedOut)
        countedOut->clear();
    std::uint32_t count = 0;
    std::uint32_t weightThirds = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (inSet[i])
        {
            ++count;
            // Pull weight in thirds of an elite: an elite body is the full unit (3),
            // a normal body a third (1). Keeps the tally integer for the x3-scaled
            // ceiling comparison at the call site.
            weightThirds += mobs[i].elite ? 3u : 1u;
            if (countedOut)
                countedOut->push_back(i);
        }
    if (weightThirdsOut)
        *weightThirdsOut = weightThirds;
    return count;
}

bool DungeonClearMath::ShouldAbortPullForCc(bool impaired, std::uint32_t ccSince,
                                            std::uint32_t now, std::uint32_t graceMs,
                                            std::uint32_t& ccSinceOut)
{
    if (!impaired)
    {
        ccSinceOut = 0;
        return false;
    }
    // Arm the latch on the first impaired tick. Never store 0 (it means "clear"),
    // so a tank impaired at the very first millisecond still latches.
    std::uint32_t const start = ccSince != 0 ? ccSince : (now != 0 ? now : 1u);
    ccSinceOut = start;
    // Guard the unsigned subtraction: start is normally <= now (latched this tick
    // or earlier), but the now==0 corner can leave start (1) ahead of now. Clamp to
    // 0 elapsed there. graceMs == 0 => aborts immediately on the first tick.
    std::uint32_t const elapsed = now >= start ? now - start : 0u;
    return elapsed >= graceMs;
}

bool DungeonClearMath::ShouldAbandonPlantedDrag(bool planted,
                                                std::uint32_t plantedSince,
                                                std::uint32_t now,
                                                std::uint32_t confirmMs,
                                                std::uint32_t& plantedSinceOut)
{
    // Identical latch semantics — arm on the first qualifying tick, disarm the
    // moment the condition clears, fire once it has persisted. Delegated rather
    // than duplicated so the two can never drift apart on the awkward corners
    // (now == 0, confirmMs == 0, a latch armed ahead of `now`).
    return ShouldAbortPullForCc(planted, plantedSince, now, confirmMs,
                                plantedSinceOut);
}

bool DungeonClearMath::ShouldTripCampSafety(bool inCombat, float healthPct,
                                            float safetyHpPct,
                                            bool attackerIsPullTarget,
                                            std::uint32_t since, std::uint32_t now,
                                            std::uint32_t graceMs,
                                            std::uint32_t& sinceOut)
{
    bool const qualifying = inCombat && safetyHpPct > 0.0f &&
                            healthPct < safetyHpPct && !attackerIsPullTarget;
    if (!qualifying)
    {
        sinceOut = 0;
        return false;
    }
    // Arm the latch on the first qualifying tick. Never store 0 (it means
    // "fine"), so a follower qualifying at the very first millisecond still
    // latches. Same contract as ShouldAbortPullForCc.
    std::uint32_t const start = since != 0 ? since : (now != 0 ? now : 1u);
    sinceOut = start;
    std::uint32_t const elapsed = now >= start ? now - start : 0u;
    return elapsed >= graceMs;
}

bool DungeonClearMath::ShouldStandDownForPull(bool packIsPullsOwn, bool pullPhaseIdle)
{
    if (packIsPullsOwn)
        return true;          // the pipeline is (or is about to be) working it
    return !pullPhaseIdle;    // in flight: never thrash the maneuver
}

bool DungeonClearMath::ShouldReleaseStandingPull(bool effectiveOn, bool standing,
                                                 bool partyInCombat, bool holdingPhase,
                                                 bool bossPullback)
{
    if (effectiveOn || !standing)
        return false;             // the pull owns itself / nothing left standing
    if (bossPullback)
        return false;             // pull-back drags run with pull mode off by design
    // Never dismantle a maneuver in flight — nor a camp anyone is still fighting at.
    return !partyInCombat && !holdingPhase;
}

bool DungeonClearMath::ShouldDropPullVerdict(bool targetPresent, std::uint32_t lostSince,
                                             std::uint32_t now, std::uint32_t graceMs,
                                             std::uint32_t& lostSinceOut)
{
    if (targetPresent)
    {
        lostSinceOut = 0;
        return false;
    }
    // Arm the latch on the first lost tick. Never store 0 (it means "present"),
    // so a target lost at the very first millisecond still latches.
    std::uint32_t const start = lostSince != 0 ? lostSince : (now != 0 ? now : 1u);
    lostSinceOut = start;
    // Guard the unsigned subtraction: start is normally <= now (latched this tick
    // or earlier), but the now==0 corner can leave start (1) ahead of now. Clamp to
    // 0 elapsed there. graceMs == 0 => drops immediately on the first tick.
    std::uint32_t const elapsed = now >= start ? now - start : 0u;
    return elapsed >= graceMs;
}

bool DungeonClearMath::ShouldRollInForLeeroy(std::uint32_t decision, bool targetAlive,
                                             float tankToTarget2d, float commitRange,
                                             float lead)
{
    // Only a standing LEEROY verdict (1) commits the tank to charging the pack.
    // No verdict (0) means it is still sizing the pack up, and Advanced (2)
    // hands the party to hold-at-camp — neither may release the scout lag.
    if (decision != 1u || !targetAlive)
        return false;
    // The boundary is inclusive: at exactly commitRange + lead the tank is
    // committing, so the party starts rolling.
    return tankToTarget2d <= commitRange + lead;
}

bool DungeonClearMath::ShouldWaitForPatrol(std::uint32_t fullCount,
                                           std::uint32_t reducedCount,
                                           std::uint32_t ceiling,
                                           std::uint32_t waitSince,
                                           std::uint32_t now, std::uint32_t waitMs,
                                           std::uint32_t& waitSinceOut)
{
    // Patrol-contended only when the lone patrollers are the ONLY reason the pull
    // is over the ceiling: full over, reduced (without them) at/under.
    bool const contended = fullCount > ceiling && reducedCount <= ceiling;
    if (!contended)
    {
        waitSinceOut = 0;
        return false;
    }
    // Arm the latch on the first contended tick. Never store 0 (it means "clear"),
    // so a contention seen at the very first millisecond still latches.
    std::uint32_t const start = waitSince != 0 ? waitSince : (now != 0 ? now : 1u);
    waitSinceOut = start;
    // Guard the unsigned subtraction (the now==0 corner can leave start ahead of
    // now). Wait while inside the budget; once it elapses, proceed with the heavy
    // verdict — the latch stays armed so the wait does not re-fire. waitMs == 0
    // proceeds immediately.
    std::uint32_t const elapsed = now >= start ? now - start : 0u;
    return elapsed < waitMs;
}

bool DungeonClearMath::ShouldMusterForScriptedStage(bool stagePending, bool toppedUp,
                                                    std::uint32_t waitSince,
                                                    std::uint32_t now,
                                                    std::uint32_t waitMs,
                                                    std::uint32_t minMs,
                                                    std::uint32_t& waitSinceOut)
{
    if (!stagePending)
    {
        waitSinceOut = 0;
        return false;
    }
    // Never armed and already topped up: the healthy case — arm nothing, pull now.
    if (waitSince == 0 && toppedUp)
    {
        waitSinceOut = 0;
        return false;
    }
    // Arm on the first short tick. Never store 0 (it means "clear"), so a muster
    // that begins at the very first millisecond still latches.
    std::uint32_t const start = waitSince != 0 ? waitSince : (now != 0 ? now : 1u);
    waitSinceOut = start;
    // Guard the unsigned subtraction (the now==0 corner can leave start ahead of
    // now).
    std::uint32_t const elapsed = now >= start ? now - start : 0u;
    // Substance floor: an armed muster holds through min(minMs, waitMs) even if
    // the percentages close first. The floors going green one tick after arming
    // means an AoE heal crossed a line, not that anyone drank — and the raised
    // rest targets need a few seconds of sitting to mean anything. The min()
    // keeps waitMs == 0 as "muster disabled outright".
    if (elapsed < std::min(minMs, waitMs))
        return true;
    if (toppedUp)
    {
        waitSinceOut = 0;
        return false;
    }
    // Hold while inside the budget; once it elapses, proceed on the ordinary
    // floors — the latch stays armed so the muster does not re-fire on the same
    // stage.
    return elapsed < waitMs;
}

bool DungeonClearMath::ShouldPlantEarly(std::vector<float> const& attackerDists,
                                        float glueRadius,
                                        std::uint32_t glueTicksNeeded, bool losPull,
                                        float distToCamp, float legStartDist,
                                        std::uint32_t& plantTicks)
{
    // An LOS-break camp's whole purpose is reaching the corner; never plant short.
    // Nothing chasing means the pull evaded/fizzled — that latch owns the outcome,
    // not a plant. Either way the gather condition can't hold, so reset and bail.
    if (losPull || attackerDists.empty())
    {
        plantTicks = 0;
        return false;
    }

    // Require at least the first half of the return leg covered: the neighbour-pack
    // clearance was measured against the camp, and half the drag retains most of
    // it. A leg with no recorded start distance (legStartDist <= 0) can't qualify.
    if (legStartDist <= 0.0f || distToCamp > legStartDist * 0.5f)
    {
        plantTicks = 0;
        return false;
    }

    // Pack gathered: EVERY live attacker within the glue radius (one straggler
    // chasing from range means the pack hasn't closed yet).
    for (float d : attackerDists)
    {
        if (d > glueRadius)
        {
            plantTicks = 0;
            return false;
        }
    }

    // Gather condition holds this tick — arm/advance the debounce latch and plant
    // once it has persisted for glueTicksNeeded consecutive ticks. glueTicksNeeded
    // == 0 plants on the first gathered tick.
    ++plantTicks;
    return plantTicks >= glueTicksNeeded;
}

float DungeonClearMath::PullTagStopDistance(float aggroRange, float meleeReach,
                                            std::uint32_t closingMs,
                                            std::uint32_t graceMs,
                                            float creepYardsPerSec, float creepFloor,
                                            bool& forceTagOut)
{
    forceTagOut = false;

    // Two yards inside the radius: far enough in that the arrival itself is a
    // relocation across the notice threshold, close enough to the edge that the pull
    // is a pull rather than a face-pull.
    float stop = aggroRange - 2.0f;

    // The creep. Bounded by `creepFloor`, which is the whole difference between an
    // ordinary pull and a scripted stage — see the header.
    if (closingMs > graceMs && creepYardsPerSec > 0.0f)
    {
        stop -= ((closingMs - graceMs) / 1000.0f) * creepYardsPerSec;
        if (stop < creepFloor)
            stop = creepFloor;
    }

    // Body contact is the last floor either way. If the pack's aggro is at or below
    // melee reach, closing cannot cross it and the caller has to swing.
    if (stop <= meleeReach)
    {
        forceTagOut = true;
        return meleeReach;
    }
    return stop;
}

bool DungeonClearMath::ShouldReleaseFollower(bool isHealer, bool alreadyInCombat,
                                             std::uint32_t combatSinceMs,
                                             std::uint32_t now, std::uint32_t leadMs,
                                             float tankHealthPct, float panicHpPct)
{
    // Healers are never held: a withheld heal is a wipe, and a heal does not rip
    // threat off the tank the way a DPS opener does.
    if (isHealer)
        return true;
    // Already in combat: the lead exists only to stop a FRESH, out-of-combat DPS
    // from stealing aggro on the opener. A follower already flagged in combat (the
    // tank pulled and dragged the pack out of its line of sight) is on the threat
    // table already — it cannot over-aggro by walking into sight, and holding it
    // strands it with no DC-owned behavior for the tick, exactly where a revived
    // stock follow-master would walk it to the human. Release it onto the fight.
    if (alreadyInCombat)
        return true;
    // Feature off.
    if (leadMs == 0)
        return true;
    // No combat stamp: the leader isn't (observed) in combat, so there is no lead
    // to serve. The caller only reaches here when assist is wanted, so releasing
    // is the safe default rather than wedging a follower out of an active fight.
    if (combatSinceMs == 0)
        return true;
    // Tank is losing the fight — pile in regardless of the lead. panicHpPct <= 0
    // disables the bypass.
    if (panicHpPct > 0.0f && tankHealthPct < panicHpPct)
        return true;
    // DPS hold until the lead window since combat start has elapsed. Guard the
    // unsigned subtraction against a combatSince stamped a tick ahead of `now`.
    std::uint32_t const elapsed = now > combatSinceMs ? now - combatSinceMs : 0u;
    return elapsed >= leadMs;
}

bool DungeonClearMath::ShouldHandoffFizzledPull(bool pulledAliveIdle, bool sameTarget,
                                                std::uint32_t maxFizzles,
                                                std::uint32_t& fizzleCount)
{
    // The pack died or is still being fought: not a fizzle. Clear the latch.
    if (!pulledAliveIdle)
    {
        fizzleCount = 0;
        return false;
    }
    // A fizzle: extend the run for the same pack, or restart at 1 for a new one.
    if (sameTarget)
        ++fizzleCount;
    else
        fizzleCount = 1;
    // Hand off once the same pack has fizzled maxFizzles times in a row. maxFizzles
    // == 0 hands off on the first fizzle.
    return fizzleCount >= maxFizzles;
}

float DungeonClearMath::DistSqToSegment2D(float px, float py,
                                         float ax, float ay,
                                         float bx, float by)
{
    float const ex = bx - ax;
    float const ey = by - ay;
    float const len2 = ex * ex + ey * ey;
    if (len2 <= 1e-6f)
    {
        float const dx = px - ax;
        float const dy = py - ay;
        return dx * dx + dy * dy;
    }
    float t = ((px - ax) * ex + (py - ay) * ey) / len2;
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    float const cx = ax + t * ex;
    float const cy = ay + t * ey;
    float const dx = px - cx;
    float const dy = py - cy;
    return dx * dx + dy * dy;
}

bool DungeonClearMath::NearestPointOnPolyline2D(std::vector<G3D::Vector3> const& route,
                                                float px, float py, G3D::Vector3& out)
{
    if (route.size() < 2)
        return false;

    float bestDist2 = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i + 1 < route.size(); ++i)
    {
        G3D::Vector3 const& a = route[i];
        G3D::Vector3 const& b = route[i + 1];
        float const ex = b.x - a.x;
        float const ey = b.y - a.y;
        float const len2 = ex * ex + ey * ey;

        // Degenerate leg (two coincident route points — the smoother emits them
        // at anchor joins): the whole leg IS point `a`.
        float t = 0.0f;
        if (len2 > 1e-6f)
        {
            t = ((px - a.x) * ex + (py - a.y) * ey) / len2;
            if (t < 0.0f) t = 0.0f;
            else if (t > 1.0f) t = 1.0f;
        }

        float const cx = a.x + t * ex;
        float const cy = a.y + t * ey;
        float const dx = px - cx;
        float const dy = py - cy;
        float const d2 = dx * dx + dy * dy;
        if (d2 < bestDist2)
        {
            bestDist2 = d2;
            out = G3D::Vector3(cx, cy, a.z + t * (b.z - a.z));
        }
    }
    return true;
}

std::size_t DungeonClearMath::PathProgressCursor(std::vector<G3D::Vector3> const& route,
                                                 float botX, float botY, float botZ)
{
    std::size_t cursor = 0;
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < route.size(); ++i)
    {
        float const dx = route[i].x - botX;
        float const dy = route[i].y - botY;
        float const dz = route[i].z - botZ;
        // 3D, squared. The Z term is the whole point: it is what stops a route
        // vertex a storey overhead from out-bidding the vertex on the floor the
        // bot is actually standing on. See the header for the Shadowfang case.
        float const d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < best)
        {
            best = d2;
            cursor = i;
        }
    }
    return cursor;
}

std::size_t DungeonClearMath::FindTrailRejoin(std::vector<Position> const& crumbs,
                                              Position const& cur, float rejoinRadius)
{
    if (rejoinRadius <= 0.0f)
        return TrailRejoinNone;

    float const r2 = rejoinRadius * rejoinRadius;
    // Latest-wins: walk from the most recent crumb backward, return the first
    // within the (3D, squared — no sqrt) radius.
    for (std::size_t i = crumbs.size(); i-- > 0; )
    {
        if (crumbs[i].GetExactDistSq(&cur) <= r2)
            return i;
    }
    return TrailRejoinNone;
}

bool DungeonClearMath::SegmentIntersectsAABB2D(float ax, float ay,
                                               float bx, float by,
                                               float minX, float minY,
                                               float maxX, float maxY)
{
    // Parametric segment P(t) = A + t*(B-A), t in [0,1]. Clip the interval
    // against each box slab; if anything survives, the segment touches the box.
    float const dx = bx - ax;
    float const dy = by - ay;
    float t0 = 0.0f;
    float t1 = 1.0f;

    // p = -direction component, q = distance to the slab edge. The pair
    // (-dx, ax-minX), (dx, maxX-ax), (-dy, ay-minY), (dy, maxY-ay) covers the
    // four edges (left, right, bottom, top).
    float const p[4] = {-dx, dx, -dy, dy};
    float const q[4] = {ax - minX, maxX - ax, ay - minY, maxY - ay};

    for (int i = 0; i < 4; ++i)
    {
        if (p[i] == 0.0f)
        {
            // Segment parallel to this slab: outside it -> no intersection.
            if (q[i] < 0.0f)
                return false;
            continue;
        }
        float const r = q[i] / p[i];
        if (p[i] < 0.0f)
        {
            if (r > t1)
                return false;
            if (r > t0)
                t0 = r;
        }
        else
        {
            if (r < t0)
                return false;
            if (r < t1)
                t1 = r;
        }
    }
    return t0 <= t1;
}

std::size_t DungeonClearMath::SelectHealTarget(std::vector<HealCandidate> const& members,
                                               float hpFloor, float tankBias)
{
    std::size_t best = HealTargetNone;
    float bestScore = 0.0f;
    for (std::size_t i = 0; i < members.size(); ++i)
    {
        HealCandidate const& m = members[i];
        // "Needs healing" gate is on RAW health, so a healthy biased tank can
        // never be selected over a hurt DPS.
        if (m.healthPct >= hpFloor)
            continue;
        float const score = m.healthPct - (m.isLeaderTank ? tankBias : 0.0f);
        if (best == HealTargetNone || score < bestScore)
        {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

std::vector<Position> DungeonClearMath::StandoffCandidates(Position const& target,
                                                           Position const& bot,
                                                           float standoffRadius,
                                                           std::uint32_t ringPoints)
{
    std::vector<Position> out;
    out.reserve(ringPoints + 1);

    // Base direction: from the center toward the bot's current side, so the first
    // candidate is the shortest reposition. Degenerate (bot on center) -> +X.
    float dx = bot.GetPositionX() - target.GetPositionX();
    float dy = bot.GetPositionY() - target.GetPositionY();
    float const len = std::sqrt(dx * dx + dy * dy);
    float baseAngle;
    if (len < 0.01f)
        baseAngle = 0.0f;
    else
        baseAngle = std::atan2(dy, dx);

    float const z = target.GetPositionZ();
    auto emit = [&](float angle)
    {
        out.emplace_back(target.GetPositionX() + std::cos(angle) * standoffRadius,
                         target.GetPositionY() + std::sin(angle) * standoffRadius,
                         z, 0.0f);
    };

    // First candidate dead on the bot's side, then fan out alternately +/- so
    // nearer-to-bot angles are tried before the far side of the target.
    emit(baseAngle);
    float const step = (2.0f * DC_PI) / float(ringPoints + 1);
    for (std::uint32_t k = 1; k <= ringPoints; ++k)
    {
        float const off = step * float((k + 1) / 2);
        float const sign = (k % 2 == 1) ? 1.0f : -1.0f;
        emit(baseAngle + sign * off);
    }
    return out;
}
