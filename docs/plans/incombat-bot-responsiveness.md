# In-combat bot responsiveness plan

Status: Phases 1–5 are implemented as uncommitted changes. Final integration
review is complete, and low-memory single-threaded compile validation passed.
`ctest` discovered 0 configured tests. Phase 0 expanded instrumentation,
focused automated tests, and representative runtime/load soak remain
deferred/pending. No configuration or rotation behavior changes are included.

## Objective and guardrails

Improve how quickly PlayerBots notice and respond to meaningful in-combat state
changes while preserving the decisions the existing strategies make. The work
should reduce avoidable idle gaps caused by update delays, stale volatile
values, movement/facing timers, and generic spell-failure backoff.

The following are explicitly out of scope:

- No spell-list changes.
- No class-rotation changes.
- No action relevance changes.
- No priority, multiplier, trigger ordering, or class-strategy tuning.
- No automatic promotion of triggers into the urgent lane based on their
  relevance. Urgent membership must be explicit and reviewable.
- No broad global cache refresh on every update. Refresh and invalidation must
  be selective and limited to values whose correctness is needed for the
  current combat transition.

An implementation may change when an already-selected action is evaluated or
executed, and may add wakeups for existing actions. It must not change which
action wins under the same snapshot and strategy configuration.

## Verified current paths

These observations were checked against `origin/main` in this worktree:

1. `PlayerbotAI::UpdateAI` decrements `aiInternalUpdateDelay`, updates facing,
   invokes `UpdateAIReaction`, and only then runs `UpdateAIInternal` when the
   internal delay is below the 100 ms gate. A currently casting generic spell
   can extend the internal delay by cast time plus `ReactDelay` and world
   average diff. See
   `src/modules/PlayerBots/playerbot/PlayerbotAI.cpp` around lines 284-292 and
   655-684, and `PlayerbotAIBase.cpp` around lines 16-30.
2. `ReactionEngine::Update` can discover an incoming reaction in one update,
   return with that reaction pending, and start it on a later update. It also
   applies a `ReactDelay`-based reaction timer when neither a pending nor an
   ongoing reaction exists. See
   `src/modules/PlayerBots/playerbot/strategy/ReactionEngine.cpp` around lines
   163-210.
3. `ReactionEngine::FindReaction` updates the object context, handles commands,
   processes reaction triggers, and evaluates the existing reaction queue. It
   uses `iterationsPerTick` as a work bound. See the same file around lines
   37-128.
4. `Engine::DoNextAction` separately updates the object context, processes
   triggers, pushes defaults, evaluates usefulness/possibility, executes one
   action, and records success/failure. It already has performance scopes and
   action-log records, but failure is not currently represented as a typed
   recovery reason. See `strategy/Engine.cpp` around lines 124-357.
5. `CalculatedValue` stores `lastCheckTime` as `time_t`. The intended subsecond
   path for short intervals therefore needs careful redesign: the current
   `(now - lastCheckTime > 0.1)` comparison still has whole-second clock
   resolution. `AiObjectContext::Update` is intentionally disabled because a
   broad update was measured as CPU-expensive. See `strategy/Value.h` around
   lines 48-89 and `strategy/AiObjectContext.cpp` around lines 110-126.
6. `UpdateFaceTarget` does not face while moving and schedules its next check
   at five times `ReactDelay` for normal activity or ten times for minimal
   activity. There is no general movement-completion wakeup in this path. See
   `PlayerbotAI.cpp` around lines 718-765.
7. Action duration and spell helpers write the shared internal delay, including
   global-cooldown and cast-wait paths. Generic failure paths can assign a
   global cooldown even when the useful recovery is movement, target refresh,
   or facing. See `PlayerbotAI.cpp` around lines 767-784, 2157-2167, and the
   `failWithDelay` branches around lines 4980-5430.
8. `PlayerbotMgr::UpdateAIInternal` currently schedules manager work at
   `ReactDelay`, while host hooks drive playerbot updates from the server tick.
   Any per-bot responsiveness change must therefore remain bounded across all
   bots and must not turn the manager into an unbounded busy loop.

