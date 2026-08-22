#!/usr/bin/env python3
"""
dc_analytics.py — turn the test-run JSONL append logs into a queryable SQLite db.

`.dc test` writes one JSON object per finished run to `dc_testruns.jsonl` and one
per finished campaign to `dc_testplans.jsonl`. Those files are append-only, deeply
nested, and versioned (run schema v7/v8/v9, plan schema v4/v5), which makes them
fine for a per-run post-mortem and useless for the questions that actually decide
whether a change helped:

  - did the wipe rate move after commit X?
  - is the pull predictor over- or under-estimating, and where?
  - which runs "succeeded" while burning 40 resnap attempts?
  - how much of a run is spent resting vs moving vs fighting?

So: flatten the records into relational tables, normalise the freeform
`failReason` prose into a groupable class, attribute each run to the commit that
was current when it started, and ship views for the questions above.

The schema drift is purely additive (v7 fields are a subset of v8, v8 of v9), so
every read is a defaulted `.get()` — there are no per-version branches.

Usage:
  python3 tools/dc_analytics.py                      # incremental ingest
  python3 tools/dc_analytics.py --rebuild            # drop and re-ingest
  python3 tools/dc_analytics.py --stats              # summary of what's in the db
  python3 tools/dc_analytics.py --views              # list the canned views
  python3 tools/dc_analytics.py --sql "SELECT ..."   # ad-hoc query, tabulated
  python3 tools/dc_analytics.py --mark-deploy <sha>  # record a real deploy, now
  python3 tools/dc_analytics.py --db /path/to.sqlite --data-dir /path/to/bin

DEPLOY ATTRIBUTION CAVEAT: unless you record real markers with --mark-deploy, a
run is attributed to the newest commit whose *commit timestamp* precedes the run.
A commit is not a deploy — the worldserver only picks up code on a rebuild and
restart, so proxy attribution lags reality by however long you sat on a commit.
Rows carry their provenance in `deploys.source` ('git-log' vs 'marker'); trust
proxy rows for "roughly which era of the code was this", not for bisection.
"""
import argparse
import json
import re
import sqlite3
import subprocess
import sys
from pathlib import Path

ETL_VERSION = 1

RUNS_FILE = "dc_testruns.jsonl"
PLANS_FILE = "dc_testplans.jsonl"

# Repos whose commits are candidate build markers, relative to the workspace root.
#
# `paths` restricts history to commits that can actually change bot behaviour.
# Without it the dashboard merges dominate — they live in this same repo, ship no
# C++, and get credited for swings they cannot possibly have caused.
DEPLOY_REPOS = [
    ("module", "azerothcore-wotlk/modules/mod-dungeon-clear", ["src", "conf"]),
    ("core", "azerothcore-wotlk", ["src"]),
]

# ---------------------------------------------------------------------------
# failReason normalisation
#
# 101 failures in the corpus produced 68 distinct reason strings, because the
# prose embeds bot and mob names: "party wiped to trash: Sethekk Shaman (last
# standing: Zyaly)" is its own bucket. Each rule below maps a reason to a stable
# class, plus a template with the volatile names replaced by placeholders, plus
# whichever of (opponent, victim) the prose exposed. That turns 68 buckets into
# ~10 and puts the names in their own columns where they can be grouped on.
#
# Order matters: first match wins.
# ---------------------------------------------------------------------------
FAIL_RULES = [
    (
        "wipe_boss",
        re.compile(r"^party wiped on (?P<opponent>.+?) \(last standing: (?P<victim>.+?)\)$"),
        "party wiped on <boss> (last standing: <bot>)",
    ),
    (
        "wipe_trash",
        re.compile(r"^party wiped to trash: (?P<opponent>.+?) \(last standing: (?P<victim>.+?)\)$"),
        "party wiped to trash: <mob> (last standing: <bot>)",
    ),
    (
        "wipe_out_of_combat",
        re.compile(r"^party wiped out of combat \(last standing: (?P<victim>.+?)\)$"),
        "party wiped out of combat (last standing: <bot>)",
    ),
    (
        "rez_impossible",
        re.compile(
            r"^run disabled: (?P<victim>.+?) died and no one left alive can resurrect"
            r"(?: — killed by (?:trash: )?(?P<opponent>.+?))?$"
        ),
        "run disabled: <bot> died and no one left alive can resurrect — killed by <mob>",
    ),
    (
        "rez_timeout",
        re.compile(
            r"^run disabled: Couldn't get (?P<victim>.+?) resurrected in time"
            r"(?: — killed by (?:trash: )?(?P<opponent>.+?))?$"
        ),
        "run disabled: couldn't get <bot> resurrected in time — killed by <mob>",
    ),
    (
        "left_dungeon",
        re.compile(r"^run disabled: Left the dungeon.*$"),
        "run disabled: left the dungeon",
    ),
    (
        "disabled_other",
        re.compile(r"^run disabled: (?P<detail>.+)$"),
        "run disabled: <detail>",
    ),
    (
        "no_progress",
        re.compile(r"^no boss/objective progress for (?P<secs>\d+)s$"),
        "no boss/objective progress for <n>s",
    ),
    (
        "door_stall",
        re.compile(r"^paused for over (?P<secs>\d+)s: (?P<detail>a closed door .*)$"),
        "paused for over <n>s: a closed door is blocking the path",
    ),
    (
        "paused_timeout",
        re.compile(r"^paused for over (?P<secs>\d+)s: (?P<detail>.*)$"),
        "paused for over <n>s: <detail>",
    ),
    (
        "stall_timeout",
        re.compile(r"^stalled for over (?P<secs>\d+)s: (?P<detail>.*)$"),
        "stalled for over <n>s: <detail>",
    ),
    (
        "overall_timeout",
        re.compile(r"^exceeded the overall time limit \((?P<secs>\d+)s\)$"),
        "exceeded the overall time limit (<n>s)",
    ),
    (
        "aborted",
        re.compile(r"^aborted by \.dc test stop$"),
        "aborted by .dc test stop",
    ),
]


def classify_fail(reason):
    """reason -> (class, template, opponent, victim). Empty reason == a clean run."""
    reason = (reason or "").strip()
    if not reason:
        return ("none", "", None, None)
    for cls, pattern, template in FAIL_RULES:
        m = pattern.match(reason)
        if m:
            groups = m.groupdict()
            return (cls, template, groups.get("opponent"), groups.get("victim"))
    # Unmatched prose is kept verbatim so a new failure mode shows up as
    # 'unclassified' in the views instead of silently folding into an existing
    # bucket. Add a rule above when one appears.
    return ("unclassified", reason, None, None)


