/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcPathWorker.h"

#include <utility>

#include "Log.h"
#include "Timer.h"

DcPathWorker& DcPathWorker::Instance()
{
    // Meyers singleton: constructed on first Submit() (from a map thread). The
    // worker thread is started lazily via call_once so nothing spins up before
    // the module is actually used, and never before main().
    static DcPathWorker instance;
    return instance;
}

DcPathWorker::~DcPathWorker()
{
    // Backstop only — OnShutdown should already have stopped us. Idempotent.
    Stop();
}

void DcPathWorker::EnsureStarted()
{
    std::call_once(_startOnce, [this]()
    {
        _running.store(true, std::memory_order_release);
        _thread = std::thread(&DcPathWorker::WorkerLoop, this);
    });
}

void DcPathWorker::WorkerLoop()
{
    while (_running.load(std::memory_order_acquire))
    {
        Job job;
        _queue.WaitAndPop(job);

        // WaitAndPop returns a default-constructed Job when the queue was
        // cancelled/empty (Stop()). jobId 0 is never handed out, so it doubles
        // as the "nothing to do / shutting down" sentinel.
        if (!_running.load(std::memory_order_acquire) || job.jobId == 0)
            continue;

        LongRangePathfinder::RawResult raw;
        try
        {
            // The ONLY work done off the map thread: pure navmesh A*+smooth.
            // job.mesh keeps the navmesh alive for the whole call.
            raw = LongRangePathfinder::BuildCoreFromMesh(job.mesh.get(), job.mapId,
                                                         job.sx, job.sy, job.sz,
                                                         job.tx, job.ty, job.tz);
        }
        catch (...)
        {
            // Detour is C and BuildCoreFromMesh only allocates via std::vector,
            // so this is just belt-and-suspenders: never let an exception
            // escape the thread (that would call std::terminate). The map
            // thread treats a failed result as "rebuild".
            raw = LongRangePathfinder::RawResult{};
            raw.tx = job.tx;
            raw.ty = job.ty;
            raw.tz = job.tz;
            raw.failureReason = "exception in pathfinding worker";
        }

        Completed done;
        done.forEntry = job.forEntry;
        done.mapId = job.mapId;
        done.completedMs = getMSTime();
        done.botGuid = job.botGuid;
        done.raw = std::move(raw);

        {
            std::lock_guard<std::mutex> lock(_mailboxLock);
            _mailbox[job.jobId] = std::move(done);
        }
        // job (and thus its mesh shared_ptr) is released here — the navmesh is
        // only pinned for the build, not for result storage (RawResult carries
        // plain floats, no mesh reference).
    }
}

uint64 DcPathWorker::Submit(uint32 mapId, uint32 forEntry, ObjectGuid botGuid,
                            std::shared_ptr<dtNavMesh> mesh,
                            float sx, float sy, float sz, float tx, float ty, float tz)
{
    EnsureStarted();

    Job job;
    job.jobId = _nextJobId.fetch_add(1, std::memory_order_relaxed);
    job.mapId = mapId;
    job.forEntry = forEntry;
    job.botGuid = botGuid;
    job.mesh = std::move(mesh);
    job.sx = sx; job.sy = sy; job.sz = sz;
    job.tx = tx; job.ty = ty; job.tz = tz;

    uint64 const id = job.jobId;
    _queue.Push(job);
    return id;
}

bool DcPathWorker::TryTake(uint64 jobId, LongRangePathfinder::RawResult& out,
                           uint32& forEntry, uint32& mapId)
{
    std::lock_guard<std::mutex> lock(_mailboxLock);
    auto it = _mailbox.find(jobId);
    if (it == _mailbox.end())
        return false;

    out = std::move(it->second.raw);
    forEntry = it->second.forEntry;
    mapId = it->second.mapId;
    _mailbox.erase(it);
    return true;
}

void DcPathWorker::Sweep(uint32 maxAgeMs)
{
    std::lock_guard<std::mutex> lock(_mailboxLock);
    if (_mailbox.empty())
        return;

    uint32 const now = getMSTime();
    for (auto it = _mailbox.begin(); it != _mailbox.end();)
    {
        if (getMSTimeDiff(it->second.completedMs, now) >= maxAgeMs)
        {
            LOG_DEBUG("playerbots.dungeonclear",
                      "[dungeon-clear] sweeping orphaned async path result job={} bot={}",
                      it->first, it->second.botGuid.ToString());
            it = _mailbox.erase(it);
        }
        else
            ++it;
    }
}

void DcPathWorker::Stop()
{
    _running.store(false, std::memory_order_release);

    // Wake WaitAndPop and drop any queued jobs (their mesh shared_ptrs release
    // as the queued Jobs are destroyed).
    _queue.Cancel();

    if (_thread.joinable())
        _thread.join();

    std::lock_guard<std::mutex> lock(_mailboxLock);
    _mailbox.clear();
}
