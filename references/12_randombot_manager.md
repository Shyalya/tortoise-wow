# 12. RandomBot Population & Realm AI Manager (.rndbot)

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [⚡ Standalone .rndbot Manual (rndbot_commands.md)](../rndbot_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

### 📚 Complete 19-Module Reference Library

| Security & Server | World & Characters | Gameplay & Items | Bots & Modules |
| :--- | :--- | :--- | :--- |
| [00. Self & Own Character](./00_self_and_player_commands.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [01. Security & Accounts](./01_security_and_account.md) | [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | **[12. RandomBot Manager](./12_randombot_manager.md)** |
| [02. Server & Reloads](./02_server_and_reloads.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [10. Turtle Custom](./10_turtle_custom_features.md) | [17. Tier Sets & Gear](./17_tier_items_and_endgame_gear.md) | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |
| [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | [18. Class Talent Specs](./18_class_talent_specs.md) | *(Standalone Portals)* | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) |

---

## 1. Categorized In-Game Lua Macros (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`) or run them directly in chat:

### Category A: Instant 5-Man Dungeon Party & Full BiS Setup (Top Priority)

#### 🌟 Macro 1: Complete 1-Click Level 60 Dungeon Group Bootstrap
Instantly assembles a balanced 5-man party (Tank, Heals, 3 DPS), boosts all bots to level 60, equips Best-in-Slot gear, enchants every slot, trains all spells, stocks consumables, and pulls them into follow formation:
```lua
.rndbot group
.rndbot level * 60
.rndbot gear * best
.rndbot enchants *
.rndbot train *
.rndbot prep *
.rndbot summon *
/p follow
```

#### 🛡️ Macro 2: Restock All Party Consumables, Potions & Class Reagents
Restocks food, drinks, healing/mana potions, class reagents (Ankhs, Soul Shards, Flash Powder, Runes), and ranged ammo:
```lua
.rndbot prep *
.rndbot food *
.rndbot potions *
.rndbot regs *
.rndbot ammo *
```

---

### Category B: Population Health, Telemetry & PID Lag Tuning (Middle Priority)

#### 📊 Macro 3: Realm Population Audit & Server Diff Diagnostics
Displays complete online/offline bot statistics, level brackets, race/class breakdown, active combat roles, and server tick latency:
```lua
.rndbot stats
.rndbot diff
.rndbot pid
```

#### 🔄 Macro 4: Mass Population Unstuck, Revive & Force AI Tick
Revives dead bots across the realm, clears stuck pathfinding states, and triggers an immediate population update tick:
```lua
.rndbot refresh
.rndbot revive
.rndbot update
```

#### 🏹 Macro 5: Disperse Bot Population to Level-Appropriate Grinding Zones
Directs roaming bots across Eastern Kingdoms and Kalimdor to level-appropriate mob farming spots:
```lua
.rndbot grind
.rndbot update
```

---

### Category C: Memory Maintenance & Deep Action Queries (Bottom Section)

#### 🧹 Macro 6: Purge Map Geometry & VMap Memory Leaks
Unloads unreferenced map instances and clears orphaned geometry:
```lua
.rndbot clean map
```

#### 🔍 Macro 7: Deep Inspect Bot Stats & Location (Synchronous)
Executes direct internal queries to read the selected target bot's stats, coordinates, and active quests:
```lua
.rndbot do stats
.rndbot do where
.rndbot do quests
```

---

## 2. Target Wildcards & Scope Specifiers

| Selector | Target Meaning | Example |
| :---: | :--- | :--- |
| ***(Target / blank)*** | **Currently selected target** (Click bot in-game) | `.rndbot gear` • `.rndbot gear=best` • `.rndbot prep` |
| **`<botname>`** | Single named bot (Works grouped or solo) | `.rndbot gear Dunpriest epic` • `.rndbot prep Dunpriest` |
| **`*`** | **All bots in active party/raid** (Requires group) | `.rndbot prep *` • `.rndbot summon *` • `.rndbot gear * best` |
| **`!`** | All bots loaded in server memory *(GM only)* | `.rndbot summon !` • `.rndbot gear ! best` |
| **`guild`** | All bots in your guild | `.rndbot init guild epic` • `.rndbot prep guild` |
| **`Name1,Name2`** | Comma-separated list of bots | `.rndbot prep Dunpriest,Tankman` |

---

## 3. Complete `.rndbot` Command Directory

Verified against CMaNGOS random playerbot manager (`src/modules/PlayerBots/playerbot/RandomPlayerbotMgr.cpp:3580-4930`):

### 📈 Realm Telemetry & Performance
- `.rndbot stats`: Background worker collects and formats full realm population metrics (levels, factions, races, classes, roles, activity states, questing progress).
- `.rndbot diff [player_diff] [empty_diff]`: Displays server tick latency (avg/max diff), Character DB latency, active sessions, and bot compute load. Passing values sets target diff thresholds.
- `.rndbot pid [p i d]`: Displays or dynamically tunes Proportional-Integral-Derivative feedback controller values for bot throttling.
- `.rndbot update`: Triggers an immediate full AI update tick across all random bots.
- `.rndbot login debug`: Toggles verbose logging for the bot login queue manager.
- `.rndbot clean map`: Unloads and reloads map geometry and VMaps to free memory.
- `.rndbot reset`: Wipes `ai_playerbot_random_bots` database table (requires server restart to rebuild clean cohort).

---

### 👥 Party & Bot Creation
- `.rndbot group [size=<n>] [level=<n>]`: Automatically creates and recruits a balanced party (Tank, Heals, DPS) matching master level and faction.
- `.rndbot create level=<n> class=<class> race=<race> group=<master> autoadd=<0|1>`: Crafts a custom bot with explicit class, race, and level.
- `.rndbot add <botname>` / `.rndbot login <botname>`: Spawns and logs in a specific bot.
- `.rndbot remove <botname|*>` / `.rndbot logout <botname|*>`: Despawns and saves bot(s).
- `.rndbot delete <botname>`: Purges character from database permanently.
- `.rndbot summon <botname|*>`: Teleports bot(s) directly to master's position.
- `.rndbot list [filter]`: Lists active bots, classes, and status.

---

### 🎒 Gearing, Enchants, Spells & Restocking
- `.rndbot gear <target> [best | epic | rare | uncommon | upgrade | sync | partial]`: Equips level-scaled gear by quality ceiling.
- `.rndbot enchants <target>`: Enchants every equipped item slot with level-appropriate permanent enchantments.
- `.rndbot train <target>`: Teaches all class spells and ranks for current level.
- `.rndbot prep <target>`: Master one-click restock: executes gear, enchants, train, consumes, food, pots, regs, ammo simultaneously.
- `.rndbot consumes <target>`: Generates level-scaled flasks, elixirs, scrolls, and bandages.
- `.rndbot potions <target>`: Stocks healing and mana potions.
- `.rndbot food <target>`: Stocks food and water drinks.
- `.rndbot regs <target>`: Stocks class reagents (Ankhs, Soul Shards, Flash Powder, Runes, Candles, Berries).
- `.rndbot ammo <target>`: Restocks arrows/bullets for ranged weapons.
- `.rndbot pet <target>`: Manages pet summoning, feeding, and training.

---

### 🌍 Open-World Activity & AI Behavior
- `.rndbot grind <target | %>`: Directs bot(s) to mob farming locations for their level.
- `.rndbot rpg <target | %>`: Teleports bot(s) to inns, cities, and quest hubs with a 10-minute travel cooldown.
- `.rndbot teleport <target | %>`: Teleports bot(s) to level-appropriate zones.
- `.rndbot change_strategy <target | %>`: Toggles bot mode between grind/PvP and RPG inn visiting.
- `.rndbot refresh <target | %>`: Soft-reboots bot(s), resetting transient action states, restoring health/mana.
- `.rndbot revive <target | %>`: Instantly revives dead bots.
- `.rndbot init <target | %> [quality]`: Full re-roll: randomizes talents, appearance, gear quality, and spells.

---

### 🤖 Modes, Spoofing & Advanced Actions
- `.rndbot always <playername>`: Toggles offline AI mode for a character.
- `.rndbot self [login]`: Toggles self-bot AI on master player.
- `.rndbot spoof <botname>` / `.rndbot spoof`: Spoofs sender identity as target bot (or clears spoof).
- `.rndbot p [botname] [message]`: Sends party message as bot (or shows party info).
- `.rndbot g [botname] [message]`: Sends guild message as bot (or shows guild info).
- `.rndbot r [botname] [message]`: Sends raid message as bot.
- `.rndbot rl [botname]`: Transfers raid leadership.
- `.rndbot w <botname> <message>`: Direct whisper routing.
- `.rndbot c <botname> <command>`: Forces bot to run chat command.
- `.rndbot do <botname> <action>`: Synchronously executes an internal AI action (e.g. `stats`, `where`, `quests`, `inventory`, `spells`).
- `.rndbot cmd <botname> <command>`: Asynchronously dispatches chat commands to bot queue.
- `.rndbot record <botname> enable/disable`: Starts/stops output recording buffer for async commands.
- `.rndbot read <botname>`: Reads recorded message buffer.
- `.rndbot clear <botname>`: Clears recorded message buffer.
- `.rndbot debug <botname> <command>`: Low-level diagnostics (`position teleport`, `position route`, `travel target`).
- `.rndbot runtest <testnamepart> [count]`: Runs automated bot unit test suite.
- `.rndbot reload`: Reloads `aiplayerbot.conf` into memory *(GM only)*.
- `.rndbot tweak`: Cycles AI tweak counter (0, 1, 2) *(GM only)*.

---

<p align="center">
  [⬅ Prev: 11. Playerbot Control Suite](./11_playerbot_suite.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [⚡ Standalone .rndbot Manual (rndbot_commands.md)](../rndbot_commands.md) • [Next: 13. Bot Whispers & Macro Book ➡](./13_bot_whispers_and_macros.md)
</p>
