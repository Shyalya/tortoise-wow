/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Synthetic scenario driver (Tier 1, Phase 1.4 of the headless-sim plan). Where
// the unit tests pin a single decision at one edge, this scripts a SEQUENCE of
// observations through a tick loop and asserts the verdict trajectory — the
// offline equivalent of watching a pull: approach a pack, the governor picks a
// mode, "insta-die" the pack (a boolean flip, not a combat sim), then resume the
// route to the next pack. It exercises the advance ladder (DecideApproach) and
// the pull governor (DecidePull) together, deterministically, with no engine.
//
// The "world" here is an abstract perception state, not a map: a handful of
// scalars (distance to the pack, whether it is alive, whether a route exists)
// that a tick advances and the decision cores consume. That is the whole point —
// DC decisions are pure functions of a perception snapshot, so a pull is fully
// reproducible from scalars alone.

#include "gtest/gtest.h"
#include "DungeonClearApproach.h"
#include "DcPullDecision.h"

#include <algorithm>
#include <cstdint>
#include <vector>

using DungeonClearApproach::DecideApproach;
using DungeonClearApproach::Observation;
using DungeonClearApproach::Verdict;
using DcPullDecision::DecidePull;
using DcPullDecision::PullObservation;
using DcPullDecision::PullVerdict;

namespace
{
    constexpr float kEngageRange = 22.0f;
    constexpr float kGlidePerTick = 6.0f;     // yards closed per tick while gliding
    constexpr float kCommitRange = 25.0f;

    // Abstract pull-scenario world. A tick reads it into the two observations,
    // decides, and mutates it (move, engage, insta-die, respawn next pack).
    struct World
    {
        float distToPack = 100.0f;   // tank -> current pack (2D yards)
        bool  packAlive  = true;
        bool  routeReachable = true;
        int   packsRemaining = 2;    // packs left to clear after the current one
        bool  hadStandingVerdict = false;  // governor committed a verdict for this pack
        std::uint32_t graceTicks = 0;      // ticks since the pack died (for the no-target grace)
    };

    Observation MakeApproachObs(World const& w)
    {
        Observation o;
        o.engageRange = kEngageRange;
        o.engageDist = w.distToPack;
        o.pathReachable = w.routeReachable;
        o.haveSplineWindow = true;    // a window is ready while travelling
        o.stuckTickLimit = 5;
        o.pursuitFailLimit = 5;
        o.doneNotEngagedLimit = 15;
        return o;
    }

    PullObservation MakePullObs(World const& w)
    {
        PullObservation o;
        o.hasTarget = w.packAlive;
        o.phaseIdle = true;
        o.hasStandingVerdict = w.hadStandingVerdict;
        // After the pack dies, the standing verdict survives a short grace before
        // dropping (modelled here as a 2-tick grace).
        o.verdictGraceExpired = !w.packAlive && w.graceTicks >= 2;
        o.advanced = false;           // small pack -> Leeroy
        return o;
    }
}

// A full clear cycle: glide to a pack (IssueSplineWindow + Leeroy), engage and
// insta-die it (HoldNoTarget within grace, then DropToLeeroy), then resume to
// the next pack. The trajectory of verdicts is the assertion.
TEST(DcScenarioDriver, ClearsTwoPacksWithLeeroyGovernor)
{
    World w;
    std::vector<Verdict> approachTrace;
    std::vector<PullVerdict> pullTrace;

    int packsCleared = 0;
    for (int tick = 0; tick < 200 && packsCleared < 2; ++tick)
    {
        Verdict const av = DecideApproach(MakeApproachObs(w));
        PullVerdict const pv = DecidePull(MakePullObs(w));
        approachTrace.push_back(av);
        pullTrace.push_back(pv);

        if (w.packAlive && w.distToPack > kEngageRange)
        {
            // Travelling toward a live pack: the route owns the tick and the
            // governor stands a Leeroy verdict the whole approach.
            EXPECT_EQ(av, Verdict::IssueSplineWindow);
            EXPECT_TRUE(pv == PullVerdict::Leeroy || pv == PullVerdict::NoOp);
            if (w.distToPack <= kCommitRange)
                w.hadStandingVerdict = true;  // committed a verdict for this pack
            w.distToPack -= kGlidePerTick;
        }
        else if (w.packAlive)
        {
            // In engage range. By now the governor has stood a Leeroy verdict for
            // this pack (commit range >= engage range, so the approach committed
            // it even if the discrete glide step stepped over kCommitRange), then
            // "insta-die" the pack — a boolean flip, no combat.
            w.hadStandingVerdict = true;
            w.packAlive = false;
            w.graceTicks = 0;
        }
        else
        {
            // Pack dead. The governor holds the standing verdict through the grace
            // (HoldNoTarget), then drops it (DropToLeeroy). pv was computed at the
            // top of the tick from the SAME w.graceTicks tested here, so the
            // assertion and the observation stay aligned; graceTicks only advances
            // on the holding path below.
            bool const drop = !w.hadStandingVerdict || w.graceTicks >= 2;
            EXPECT_EQ(pv, drop ? PullVerdict::DropToLeeroy : PullVerdict::HoldNoTarget);
            if (drop)
            {
                ++packsCleared;
                if (w.packsRemaining > 0)
                {
                    // Spawn the next pack and reset for a fresh approach.
                    --w.packsRemaining;
                    w.distToPack = 80.0f;
                    w.packAlive = true;
                    w.hadStandingVerdict = false;
                    w.graceTicks = 0;
                }
            }
            else
            {
                ++w.graceTicks;
            }
        }
    }

    EXPECT_EQ(packsCleared, 2) << "scenario did not clear both packs";
    // Sanity: the trajectory actually exercised travel, a hold, and a drop.
    EXPECT_NE(std::find(approachTrace.begin(), approachTrace.end(),
                        Verdict::IssueSplineWindow),
              approachTrace.end());
    EXPECT_NE(std::find(pullTrace.begin(), pullTrace.end(), PullVerdict::HoldNoTarget),
              pullTrace.end());
    EXPECT_NE(std::find(pullTrace.begin(), pullTrace.end(), PullVerdict::DropToLeeroy),
              pullTrace.end());
}

// A blocked route: while no navigable path exists and an async build is in
// flight, the advance ladder must hold quietly (PlanRouteWait), never stall.
TEST(DcScenarioDriver, AsyncRouteBuildHoldsThenResumes)
{
    World w;
    w.routeReachable = false;

    Observation o = MakeApproachObs(w);
    o.asyncPending = true;
    EXPECT_EQ(DecideApproach(o), Verdict::PlanRouteWait);

    // Build completes: route reachable, window ready -> glide.
    w.routeReachable = true;
    EXPECT_EQ(DecideApproach(MakeApproachObs(w)), Verdict::IssueSplineWindow);
}