# ---------------------------------------------------------------------------
# schema
# ---------------------------------------------------------------------------
SCHEMA = """
CREATE TABLE IF NOT EXISTS meta (
    key         TEXT PRIMARY KEY,
    value       TEXT
);

CREATE TABLE IF NOT EXISTS deploys (
    id              INTEGER PRIMARY KEY,
    sha             TEXT NOT NULL,
    repo            TEXT NOT NULL,     -- 'module' | 'core'
    committed_at_ms INTEGER NOT NULL,
    subject         TEXT,
    source          TEXT NOT NULL,     -- 'git-log' (proxy) | 'marker' (real deploy)
    UNIQUE (sha, repo, source)
);
CREATE INDEX IF NOT EXISTS idx_deploys_time ON deploys (repo, committed_at_ms);

CREATE TABLE IF NOT EXISTS runs (
    run_id              TEXT PRIMARY KEY,
    schema_version      INTEGER,
    plan_id             TEXT,
    dungeon             TEXT,
    dungeon_name        TEXT,
    wing                TEXT,
    map_id              INTEGER,
    instance_id         INTEGER,
    level               INTEGER,
    heroic              INTEGER,
    comp_seed           INTEGER,
    gear_ilvl           INTEGER,
    gear_quality        INTEGER,
    roster              INTEGER,
    started_at_ms       INTEGER,
    ended_at_ms         INTEGER,
    started_at          TEXT,          -- ISO-8601 UTC, for eyeballing
    duration_s          INTEGER,
    result              TEXT,
    fail_reason         TEXT,          -- verbatim prose
    fail_class          TEXT,          -- normalised bucket
    fail_template       TEXT,          -- prose with names -> placeholders
    fail_opponent       TEXT,          -- mob/boss parsed out of the prose
    fail_victim         TEXT,          -- bot parsed out of the prose
    disable_reason      TEXT,
    bosses_total        INTEGER,
    bosses_killed       INTEGER,
    pulls_elided        INTEGER,
    setup_stage         TEXT,
    stall_at_end        TEXT,
    phase_at_end        TEXT,
    wipe_on_boss        INTEGER,
    wipe_opponent       TEXT,
    wipe_opponent_entry INTEGER,
    party_size          INTEGER,
    final_tank_map      INTEGER,
    final_tank_x        REAL,
    final_tank_y        REAL,
    final_tank_z        REAL,
    wd_pause_grace_s    INTEGER,
    wd_stall_grace_s    INTEGER,
    wd_no_progress_s    INTEGER,
    wd_overall_s        INTEGER,
    -- diag snapshot, captured at teardown
    diag_valid          INTEGER,
    diag_captured_at    TEXT,
    diag_enabled        INTEGER,
    diag_paused         INTEGER,
    diag_pause_reason   TEXT,
    diag_paused_at_door INTEGER,
    diag_phase          TEXT,
    diag_state          TEXT,
    diag_detail         TEXT,
    diag_stall_reason   TEXT,
    diag_smart_rest_latched INTEGER,
    route_reachable     INTEGER,
    route_complete      INTEGER,
    route_start_far     INTEGER,
    route_fail_reason   TEXT,
    route_segments      INTEGER,
    route_off_path_ticks INTEGER,
    route_deviation     INTEGER,
    route_cursor_past_end INTEGER,
    wd_route_glide      INTEGER,
    wd_door_walk_in     INTEGER,
    wd_pursuit          INTEGER,
    wd_final_approach   INTEGER,
    wd_stuck_count      INTEGER,
    wd_rebuild_attempts INTEGER,
    wd_resnap_attempts  INTEGER,
    wd_party_not_ready  INTEGER,
    wd_door_stalled     INTEGER,
    wd_door_stalled_ms  INTEGER,
    pull_setting        INTEGER,
    pull_phase          INTEGER,
    pull_decision       INTEGER,
    pull_fizzle_count   INTEGER,
    pull_has_camp       INTEGER,
    world_in_combat     INTEGER,
    world_moving        INTEGER,
    world_encounter_mask INTEGER,
    world_cleared_anchors INTEGER,
    world_skipped       INTEGER,
    party_alive         INTEGER,
    party_offline       INTEGER,
    party_in_combat     INTEGER,
    -- derived
    deploy_id           INTEGER REFERENCES deploys (id),
    boss_clear_pct      REAL,
    -- statusTimeline is head/tail truncated by the writer (kStatusHead=50 +
    -- kStatusTail=150, DcTestRunRecord.cpp): on a long run the MIDDLE is dropped
    -- as one contiguous block and replaced by a '...' marker. These three columns
    -- exist so no query can silently mistake the surviving sample for the run.
    timeline_truncated  INTEGER,   -- a '...' marker was present
    timeline_elided     INTEGER,   -- transitions dropped, parsed from the marker
    timeline_covered_s  INTEGER,   -- seconds the surviving samples account for
    timeline_coverage_pct REAL,    -- covered_s as a share of duration_s
    ingested_at         TEXT
);
CREATE INDEX IF NOT EXISTS idx_runs_dungeon ON runs (dungeon, heroic);
CREATE INDEX IF NOT EXISTS idx_runs_started ON runs (started_at_ms);
CREATE INDEX IF NOT EXISTS idx_runs_result ON runs (result, fail_class);
CREATE INDEX IF NOT EXISTS idx_runs_plan ON runs (plan_id);

CREATE TABLE IF NOT EXISTS run_comp (
    run_id          TEXT NOT NULL REFERENCES runs (run_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    name            TEXT,
    class           TEXT,
    spec            TEXT,
    role            TEXT,
    detected_role   TEXT,
    role_mismatch   INTEGER,
    guid            INTEGER,
    level           INTEGER,
    from_map        INTEGER,
    from_x          REAL,
    from_y          REAL,
    from_z          REAL,
    PRIMARY KEY (run_id, idx)
);
CREATE INDEX IF NOT EXISTS idx_comp_class ON run_comp (class, role);

-- Units, because three different ones are in play (DcTestRunRecord.h):
--   predicted        bodies      EstimateAggroCount's body count
--   predicted_thirds elite-weight  non-elite = 1, elite = 3
--   observed         bodies      most distinct hostiles seen at one 1 Hz sample
--   observed_elites  bodies      ...the elite subset of that same sample
--
-- observed is sampled at 1 Hz, so a mob that joined and died inside one second
-- is missed: it is a FLOOR on the real fight, never an over-count. Consequence
-- for reading the error columns — only the underestimate direction is provable.
-- error > 0 means more showed up than predicted, which a floor can only
-- understate. error < 0 ("over-estimated") may just be the sampler blinking.
CREATE TABLE IF NOT EXISTS pulls (
    run_id          TEXT NOT NULL REFERENCES runs (run_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    t               INTEGER,
    entry           INTEGER,
    predicted       INTEGER,
    predicted_thirds INTEGER,
    ceiling_thirds  INTEGER,
    observed        INTEGER,
    observed_elites INTEGER,
    advanced        INTEGER,
    wiped_here      INTEGER,
    -- derived. Sign matches DcTestPlanSummary: observed - predicted, so a
    -- negative error means the governor predicted more than was seen.
    error_bodies    INTEGER,   -- observed - predicted, in bodies
    observed_thirds INTEGER,   -- observed re-weighted into the predicted unit
    error_thirds    INTEGER,   -- observed_thirds - predicted_thirds
    over_ceiling    INTEGER,   -- predicted_thirds > ceiling_thirds (should == advanced)
    PRIMARY KEY (run_id, idx)
);
CREATE INDEX IF NOT EXISTS idx_pulls_entry ON pulls (entry);
CREATE INDEX IF NOT EXISTS idx_pulls_wiped ON pulls (wiped_here);

CREATE TABLE IF NOT EXISTS deaths (
    run_id          TEXT NOT NULL REFERENCES runs (run_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    t               INTEGER,
    name            TEXT,
    opponent        TEXT,
    opponent_entry  INTEGER,
    on_boss         INTEGER,
    PRIMARY KEY (run_id, idx)
);
CREATE INDEX IF NOT EXISTS idx_deaths_opponent ON deaths (opponent);

CREATE TABLE IF NOT EXISTS boss_kills (
    run_id          TEXT NOT NULL REFERENCES runs (run_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    t               INTEGER,
    entry           INTEGER,
    name            TEXT,
    via             TEXT,
    PRIMARY KEY (run_id, idx)
);
CREATE INDEX IF NOT EXISTS idx_bosskills_name ON boss_kills (name);

CREATE TABLE IF NOT EXISTS status_timeline (
    run_id          TEXT NOT NULL REFERENCES runs (run_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    t               INTEGER,
    state           TEXT,
    detail          TEXT,
    -- seconds until the next sample; NULL on the last sample of a run and on
    -- '...' elision markers, where the gap is not real elapsed time in one state
    dwell_s         INTEGER,
    PRIMARY KEY (run_id, idx)
);
CREATE INDEX IF NOT EXISTS idx_status_state ON status_timeline (state);

CREATE TABLE IF NOT EXISTS pauses (
    run_id          TEXT NOT NULL REFERENCES runs (run_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    t               INTEGER,
    reason          TEXT,
    PRIMARY KEY (run_id, idx)
);

CREATE TABLE IF NOT EXISTS boss_roster (
    run_id          TEXT NOT NULL REFERENCES runs (run_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    name            TEXT,
    PRIMARY KEY (run_id, idx)
);

CREATE TABLE IF NOT EXISTS diag_roster (
    run_id          TEXT NOT NULL REFERENCES runs (run_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    entry           INTEGER,
    ord             INTEGER,
    name            TEXT,
    kind            TEXT,
    status          TEXT,
    done_via        TEXT,
    encounter_index INTEGER,
    x               REAL,
    y               REAL,
    z               REAL,
    PRIMARY KEY (run_id, idx)
);
CREATE INDEX IF NOT EXISTS idx_diagroster_name ON diag_roster (name, status);

CREATE TABLE IF NOT EXISTS diag_party (
    run_id          TEXT NOT NULL REFERENCES runs (run_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    name            TEXT,
    guid            INTEGER,
    level           INTEGER,
    is_bot          INTEGER,
    online          INTEGER,
    map             INTEGER,
    x               REAL,
    y               REAL,
    z               REAL,
    dist_to_tank    REAL,
    alive           INTEGER,
    hp              INTEGER,
    mp              INTEGER,
    in_combat       INTEGER,
    victim          TEXT,
    dc_strategy     INTEGER,
    dc_combat_strategy INTEGER,
    PRIMARY KEY (run_id, idx)
);

CREATE TABLE IF NOT EXISTS plans (
    plan_id         TEXT PRIMARY KEY,
    schema_version  INTEGER,
    dungeon         TEXT,
    dungeon_name    TEXT,
    req_total       INTEGER,
    req_concurrent  INTEGER,
    req_level       INTEGER,
    req_heroic      INTEGER,
    req_seed_base   INTEGER,
    req_gear_ilvl   INTEGER,
    req_gear_quality INTEGER,
    started_at_ms   INTEGER,
    ended_at_ms     INTEGER,
    started_at      TEXT,
    duration_s      INTEGER,
    result          TEXT,
    abort_reason    TEXT,
    launched        INTEGER,
    succeeded       INTEGER,
    failed          INTEGER,
    dur_min_s       INTEGER,
    dur_median_s    INTEGER,
    dur_avg_s       INTEGER,
    dur_max_s       INTEGER,
    pulls_count     INTEGER,
    pulls_advanced  INTEGER,
    pulls_underestimated INTEGER,
    pulls_error_p50 REAL,
    pulls_error_p90 REAL,
    pulls_observed_p50 REAL,
    pulls_observed_p90 REAL,
    pulls_observed_max REAL,
    pulls_wipe_pulls INTEGER,
    pulls_wipe_observed_max REAL,
    unattributed_wipes INTEGER,
    ingested_at     TEXT
);

CREATE TABLE IF NOT EXISTS plan_verdicts (
    plan_id         TEXT NOT NULL REFERENCES plans (plan_id) ON DELETE CASCADE,
    verdict         TEXT NOT NULL,
    count           INTEGER,
    PRIMARY KEY (plan_id, verdict)
);

CREATE TABLE IF NOT EXISTS plan_fail_reasons (
    plan_id         TEXT NOT NULL REFERENCES plans (plan_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    reason          TEXT,
    fail_class      TEXT,
    fail_template   TEXT,
    count           INTEGER,
    PRIMARY KEY (plan_id, idx)
);

CREATE TABLE IF NOT EXISTS plan_boss_funnel (
    plan_id         TEXT NOT NULL REFERENCES plans (plan_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    name            TEXT,
    killed          INTEGER,
    wiped           INTEGER,
    PRIMARY KEY (plan_id, idx)
);

CREATE TABLE IF NOT EXISTS plan_trash_wipes (
    plan_id         TEXT NOT NULL REFERENCES plans (plan_id) ON DELETE CASCADE,
    idx             INTEGER NOT NULL,
    name            TEXT,
    count           INTEGER,
    PRIMARY KEY (plan_id, idx)
);
"""

