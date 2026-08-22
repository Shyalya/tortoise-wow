/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTPLANMANAGER_H
#define _PLAYERBOT_DCTESTPLANMANAGER_H

#include <cstdint>
#include <string>
#include <vector>

#include "ObjectGuid.h"
#include "TestRun/DcTestPlan.h"
#include "TestRun/DcTestRunLiveJson.h"

class Player;

// Registry of live `.dc test plan` campaigns. Each plan launches child test
// runs through DcTestRunManager (at most one per world tick per plan, so the
// shared provisioning budget is never mobbed), tracks their outcomes, and on
// completion appends one summary line to dc_testplans.jsonl.
//
// Threading: everything here runs on the world thread — Start/Stop from the
// command handler, Tick from the module's world-update hook, OnRunFinished
// from DcTestRunManager's erase pass, Snapshots from WriteLiveStatus. The
// observer callbacks that fire from map threads never reach this class, so no
// mutex is needed (unlike DcTestRunManager's _runs).
class DcTestPlanManager
{
public:
    static DcTestPlanManager& Instance();

    // Validate + register a plan (spec.planId is assigned here). The issuing
    // GM is stored as a guid and re-resolved at every launch, so gm may be
    // nullptr: a console/dashboard start registers the plan while the headless
    // driver is still logging in, and the first launch picks the driver up.
    bool Start(DcTestPlan::Spec spec, Player* gm, std::string* msg);

    // Stop the plan(s) the selector resolves to: "" (single active plan),
    // "all", or an exact planId. Marks them draining and aborts their live
    // child runs; the summary is written once the children finish.
    bool Stop(std::string const& selector, std::string* msg);

    // `.dc test stop all` funnel: stop every plan from launching more runs
    // (the caller then aborts all runs, plan children included).
    void StopAll(std::string const& reason);

    std::string StatusText() const;
    bool HasActivePlans() const { return !_plans.empty(); }

    // Drive launches / backoff / finalization; call right before
    // DcTestRunManager::Tick.
    void Tick(uint32 diff);

    // A plan child finished: called by DcTestRunManager's erase pass with the
    // outcome distilled from the run's record.
    void OnRunFinished(std::string const& planId, DcTestPlan::RunOutcome outcome);

    // Per-plan snapshots for the live-status file.
    std::vector<DcTestRunLive::PlanSnapshot> Snapshots() const;

private:
    DcTestPlanManager() = default;

    struct Plan
    {
        DcTestPlan::Spec spec;
        DcTestPlan::Counters counters;
        ObjectGuid gmGuid;
        uint64 startedAtMs = 0;
        uint32 backoffMs = 0;         // remaining transient-rejection backoff
        uint32 transientStreak = 0;   // consecutive rejections with 0 active
        uint64 driverWaitSinceMs = 0; // 0 = not waiting on the driver login
        bool stopping = false;        // no more launches; drain then summarize
        std::string result;           // final disposition once stopping/complete
        std::string abortReason;
        std::vector<DcTestPlan::RunOutcome> outcomes;
        std::vector<std::string> activeRunIds;
    };

    void TickPlan(Plan& plan, uint32 diff);
    void Finalize(Plan& plan);
    void AbortPlan(Plan& plan, std::string const& reason);
    static std::string StatusLine(Plan const& plan);

    std::vector<Plan> _plans;  // world-thread only
};

#endif  // _PLAYERBOT_DCTESTPLANMANAGER_H
