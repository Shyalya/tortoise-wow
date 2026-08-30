# 08. NPC Spawning & World Editing

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

### Category A: Essential World Spawning & Deletion (Top Priority)

#### 🐉 Macro 1: Spawn Permanent NPC & Inspect Metadata
Spawns an NPC into the database at current coordinates and outputs creature entry, GUID, and faction ID:
```lua
.npc add 10182
.npc info
```

#### 📦 Macro 2: Spawn Permanent GameObject & Align
Spawns a static GameObject (chest, portal, chair, forge) into `tw_world.gameobject` and targets it:
```lua
.gobject add 179501
.gobject target
```

#### 🗑️ Macro 3: Delete Targeted NPC / GameObject
Permanently deletes selected creature or GameObject from the active map and database:
```lua
.npc delete
.gobject delete
```

#### 🔄 Macro 4: Mass World Respawn (NPCs + Chests + Mining Nodes)
Forces an immediate respawn of all dead creatures and harvested resource nodes within active grid:
```lua
.respawn
```

---

### Category B: NPC Positioning, Scale & Vendor Customization (Middle Priority)

#### 📍 Macro 5: Move Selected NPC to Player Coordinates
Moves selected NPC directly to your feet and prints updated position:
```lua
.npc move
.npc info
```

#### 🛒 Macro 6: Add Item to NPC Vendor Stock
Adds an item entry directly to selected vendor inventory with infinite restock count:
```lua
.npc additem 19019 1 0
```

---

### Category C: NPC Dialogue & Emotes (Bottom Section)

#### 🗣️ Macro 7: Force Selected NPC Dialogue & Emotes
Forces selected NPC to speak in `/say` and perform an animation emote:
```lua
.npc say Welcome to Tortoise-WoW, traveler!
.npc playemote 65
```

---

## 2. Spawning Commands Reference (`.npc` / `.gobject`)

Verified against CMaNGOS command handlers (`src/game/Chat/Chat.cpp`):

### 🧙 Creature Spawning & Editing (`.npc`)
- `.npc add <entry_id>`
  - **Security**: Developer (3)
  - **What it does**: Permanently spawns NPC into `tw_world.creature` at current player coordinates.
  - **Example**: `.npc add 10182`

- `.npc summon <entry_id>`
  - **Security**: Developer (3)
  - **What it does**: Spawns temporary NPC (despawns upon server restart without DB write).
  - **Example**: `.npc summon 10182`

- `.npc delete`
  - **Security**: Developer (3)
  - **What it does**: Permanently deletes selected creature spawn from world and database.
  - **Example**: `.npc delete`

- `.npc info`
  - **Security**: Developer (3)
  - **What it does**: Displays GUID, entry ID, faction ID, display model ID, and active AI script.
  - **Example**: `.npc info`

- `.npc move`
  - **Security**: Developer (3)
  - **What it does**: Moves selected NPC to your current player coordinates.
  - **Example**: `.npc move`

- `.npc scale <multiplier>`
  - **Security**: Developer (3)
  - **What it does**: Modifies creature model scale.
  - **Example**: `.npc scale 1.5`

- `.npc additem <item_id> [count] [incr_time]`
  - **Security**: Developer (3)
  - **What it does**: Adds item to selected vendor NPC stock.
  - **Example**: `.npc additem 19019 1 0`

- `.npc delitem <item_id>`
  - **Security**: Developer (3)
  - **What it does**: Removes item from selected vendor inventory.
  - **Example**: `.npc delitem 19019`

- `.npc say <text>`
  - **Security**: Developer (3)
  - **What it does**: Forces selected NPC to speak message in `/say`.
  - **Example**: `.npc say Welcome travelers!`

- `.npc yell <text>`
  - **Security**: Developer (3)
  - **What it does**: Forces selected NPC to shout message in `/yell`.
  - **Example**: `.npc yell Defend the realm!`

- `.npc follow`
  - **Security**: Developer (3)
  - **What it does**: Forces selected NPC to follow player character.
  - **Example**: `.npc follow`

- `.npc unfollow`
  - **Security**: Developer (3)
  - **What it does**: Releases NPC from following player.
  - **Example**: `.npc unfollow`

- `.npc evade`
  - **Security**: Developer (3)
  - **What it does**: Resets combat state and forces creature to return to home spawn point.
  - **Example**: `.npc evade`

- `.respawn`
  - **Security**: Developer (3)
  - **What it does**: Forces immediate respawn of all nearby NPCs, corpses, and GameObjects.
  - **Example**: `.respawn`

---

### 📦 GameObject Manipulation (`.gobject` / `.wp` / `.path`)
- `.gobject add <entry_id>`
  - **Security**: Developer (3)
  - **What it does**: Spawns permanent GameObject (chest, door, portal, chair) into `tw_world.gameobject`.
  - **Example**: `.gobject add 179501`

- `.gobject tmpadd <entry_id>`
  - **Security**: Developer (3)
  - **What it does**: Spawns temporary GameObject (despawns on restart).
  - **Example**: `.gobject tmpadd 179501`

- `.gobject delete`
  - **Security**: Developer (3)
  - **What it does**: Permanently deletes selected GameObject from world and database.
  - **Example**: `.gobject delete`

- `.gobject near [radius]`
  - **Security**: Developer (3)
  - **What it does**: Lists all nearby GameObjects within radius with GUIDs and distances.
  - **Example**: `.gobject near 15`

- `.gobject target [guid]`
  - **Security**: Developer (3)
  - **What it does**: Selects nearest or specified GameObject.
  - **Example**: `.gobject target`

- `.gobject move`
  - **Security**: Developer (3)
  - **What it does**: Moves targeted GameObject to your current position.
  - **Example**: `.gobject move`

- `.gobject turn <angle>`
  - **Security**: Developer (3)
  - **What it does**: Rotates targeted GameObject by specified angle.
  - **Example**: `.gobject turn 90`

- `.gobject scale <val>`
  - **Security**: Developer (3)
  - **What it does**: Sets the 3D scale multiplier on the targeted GameObject.
  - **Example**: `.gobject scale 1.5`

- `.gobject set gostate <0|1|2>` / `.gobject set respawntime <secs>`
  - **Security**: Developer (3)
  - **What it does**: Modifies the GameObject state (door open/closed/active) or respawn delay.
  - **Example**: `.gobject set gostate 1`

- `.path setup <path_id>` / `.path add` / `.path start`
  - **Security**: Developer (3)
  - **What it does**: Records and initiates creature waypoint patrol paths live.
  - **Example**: `.path setup 1001`

- `.wp add <point_id>` / `.wp show on` / `.wp export <creature_guid>`
  - **Security**: Administrator (4)
  - **What it does**: Manages waypoint grids and exports path SQL to disk.
  - **Example**: `.wp show on`

---

<p align="center">
  [⬅ Prev: 07. Items, Economy & Mail](./07_items_economy_and_mail.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 09. Moderation, Tickets & Anticheat ➡](./09_moderation_tickets_and_anticheat.md)
</p>
