/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestPlanManager.h"

#include <algorithm>
#include <ctime>

#include "Chat.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"

#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"

#include "TestRun/DcTestDriver.h"
#include "TestRun/DcTestDungeonRegistry.h"
#include "TestRun/DcTestPlanSummary.h"
#include "TestRun/DcTestRunManager.h"

namespace
{
    uint64 NowUnixMs()
    {
        return static_cast<uint64>(std::time(nullptr)) * 1000;
    }

    std::string MakePlanId()
    {
        static uint32 counter = 0;
        std::time_t const now = std::time(nullptr);
        std::tm tmBuf{};
        localtime_r(&now, &tmBuf);
        char buf[32];
        std::strftime(buf, sizeof(buf), "tp-%Y%m%d-%H%M%S", &tmBuf);
        return std::string(buf) + "-" + std::to_string(++counter);
    }

    // How many consecutive transient Start rejections (with nothing in flight
    // to unblock us) before the plan is declared stalled and aborted.
    constexpr uint32 kMaxTransientStreak = 3;
}

DcTestPlanManager& DcTestPlanManager::Instance()
{
    static DcTestPlanManager instance;
    return instance;
}

bool DcTestPlanManager::Start(DcTestPlan::Spec spec, Player* gm, std::string* msg)
{
    auto fail = [&](std::string const& why) -> bool
    {
        if (msg)
            *msg = "Test plan not started: " + why;
        return false;
    };

    DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find(spec.dungeonToken);
    if (!row)
        return fail("unknown dungeon '" + spec.dungeonToken + "' — see .dc test list");
    spec.dungeonToken = row->token;  // canonicalize a mapId argument to the token

    // Same gate as DcTestRunManager::Start — reject up front so the plan
    // doesn't burn its launch budget on runs that can never start.
    if (spec.heroic && row->heroicLevel == 0)
        return fail("'" + std::string(row->token) +
                    "' has no heroic mode (classic dungeons have none)");

    uint32 const maxPlans = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.MaxPlans");
    if (maxPlans != 0 && _plans.size() >= maxPlans)
        return fail("max active test plans reached (" + std::to_string(maxPlans) +
                    ") — .dc test plan stop <planId> first");

    // 0 = unlimited (the default): the plan's size is the caller's call.
    uint32 const maxTotal = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.Plan.MaxTotal");
    if (maxTotal != 0 && spec.total > maxTotal)
        return fail("total=" + std::to_string(spec.total) + " exceeds the cap (" +
                    std::to_string(maxTotal) + ", DungeonClear.TestRun.Plan.MaxTotal)");

    // Default + clamp the plan's concurrency to the run manager's global cap so
    // the scheduler isn't asking for launches Start would always reject. With
    // MaxConcurrent at its 0 = unlimited default nothing is clamped: an omitted
    // concurrent= still defaults to a modest 5, but an explicit concurrent=N is
    // honoured for any N — the machine's bot budget is the only ceiling.
    uint32 const maxConcurrent =
        DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.MaxConcurrent");
    if (spec.concurrent == 0)
        spec.concurrent = std::min<uint32>(5, maxConcurrent ? maxConcurrent : 5);
    if (maxConcurrent != 0)
        spec.concurrent = std::min(spec.concurrent, maxConcurrent);
    spec.concurrent = std::max<uint32>(spec.concurrent, 1);
    spec.concurrent = std::min(spec.concurrent, spec.total);

    spec.planId = MakePlanId();

    Plan plan;
    plan.spec = spec;
    if (gm)
        plan.gmGuid = gm->GetObjectGuid();
    plan.startedAtMs = NowUnixMs();
    _plans.push_back(std::move(plan));

    LOG_INFO("playerbots.dungeonclear",
             "TESTPLAN START {} dungeon={} total={} concurrent={} level={} heroic={} seedBase={} "
             "gear={} gm={}",
             spec.planId, spec.dungeonToken, spec.total, spec.concurrent, spec.level,
             spec.heroic ? 1 : 0, spec.seedBase, DcTestGearTiers::Describe(spec.gear),
             gm ? gm->GetName() : "<pending test driver>");

    if (msg)
        *msg = Acore::StringFormat("Test plan started: {} {}{} total={} concurrent={} gear={}{}{}",
                                   spec.planId, spec.dungeonToken,
                                   spec.heroic ? std::string(" (heroic)") : std::string(),
                                   spec.total, spec.concurrent,
                                   DcTestGearTiers::Describe(spec.gear),
                                   spec.seedBase ? Acore::StringFormat(" seedBase={}", spec.seedBase)
                                                 : std::string(),
                                   gm ? std::string()
                                      : std::string(" — first run launches once the test "
                                                    "driver finishes logging in"));
    return true;
}

