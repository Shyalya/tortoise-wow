# PlayerBots autonomy roadmap

## Goal

Make bots feel like party members who notice the same situations as a player,
prepare themselves, coordinate with the group, and recover from failure without
waiting for a chat command. The primary success metric should be **player
interventions per hour**, not merely combat damage.

This review compares the current Tortoise/CMaNGOS port with:

- [CMaNGOS PlayerBots](https://github.com/cmangos/playerbots), the closest
  compatible upstream.
- [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots), a useful
  AzerothCore/WotLK design reference whose code is not directly portable to this
  Classic core.
- The local `DungeonClear` module, which is already ahead of the generic bot
  loop for autonomous Classic instance routing, recovery, rest and events.

Upstream snapshots examined on 2026-08-27 were CMaNGOS PlayerBots
`076045efa835da9aab7caa943bca752aebe1baad` and mod-playerbots
`2f7d9f774987d0157c6a0d0cc08c40bec3db3945`.

## Current branch implementation status (`cc6e2d7`…`ee6031c`)

The following P0 items are implemented in code on this branch. This is a source
review only: none of the evaluation scenarios below have been runtime-validated
in-game here.

| P0 item | Status | Notes / remaining caveats |
| --- | --- | --- |
| Real summoning-ritual assistance | Implemented | Controlled bots join a nearby eligible ritual GO via `CMSG_GAMEOBJ_USE` (not the meeting-stone teleport). The ritual GUID is cached across same-tick Trigger/`isUseful`/`Execute` scans and revalidated before reuse. Still needs in-game smoke (two eligible bots, owner channeling, summon target present). |
| Quest-item target use | Implemented (random bots) | `UseRandomQuestItemAction` again uses `ItemRequiredTargetMap` / incomplete quest status on nearby creatures and game objects, keeping StartQuest item behavior. Failed uses are bounded with a 15s per-item/target retry. Gated to bots **without** an active player master; mastered companions still need a separate path. Needs live quest coverage (creature, corpse, GO). |
| Party-stop maintenance | Implemented (partial) | When a real master opens gossip on a vendor/repair NPC, grouped bots sell **strict** `ITEM_USAGE_VENDOR` trash and repair at that **exact** NPC (`AutoMaintenanceOnMasterVendor`, default on). No automatic buying of food/drink/ammo/reagents. Needs in-game checks for range, combat gates, and sell/repair only. |
| Unmarked crowd control | Implemented (partial) | Explicit RTI CC remains authoritative; in non-raid dungeons, when no CC icon is set, CC-enabled bots may pick a safer fallback add (excludes bosses, kill/RTI kill target, tank-held, low-health, DoT’d except fear/banish, already controlled). **No shared reservation yet**, so two bots can still race the same add. |
| Per-spell immunity checks | Implemented | Damaging casts are rejected when the target is immune to the spell or school (Classic spell/school/damage checks), without blocking neutral/non-damaging utility. Needs in-game confirmation on mixed-school packs and multi-effect spells. |

Still open relative to the P0 list: shared CC (and broader group) reservations, mastered-bot quest-item assist, and vendor buying driven by existing need calculations. P1/P2 and the evaluation harness remain future work.

## What is already strong

The repository already contains most of the low-level capabilities needed for
convincing bots:

- travel nodes, taxi and transport handling, quest selection and turn-in;
- role/spec selection, class combat strategies, threat control, AoE avoidance,
  consumables, buffing and resurrection;
- loot evaluation, equipment upgrades, repair, selling, buying, training,
  auction-house use, mail, professions and guild sharing/craft orders;
- group invitations, quest sharing, ready checks, LFG and battlegrounds;
- world RPG actions such as vendors, inns, mailboxes, auctioneers, guilds,
  crafting, duels and emotes;
- autonomous Classic dungeon routing and many scripted instance interactions
  through `DungeonClear`.

The largest problem is therefore not a lack of actions. It is that controlled
bots often do not decide to run those actions. The generic RPG-use trigger, for
example, deliberately stops when a bot has an active player master. Packet
mirroring only covers selected master actions and can invoke the wrong semantic
operation (the existing summoning-ritual handler teleports a bot instead of
joining the ritual).

## Priority capabilities

### P0: remove common manual interventions

1. **Real summoning-ritual assistance.** Detect a nearby active ritual owned by
   an eligible group member, move into interaction range and use the actual game
   object once. Do not use the existing meeting-stone teleport shortcut.
   Controlled-bot assist with cached GO validation is in; confirm in-game.

2. **Quest-item target use.** Data-driven use of quest items on creatures or
   game objects via `ItemRequiredTargetMap`, incomplete quest status and spell
   validation, with per-target retry cooldowns. Random-bot path is in; mastered
   companions still need an equivalent assist path.

3. **Party-stop maintenance.** When the player opens a nearby vendor or repair
   NPC, controlled bots should safely run their existing repair and junk-selling
   policies at that NPC. Exact-NPC repair and strict vendor-trash selling are in;
   buying should only be added where the existing need calculation can determine
   food, drink, ammunition and reagents without a new economy policy.

4. **Unmarked crowd control.** Explicit raid icons must remain authoritative,
   but a CC-enabled bot should select a safe add when no CC icon exists. Exclude
   bosses, the kill target, tank-held targets, damaged/DoT targets and already
   controlled enemies. Fallback selection is in; a shared reservation is still
   needed to prevent two bots from choosing the same add on the same tick.

5. **Per-spell immunity checks.** Target selection already avoids broad
   invulnerability; spell validation should also reject damage spells when the
   target is immune to that spell or school. Damage spell/school rejection is
   in; confirm in-game on mixed-school and multi-effect cases.

### P1: make the party self-managing

1. **Companion-autonomy policy.** Add one coherent configuration/strategy rather
   than a collection of unrelated switches:

   - `strict`: follow explicit commands; current controlled-bot behavior;
   - `assist`: opportunistic safe interactions and maintenance near the master;
   - `independent`: accept nearby quests, gather, maintain and travel within a
     configurable leash when the group is idle.

   Each autonomous action should state which policy permits it. Avoid broadly
   enabling the current RPG strategy for mastered bots because it includes
   social and movement choices that can pull them away from the party.

2. **Group readiness state machine.** Before a pull or when a ready check starts,
   evaluate all members for health/mana, durability, ammunition, poisons,
   weapon imbues, shards/reagents, food/drink and essential buffs. Let bots fix
   local problems, report only blockers, and automatically re-evaluate. Extend
   `DungeonClear`'s existing rest/recovery gate rather than adding a second
   dungeon-only readiness system.

3. **Shared group intent/reservations.** Maintain short-lived reservations for
   CC targets, loot/game objects, interrupts, resurrect targets and maintenance
   NPCs. The group leader or a lightweight shared value should assign work;
   individual bot ticks should not race independently.

4. **Autonomous dungeon lifecycle.** Compose existing LFG, travel/summon and
   `DungeonClear` capabilities into an optional flow: form a role-valid party,
   travel to or enter the instance, start when ready, recover from wipes, finish,
   leave and repair. Keep automatic start opt-in so normal player-led dungeon
   runs are not hijacked.

5. **Generic cooperative game-object interaction.** Introduce a filtered action
   for levers, ritual objects and quest-relevant goobers. It should use server
   eligibility and quest/event state, not names or a blanket mirror of every
   `CMSG_GAMEOBJ_USE`; blindly mirroring doors and buttons can immediately undo
   the player's action.

### P2: make behavior believable and diagnosable

1. **Persistent intent and outcome memory.** The mod-playerbots “new RPG” work is
   valuable mainly because it models explicit states such as questing, camping,
   grinding, taxi travel and outdoor PvP. Port the idea, not the WotLK code: keep
   a current goal, expected outcome, deadline and failure count. A bot should
   stop retrying an unusable object or immune spell and choose a recovery plan.

2. **World social behavior.** Add low-frequency, rate-limited interactions such
   as buffing nearby real players, helping with an attacked mob, greeting,
   trading useful crafted items and joining suitable groups. Existing RPG,
   guild and broadcast machinery covers much of the execution; the missing part
   is restrained target selection and encounter memory.

3. **Instance-mechanic framework.** Continue moving repeated mechanics out of
   boss-specific code into reusable primitives: spread/stack, interrupt rotation,
   dispel priority, avoid/stand-in area, click/hold object, kite, focus adds and
   synchronized movement. Keep per-encounter files mostly declarative.

4. **Activity scaling.** If many random bots are enabled, borrow the
   mod-playerbots concept of a rotating active population with force-active
   rules for combat, groups, instances, queues, friends/guild members and nearby
   real players. This makes richer thinking affordable without updating every
   world bot every tick.

## Proposed control loop

The reusable implementation should have four layers:

1. **Sense:** convert packets and nearby world state into facts such as “ritual
   needs one participant”, “vendor opened”, “quest item has a valid target” or
   “party lacks one buff”.
2. **Propose:** capability-specific code emits an intent with relevance, safety
   requirements, expected outcome, timeout and autonomy policy.
3. **Coordinate:** group reservations resolve duplicate targets and role-specific
   work before normal engine arbitration.
4. **Verify:** observe the outcome, clear the intent on success, or apply a
   bounded retry/backoff and recovery choice on failure.

This fits the existing trigger/action/value engine while preventing every new
autonomy feature from becoming another bespoke high-priority trigger.

## Evaluation scenarios

Add deterministic or scripted smoke scenarios before expanding the autonomy
surface:

- a warlock starts Ritual of Summoning with two eligible bots nearby;
- a quest requires using an item on a live creature, corpse and game object;
- the player visits a repair vendor with damaged bots and full bags;
- an unmarked three-mob pull has two different CC-capable bots;
- a target is immune to one damage school but vulnerable to another;
- a dungeon party enters with missing ammunition/reagents and low durability;
- a wipe includes dead, disconnected and out-of-instance members;
- an unusable object remains visible for several minutes.

Track at least:

- commands/interventions per player-hour;
- time spent stuck, idle while grouped, or repeating a failed action;
- quest objectives completed without commands;
- invalid casts/interactions and repeated failures;
- dungeon completion time, wipes, recovery time and abandoned runs;
- repair/reagent/ammunition blockers at pull time;
- group reservation collisions (duplicate CC, interrupt or resurrect attempts).

## Integration rule

Treat CMaNGOS PlayerBots as the first backport source. Use mod-playerbots as a
capability and architecture reference only: it targets AzerothCore/WotLK and its
core APIs, spell data, LFG, movement and instance assumptions differ materially
from this Classic/Turtle server. Every backport should be small, independently
reviewed, compile-validated with the repository's low-memory `-j1` build, and
tested in one of the scenarios above before it becomes a default behavior.