VIEWS = """
DROP VIEW IF EXISTS v_runs;
CREATE VIEW v_runs AS
SELECT r.*,
       d.sha    AS deploy_sha,
       d.repo   AS deploy_repo,
       d.source AS deploy_source,
       d.subject AS deploy_subject,
       CASE WHEN r.result = 'success' THEN 1 ELSE 0 END AS ok
FROM runs r
LEFT JOIN deploys d ON d.id = r.deploy_id;

-- Success rate per dungeon+difficulty. n is small outside heroic Sethekk, so
-- treat low-n rows as anecdote: the corpus is deliberately deep on one dungeon.
DROP VIEW IF EXISTS v_success_by_dungeon;
CREATE VIEW v_success_by_dungeon AS
SELECT dungeon, dungeon_name, heroic,
       COUNT(*)                                        AS runs,
       SUM(ok)                                         AS succeeded,
       ROUND(100.0 * SUM(ok) / COUNT(*), 1)            AS success_pct,
       ROUND(AVG(boss_clear_pct), 1)                   AS avg_boss_clear_pct,
       CAST(AVG(duration_s) AS INTEGER)                AS avg_duration_s
FROM v_runs
GROUP BY dungeon, heroic
ORDER BY runs DESC;

-- What kind of failure, not just how many. A change that trades door stalls for
-- wipes reads as flat on a single success-rate line but obvious here.
DROP VIEW IF EXISTS v_fail_mix;
CREATE VIEW v_fail_mix AS
SELECT dungeon, heroic, fail_class,
       COUNT(*)                                        AS n,
       ROUND(100.0 * COUNT(*) / SUM(COUNT(*)) OVER (PARTITION BY dungeon, heroic), 1) AS pct_of_runs
FROM v_runs
GROUP BY dungeon, heroic, fail_class
ORDER BY dungeon, heroic, n DESC;

-- Failure mix per attributed commit — the regression-detection view.
DROP VIEW IF EXISTS v_fail_by_deploy;
CREATE VIEW v_fail_by_deploy AS
SELECT deploy_sha, deploy_source, deploy_subject, dungeon, heroic,
       COUNT(*)                             AS runs,
       SUM(ok)                              AS succeeded,
       ROUND(100.0 * SUM(ok) / COUNT(*), 1) AS success_pct,
       SUM(CASE WHEN fail_class LIKE 'wipe%'  THEN 1 ELSE 0 END) AS wipes,
       SUM(CASE WHEN fail_class LIKE '%stall%' OR fail_class LIKE '%timeout%'
                THEN 1 ELSE 0 END)          AS stalls,
       SUM(CASE WHEN fail_class = 'no_progress' THEN 1 ELSE 0 END) AS no_progress,
       MIN(started_at)                      AS first_run,
       MAX(started_at)                      AS last_run
FROM v_runs
GROUP BY deploy_sha, dungeon, heroic
ORDER BY first_run;

-- Groupable failure buckets: names live in their own columns, so the same defect
-- across five different bots collapses to one row.
DROP VIEW IF EXISTS v_fail_templates;
CREATE VIEW v_fail_templates AS
SELECT fail_class, fail_template, COUNT(*) AS n,
       COUNT(DISTINCT fail_opponent) AS distinct_opponents,
       COUNT(DISTINCT dungeon)       AS distinct_dungeons
FROM v_runs
WHERE fail_class <> 'none'
GROUP BY fail_class, fail_template
ORDER BY n DESC;

-- Which mobs actually end runs, wipes and rez-failures folded together.
DROP VIEW IF EXISTS v_killer_mobs;
CREATE VIEW v_killer_mobs AS
SELECT COALESCE(NULLIF(fail_opponent, ''), NULLIF(wipe_opponent, '')) AS opponent,
       dungeon, heroic,
       COUNT(*) AS run_enders,
       SUM(CASE WHEN wipe_on_boss = 1 THEN 1 ELSE 0 END) AS as_boss
FROM v_runs
WHERE opponent IS NOT NULL
GROUP BY opponent, dungeon, heroic
ORDER BY run_enders DESC;

-- Pull predictor calibration. Sign follows DcTestPlanSummary: error > 0 means
-- more showed up than predicted. Since `observed` is a 1 Hz floor, treat
-- underestimates as fact and "overestimates" as an upper bound on over-caution.
DROP VIEW IF EXISTS v_pull_calibration;
CREATE VIEW v_pull_calibration AS
SELECT p.*, r.dungeon, r.heroic, r.gear_ilvl, r.deploy_id
FROM pulls p
JOIN runs r ON r.run_id = p.run_id
WHERE p.predicted IS NOT NULL;

-- The two diagnoses this is built to separate (per DcTestPlanSummary.h):
--   error ~0 but observed high  -> estimate is right, CEILING too generous
--   error well above 0          -> ESTIMATE is blind to part of the room
DROP VIEW IF EXISTS v_pull_calibration_by_dungeon;
CREATE VIEW v_pull_calibration_by_dungeon AS
SELECT dungeon, heroic,
       COUNT(*)                                     AS pulls,
       ROUND(AVG(error_bodies), 2)                  AS avg_error_bodies,
       ROUND(AVG(error_thirds), 2)                  AS avg_error_thirds,
       -- provable: a floor exceeding the prediction is definitive
       ROUND(AVG(CASE WHEN error_bodies > 0 THEN 1.0 ELSE 0.0 END) * 100, 1)
                                                    AS pct_underestimated,
       -- not provable, the 1 Hz sampler may simply have blinked
       ROUND(AVG(CASE WHEN error_bodies < 0 THEN 1.0 ELSE 0.0 END) * 100, 1)
                                                    AS pct_apparent_over,
       MAX(observed)                                AS observed_max,
       ROUND(AVG(advanced) * 100, 1)                AS pct_advanced,
       SUM(wiped_here)                              AS wipe_pulls
FROM v_pull_calibration
GROUP BY dungeon, heroic
ORDER BY pulls DESC;

-- Sanity check: `advanced` is the recorded verdict, over_ceiling recomputes it
-- from predicted_thirds vs ceiling_thirds. Rows here mean the two disagree, so
-- either a patrol-hold reduced the weight or the verdict changed after sampling.
DROP VIEW IF EXISTS v_pull_verdict_mismatch;
CREATE VIEW v_pull_verdict_mismatch AS
SELECT run_id, dungeon, heroic, t, entry, predicted_thirds, ceiling_thirds,
       advanced, over_ceiling
FROM v_pull_calibration
WHERE over_ceiling IS NOT NULL AND advanced IS NOT NULL
  AND over_ceiling <> advanced;

-- The pulls that killed the party, against what the predictor said. Wipes at
-- large positive error_bodies mean the estimate missed the room; wipes at
-- error ~0 mean the ceiling let through a fight the party simply loses.
DROP VIEW IF EXISTS v_wipe_pulls;
CREATE VIEW v_wipe_pulls AS
SELECT run_id, dungeon, heroic, t, entry, predicted, predicted_thirds,
       ceiling_thirds, observed, observed_elites, error_bodies, error_thirds,
       advanced
FROM v_pull_calibration
WHERE wiped_here = 1
ORDER BY error_bodies DESC;

-- Which diag_* columns mean anything, per run outcome.
--
-- The snapshot is taken at teardown (DcTestRunJob::Teardown). On a run that
-- COMPLETED, dungeon-clear has already disabled itself ("All bosses cleared!"),
-- so diag_state is 'off', diag_enabled is 0, and the route/target/pull blocks
-- describe a module that has shut down — not the run. Only failed runs carry a
-- live snapshot. Check here before building anything on a diag_* column.
DROP VIEW IF EXISTS v_diag_availability;
CREATE VIEW v_diag_availability AS
SELECT result, diag_state, COUNT(*) AS runs,
       SUM(diag_enabled)             AS live_snapshots,
       SUM(wd_resnap_attempts)       AS resnap_total,
       SUM(wd_rebuild_attempts)      AS rebuild_total,
       SUM(wd_stuck_count)           AS stuck_total
FROM runs
GROUP BY result, diag_state
ORDER BY runs DESC;

-- Runs that PASSED while spending real time in a recovery state. A success that
-- sat 60s in 'stalled' is a bug that hasn't bitten yet, and the verdict hides it.
--
-- Built on status_timeline dwell, NOT on the diag watchdog counters: those are
-- documented as *consecutive* counters reset on progress (DcApproachState.h), so
-- a single teardown sample of them is ~always zero and carries no signal. Getting
-- run-lifetime friction totals would need the harness to accumulate them.
DROP VIEW IF EXISTS v_silent_struggles;
CREATE VIEW v_silent_struggles AS
SELECT r.run_id, r.dungeon, r.heroic, r.duration_s, r.started_at,
       r.timeline_coverage_pct,
       SUM(CASE WHEN s.state = 'stalled'    THEN s.dwell_s ELSE 0 END) AS stalled_s,
       SUM(CASE WHEN s.state = 'recovering' THEN s.dwell_s ELSE 0 END) AS recovering_s,
       SUM(CASE WHEN s.state = 'pathing'    THEN s.dwell_s ELSE 0 END) AS pathing_s,
       SUM(CASE WHEN s.state = 'pursuing'   THEN s.dwell_s ELSE 0 END) AS pursuing_s,
       SUM(CASE WHEN s.state = 'idle'       THEN s.dwell_s ELSE 0 END) AS idle_s,
       -- weighted: an outright stall is worse than a long pursuit
       SUM(CASE s.state WHEN 'stalled' THEN s.dwell_s * 4
                        WHEN 'recovering' THEN s.dwell_s * 3
                        WHEN 'pathing' THEN s.dwell_s * 2
                        WHEN 'pursuing' THEN s.dwell_s
                        WHEN 'idle' THEN s.dwell_s
                        ELSE 0 END) AS friction
FROM runs r
JOIN status_timeline s ON s.run_id = r.run_id
WHERE r.result = 'success' AND s.dwell_s IS NOT NULL
GROUP BY r.run_id
HAVING friction > 0
ORDER BY friction DESC;

-- How much of each run's wall clock the timeline actually accounts for. Read this
-- BEFORE trusting any state profile: a truncated run keeps only its first 50 and
-- last 150 transitions, so coverage runs ~60% and the missing part is the MIDDLE
-- of the run, not a random sample.
DROP VIEW IF EXISTS v_timeline_coverage;
CREATE VIEW v_timeline_coverage AS
SELECT dungeon, heroic,
       COUNT(*)                                  AS runs,
       SUM(timeline_truncated)                   AS truncated_runs,
       ROUND(AVG(timeline_coverage_pct), 1)      AS avg_coverage_pct,
       MIN(timeline_coverage_pct)                AS min_coverage_pct,
       SUM(timeline_elided)                      AS transitions_lost
FROM runs
GROUP BY dungeon, heroic
ORDER BY runs DESC;

-- State profile over the SAMPLED portion only. pct is a share of covered time,
-- not of the run — on truncated runs the mid-run block is absent, which biases
-- this toward whatever happens early and late. Use v_status_profile_untruncated
-- when you need a figure that describes a whole run.
DROP VIEW IF EXISTS v_status_profile;
CREATE VIEW v_status_profile AS
SELECT r.dungeon, r.heroic, s.state,
       COUNT(*)                  AS samples,
       SUM(s.dwell_s)            AS sampled_s,
       ROUND(100.0 * SUM(s.dwell_s) / SUM(SUM(s.dwell_s)) OVER (PARTITION BY r.dungeon, r.heroic), 1)
                                 AS pct_of_sampled,
       SUM(r.timeline_truncated) AS from_truncated_runs
FROM status_timeline s
JOIN runs r ON r.run_id = s.run_id
WHERE s.dwell_s IS NOT NULL
GROUP BY r.dungeon, r.heroic, s.state
ORDER BY r.dungeon, r.heroic, sampled_s DESC;

-- The unbiased version: only runs whose timeline was never truncated, so the
-- percentages really are shares of the whole run. Smaller n, trustworthy shape.
DROP VIEW IF EXISTS v_status_profile_untruncated;
CREATE VIEW v_status_profile_untruncated AS
SELECT r.dungeon, r.heroic, s.state,
       COUNT(DISTINCT s.run_id)  AS runs,
       SUM(s.dwell_s)            AS total_s,
       ROUND(100.0 * SUM(s.dwell_s) / SUM(SUM(s.dwell_s)) OVER (PARTITION BY r.dungeon, r.heroic), 1)
                                 AS pct_of_time
FROM status_timeline s
JOIN runs r ON r.run_id = s.run_id
WHERE s.dwell_s IS NOT NULL AND r.timeline_truncated = 0
GROUP BY r.dungeon, r.heroic, s.state
ORDER BY r.dungeon, r.heroic, total_s DESC;

-- Per-run state profile, for spotting the run that spent 60% of itself resting.
-- coverage_pct comes along so a low-coverage run can be discounted.
DROP VIEW IF EXISTS v_status_profile_by_run;
CREATE VIEW v_status_profile_by_run AS
SELECT s.run_id, r.dungeon, r.heroic, r.result, r.timeline_coverage_pct, s.state,
       SUM(s.dwell_s) AS sampled_s,
       ROUND(100.0 * SUM(s.dwell_s) / SUM(SUM(s.dwell_s)) OVER (PARTITION BY s.run_id), 1)
                      AS pct_of_sampled
FROM status_timeline s
JOIN runs r ON r.run_id = s.run_id
WHERE s.dwell_s IS NOT NULL
GROUP BY s.run_id, s.state;

-- Time-to-kill per boss. A widening spread localises a slowdown to one fight.
DROP VIEW IF EXISTS v_boss_pacing;
CREATE VIEW v_boss_pacing AS
SELECT b.name, r.dungeon, r.heroic,
       COUNT(*)                         AS kills,
       MIN(b.t)                         AS min_t,
       CAST(AVG(b.t) AS INTEGER)        AS avg_t,
       MAX(b.t)                         AS max_t,
       SUM(CASE WHEN b.via = 'mask' THEN 1 ELSE 0 END) AS via_mask
FROM boss_kills b
JOIN runs r ON r.run_id = b.run_id
GROUP BY b.name, r.dungeon, r.heroic
ORDER BY r.dungeon, avg_t;

-- How far each boss gets reached vs killed, from the per-run roster snapshot.
DROP VIEW IF EXISTS v_boss_funnel;
CREATE VIEW v_boss_funnel AS
SELECT r.dungeon, r.heroic, dr.name, dr.ord,
       COUNT(*)                                             AS appearances,
       SUM(CASE WHEN dr.status = 'dead' THEN 1 ELSE 0 END)   AS dead,
       ROUND(100.0 * SUM(CASE WHEN dr.status = 'dead' THEN 1 ELSE 0 END) / COUNT(*), 1)
                                                            AS kill_pct
FROM diag_roster dr
JOIN runs r ON r.run_id = dr.run_id
WHERE dr.kind = 'boss'
GROUP BY r.dungeon, r.heroic, dr.name
ORDER BY r.dungeon, dr.ord;

-- Class/spec presence vs outcome. Per-class n is small once you filter to one
-- dungeon — a hint generator, not evidence.
DROP VIEW IF EXISTS v_comp_outcomes;
CREATE VIEW v_comp_outcomes AS
SELECT c.class, c.role, r.dungeon, r.heroic,
       COUNT(*)                             AS runs,
       SUM(r.ok)                            AS succeeded,
       ROUND(100.0 * SUM(r.ok) / COUNT(*), 1) AS success_pct
FROM run_comp c
JOIN v_runs r ON r.run_id = c.run_id
GROUP BY c.class, c.role, r.dungeon, r.heroic
ORDER BY runs DESC;

-- Which bots die most, and to what.
DROP VIEW IF EXISTS v_death_toll;
CREATE VIEW v_death_toll AS
SELECT d.opponent, d.on_boss, r.dungeon, r.heroic,
       COUNT(*)                       AS deaths,
       COUNT(DISTINCT d.run_id)       AS runs_affected,
       CAST(AVG(d.t) AS INTEGER)      AS avg_t
FROM deaths d
JOIN runs r ON r.run_id = d.run_id
GROUP BY d.opponent, d.on_boss, r.dungeon, r.heroic
ORDER BY deaths DESC;

-- Where runs ended, in map coordinates. Nav bugs are geographic: a cluster is a
-- place, not a statistic. No background map — pair with a navmesh render.
DROP VIEW IF EXISTS v_death_map;
CREATE VIEW v_death_map AS
SELECT dungeon, heroic, final_tank_map AS map_id, fail_class,
       ROUND(final_tank_x, 1) AS x, ROUND(final_tank_y, 1) AS y,
       ROUND(final_tank_z, 1) AS z,
       run_id, started_at, stall_at_end
FROM v_runs
WHERE result <> 'success' AND final_tank_map IS NOT NULL;
"""


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
def find_workspace_root(start):
    """Walk up looking for the dir that holds env/dist/bin/<runs file>.

    Beats counting `..` segments because this script also runs from a git
    worktree under .claude/worktrees/<name>/tools/, where the depth differs.
    """
    for candidate in [start, *start.parents]:
        if (candidate / "env" / "dist" / "bin" / RUNS_FILE).exists():
            return candidate
    return None


