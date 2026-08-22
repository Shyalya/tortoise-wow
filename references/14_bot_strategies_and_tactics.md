# 14. Bot Strategies & Tactics

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

### 📚 Complete 16-Module Reference Library

| Security & Server | World & Characters | Gameplay & Items | Bots & Modules |
| :--- | :--- | :--- | :--- |
| [01. Security & Accounts](./01_security_and_account.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [02. Server & Reloads](./02_server_and_reloads.md) | [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [10. Turtle Custom](./10_turtle_custom_features.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) | **[14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md)** |

---

## 1. Categorized In-Game Lua Macros (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`). Simply **target any bot (`%t`)** in your group and press the respective macro:

### Category A: Essential Raid Tanks & Healers (Top Priority)

#### 🛡️ Macro 1: Configure Target Bot as Protection Main Tank (Warrior)
Equips optimal threat rotation, taunts, defensive shield blocks, potion usage, and food:
```lua
/w %t co +tank,prot,tank assist,avoid aoe,potions
/w %t nc +food
```

#### ✨ Macro 2: Configure Target Bot as Holy Priest (Raid & Tank Healer)
Enables priority flash heal, renew, prayer of healing, mana conservation, and potion usage:
```lua
/w %t co +heal,holy,conserve mana,avoid aoe,potions
/w %t nc +food
```

#### ⚡ Macro 3: Configure Target Bot as Restoration Shaman (Chain Healer & Totems)
Deploys windfury/mana spring totems, chain heal rotation, and mana potions:
```lua
/w %t co +heal,restoration,totems,conserve mana,potions
/w %t nc +food
```

#### 🌿 Macro 4: Configure Target Bot as Restoration Druid (HoT Healer)
Applies Rejuvenation, Regrowth, Healing Touch, innervates, and tranquilizes:
```lua
/w %t co +heal,restoration,conserve mana,potions
/w %t nc +food
```

#### ✝️ Macro 5: Configure Target Bot as Holy Paladin (Main Tank Healer)
Casts Holy Light, Flash of Light, Blessings, and uses mana potions:
```lua
/w %t co +heal,holy,conserve mana,avoid aoe,potions
/w %t nc +food
```

---

### Category B: Primary DPS Classes & Specializations (Middle Priority)

#### 🔥 Macro 6: Configure Target Bot as Fire Mage (Maximum Burst DPS)
Enables Fireball, Pyroblast, Scorch, Combustion, threat throttling, and mana gems/potions:
```lua
/w %t co +dps,fire,threat,avoid aoe,potions
/w %t nc +food
```

#### ❄️ Macro 7: Configure Target Bot as Frost Mage (AoE & Control)
Enables Frostbolt, Blizzard, Cone of Cold, and Ice Barrier:
```lua
/w %t co +dps,frost,threat,avoid aoe,potions
/w %t nc +food
```

#### 🗡️ Macro 8: Configure Target Bot as Combat Rogue (Behind Flank DPS)
Forces bot to position behind boss, stealth opener, Sinister Strike, Eviscerate, and Blade Flurry:
```lua
/w %t co +dps,combat,behind,stealth,potions
/w %t nc +food
```

#### 🏹 Macro 9: Configure Target Bot as Marksmanship Hunter (Ranged DPS & Pet)
Enables Aimed Shot, Multi-Shot, Serpent Sting, Hunter's Mark, and Pet attack coordination:
```lua
/w %t co +dps,marksmanship,threat,pet,potions
/w %t nc +food
```

#### 🔮 Macro 10: Configure Target Bot as Affliction Warlock (DoT & Curses)
Applies Corruption, Curse of Agony, Siphon Life, Shadow Bolt, and Life Tap management:
```lua
/w %t co +dps,affliction,threat,pet,potions
/w %t nc +food
```

#### ⚔️ Macro 11: Configure Target Bot as Fury Warrior (Dual-Wield Melee DPS)
Positions behind boss, casts Bloodthirst, Whirlwind, Heroic Strike, and executes:
```lua
/w %t co +dps,fury,behind,avoid aoe,potions
/w %t nc +food
```

---

### Category C: Strategy Query & Reset Tools (Bottom Section)

#### ❓ Macro 12: Query Target Bot's Active Strategy Buckets
Whispers bot to list all currently enabled combat (`co`), non-combat (`nc`), and dead (`de`) strategies:
```lua
/w %t co ?
/w %t nc ?
```

#### 🔄 Macro 13: Reset Target Bot Strategies to Class Defaults
Clears custom strategy overrides and returns bot to baseline class rotation:
```lua
/w %t reset strats
```

---

## 2. Strategy Bucket Directory

Bots execute dynamic behavior trees divided into four state buckets:

- **`co`**: Combat Strategies (damage rotation, healing priority, tanking)
- **`nc`**: Non-Combat Strategies (eating, looting, gathering, following)
- **`de`**: Dead Strategies (releasing spirit, corpse running)
- **`react`**: Reaction / Threat Triggers

### Modification Syntax
- `co ?` : Displays active and available combat strategies for the bot.
- `co +<strat>` : Adds a combat strategy (e.g. `co +frost`, `co +behind`).
- `co -<strat>` : Removes a combat strategy (e.g. `co -fire`).
- `co ~<strat>` : Toggles a combat strategy on/off.
- `nc +<strat>` : Modifies non-combat strategies (e.g. `nc +food`).
- `reset strats` : Resets strategies to class defaults.

---

<p align="center">
  [⬅ Prev: 13. Bot Whispers & Macro Book](./13_bot_whispers_and_macros.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 15. Dungeon Clear AI Module ➡](./15_dungeon_clear_module.md)
</p>
