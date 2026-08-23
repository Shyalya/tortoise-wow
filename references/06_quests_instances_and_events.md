# 06. Quests, Instances & Events

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

### Category A: Raid & Instance Lockout Resets (Top Priority)

#### 🔓 Macro 1: Clear All Personal Raid & Dungeon Lockouts
Clears all saved instance locks (Molten Core, BWL, AQ40, Naxxramas, Onyxia, ZG) for your character:
```lua
.instance unbind all
.instance listbinds
```

#### 🌍 Macro 2: Server-Wide Raid Lockout Reset
Forces an immediate reset on all active raid locks across all online players and parties:
```lua
.server resetallraids
```

#### 📜 Macro 3: Instant Quest Auto-Complete & Inspection
Marks all quest objectives complete in character quest log ready for turn-in:
```lua
.quest complete 870
.quest status 870
```

---

### Category B: World Events & Hunter Pets (Middle Priority)

#### 🎪 Macro 4: Force Start World Event (Darkmoon Faire / Holidays)
Activates the specified world event ID and verifies active event status:
```lua
.event start 1
.event list
```

#### 🐾 Macro 5: Hunter Pet Max Loyalty & Spells
Sets pet loyalty to Rank 6 (Best Friend) and teaches pet abilities:
```lua
.pet loyalty 6
.pet learnspell 24597
```

---

### Category C: Instance Performance & Diagnostics (Bottom Section)

#### 📊 Macro 6: Instance Memory & Tick Performance Dumps
Outputs live map grid memory allocations and instance performance metrics:
```lua
.instance stats
.instance perfinfos
```

---

## 2. Quests & Instances Commands Reference

Verified against CMaNGOS command handlers (`src/game/Chat/Chat.cpp` & `Commands.cpp`):

### 📜 Quest Management (`.quest`)
- `.quest add <quest_id>`
  - **Security**: Developer (3)
  - **What it does**: Adds quest directly to target character quest log.
  - **Example**: `.quest add 870`

- `.quest complete <quest_id>`
  - **Security**: Developer (3)
  - **What it does**: Marks all objectives complete for quest in character log, ready for turn-in.
  - **Example**: `.quest complete 870`

- `.quest remove <quest_id>`
  - **Security**: Developer (3)
  - **What it does**: Removes quest from target character quest log.
  - **Example**: `.quest remove 870`

- `.quest test <quest_id>`
  - **Security**: Developer (3)
  - **What it does**: Tests quest conditions, criteria, and requirements.
  - **Example**: `.quest test 870`

- `.quest status <quest_id>`
  - **Security**: Moderator (2)
  - **What it does**: Displays current quest status and objective counters for character.
  - **Example**: `.quest status 870`

---

### 🏰 Instance Lockouts & Controls (`.instance`)
- `.instance unbind all`
  - **Security**: Developer (3)
  - **What it does**: Clears all saved dungeon and raid lockout IDs for target character.
  - **Example**: `.instance unbind all`

- `.instance groupunbind <map_id>`
  - **Security**: Developer (3)
  - **What it does**: Unbinds entire party from specific map instance ID (409 = MC, 469 = BWL, 509 = AQ20, 531 = AQ40, 533 = Naxx).
  - **Example**: `.instance groupunbind 409`

- `.instance listbinds`
  - **Security**: Developer (3)
  - **What it does**: Lists all saved instance lockout IDs and reset timestamps for player.
  - **Example**: `.instance listbinds`

- `.instance stats`
  - **Security**: Developer (3)
  - **What it does**: Dumps active dungeon instance memory and player counts.
  - **Example**: `.instance stats`

- `.instance savedata`
  - **Security**: Administrator (4)
  - **What it does**: Forces an immediate write of instance memory state to database.
  - **Example**: `.instance savedata`

---

### 🤖 Autonomous Dungeon Clear & Test Harness (`.dc`)

For full documentation and GM test automation, see **[15. Dungeon Clear AI Module](./15_dungeon_clear_module.md)**:

- `.dc on` / `.dc off`: Activates or disables party tank autonomous dungeon routing and pulls.
- `.dc status`: Displays live instance run progression, target boss, and party readiness.
- `.dc bosses`: Lists all instance bosses, encounter kill bitmask, and status.
- `.dc skip` / `.dc pause` / `.dc pull`: Controls tank movement, skips packs/bosses, or forces pulls.
- `.dc spectate [follow [name] | next | prev | list]`: Spectator camera control across bots.
- `.dc test list`: Lists all supported automated test dungeons.
- `.dc test start <dungeon> [heroic]`: Spawns automated 5-bot party to clear dungeon headlessly.
- `.dc test watch [next|off]`: Teleports GM camera to follow live automated test runs invisibly.
- `.dc test stop [all]`: Stops running headless test runs.

---

### 🎪 World Events & Pets (`.event` / `.pet`)
- `.event start <event_id>`: Forces world event to activate (e.g. `.event start 1`).
- `.event stop <event_id>`: Forces world event to terminate (e.g. `.event stop 1`).
- `.event list`: Lists all active and scheduled world events.
- `.pet tp <points>`: Grants training points to hunter pet (e.g. `.pet tp 300`).
- `.pet loyalty <1-6>`: Sets pet loyalty level (e.g. `.pet loyalty 6`).
- `.pet rename <name>`: Renames hunter pet (e.g. `.pet rename Rex`).

---

<p align="center">
  [⬅ Prev: 05. Combat, Spells & Skills](./05_combat_spells_and_skills.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 07. Items, Economy & Mail ➡](./07_items_economy_and_mail.md)
</p>