bool DcTestPlanManager::Stop(std::string const& selector, std::string* msg)
{
    if (_plans.empty())
    {
        if (msg)
            *msg = "no test plan active";
        return false;
    }

    std::vector<Plan*> targets;
    if (selector.empty())
    {
        if (_plans.size() > 1)
        {
            if (msg)
            {
                *msg = "multiple test plans active — specify a planId or 'all':";
                for (Plan const& plan : _plans)
                    *msg += "\n  " + StatusLine(plan);
            }
            return false;
        }
        targets.push_back(&_plans.front());
    }
    else if (selector == "all")
    {
        for (Plan& plan : _plans)
            targets.push_back(&plan);
    }
    else
    {
        for (Plan& plan : _plans)
            if (plan.spec.planId == selector)
                targets.push_back(&plan);
        if (targets.empty())
        {
            if (msg)
            {
                *msg = "no plan matches '" + selector + "' — active plans:";
                for (Plan const& plan : _plans)
                    *msg += "\n  " + StatusLine(plan);
            }
            return false;
        }
    }

    std::string acc;
    for (Plan* plan : targets)
    {
        plan->stopping = true;
        if (plan->result.empty())
            plan->result = "stopped";

        // Abort the live children through the run manager's own selector path;
        // their outcomes flow back via OnRunFinished and the drain finalizes.
        for (std::string const& runId : plan->activeRunIds)
        {
            std::string ignored;
            DcTestRunManager::Instance().Stop(runId, &ignored);
        }

        if (!acc.empty())
            acc += '\n';
        acc += "stopping " + plan->spec.planId + " (" + plan->spec.dungeonToken + ", " +
               std::to_string(plan->counters.activeNow) + " run(s) draining)";
    }
    if (msg)
        *msg = acc;
    return true;
}

void DcTestPlanManager::StopAll(std::string const& reason)
{
    for (Plan& plan : _plans)
    {
        plan.stopping = true;
        if (plan.result.empty())
        {
            plan.result = "stopped";
            plan.abortReason = reason;
        }
    }
}

std::string DcTestPlanManager::StatusLine(Plan const& plan)
{
    DcTestPlan::Counters const& c = plan.counters;
    return Acore::StringFormat("{} {}{} {}/{} done ({} ok, {} fail), {} active{}",
                               plan.spec.planId, plan.spec.dungeonToken,
                               plan.spec.heroic ? " (heroic)" : "",
                               c.succeeded + c.failed, plan.spec.total, c.succeeded, c.failed,
                               c.activeNow,
                               plan.stopping ? ", draining"
                                             : (plan.backoffMs ? ", backoff" : ""));
}

std::string DcTestPlanManager::StatusText() const
{
    if (_plans.empty())
        return "no test plans active";

    std::string out = std::to_string(_plans.size()) +
                      (_plans.size() == 1 ? " test plan active:" : " test plans active:");
    for (Plan const& plan : _plans)
        out += "\n  " + StatusLine(plan);
    return out;
}

std::vector<DcTestRunLive::PlanSnapshot> DcTestPlanManager::Snapshots() const
{
    std::vector<DcTestRunLive::PlanSnapshot> out;
    out.reserve(_plans.size());
    uint64 const nowMs = NowUnixMs();
    for (Plan const& plan : _plans)
    {
        DcTestRunLive::PlanSnapshot s;
        s.planId = plan.spec.planId;
        s.dungeon = plan.spec.dungeonToken;
        s.total = plan.spec.total;
        s.launched = plan.counters.launched;
        s.succeeded = plan.counters.succeeded;
        s.failed = plan.counters.failed;
        s.activeNow = plan.counters.activeNow;
        s.concurrent = plan.spec.concurrent;
        s.heroic = plan.spec.heroic;
        s.state = plan.stopping      ? "draining"
                  : plan.driverWaitSinceMs ? "waiting for test driver"
                  : plan.backoffMs     ? "backoff"
                                       : "running";
        s.elapsedS = static_cast<uint32>((nowMs - plan.startedAtMs) / 1000);
        out.push_back(std::move(s));
    }
    return out;
}

