/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestRunManager.h"

#include <ctime>
#include <fstream>
#include <limits>

#include "CharacterCache.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"

#include "Playerbots.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"

#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"

#include "TestRun/DcTestComp.h"
#include "TestRun/DcTestDungeonRegistry.h"
#include "TestRun/DcTestPlanManager.h"
#include "TestRun/DcTestRunJob.h"
#include "TestRun/DcTestRoster.h"
#include "TestRun/DcTestRunLiveJson.h"
#include "TestRun/DcTestRunRecord.h"
#include "TestRun/DcTestRunSelect.h"

namespace
{
    uint64 NowUnixMs()
    {
        return static_cast<uint64>(std::time(nullptr)) * 1000;
    }
}

DcTestRunManager& DcTestRunManager::Instance()
{
    static DcTestRunManager instance;
    return instance;
}

bool DcTestRunManager::Start(Player* gm, std::string const& dungeonToken,
                             uint32 levelOverride, uint32 seed, bool heroic,
                             DcTestGearTiers::Spec const& gear, std::string* msg,
                             std::string const& planId, StartErr* errOut, std::string* runIdOut)
{
    if (errOut)
        *errOut = StartErr::None;

    auto fail = [&](StartErr kind, std::string const& why) -> bool
    {
        if (msg)
            *msg = "Test run not started: " + why;
        if (errOut)
            *errOut = kind;
        return false;
    };

    DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find(dungeonToken);
    if (!row)
        return fail(StartErr::UnknownDungeon,
                    "unknown dungeon '" + dungeonToken + "' — see .dc test list");

    // heroicLevel 0 = heroic not offered: only classic dungeons, which have no
    // heroic difficulty at all (TBC rows run at 70, WotLK at 80).
    if (heroic && row->heroicLevel == 0)
        return fail(StartErr::UnknownDungeon,
                    "'" + std::string(row->token) + "' has no heroic mode (classic dungeons have none)");

    if (!gm || !GET_PLAYERBOT_MGR(gm))
        return fail(StartErr::NoMgr, "no playerbot manager on this account");

    // Concurrency cap (0 = unlimited). World-thread read, no lock.
    uint32 const maxConcurrent = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.MaxConcurrent");
    if (maxConcurrent != 0 && _runs.size() >= maxConcurrent)
        return fail(StartErr::CapHit,
                    "max concurrent test runs reached (" + std::to_string(maxConcurrent) +
                    ") — .dc test stop <run> first");

    // MaxAddedBots pre-check: the core enforces this silently inside
    // AddPlayerBot, so without the pre-check a party over the cap surfaces only
    // as a 60s spawn timeout. Name the knob instead.
    uint32 const currentBots = uint32(GET_PLAYERBOT_MGR(gm)->GetAllBots().size());
    if (sPlayerbotAIConfig.maxAddedBots > 0 &&
        currentBots + DcTestComp::kPartySize > static_cast<uint32>(sPlayerbotAIConfig.maxAddedBots))
        return fail(StartErr::BotBudget,
                    "would exceed AiPlayerbot.MaxAddedBots (" +
                    std::to_string(sPlayerbotAIConfig.maxAddedBots) + "; " +
                    std::to_string(currentBots) + " bots already added, this run needs " +
                    std::to_string(DcTestComp::kPartySize) + ") — raise it or stop a run");

    // Create picks slot guids skipping _reservedGuids (all synchronous on the
    // world thread — no TOCTOU with other runs).
    std::string err;
    std::unique_ptr<DcTestRunJob> job =
        DcTestRunJob::Create(gm, *row, levelOverride, seed, heroic, gear, _reservedGuids, planId, &err);
    if (!job)
        return fail(StartErr::PoolExhausted, err);

    if (runIdOut)
        *runIdOut = job->RunId();

    // Reserve the slot guids for the life of the run (covers both the async
    // login and the async logout windows — released only at job erase).
    for (ObjectGuid const& g : job->BotGuids())
        _reservedGuids.insert(g);

    std::string const started = "Test run started: " + job->StatusLine();
    {
        std::lock_guard<std::mutex> lock(_runsMutex);
        _runs.push_back(std::move(job));
    }
    if (msg)
        *msg = started;
    return true;
}

