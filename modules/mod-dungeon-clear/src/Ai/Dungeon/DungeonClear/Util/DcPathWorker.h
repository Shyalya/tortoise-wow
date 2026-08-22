/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCPATHWORKER_H
#define _PLAYERBOT_DCPATHWORKER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "ObjectGuid.h"
#include "PCQueue.h"
#include "Ai/Dungeon/DungeonClear/Util/LongRangePathfinder.h"  // RawResult

class dtNavMesh;

// DcPathWorker offloads the expensive, navmesh-only long-range A*+smooth
// (LongRangePathfinder::BuildCoreFromMesh) onto a single dedicated background
// thread so a cache-miss no longer micro-stutters the map-update tick that the
// bot AI runs on (review point #1).
//
// MEMORY/CRASH SAFETY (see the plan doc's matrix):
//   * A job carries ONLY value types plus a shared_ptr<dtNavMesh>. It never
//     holds a Player*/Map*/AiObjectContext*, so the worker cannot touch live
//     game state or outlive a logging-out bot.
//   * The shared_ptr pins the map's navmesh alive for exactly the duration of
//     the build, so an instance teardown mid-build can't free the mesh under
//     the reader. AzerothCore never removeTile()s mid-run, so the only residual
//     race is a concurrent addTile() (grid load) — which never frees memory, so
//     the worst case is an invalid polyref that Detour rejects → "unreachable"
//     → rebuilt next tick. No use-after-free, no leak.
//   * Exactly one job per bot is ever in flight (the caller gates on its stored
//     pendingJobId), so the queue/mailbox stay tiny and bounded.
//   * Results are keyed by a monotonic jobId; the consumer erases on take and a
//     periodic Sweep() drops orphans (logout/dc-off), so nothing accumulates.
//   * Stop() (called from WorldScript::OnShutdown, before maps unload) cancels
//     the queue, joins the thread, and clears the mailbox — releasing every
//     held mesh ref. The destructor calls Stop() too, idempotently.
class DcPathWorker
{
public:
    static DcPathWorker& Instance();

    DcPathWorker(DcPathWorker const&) = delete;
    DcPathWorker& operator=(DcPathWorker const&) = delete;

    // Queue a build. `mesh` MUST be the owning map's navmesh shared_ptr
    // (Map::GetMapCollisionData().GetMMapNavMeshSharedPtr()) so it stays alive
    // across the worker round-trip. Returns a non-zero jobId to poll with.
    uint64 Submit(uint32 mapId, uint32 forEntry, ObjectGuid botGuid,
                  std::shared_ptr<dtNavMesh> mesh,
                  float sx, float sy, float sz, float tx, float ty, float tz);

    // If job `jobId` has finished, move its result out (plus the boss entry and
    // map id it was built for, for staleness checks), erase the mailbox entry,
    // and return true. Otherwise return false (still running / unknown id).
    bool TryTake(uint64 jobId, LongRangePathfinder::RawResult& out,
                 uint32& forEntry, uint32& mapId);

    // Drop completed-but-uncollected results older than maxAgeMs (orphaned by a
    // bot logging out or toggling dc off before it polled). Cheap; call from a
    // global tick.
    void Sweep(uint32 maxAgeMs);

    // Stop the worker thread and free everything. Idempotent; safe to call when
    // never started. Call from WorldScript::OnShutdown.
    void Stop();

private:
    DcPathWorker() = default;
    ~DcPathWorker();

    struct Job
    {
        uint64 jobId{0};
        uint32 mapId{0};
        uint32 forEntry{0};
        ObjectGuid botGuid;
        std::shared_ptr<dtNavMesh> mesh;   // pins the navmesh for the build
        float sx{0.0f}, sy{0.0f}, sz{0.0f};
        float tx{0.0f}, ty{0.0f}, tz{0.0f};
    };

    struct Completed
    {
        uint32 forEntry{0};
        uint32 mapId{0};
        uint32 completedMs{0};
        ObjectGuid botGuid;
        LongRangePathfinder::RawResult raw;
    };

    void EnsureStarted();
    void WorkerLoop();

    std::once_flag _startOnce;
    std::thread _thread;
    std::atomic<bool> _running{false};
    std::atomic<uint64> _nextJobId{1};
    ProducerConsumerQueue<Job> _queue;

    std::mutex _mailboxLock;
    std::unordered_map<uint64, Completed> _mailbox;
};

#endif