## Ordered implementation phases

Each phase should be a small commit or review unit. A phase may be delegated to
one implementation agent, followed by a separate review agent that checks the
scope and the invariants in this document. Agents must read the current code
and tests before editing and must report exact files changed.

### Phase 0: Baseline, contracts, and instrumentation scaffolding

Purpose: establish measurements before changing cadence and make later
regressions diagnosable.

Expected touch points:

- `src/modules/PlayerBots/playerbot/PerformanceMonitor.h/.cpp`
- `src/modules/PlayerBots/playerbot/BotActionLog.h/.cpp`
- `src/modules/PlayerBots/playerbot/strategy/Engine.cpp/.h`
- `src/modules/PlayerBots/playerbot/strategy/ReactionEngine.cpp/.h`
- `src/modules/PlayerBots/playerbot/PlayerbotAI.cpp/.h`
- `src/modules/PlayerBots/playerbot/PlayerbotAIConfig.cpp/.h`
- Relevant `aiplayerbot.conf.dist.in*` files only if a new diagnostic setting
  is needed.

Work:

- Add low-volume counters or structured diagnostic records for:
  - update entry to reaction discovery;
  - reaction discovery to execution;
  - update entry to normal action execution;
  - movement completion to next combat decision;
  - cast completion/interruption to next decision;
  - action outcome and typed failure category;
  - time spent waiting with no eligible action.
- Reuse existing performance scopes and per-bot action logs. Do not log every
  value read or every server tick by default.
- Add a disabled-by-default configuration switch or existing diagnostics
  integration if runtime collection is required. Make sampling and output
  bounded per bot and per map.
- Define a stable event vocabulary in comments or a small enum so later agents
  do not invent incompatible labels.

Risks and controls:

- Logging can itself become a load source during an attack or a large raid.
  Use counters, sampling, and bounded ring buffers; preserve current logging
  defaults.
- Do not expose attacker-controlled strings without existing sanitization and
  length limits.

Success metrics:

- A controlled test can distinguish reaction discovery, reaction execution,
  movement arrival, cast completion, and action failure.
- Diagnostics disabled means no material per-tick allocation or log output.

Rollback/config:

- Revert the instrumentation commit independently.
- Keep all new collection opt-in, with no change to `ReactDelay`,
  `GlobalCooldown`, or `MaxWaitForMove` defaults.

### Phase 1: Same-update reaction execution and bounded preemption

Purpose: remove the known extra update between finding an existing urgent
reaction and starting it, without changing reaction selection.

Expected touch points:

- `src/modules/PlayerBots/playerbot/strategy/ReactionEngine.cpp/.h`
- `src/modules/PlayerBots/playerbot/PlayerbotAI.cpp/.h`
- `src/modules/PlayerBots/playerbot/PlayerbotAIBase.cpp/.h` only if an
  explicit wakeup API is required.
- Reaction-focused tests under
  `src/modules/PlayerBots/playerbot/strategy/tests/`.

Work:

- Refactor the reaction update state machine so that, after
  `FindReaction` selects a valid existing reaction, `StartReaction` can run in
  the same update before returning to `PlayerbotAI`.
- Preserve the distinction between `incomingReaction` and
  `ongoingReaction`, listener ordering, prerequisite/alternative processing,
  interrupt-cast behavior, interrupt-movement behavior, and action duration.
- Ensure a reaction that is already ongoing is not started twice and that a
  failed execution is not converted into an ongoing reaction.
- Define whether a same-update execution is reported as `reactionFound` and
  keep that meaning consistent for facing, interruption, telemetry, and the
  normal engine gate.
- Keep the existing queue iteration bound. A same-tick path must execute at
  most one selected reaction and must not recursively drain the reaction queue.

Risks and controls:

- Re-entrancy or double execution could cause duplicate casts. Add explicit
  state assertions and tests for pending, ongoing, failed, and completed
  reactions.
- A reaction may interrupt a cast or movement before normal AI runs. Verify
  this remains governed solely by the existing action flags.

Success metrics:

- An eligible existing reaction executes in the same AI update in which it is
  discovered.
