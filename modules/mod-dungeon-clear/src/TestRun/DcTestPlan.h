/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTPLAN_H
#define _PLAYERBOT_DCTESTPLAN_H

#include <cstdint>
#include <string>
#include <vector>

#include "TestRun/DcTestGearTiers.h"

// Pure kernel for `.dc test plan` campaigns: a plan keeps up to `concurrent`
// test runs of one dungeon in flight until `total` have completed, then the
// outcomes are aggregated into one summary (DcTestPlanSummary). Engine-free —
// the launch decision and the argument parsing are unit-testable in isolation
// (mirroring DcTestRunVerdict / DcTestRunSelect); DcTestPlanManager owns the
// live state and feeds this kernel.

namespace DcTestPlan
{
    struct Spec
    {
        std::string planId;        // "tp-…", assigned by the manager
        std::string dungeonToken;  // DcTestDungeonRegistry token
        std::uint32_t total = 0;       // runs to complete (failures count)
        std::uint32_t concurrent = 0;  // plan-local in-flight cap
        std::uint32_t level = 0;       // 0 = registry default for the difficulty
        bool heroic = false;           // children run at DUNGEON_DIFFICULTY_HEROIC
        std::uint32_t seedBase = 0;    // 0 = random comp per run;
                                       // N = child i replays seed N+i
        // Gear ceiling every child run is geared to. Plan-wide rather than
        // per-child on purpose: a campaign exists to compare runs against each
        // other, which only means anything at one ceiling.
        DcTestGearTiers::Spec gear;
    };

    struct Counters
    {
        std::uint32_t launched = 0;   // Start() accepted (active + finished)
        std::uint32_t succeeded = 0;
        std::uint32_t failed = 0;     // any non-success incl. setup_failed
        std::uint32_t activeNow = 0;  // live child runs
    };

    // One finished child run, distilled from its DcTestRunRecord::Record by the
    // run manager's erase pass. bossKills keeps only named mask-kills (in kill
    // order) — anchor "objective" completions carry no name worth aggregating.
    // One pull carried up from a child run for the plan-level pull stats. The
    // full DcTestRunRecord::PullEntry stays in the run's own JSONL; the plan only
    // needs the two numbers whose relationship is the diagnosis (what the
    // governor predicted vs. what showed up), plus the two flags that split the
    // population (which maneuver it chose, and whether the party died on it).
    struct PullSample
    {
        std::uint32_t predicted = 0;
        std::uint32_t observed = 0;
        bool advanced = false;
        bool wipedHere = false;
    };

    struct RunOutcome
    {
        std::string runId;
        std::string result;      // verdict token ("success", "setup_failed", …)
        std::string failReason;
        std::uint32_t durationS = 0;
        std::uint32_t bossesKilled = 0;
        std::uint32_t bossesTotal = 0;
        std::vector<std::string> bossKills;
        // Every boss on the run's roster, in progression order. Gives the plan
        // summary a denominator that includes bosses no run ever reached.
        std::vector<std::string> bossRoster;
        // What the party was down to when the run ended, boss or trash; empty
        // when nobody died or nothing was on them (DcTestRunRecord::wipeOpponent).
        std::string wipeOpponent;
        bool wipeOnBoss = false;
        std::vector<PullSample> pulls;
    };

    // How many new runs to launch this tick: bounded by the plan's remaining
    // budget (total - launched), the plan's own concurrency headroom, and the
    // run manager's global cap headroom; zero while a backoff is pending.
    std::uint32_t LaunchesWanted(Spec const& s, Counters const& c,
                                 std::uint32_t globalHeadroom, std::uint32_t backoffMs);

    // A plan started from the console has no issuing GM until the headless
    // test driver finishes logging in, so the scheduler has to sit on its
    // hands for the first few ticks. Wait while the login is in flight, but
    // bound it — a driver that can't come up at all (or one whose login never
    // lands) must fail the plan rather than leave it parked forever.
    enum class DriverWait
    {
        Wait,
        Abort,
    };
    inline DriverWait DriverWaitVerdict(bool loginPending, std::uint32_t waitedMs,
                                        std::uint32_t capMs)
    {
        if (!loginPending)
            return DriverWait::Abort;  // misconfigured — retrying can't fix it
        return waitedMs >= capMs ? DriverWait::Abort : DriverWait::Wait;
    }

    // A plan is finished (ready to summarize) once nothing is in flight and
    // either the total has completed or the plan was told to stop launching.
    inline bool IsFinished(Spec const& s, Counters const& c, bool stopping)
    {
        return c.activeNow == 0 && (stopping || c.succeeded + c.failed >= s.total);
    }

    // `.dc test plan start <token> [heroic] total=N [concurrent=N] [level=N]
    // [seed=N] [ilvl=N|none] [quality=N|epic|…]`. Fills the whole Spec except
    // planId. ok is false with a usage-shaped err on a missing token/total, a
    // duplicate bare word, or a malformed key=value.
    struct ParseResult
    {
        bool ok = false;
        std::string err;
        Spec spec;
    };
    ParseResult ParseStartArgs(std::string const& args);
}

#endif  // _PLAYERBOT_DCTESTPLAN_H
