# PlayerBots Known Issues & Fix Log

Living record of bugs found in the PlayerBots module (and adjacent core systems it
touches), what's confirmed vs. still open, and which commit fixed what. Kept so
investigation already done doesn't get re-walked from scratch in a future session.

Format for new entries: symptom -> evidence -> root cause (or "not found") ->
fix/commit -> live-verification status. Don't mark something Fixed until it's been
confirmed live, not just compiled clean.

---

## OPEN

### Bots freeze inside battlegrounds
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
