# Turtle WoW full-audit stabilization notes — 2026-09-03

This pass reviewed the complete core and SQL tree, the production configuration,
the four Turtle databases, the current server logs, three crash dumps, and the
available Classic CMaNGOS implementation as a reference. It distinguishes
repairs proven by source/data evidence from features that require live gameplay
validation. Static inspection cannot honestly prove every encounter action,
door transition, quest choice, or client interaction without running the realm.

## Deployed repairs

### Core stability and performance

- Removed an unsafe background calculation from `PlayerBotLoginMgr`. The worker
  mutated the bot pool and read live `Player` objects concurrently with the
  world thread, then returned pointers into that same pool. Both captured
  SIGABRT crashes occurred during large bot character-load waves. Queue
  selection now runs on the world thread while character SQL holders remain
  asynchronous.
- Reduced the production bot login burst from 30 to 8 bots per interval. The
  configured population remains 1,000; startup admission is deliberately
  smoother to avoid the observed database and map-update spike.
- Fixed the account analysis IP-history query for MySQL 8
  `ONLY_FULL_GROUP_BY`. The aggregate total is now computed in a separate
  subquery rather than mixed with nonaggregated columns.
- Deduplicated the missing proc-trigger diagnostic by aura/trigger pair. The
  first malformed reference is still logged with IDs and effect index, but the
  same combat fault no longer floods hundreds of identical lines.

### World-data integrity

The production migration is `20260903214500_world.sql`. It was executed first
against an exact backup copy, executed a second time with zero changes to prove
idempotence, and then applied to `tw_world`.

- Normalized literal `script_name='0'` values to an empty script name.
- Cleared 130 impossible creature loot-store references, three missing
  pickpocket stores, and one missing skinning store. Valid loot rows were not
  altered and replacement loot was not fabricated.
- Removed one gameobject-loot row and one vendor row referencing nonexistent
  item templates.
- Removed 304 movement rows whose creature spawns do not exist.
- Removed 41 broken creature-link records and 24 broken creature-group records.
- Removed stale event links for missing creature/gameobject spawns.
- Removed stale creature, gameobject, and nested pool memberships whose spawn or
  pool endpoint does not exist.
- Removed questgiver/quest-finisher relationships whose object, creature, or
  quest endpoint does not exist.
- Terminated quest-chain links that point to nonexistent next quests.
- Enabled waypoint movement for existing spawns that have at least two actual
  waypoint records. Single-point rows were intentionally preserved because
  scripts may use them as markers.

### Earlier repairs included in this binary/database state

- Repaired Turtle transport template path IDs against the supplied 1.18.1 DBC
  data, including the custom boat/zeppelin routes.
- Stabilized transport/session handling and world-data range errors.
- Restored the GM free-flight command and playable rank-five assignment.
- Kept ADMIN and TAYTAY at rank 5, the highest playable rank. Rank 6 is console
  only and cannot be assigned to a player session by design.
- Shipped the 1.12-compatible DungeonClear addon. Its optional server module is
  present in source but remains disabled in this production build; enabling the
  large AzerothCore-derived automation layer during a stability repair would
  add unrelated runtime behavior and invalidate the baseline comparison.

## Audit coverage and findings

- Build: the full Release core with playerbots and scripts compiles and links.
- Accounts/characters: foreign-key-style account/character checks completed;
  bots and human players were separated in analytics interpretation.
- Progression: level 1–60 XP rows, playable race/class level endpoints, and
  class-level stats are present.
- Spawns: all active creature and gameobject spawns resolve to templates after
  cleanup.
- Loot/vendor/skinning/pickpocket: active template references were checked and
  unresolved references repaired as described above.
- Movement/travel: creature waypoints, pools, event links, taxi/transport loader
  paths, and custom transport templates were inspected. Transport correctness
  still needs one live cycle per route after restart.
- Instances: every dungeon/raid/custom map was inventoried for creature and
  gameobject population and script registration. Empty development maps were
  identified separately from populated instances. Vanilla does not use the
  later `dungeonencounter` DBC table, so its absence is not treated as a defect.
- Encounters: instance/boss scripts and ScriptLoader registration were scanned.
  Several commented functions are superseded by EventAI or are already
  registered by their instance script; blindly enabling them would double-run
  mechanics. They were left unchanged.
- Quests/events/doors: quest endpoints, chains, pool/event references, instance
  script registrations, and gameobject relations were checked. Custom scripts
  named in SQL but absent from the published source remain a source-repository
  gap; their behavior cannot be reconstructed safely without specifications.
- PvP/social/economy: battleground, honor, guild, group, trade, mail, auction,
  chat, and WHO handlers were source-reviewed and compiled. Protocol-level
  behavior needs live multi-client regression testing.
- Bots/AI: startup/login scheduling, character query holders, randomization,
  class strategies, dungeon-clear coverage, auction bot, and WHO bot filtering
  were reviewed. The production population stays at 1,000.
- Rates: XP/rate configuration keys and load paths are present and compile; each
  selected production value must be verified live because a static build cannot
  award test XP.

## Remaining live verification checklist

After the administrator starts the realm manually:

1. Wait for the explicit world-started message before logging in.
2. Watch `perf.log` while the eight-at-a-time bot admission completes; confirm
   map-system updates fall below the previous sustained 200–641 ms range.
3. Exercise `/who`, `/who <name>`, `/who <zone>`, and a class/level filter with
   one human player and the bot population.
4. Ride each repaired boat/zeppelin route through at least one complete loop and
   test disembark/reconnect on a transport.
5. Create a fresh Turtle character and compare it with an imported Classic
   character before attributing any character-specific crash to the core.
6. Run representative dungeon smoke tests for entrance/exit, required doors,
   boss aggro/reset/death, loot, and instance reset. The supplied DungeonClear
   roster is not complete for every custom map, so this remains a manual/runtime
   validation task.
7. Exercise trade, mail, auction listing/purchase, guild/group invite, chat,
   taxi, battleground queue, quest accept/complete/reward, XP gain, and loot
   methods with two real clients.

## Rollback

Exact pre-change database copies remain available as:

- `tw_world_backup_fullaudit_20260903`
- `tw_char_backup_fullaudit_20260903`
- `tw_logon_backup_fullaudit_20260903`
- `tw_logs_backup_fullaudit_20260903`

The pre-deployment server files remain in the dated backup directory on the
Turtle share.
