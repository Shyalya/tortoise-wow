# 14. Bot Strategies & Tactics

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

## 2. Strategy Bucket & Stance Directory

Bots execute dynamic behavior trees divided into four state buckets:

- **`co`**: Combat Strategies (damage rotation, healing priority, tanking, threat)
- **`nc`**: Non-Combat Strategies (eating, looting, gathering, following, RPG)
- **`de`**: Dead Strategies (releasing spirit, corpse running)
- **`react`**: Reaction / Threat / Interrupt Triggers

### Modification Syntax
- `co ?` : Displays active and available combat strategies for the bot.
- `co +<strat>` : Adds a combat strategy (e.g. `co +frost`, `co +behind`, `co +avoid aoe`).
- `co -<strat>` : Removes a combat strategy (e.g. `co -fire`).
- `co ~<strat>` : Toggles a combat strategy on/off.
- `nc +<strat>` : Modifies non-combat strategies (e.g. `nc +food`, `nc +loot`, `nc +gather`).
- `reset strats` : Resets all strategies to baseline class defaults.

---

## 3. Comprehensive Strategy Reference Table

Verified against `src/modules/PlayerBots/playerbot/strategy/StrategyContext.h` and class AI contexts:

### 🛡️ Universal Combat & Positioning Strategies (`co +...`)
| Strategy | Bucket | Effect / Behavior |
| :--- | :--- | :--- |
| `tank assist` | `co` | Assists the group's main tank, attacking their active target. |
| `dps assist` | `co` | Assists the group's highest priority DPS or group leader. |
| `dps aoe` | `co` | Activates multi-target area-of-effect spells and cleaves. |
| `avoid aoe` | `co` | Detects ground hazard zones and immediately runs out of damage circles. |
| `avoid mobs` | `co` / `nc` | Paths around roaming hostile mob aggro radii. |
| `behind` | `co` | Navigates behind target mob to avoid parry/block (Rogues, Melee DPS). |
| `close` / `melee` | `co` | Closes distance into melee range (<5 yards). |
| `ranged` | `co` | Maintains safe distance at maximum spell/ranged weapon range. |
| `kite` | `co` | Stutters steps and casts while maintaining separation from melee attacker. |
| `flee` | `co` | Breaks target lock and sprints to group master. |
| `threat` | `co` | Throttles DPS output if bot approaches 90%+ threat on mob. |
| `tell target` | `co` | Whispers target name or spell alerts in group chat. |
| `pvp` | `co` | Prioritizes hostile enemy player characters over world NPCs. |
| `mark rti` | `co` | Automatically assigns Raid Target Icons (Skull, Cross) to priority kill targets. |
| `focus heal targets` | `co` | Restricts healing to specified tank or low-HP party members. |
| `focus rti targets` | `co` | Restricts DPS spells exclusively to targets marked with Raid Icons. |
| `heal interrupt` | `co` | Prioritizes casting interrupt spells when enemy begins casting a heal. |
| `preheal` | `co` | Begins pre-casting heals on tank anticipating incoming spike damage. |
| `potions` | `co` | Automatically drinks healing, mana, and protection potions in combat. |
| `conserve mana` | `co` | Downranks spells or wand-casts when mana drops below 30%. |

---

### 📦 Non-Combat Strategies (`nc +...`)
| Strategy | Bucket | Effect / Behavior |
| :--- | :--- | :--- |
| `food` | `nc` | Automatically eats food and drinks water when health/mana is depleted. |
| `consumables` | `nc` | Uses flasks, elixirs, sharpening stones, and scrolls before pulls. |
| `loot` | `nc` | Scans and loots all dead mob corpses in range. |
| `gather` | `nc` | Harvests nearby Mining nodes, Herbalism herbs, and chests. |
| `roll` / `delayed roll` | `nc` | Automatically rolls (Need / Greed / Pass) on dungeon loot drops. |
| `quest` | `nc` | Automatically turns in and progresses shared party quests. |
| `accept all quests` | `nc` | Accepts every available quest offered by nearby quest givers. |
| `fish` / `tfish` | `nc` | Equips fishing rod and casts into nearby water pools. |
| `mount` | `nc` | Automatically mounts when traveling long distances outdoors. |
| `wbuff` / `wbuff travel`| `nc` | Gathers realm world buffs (Onyxia, Hakkar, DM Tribute, Songflower). |
| `rpg` | `nc` | Visits town vendors, repairs gear, trains spells, and visits inns. |
| `travel` | `nc` | Walks or rides along roads toward dungeon/quest destinations. |
| `silent` | `nc` | Disables bot chat banter and voice spam in group. |

---

### ⚔️ Class Combat Specializations (`co +...`)
| Class | Role Strategies | Key Spells / Rotation Triggers |
| :--- | :--- | :--- |
| **Warrior** | `prot`, `tank`, `arms`, `fury` | Shield Slam, Devastate, Sunder Armor, Bloodthirst, Mortal Strike, Whirlwind |
| **Paladin** | `holy`, `heal`, `prot`, `tank`, `retribution` | Holy Light, Flash of Light, Consecration, Seal of Righteousness/Command |
| **Hunter** | `beastmastery`, `marksmanship`, `survival`, `pet` | Aimed Shot, Multi-Shot, Serpent Sting, Feign Death, Pet Aggro |
| **Rogue** | `assassination`, `combat`, `subtlety`, `stealth` | Sinister Strike, Backstab, Slice and Dice, Eviscerate, Blade Flurry |
| **Priest** | `holy`, `heal`, `discipline`, `shadow` | Flash Heal, Greater Heal, Renew, Mind Blast, Shadow Word: Pain, Mind Flay |
| **Shaman** | `restoration`, `heal`, `enhancement`, `elemental`, `totems` | Chain Heal, Healing Wave, Windfury Totem, Mana Spring, Lightning Bolt, Earth Shock |
| **Mage** | `arcane`, `fire`, `frost`, `aoe` | Fireball, Pyroblast, Frostbolt, Blizzard, Arcane Missiles, Evocation |
| **Warlock** | `affliction`, `demonology`, `destruction`, `pet` | Shadow Bolt, Corruption, Immolate, Drain Life, Life Tap, Curse of Shadows/Elements |
| **Druid** | `balance`, `feral`, `cat`, `bear`, `tank`, `restoration`, `heal` | Healing Touch, Rejuvenation, Regrowth, Maul, Swipe, Shred, Ferocious Bite, Moonfire |

---

<p align="center">
  [⬅ Prev: 13. Bot Whispers & Macro Book](./13_bot_whispers_and_macros.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 15. Dungeon Clear AI Module ➡](./15_dungeon_clear_module.md)
</p>
