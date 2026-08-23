# 04. Character Stats, Reputation & Customization

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

### Category A: Essential Raid Readiness & Reputations (Top Priority)

#### 👑 Macro 1: Max Exalted Alliance Raid Reputations
Instantly sets Argent Dawn (Naxxramas), Cenarion Circle (AQ20/40), Zandalar Tribe (ZG), Brood of Nozdormu (AQ40), and Timbermaw Hold to Exalted (42,999 rep):
```lua
.modify rep 529 42999
.modify rep 609 42999
.modify rep 270 42999
.modify rep 910 42999
.modify rep 576 42999
```

#### 🏹 Macro 2: Max Exalted Horde Raid Reputations
Instantly sets Horde raid reputations (Argent Dawn, Cenarion, Zandalar, Nozdormu, Timbermaw) to Exalted:
```lua
.modify rep 529 42999
.modify rep 609 42999
.modify rep 270 42999
.modify rep 910 42999
.modify rep 576 42999
```

#### ⚔️ Macro 3: Grant PvP Honor Points (Rank 14 Gear Unlock)
Awards 5,000 spendable PvP Honor points for purchasing Grand Marshal / High Warlord gear:
```lua
.modify honor 5000
```

#### 👤 Macro 4: Full Demorph & Baseline Character Reset
Restores character to original race and gender, resetting size and run speeds back to standard 1.0:
```lua
.demorph
.modify scale 1.0
.modify speed 1.0
.modify aspeed 1.0
```

---

### Category B: Everyday Movement & City Reputations (Middle Priority)

#### 🐎 Macro 5: Epic Mount Speed (200% Movement)
Sets movement speed to match 100% epic mount travel for fast traversal:
```lua
.modify speed 2.0
```

#### 🏛️ Macro 6: Exalted Alliance Capital Cities (SW, IF, Darnassus, Gnomes)
Sets all 4 Alliance capital city factions to Exalted standing:
```lua
.modify rep 72 42999
.modify rep 47 42999
.modify rep 69 42999
.modify rep 54 42999
```

#### 🐺 Macro 7: Exalted Horde Capital Cities (Org, Undercity, TB, Darkspear)
Sets all 4 Horde capital city factions to Exalted standing:
```lua
.modify rep 76 42999
.modify rep 68 42999
.modify rep 81 42999
.modify rep 530 42999
```

---

### Category C: Fun, Testing & Sandbox Morphs (Bottom Section)

#### 🦸 Macro 8: Sandbox Super Stats (Testing Solo Raids)
Grants high health pool and attack power for solo sandbox boss testing:
```lua
.modify hp 50000
.modify strength 400
.modify stamina 500
.modify ap 1200
.modify spellpower 800
```

#### 🐉 Macro 9: Onyxia Dragon Morph
Morphs character model into Onyxia for visual testing:
```lua
.modify morph 10182
.modify scale 0.6
```

#### 🔥 Macro 10: Ragnaros Firelord Morph
Morphs character model into Ragnaros the Firelord:
```lua
.modify morph 11121
.modify scale 0.4
```

---

## 2. Major Faction IDs for Exalted Reputation

| Faction ID | Alliance Faction | Faction ID | Horde Faction |
| :---: | :--- | :---: | :--- |
| **529** | **Argent Dawn (Naxxramas Rep)** | **529** | **Argent Dawn (Naxxramas Rep)** |
| **609** | **Cenarion Circle (AQ20/40 Rep)** | **609** | **Cenarion Circle (AQ20/40 Rep)** |
| **270** | **Zandalar Tribe (ZG Rep)** | **270** | **Zandalar Tribe (ZG Rep)** |
| **910** | **Brood of Nozdormu (AQ40 Rep)** | **910** | **Brood of Nozdormu (AQ40 Rep)** |
| **576** | **Timbermaw Hold (Enchants/Rep)** | **576** | **Timbermaw Hold (Enchants/Rep)** |
| **72** | Stormwind City | **76** | Orgrimmar |
| **47** | Ironforge | **68** | Undercity |
| **69** | Darnassus | **81** | Thunder Bluff |
| **54** | Gnomeregan Exiles | **530** | Darkspear Trolls |
| **509** | League of Arathor (AB Rep) | **510** | The Defilers (AB Rep) |
| **730** | Stormpike Guard (AV Rep) | **729** | Frostwolf Clan (AV Rep) |

