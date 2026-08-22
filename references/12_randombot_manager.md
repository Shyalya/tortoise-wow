# 12. RandomBot Population

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

### 📚 Complete 16-Module Reference Library

| Security & Server | World & Characters | Gameplay & Items | Bots & Modules |
| :--- | :--- | :--- | :--- |
| [01. Security & Accounts](./01_security_and_account.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [02. Server & Reloads](./02_server_and_reloads.md) | [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | **[12. RandomBot Manager](./12_randombot_manager.md)** |
| [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [10. Turtle Custom](./10_turtle_custom_features.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |

---

## 1. Categorized In-Game Lua Macros (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`) or run them directly in chat:

### Category A: Population Telemetry & World Activity (Top Priority)

#### 📊 Macro 1: Inspect Server Bot Population & Server Load
Displays online/offline random bot counts, memory footprint, and server PID load balancing factors:
```lua
.rndbot stats
.rndbot pid
```

#### 🔄 Macro 2: Force Immediate Population Evaluation & AI Tick
Forces world server to evaluate zone bot density, respawns, and questing activity immediately:
```lua
.rndbot update
```

---

### Category B: Mass Bot Initialization & Grinding Behaviors (Middle Priority)

#### ⚔️ Macro 3: Mass Re-Roll & Initialize All Random Bots at Level 60
Re-initializes all random bots across the entire realm, equipping them with level 60 gear and talent builds:
```lua
.rndbot init % 60
```

#### 🏹 Macro 4: Deploy All Bots to Level-Appropriate Grinding Zones
Directs roaming bots to level-appropriate mob farming hotspots across Eastern Kingdoms and Kalimdor:
```lua
.rndbot grind *
```

#### 🛠️ Macro 5: Refresh All Random Bots (Clear Stuck States + Full Repair)
Restores health/mana, clears stuck pathfinding nodes, and repairs gear durability for all active bots:
```lua
.rndbot refresh *
.rndbot upgrade *
```

---

### Category C: Map Maintenance & Despawn (Bottom Section)

#### 🧹 Macro 6: Purge Stuck Bots from Unmapped Coordinates
Finds and despawns any orphaned bots that may have fallen through map geometry:
```lua
.rndbot clean map
```

---

## 2. Complete `.rndbot` Command Directory

Verified against CMaNGOS random playerbot manager (`src/modules/PlayerBots/playerbot/RandomPlayerbotMgr.cpp:3720`):

### 📈 Population & Telemetry Commands
- `.rndbot stats`
  - **What it does**: Outputs total registered bots, online/offline breakdown, class distribution, and memory allocations.
  - **Example**: `.rndbot stats`

- `.rndbot pid`
  - **What it does**: Displays the dynamic PID feedback controller values used for throttling bot processing based on server tick lag.
  - **Example**: `.rndbot pid`

- `.rndbot diff`
  - **What it does**: Displays AI tick execution time diffs.
  - **Example**: `.rndbot diff`

- `.rndbot update`
  - **What it does**: Triggers an immediate full cycle of random bot login/logout and activity evaluation.
  - **Example**: `.rndbot update`

---

### ⚙️ Lifecycle & Routine Management
- `.rndbot init <target | %> [level]`
  - **What it does**: Re-rolls talent build, spells, and equipment for specified bot (or `%` for all bots server-wide).
  - **Example**: `.rndbot init % 60`

- `.rndbot refresh <target | *>`
  - **What it does**: Refreshes stats, clears stuck states, and repairs gear.
  - **Example**: `.rndbot refresh *`

- `.rndbot upgrade <target | *>`
  - **What it does**: Upgrades equipment, spells, and talent tree for bot's current level.
  - **Example**: `.rndbot upgrade *`

- `.rndbot grind <target | *>`
  - **What it does**: Teleports bot(s) to mob farming locations appropriate for their level.
  - **Example**: `.rndbot grind *`

- `.rndbot rpg <target | *>`
  - **What it does**: Teleports bot(s) to cities, taverns, and quest hubs for RPG interaction.
  - **Example**: `.rndbot rpg *`

- `.rndbot clean map`
  - **What it does**: Scans all active map instances and removes unlinked or out-of-bounds bots.
  - **Example**: `.rndbot clean map`

---

<p align="center">
  [⬅ Prev: 11. Playerbot Control Suite](./11_playerbot_suite.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 13. Bot Whispers & Macro Book ➡](./13_bot_whispers_and_macros.md)
</p>
