# 15. Dungeon Clear AI Module

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

### 📚 Complete 16-Module Reference Library

| Security & Server | World & Characters | Gameplay & Items | Bots & Modules |
| :--- | :--- | :--- | :--- |
| [01. Security & Accounts](./01_security_and_account.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [02. Server & Reloads](./02_server_and_reloads.md) | [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [10. Turtle Custom](./10_turtle_custom_features.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | **[15. Dungeon Clear AI](./15_dungeon_clear_module.md)** | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |

---

## 1. Categorized In-Game Lua Macros (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`) or run them directly in chat:

### Category A: Autonomous Dungeon Solver & Combat Coordination (Top Priority)

#### 🏰 Macro 1: Activate Full Autonomous Dungeon Clear AI
Instructs all party bots to resolve waypoints, handle boss mechanics, manage healer mana thresholds (>60%), avoid AoE hazards, and roll/loot items:
```lua
/p @all co +dungeon clear,avoid aoe,roll,loot,potions
/p @all nc +food
/p @all formation near
/p @all follow
```

#### 🚨 Macro 2: Emergency Combat Disengage & Reset
Forces all bots to immediately drop combat targets, sprint to player, and reset combat state:
```lua
/p @all flee
/p @all follow
.combatstop
```

---

### Category B: Post-Combat Looting & Instance Breaches (Middle Priority)

#### 💰 Macro 3: Fast Post-Boss Loot & Consumable Recovery
Orders party to loot all boss/trash corpses, eat/drink for full mana, and rebuff:
```lua
/p @all loot
/p @all food
/p @all buff
```

#### 🚪 Macro 4: Breach & Interact with Nearest Door / Lever
Targets nearest locked door, gate, or lever and orders bots to interact:
```lua
.gobject target
/p @all talk
```

---

## 2. Mod-Dungeon-Clear Architecture & Configuration

`mod-dungeon-clear` is a dedicated C++ behavioral module that turns PlayerBots into autonomous dungeon explorers:

- **Navmesh Route Resolution**: Automatically resolves dungeon corridors, ramps, elevators, and doors.
- **Pull Governor**: Coordinates tank pulls, prevents unintended trash add packs, and pauses pulls when healer mana drops below threshold.
- **Boss Encounter Logic**: Handles interrupts, dispels, phase positioning, and mechanics.

### Configuration Settings (`run/modules/mod_dungeon_clear.conf`)
| Setting | Default | Description |
| :--- | :---: | :--- |
| `DungeonClear.Enable` | `1` | Master toggle for intelligent dungeon solver. |
| `DungeonClear.AllowRaid` | `0` | Allows autonomous AI clearing in 10/20/40-man raids. |
| `DungeonClear.MinHealerMana` | `60` | Tank pauses pulls until healer mana reaches this percentage. |
| `DungeonClear.DoorPolicy` | `1` | Enables bots to open doors and interact with levers. |
| `DungeonClear.AggroRange` | `25.0` | Maximum trash pack pull detection radius. |

---

<p align="center">
  [⬅ Prev: 14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 16. Diagnostics & Lookups ➡](./16_developer_and_diagnostics.md)
</p>