---

## 3. Character Commands Reference (`.modify`)

Verified against CMaNGOS command table (`src/game/Chat/Chat.cpp:360`):

### 🏆 Essential Progression & Factions (Top Priority)
- `.modify rep <faction_id> <amount>`
  - **Handler**: `HandleModifyRepCommand`
  - **Security**: Developer (3)
  - **What it does**: Sets reputation standing with the target faction (`42999` = Exalted).
  - **Example**: `.modify rep 529 42999` *(Exalted Argent Dawn)*

- `.modify honor <amount>`
  - **Handler**: `HandleModifyHonorCommand`
  - **Security**: Administrator (4)
  - **What it does**: Awards PvP honor points for purchasing PvP gear.
  - **Example**: `.modify honor 5000`

- `.modify speed <multiplier>`
  - **Handler**: `HandleModifyASpeedCommand`
  - **Security**: Developer (3)
  - **What it does**: Sets movement speed (`1.0` = normal running, `1.6` = 60% mount, `2.0` = 100% epic mount).
  - **Example**: `.modify speed 1.0` *(Reset to normal)*

- `.demorph`
  - **Handler**: `HandleDeMorphCommand`
  - **Security**: Moderator (2)
  - **What it does**: Removes active creature morphs and restores natural character race/gender.
  - **Example**: `.demorph`

- `.modify scale <multiplier>`
  - **Handler**: `HandleModifyScaleCommand`
  - **Security**: Developer (3)
  - **What it does**: Sets 3D character scale (`1.0` = standard default player size).
  - **Example**: `.modify scale 1.0`

- `.modify aspeed <multiplier>`
  - **Handler**: `HandleModifyASpeedCommand`
  - **Security**: Developer (3)
  - **What it does**: Sets attack speed animation multiplier (`1.0` = normal).
  - **Example**: `.modify aspeed 1.0`

- `.modify gender [male | female]`
  - **Handler**: `HandleModifyGenderCommand`
  - **Security**: Administrator (4)
  - **What it does**: Switches character gender live.
  - **Example**: `.modify gender female`

- `.modify title <title_id>`
  - **Handler**: `HandleModifyTitleCommand`
  - **Security**: Developer (3)
  - **What it does**: Sets active character title prefix/suffix.
  - **Example**: `.modify title 1`

- `.modify money <copper_amount>`
  - **Handler**: `HandleModifyMoneyCommand`
  - **Security**: Developer (3)
  - **What it does**: Adds gold directly to your character (`10000000` = 1,000 Gold).
  - **Example**: `.modify money 10000000`

---

### 🧪 Sandbox & Testing Commands (Bottom Section)
- `.modify hp <amount>`: Sets current and maximum health pool (e.g. `.modify hp 50000`).
- `.modify mana <amount>`: Sets current and maximum mana pool (e.g. `.modify mana 50000`).
- `.modify rage <amount>`: Sets rage value between 0 and 1000 (e.g. `.modify rage 1000`).
- `.modify energy <amount>`: Sets energy value between 0 and 100 (e.g. `.modify energy 100`).
- `.modify strength <val>`: Direct stat override for Strength (e.g. `.modify strength 500`).
- `.modify agility <val>`: Direct stat override for Agility (e.g. `.modify agility 500`).
- `.modify stamina <val>`: Direct stat override for Stamina (e.g. `.modify stamina 500`).
- `.modify intellect <val>`: Direct stat override for Intellect (e.g. `.modify intellect 500`).
- `.modify spirit <val>`: Direct stat override for Spirit (e.g. `.modify spirit 500`).
- `.modify ap <val>`: Direct stat override for Attack Power (e.g. `.modify ap 1500`).
- `.modify spellpower <val>`: Direct stat override for Spell Power (e.g. `.modify spellpower 1000`).
- `.modify parry <val>`: Direct stat override for Parry rating (e.g. `.modify parry 25`).
- `.modify morph <display_id>`: Morphs character into creature model ID (e.g. `.modify morph 10182`).
- `.modify drunk <0-100>`: Sets intoxication level (e.g. `.modify drunk 100`).

---

<p align="center">
  [⬅ Prev: 03. Teleports & Movement Directory](./03_teleports_and_movement.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 05. Combat, Spells & Skills ➡](./05_combat_spells_and_skills.md)
</p>
