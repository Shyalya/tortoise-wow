# Tortoise-WoW Master Game Master & Playerbot Command Portal

> **Target Platform**: Turtle-WoW 1.18.1 (Build 7272)
> **Architecture**: CMaNGOS + PlayerBots (ike3 / r-o-sh) + Dynamic Module System
> **Coverage**: 100% of all 539 core commands documented & verified live.

---

## 📚 Modular Enterprise Reference Sitemaps

To provide deep technical parameters and copy-paste in-game macro templates for every domain, the command manual is organized into 16 specialized reference modules:

| # | Reference Module | Scope & Core Topics | Ready Macros |
| :-: | :--- | :--- | :-: |
| **00** | [**Self & Own Character Commands**](references/00_self_and_player_commands.md) | **100% dedicated to your own character** (level 60 boost, level-scaled random gear/enchants/consumes, bank/mail, god mode, flight). | [Macro Card](references/00_self_and_player_commands.md#1-categorized-in-game-lua-macros-top-priority) |
| **01** | [**Security Ranks, Accounts & Permissions**](references/01_security_and_account.md) | Security Levels 0–6, account creation, password/IP locks, GM permissions. | [Macro Card](references/01_security_and_account.md#1-categorized-in-game-lua-macros-top-priority) |
| **02** | [**Server Operations, Lifecycle & Live Reloads**](references/02_server_and_reloads.md) | `.server` restarts/shutdowns, 106+ `.reload` tables (configs, items, spells, strings). | [Macro Card](references/02_server_and_reloads.md#3-ready-to-use-in-game-macros) |
| **03** | [**Movement, Teleportation & Turtle-WoW Map Directory**](references/03_teleports_and_movement.md) | `.tele` tokens (647+ destinations), custom Turtle dungeons & raids, `.go xyz`, `.hover`. | [Macro Card](references/03_teleports_and_movement.md#3-ready-to-use-in-game-macros) |
| **04** | [**Character Stats, Visuals & Modifications**](references/04_character_stats_and_visuals.md) | `.modify` suite (HP, Mana, Stats, Honor Currency, Morphs, Scales, Titles, Speed). | [Macro Card](references/04_character_stats_and_visuals.md#2-ready-to-use-in-game-macros) |
| **05** | [**Combat, Spells, Talents & Weapon Skills**](references/05_combat_spells_and_skills.md) | `.god`, `.revive`, `.die`, `.learn all_myspells`, `.maxskill`, `.cooldown`, `.aura`. | [Macro Card](references/05_combat_spells_and_skills.md#3-ready-to-use-in-game-macros) |
| **06** | [**Quests, Instances, Events & AQ War Effort**](references/06_quests_instances_and_events.md) | `.quest complete`, `.pet`, `.instance groupunbind`, `.bg`, `.wareffort`, holidays. | [Macro Card](references/06_quests_instances_and_events.md#5-ready-to-use-in-game-macros) |
| **07** | [**Items, Equipment, Economy & Remote Mail**](references/07_items_economy_and_mail.md) | `.additem`, `.additemset`, `.bank`, `.mailbox`, `.modify money`, `.send items`. | [Macro Card](references/07_items_economy_and_mail.md#4-ready-to-use-in-game-macros) |
| **08** | [**NPC Spawning, GameObjects & World Editing**](references/08_spawning_and_world_editing.md) | NPC spawning & editing (`.npc`), GameObjects (`.gobject`), `.wchange`, `.settime`. | [Macro Card](references/08_spawning_and_world_editing.md#4-ready-to-use-in-game-macros) |
| **09** | [**Moderation, Bans, Tickets & Anticheat**](references/09_moderation_tickets_and_anticheat.md) | `.kick`, `.freeze`, `.mute`, `.ban`, `.ticket`, `.pdump`, Warden telemetry. | [Macro Card](references/09_moderation_tickets_and_anticheat.md#6-ready-to-use-in-game-macros) |
| **10** | [**Turtle-WoW Custom Features & Vanity Systems**](references/10_turtle_custom_features.md) | Hardcore modes, Trainee challenges, Cartographer reveal, Custom Guild housing. | [Macro Card](references/10_turtle_custom_features.md#2-ready-to-use-in-game-macros) |
| **11** | [**Playerbot Control Suite (.bot)**](references/11_playerbot_suite.md) | Complete `.bot` verbs (`add`, `remove`, `summon`, `gear`, `train`, `p`, `create`). | [Macro Card](references/11_playerbot_suite.md#3-ready-to-use-in-game-macros) |
| **12** | [**RandomBot Population Manager (.rndbot)**](references/12_randombot_manager.md) | Autonomous bot pool verbs (`stats`, `init`, `upgrade`, `grind`, `rpg`, `clean map`). | [Macro Card](references/12_randombot_manager.md#3-ready-to-use-in-game-macros) |
| **13** | [**Bot In-Game Whispers, Party Controls & Macro Book**](references/13_bot_whispers_and_macros.md) | **1-Click ready in-game macro book** for Attack, Tank Pull, Follow, Loot, and Trade. | [Macro Book](references/13_bot_whispers_and_macros.md#3-the-ultimate-in-game-macro-book) |
| **14** | [**Bot Strategies, Tactical AI & Raid Tactics**](references/14_bot_strategies_and_tactics.md) | Strategy buckets (`co`, `nc`, `de`, `react`), **target-based (`%t`) class/spec macros**. | [Macro Card](references/14_bot_strategies_and_tactics.md#2-universal-target-based-class-spec-macros) |
| **15** | [**Mod-Dungeon-Clear AI & Automated Route Engine**](references/15_dungeon_clear_module.md) | Autonomous dungeon clearing behavior trees, pull governors, and door policies. | [Macro Card](references/15_dungeon_clear_module.md#3-ready-to-use-in-game-macros) |
| **16** | [**Developer Diagnostics, Lookups & Engine Debugging**](references/16_developer_and_diagnostics.md) | Universal `.lookup` engine, `.list` inspectors, `.debug` profilers, field modifiers. | [Macro Card](references/16_developer_and_diagnostics.md#1-categorized-in-game-lua-macros-top-priority) |
| **17** | [**Tier Sets & Endgame Equipment Directory**](references/17_tier_items_and_endgame_gear.md) | **Complete class item IDs** (Tier 1–3, legendaries, BiS weapons, trinkets, rings, shields). | [Macro Card](references/17_tier_items_and_endgame_gear.md#1-1-click-bis-class-loadouts-top-priority) |
| **18** | [**Class Talent Specs & Raid Meta Builds**](references/18_class_talent_specs.md) | **18+ high-performance specs** (DW Fury/Prot 0/31/20, Deep Prot, Fury DPS, Ignite Mage, DS/Ruin). | [Spec Directory](references/18_class_talent_specs.md#1-warrior-talent-specs) |

---

## ⚡ Master Quick-Lookup Macro Cheat Sheet

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`):

### 🌟 1. Complete Player Power-Boost Suite (Level 60 + Spells + Skills + Travel)

```text
/script DEFAULT_CHAT_FRAME:AddMessage("Executing Complete Character Power-Boost...", 0, 1, 0);
.levelup 60
.learn all_myspells
.learn all_trainer
.learn all_recipes
.maxskill
.modify money 10000000
.taxicheat on
.explorecheat on
.repairitems
.replenish
```

### 🤖 2. Complete Bot Party Bootstrap (Level 60 + BiS Gear + Enchants + Spells + Prep)

```text
/script DEFAULT_CHAT_FRAME:AddMessage("Bootstrapping Party Bots (Level 60, Gear, Enchants, Spells, Pots, Reagents)...", 0, 1, 0);
.bot add *
.bot summon *
.bot level * 60
.bot gear *
.bot enchants *
.bot train *
.bot prep *
.bot food *
.bot potions *
.bot regs *
.bot ammo *
.bot p follow
```

### ⚔️ 3. Main Tank Engage & DPS Attack

```text
/script DEFAULT_CHAT_FRAME:AddMessage("Ordering Main Tank & Party to Engage Target...", 1, 0, 0);
/p @all tank attack
/p @all attack
/p @all max dps
```

### 🛑 4. Emergency Disengage & Flee to Master

```text
/script DEFAULT_CHAT_FRAME:AddMessage("EMERGENCY RETREAT - Regrouping on Player!", 1, 0.2, 0.2);
/p @all flee
/p @all follow
/p @all stay
.combatstop
```

### 📦 5. Post-Combat Loot & Consumables Restock

```text
/script DEFAULT_CHAT_FRAME:AddMessage("Harvesting Corpses & Restocking Consumables...", 0.5, 1, 0.5);
/p @all loot
/p @all food
.bot prep *
```

### 🗺️ 6. Enable Autonomous Dungeon Clear Solver

```text
/script DEFAULT_CHAT_FRAME:AddMessage("Enabling Autonomous Dungeon Clear Engine...", 0, 1, 1);
/p @all co +dungeon clear,avoid aoe,roll,loot,potions
/p @all nc +gather,food
/p @all formation near
```

### 🛡️ 7. Panic Combat Recovery & Full Reset

```text
/script DEFAULT_CHAT_FRAME:AddMessage("PANIC RESET: Godmode On, Combat Dropped, Cooldowns Cleared...", 1, 0.2, 0.2);
.god on
.combatstop
.cooldown
.replenish
```

---

## 🔧 Bot Leveling & Population Optimization

- **Alt Autologin**: With `AiPlayerbot.BotAutologin = 1` enabled in `run/aiplayerbot.conf`, all alternate characters on your account automatically log in as bots upon joining the realm.
- **Level Sync**: With `AiPlayerbot.SyncAltLevelToMaster = 1`, all your alt bots level up automatically to match your character.
- **Kill & Quest XP Rates**: Configured at **2x rate** in `run/mangosd.conf` (`Rate.XP.Kill = 2`, `Rate.XP.Quest = 2`, `Rate.XP.Explore = 2`).
- **Autonomous Density**: Configured at `AiPlayerbot.botActiveAlone = 50` to maintain vibrant open-world and dungeon life.

```text
.bank
.mailbox
.repairitems
```

#### 📦 8. Post-Combat Loot & Consumables Restock

Orders bots to loot surrounding corpses, sit to eat/drink, and restocks supplies:

```text
/p loot
/p food
.bot prep *
```

#### 🗺️ 9. Enable Autonomous Dungeon Clear Solver

Engages the `.dc` solver and configures dungeon combat tactics:

```text
.dc on
/p co +dungeon clear,avoid aoe,roll,loot,potions
/p nc +gather,food
/p formation near
```

#### 🛡️ 10. Panic Combat Recovery & Full Reset

Activates godmode, drops combat aggro, clears cooldowns, and restores 100% HP/mana:

```text
.god on
.combatstop
.cooldown
.replenish
```

---

## 🔧 Bot Leveling & Population Optimization

- **Alt Autologin**: With `AiPlayerbot.BotAutologin = 1` enabled in `run/aiplayerbot.conf`, all alternate characters on your account automatically log in as bots upon joining the realm.
- **Level Sync**: With `AiPlayerbot.SyncAltLevelToMaster = 1`, all your alt bots level up automatically to match your character.
- **Kill & Quest XP Rates**: Configured at **2x rate** in `run/mangosd.conf` (`Rate.XP.Kill = 2`, `Rate.XP.Quest = 2`, `Rate.XP.Explore = 2`).
- **Autonomous Density**: Configured at `AiPlayerbot.botActiveAlone = 50` to maintain vibrant open-world and dungeon life.

---

## 🧭 Complete 16-Module Navigation Matrix

| # | Reference Module | Domain & Key Topics | Direct Link |
| :-: | :--- | :--- | :-: |
| **01** | [**01. Security Ranks & Accounts**](./references/01_security_and_account.md) | Permissions (0–6), Accounts, IP Locks, 2FA, GM Mode | [Open 01_security_and_account.md](./references/01_security_and_account.md) |
| **02** | [**02. Server Operations & Reloads**](./references/02_server_and_reloads.md) | Server Operations, 106+ .reload Tables, Dynamic Modules | [Open 02_server_and_reloads.md](./references/02_server_and_reloads.md) |
| **03** | [**03. Teleports & Movement Directory](./references/03_teleports_and_movement.md)** | 647+ .tele Tokens, Turtle Custom Dungeons & Raids | [Open 03_teleports_and_movement.md](./references/03_teleports_and_movement.md) |
| **04** | [**04. Character Stats & Visuals](./references/04_character_stats_and_visuals.md)** | Reputations, Honor, Speeds, Scales, Demorph, Reset | [Open 04_character_stats_and_visuals.md](./references/04_character_stats_and_visuals.md) |
| **05** | [**05. Combat, Spells & Skills](./references/05_combat_spells_and_skills.md)** | .god, .revive, .learn all_myspells, .maxskill | [Open 05_combat_spells_and_skills.md](./references/05_combat_spells_and_skills.md) |
| **06** | [**06. Quests, Instances & Events](./references/06_quests_instances_and_events.md)** | .quest, .instance groupunbind, Events, Pets | [Open 06_quests_instances_and_events.md](./references/06_quests_instances_and_events.md) |
| **07** | [**07. Items, Economy & Mail](./references/07_items_economy_and_mail.md)** | Tier 1/2/3 Sets, Ilvl/Quality Gearing, Bags, Bank | [Open 07_items_economy_and_mail.md](./references/07_items_economy_and_mail.md) |
| **08** | [**08. NPC Spawning & World Editing](./references/08_spawning_and_world_editing.md)** | NPC Spawning (.npc), GameObjects (.gobject), Weather | [Open 08_spawning_and_world_editing.md](./references/08_spawning_and_world_editing.md) |
| **09** | [**09. Moderation & Anticheat](./references/09_moderation_tickets_and_anticheat.md)** | .kick, .ban, .mute, .ticket, Warden Telemetry | [Open 09_moderation_tickets_and_anticheat.md](./references/09_moderation_tickets_and_anticheat.md) |
| **10** | [**10. Turtle Custom Features](./references/10_turtle_custom_features.md)** | Hardcore Modes, Trainee, Cartographer, Guild Houses | [Open 10_turtle_custom_features.md](./references/10_turtle_custom_features.md) |
| **11** | [**11. Playerbot Control Suite](./references/11_playerbot_suite.md)** | .bot add/remove/summon/gear/train/prep Suite | [Open 11_playerbot_suite.md](./references/11_playerbot_suite.md) |
| **12** | [**12. RandomBot Population](./references/12_randombot_manager.md)** | Autonomous Bot Pool (.rndbot), PID, Grind, RPG | [Open 12_randombot_manager.md](./references/12_randombot_manager.md) |
| **13** | [**13. Bot Whispers & Macro Book](./references/13_bot_whispers_and_macros.md)** | 32 Ready In-Game Party & Whisper Macros | [Open 13_bot_whispers_and_macros.md](./references/13_bot_whispers_and_macros.md) |
| **14** | [**14. Bot Strategies & Tactics](./references/14_bot_strategies_and_tactics.md)** | Target-Based (%t) Macros for All 28 Class/Specs | [Open 14_bot_strategies_and_tactics.md](./references/14_bot_strategies_and_tactics.md) |
| **15** | [**15. Dungeon Clear AI Module](./references/15_dungeon_clear_module.md)** | Autonomous Route Solver, Pull Governor, Door Policy | [Open 15_dungeon_clear_module.md](./references/15_dungeon_clear_module.md) |
| **16** | [**16. Diagnostics & Lookups](./references/16_developer_and_diagnostics.md)** | Universal .lookup, .list Inspectors, .debug Tools | [Open 16_developer_and_diagnostics.md](./references/16_developer_and_diagnostics.md) |

<p align="center">
  [🏠 Master Portal (gm_commands.md)](./gm_commands.md) • [📖 Project Readme](./README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./references/13_bot_whispers_and_macros.md)
</p>
