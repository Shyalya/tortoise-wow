# 13. Bot Whispers & Macro Book

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

### Category A: Raid & Dungeon Combat Triggers (Top Priority)

#### 🛡️ Macro 1: Main Tank Engage & Hold Aggro
Orders the designated party/raid tank bot to charge and establish maximum threat on selected target:
```lua
/p @all tank attack
```

#### ⚔️ Macro 2: All Bots Engage Target (Maximum DPS Burst)
Orders all DPS and pet bots to charge selected target and activate burst cooldowns:
```lua
/p @all attack
/p @all max dps
```

#### 🏹 Macro 3: Ranged Pull to Group
Orders a ranged bot (Hunter, Mage, Warlock) to shoot/cast on target and bring them back to the party:
```lua
/p @all pull
/p @all stay
```

#### 💀 Macro 4: Focus Fire Raid Target Icon (Skull)
Orders all bots to focus down the target marked with the Skull icon:
```lua
/p @all attack rti
```

#### 🚨 Macro 5: Panic Disengage & Flee to Master
Forces all bots to drop combat actions, break target lock, and sprint directly to your feet:
```lua
/p @all flee
/p @all follow
```

---

### Category B: Formations, Buffs & Group Utility (Middle Priority)

#### ✨ Macro 6: Recast Missing Raid/Party Buffs
Orders all bot casters (Priests, Mages, Druids, Paladins) to scan group and apply missing buffs:
```lua
/p @all buff
```

#### 🛡️ Macro 7: Set Defensive Shield Formation
Positions tanks in front and ranges healers/casters safely behind:
```lua
/p @all formation shield
/p @all follow
```

#### 🛑 Macro 8: Hold Position / Stop Following
Freezes bot movement in place for precise positioning:
```lua
/p @all stay
```

#### 🏃 Macro 9: Resume Close Follow
Restores standard follow behavior behind player character:
```lua
/p @all follow
```

#### 📋 Macro 10: Ready Check
Performs a ready check across all bot party members:
```lua
/p @all ready
```

---

### Category C: Looting, Inventory & Vendor Trading (Bottom Section)

#### 💰 Macro 11: Loot All Nearby Corpses
Forces all bots to scan and harvest loot from all dead mob corpses in range:
```lua
/p @all loot
```

#### 🎒 Macro 12: Inspect Selected Bot Bag Inventory
Whispers selected bot to list free bag slots and item contents:
```lua
/w %t c
```

#### 🛠️ Macro 13: Order Bots to Repair & Sell Grays at Vendor
Orders bots to interact with targeted vendor NPC, repair gear, and sell junk items:
```lua
/p @all repair
/p @all s all gray
```

---

## 2. In-Game Bot Action Whispers Reference

Direct party chat triggers and whisper syntax:

| Trigger | Description | Target Scope |
| :--- | :--- | :--- |
| `follow` | Halts current action and follows player closely. | `/p @all follow` or `/w %t follow` |
| `stay` | Holds current ground position. | `/p @all stay` or `/w %t stay` |
| `guard` | Anchors to position and defends area within spell range. | `/p @all guard` or `/w %t guard` |
| `flee` | Drops combat and sprints directly to master. | `/p @all flee` |
| `attack` | All bots charge and attack your selected target. | `/p @all attack` |
| `attack rti` | Bots attack the designated Raid Target Icon. | `/p @all attack rti` |
| `pull` | Ranged bot pulls your selected target to group. | `/p @all pull` |
| `tank attack` | Main tank engages and builds maximum threat. | `/p @all tank attack` |
| `max dps` | All DPS bots pop cooldowns and burst damage. | `/p @all max dps` |
| `ready` | Performs a ready check across all bot party members. | `/p @all ready` |
| `buff` | Forces bots to recast missing raid/party buffs. | `/p @all buff` |
| `loot` | Forces bots to run and loot all nearby dead corpses. | `/p @all loot` |
| `repair` | Bot visits repair vendor and fixes gear. | `/p @all repair` |
| `trainer` | Bot visits class trainer and learns abilities. | `/p @all trainer` |
| `formation <type>` | Sets party formation (`near`, `line`, `shield`, `arrow`, `circle`). | `/p @all formation shield` |

---

<p align="center">
  [⬅ Prev: 12. RandomBot Population](./12_randombot_manager.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 14. Bot Strategies & Tactics ➡](./14_bot_strategies_and_tactics.md)
</p>