- No change in selected reaction, relevance, prerequisite order, or action
  duration.
- No additional queue drain beyond the configured iteration bound.

Rollback/config:

- Hide the same-tick path behind a narrowly scoped feature flag during rollout
  if the existing configuration conventions permit it; default it to the
  current behavior until soak testing completes.
- The flag must only control timing, never action membership or relevance.

### Phase 2: Selective millisecond freshness and targeted invalidation

Purpose: make only the volatile combat facts fresh enough for fast reactions,
without enabling the disabled global context update or refreshing all values.

Expected touch points:

- `src/modules/PlayerBots/playerbot/strategy/Value.h/.cpp`
- `src/modules/PlayerBots/playerbot/strategy/AiObjectContext.h/.cpp`
- `src/modules/PlayerBots/playerbot/strategy/values/` classes for the selected
  values after their actual implementations are identified.
- `src/modules/PlayerBots/playerbot/PlayerbotAI.cpp/.h`
- Targeted tests in `strategy/tests/`.

Work:

- Introduce a monotonic millisecond timestamp or equivalent freshness policy
  for explicitly selected volatile values. Candidate values include current
  health/power, current cast/channel, current target validity, distance/range,
  facing, movement state, crowd-control state, and hazard/emergency state.
- Preserve existing second-scale behavior for expensive scans, long-lived
  memory values, travel, loot, and strategy data.
- Add a narrow invalidation API that can reset a named value or a documented
  small group of combat values. Keep invalidation keyed by value name and
  qualifier where applicable so it does not flush the whole context.
- Invalidate at known transitions: combat entry/exit, target death or target
  replacement, cast start/finish/interruption, aura/control changes, movement
  completion, and geometry-related action failure.
- Do not call `AiObjectContext::Update` globally per server tick and do not
  replace all `time_t` storage blindly. Any conversion must account for
  persisted/memory values and existing interval semantics.
- Document each fast value’s maximum acceptable age, update cost, and
  invalidation sources.

Risks and controls:

- A clock or interval conversion can change value semantics globally. Limit
  the new policy to explicit subclasses/instances and add boundary tests for
  zero, subsecond, and long intervals.
- Resetting a value while it is being evaluated can cause inconsistent reads.
  Perform invalidation at update boundaries and keep a coherent per-update
  combat snapshot for values that must agree.
- Target pointers can become invalid. Use existing GUID-safe lookup patterns
  and validate object lifetime before dereferencing.

Success metrics:

- Selected volatile values meet their documented freshness budget (target:
  100–200 ms under normal combat cadence) without changing unrelated value
  refresh rates.
- Target death, target replacement, cast completion, and control changes are
  visible on the next eligible decision.
- CPU and allocation overhead for a bot outside combat remains effectively
  unchanged.

Rollback/config:

- Provide a configuration switch for the fast freshness policy if needed for
  staged deployment. Default it conservatively and retain current intervals
  as the fallback.
- Roll back value policy and invalidation separately from scheduler changes.

### Phase 3: Movement completion and facing wakeups

Purpose: prevent a bot from waiting for an unrelated timer after it reaches
range, gains line of sight, stops being blocked, or becomes able to face its
target.

Expected touch points:

- `src/modules/PlayerBots/playerbot/PlayerbotAI.cpp/.h`
- `src/modules/PlayerBots/playerbot/PlayerbotAIBase.cpp/.h`
- `src/modules/PlayerBots/playerbot/strategy/actions/MovementActions.cpp`
  and the specific movement action/value files identified by the agent.
- Movement/target/facing value implementations under
  `src/modules/PlayerBots/playerbot/strategy/values/`.
- `src/modules/PlayerBots/playerbot/strategy/tests/MonitorMovement.*` and
  related combat tests.

Work:

- Track movement state transitions rather than only a periodic timer: moving
  to stopped, path completion, arrival in usable range, LOS restoration,
  movement interruption, and stuck/blocked detection.
- On a meaningful transition, invalidate only geometry/facing/target values
  and wake the AI at the next safe update boundary.
