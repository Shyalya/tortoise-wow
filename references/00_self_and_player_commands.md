# 00. Self & Own Character Commands

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

### 📚 Complete Module Reference Library

| Self & Accounts | Server & World | Gameplay & Progression | Bots & Automation |
| :--- | :--- | :--- | :--- |
| **[00. Self & Player Commands](./00_self_and_player_commands.md)** | [02. Server & Reloads](./02_server_and_reloads.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [01. Security & Accounts](./01_security_and_account.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | [17. Tier Sets & Gear](./17_tier_items_and_endgame_gear.md) | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |
| [10. Turtle Custom](./10_turtle_custom_features.md) | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) | **[18. Class Talent Specs](./18_class_talent_specs.md)** | [13. Macro Book](./13_bot_whispers_and_macros.md) |

---

## 1. Categorized In-Game Lua Macros (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`) or run them directly in chat to manage your **own player character**:

### Category A: Essential Character Boost, Gear & Resources (Top Priority)

#### 🌟 Macro 1: Complete Level 60 Instant Boost Suite (Spells + Skills + Travel + Bags + Gold)
Instantly raises your character to level 60, teaches all class abilities and trainer recipes, maxes all weapon and defense skills to 300, unlocks all flight paths and map fog, awards 1,000 Gold with 4x 18-slot Bottomless Bags, and fully replenishes HP and mana:
```lua
.levelup 60
.learn all_myspells
.learn all_trainer
.learn all_recipes
.maxskill
.modify money 10000000
.additem 14156 4
.taxicheat on
.explorecheat on
.repairitems
.replenish
```

#### 🎲 Macro 2: Randomize Level-Scaled Epic Gear, Permanent Enchants & Consumables
Generates randomized **Epic** quality armor and weapons scaled to your level, applies optimal permanent enchantments to all slots, and stocks class consumables, potions, and food:
```lua
.bot gear * epic
.bot enchants *
.bot consumes *
.bot potions *
.bot regs *
.bot food *
```

#### 💎 Macro 3: Equip Best-in-Slot Gear (Highest Item Level for Current Level)
Equips the absolute highest stat Best-in-Slot items from the database for your current level and enchants all gear:
```lua
.bot gear * best
.bot enchants *
.repairitems
```

#### 🏦 Macro 4: Remote Bank Vault, Mailbox & 100% Gear Repair
Opens your personal bank storage and mailbox anywhere in the world and repairs all equipped gear to full durability:
```lua
.bank
.mailbox
.repairitems
```

#### 🔄 Macro 5: Free Talent Respec & Cooldown Reset
Clears your talent tree free of charge without trainer fees and resets all ability cooldowns immediately:
```lua
.reset talents
.cooldown
```

---

### Category B: Survival, World Buffs & Instant Recovery (Middle Priority)

#### 🛡️ Macro 6: Panic Combat Recovery (God Mode + Drop Combat + Clear Cooldowns + Full Resources)
Instantly prevents death, clears aggro/combat lock, resets all cooldowns, and restores 100% health, mana, rage, and energy:
```lua
.god on
.combatstop
.cooldown
.replenish
```

#### ⚰️ Macro 7: Instant Revive, Cleanse & Cooldown Reset
Resurrects your character from ghost/corpse with 100% health and mana and clears cooldowns:
```lua
.revive
.replenish
.cooldown
```

#### ✨ Macro 8: Apply Major Raid World Buffs (Dragonslayer + Songflower + Warchief)
Applies full world buffs to your character:
```lua
.aura 22888
.aura 15366
.aura 16609
```

#### 👑 Macro 9: Max Exalted Raid Reputations (Argent Dawn, Cenarion, Zandalar, Nozdormu) + 5k Honor
Instantly sets all major raid factions to Exalted (42,999 standing) and grants 5,000 spendable PvP Honor points:
```lua
.modify rep 529 42999
.modify rep 609 42999
.modify rep 270 42999
.modify rep 910 42999
.modify rep 576 42999
.modify honor 5000
```

---

### Category C: Fast Travel, Flight & Mobility (Bottom Section)

#### 🐎 Macro 10: Epic Mount Travel Speed (200% Movement)
Sets your character's running speed to match a 100% epic ground mount:
```lua
.modify speed 2.0
```

#### 🚀 Macro 11: GM Flight Mode + High Speed
Enables full levitation/flight and sets 250% travel speed:
```lua
.hover on
.modify speed 2.5
```

#### ⚡ Macro 12: 20-Yard Forward Blink & 15-Yard Upward Leap
Instantly blinks 20 yards through world geometry in your facing direction and lifts 15 yards:
```lua
.go forward 20
.go up 15
```

#### 🛑 Macro 13: Normal Mobility Reset
Disables flying and resets movement speed back to default 1.0:
```lua
.hover off
.modify speed 1.0
```

#### 🆘 Macro 14: Emergency Hearthstone Return
Rescues stuck characters by triggering an instant hearthstone homebind return:
```lua
.unstuck
```

---

## 2. Complete Self / Own Character Commands Directory

Verified against CMaNGOS core engine handlers (`src/game/Commands/Commands.cpp` & `Chat.cpp`):

### ⚡ Character Progression & Training
- `.levelup [levels]`
  - **What it does**: Advances your character by specified levels (e.g. `.levelup 60` or `.levelup 1`).
- `.learn all_myspells`
  - **What it does**: Teaches all class abilities matching your current level.
- `.learn all_trainer`
  - **What it does**: Teaches all trainer spells for your class.
- `.learn all_recipes`
  - **What it does**: Teaches all craft recipes for your active professions.
- `.maxskill`
  - **What it does**: Raises all known weapon, defense, and profession skills to current level cap (300 at level 60).
- `.reset talents`
  - **What it does**: Resets talent tree without trainer gold fees.
- `.cooldown`
  - **What it does**: Instantly clears all active spell and ability cooldown timers.

---

### 🎒 Gear, Enchants & Consumables by Level
- `.bot gear * [epic | rare | green | best | upgrade]`
  - **What it does**: Equips randomized gear scaled to your level of chosen quality.
- `.bot enchants *`
  - **What it does**: Applies optimal permanent enchantments to all equipped armor and weapon slots.
- `.bot consumes *`
  - **What it does**: Generates level-scaled flasks, elixirs, bandages, and scrolls in your bags.
- `.bot potions *`
  - **What it does**: Stocks level-appropriate healing and mana potions.
- `.bot regs *`
  - **What it does**: Stocks class reagents (Soul Shards, Ankhs, Flash Powder, Feathers, Candles).
- `.bot food *`
  - **What it does**: Stocks level-appropriate food and water drinks.
- `.bot prep *`
  - **What it does**: Master restock — refills all consumables, potions, reagents, and food in one click.
- `.additem <item_id> [count]`
  - **What it does**: Adds item directly into character inventory bags (e.g. `.additem 19019 1` for Thunderfury). Complete class item lists are indexed in [17. Tier Sets & Endgame Gear](./17_tier_items_and_endgame_gear.md).
- `.repairitems`
  - **What it does**: Repairs durability on all equipped gear and bag items to 100%.

---

### 🎲 RandomBot & Autonomous Scaling Engine (`.rndbot`)
Verified in `src/modules/PlayerBots/playerbot/RandomPlayerbotMgr.cpp:3658-3735`:
- `.rndbot init <target|%> [level]`
  - **What it does**: Completely re-rolls talents, equips randomized level-appropriate gear/enchants, learns all spells, and stocks consumables for current or specified level.
- `.rndbot upgrade <target|%>`
  - **What it does**: Automatically upgrades all weak equipment slots, learns new rank spells, and spends unassigned talent points.
- `.rndbot refresh <target|%>`
  - **What it does**: Restores full HP/mana, unstucks navigation, and repairs 100% item durability.
- `.rndbot revive <target|%>`
  - **What it does**: Resurrects dead bot or player character instantly.
- `.rndbot grind <target|%>`
  - **What it does**: Teleports to an active level-appropriate mob grinding/farming location.
- `.rndbot rpg <target|%>`
  - **What it does**: Teleports character/bot to the nearest town/city RPG social hub.
- `.rndbot stats`
  - **What it does**: Prints active bot population, online counts, memory load, and activity states.

---

### 💵 Economy, Banking & Mail
- `.modify money <copper_amount>`
  - **What it does**: Adds gold directly to your character (`10000000` = 1,000 Gold).
- `.bank`
  - **What it does**: Opens your personal bank storage window anywhere in the world.
- `.mailbox`
  - **What it does**: Opens your personal mailbox window anywhere.

---

### 🛡️ Combat Survival & Recovery
- `.god [on | off]`
  - **What it does**: Toggles invulnerability / god mode. You take zero damage from all sources.
- `.revive`
  - **What it does**: Resurrects yourself with 100% health and mana.
- `.replenish`
  - **What it does**: Restores full Health, Mana, Rage, and Energy immediately.
- `.combatstop`
  - **What it does**: Drops combat state immediately, clearing threat and allowing eating/mounting.
- `.aura <spell_id>`
  - **What it does**: Applies an aura or world buff directly onto your character (`22888` = Dragonslayer, `15366` = Songflower, `16609` = Warchief's Blessing).
- `.unaura <spell_id>`
  - **What it does**: Removes an active aura/buff from your character.

---

### 🗺️ Mobility & World Exploration
- `.modify speed <multiplier>`
  - **What it does**: Sets movement speed (`1.0` = normal, `2.0` = 100% mount speed, `2.5` = fast sprint).
- `.hover [on | off]`
  - **What it does**: Enables levitation / flight mode.
- `.waterwalk [on | off]`
  - **What it does**: Enables walking on water surfaces without swimming.
- `.taxicheat [on | off]`
  - **What it does**: Unlocks all flight master taxi nodes globally.
- `.explorecheat [on | off]`
  - **What it does**: Uncovers all world map fog-of-war.
- `.go forward [distance]`
  - **What it does**: Blinks forward through world geometry in your facing direction.
- `.go up [distance]`
  - **What it does**: Teleports straight up along the vertical Z axis.
- `.go corpse`
  - **What it does**: Teleports directly to your ghost's dead body location.
- `.unstuck`
  - **What it does**: Casts emergency hearthstone return to rescue stuck characters.

---

<p align="center">
  [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 01. Security & Accounts ➡](./01_security_and_account.md)
</p>