def iter_jsonl(path):
    """Yield objects from a JSONL file, tolerating a half-written final line.

    These files are appended to by a live worldserver, so the last line can be
    truncated mid-write. A decode error on the *final* line is expected and
    skipped; anywhere else it is a real corruption and reported.
    """
    if not path.exists():
        return
    with path.open("r", encoding="utf-8", errors="replace") as fh:
        lines = fh.readlines()
    last = len(lines) - 1
    for i, line in enumerate(lines):
        line = line.strip()
        if not line:
            continue
        try:
            yield json.loads(line)
        except json.JSONDecodeError as exc:
            if i == last:
                print(f"  note: ignoring incomplete final line of {path.name} "
                      f"(run in progress)", file=sys.stderr)
            else:
                print(f"  WARNING: {path.name} line {i + 1} is not valid JSON: {exc}",
                      file=sys.stderr)


def iso(ms):
    if not ms:
        return None
    import datetime
    return datetime.datetime.utcfromtimestamp(ms / 1000).strftime("%Y-%m-%dT%H:%M:%SZ")


def b(value):
    """JSON bool/int/None -> SQLite int, preserving NULL."""
    if value is None:
        return None
    return 1 if value else 0


def g(obj, *path, default=None):
    """Nested .get() chain — the whole schema-drift strategy in one function."""
    cur = obj
    for key in path:
        if not isinstance(cur, dict):
            return default
        cur = cur.get(key)
        if cur is None:
            return default
    return cur