void DcTestPlanManager::Tick(uint32 diff)
{
    if (_plans.empty())
        return;

    for (Plan& plan : _plans)
        TickPlan(plan, diff);

    for (auto it = _plans.begin(); it != _plans.end();)
    {
        if (DcTestPlan::IsFinished(it->spec, it->counters, it->stopping))
        {
            Finalize(*it);
            it = _plans.erase(it);
        }
        else
            ++it;
    }
}

void DcTestPlanManager::TickPlan(Plan& plan, uint32 diff)
{
    plan.backoffMs = plan.backoffMs > diff ? plan.backoffMs - diff : 0;

    if (plan.stopping)
        return;

    uint32 const wanted = DcTestPlan::LaunchesWanted(
        plan.spec, plan.counters, DcTestRunManager::Instance().CapHeadroom(), plan.backoffMs);
    if (wanted == 0)
        return;

    // At most one launch per world tick per plan: each accepted Start feeds
    // five async bot logins into the shared provisioning budget, and spreading
    // the starts keeps the world tick smooth. The next tick launches the next.
    //
    // Issuer resolution happens per launch, not per plan: the plan may have
    // been started in-game (issuer = that GM) or from the console (issuer =
    // the headless driver). Either way, when the stored issuer is gone the
    // driver takes over — a GM can start a 20-run plan and log off.
    Player* gm = ObjectAccessor::FindConnectedPlayer(plan.gmGuid);
    uint32 const backoffCfg =
        DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.Plan.BackoffMs");
    if (!gm)
    {
        // No issuing GM: either the plan came from the console (there never was
        // one) or the GM logged off mid-campaign. Either way the headless
        // driver takes over — and since a console start registers the plan on
        // the same click that kicks the login off, waiting here is the normal
        // first-tick path, not an error.
        std::string why;
        DcTestDriver::Readiness const ready = DcTestDriver::Ensure(&why);
        if (ready == DcTestDriver::Readiness::Ready)
        {
            gm = DcTestDriver::Get();
            plan.driverWaitSinceMs = 0;
        }
        else
        {
            uint64 const nowMs = NowUnixMs();
            if (!plan.driverWaitSinceMs)
                plan.driverWaitSinceMs = nowMs;
            plan.backoffMs = backoffCfg;

            uint32 const waitCap =
                DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.Plan.DriverWaitMs");
            if (DcTestPlan::DriverWaitVerdict(
                    ready == DcTestDriver::Readiness::PendingLogin,
                    static_cast<uint32>(nowMs - plan.driverWaitSinceMs),
                    waitCap) == DcTestPlan::DriverWait::Abort)
                AbortPlan(plan, "no issuing GM: " + why);
            return;
        }
    }

    uint32 const seed =
        plan.spec.seedBase ? plan.spec.seedBase + plan.counters.launched : 0;

    std::string msg;
    std::string runId;
    DcTestRunManager::StartErr err = DcTestRunManager::StartErr::None;
    bool const ok = DcTestRunManager::Instance().Start(gm, plan.spec.dungeonToken,
                                                       plan.spec.level, seed, plan.spec.heroic,
                                                       plan.spec.gear, &msg, plan.spec.planId,
                                                       &err, &runId);
    if (ok)
    {
        ++plan.counters.launched;
        ++plan.counters.activeNow;
        plan.activeRunIds.push_back(runId);
        plan.transientStreak = 0;
        LOG_INFO("playerbots.dungeonclear", "TESTPLAN {} launched {} ({}/{})",
                 plan.spec.planId, runId, plan.counters.launched, plan.spec.total);
        return;
    }

    switch (err)
    {
        case DcTestRunManager::StartErr::CapHit:
        case DcTestRunManager::StartErr::BotBudget:
        case DcTestRunManager::StartErr::PoolExhausted:
            plan.backoffMs = backoffCfg;
            // With anything in flight a rejection resolves itself when a run
            // finishes; only a rejection with the whole harness idle can be a
            // permanent misconfiguration (empty pool, MaxAddedBots at 0) —
            // count those. The harness-wide check (not just this plan's own
            // children) is what lets plans queue: with MaxPlans unlimited, a
            // plan launched while another is eating the bot budget has nothing
            // active of its own and would otherwise abort itself within three
            // backoffs instead of waiting its turn.
            if (plan.counters.activeNow == 0 && !DcTestRunManager::Instance().IsActive() &&
                ++plan.transientStreak >= kMaxTransientStreak)
                AbortPlan(plan, "stalled: " + msg);
            break;
        default:
            AbortPlan(plan, msg);
            break;
    }
}

