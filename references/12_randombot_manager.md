# 12. RandomBot Population & Realm AI Manager (.rndbot)

> **Target Core**: Turtle-WoW 1.18.1 (Build 7272) • CMaNGOS + PlayerBots Framework  
> **Source Verification**: `src/modules/PlayerBots/playerbot/RandomPlayerbotMgr.cpp` & `PlayerbotMgr.cpp`  
> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [⚡ Standalone .rndbot Manual (rndbot_commands.md)](../rndbot_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

## Complete 19-Module Reference Library

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

| Selector | Target Meaning | Example Usage |
| :---: | :--- | :--- |
| ***(Target / blank)*** | **Currently selected target** (Click bot in-game) | `.rndbot gear` • `.rndbot gear=best` • `.rndbot prep` |
| **`<botname>`** | Single named bot (Works grouped or solo) | `.rndbot gear Dunpriest epic` • `.rndbot prep Dunpriest` |
| **`*`** | **All bots in active party/raid** (Requires group) | `.rndbot prep *` • `.rndbot summon *` • `.rndbot gear * best` |
| **`%`** | **All random bots server-wide** | `.rndbot init %` • `.rndbot refresh %` • `.rndbot grind %` |
| **`!`** | All bots loaded in server memory *(GM only)* | `.rndbot summon !` • `.rndbot gear ! best` |
| **`guild`** | All bots in your guild | `.rndbot init guild epic` • `.rndbot prep guild` |
| **`Name1,Name2`** | Comma-separated list of bots | `.rndbot prep Dunpriest,Tankman,Magedot` |

---

## 3. Complete `.rndbot` Command Directory

Verified against CMaNGOS random playerbot manager (`src/modules/PlayerBots/playerbot/RandomPlayerbotMgr.cpp` & `PlayerbotMgr.cpp`):

### 📈 Realm Telemetry & Performance

- `.rndbot stats`
  - **Security**: Player (0)
  - **What it does**: Background worker collects and formats full realm population metrics (total bots, level brackets, Alliance/Horde ratio, classes, specs, activity states, and active instances).
  - **Example**: `.rndbot stats`

- `.rndbot diff [player_diff] [empty_diff]`
  - **Security**: Player (0)
  - **What it does**: Displays server tick latency (avg/max diff), Character DB latency, active player/bot sessions, and bot compute load. Passing values dynamically adjusts target diff thresholds.
  - **Example**: `.rndbot diff`

- `.rndbot pid [p i d]`
  - **Security**: Player (0)
  - **What it does**: Displays or dynamically tunes Proportional-Integral-Derivative feedback controller values for automatic bot activity throttling.
  - **Example**: `.rndbot pid 0.05 0.001 0.05`

- `.rndbot update`
  - **Security**: Player (0)
  - **What it does**: Triggers an immediate full AI update tick across all random bots.
  - **Example**: `.rndbot update`

- `.rndbot clean map`
  - **Security**: Player (0)
  - **What it does**: Unloads unreferenced map instances and resets dynamic geometry to free system RAM.
  - **Example**: `.rndbot clean map`

- `.rndbot reset`
  - **Security**: Player (0)
  - **What it does**: Wipes `ai_playerbot_random_bots` database table (requires server restart to rebuild clean cohort).
  - **Example**: `.rndbot reset`

---

### 👥 Party & Bot Creation

- `.rndbot group [size=<n>] [level=<n>]`
  - **Security**: Player (0)
  - **What it does**: Automatically creates and recruits a balanced party (Tank, Heals, DPS) matching master level and faction.
  - **Example**: `.rndbot group`

- `.rndbot create level=<n> class=<class> race=<race> group=<master> autoadd=<0|1>`
  - **Security**: Player (0)
  - **What it does**: Crafts a custom bot with explicit class, race, and level.
  - **Example**: `.rndbot create level=60 class=warrior race=human group=Sam autoadd=1`

- `.rndbot add <botname>` / `.rndbot login <botname>`
  - **Security**: Player (0)
  - **What it does**: Spawns and logs in a specific bot from the pool.
  - **Example**: `.rndbot add Dunpriest`

- `.rndbot remove <botname|*>` / `.rndbot logout <botname|*>`
  - **Security**: Player (0)
  - **What it does**: Despawns and saves bot(s) to database.
  - **Example**: `.rndbot remove *`

- `.rndbot delete <botname>`
  - **Security**: Player (0)
  - **What it does**: Purges character from database permanently.
  - **Example**: `.rndbot delete BotToDelete`

- `.rndbot summon <botname|*>`
  - **Security**: Player (0)
  - **What it does**: Teleports bot(s) directly to master's position.
  - **Example**: `.rndbot summon *`

- `.rndbot list [filter]`
  - **Security**: Player (0)
  - **What it does**: Lists active bots, classes, and online status.
  - **Example**: `.rndbot list`

---

### 🎒 Gearing, Enchants, Spells & Restocking

- `.rndbot gear <target> [best | epic | rare | uncommon | upgrade | sync | partial]`
  - **Security**: Player (0)
  - **What it does**: Equips level-scaled gear by quality ceiling. `best` selects highest stat items in database.
  - **Example**: `.rndbot gear * best`

- `.rndbot enchants <target>`
  - **Security**: Player (0)
  - **What it does**: Enchants every equipped item slot with optimal permanent enchantments.
  - **Example**: `.rndbot enchants *`

- `.rndbot train <target>`
  - **Security**: Player (0)
  - **What it does**: Teaches all class spells and ranks for current level.
  - **Example**: `.rndbot train *`

- `.rndbot prep <target>`
  - **Security**: Player (0)
  - **What it does**: Master one-click restock: executes gear, enchants, train, consumes, food, pots, regs, ammo simultaneously.
  - **Example**: `.rndbot prep *`

- `.rndbot consumes <target>`
  - **Security**: Player (0)
  - **What it does**: Generates level-scaled flasks, elixirs, scrolls, and bandages.
  - **Example**: `.rndbot consumes *`

- `.rndbot potions <target>`
  - **Security**: Player (0)
  - **What it does**: Stocks healing and mana potions.
  - **Example**: `.rndbot potions *`

- `.rndbot food <target>`
  - **Security**: Player (0)
  - **What it does**: Stocks food and water drinks.
  - **Example**: `.rndbot food *`

- `.rndbot regs <target>`
  - **Security**: Player (0)
  - **What it does**: Stocks class reagents (Ankhs, Soul Shards, Flash Powder, Runes, Candles, Berries).
  - **Example**: `.rndbot regs *`

- `.rndbot ammo <target>`
  - **Security**: Player (0)
  - **What it does**: Restocks arrows/bullets for ranged weapons.
  - **Example**: `.rndbot ammo *`

- `.rndbot pet <target>`
  - **Security**: Player (0)
  - **What it does**: Manages pet summoning, feeding, and training.
  - **Example**: `.rndbot pet *`

---

### 🌍 Open-World Activity & AI Behavior

- `.rndbot grind <target | %>`
  - **Security**: Player (0)
  - **What it does**: Directs bot(s) to mob farming locations for their level.
  - **Example**: `.rndbot grind %`

- `.rndbot rpg <target | %>`
  - **Security**: Player (0)
  - **What it does**: Teleports bot(s) to inns, cities, and quest hubs with a 10-minute travel cooldown.
  - **Example**: `.rndbot rpg %`

- `.rndbot refresh <target | %>`
  - **Security**: Player (0)
  - **What it does**: Soft-reboots bot(s), resetting transient action states and restoring health/mana.
  - **Example**: `.rndbot refresh *`

- `.rndbot revive <target | %>`
  - **Security**: Player (0)
  - **What it does**: Instantly revives dead bots.
  - **Example**: `.rndbot revive *`

- `.rndbot init <target | %> [quality]`
  - **Security**: Player (0)
  - **What it does**: Full re-roll: randomizes talents, appearance, gear quality, and spells.
  - **Example**: `.rndbot init * epic`

---

### 🤖 Modes, Spoofing & Advanced Actions

- `.rndbot always <playername>`
  - **Security**: Player (0)
  - **What it does**: Toggles offline AI mode for a character so they stay active even when the human player logs out.
  - **Example**: `.rndbot always Sam`

- `.rndbot self [login]`
  - **Security**: Player (0)
  - **What it does**: Toggles self-bot AI on master player.
  - **Example**: `.rndbot self`

- `.rndbot spoof <botname>` / `.rndbot spoof`
  - **Security**: Player (0)
  - **What it does**: Spoofs sender identity as target bot (or clears spoof).
  - **Example**: `.rndbot spoof Dunpriest`

- `.rndbot p [botname] [message]`
  - **Security**: Player (0)
  - **What it does**: Sends party message as bot (or shows party info).
  - **Example**: `.rndbot p Dunpriest Hello party!`

- `.rndbot g [botname] [message]`
  - **Security**: Player (0)
  - **What it does**: Sends guild message as bot (or shows guild info).
  - **Example**: `.rndbot g Dunpriest Greetings guild!`

- `.rndbot r [botname] [message]`
  - **Security**: Player (0)
  - **What it does**: Sends raid message as bot.
  - **Example**: `.rndbot r Dunpriest Ready for pull`

- `.rndbot rl [botname]`
  - **Security**: Player (0)
  - **What it does**: Transfers raid leadership to bot.
  - **Example**: `.rndbot rl Dunpriest`

- `.rndbot w <botname> <message>`
  - **Security**: Player (0)
  - **What it does**: Direct whisper routing.
  - **Example**: `.rndbot w Dunpriest follow`

- `.rndbot c <botname> <command>`
  - **Security**: Player (0)
  - **What it does**: Forces bot to run chat command.
  - **Example**: `.rndbot c Dunpriest cast 2050`

- `.rndbot do <botname> <action>`
  - **Security**: Player (0)
  - **What it does**: Synchronously executes an internal AI action (e.g. `stats`, `where`, `quests`, `inventory`, `spells`).
  - **Example**: `.rndbot do Dunpriest stats`

- `.rndbot cmd <botname> <command>`
  - **Security**: Player (0)
  - **What it does**: Asynchronously dispatches chat commands to bot queue.
  - **Example**: `.rndbot cmd Dunpriest follow`

- `.rndbot record <botname> enable/disable`
  - **Security**: Player (0)
  - **What it does**: Starts/stops output recording buffer for async commands.
  - **Example**: `.rndbot record Dunpriest enable`

- `.rndbot read <botname>`
  - **Security**: Player (0)
  - **What it does**: Reads recorded message buffer.
  - **Example**: `.rndbot read Dunpriest`

- `.rndbot clear <botname>`
  - **Security**: Player (0)
  - **What it does**: Clears recorded message buffer.
  - **Example**: `.rndbot clear Dunpriest`

- `.rndbot runtest <testnamepart> [count]`
  - **Security**: Player (0)
  - **What it does**: Runs automated bot unit test suite.
  - **Example**: `.rndbot runtest movement 1`

- `.rndbot reload`
  - **Security**: Administrator (4)
  - **What it does**: Reloads `aiplayerbot.conf` into memory live without restarting server.
  - **Example**: `.rndbot reload`

---

[⬅ Prev: 11. Playerbot Control Suite](./11_playerbot_suite.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [⚡ Standalone .rndbot Manual (rndbot_commands.md)](../rndbot_commands.md) • [Next: 13. Bot Whispers & Macro Book ➡](./13_bot_whispers_and_macros.md)