- Revisit `UpdateFaceTarget`’s `ReactDelay * 5/10` timer. Keep a periodic
  fallback for missed events, but allow immediate scheduling after movement
  stops or the target changes. Do not force facing while movement or control
  state makes it unsafe.
- Distinguish movement action duration from “do not evaluate anything” delay.
  A long estimated travel time must not suppress urgent reactions; the safety
  cap represented by `MaxWaitForMove` remains a watchdog, not the normal
  arrival latency.
- Avoid issuing repeated movement commands when a movement generator is still
  making progress. Add hysteresis/debounce around arrival and stuck signals.

Risks and controls:

- Frequent wakeups while a bot is pathing can multiply CPU use. Wake only on a
  real state transition, enforce a minimum debounce, and preserve a hard
  per-bot work budget.
- Facing packet emission must remain compatible with current movement and
  control checks; do not send repeated facing packets for an unchanged angle.

Success metrics:

- After movement completion or LOS/range restoration, the next eligible combat
  decision occurs within 100–200 ms in normal conditions.
- No repeated movement/facing oscillation and no increase in movement packet
  rate beyond an agreed bounded threshold.

Rollback/config:

- Keep the periodic timer path available as a fallback.
- Make debounce/minimum wake interval configurable only if required; do not
  alter combat strategy settings.

### Phase 4: Typed cast-failure recovery

Purpose: replace generic failure sleeping with narrowly targeted recovery while
preserving existing action selection and spell behavior.

Expected touch points:

- `src/modules/PlayerBots/playerbot/PlayerbotAI.cpp/.h`
- `src/modules/PlayerBots/playerbot/strategy/Action.cpp/.h`
- `src/modules/PlayerBots/playerbot/strategy/Engine.cpp/.h`
- Existing spell/action helper implementations that provide cast results.
- `src/modules/PlayerBots/playerbot/strategy/tests/TestAction.*` and
  `TestStrategy.*`.

Work:

- Establish a typed, non-user-facing failure classification at the lowest
  existing layer that has enough information: global cooldown/cast in
  progress, out of range, line of sight, facing, invalid/dead target, control
  restriction, resource/cooldown unavailable, movement conflict, or unknown.
- Record the classification for diagnostics and invalidate only the stale
  dependency. Examples: geometry failure wakes movement/facing; invalid target
  wakes target validation; cast completion schedules the actual earliest retry;
  control failure does not spin until control changes.
- Preserve existing alternatives and continuers. A failed action may cause
  the already-existing alternative path to be considered, but no new
  relevance or priority may be introduced.
- Replace unconditional `GlobalCooldown` backoff only where the core spell
  result proves that waiting for the global cooldown is the correct recovery.
  Unknown failures retain a conservative bounded delay.
- Guard against hot-looping on deterministic failures with per-action,
  per-target debounce and a maximum retry rate.

Risks and controls:

- Spell result APIs vary across supported core variants. Isolate compatibility
  code and compile every relevant build variant where practical.
- Incorrect classification can cause repeated casts or movement thrashing.
  Default unknown results to the existing safe delay and test each class.

Success metrics:

- Range/LOS/facing/target failures lead to the matching recovery path on the
  next bounded update rather than waiting a full unrelated delay.
- No repeated cast loop exceeds the configured retry budget.
- Existing successful casts and action alternatives remain unchanged.

Rollback/config:

- Keep the current generic failure delay as the fallback for unknown results.
- Roll back typed recovery independently from instrumentation and freshness.

### Phase 5: Explicit urgent-reaction routing

Purpose: ensure already-existing emergency behavior is evaluated through the
fast path when it is safe and useful, without changing what any strategy
considers relevant.

Expected touch points:

- `src/modules/PlayerBots/playerbot/strategy/ReactionEngine.cpp/.h`
- `src/modules/PlayerBots/playerbot/strategy/Trigger.cpp/.h` or the existing
  trigger registration/context files, if explicit metadata belongs there.
- Existing reaction trigger/action registration contexts for interrupts,
  defensives, dispels, hazard escape, and emergency target recovery.
- `src/modules/PlayerBots/playerbot/PlayerbotAI.cpp/.h`
- Reaction and combat tests.

