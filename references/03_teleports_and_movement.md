# 03. Teleports & Movement Directory

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

### 📚 Complete 16-Module Reference Library

| Security & Server | World & Characters | Gameplay & Items | Bots & Modules |
| :--- | :--- | :--- | :--- |
| [01. Security & Accounts](./01_security_and_account.md) | **[03. Teleports & Movement](./03_teleports_and_movement.md)** | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [02. Server & Reloads](./02_server_and_reloads.md) | [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [10. Turtle Custom](./10_turtle_custom_features.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |

---

## 1. Categorized In-Game Lua Macros (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`) or run them directly in chat:

### Category A: Raid & Custom Dungeon Fast Group Teleports (Top Priority)

#### 🌋 Macro 1: Group Teleport to Molten Core & Rally
Teleports your character and entire party directly to the Molten Core portal and orders bots to follow:
```lua
.tele group molten
.bot summon *
/p follow
```

#### 🏰 Macro 2: Group Teleport to Scarlet Citadel (Turtle Custom Raid)
Teleports group to the custom 10-man Scarlet Citadel raid entrance:
```lua
.tele group ScarletCitadel
.bot summon *
/p follow
```

#### 🌿 Macro 3: Group Teleport to Crescent Grove (Turtle Custom Dungeon)
Teleports group to the custom level 32–38 Crescent Grove dungeon in Ashenvale:
```lua
.tele group crescentgrove
.bot summon *
/p follow
```

#### ⛏️ Macro 4: Group Teleport to Hateforge Quarry (Turtle Custom Dungeon)
Teleports group to the custom level 50–60 Hateforge Quarry dungeon in Burning Steppes:
```lua
.tele group hateforgeinstance
.bot summon *
/p follow
```

#### 💀 Macro 5: Group Teleport to Naxxramas
Teleports group to Naxxramas entrance in Eastern Plaguelands:
```lua
.tele group naxxramas
.bot summon *
/p follow
```

---

### Category B: Spatial Navigation, Blink & Body Recovery (Middle Priority)

#### ⚡ Macro 6: 20-Yard Forward Blink & 15-Yard Upward Leap
Instantly blinks 20 yards through world geometry in facing direction and lifts 15 yards up:
```lua
.go forward 20
.go up 15
```

#### ⚰️ Macro 7: Teleport to Corpse & Instant Revive
Teleports your ghost directly to your dead corpse coordinates and restores 100% health/mana:
```lua
.go corpse
.revive
```

#### 🎯 Macro 8: Teleport Directly to Selected Target Unit
Teleports your character directly on top of your selected target player or NPC:
```lua
.go target
```

#### 🚀 Macro 9: Master Fast Travel Mode (Flight + Speed + Full Maps)
Unlocks all flight paths, uncovers world map fog, enables levitation/flight, and sets 250% run speed:
```lua
.taxicheat on
.explorecheat on
.hover on
.waterwalk on
.modify speed 2.5
```

---

### Category C: City Teleports & Unstuck (Bottom Section)

#### 🏛️ Macro 10: Teleport to Stormwind City (Alliance Hub)
```lua
.tele stormwind
```

#### 🐺 Macro 11: Teleport to Orgrimmar (Horde Hub)
```lua
.tele orgrimmar
```

#### 🆘 Macro 12: Emergency Hearthstone Unstuck
Casts emergency hearthstone homebind return to rescue stuck characters:
```lua
.unstuck
```

---

## 2. Complete Turtle-WoW Custom Instance Directory

### Custom Turtle-WoW Dungeons (5-Man)
| Destination Token | Instance Name | Level Range | Map ID | Location |
| :--- | :--- | :---: | :---: | :--- |
| `.tele crescentgrove` | Crescent Grove | 32–38 | **805** | Ashenvale Night Elf Barrow Den |
| `.tele hateforgeinstance`| Hateforge Quarry | 50–60 | **806** | Burning Steppes Dark Iron Quarry |
| `.tele gilneascity` | Gilneas City | 42–48 | **807** | Gilnean Gothic Ruins |
| `.tele alahthalas` | Alah'Thalas | 55–60 | **808** | Quel'Thalas High Elf Arcane Sanctum |

### Custom Turtle-WoW Raids (10-Man / 20-Man / 40-Man)
| Destination Token | Raid Name | Size | Map ID | Location |
| :--- | :--- | :---: | :---: | :--- |
| `.tele ScarletCitadel` | Scarlet Citadel | 10-Man | **811** | Tirisfal Glades Scarlet Bastion |
| `.tele Karazhan` | Tower of Karazhan | 10-Man | **812** | Deadwind Pass Medivh's Quarters |
| `.tele sanctumraid` | Emerald Sanctum | 40-Man | **814** | Hyjal Emerald Dream Portal |
| `.tele TimbermawHold` | Timbermaw Hold | 20-Man | **815** | Felwood Furbolg Holy Burrow |

---

## 3. Movement & Teleport Commands Reference (`.go` / `.tele`)

Verified against CMaNGOS core command handlers (`src/game/Chat/Chat.cpp:227` & `Commands.cpp`):

### 🎯 Spatial Movement & Navigation (`.go`)
- `.gps [target]`
  - **Security**: Observer (1)
  - **What it does**: Outputs current Map ID, Area ID, Zone ID, exact 3D X, Y, Z coordinates, and facing orientation angle.
  - **Example**: `.gps`

- `.go xyz <x> <y> <z> [map_id] [orientation]`
  - **Security**: Observer (1)
  - **What it does**: Teleports directly to exact 3D world coordinates. Defaults to current map if omitted (0 = Eastern Kingdoms, 1 = Kalimdor).
  - **Example**: `.go xyz -8949.95 -132.49 83.53 0` *(Stormwind Gates)*

- `.go xy <x> <y> [map_id]`
  - **Security**: Observer (1)
  - **What it does**: Teleports to X, Y coordinates, automatically placing character on terrain ground level.
  - **Example**: `.go xy 1629 -4415 1` *(Orgrimmar Center)*

- `.go forward [distance]`
  - **Security**: Observer (1)
  - **What it does**: Blinks forward through geometry in the direction character is currently facing (default 5yd).
  - **Example**: `.go forward 20`

- `.go up [distance]`
  - **Security**: Observer (1)
  - **What it does**: Teleports straight up along the vertical Z axis.
  - **Example**: `.go up 15`

- `.go target`
  - **Security**: Observer (1)
  - **What it does**: Teleports directly on top of your selected target unit or player.
  - **Example**: `.go target`

- `.go corpse`
  - **Security**: Developer (3)
  - **What it does**: Teleports directly to your ghost's dead body location.
  - **Example**: `.go corpse`

- `.go creature <guid>`
  - **Security**: Observer (1)
  - **What it does**: Teleports to creature spawn location matching database GUID.
  - **Example**: `.go creature 10182`

- `.go object <guid>`
  - **Security**: Observer (1)
  - **What it does**: Teleports to static GameObject matching database GUID.
  - **Example**: `.go object 179501`

- `.go ticket <ticket_id>`
  - **Security**: Moderator (2)
  - **What it does**: Teleports directly to player coordinates who submitted the specified support ticket.
  - **Example**: `.go ticket 12`

---

### 🌐 Predefined Named Teleports (`.tele`)
- `.tele <dest_token>`
  - **Security**: Observer (1)
  - **What it does**: Teleports character to named location token stored in `tw_world.game_tele`.
  - **Example**: `.tele stormwind` or `.tele orgrimmar`

- `.tele group <dest_token>`
  - **Security**: Developer (3)
  - **What it does**: Teleports your character and all party/raid members directly to destination token.
  - **Example**: `.tele group molten` or `.tele group ScarletCitadel`

- `.tele name <player> <dest_token>`
  - **Security**: Developer (3)
  - **What it does**: Teleports specified player directly to destination token.
  - **Example**: `.tele name Sam naxxramas`

- `.tele add <dest_token>`
  - **Security**: Developer (3)
  - **What it does**: Saves your current coordinates as a brand new permanent `.tele` destination token into `tw_world.game_tele`.
  - **Example**: `.tele add raidspot1`

- `.tele del <dest_token>`
  - **Security**: Developer (3)
  - **What it does**: Deletes a `.tele` destination token from the database.
  - **Example**: `.tele del raidspot1`

---

### 👥 Player Summoning & Fast Travel Cheats
- `.appear <player>` / `.goname <player>`
  - **Security**: Moderator (2)
  - **What it does**: Teleports you directly to specified player's current coordinates.
  - **Example**: `.appear Sam`

- `.summon <player>` / `.namego <player>`
  - **Security**: Moderator (2)
  - **What it does**: Summons specified player directly to your current coordinates.
  - **Example**: `.summon Sam`

- `.groupsummon <player>`
  - **Security**: Moderator (2)
  - **What it does**: Summons specified player and all members in their party/raid to your location.
  - **Example**: `.groupsummon Sam`

- `.recall [player]`
  - **Security**: Moderator (2)
  - **What it does**: Returns player to their previous coordinates before their last teleport.
  - **Example**: `.recall`

- `.hover [on | off]`
  - **Security**: Developer (3)
  - **What it does**: Enables character flight / levitation mode.
  - **Example**: `.hover on`

- `.waterwalk [on | off]`
  - **Security**: Developer (3)
  - **What it does**: Enables walking across water surfaces without swimming.
  - **Example**: `.waterwalk on`

- `.taxicheat [on | off]`
  - **Security**: Developer (3)
  - **What it does**: Unlocks all flight master taxi nodes globally across Eastern Kingdoms and Kalimdor.
  - **Example**: `.taxicheat on`

- `.explorecheat [on | off]`
  - **Security**: Developer (3)
  - **What it does**: Uncovers all world map fog-of-war across all zones.
  - **Example**: `.explorecheat on`

- `.unstuck`
  - **Security**: Player (0)
  - **What it does**: Casts emergency hearthstone return to rescue stuck characters.
  - **Example**: `.unstuck`

---

<p align="center">
  [⬅ Prev: 02. Server Operations & Reloads](./02_server_and_reloads.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 04. Character Stats & Visuals ➡](./04_character_stats_and_visuals.md)
</p>