# ---------------------------------------------------------------------------
# deploys
# ---------------------------------------------------------------------------
def collect_git_deploys(ws_root, branch="master", since="90 days ago"):
    """Candidate build markers from commit history. Proxy, not ground truth.

    --first-parent on the integration branch, because that is the lineage that
    was ever actually deployable: a commit on an unmerged feature branch never
    ran, and crediting runs to it would be pure noise.
    """
    rows = []
    for repo_label, rel, paths in DEPLOY_REPOS:
        repo = ws_root / rel
        if not (repo / ".git").exists():
            continue
        cmd = ["git", "-C", str(repo), "log", "--first-parent",
               "--format=%H%x01%ct%x01%s", f"--since={since}", branch]
        if paths:
            cmd += ["--", *paths]
        try:
            out = subprocess.run(cmd, capture_output=True, text=True,
                                 timeout=60, check=True).stdout
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError) as exc:
            print(f"  note: no usable git history for {repo_label}: {exc}",
                  file=sys.stderr)
            continue
        for line in out.splitlines():
            parts = line.split("\x01")
            if len(parts) != 3:
                continue
            sha, ct, subject = parts
            rows.append((sha, repo_label, int(ct) * 1000, subject, "git-log"))
    return rows


def attribute_deploy(conn, repo_label="module"):
    """Point each run at the newest preceding marker, real markers preferred.

    A 'marker' row is a recorded deploy; a 'git-log' row is only "this commit
    existed by then". Real markers win at equal timestamps.
    """
    conn.execute(
        """
        UPDATE runs SET deploy_id = (
            SELECT d.id FROM deploys d
            WHERE d.repo = ?
              AND d.committed_at_ms <= runs.started_at_ms
            ORDER BY d.committed_at_ms DESC,
                     CASE d.source WHEN 'marker' THEN 0 ELSE 1 END
            LIMIT 1
        )
        WHERE started_at_ms IS NOT NULL
        """,
        (repo_label,),
    )


# ---------------------------------------------------------------------------
# ingest
# ---------------------------------------------------------------------------
CHILD_TABLES = [
    "run_comp", "pulls", "deaths", "boss_kills", "status_timeline",
    "pauses", "boss_roster", "diag_roster", "diag_party",
]

ELIDED_RE = re.compile(r"^(\d+) transitions elided")


def build_timeline(run_id, timeline, duration_s):
    """Rows for status_timeline, plus the coverage metrics for the parent run.

    The writer keeps the first 50 and last 150 transitions and replaces the middle
    with a single '...' marker (DcTestRunRecord.cpp). So on any long run there is a
    hole of unknown duration in the middle, and `dwell_s` must be NULL on both
    sides of it — the gap across the marker is elided wall-clock, not time spent
    in one state. Attributing it to a state would inflate whatever happened to sit
    at the seam.
    """
    rows = []
    truncated = 0
    elided = 0
    covered = 0
    for i, s in enumerate(timeline):
        state = s.get("state")
        detail = s.get("detail")
        if state == "...":
            truncated = 1
            m = ELIDED_RE.match(detail or "")
            if m:
                elided = int(m.group(1))
            rows.append((run_id, i, s.get("t"), state, detail, None))
            continue
        dwell = None
        if i + 1 < len(timeline):
            nxt = timeline[i + 1]
            if (nxt.get("state") != "..." and s.get("t") is not None
                    and nxt.get("t") is not None):
                delta = nxt["t"] - s["t"]
                if delta >= 0:
                    dwell = delta
                    covered += delta
        elif duration_s and s.get("t") is not None and duration_s >= s["t"]:
            # Last transition: the run ended while in this state, so the tail is
            # known exactly. Without this, every run silently loses the stretch
            # between its final transition and its end.
            dwell = duration_s - s["t"]
            covered += dwell
        rows.append((run_id, i, s.get("t"), state, detail, dwell))

    coverage_pct = None
    if duration_s:
        coverage_pct = round(100.0 * covered / duration_s, 1)
    return rows, {
        "truncated": truncated,
        "elided": elided,
        "covered_s": covered,
        "coverage_pct": coverage_pct,
    }