bool DcTestRunManager::StartRoster(Player* gm, std::string const& dungeonToken,
                                   std::string const& partySpec, bool heroic, std::string* msg,
                                   std::string const& planId, StartErr* errOut,
                                   std::string* runIdOut)
{
    if (errOut)
        *errOut = StartErr::None;

    auto fail = [&](StartErr kind, std::string const& why) -> bool
    {
        if (msg)
            *msg = "Test run not started: " + why;
        if (errOut)
            *errOut = kind;
        return false;
    };

    DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find(dungeonToken);
    if (!row)
        return fail(StartErr::UnknownDungeon,
                    "unknown dungeon '" + dungeonToken + "' — see .dc test list");

    if (heroic && row->heroicLevel == 0)
        return fail(StartErr::UnknownDungeon,
                    "'" + std::string(row->token) + "' has no heroic mode (classic dungeons have none)");

    if (!gm || !GET_PLAYERBOT_MGR(gm))
        return fail(StartErr::NoMgr, "no playerbot manager on this account");

    uint32 const maxConcurrent = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.MaxConcurrent");
    if (maxConcurrent != 0 && _runs.size() >= maxConcurrent)
        return fail(StartErr::CapHit,
                    "max concurrent test runs reached (" + std::to_string(maxConcurrent) +
                    ") — .dc test stop <run> first");

    // No MaxAddedBots pre-check here: roster members log in masterless
    // (AddPlayerBot with masterAccountId 0), and that cap is only applied to bots
    // added against a master's account.

    DcTestRoster::Result const parsed = DcTestRoster::Parse(partySpec);
    if (parsed.kind != DcTestRoster::Kind::Ok)
        return fail(StartErr::BadRoster, parsed.detail);

    std::vector<DcTestRunJob::RosterEntry> roster;
    roster.reserve(parsed.members.size());
    uint32 team = 0;
    bool teamKnown = false;

    for (DcTestRoster::Member const& m : parsed.members)
    {
        // The cache is keyed by the stored (capitalised) name; accept whatever
        // case the Command Deck or the typist used.
        std::string name = m.name;
        normalizePlayerName(name);
        ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(name);
        if (!guid)
            return fail(StartErr::BadRoster, "no character named '" + m.name + "'");

        // A human playing the character wins: AddPlayerBot no-ops on a connected
        // character, so without this the run would sit out its spawn timeout.
        if (ObjectAccessor::FindConnectedPlayer(guid))
            return fail(StartErr::CharacterOnline,
                        "'" + name + "' is logged in — a roster character must be offline");

        if (_reservedGuids.find(guid) != _reservedGuids.end())
            return fail(StartErr::CharacterBusy,
                        "'" + name + "' is already in another live test run");

        // GetCharacterTeamByGuid returns TeamId, and 0 both for Alliance and for
        // an unknown guid — safe here only because the guid resolved above.
        uint32 const memberTeam = sCharacterCache->GetCharacterTeamByGuid(guid);
        if (!teamKnown)
        {
            team = memberTeam;
            teamKnown = true;
        }
        else if (memberTeam != team)
            return fail(StartErr::FactionMismatch,
                        "'" + name + "' is not the same faction as the rest of the roster — "
                        "a cross-faction party cannot be grouped");

        roster.push_back({guid, name, m.role});
    }

    std::string err;
    std::unique_ptr<DcTestRunJob> job =
        DcTestRunJob::CreateFromRoster(gm, *row, heroic, roster, planId, &err);
    if (!job)
        return fail(StartErr::BadRoster, err);

    if (runIdOut)
        *runIdOut = job->RunId();

    for (ObjectGuid const& g : job->BotGuids())
        _reservedGuids.insert(g);

    std::string const started = "Test run started: " + job->StatusLine();
    {
        std::lock_guard<std::mutex> lock(_runsMutex);
        _runs.push_back(std::move(job));
    }
    if (msg)
        *msg = started;
    return true;
}

bool DcTestRunManager::WatchTarget(std::string const& selector, ObjectGuid* tankOut,
                                   std::string* msg, std::string* tokenOut) const
{
    std::vector<DcTestRunSelect::RunRef> refs;
    refs.reserve(_runs.size());
    for (auto const& job : _runs)
        refs.push_back({job->RunId(), job->DungeonToken()});

    DcTestRunSelect::Result const res = DcTestRunSelect::Resolve(selector, refs);
    switch (res.kind)
    {
        case DcTestRunSelect::Kind::NoRuns:
            if (msg)
                *msg = "no test run active";
            return false;
        case DcTestRunSelect::Kind::Ambiguous:
            if (msg)
                *msg = "multiple test runs active — specify a runId or dungeon token:\n" +
                       ListActiveRuns();
            return false;
        case DcTestRunSelect::Kind::NotFound:
            if (msg)
                *msg = "no run matches '" + selector + "' — active runs:\n" + ListActiveRuns();
            return false;
        case DcTestRunSelect::Kind::All:
        case DcTestRunSelect::Kind::Matched:
            break;
    }

    // A camera can only sit in one instance. "all", or a token matching several
    // runs, is a question with no single answer — say so instead of guessing.
    if (res.indices.size() != 1)
    {
        if (msg)
            *msg = "'" + selector + "' matches " + std::to_string(res.indices.size()) +
                   " runs — watch one at a time:\n" + ListActiveRuns();
        return false;
    }

    DcTestRunJob const* job = _runs[res.indices.front()].get();
    if (tankOut)
        *tankOut = job->TankGuid();
    if (tokenOut)
        *tokenOut = job->DungeonToken();
    if (msg)
        *msg = job->RunId() + " (" + job->DungeonToken() + ")";
    return true;
}

