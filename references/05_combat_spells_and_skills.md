# 05. Combat, Spells & Skills

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

### Category A: Instant Level 60 Boost & Raid Readiness (Top Priority)

#### 🌟 Macro 1: Complete Level 60 Boost Suite (Spells + Skills + Full Resources)
Instantly levels character to 60, teaches all class abilities and trainer recipes, maxes all weapon and defense skills to 300, and fully restores HP/mana:
```lua
.levelup 60
.learn all_myspells
.learn all_trainer
.learn all_recipes
.maxskill
.replenish
```

#### 🛡️ Macro 2: Panic Raid Recovery (God Mode + Drop Combat + Clear Cooldowns)
Instantly prevents death, breaks combat lock, resets all ability cooldowns, and restores full health and mana:
```lua
.god on
.combatstop
.cooldown
.replenish
```

#### ⚰️ Macro 3: Instant Revive & Full Recovery
Resurrects selected target or yourself with 100% health and mana and clears cooldowns:
```lua
.revive
.replenish
.cooldown
```

#### 🔄 Macro 4: Free Talent Tree Reset
Resets talent points for the selected player character without gold cost or increasing trainer respec fee:
```lua
.reset talents
```

---

### Category B: Targeted Abilities, Skills & Auras (Middle Priority)

#### 🐎 Macro 5: Teach Epic Mount & Riding Skill
Teaches Dreadsteed / Charger / Epic mount and sets Riding skill to max:
```lua
.learn 23161
.setskill 148 300 300
```

#### ✨ Macro 6: Apply World Buffs (Rallying Cry & Songflower)
Applies major raid world buffs to target:
```lua
.aura 22888
.aura 15366
```

#### 🎯 Macro 7: Max Weapon & Defense Skills
Raises all equipped weapon skills (Swords, Axes, Maces, Bows, Defense) to maximum skill cap (300):
```lua
.maxskill
```

---

### Category C: Combat Cheats, Testing & Resets (Bottom Section)

#### ⚡ Macro 8: 10,000 Direct Damage & Kill Target
Deals massive damage or instantly slays the selected target:
```lua
.damage 10000
.die
```

#### 💣 Macro 9: Area-of-Effect Radial Damage (5,000 DMG)
Deals direct spell damage to all enemy units within radius:
```lua
.aoedamage 5000
```

#### ⚠️ Macro 10: Complete Spells & Stats Reset
Clears learned spells and recalculates character stats:
```lua
.reset spells
.reset stats
```

---

## 2. Combat & Survival Commands Reference

Verified against CMaNGOS command handlers (`src/game/Chat/Chat.cpp` & `Commands.cpp`):

### 🛡️ Combat Cheats (`.god` / `.combatstop` / `.revive`)
- `.god [on | off]`
  - **Security**: Developer (3)
  - **What it does**: Toggles invulnerability / god mode. Character takes zero damage from all sources.
  - **Example**: `.god on`

- `.revive`
  - **Security**: Developer (3)
  - **What it does**: Resurrects targeted dead character with 100% health and mana.
  - **Example**: `.revive`

- `.die`
  - **Security**: Developer (3)
  - **What it does**: Instantly kills selected unit or player.
  - **Example**: `.die`

- `.replenish`
  - **Security**: Developer (3)
  - **What it does**: Restores full Health, Mana, Rage, and Energy immediately.
  - **Example**: `.replenish`

- `.combatstop`
  - **Security**: Moderator (2)
  - **What it does**: Drops combat state immediately for target character or party.
  - **Example**: `.combatstop`

- `.cooldown`
  - **Security**: Developer (3)
  - **What it does**: Clears all active spell and ability cooldowns instantly.
  - **Example**: `.cooldown`

- `.aura <spell_id>`
  - **Security**: Developer (3)
  - **What it does**: Applies an aura/buff spell ID directly onto the selected target.
  - **Example**: `.aura 22888` *(Rallying Cry of the Dragonslayer)*

- `.unaura <spell_id>`
  - **Security**: Developer (3)
  - **What it does**: Removes an active aura/buff from target.
  - **Example**: `.unaura 22888`

- `.damage <amount>`
  - **Security**: Developer (3)
  - **What it does**: Deals direct damage to selected target.
  - **Example**: `.damage 10000`

- `.aoedamage <amount>`
  - **Security**: Developer (3)
  - **What it does**: Deals direct damage to all hostile creatures in radius.
  - **Example**: `.aoedamage 5000`

---

### 📚 Spells, Skills & Resets Reference (`.learn` / `.maxskill` / `.reset`)
- `.learn all_myspells`
  - **Security**: Developer (3)
  - **What it does**: Teaches all class spells appropriate for target character's current level.
  - **Example**: `.learn all_myspells`

- `.learn all_trainer`
  - **Security**: Developer (3)
  - **What it does**: Teaches all available trainer spells for target's class.
  - **Example**: `.learn all_trainer`

- `.learn all_recipes`
  - **Security**: Developer (3)
  - **What it does**: Teaches all recipes for current active professions.
  - **Example**: `.learn all_recipes`

- `.learn <spell_id>`
  - **Security**: Developer (3)
  - **What it does**: Teaches target character a specific spell ID.
  - **Example**: `.learn 23161` *(Dreadsteed)*

- `.unlearn <spell_id>`
  - **Security**: Developer (3)
  - **What it does**: Removes a spell ID from target character.
  - **Example**: `.unlearn 23161`

- `.maxskill`
  - **Security**: Developer (3)
  - **What it does**: Raises all known weapon, defense, and profession skills to current level cap (300 at level 60).
  - **Example**: `.maxskill`

- `.setskill <id> <val> [max]`
  - **Security**: Developer (3)
  - **What it does**: Sets specific skill ID value and maximum.
  - **Example**: `.setskill 95 300 300` *(Defense)*

- `.reset talents`
  - **Security**: Developer (3)
  - **What it does**: Resets talent points for selected character free of charge.
  - **Example**: `.reset talents`

- `.reset spells`
  - **Security**: Administrator (4)
  - **What it does**: Clears all learned spells and resets character to baseline.
  - **Example**: `.reset spells`

- `.reset stats`
  - **Security**: Administrator (4)
  - **What it does**: Recalculates all base stats and modifiers.
  - **Example**: `.reset stats`

---

<p align="center">
  [⬅ Prev: 04. Character Stats & Customization](./04_character_stats_and_visuals.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 06. Quests, Instances & Events ➡](./06_quests_instances_and_events.md)
</p>
