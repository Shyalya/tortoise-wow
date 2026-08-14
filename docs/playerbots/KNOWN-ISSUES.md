# PlayerBots Known Issues & Fix Log

Living record of bugs found in the PlayerBots module (and adjacent core systems it
touches), what's confirmed vs. still open, and which commit fixed what. Kept so
investigation already done doesn't get re-walked from scratch in a future session.

Format for new entries: symptom -> evidence -> root cause (or "not found") ->
fix/commit -> live-verification status. Don't mark something Fixed until it's been
confirmed live, not just compiled clean.

---

## OPEN

### ~~Bots freeze inside battlegrounds~~ — FIXED 2026-08-14, see FIXED table below
Full investigation narrative kept here since it's the reference trail for how the
root cause was actually found (several ruled-out theories, then the real one).
**Symptom:** bots that successfully queue and enter a BG stop running AI entirely.
Within ~15 minutes they sit on identical coordinates with identical health.
**Status:** root cause not found. Ruled out so far (each with live evidence):
- AI not running / minimal-mode misclassification — `[BGDIAG2]` sampling showed
  `minimal=0 strat=yes` in 67/67 samples (commit `15f84e2`).
- Missing BG combat strategy — was real (inverted conditional in
  `AiFactory::AddDefaultCombatStrategies`), fixed in `6fdc731`, confirmed via
  12min/144-bot measurement this did **not** fix the freeze.
- Missing navmesh/vmap data — confirmed present for maps 489/12 tiles, 529/16
  tiles, 30/27 tiles.
- Match stuck in preparation — `status=3` (STATUS_IN_PROGRESS) confirmed firing.
- Blood Ring specifically freezing worse than WSG/AB was traced to a separate bug
  (uncapped arena queue letting bots pile into a match that never ends) and mitigated
  in `04fe310` by capping bots out of Blood Ring entirely — this reduces exposure to
  the freeze but does not fix the freeze itself, which is arena-agnostic.
