# tools/ — offline developer utilities

**None of this directory is part of the running module.** These scripts are not
compiled in (`tools/` is deliberately excluded from the CMake source GLOB — see
`CMakeLists.txt`), are not invoked by any hook, tick, command or config option,
and are never executed by worldserver. Nothing here can affect a running server
or a connected client. They are run by hand, by a developer, from a shell.

The DB-backed ones open a read-only `mysql` CLI session against `acore_world`
and issue `SELECT`s only — they write nothing, to the DB or to disk.

| script | what it does | reads |
|---|---|---|
| `room_aggro_scan.py` | finds bosses sitting in a dense room of static trash (candidates for pre-clear) | `acore_world` |
| `room_aggro_audit.py` | diffs `RoomAggroRegistry` against SmartAI actions 38/39 + C++ idioms | `acore_world`, `src/` |
| `dungeon_model.py` | ASCII top-down spatial model of a dungeon; DC ring / dead-band report | `acore_world` |
| `probe_navmesh.py` | dumps Detour navmesh verts/polys near a point | `mmaps/` |
| `slice_mapdata.py` | slices navmesh test fixtures for the headless sim | `mmaps/` |
| `dc_test_run.py` | **everything known about one test run, by its id** — record, plan, live state, and the log lines sliced down to that run | run logs, `*.log` |
| `dc_analytics.py` | ingests test-run JSONL logs into a queryable SQLite db | run logs |
| `check_config_reads.py` | build guard: every tunable must be read via `DcSettings` | `src/` |
| `check_determinism.sh` | determinism guard for the decision cores | build output |
| `dc-feature.sh` | starts a feature in an isolated git worktree | git |

`check_config_reads.py` and `check_determinism.sh` are CI/build-time guards, not
runtime code either.

`dc_test_run.py` is the entry point for "look at run tr-…": it resolves the
record out of `dc_testruns.jsonl` (or `dc_testrun_live.json` if the run is still
in flight), pulls in the plan and its sibling runs, prints the teardown DcDiag
snapshot, and then slices every `*.log` in the data dir down to the lines that
belong to that one run — correlating on the run's own bot names inside the run's
time window, because no log line carries a run id. See `--help` for the
drill-down flags (`--logs`, `--grep`, `--dump`, `--json`).

The spawn-table tools handle both `creature.id1` and the newer `creature.id`
spelling by probing `information_schema`, so they work across the upstream
rename.
