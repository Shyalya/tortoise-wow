# 02. Server Operations & Reloads

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

### Category A: Hotfix Content & Database Reloads (Top Priority)

#### 🔄 Macro 1: Master Hotfix Reload (Items, Spells, Quests, NPCs, Vendors)
Reloads the primary content database tables live into server memory without restarting the world daemon:
```lua
.reload item_template
.reload quest_template
.reload creature_template
.reload spell_affect
.reload npc_vendor
.reload npc_trainer
```

#### 📦 Macro 2: Dynamic Module & Localization Reload
Reloads dynamic module strings, translations, and server configurations live:
```lua
.reload config
.reload module_string
.reload module_string_locale
.reload mangos_string
```

---

### Category B: Loot Tables & World Portals Reload (Middle Priority)

#### 💰 Macro 3: Loot Tables & Drop Rates Reload
Reloads creature loot, reference loot, skinning, and fishing drop tables:
```lua
.reload creature_loot_template
.reload reference_loot_template
.reload item_loot_template
.reload skinning_loot_template
```

#### 🗺️ Macro 4: Teleports & World Portals Reload
Reloads teleport destination tokens, area triggers, and taverns:
```lua
.reload game_tele
.reload areatrigger_teleport
.reload areatrigger_tavern
```

---

### Category C: Server Maintenance, Broadcasts & Lifecycle (Bottom Section)

#### 📢 Macro 5: Server Maintenance Broadcast & Safe Save
Sends a realm-wide announcement and flushes all character inventories to the database:
```lua
.broadcast Server maintenance in 15 minutes. Please find a safe resting area.
.saveall
```

#### ⏱️ Macro 6: 5-Minute Graceful Restart Countdown
Starts an automated 300-second restart countdown with automatic player notifications:
```lua
.server restart 300
```

#### 🛑 Macro 7: Cancel Pending Restart / Shutdown
Cancels any active server restart or shutdown countdown immediately:
```lua
.server restart cancel
.server shutdown cancel
```

---

## 2. Server Administration Commands (`.server` / `.broadcast` / `.notify` / `.saveall`)

| Command | Security | Description | Usage / Example |
| :--- | :--- | :--- | :--- |
| `.server info` | Player (0) | Displays server uptime, active player count, and bot count. | `.server info` |
| `.server restart <seconds>` | Admin (4) | Initiates a countdown and restarts the server process. | `.server restart 60` |
| `.server restart cancel` | Admin (4) | Cancels a pending server restart. | `.server restart cancel` |
| `.server shutdown <seconds>` | Admin (4) | Initiates a countdown and shuts down the server. | `.server shutdown 120` |
| `.server shutdown cancel` | Admin (4) | Cancels a pending server shutdown. | `.server shutdown cancel` |
| `.server corpses` | Admin (4) | Forces immediate cleanup of world corpses. | `.server corpses` |
| `.server resetallraids` | Admin (4) | Forces an immediate reset on all active raid locks. | `.server resetallraids` |
| `.broadcast <message>` | Admin (4) | Sends system-wide chat announcement to all players. | `.broadcast Event starting in 10 minutes!` |
| `.notify <message>` | Admin (4) | Displays center-screen popup message to all players. | `.notify Welcome to Tortoise-WoW!` |
| `.saveall` | Admin (4) | Forces an immediate DB write for all online characters. | `.saveall` |
| `.server exit` | Console (6) | Immediately stops the process without clean shutdown. | `.server exit` |

---

## 3. Complete Database Hot-Reload Target Catalog (`.reload`)

Reload tables directly into server memory from MariaDB without restarting `mangosd`:

```lua
.reload <table_name>
```

