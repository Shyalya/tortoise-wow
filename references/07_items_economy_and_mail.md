# 07. Items, Economy & Mail

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

### 📚 Complete 16-Module Reference Library

| Security & Server | World & Characters | Gameplay & Items | Bots & Modules |
| :--- | :--- | :--- | :--- |
| [01. Security & Accounts](./01_security_and_account.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [02. Server & Reloads](./02_server_and_reloads.md) | [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [08. Spawning & Editing](./08_spawning_and_world_editing.md) | **[07. Items & Economy](./07_items_economy_and_mail.md)** | [10. Turtle Custom](./10_turtle_custom_features.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |

---

## 1. Categorized In-Game Lua Macros (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`) or run them directly in chat:

### Category A: Essential Gear, Bags & Currency (Top Priority)

#### 💰 Macro 1: Grant 1,000 Gold & 4x 18-Slot Bottomless Bags
Adds 1,000 Gold (10,000,000 copper) and four 18-slot Bottomless Bags directly to character inventory:
```lua
.modify money 10000000
.additem 14156 4
```

#### 🛡️ Macro 2: Spawn Warrior Tier 3 (Dreadnaught) + Thunderfury
Spawns complete 9-piece Dreadnaught armor set, Thunderfury, Elementium Reinforced Bulwark, and repairs all gear:
```lua
.additemset 523
.additem 19019 1
.additem 19349 1
.repairitems
```

#### 🏦 Macro 3: Remote Bank, Mailbox & Full Gear Repair
Opens your personal bank vault and mailbox anywhere in the world and restores 100% item durability:
```lua
.bank
.mailbox
.repairitems
```

#### 🎲 Macro 4: Level-Scaled Random Gear, Enchants & Consumables
Generates randomized Epic quality armor and weapons matching current level, enchants all slots, and stocks consumables/potions:
```lua
.bot gear * epic
.bot enchants *
.bot consumes *
.bot potions *
.bot regs *
```

---

### Category B: Remote Mail Delivery to Players & Bots (Middle Priority)

#### 📬 Macro 4: Remote Mail Gold to Player
Sends 1,000 Gold directly to the recipient's in-game mailbox:
```lua
.send money Sam "Raid Supplies" "Guild bank funding for consumables" 10000000
```

#### 📦 Macro 5: Remote Mail Legendary Items
Mails Thunderfury and Bindings of the Windseeker directly to player mailbox:
```lua
.send items Sam "Raid Drop" "Congratulations on your raid drop" 19019:1 18563:1
```

---

### Category C: Inventory Cleanup & Maintenance (Bottom Section)

#### 🧹 Macro 6: Inventory Item Removal & Full Repair
Removes a specific item entry from bags and repairs all gear:
```lua
.deleteitem 19019 1
.repairitems
```

---

## 2. Items, Economy & Mail Commands Reference

Verified against CMaNGOS command handlers (`src/game/Chat/Chat.cpp` & `Commands.cpp`):

### 🎒 Item & Equipment Spawning (`.additem` / `.additemset`)
- `.additem <item_id> [count]`
  - **Security**: Developer (3)
  - **What it does**: Adds specified item ID directly into character inventory bags.
  - **Example**: `.additem 19019 1` *(Thunderfury, Blessed Blade of the Windseeker)*

- `.additemset <set_id>`
  - **Security**: Developer (3)
  - **What it does**: Spawns complete armor set directly into bags (`523` = Warrior Dreadnaught, `524` = Paladin Redemption, `525` = Hunter Cryptstalker, `526` = Shaman Earthshatterer, `527` = Rogue Bonescythe, `528` = Druid Dreamwalker, `529` = Priest Faith, `530` = Warlock Plagueheart, `531` = Mage Frostfire).
  - **Example**: `.additemset 523`

- `.deleteitem <item_id> [count]`
  - **Security**: Developer (3)
  - **What it does**: Deletes item from target character inventory.
  - **Example**: `.deleteitem 19019 1`

- `.repairitems`
  - **Security**: Developer (3)
  - **What it does**: Restores 100% durability to all equipped gear and bag items.
  - **Example**: `.repairitems`

- `.bank`
  - **Security**: Developer (3)
  - **What it does**: Opens your personal bank storage window anywhere in the world.
  - **Example**: `.bank`

- `.mailbox`
  - **Security**: Developer (3)
  - **What it does**: Opens your personal mailbox window anywhere.
  - **Example**: `.mailbox`

---

### 💵 Economy & Currency (`.modify money`)
- `.modify money <copper_amount>`
  - **Security**: Developer (3)
  - **What it does**: Adds copper currency directly to character (`10000` = 1 Gold, `10000000` = 1,000 Gold).
  - **Example**: `.modify money 10000000`

---

### 📬 Remote Mail Delivery (`.send`)
- `.send items <player> "<subject>" "<body>" <item_id[:count]> ...`
  - **Security**: Developer (3)
  - **What it does**: Delivers items via in-game mail with custom subject and letter body text.
  - **Example**: `.send items Sam "Raid Reward" "Great performance" 19019:1 19349:1`

- `.send money <player> "<subject>" "<body>" <copper_amount>`
  - **Security**: Developer (3)
  - **What it does**: Sends money directly to player character mailbox.
  - **Example**: `.send money Sam "Guild Payout" "Weekly raid dividend" 5000000`

- `.send mail <player> "<subject>" "<body>"`
  - **Security**: Moderator (2)
  - **What it does**: Sends a standard text letter to player mailbox.
  - **Example**: `.send mail Sam "Notice" "Please check your Discord PM"`

---

<p align="center">
  [⬅ Prev: 06. Quests, Instances & Events](./06_quests_instances_and_events.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 08. Spawning & World Editing ➡](./08_spawning_and_world_editing.md)
</p>
