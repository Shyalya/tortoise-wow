# 11. Playerbot Control Suite

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

### 📚 Complete 16-Module Reference Library

| Security & Server | World & Characters | Gameplay & Items | Bots & Modules |
| :--- | :--- | :--- | :--- |
| [01. Security & Accounts](./01_security_and_account.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | **[11. Playerbot Suite](./11_playerbot_suite.md)** |
| [02. Server & Reloads](./02_server_and_reloads.md) | [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [10. Turtle Custom](./10_turtle_custom_features.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |

---

## 1. Categorized In-Game Lua Macros (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`) or run them directly in chat:

### Category A: Level-Appropriate Random Gear, Enchants & Consumables (Top Priority)

#### 🎲 Macro 1: Randomize & Equip Level-Appropriate Epic Gear + Full Enchants

Automatically equips full **Epic** quality gear scaled to target/own level, applies permanent enchantments, and learns all class spells:

```lua
.bot gear * epic
.bot enchants *
.bot train *
.bot prep *
```

#### 💎 Macro 2: Equip Best-in-Slot Gear (Highest Item Level for Current Level)

Equips the highest stat BiS gear available in the database for current level:

```lua
.bot gear * best
.bot enchants *
```

#### 🧪 Macro 3: Restock Level-Appropriate Consumables, Potions & Reagents

Generates flasks, elixirs, bandages, healing/mana potions, class reagents (Ankhs, Soul Shards, Flash Powder), and ammo scaled to current level:

```lua
.bot consumes *
.bot potions *
.bot food *
.bot regs *
.bot ammo *
```

#### 🌟 Macro 4: Complete Character/Bot Re-Roll & Initialization (Level 60 Epics)

Completely re-rolls talents, equips randomized Epic armor/weapons, enchants every slot, stocks consumables, and trains all spells:

```lua
.bot level * 60
.bot init * epic
.bot enchants *
.bot prep *
/p follow
```

---

### Category B: Bot Party Spawning, Summoning & Management (Middle Priority)

#### 🏃 Macro 5: Instant Party Bot Summon & Follow

Teleports all party bots directly to your feet and locks follow formation:

```lua
.bot summon *
/p follow
```

#### 🛡️ Macro 6: Create Tank Warrior Bot (Level 60 Human)

Spawns a brand new tank-specced Warrior bot and readies him for dungeon tanking:

```lua
.bot create tank warrior human MainTank 60
.bot init MainTank epic
.bot enchants MainTank
```

#### ➕ Macro 7: Login & Invite Alt Character as Bot

Logs in an alternate character from your account and invites them directly to your party:

```lua
.bot add AltPriest
.bot summon AltPriest
/p follow
```

#### 🚪 Macro 8: Logout & Save All Party Bots

Dismisses and logs out all party bots cleanly to the database:

```lua
.bot remove *
```

---

### Category C: Configuration & Maintenance (Bottom Section)

#### 📌 Macro 9: Mark Bot to Remain Always Online

Flags bot to stay permanently logged in across server restarts and area changes:

```lua
.bot always MainTank
```

#### ⚙️ Macro 10: Reload Playerbot Configuration (`aiplayerbot.conf`)

Live reloads AI bot combat settings and heuristics without dropping world connections:

```lua
.bot reload
```

---

## 2. Complete `.bot` Command Directory

Verified against CMaNGOS playerbot engine handlers (`src/modules/PlayerBots/playerbot/PlayerbotMgr.cpp:247-3025`):

### Target Scope Specifiers

- `<name>`: Targets a single specific character/bot by name (e.g. `.bot gear MainTank epic`).
- `*`: Targets **all bots in your active party/raid** (e.g. `.bot summon *`).
- `guild`: Targets **all bots in your guild** (e.g. `.bot init guild epic`).
- `!`: Targets **all playerbots server-wide** (Game Master only).
- Target Unit: Select any character or bot portrait in-game and run `.bot gear epic` to execute on the selected unit.

---

### 🎒 Level-Scaled Equipment & Quality Selectors (`.bot gear`)

Verified in `PlayerbotMgr.cpp:2868`:

| Quality Flag | Command Example | What it Generates |
| :--- | :--- | :--- |
| **`epic`** / **`purple`** | `.bot gear * epic` | Generates full randomized **Epic** gear scaled to character's current level. |
| **`rare`** / **`blue`** | `.bot gear * rare` | Generates full randomized **Rare / Superior** gear scaled to character level. |
| **`uncommon`** / **`green`** | `.bot gear * green` | Generates full randomized **Uncommon / Green** gear scaled to level. |
| **`best`** | `.bot gear * best` | Equips the absolute highest stat Best-in-Slot items for current level. |
| **`upgrade`** | `.bot gear * upgrade` | Replaces only outdated gear slots with upgrades matching master/current level. |
| **`partial`** | `.bot gear * partial` | Upgrades a randomized selection of weak item slots. |
| *(default)* | `.bot gear *` | Equips standard level-appropriate randomized gear. |

---

### ✨ Permanent Gear Enchants (`.bot enchants`)

Verified in `PlayerbotMgr.cpp:3013`:

- `.bot enchants <target>` / `.bot enchants *`
  - **What it does**: Scans every equipped item slot and applies the highest-tier level-appropriate permanent enchantment (e.g. Crusader, Spell Power, +Stats, Defense).
  - **Example**: `.bot enchants *`

---

### 🧪 Level-Scaled Consumables & Reagents

Verified in `PlayerbotMgr.cpp:2930-2968`:

- `.bot consumes <target>` / `.bot consumes *`
  - **What it does**: Generates flasks, elixirs, bandages, and scrolls scaled to character level.
  - **Example**: `.bot consumes *`

- `.bot potions <target>` / `.bot potions *`
  - **What it does**: Stocks healing and mana potions matching character level.
  - **Example**: `.bot potions *`

- `.bot food <target>` / `.bot food *`
  - **What it does**: Stocks level-appropriate food and water drinks.
  - **Example**: `.bot food *`

- `.bot regs <target>` / `.bot regs *`
  - **What it does**: Stocks class reagents (Soul Shards, Ankhs, Flash Powder, Feathers, Candles, Runes).
  - **Example**: `.bot regs *`

- `.bot ammo <target>` / `.bot ammo *`
  - **What it does**: Stocks arrows/bullets for Hunters, and rogue poisons matching character level.
  - **Example**: `.bot ammo *`

- `.bot prep <target>` / `.bot prep *`
  - **What it does**: Master restock — refills all consumables, potions, reagents, and food in one command.
  - **Example**: `.bot prep *`

---

### 🔄 Character Initialization & Re-Roll (`.bot init`)

Verified in `PlayerbotMgr.cpp:2970`:

- `.bot init <target> [white | green | blue | epic | legendary]`
  - **What it does**: Completely wipes and re-rolls talent build, learns all class abilities, equips full armor/weapon set of chosen quality, and enchants all gear.
  - **Example**: `.bot init * epic`

---

<p align="center">
  [⬅ Prev: 10. Turtle Custom Features](./10_turtle_custom_features.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 12. RandomBot Manager ➡](./12_randombot_manager.md)
</p>