### Core World, Content & Dynamic Module Reloads
| Target | Security | What it Reloads |
| :--- | :--- | :--- |
| `.reload config` | Admin (4) | Reloads `mangosd.conf` server configuration file live. |
| `.reload module_string` | Admin (4) | Reloads dynamic module string tables (`tw_world.module_string`). |
| `.reload module_string_locale` | Admin (4) | Reloads localized module string translations (`tw_world.module_string_locale`). |
| `.reload item_template` | Admin (4) | Reloads item stats, requirements, and display models. |
| `.reload creature_template` | Admin (4) | Reloads NPC stats, models, spells, and flags. |
| `.reload creature_loot_template` | Admin (4) | Reloads NPC creature loot drop tables. |
| `.reload quest_template` | Admin (4) | Reloads quest objectives, rewards, and text. |
| `.reload gameobject` | Admin (4) | Reloads static game objects and spawns. |
| `.reload npc_vendor` | Admin (4) | Reloads vendor inventory stock and purchase costs. |
| `.reload npc_trainer` | Admin (4) | Reloads trainer spell offerings and skill caps. |
| `.reload game_tele` | Admin (4) | Reloads predefined `.tele` destination tokens. |

### Complete Alphabetic Target Directory
- **Accounts & Bans**: `.reload account_banned`, `.reload ip_banned`
- **Area Triggers & Graveyards**: `.reload areatrigger_involvedrelation`, `.reload areatrigger_tavern`, `.reload areatrigger_teleport`, `.reload game_graveyard_zone`, `.reload game_weather`, `.reload taxi_path_transitions`
- **Creatures & Spawns**: `.reload creature`, `.reload creature_ai_events`, `.reload creature_battleground`, `.reload creature_display_info_addon`, `.reload creature_groups`, `.reload creature_involvedrelation`, `.reload creature_onkill_reputation`, `.reload creature_questrelation`, `.reload creature_spells`, `.reload creature_spells_scripts`, `.reload locales_creature`, `.reload pet_name_generation`, `.reload trainer_greeting`
- **GameObjects**: `.reload gameobject_battleground`, `.reload gameobject_involvedrelation`, `.reload gameobject_loot_template`, `.reload gameobject_questrelation`, `.reload gameobject_requirement`, `.reload gameobject_scripts`, `.reload locales_gameobject`
- **Items & Loot Tables**: `.reload item_enchantment_template`, `.reload item_loot_template`, `.reload item_required_target`, `.reload locales_item`, `.reload disenchant_loot_template`, `.reload fishing_loot_template`, `.reload mail_loot_template`, `.reload map_loot_disabled`, `.reload pickpocketing_loot_template`, `.reload reference_loot_template`, `.reload skinning_loot_template`
- **Locales & Texts**: `.reload locales_gossip_menu_option`, `.reload locales_page_text`, `.reload locales_points_of_interest`, `.reload locales_quest`, `.reload mangos_string`, `.reload npc_gossip`, `.reload npc_text`, `.reload page_text`, `.reload points_of_interest`
- **Quests & Reputations**: `.reload quest_end_scripts`, `.reload quest_greeting`, `.reload quest_start_scripts`, `.reload reputation_reward_rate`, `.reload reputation_spillover_template`, `.reload reserved_name`
- **Spells & Auras**: `.reload spell_affect`, `.reload spell_area`, `.reload spell_chain`, `.reload spell_disabled`, `.reload spell_elixir`, `.reload spell_group`, `.reload spell_group_stack_rules`, `.reload spell_learn_spell`, `.reload spell_mod`, `.reload spell_pet_auras`, `.reload spell_proc_event`, `.reload spell_proc_item_enchant`, `.reload spell_script_target`, `.reload spell_scripts`, `.reload spell_target_position`, `.reload spell_threats`, `.reload instance_buff_removal`
- **Faction Change Mappings**: `.reload player_factionchange_items`, `.reload player_factionchange_mounts`, `.reload player_factionchange_quests`, `.reload player_factionchange_reputations`, `.reload player_factionchange_spells`

---

<p align="center">
  [⬅ Prev: 01. Security & Accounts](./01_security_and_account.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 03. Teleports & Movement ➡](./03_teleports_and_movement.md)
</p>