Work:

- Define explicit metadata or a reviewed registry for urgent reaction classes.
  Candidate categories are interrupt, imminent lethal/critical defensive,
  dispel/purge of urgent control, hazard escape, and invalid-target recovery.
- Route only those explicitly tagged existing triggers through the fast
  reaction lane. Preserve their action objects, `isUseful`, `isPossible`,
  multipliers, relevance values, prerequisites, alternatives, and listener
  checks.
- Ensure urgent routing is preemptive only where the existing action flags
  authorize cast or movement interruption. It must not cancel an unrelated
  cast merely because a trigger has a high relevance value.
- Define fairness: urgent processing has a strict per-update and per-bot cap,
  and normal combat still receives service after the urgent lane is empty.
- Do not infer urgency automatically from numeric relevance and do not promote
  every defensive or interrupt-like action without a reviewed registration.

Risks and controls:

- The main risk is accidentally changing strategy semantics through trigger
  registration edits. A reviewer should compare the before/after action graph
  and reject any unrelated class-strategy modifications.
- A malicious or noisy state transition could generate many urgent checks.
  Deduplicate by event/state generation and cap work.

Success metrics:

- Tagged urgent reactions are discoverable within one fast update under load.
- Untagged normal actions retain the existing engine path and ordering.
- No increase in urgent queue growth, duplicate execution, or starvation of
  normal combat decisions.

Rollback/config:

- Keep an explicit kill switch for urgent routing during rollout.
- Removing metadata must return the trigger to the existing normal path
  without deleting or altering its action.

### Phase 6: Performance safeguards and configuration review

Purpose: make faster reactions safe for servers with many bots and resistant
to event storms.

Expected touch points:

- `src/modules/PlayerBots/playerbot/PlayerbotAIBase.cpp/.h`
- `src/modules/PlayerBots/playerbot/PlayerbotAI.cpp/.h`
- `src/modules/PlayerBots/playerbot/PlayerbotMgr.cpp/.h`
- `src/modules/PlayerBots/playerbot/RandomPlayerbotMgr.cpp/.h` if the shared
  scheduling path is affected.
- `PlayerbotAIConfig.cpp/.h` and the applicable `aiplayerbot.conf.dist.in*`
  files.
- `PerformanceMonitor.*` and diagnostics files from Phase 0.

Work:

- Add a per-bot and aggregate work budget for fast-path evaluations, using
  existing `iterationsPerTick` and manager scheduling concepts rather than
  unbounded loops.
- Use deadline-based wakeups where possible: earliest cast completion, global
  cooldown, movement arrival estimate, or debounce expiry. Never schedule a
  negative/zero-delay busy loop for a state that cannot change yet.
- Preserve a slower minimal path for inactive/out-of-combat bots and avoid
  increasing random-bot manager cadence globally.
- Add configuration for new timings/caps only when operational tuning is
  necessary. Document units, safe ranges, default values, and the fallback
  behavior. Keep existing defaults unchanged unless a later benchmark proves
  a measured regression.
- Add counters for budget exhaustion, suppressed wakeups, repeated failures,
  and maximum queue depth.

Risks and controls:

- Lowering delays globally can amplify CPU and I/O use across hundreds of
  bots. Require before/after map-tick and AI-time measurements at representative
  bot counts.
- Config values must be clamped to safe minimums and maximums. A zero fast
  delay must not mean an infinite same-frame loop.

Success metrics:

- No unbounded loop under repeated combat events, movement failures, or cast
  failures.
- In-combat reaction latency improves while aggregate AI CPU, allocations,
  action-log volume, and map tick time stay within agreed budgets.
- Out-of-combat and minimal-mode cost remains unchanged within measurement
  noise.

Rollback/config:

- Every new behavior has a kill switch or can be disabled by restoring the
  existing delay path.
- Document an operator rollback sequence: disable fast routing, restore
  conservative freshness, then revert code if needed.

### Phase 7: Tests, load validation, and integration review

Purpose: validate timing improvements and prove that rotations and strategy
semantics were not changed.

Expected touch points:

