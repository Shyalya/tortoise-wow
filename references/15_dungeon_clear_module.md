# 15. Dungeon Clear AI Module (.dc)

> **Target Core**: Turtle-WoW 1.18.1 (Build 7272) • CMaNGOS + PlayerBots Framework  
> **Source Verification**: `modules/mod-dungeon-clear/src/DungeonClearCommand.cpp`, `DungeonClearModule.cpp` & `data/dc_roster.txt`  
> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

## Complete 19-Module Reference Library

| Security & Server | World & Characters | Gameplay & Items | Bots & Modules |
| :--- | :--- | :--- | :--- |
| [00. Self & Own Character](./00_self_and_player_commands.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [01. Security & Accounts](./01_security_and_account.md) | [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [02. Server & Reloads](./02_server_and_reloads.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [10. Turtle Custom](./10_turtle_custom_features.md) | [17. Tier Sets & Gear](./17_tier_items_and_endgame_gear.md) | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |
| [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | [18. Class Talent Specs](./18_class_talent_specs.md) | *(Standalone Manual)* | **[15. Dungeon Clear AI](./15_dungeon_clear_module.md)** |

---

## 1. Categorized In-Game Lua Macros (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`) or run them directly in chat:

### Category A: Autonomous Dungeon Solver & Combat Coordination (Top Priority)

#### 🏰 Macro 1: Activate Full Autonomous Dungeon Clear AI

Instructs all party bots to resolve waypoints, handle boss mechanics, manage healer mana thresholds (>60%), avoid AoE hazards, and roll/loot items:

```lua
.dc on
/p @all co +dungeon clear,avoid aoe,roll,loot,potions
/p @all nc +food
/p @all formation near
/p @all follow
```

#### 🚨 Macro 2: Emergency Combat Disengage & Reset

Forces all bots to immediately drop combat targets, sprint to player, and reset combat state:

```lua
.dc pause
/p @all flee
/p @all follow
.combatstop
```

#### 🎯 Macro 3: Order Instant Tank Pull on Next Pack

Orders the leader tank bot to initiate a controlled pull on the targeted enemy mob or next waypoint pack:

```lua
.dc pull
```

#### ⏩ Macro 4: Skip Current Trash Pack or Boss

Instructs the tank routing solver to bypass the current encounter and advance to the next objective:

```lua
.dc skip
```

---

### Category B: Spectator Camera & Diagnostics (Middle Priority)

#### 🎥 Macro 5: Attach Spectator Camera to Party Tank Bot

Switches player view to ride the lead tank bot during automated runs:

```lua
.dc spectate follow
```

#### 🔄 Macro 6: Cycle Spectator Camera to Next Party Member

Cycles spectator viewpoint through all party members:

```lua
.dc spectate next
```

#### 📊 Macro 7: Instance Progress & Boss Bitmask Audit

Displays live instance completion percentage, defeated bosses, skipped mobs, and wipe counters:

```lua
.dc status
.dc bosses
```

---

### Category C: Post-Combat Looting & Instance Breaches (Bottom Section)

#### 💰 Macro 8: Fast Post-Boss Loot & Consumable Recovery

Orders party to loot all boss/trash corpses, eat/drink for full mana, and rebuff:

```lua
/p @all loot
/p @all food
/p @all buff
```

#### 🚪 Macro 9: Breach & Interact with Nearest Door / Lever

Targets nearest locked door, gate, or lever and orders bots to interact:

```lua
.gobject target
/p @all talk
```

---

## 2. Complete `.dc` Slash Command Directory

Verified against `modules/mod-dungeon-clear/src/DungeonClearCommand.cpp`:

### 🏰 In-Dungeon Tank Automation Commands (`.dc`)

These commands dispatch directly to the party's leader tank bot:

| Command | Arguments | Description | Example Usage |
| :--- | :--- | :--- | :--- |
| `.dc on` | *None* | Activates autonomous dungeon navigation, pulls, and boss strategies on party tank bot. | `.dc on` |
| `.dc off` | *None* | Disables autonomous dungeon clearing mode. | `.dc off` |
| `.dc status` | `[param]` | Displays live progression, current target boss, party readiness, and current phase. | `.dc status` |
| `.dc bosses` | `[param]` | Lists all bosses in the instance with alive, killed, or skipped status and encounter bitmask. | `.dc bosses` |
| `.dc skip` | *None* | Skips the current targeted mob pack or boss and routes to next objective. | `.dc skip` |
| `.dc pause` | *None* | Pauses tank advance and pull progression. | `.dc pause` |
| `.dc pull` | `[target]` | Orders tank bot to pull the next pack or targeted creature immediately. | `.dc pull` |
| `.dc go` | `<targetBoss>` | Sets the target boss destination to route directly toward. | `.dc go VanCleef` |
| `.dc config` | *None* | Dumps all live `DungeonClear.*` configuration variables and effective per-run addon overrides. | `.dc config` |

---

### 🎥 Spectator Camera Controls (`.dc spectate`)

Free-flying or bot-riding spectator camera:

| Command | Description | Example Usage |
| :--- | :--- | :--- |
| `.dc spectate` | Toggles free-flying spectator camera mode. | `.dc spectate` |
| `.dc spectate follow [name]` | Seats spectator camera on specified bot (or tank). Toggle again to turn off. | `.dc spectate follow Tankman` |
| `.dc spectate next` (or `.dc spectate n`) | Cycles spectator camera to the next party member. | `.dc spectate next` |
| `.dc spectate prev` (or `.dc spectate p`) | Cycles spectator camera to the previous party member. | `.dc spectate prev` |
| `.dc spectate list` (or `.dc spectate who`) | Lists all watchable bots in the current instance. | `.dc spectate list` |

---

### 🧪 Automated Test Harness & Headless Verification (`.dc test`)

Allows Game Masters or Console to spawn, gear, and run full 5-bot automated dungeon runs without human players:

| Command | Syntax / Example | Description |
| :--- | :--- | :--- |
| `.dc test list` | `.dc test list` | Lists all supported test dungeons, tokens (`deadmines`, `sm`, `strat`, etc.), map IDs, and levels. |
| `.dc test gear` | `.dc test gear <dungeon> [heroic]` | Displays recommended item level (ilvl) ladders and gear ceilings for that dungeon. |
| `.dc test start` | `.dc test start <dungeon> [heroic] [level=N] [seed=N] [ilvl=N\|none] [quality=rare\|epic]` | Spawns a full 5-bot party with random class comp and starts automated dungeon clear. |
| `.dc test start` | `.dc test start <dungeon> party=Tank,Heal,D1,D2,D3 [heroic]` | Starts automated run using a hand-picked roster of real player characters. |
| `.dc test status` | `.dc test status` | Shows live test run status, boss count, kill bitmask, party health, run duration, and watchdog logs. |
| `.dc test watch` | `.dc test watch [selector]` | Teleports GM to instance entrance in GM invisible mode, binds to instance, and attaches camera to tank bot. |
| `.dc test watch` | `.dc test watch next` | Tours the next active test run in a batch campaign. |
| `.dc test watch` | `.dc test watch off` | Disengages camera, restores GM visibility, teleports GM back to world recall position, and drops locks. |
| `.dc test stop` | `.dc test stop [selector\|all]` | Aborts single active run, specific run by ID/token, or `all` active test runs. |
| `.dc test plan start` | `.dc test plan start <spec>` | Starts a batched test campaign plan across multiple runs/concurrency. |
| `.dc test plan status` | `.dc test plan status` | Displays status of running batched test plans. |
| `.dc test plan stop` | `.dc test plan stop [planId\|all]` | Stops active test campaign plans. |

---

## 3. Mod-Dungeon-Clear Architecture & Dynamic Roster Data

`mod-dungeon-clear` is a dedicated C++ behavioral module that turns PlayerBots into autonomous dungeon explorers:

- **Navmesh Route Resolution**: Automatically resolves dungeon corridors, ramps, elevators, and doors via `NavmeshSnap::SnapColumn` and `LongRangePathfinder`.
- **Pull Governor**: Coordinates tank pulls, prevents unintended trash add packs, and pauses pulls when healer mana drops below threshold.
- **Dynamic Boss Roster & Order Overlay (`data/dc_roster.txt`)**: Allows live configuration of dungeon boss rosters, encounter orders, and dropped pool rares without needing C++ rebuilds. Live updates take effect immediately with `.reload config`:
  - `credit <entry> [<entry> ...]`: Adds creature entry IDs to the required boss kill list.
  - `order <mapId> <entry> <index>`: Assigns strict encounter order (1–32) for that map.
  - `drop <entry>`: Excludes low-percentage pool rares (e.g. Fallen Champion 2%, Earthcaller Halmgar 30%) so bots don't deadlock waiting for non-spawned creatures.

### Configuration Settings (`run/modules/mod_dungeon_clear.conf`)

| Setting | Default | Description |
| :--- | :---: | :--- |
| `DungeonClear.Enable` | `1` | Master toggle for intelligent dungeon solver. |
| `DungeonClear.AllowRaid` | `0` | Allows autonomous AI clearing in 10/20/40-man raids. |
| `DungeonClear.MinHealerMana` | `60` | Tank pauses pulls until healer mana reaches this percentage. |
| `DungeonClear.DoorPolicy` | `1` | Enables bots to open doors and interact with levers. |
| `DungeonClear.AggroRange` | `25.0` | Maximum trash pack pull detection radius. |

---

[⬅ Prev: 14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 16. Diagnostics & Lookups ➡](./16_developer_and_diagnostics.md)