bool DcTestRunManager::NextWatchTarget(Player* watcher, ObjectGuid* tankOut,
                                       std::string* msg, std::string* tokenOut) const
{
    if (!watcher)
    {
        if (msg)
            *msg = "no watcher";
        return false;
    }

    // Watchable = the tank is in world and can be sat on right now. A run that
    // is still logging its party in is simply not in the tour yet.
    std::vector<DcTestRunJob const*> watchable;
    std::size_t current = DcTestRunSelect::kNone;
    watchable.reserve(_runs.size());
    for (auto const& job : _runs)
    {
        Player* tank = ObjectAccessor::FindConnectedPlayer(job->TankGuid());
        if (!tank || !tank->IsInWorld())
            continue;

        // Same map copy = this is the run the watcher is on. Compared by
        // Map*, which is per-instance, so two runs of the same dungeon are
        // never confused for each other.
        if (watcher->IsInWorld() && tank->FindMap() == watcher->FindMap())
            current = watchable.size();
        watchable.push_back(job.get());
    }

    std::size_t const next = DcTestRunSelect::NextWatchIndex(watchable.size(), current);
    if (next == DcTestRunSelect::kNone)
    {
        if (msg)
            *msg = watchable.empty()
                       ? (_runs.empty() ? "no test run active"
                                        : "no test run has a tank in world yet — try again in a moment")
                       : "only one test run is live and you are already watching it";
        return false;
    }

    DcTestRunJob const* job = watchable[next];
    if (tankOut)
        *tankOut = job->TankGuid();
    if (tokenOut)
        *tokenOut = job->DungeonToken();
    if (msg)
        *msg = job->RunId() + " (" + job->DungeonToken() + ")" +
               " — run " + std::to_string(next + 1) + " of " +
               std::to_string(watchable.size());
    return true;
}

bool DcTestRunManager::Stop(std::string const& selector, std::string* msg)
{
    std::vector<DcTestRunSelect::RunRef> refs;
    refs.reserve(_runs.size());
    for (auto const& job : _runs)
        refs.push_back({job->RunId(), job->DungeonToken()});

    DcTestRunSelect::Result const res = DcTestRunSelect::Resolve(selector, refs);
    switch (res.kind)
    {
        case DcTestRunSelect::Kind::NoRuns:
            if (msg)
                *msg = "no test run active";
            return false;
        case DcTestRunSelect::Kind::Ambiguous:
            if (msg)
                *msg = "multiple test runs active — specify a runId, dungeon token, or 'all':\n" +
                       ListActiveRuns();
            return false;
        case DcTestRunSelect::Kind::NotFound:
            if (msg)
                *msg = "no run matches '" + selector + "' — active runs:\n" + ListActiveRuns();
            return false;
        case DcTestRunSelect::Kind::All:
        case DcTestRunSelect::Kind::Matched:
            break;
    }

    std::string acc;
    for (std::size_t idx : res.indices)
    {
        DcTestRunJob* job = _runs[idx].get();
        if (job->Done())
            continue;  // already torn down this tick, awaiting erase
        if (job->IsMonitoring())
            job->RequestAbort("aborted by .dc test stop");
        else
            job->AbortSetup("aborted by .dc test stop");  // synchronous FailSetup -> Teardown
        if (!acc.empty())
            acc += '\n';
        acc += "aborting " + job->RunId() + " (" + job->DungeonToken() + ")";
    }
    if (msg)
        *msg = acc;
    return true;
}

std::string DcTestRunManager::ListActiveRuns() const
{
    std::string out;
    for (auto const& job : _runs)
    {
        if (!out.empty())
            out += '\n';
        out += "  " + job->StatusLine();
    }
    return out;
}

std::string DcTestRunManager::StatusText() const
{
    if (_runs.empty())
        return "no test runs active";

    uint32 const maxConcurrent = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.MaxConcurrent");
    std::string out = std::to_string(_runs.size()) +
                      (_runs.size() == 1 ? " test run active" : " test runs active") + " (max " +
                      (maxConcurrent == 0 ? std::string("unlimited") : std::to_string(maxConcurrent)) +
                      "):";
    for (auto const& job : _runs)
        out += "\n  " + job->StatusLine();
    return out;
}