void DcTestPlanManager::AbortPlan(Plan& plan, std::string const& reason)
{
    plan.stopping = true;
    plan.result = "aborted";
    plan.abortReason = reason;
    LOG_INFO("playerbots.dungeonclear", "TESTPLAN {} aborting: {}", plan.spec.planId, reason);
    for (std::string const& runId : plan.activeRunIds)
    {
        std::string ignored;
        DcTestRunManager::Instance().Stop(runId, &ignored);
    }
}

void DcTestPlanManager::OnRunFinished(std::string const& planId, DcTestPlan::RunOutcome outcome)
{
    for (Plan& plan : _plans)
    {
        if (plan.spec.planId != planId)
            continue;

        auto it = std::find(plan.activeRunIds.begin(), plan.activeRunIds.end(), outcome.runId);
        if (it != plan.activeRunIds.end())
            plan.activeRunIds.erase(it);
        if (plan.counters.activeNow > 0)
            --plan.counters.activeNow;

        if (outcome.result == "success")
            ++plan.counters.succeeded;
        else
            ++plan.counters.failed;
        plan.outcomes.push_back(std::move(outcome));
        return;
    }
}

void DcTestPlanManager::Finalize(Plan& plan)
{
    DcTestPlanSummary::Stats const stats = DcTestPlanSummary::Build(plan.outcomes);

    DcTestPlanSummary::Header h;
    h.planId = plan.spec.planId;
    h.dungeon = plan.spec.dungeonToken;
    if (DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find(plan.spec.dungeonToken))
        h.dungeonName = row->name;
    h.total = plan.spec.total;
    h.concurrent = plan.spec.concurrent;
    h.level = plan.spec.level;
    h.heroic = plan.spec.heroic;
    h.seedBase = plan.spec.seedBase;
    h.gearIlvl = plan.spec.gear.ilvl;
    h.gearQuality = plan.spec.gear.quality;
    h.startedAtMs = plan.startedAtMs;
    h.endedAtMs = NowUnixMs();
    h.durationS = static_cast<uint32>((h.endedAtMs - h.startedAtMs) / 1000);
    h.result = plan.result.empty() ? "completed" : plan.result;
    h.abortReason = plan.abortReason;
    DcTestPlanSummary::Append(h, stats);

    LOG_INFO("playerbots.dungeonclear",
             "TESTPLAN END {} result={} runs={}/{} ok={} fail={} duration={}s{}",
             h.planId, h.result, stats.launched, h.total, stats.succeeded, stats.failed,
             h.durationS, h.abortReason.empty() ? std::string() : (" reason=" + h.abortReason));

    if (Player* gm = ObjectAccessor::FindConnectedPlayer(plan.gmGuid))
        ChatHandler(gm->GetSession()).SendSysMessage(Acore::StringFormat(
            "Test plan {} {}: {} — {}/{} runs succeeded{}{}",
            h.planId, h.dungeon, h.result, stats.succeeded, stats.launched,
            stats.succeeded ? Acore::StringFormat(", median {}s", stats.medianS) : std::string(),
            h.abortReason.empty() ? std::string() : (" — " + h.abortReason)));
}
