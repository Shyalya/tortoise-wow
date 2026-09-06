# Runtime architecture integration: review guide

This is a broad integration proposal, not a claim of merge readiness. It carries
the runtime architecture and supporting compatibility changes from the fork
through `258e6db5b5b5b5070a9928f5f02a8588b7e342a5`, applied to upstream
`2814ea80`. It excludes deployment files, private operational data, generated
audit exports, world SQL migrations, and the subsequent AHBot administration
commands and auction-volume changes.

## Scope and review order

1. `MapManager`, `Map`, `MapTaskExecutor`, `ThreadPool`: shared worker ownership,
   joined phases, read-only cell discovery, idle-AI batches, and ordered gameplay.
   Legacy motion/cell execution lanes must not run a second copy of gameplay.
2. `WorldSession`, character login, SQL queues, `PlayerbotLoginMgr`: player login
   priority, bounded bot admission, owner-thread completion, and queue budgets.
3. `RandomPlayerbotMgr`, `Engine`, spatial/location indexes: resumable maintenance,
   deferred background work, bounded retries, and indexed location filtering.
   Failure caching is not a substitute for native action prerequisites.
4. Terrain/Detour: tile mutation gates, reader lifetime, cleanup and cached query
   ownership. Shutdown and cross-map transitions require special review.
5. Movement/visibility: inverse viewer bookkeeping and reusable compression and
   path storage. Verify insert/remove symmetry through transfers and logout.
6. Content hooks and runtime compatibility: script owner/instance guards, bot
   lifecycle, trainer/spell handling, transport and travel transitions, group
   behavior, and timer accounting. This proposal contains these supporting
   changes, not only a replacement thread pool.

The latest upstream Arathi Basin capture implementation is preserved. The old
banner-fragment regression was removed from the imported combined test because
it exercised the replaced implementation; other assertions remain.

The integrated flight-cache correction overlaps the separately proposed narrow
flight-cache fix. Merge/review the narrow change independently, then rebase this
proposal before merging it. Neither requires renumbering flight-master NPCs.

## Configuration

New scheduling controls are documented in the distributed templates. Worker pool
sizes require a restart. Existing configurations should be reviewed explicitly;
do not blindly copy another realm's configuration. Population, save interval,
visibility range, taxi-cheat, chat and AHBot account defaults are not copied from
the deployment. Existing AHBot background scheduling participates in the runtime
changes, but the later AHBot command/rebuild feature is excluded.

## Diagnostic inventory and removal boundary

The instrumentation is part of this review, not assumed free:

| Facility | Main implementation | Control / cleanup consideration |
| --- | --- | --- |
| Architecture phase metrics | `ArchitectureDiagnostics.h`, map/world/session hooks | `Diagnostics.Architecture.Enabled=0` by default; interval controls aggregation |
| Stall breadcrumbs | `ExecutionWatch.h`, `Master.cpp` | `Diagnostics.StallSeconds=0` disables reporting; do not remove ownership synchronization with diagnostics |
| Player update aggregates | `Map.cpp`, performance log | `PerformanceLog.PlayerUpdateSummaryInterval=0` disables summaries |
| Detailed work accounting | `DetailedWorkDiagnostics.h`, `WorkMetrics.h` | Review call sites before removal; timing/counters can still cost work when output is quiet |
| Bounded bot behavior samples | `BoundedBotTrace.h`, bot diagnostics and action hooks | `AiPlayerbot.BehaviorTrace=0` by default; bounded bots, rate, duration and total lines |
| Bot memory/cache reporting | playerbot AI/config | `AiPlayerbot.MemoryTelemetryInterval`; distinguish reporting from required cache cleanup |
| Log retention | `Log.cpp` | Size/retention controls govern startup rotation, not scheduling |

When diagnostics are retired, remove their timers, counters, hooks and config keys
together. Keep correctness mechanisms (owner barriers, mutation gates, fairness
deadlines, queue limits and timer accounting). Turning output off does not prove
zero instrumentation overhead. Benchmark enabled and disabled variants.

## Validation and outstanding work

The standalone `tests/architecture` CMake suite passes 33/33 tests on Windows.
Tests include extracted native function bodies, mock boundaries, helper tests,
and source-contract checks. They do not execute the entire world server.

The originating fork was reported playable with 6,000 bots, but that is not a
controlled comparison and does not validate this newly integrated upstream
branch. A fresh full server build/link, race-sensitive runtime validation,
login/logout and map-transfer stress, populated instance/transport tests, and
full-load latency/memory measurements remain required before merge/deployment.

Suggested comparison: use identical data/configuration/hardware and record
human action latency, tick percentiles and stalls, login completion time, bot
population/activity, memory, and per-phase costs. Include empty-world and normal
population controls, not just the maximum-bot case.
