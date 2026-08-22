> Upstream file from jrad7/mod-dungeon-clear, kept for reference. Its workflow
> rules applied to that repository, NOT to this tree - renamed so no tooling
> picks it up as live instructions.

# mod-dungeon-clear — development workflow

These rules exist because uncommitted work was getting orphaned across sessions
and feature branches got stacked on each other, forcing painful cherry-pick
untangling. Follow them exactly.

## The five rules

1. **Always branch from `master`, never from another feature branch.**
   `git switch -c feat/<name> master`. Stacking a feature on top of another
   unmerged feature is what forced the ZF / Sunken-Temple cherry-pick rewrite.
   Only branch off another feature branch when the dependency is deliberate and
   stated.

2. **Never use git worktrees.** Work only in the main checkout, switching
   branches with `git switch`. Worktrees were tried and retired: they
   accumulated unmanaged, half-forgotten trees whose branches drifted hundreds
   of commits behind master, and work went stale in directories nobody
   revisited. `tools/dc-feature.sh` is retired — do not run it. Rule 3 is what
   keeps concurrent features from mixing: commit before you switch, so there
   are never loose files to collide.

   **Two worktrees are permanent and must never be removed** — they are live
   running copies, not feature work:
   - `.claude/worktrees/ac-command-deck` (`feat/ac-command-deck`) — AC Command
     Deck, served on :8788 from its `dashboard/` subdir.
   - `.claude/worktrees/testdeck` (`feat/testdeck-clean`) — Test Deck, served
     on :8790 from its `testdeck/` subdir.

   Before removing any worktree, run `git status --ignored`: `git worktree
   remove` deletes gitignored files without warning and without needing
   `--force`. That is how the site-local `ac-dashboard.toml` and
   `testdeck.toml` were lost. Their contents now survive that — see
   *Site-local config lives outside the repo* below.

3. **A session boundary is a commit boundary — never stop on a dirty tree.**
   Before ending a session, commit, even if the feature is half-done:
   `git commit -m "wip: <what is done / what remains>"` on the feature branch.
   A half-finished feature is a *named branch with a wip commit*, never loose
   files in the working tree.

4. **Stamp commits with the originating session id** as a trailer, so any change
   can be traced back to the session that produced it:
   ```
   Session: S557
   ```

5. **Delete a feature branch the moment it merges.**
   `git merge --no-ff feat/<name> && git branch -d feat/<name>`. Keep the branch
   list to *only things in flight*. Periodically sweep:
   `git branch --merged master | grep -vE '^\*|master$' | xargs -r git branch -d`.

## Site-local config lives outside the repo

`streamcast.toml`, `testdeck.toml` and `ac-dashboard.toml` are site-local: not
in git, no backup. The real files live in `deployment-files/site-config/`, and
the checkouts hold **relative symlinks** to them. Losing a link is now a
nuisance, not data loss — restore every link with:

```
bash deployment-files/site-config/link-site-configs.sh
```

Two rules keep this working:

- **Add a new site-local config's ignore rule to `master`'s `.gitignore`, never
  only to the feature branch that introduces it.** A rule on a feature branch
  protects the file *only while you stand on that branch*; off it the file is
  plain untracked and the next `git clean -fd` / `git stash -u` /
  `git worktree remove` deletes it. That is exactly how `streamcast.toml`
  vanished. `.git/info/exclude` carries the same paths as a branch-proof
  backstop (it is shared with the linked worktrees).
- **Never replace one of these symlinks with a regular file.** Writers must
  open-and-truncate (`Path.write_text`, which `testdeck setup` uses) so the
  write passes through to the real file; a temp-file-plus-rename would silently
  break the link.

## Branch naming
`feat/` `fix/` `refactor/` `tune/` `perf/` `test/` `diag/` — prefix matches the
nature of the change.

## Intentionally-preserved unmerged branches
These diverge from master on purpose; do **not** delete them:
- `feat/cinematic-camera` — abandoned approach, history kept for reference.
- `fix/pause-leader-follow-hold`, `fix/pause-toggle-autopause-race` — reverted
  off master, preserved for re-diagnosis if the pause bug resurfaces.
- `refactor/consolidate-party-combat-state` — genuine WIP.

## Session start / end safety net
A `SessionStart` hook prints this module's git state (dirty tree + in-flight
branches) at the top of every session; a `Stop` hook warns if you try to end a
session with a dirty tree. They are reminders, not a substitute for the rules.

## Build & test
The user always builds and deploys — do not build or inspect the binary.
Run the gtest suite from this module root: `sudo bash t/run_tests.sh`.
Plan / design / review docs live in `deployment-files/docs/`, never committed
to this repo.

## Investigating a test run — always start here
When a task begins with a test-run id (`tr-20260801-174432-3`, or a plan id
`tp-…`, or a whole batch prefix `tr-20260801-174432`), run:

```
python3 tools/dc_test_run.py <id>
```

That is the first command, before any grepping. It gathers, in one pass, what is
otherwise scattered across five files: the run record from `dc_testruns.jsonl`
(comp, result, fail reason, boss/status/pull timelines, deaths, pauses, watchdog
budgets), the teardown **DcDiag snapshot** (target, route, pull FSM, per-member
party state, objective roster — usually where the answer is), the plan and its
sibling runs, live state for a run still in flight, and every `*.log` line that
belongs to that run. Log lines carry no run id, so it correlates on the run's own
bot names inside the run's time window, then reports a SIGNALS section counting
the known diagnostic shapes (stalls, resnaps, stranded recovery, pull release,
door blocks, wipes).

Drill down with `--grep RE`, `--logs pull`, `--level info`, `--dump DIR` (writes
the per-run slice to a file), `--json`. `--list` shows recent run ids.

Do not hand-grep `DungeonClear.log` for a run id — it only appears on the START
line. And note the appenders open with mode `w`: a worldserver restart wipes the
logs, so for an older run the record plus the diag snapshot are all that survive.
The tool says so explicitly when that has happened.