uint32 DcTestRunManager::CapHeadroom() const
{
    uint32 const maxConcurrent = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.MaxConcurrent");
    if (maxConcurrent == 0)
        return std::numeric_limits<uint32>::max();
    return maxConcurrent > _runs.size() ? maxConcurrent - static_cast<uint32>(_runs.size()) : 0;
}

void DcTestRunManager::Tick(uint32 diff)
{
    // A plan in a between-runs window (backoff, or all children just finished)
    // has zero jobs here but must keep the live heartbeat fresh — otherwise
    // the dashboard reads the file as stale mid-campaign.
    bool const plansActive = DcTestPlanManager::Instance().HasActivePlans();
    if (_runs.empty() && !plansActive)
    {
        // One final active:false write on the tick everything drained, so the
        // dashboard flips at once rather than waiting out the staleness window.
        if (_liveWasActive)
        {
            _liveWasActive = false;
            WriteLiveStatus();
        }
        return;
    }
    _liveWasActive = true;

    if (!_runs.empty())
    {
        // One factory-provision roll per world tick across ALL runs; the
        // rotating offset gives each run a fair first crack at the budget.
        std::size_t const n = _runs.size();
        bool provisionBudget = true;
        for (std::size_t k = 0; k < n; ++k)
            _runs[(_provisionRR + k) % n]->Tick(diff, provisionBudget);
        _provisionRR = (_provisionRR + 1) % n;

        // Erase finished jobs, releasing their reserved guids first and handing
        // plan children's outcomes to the plan scheduler. Hold the lock across
        // the whole pass so an observer thread can't be mid-callback on a job
        // we destroy.
        std::lock_guard<std::mutex> lock(_runsMutex);
        for (auto it = _runs.begin(); it != _runs.end();)
        {
            if ((*it)->Done())
            {
                if (!(*it)->PlanId().empty())
                {
                    DcTestRunRecord::Record const& rec = (*it)->RecordData();
                    DcTestPlan::RunOutcome outcome;
                    outcome.runId = rec.runId;
                    outcome.result = rec.result;
                    outcome.failReason = rec.failReason;
                    outcome.durationS = rec.durationS;
                    outcome.bossesKilled = rec.bossesKilled;
                    outcome.bossesTotal = rec.bossesTotal;
                    outcome.bossRoster = rec.bossRoster;
                    outcome.wipeOpponent = rec.wipeOpponent;
                    outcome.wipeOnBoss = rec.wipeOnBoss;
                    for (DcTestRunRecord::BossKill const& kill : rec.bossTimeline)
                        if (kill.via == "mask")
                            outcome.bossKills.push_back(kill.name);
                    outcome.pulls.reserve(rec.pulls.size());
                    for (DcTestRunRecord::PullEntry const& p : rec.pulls)
                        outcome.pulls.push_back({p.predictedCount, p.observedMax,
                                                 p.advanced, p.wipedHere});
                    DcTestPlanManager::Instance().OnRunFinished((*it)->PlanId(),
                                                                std::move(outcome));
                }
                for (ObjectGuid const& g : (*it)->BotGuids())
                    _reservedGuids.erase(g);
                it = _runs.erase(it);
            }
            else
                ++it;
        }
    }

    // Live-status throttle: every ~2s while runs or plans exist. The final
    // active:false write happens above, on the first fully-idle tick.
    _liveAccumMs += diff;
    if (_liveAccumMs >= 2000)
    {
        _liveAccumMs = 0;
        WriteLiveStatus();
    }
}

void DcTestRunManager::WriteLiveStatus()
{
    std::vector<DcTestRunLive::RunSnapshot> snaps;
    {
        std::lock_guard<std::mutex> lock(_runsMutex);
        snaps.reserve(_runs.size());
        for (auto const& job : _runs)
            snaps.push_back(job->Snapshot());
    }

    std::string const json = DcTestRunLive::Build(NowUnixMs() / 1000, snaps,
                                                  DcTestPlanManager::Instance().Snapshots());
    std::ofstream f(DcTestRunRecord::LivePath(), std::ios::out | std::ios::trunc);
    if (!f.is_open())
        return;
    f << json << '\n';
}

void DcTestRunManager::OnRunDisabled(Player* leader, std::string const& reason)
{
    if (!leader)
        return;
    ObjectGuid const guid = leader->GetObjectGuid();

    std::lock_guard<std::mutex> lock(_runsMutex);
    for (auto const& job : _runs)
        if (job->TankGuid() == guid && job->IsMonitoring())
        {
            job->OnRunDisabled(reason);
            return;
        }
}

void DcTestRunManager::OnStatusPayload(ObjectGuid tank, std::string const& payload)
{
    std::lock_guard<std::mutex> lock(_runsMutex);
    for (auto const& job : _runs)
        if (job->TankGuid() == tank)
        {
            job->OnStatusPayload(payload);
            return;
        }
}