- `src/modules/PlayerBots/playerbot/strategy/tests/TestAction.*`
- `src/modules/PlayerBots/playerbot/strategy/tests/TestStrategy.*`
- `src/modules/PlayerBots/playerbot/strategy/tests/TestContext.*`
- `src/modules/PlayerBots/playerbot/strategy/tests/MonitorCombat.*`
- `src/modules/PlayerBots/playerbot/strategy/tests/MonitorMovement.*`
- New focused test files only if the existing harness cannot express the
  cases.
- Build/test configuration only where the existing bot test target requires
  registration.

Required behavioral tests:

- Same-update reaction discovery and execution, including interrupt and
  movement-interrupt flags.
- Pending reaction, ongoing reaction, failed reaction execution, and reaction
  completion without duplicate execution.
- Millisecond freshness boundaries for selected volatile values; unrelated
  values must retain their existing refresh behavior.
- Target death/replacement invalidates current-target data without flushing
  the whole context.
- Cast start, completion, and interruption wake the appropriate path.
- Movement stopped/arrived, range restored, LOS restored, facing required, and
  stuck/blocked transitions produce one bounded wakeup.
- Typed cast failures select targeted recovery and unknown failures retain the
  conservative fallback.
- Urgent routing honors explicit membership, existing usefulness/possibility,
  action flags, queue caps, and normal-action fairness.
- Repeated events do not create a busy loop or duplicate cast/movement packet.

Semantic regression checks:

- For deterministic snapshots, compare action names, selected strategy state,
  relevance, alternatives, prerequisites, and continuers before and after.
  Timing may differ; decisions may not.
- Audit the diff for forbidden changes to class rotation files, spell lists,
  relevance values, priority tables, and trigger ordering.

Performance validation:

- Run focused bot tests first.
- Run representative combat/load scenarios with diagnostics off and then with
  sampled diagnostics on; record reaction latency, AI time, map tick time,
  queue depth, action count, movement/facing packet count, and allocations.
- Exercise many simultaneous bots and event storms, not only a single bot.

Build constraints:

- This VPS has approximately 2 GiB RAM. For compile validation, create the
  launcher inside the ignored `build/` directory:

  ```sh
  mkdir -p build
  printf '%s\n' '#!/bin/sh' 'exec "$@" -O0 -g0' > build/lowmem-compiler-launcher.sh
  chmod +x build/lowmem-compiler-launcher.sh

  cmake -S . -B build \
    -DDEBUG_SYMBOLS=OFF \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$PWD/build/lowmem-compiler-launcher.sh" \
    -DCMAKE_C_COMPILER_LAUNCHER="$PWD/build/lowmem-compiler-launcher.sh"

  nice -n 15 ionice -c 3 cmake --build build -- -j1
  ```

- Never use `-j2` or higher in this worktree. Do not run a production `-O3`
  build unless explicitly requested and the additional memory/time cost is
  approved.

Exit criteria:

- All focused and applicable existing tests pass.
- Low-memory single-threaded compile validation passes.
- Measurements show a meaningful reduction in avoidable reaction/movement
  latency with bounded CPU and I/O cost.
- Review confirms no rotation, spell-list, relevance, or priority changes.
- New configuration and rollback behavior are documented.

## Agent execution order and review gates

Use one implementation agent per phase, with the following dependencies:

```text
Phase 0 instrumentation
        |
        +--> Phase 1 same-tick reactions
        +--> Phase 2 selective freshness/invalidation
                 |
                 +--> Phase 3 movement/facing wakeups
                 +--> Phase 4 cast-failure recovery
                                  |
                                  +--> Phase 5 urgent routing
                                                   |
                                                   +--> Phase 6 safeguards
                                                                    |
                                                                    +--> Phase 7 tests/load review
```

Phase 1 and Phase 2 may be developed in parallel after Phase 0, but they must
be integrated and reviewed together before movement, failure, or urgent
routing work relies on their APIs. Every agent should keep its diff narrow,
avoid drive-by formatting, and leave the worktree buildable. A reviewer should
reject any patch that implements broad global cache refresh or automatic
relevance-based trigger promotion.