**Next step (per `6fdc731`'s own note):** log which action `Engine::DoNextAction`
actually selects for a frozen bot inside a BG — the freeze is confirmed to be below
the trigger/strategy layer, inside movement or objective action selection itself.

**2026-08-14 update — sharper evidence, one confirmed sub-bug, root cause still open:**
Live-observed on commit `d59176f`, a real WSG match (map 489) with ~19 bots + 1 real
player, ~10+ minutes in.

- **DB position proof:** `SELECT guid,name,position_x,position_y,position_z FROM
  characters WHERE map=489 AND online=1` showed 8 of 19 bots sitting at exactly
  identical coordinates (to the thousandth decimal) in two clusters — e.g. Adelani
  (guid 704), Augustina (967), Janchuna (1982), Periela (471), Rohander (1774) all at
  `(933.331, 1433.72, 345.536)`; Amelineri (2917), Boastin (3542), Dilena (2923) all
  at `(1519.53, 1481.87, 352.024)`. These read as each team's BG entry platform —
  bots frozen from the moment they spawned in, not mid-match.
- **bot_events.csv proof (new, more precise than `[BGDIAG2]`):** this log records
  every action a bot's AI *actually executes* (combat, quests, movement, everything).
  For every affected bot checked (Adelani, Amelineri, Augustina, Rohander), the
  **last ever entry is their `BGJoinAction` queue confirmation** — after that,
  crossing into the BG instance, there is **not one further logged action of any
  kind** for the rest of the match (10+ minutes and counting). This is stronger than
  the earlier `[BGDIAG2]` finding: it's not just movement that stops, it's every
  action type — combat included.
- **Cross-checked against `[BGDIAG2]`:** these same bots still hit `DoNextAction
  reached, minimal=0 strat=yes` repeatedly during the freeze (confirmed in the live
  log) — so the AI update loop is technically still running and not in minimal/idle
  mode. The break is between "trigger evaluated as active" and "an action actually
  executes and gets logged" — i.e. below `ProcessTriggers`, inside action selection
  or `Action::Execute()` dispatch itself, specifically once the bot is inside a BG
  instance.
- **One confirmed, fixed sub-bug found along the way** (real, but not proven to be
  the root cause of the above): `BGTactics::atFlag()`,
  `strategy/actions/BattleGroundTactics.cpp:4469` had
  `if (!bot->CanInteract(go) && bgType != BATTLEGROUND_WS)` — C++ evaluates the left
  operand first, so `bot->CanInteract(go)` (and its core-engine
  `CanInteractWithGameObject` error-log side effect) fired unconditionally every tick
  for every WSG bot regardless of distance; only the resulting `continue` was
  conditional on `bgType`. This is what produced 15,000+ `ERROR:CanInteractWithGameObject:
  ... too far away ...` log lines (confirmed present as far back as the
  2026-08-12 log, so pre-existing, not introduced by any recent merge). Fixed by
  reordering to `if (bgType != BATTLEGROUND_WS && !bot->CanInteract(go))`, matching
  the evident original intent (WSG has its own distance handling further down via
  `flagRange`/`INTERACTION_DISTANCE`). **Not yet rebuilt/deployed/verified live** —
  found and patched in source only as of this entry.
- **Also noted, not confirmed as causal:** `BGTactics::startNewPathFree()`
  (`BattleGroundTactics.cpp:4385`) computes `currentPoint = closestPoint - 1` where
  both are `uint32` — if the bot's closest waypoint is index 0, this underflows to
  `UINT32_MAX`. Traced forward into `moveToObjectiveWp()`: due to unsigned wraparound
  the very next `currPoint++` brings it back to a small valid index, so this likely
  self-corrects rather than crashing/hanging — flagged for awareness, not treated as
  the freeze cause.
- **What this rules out for the next session:** it's not a movement-specific bug
  (combat actions are equally silent), and it's not a trigger/strategy-assignment
  problem (already fixed in `6fdc731`, and `[BGDIAG2]` confirms triggers fire). The
  next concrete step is instrumenting **action selection/dispatch itself** (what
  `Engine::DoNextAction` picks, and whether `Action::Execute()` is even being called)
  specifically for bots with `bot->InBattleGround()==true`, since that's the one
  variable that flips exactly when the freeze begins.

**ROOT CAUSE FOUND AND FIXED, 2026-08-14 (moved to FIXED section below).** Added
temporary `[BGDIAG3]` instrumentation directly inside `Engine::DoNextAction()`'s
action-processing loop (queue size before/after `PushDefaultActions`, per-basket
useful/possible/executed verdicts, queue size at the end of every iteration). Live
trace on a real WSG/AB match showed the exact mechanism: `queue.Size()` would drop
from a healthy N (8-9 items) straight to 0 in a single iteration, immediately after
`check flag` (backed by `BGTactics::atFlag()`/`BGTactics::Execute()`) ran - even
though other actions failing in the same loop (`check objective`, `emote`) did not
wipe the queue. Root cause: `BGTactics::Execute()` - the single function backing
every BG action name (`check flag`, `check objective`, `move to objective`, `select
objective`, `protect fc`, `use buff`, `move to start`) - unconditionally calls
`ai->ChangeStrategy("-buff", BotState::BOT_STATE_NON_COMBAT)` on **every
invocation**, with no check for whether "buff" is already removed. `ChangeStrategy`
routes to `Engine::ChangeStrategy()` on the exact non-combat engine that is
*currently mid-iteration through its own action queue, from inside this very call*,
and ends by calling `Init()`, which starts with `Reset()` - a `while` loop that
drains and deletes every item in `queue`. So every tick, the first BGTactics action
that runs wipes out every other queued action (the movement/objective fallbacks a
failed `check flag` should fall through to) before it even evaluates whether the
flag is in range - and since the same trigger keeps re-firing the same high-relevance
`check flag` basket every tick, this repeated indefinitely.

### Bots appearing ungeared in battlegrounds
**Symptom:** user-reported — bots showing up in BGs without proper gear.
**Investigated 2026-08-13:** no config switch literally named "Create Gear on Level
Up" exists in either `Shyalya/tortoise-wow` or `Penqle/tortoise-wow`. Closest
functional match is `AiPlayerbot.RandomGearUpgradeEnabled` ("Regularly upgrade bots
equipment for their level", `RandomPlayerbotMgr.cpp:3250`) — confirmed **enabled**
(`=1`) in the live `install/aiplayerbot.conf`, so it is not currently disabled.
`AiPlayerbot.GearProgressionSystem.Enable` is `0` but that system only applies to
max-level random bots grouped with a real player, unrelated to general BG gearing.
**Working theory, unconfirmed:** may not be a gearing bug at all — a bot frozen
solid by the freeze bug above, sampled mid-fight, could visually read as "not
properly equipped/participating" without actually lacking gear. Needs a direct
in-game check (inspect a bot mid-BG-freeze for its actual equipped items) to
confirm or rule this out before chasing it as a separate bug.
**Next step:** get eyes on a frozen BG bot's character sheet/paperdoll in-game to
determine whether this is the same bug as the freeze above, or genuinely separate.

### TravelNode transport links missing vehicle entry
**Symptom:** 147 of 233 rows in `ai_playerbot_travelnode_link` still carry
`object = 0` for dock-to-vehicle hops.
**Status:** `3a31c76` fixed the generator so future graph regenerations will
populate this correctly, but the fix is currently inert — the node tables are
pre-seeded from SQL and only rebuild when empty, so existing bad rows persist
until either a full regeneration or a manual SQL remap.

### GroupValues cross-bot recursion
**Status:** investigated twice, downgraded to low priority, left as-is. Actual
cost is cheap cache-hit lookups, not expensive recomputation as originally
feared. Sits in combat engage/avoid decision paths, so any future change here
needs live raid verification, not a drive-by fix.

### Trigger-tiering not yet applied to `NeedTravelPurposeValue`/`ShouldTravelNamedValue`
**Status:** deferred — these are reached via dynamic-qualifier trigger strings
(`"val::need travel purpose::64"`), so fixing them needs a bespoke `Trigger`
subclass, not the one-line registration used for the other trigger-tiering wins.

---

## FIXED (verify entries stay live-confirmed, not just committed)

| Fix | Commit | Verified live? |
|---|---|---|
| **Bots froze completely inside battlegrounds.** `BGTactics::Execute()` (backs every BG action name) unconditionally called `ai->ChangeStrategy("-buff", BOT_STATE_NON_COMBAT)` every invocation with no idempotency check — this re-entrantly called `Engine::Init()`→`Reset()` on the *same* non-combat engine currently mid-iteration through its own action queue, draining every other queued action (movement/objective fallbacks) out from under the loop the instant any BG action ran. Gated the call on `HasStrategy("buff", ...)` so it only fires once. | pending commit (see `strategy/actions/BattleGroundTactics.cpp:2716-2729`) | **Yes** — post-deploy: bot DB positions went from identical-to-the-thousandth frozen clusters to fully unique/spread coordinates; `bot_events.csv` went from zero logged actions for the whole match to continuous `CheckMountStateAction` position changes and multiple real `AutoReleaseSpiritAction` combat deaths |
| BG queue join used real Battlemaster guid when cached, failing a silent proximity check and drifting the bot-count tracker ahead of the real queue | `74f8c95` (superseded local `5d87e53`) | Yes — 28→194 queue joins in 11min, both brackets filled |
| `AiFactory::AddDefaultCombatStrategies` had an inverted `InBattleGround()` check, so bots got zero combat strategies while actually in a BG | `6fdc731` | Yes (fix confirmed correct; did not resolve the separate freeze bug) |
| Blood Ring arena had no way to cap bots out (per-team cap ignored for arenas) — bots drained into it, its matches never ended, starving WSG/AB of bot pop (217→12 WSG matches/day) | `04fe310` | Not yet independently re-measured post-merge |
| Auction bot completed purchases from the bot thread, touching live sessions (`SendAuctionOwnerNotification`) unsafely | `7efdbe2` | Not yet fired in practice (bot-owned auctions only, so far) |
| `AhBot::ForceUpdate` iterated the live `AuctionEntryMap` on a detached thread while the world thread mutated it — real crash observed, `std::_Rb_tree_increment` | `d651158` | Crash fix, not yet re-tested for a repeat crash |
| `Map::GetTransports()` was a stub returning empty; `WorldPosition::getTransports` built a malformed guid | `b8016d7` | Not yet visually confirmed (bots boarding boats/zeppelins) |
| Boarding a transport was never logged | `bbdf9f7` | N/A (logging only) |
| `OnTaxiFlightEject` was a no-op stub, so a bot already flying could never start its next taxi leg | `ec5e62b` | Not yet re-tested |
| Trueshot Aura silently removed minutes after cast (misclassified as a stacking-exclusive Hunter Aspect) | `a45e88e` (Penqle `1181dev`) | Pending live verification — see below |
| Server tick time reduced ~1476ms → ~307-522ms (multiple passes: redundant DB queries, uncached grid scans, redundant `Map::UpdatePlayers()` bot reprocessing, activity-priority re-enable) | multiple, see `performance_optimization_mandate` session notes | Yes, each pass individually confirmed via `.performance`/`perf.log` at the time |

---

## How to add an entry

New open issue: symptom, what evidence ruled things out (with commit/log
references), current best theory, next concrete step. New fix: what was broken,
the commit hash, and whether it's been confirmed live — not just compiled.
