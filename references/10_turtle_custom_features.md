# 10. Turtle Custom Features

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

### Category A: Guild Administration & Guild Housing (Top Priority)

#### 🏰 Macro 1: Fast Travel to Instantiated Guild House
Teleports your character directly to your guild's custom instantiated guild house:
```lua
.guild house
```

#### 🛡️ Macro 2: Create Guild & Grant Guild Master Rank
Creates a guild with designated leader and assigns Guild Master Rank 0:
```lua
.guild create Sam "Knights of Tortoise"
.guild rank Sam 0
```

#### ✉️ Macro 3: Invite Player Directly to Guild
Adds target character directly to guild roster without requiring a guild charter:
```lua
.guild invite Sam "Knights of Tortoise"
```

---

### Category B: Hardcore & Turtle Challenge Modes (Middle Priority)

#### 💀 Macro 4: Hardcore Alerts & Dedicated Hardcore Chat
Enables global broadcast notifications for Hardcore character deaths and unlocks the Hardcore channel:
```lua
.hcmessages 1
.hcchat 1
```

#### 🗺️ Macro 5: Cartographer Addon Map Explorer
Reveals all map tiles for Cartographer addon integration:
```lua
.cartographer
.explorecheat on
```

#### 🐢 Macro 6: Slow & Steady / Trainee Challenge Mode
Toggles the custom Turtle-WoW "Trainee" slow-leveling mode:
```lua
.toggletrainee
```

---

### Category C: Engine Profiling & Performance Telemetry (Bottom Section)

#### 📈 Macro 7: Live Performance Monitor HUD
Toggles real-time server tick rates, memory usage, and thread performance HUD:
```lua
.perfmon
```

---

## 2. Guild & Custom Features Reference (`.guild` / `.cartographer` / `.hcmessages`)

Verified against CMaNGOS command handlers (`src/game/Chat/Chat.cpp` & `Commands.cpp`):

### 🏰 Guild Operations (`.guild`)
- `.guild house`
  - **Security**: Developer (3)
  - **What it does**: Teleports player to their guild's instantiated guild house map.
  - **Example**: `.guild house`

- `.guild create <leader> "<guild_name>"`
  - **Security**: Developer (3)
  - **What it does**: Instantly establishes a new guild.
  - **Example**: `.guild create Sam "Knights of Tortoise"`

- `.guild delete "<guild_name>"`
  - **Security**: Developer (3)
  - **What it does**: Disbands the specified guild.
  - **Example**: `.guild delete "Knights of Tortoise"`

- `.guild invite <player> "<guild_name>"`
  - **Security**: Moderator (2)
  - **What it does**: Adds player to guild roster.
  - **Example**: `.guild invite Sam "Knights of Tortoise"`

- `.guild uninvite <player>`
  - **Security**: Moderator (2)
  - **What it does**: Removes player from guild.
  - **Example**: `.guild uninvite Sam`

- `.guild rank <player> <rank_index>`
  - **Security**: Developer (3)
  - **What it does**: Sets player's guild rank index (`0` = Guild Master).
  - **Example**: `.guild rank Sam 0`

- `.guild leader "<guild_name>" <new_leader>`
  - **Security**: Developer (3)
  - **What it does**: Transfers guild ownership to new player.
  - **Example**: `.guild leader "Knights of Tortoise" Sam`

---

### 🐢 Turtle Custom System Commands
- `.cartographer`
  - **Security**: Player (0)
  - **What it does**: Uncovers map fog-of-war for Cartographer addon.
  - **Example**: `.cartographer`

- `.hcmessages <0 | 1>`
  - **Security**: Player (0)
  - **What it does**: Toggles Hardcore death broadcast alerts across the realm.
  - **Example**: `.hcmessages 1`

- `.hcchat <0 | 1>`
  - **Security**: Player (0)
  - **What it does**: Toggles dedicated Hardcore world chat channel.
  - **Example**: `.hcchat 1`

- `.toggletrainee`
  - **Security**: Developer (3)
  - **What it does**: Toggles Trainee / Slow & Steady challenge mode.
  - **Example**: `.toggletrainee`

- `.xp [on | off]`
  - **Security**: Player (0)
  - **What it does**: Turns character experience gain on or off.
  - **Example**: `.xp off`

- `.perfmon`
  - **Security**: Moderator (2)
  - **What it does**: Toggles real-time performance telemetry HUD.
  - **Example**: `.perfmon`

---

<p align="center">
  [⬅ Prev: 09. Moderation, Tickets & Anticheat](./09_moderation_tickets_and_anticheat.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 11. Playerbot Suite ➡](./11_playerbot_suite.md)
</p>
