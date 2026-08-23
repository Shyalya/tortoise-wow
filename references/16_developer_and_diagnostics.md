# 16. Diagnostics & Lookups

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

### 📚 Complete 19-Module Reference Library

| Security & Server | World & Characters | Gameplay & Items | Bots & Modules |
| :--- | :--- | :--- | :--- |
| [00. Self & Own Character](./00_self_and_player_commands.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [01. Security & Accounts](./01_security_and_account.md) | [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [02. Server & Reloads](./02_server_and_reloads.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [10. Turtle Custom](./10_turtle_custom_features.md) | [17. Tier Sets & Gear](./17_tier_items_and_endgame_gear.md) | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |
| [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | [18. Class Talent Specs](./18_class_talent_specs.md) | *(Standalone Manual)* | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) |

---

## 1. Categorized In-Game Lua Macros (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`) or run them directly in chat:

### Category A: Universal Database Lookups & Item Discovery (Top Priority)

#### 🗡️ Macro 1: Lookup Legendary Items & Raid Itemsets
Searches database tables and prints matching entry IDs for items and full gear sets:
```lua
.lookup item Thunderfury
.lookup itemset Dreadnaught
```

#### 🐉 Macro 2: Lookup Boss Creature IDs & Spells
Finds creature spawn entry IDs and spell IDs directly in-game:
```lua
.lookup creature Onyxia
.lookup spell Frostbolt
```

#### 📜 Macro 3: Lookup Quest & Profession Skill IDs
Queries quest IDs and profession skill entry numbers:
```lua
.lookup quest Diplomat
.lookup skill Blacksmithing
```

#### 📏 Macro 4: Target GUID & Exact Distance Calculation
Outputs target's low/high database GUID, exact 3D Euclidean distance in yards, and coordinates:
```lua
.guid
.distance
.gps
```

---

### Category B: Aura, Threat & Spawn Inspection (Middle Priority)

#### ✨ Macro 5: Dump Active Buffs & Auras on Target
Lists all applied aura spell IDs, duration remaining, and stack counts on target unit:
```lua
.list auras
```

#### ⚔️ Macro 6: Inspect Live Mob Threat Table & Aggro Refs
Prints real-time threat values for all players and bots attacking the selected mob:
```lua
.list threat
.list hostilerefs
```

#### 🗺️ Macro 7: Locate All Spawns of Creature / GameObject
Lists map IDs and coordinates of every world spawn instance for the specified entry ID:
```lua
.list creature 10182
.list object 179501
```

---

### Category C: Engine Diagnostics & Network Telemetry (Bottom Section)

#### 🔬 Macro 8: Low-Level Engine Diagnostics & Update Fields
Dumps network opcode throughput, instance script memory, and unit update fields:
```lua
.debug packetstats
.debug instancedata
.debug fields show
```

---

## 2. Universal Search Commands (`.lookup`)

Verified against CMaNGOS command handlers (`src/game/Chat/Chat.cpp:330` & `Commands.cpp`):

| Command | Security | Description | Usage / Example |
| :--- | :--- | :--- | :--- |
| `.lookup item <name>` | Moderator (2) | Finds item IDs matching name string. | `.lookup item Thunderfury` |
| `.lookup itemset <name>` | Moderator (2) | Finds armor set IDs matching name. | `.lookup itemset Dreadnaught` |
| `.lookup creature <name>` | Moderator (2) | Finds NPC / creature entry IDs. | `.lookup creature Onyxia` |
| `.lookup object <name>` | Moderator (2) | Finds static GameObject entry IDs. | `.lookup object Black Lotus` |
| `.lookup spell <name>` | Moderator (2) | Finds spell IDs matching ability name. | `.lookup spell Frostbolt` |
| `.lookup quest <name>` | Moderator (2) | Finds quest IDs matching title. | `.lookup quest The Missing Diplomat` |
| `.lookup skill <name>` | Moderator (2) | Finds skill IDs (professions, weapon skills). | `.lookup skill Blacksmithing` |
| `.lookup faction <name>` | Moderator (2) | Finds faction template IDs. | `.lookup faction Stormwind` |
| `.lookup event <name>` | Moderator (2) | Finds world event IDs. | `.lookup event Darkmoon Faire` |
| `.lookup player name <name>` | Moderator (2) | Looks up player account info by character name. | `.lookup player name Sam` |
| `.lookup player account <acc>` | Moderator (2) | Looks up player info by account name. | `.lookup player account Sam` |
| `.lookup player ip <ip>` | Moderator (2) | Looks up accounts logged from IP address. | `.lookup player ip 127.0.0.1` |

---

## 3. World & Target Inspectors (`.list`)

Verified against CMaNGOS command handlers (`src/game/Chat/Chat.cpp:315` & `Commands.cpp`):

| Command | Security | Description | Usage / Example |
| :--- | :--- | :--- | :--- |
| `.list auras` | Developer (3) | Lists all active aura spell IDs on target unit. | `.list auras` |
| `.list creature <entry_id>` | Developer (3) | Lists world spawn coordinates of creature ID. | `.list creature 10182` |
| `.list object <entry_id>` | Developer (3) | Lists world spawn coordinates of GameObject ID. | `.list object 179501` |
| `.list threat` | Observer (1) | Displays live threat table values on target NPC. | `.list threat` |
| `.list hostilerefs` | Observer (1) | Lists hostile threat references for creature. | `.list hostilerefs` |
| `.list maps` | Developer (3) | Lists active loaded map instances in memory. | `.list maps` |
| `.list battlegrounds` | Developer (3) | Lists active battleground instance IDs. | `.list battlegrounds` |

---

## 4. Diagnostics & Debugging (`.debug`)

Verified against CMaNGOS command handlers (`src/game/Chat/Chat.cpp:160` & `Commands.cpp`):

- `.debug instancedata`
  - **Security**: Developer (3)
  - **What it does**: Dumps active instance script memory and encounter state.
  - **Example**: `.debug instancedata`

- `.debug packetstats`
  - **Security**: Developer (3)
  - **What it does**: Displays network opcode packet statistics and latency counters.
  - **Example**: `.debug packetstats`

- `.debug fields show`
  - **Security**: Developer (3)
  - **What it does**: Dumps raw unit update fields for selected unit.
  - **Example**: `.debug fields show`

- `.debug condition <id>`
  - **Security**: Developer (3)
  - **What it does**: Evaluates condition rule requirements.
  - **Example**: `.debug condition 5`

- `.distance`
  - **Security**: Developer (3)
  - **What it does**: Calculates exact 3D distance in yards to selected target.
  - **Example**: `.distance`

- `.guid`
  - **Security**: Moderator (2)
  - **What it does**: Displays low GUID and high GUID of selected target.
  - **Example**: `.guid`

---

<p align="center">
  [⬅ Prev: 15. Dungeon Clear AI Module](./15_dungeon_clear_module.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [01. Security & Accounts ➡](./01_security_and_account.md)
</p>