def ingest_run(conn, rec, now):
    run_id = rec.get("runId")
    if not run_id:
        return False

    fail_class, fail_template, fail_opponent, fail_victim = classify_fail(rec.get("failReason"))

    bosses_total = rec.get("bossesTotal") or 0
    bosses_killed = rec.get("bossesKilled") or 0
    clear_pct = round(100.0 * bosses_killed / bosses_total, 1) if bosses_total else None

    diag = rec.get("diag") or {}
    timeline_rows, tl = build_timeline(run_id, rec.get("statusTimeline") or [],
                                      rec.get("durationS"))

    conn.execute("DELETE FROM runs WHERE run_id = ?", (run_id,))
    for table in CHILD_TABLES:
        conn.execute(f"DELETE FROM {table} WHERE run_id = ?", (run_id,))

    conn.execute(
        """
        INSERT INTO runs VALUES (
            :run_id, :schema_version, :plan_id, :dungeon, :dungeon_name, :wing,
            :map_id, :instance_id, :level, :heroic, :comp_seed, :gear_ilvl,
            :gear_quality, :roster, :started_at_ms, :ended_at_ms, :started_at,
            :duration_s, :result, :fail_reason, :fail_class, :fail_template,
            :fail_opponent, :fail_victim, :disable_reason, :bosses_total,
            :bosses_killed, :pulls_elided, :setup_stage, :stall_at_end,
            :phase_at_end, :wipe_on_boss, :wipe_opponent, :wipe_opponent_entry,
            :party_size, :final_tank_map, :final_tank_x, :final_tank_y,
            :final_tank_z, :wd_pause_grace_s, :wd_stall_grace_s,
            :wd_no_progress_s, :wd_overall_s, :diag_valid, :diag_captured_at,
            :diag_enabled, :diag_paused, :diag_pause_reason, :diag_paused_at_door,
            :diag_phase, :diag_state, :diag_detail, :diag_stall_reason,
            :diag_smart_rest_latched, :route_reachable, :route_complete,
            :route_start_far, :route_fail_reason, :route_segments,
            :route_off_path_ticks, :route_deviation, :route_cursor_past_end,
            :wd_route_glide, :wd_door_walk_in, :wd_pursuit, :wd_final_approach,
            :wd_stuck_count, :wd_rebuild_attempts, :wd_resnap_attempts,
            :wd_party_not_ready, :wd_door_stalled, :wd_door_stalled_ms,
            :pull_setting, :pull_phase, :pull_decision, :pull_fizzle_count,
            :pull_has_camp, :world_in_combat, :world_moving,
            :world_encounter_mask, :world_cleared_anchors, :world_skipped,
            :party_alive, :party_offline, :party_in_combat, NULL,
            :boss_clear_pct, :timeline_truncated, :timeline_elided,
            :timeline_covered_s, :timeline_coverage_pct, :ingested_at
        )
        """,
        {
            "run_id": run_id,
            "schema_version": rec.get("schema"),
            "plan_id": rec.get("planId") or None,
            "dungeon": rec.get("dungeon"),
            "dungeon_name": rec.get("dungeonName"),
            "wing": rec.get("wing"),
            "map_id": rec.get("mapId"),
            "instance_id": rec.get("instanceId"),
            "level": rec.get("level"),
            "heroic": b(rec.get("heroic")),
            "comp_seed": rec.get("compSeed"),
            "gear_ilvl": rec.get("gearIlvl"),
            "gear_quality": rec.get("gearQuality"),
            "roster": b(rec.get("roster")),
            "started_at_ms": rec.get("startedAtMs"),
            "ended_at_ms": rec.get("endedAtMs"),
            "started_at": iso(rec.get("startedAtMs")),
            "duration_s": rec.get("durationS"),
            "result": rec.get("result"),
            "fail_reason": rec.get("failReason") or "",
            "fail_class": fail_class,
            "fail_template": fail_template,
            "fail_opponent": fail_opponent,
            "fail_victim": fail_victim,
            "disable_reason": rec.get("disableReason"),
            "bosses_total": bosses_total,
            "bosses_killed": bosses_killed,
            "pulls_elided": rec.get("pullsElided"),
            "setup_stage": rec.get("setupStage"),
            "stall_at_end": rec.get("stallAtEnd"),
            "phase_at_end": rec.get("phaseAtEnd"),
            "wipe_on_boss": b(rec.get("wipeOnBoss")),
            "wipe_opponent": rec.get("wipeOpponent"),
            "wipe_opponent_entry": rec.get("wipeOpponentEntry"),
            "party_size": g(diag, "party", "size"),
            "final_tank_map": g(rec, "finalTankPos", "map"),
            "final_tank_x": g(rec, "finalTankPos", "x"),
            "final_tank_y": g(rec, "finalTankPos", "y"),
            "final_tank_z": g(rec, "finalTankPos", "z"),
            "wd_pause_grace_s": g(rec, "watchdog", "pauseGraceS"),
            "wd_stall_grace_s": g(rec, "watchdog", "stallGraceS"),
            "wd_no_progress_s": g(rec, "watchdog", "noProgressS"),
            "wd_overall_s": g(rec, "watchdog", "overallS"),
            "diag_valid": b(diag.get("valid")),
            "diag_captured_at": diag.get("capturedAt"),
            "diag_enabled": b(diag.get("enabled")),
            "diag_paused": b(diag.get("paused")),
            "diag_pause_reason": diag.get("pauseReason"),
            "diag_paused_at_door": b(diag.get("pausedAtDoor")),
            "diag_phase": diag.get("phase"),
            "diag_state": diag.get("state"),
            "diag_detail": diag.get("detail"),
            "diag_stall_reason": diag.get("stallReason"),
            "diag_smart_rest_latched": b(diag.get("smartRestLatched")),
            "route_reachable": b(g(diag, "route", "reachable")),
            "route_complete": b(g(diag, "route", "complete")),
            "route_start_far": b(g(diag, "route", "startFarFromPoly")),
            "route_fail_reason": g(diag, "route", "failureReason"),
            "route_segments": g(diag, "route", "segments"),
            "route_off_path_ticks": g(diag, "route", "offPathTicks"),
            "route_deviation": g(diag, "route", "deviation"),
            "route_cursor_past_end": b(g(diag, "route", "cursorPastPathEnd")),
            "wd_route_glide": g(diag, "watchdogs", "routeGlide"),
            "wd_door_walk_in": g(diag, "watchdogs", "doorWalkIn"),
            "wd_pursuit": g(diag, "watchdogs", "pursuit"),
            "wd_final_approach": g(diag, "watchdogs", "finalApproach"),
            "wd_stuck_count": g(diag, "watchdogs", "stuckCount"),
            "wd_rebuild_attempts": g(diag, "watchdogs", "rebuildAttempts"),
            "wd_resnap_attempts": g(diag, "watchdogs", "resnapAttempts"),
            "wd_party_not_ready": g(diag, "watchdogs", "partyNotReadyTicks"),
            "wd_door_stalled": b(g(diag, "watchdogs", "doorStalled")),
            "wd_door_stalled_ms": g(diag, "watchdogs", "doorStalledForMs"),
            "pull_setting": g(diag, "pull", "setting"),
            "pull_phase": g(diag, "pull", "phase"),
            "pull_decision": g(diag, "pull", "decision"),
            "pull_fizzle_count": g(diag, "pull", "fizzleCount"),
            "pull_has_camp": b(g(diag, "pull", "hasCamp")),
            "world_in_combat": b(g(diag, "world", "inCombat")),
            "world_moving": b(g(diag, "world", "moving")),
            "world_encounter_mask": g(diag, "world", "completedEncounterMask"),
            "world_cleared_anchors": g(diag, "world", "clearedAnchors"),
            "world_skipped": g(diag, "world", "skipped"),
            "party_alive": g(diag, "party", "alive"),
            "party_offline": g(diag, "party", "offline"),
            "party_in_combat": g(diag, "party", "inCombat"),
            "boss_clear_pct": clear_pct,
            "timeline_truncated": tl["truncated"],
            "timeline_elided": tl["elided"],
            "timeline_covered_s": tl["covered_s"],
            "timeline_coverage_pct": tl["coverage_pct"],
            "ingested_at": now,
        },
    )

    for i, m in enumerate(rec.get("comp") or []):
        conn.execute(
            "INSERT INTO run_comp VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (run_id, i, m.get("name"), m.get("class"), m.get("spec"), m.get("role"),
             m.get("detectedRole"), b(m.get("roleMismatch")), m.get("guid"),
             m.get("level"), g(m, "from", "map"), g(m, "from", "x"),
             g(m, "from", "y"), g(m, "from", "z")),
        )

    for i, p in enumerate(rec.get("pulls") or []):
        predicted = p.get("predicted")
        predicted_thirds = p.get("predictedThirds")
        ceiling_thirds = p.get("ceilingThirds")
        observed = p.get("observed")
        observed_elites = p.get("observedElites")

        error_bodies = None
        if predicted is not None and observed is not None:
            error_bodies = observed - predicted

        # Re-weight the observed bodies into the predictor's unit so the two are
        # comparable: elite = 3, non-elite = 1 (DungeonClearMath weightThirds).
        observed_thirds = error_thirds = None
        if observed is not None and observed_elites is not None:
            non_elites = max(0, observed - observed_elites)
            observed_thirds = non_elites + (observed_elites * 3)
            if predicted_thirds is not None:
                error_thirds = observed_thirds - predicted_thirds

        over_ceiling = None
        if predicted_thirds is not None and ceiling_thirds is not None:
            over_ceiling = 1 if predicted_thirds > ceiling_thirds else 0

        conn.execute(
            "INSERT INTO pulls VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (run_id, i, p.get("t"), p.get("entry"), predicted, predicted_thirds,
             ceiling_thirds, observed, observed_elites, b(p.get("advanced")),
             b(p.get("wipedHere")), error_bodies, observed_thirds, error_thirds,
             over_ceiling),
        )

    for i, d in enumerate(rec.get("deaths") or []):
        conn.execute(
            "INSERT INTO deaths VALUES (?,?,?,?,?,?,?)",
            (run_id, i, d.get("t"), d.get("name"), d.get("opponent"),
             d.get("opponentEntry"), b(d.get("onBoss"))),
        )

    for i, k in enumerate(rec.get("bossTimeline") or []):
        conn.execute(
            "INSERT INTO boss_kills VALUES (?,?,?,?,?,?)",
            (run_id, i, k.get("t"), k.get("entry"), k.get("name"), k.get("via")),
        )

    conn.executemany("INSERT INTO status_timeline VALUES (?,?,?,?,?,?)", timeline_rows)

    for i, p in enumerate(rec.get("pauses") or []):
        conn.execute("INSERT INTO pauses VALUES (?,?,?,?)",
                     (run_id, i, p.get("t"), p.get("reason")))

    for i, name in enumerate(rec.get("bossRoster") or []):
        conn.execute("INSERT INTO boss_roster VALUES (?,?,?)", (run_id, i, name))

    for i, e in enumerate(g(diag, "roster", default=[]) or []):
        conn.execute(
            "INSERT INTO diag_roster VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
            (run_id, i, e.get("entry"), e.get("order"), e.get("name"), e.get("kind"),
             e.get("status"), e.get("doneVia"), e.get("encounterIndex"),
             e.get("x"), e.get("y"), e.get("z")),
        )

    for i, m in enumerate(g(diag, "party", "members", default=[]) or []):
        conn.execute(
            "INSERT INTO diag_party VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (run_id, i, m.get("name"), m.get("guid"), m.get("level"), b(m.get("bot")),
             b(m.get("online")), m.get("map"), m.get("x"), m.get("y"), m.get("z"),
             m.get("distToTank"), b(m.get("alive")), m.get("hp"), m.get("mp"),
             b(m.get("inCombat")), m.get("victim"), b(m.get("dcStrategy")),
             b(m.get("dcCombatStrategy"))),
        )
    return True


PLAN_CHILD_TABLES = ["plan_verdicts", "plan_fail_reasons", "plan_boss_funnel",
                     "plan_trash_wipes"]


def ingest_plan(conn, rec, now):
    plan_id = rec.get("planId")
    if not plan_id:
        return False

    conn.execute("DELETE FROM plans WHERE plan_id = ?", (plan_id,))
    for table in PLAN_CHILD_TABLES:
        conn.execute(f"DELETE FROM {table} WHERE plan_id = ?", (plan_id,))

    conn.execute(
        """
        INSERT INTO plans (
            plan_id, schema_version, dungeon, dungeon_name,
            req_total, req_concurrent, req_level, req_heroic, req_seed_base,
            req_gear_ilvl, req_gear_quality,
            started_at_ms, ended_at_ms, started_at, duration_s, result, abort_reason,
            launched, succeeded, failed,
            dur_min_s, dur_median_s, dur_avg_s, dur_max_s,
            pulls_count, pulls_advanced, pulls_underestimated,
            pulls_error_p50, pulls_error_p90,
            pulls_observed_p50, pulls_observed_p90, pulls_observed_max,
            pulls_wipe_pulls, pulls_wipe_observed_max,
            unattributed_wipes, ingested_at
        ) VALUES (""" + ",".join("?" * 36) + ")",
        (plan_id, rec.get("schema"), rec.get("dungeon"), rec.get("dungeonName"),
         g(rec, "requested", "total"), g(rec, "requested", "concurrent"),
         g(rec, "requested", "level"), b(g(rec, "requested", "heroic")),
         g(rec, "requested", "seedBase"), g(rec, "requested", "gearIlvl"),
         g(rec, "requested", "gearQuality"), rec.get("startedAtMs"),
         rec.get("endedAtMs"), iso(rec.get("startedAtMs")), rec.get("durationS"),
         rec.get("result"), rec.get("abortReason"), g(rec, "runs", "launched"),
         g(rec, "runs", "succeeded"), g(rec, "runs", "failed"),
         g(rec, "duration", "minS"), g(rec, "duration", "medianS"),
         g(rec, "duration", "avgS"), g(rec, "duration", "maxS"),
         g(rec, "pulls", "count"), g(rec, "pulls", "advanced"),
         g(rec, "pulls", "underestimated"), g(rec, "pulls", "errorP50"),
         g(rec, "pulls", "errorP90"), g(rec, "pulls", "observedP50"),
         g(rec, "pulls", "observedP90"), g(rec, "pulls", "observedMax"),
         g(rec, "pulls", "wipePulls"), g(rec, "pulls", "wipeObservedMax"),
         rec.get("unattributedWipes"), now),
    )

    # verdicts is a sparse map: a key is absent when that verdict never occurred,
    # which is why v4 and v5 records disagree on which keys exist.
    for verdict, count in (rec.get("verdicts") or {}).items():
        conn.execute("INSERT INTO plan_verdicts VALUES (?,?,?)", (plan_id, verdict, count))

    for i, fr in enumerate(rec.get("failReasons") or []):
        cls, template, _, _ = classify_fail(fr.get("reason"))
        conn.execute("INSERT INTO plan_fail_reasons VALUES (?,?,?,?,?,?)",
                     (plan_id, i, fr.get("reason"), cls, template, fr.get("count")))

    for i, bf in enumerate(rec.get("bossFunnel") or []):
        conn.execute("INSERT INTO plan_boss_funnel VALUES (?,?,?,?,?)",
                     (plan_id, i, bf.get("name"), bf.get("killed"), bf.get("wiped")))

    for i, tw in enumerate(rec.get("trashWipes") or []):
        conn.execute("INSERT INTO plan_trash_wipes VALUES (?,?,?,?)",
                     (plan_id, i, tw.get("name"), tw.get("count")))
    return True


# ---------------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------------
def cmd_ingest(conn, data_dir, ws_root, args):
    import datetime
    now = datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")

    existing_runs = set()
    existing_plans = set()
    if not args.force:
        existing_runs = {r[0] for r in conn.execute("SELECT run_id FROM runs")}
        existing_plans = {r[0] for r in conn.execute("SELECT plan_id FROM plans")}

    runs_path = data_dir / RUNS_FILE
    plans_path = data_dir / PLANS_FILE

    print(f"reading {runs_path}")
    new_runs = skipped_runs = 0
    for rec in iter_jsonl(runs_path):
        if rec.get("runId") in existing_runs:
            skipped_runs += 1
            continue
        if ingest_run(conn, rec, now):
            new_runs += 1

    print(f"reading {plans_path}")
    new_plans = skipped_plans = 0
    for rec in iter_jsonl(plans_path):
        if rec.get("planId") in existing_plans:
            skipped_plans += 1
            continue
        if ingest_plan(conn, rec, now):
            new_plans += 1

    deploy_rows = collect_git_deploys(ws_root, args.deploy_branch)
    conn.executemany(
        "INSERT OR IGNORE INTO deploys (sha, repo, committed_at_ms, subject, source) "
        "VALUES (?,?,?,?,?)", deploy_rows,
    )
    attribute_deploy(conn, args.deploy_repo)

    conn.executemany(
        "INSERT OR REPLACE INTO meta (key, value) VALUES (?,?)",
        [("etl_version", str(ETL_VERSION)), ("last_ingest", now),
         ("runs_source", str(runs_path)), ("plans_source", str(plans_path)),
         ("deploy_repo", args.deploy_repo)],
    )
    conn.commit()

    print(f"\n  runs:    +{new_runs} new, {skipped_runs} already present")
    print(f"  plans:   +{new_plans} new, {skipped_plans} already present")
    print(f"  deploys: {len(deploy_rows)} commit markers scanned")


def cmd_stats(conn):
    def one(sql, *a):
        row = conn.execute(sql, a).fetchone()
        return row[0] if row else None

    print("=== corpus ===")
    print(f"  runs:  {one('SELECT COUNT(*) FROM runs')}"
          f"  ({one('SELECT MIN(started_at) FROM runs')} .. "
          f"{one('SELECT MAX(started_at) FROM runs')})")
    print(f"  plans: {one('SELECT COUNT(*) FROM plans')}")
    for table in ["pulls", "deaths", "boss_kills", "status_timeline", "diag_roster"]:
        print(f"  {table+':':<17}{one(f'SELECT COUNT(*) FROM {table}')}")

    print("\n=== verdicts ===")
    for result, n in conn.execute(
            "SELECT result, COUNT(*) FROM runs GROUP BY result ORDER BY 2 DESC"):
        print(f"  {result:<18}{n}")

    print("\n=== fail classes (68 raw reason strings collapse to these) ===")
    for cls, n in conn.execute(
            "SELECT fail_class, COUNT(*) FROM runs WHERE fail_class <> 'none' "
            "GROUP BY fail_class ORDER BY 2 DESC"):
        print(f"  {cls:<20}{n}")

    unclassified = one("SELECT COUNT(*) FROM runs WHERE fail_class = 'unclassified'")
    if unclassified:
        print(f"\n  !! {unclassified} unclassified reason(s) — add a FAIL_RULES entry:")
        for (reason,) in conn.execute(
                "SELECT DISTINCT fail_reason FROM runs WHERE fail_class = 'unclassified' "
                "LIMIT 10"):
            print(f"     {reason}")

    print("\n=== success by dungeon ===")
    for row in conn.execute(
            "SELECT dungeon, heroic, runs, success_pct FROM v_success_by_dungeon"):
        print(f"  {row[0]:<17} heroic={bool(row[1])!s:<6} n={row[2]:<5} {row[3]}%")

    print("\n=== pull calibration (error = observed - predicted, bodies) ===")
    print("    'under' is provable; 'over' is an upper bound (observed is a 1 Hz floor)")
    for row in conn.execute(
            "SELECT dungeon, heroic, pulls, avg_error_bodies, pct_underestimated, "
            "pct_apparent_over, observed_max, wipe_pulls "
            "FROM v_pull_calibration_by_dungeon LIMIT 8"):
        print(f"  {row[0]:<17} heroic={bool(row[1])!s:<6} n={row[2]:<6} "
              f"avg_err={str(row[3]):>6}  under={row[4]}% over<={row[5]}% "
              f"obs_max={row[6]} wipes={row[7]}")

    print("\n=== deploy attribution ===")
    print(f"  runs attributed: {one('SELECT COUNT(*) FROM runs WHERE deploy_id IS NOT NULL')}"
          f" / {one('SELECT COUNT(*) FROM runs')}")
    # v_fail_by_deploy splits by dungeon on purpose; roll that up here so the
    # summary shows one line per commit.
    for row in conn.execute(
            """
            SELECT substr(deploy_sha, 1, 8), deploy_source, COUNT(*) AS runs,
                   ROUND(100.0 * SUM(ok) / COUNT(*), 1) AS success_pct,
                   substr(deploy_subject, 1, 46), MAX(started_at)
            FROM v_runs WHERE deploy_sha IS NOT NULL
            GROUP BY deploy_sha ORDER BY MAX(started_at) DESC LIMIT 10
            """):
        print(f"  {row[0]}  {row[1]:<9} n={row[2]:<5} {str(row[3]):>6}%  {row[4]}")


def cmd_sql(conn, sql):
    """Ad-hoc query, so this tool needs no sqlite3 CLI (which isn't installed here)."""
    cur = conn.execute(sql)
    if cur.description is None:
        conn.commit()
        print(f"ok ({cur.rowcount} row(s) affected)")
        return
    cols = [d[0] for d in cur.description]
    rows = cur.fetchall()
    widths = [len(c) for c in cols]
    for row in rows:
        for i, val in enumerate(row):
            widths[i] = max(widths[i], len(str(val)))
    print("  ".join(c.ljust(widths[i]) for i, c in enumerate(cols)))
    print("  ".join("-" * w for w in widths))
    for row in rows:
        print("  ".join(str(v).ljust(widths[i]) for i, v in enumerate(row)))
    print(f"\n({len(rows)} row(s))")


def cmd_views(conn):
    """List the views with a row count each — the menu of canned questions."""
    for (name,) in conn.execute(
            "SELECT name FROM sqlite_master WHERE type='view' ORDER BY name"):
        try:
            n = conn.execute(f"SELECT COUNT(*) FROM {name}").fetchone()[0]
        except sqlite3.Error as exc:
            n = f"ERROR: {exc}"
        print(f"  {name:<34}{n}")


def cmd_mark_deploy(conn, sha, repo_label):
    import datetime
    now_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
    conn.execute(
        "INSERT OR REPLACE INTO deploys (sha, repo, committed_at_ms, subject, source) "
        "VALUES (?,?,?,?,'marker')",
        (sha, repo_label, now_ms, "recorded deploy"),
    )
    attribute_deploy(conn, repo_label)
    conn.commit()
    print(f"recorded deploy marker {sha[:12]} ({repo_label}) at {iso(now_ms)}")


def main():
    here = Path(__file__).resolve().parent
    ws_guess = find_workspace_root(here)

    ap = argparse.ArgumentParser(
        description="ETL the dungeon-clear test-run JSONL logs into SQLite.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--data-dir", type=Path, default=None,
                    help="dir holding dc_testruns.jsonl (default: <ws>/env/dist/bin)")
    ap.add_argument("--db", type=Path, default=None,
                    help="sqlite path (default: <ws>/deployment-files/analytics/"
                         "dc_analytics.sqlite)")
    ap.add_argument("--ws-root", type=Path, default=ws_guess,
                    help="workspace root (auto-detected)")
    ap.add_argument("--rebuild", action="store_true",
                    help="delete the db and re-ingest from scratch")
    ap.add_argument("--force", action="store_true",
                    help="re-ingest records already present (in place)")
    ap.add_argument("--stats", action="store_true",
                    help="print a summary instead of ingesting")
    ap.add_argument("--views", action="store_true",
                    help="list the canned views and their row counts")
    ap.add_argument("--sql", metavar="QUERY", default=None,
                    help="run a query and print a table (no sqlite3 CLI needed)")
    ap.add_argument("--mark-deploy", metavar="SHA", default=None,
                    help="record a real deploy of SHA as of now")
    ap.add_argument("--deploy-repo", default="module", choices=[r[0] for r in DEPLOY_REPOS],
                    help="which repo's commits attribute runs (default: module)")
    ap.add_argument("--deploy-branch", default="master",
                    help="integration branch whose first-parent history supplies "
                         "build markers (default: master)")
    args = ap.parse_args()

    ws_root = args.ws_root
    if ws_root is None:
        ap.error("could not locate the workspace root (no env/dist/bin/"
                 f"{RUNS_FILE} found above {here}); pass --ws-root")

    data_dir = args.data_dir or (ws_root / "env" / "dist" / "bin")
    db_path = args.db or (ws_root / "deployment-files" / "analytics" / "dc_analytics.sqlite")

    if args.rebuild and db_path.exists():
        db_path.unlink()
        print(f"removed {db_path}")

    db_path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA foreign_keys = ON")
    conn.executescript(SCHEMA)
    conn.executescript(VIEWS)

    if args.mark_deploy:
        cmd_mark_deploy(conn, args.mark_deploy, args.deploy_repo)
    elif args.sql:
        cmd_sql(conn, args.sql)
    elif args.views:
        cmd_views(conn)
    elif args.stats:
        cmd_stats(conn)
    else:
        cmd_ingest(conn, data_dir, ws_root, args)
        print(f"\ndb: {db_path}")
        print("next: tools/dc_analytics.py --stats   (summary)")
        print("      tools/dc_analytics.py --views   (canned questions)")
        print("      tools/dc_analytics.py --sql 'SELECT * FROM v_fail_mix'")

    conn.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
