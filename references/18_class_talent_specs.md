# 18. Class Talent Specs & Raid Meta Builds (Turtle-WoW 1.18.1)

> **Target Core**: Turtle-WoW 1.18.1 (Build 7272) • CMaNGOS Engine + Custom Class Reworks  
> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [⚡ Self Commands (00_self_and_player_commands.md)](./00_self_and_player_commands.md) • [🎒 Tier Items (17_tier_items_and_endgame_gear.md)](./17_tier_items_and_endgame_gear.md) • [⚔️ Bot Tactics (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md)

---

## Complete 19-Module Reference Library

| Security & Server | World & Characters | Gameplay & Items | Bots & Modules |
| :--- | :--- | :--- | :--- |
| [00. Self & Own Character](./00_self_and_player_commands.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [01. Security & Accounts](./01_security_and_account.md) | [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [02. Server & Reloads](./02_server_and_reloads.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [10. Turtle Custom](./10_turtle_custom_features.md) | [17. Tier Sets & Gear](./17_tier_items_and_endgame_gear.md) | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |
| [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | **[18. Class Talent Specs](./18_class_talent_specs.md)** | *(Standalone Manual)* | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) |

---

## Official Turtle-WoW Interactive Talent Calculator Directory

Use these official interactive calculators to preview talent tree node prerequisites, ranks, and point allocations live:

| Class | Official Live Calculator Link | Signature 1.18.1 Meta Specializations |
| :--- | :--- | :--- |
| **Warrior** | [🎮 Turtle WoW Warrior Calculator](https://talents.turtle-wow.org/warrior) | DW Fury/Prot (0/31/20) • Deep Prot (11/5/35) • Fury DPS (17/34/0) • Arms (31/20/0) |
| **Rogue** | [🎮 Turtle WoW Rogue Calculator](https://talents.turtle-wow.org/rogue) | Combat Swords (19/32/0) • Combat Daggers (15/31/5) • Rogue Tank (Flourish) |
| **Mage** | [🎮 Turtle WoW Mage Calculator](https://talents.turtle-wow.org/mage) | Ignite Fire (17/31/3) • AP Frost (28/0/23) • Arcane Mind (31/0/20) |
| **Warlock** | [🎮 Turtle WoW Warlock Calculator](https://talents.turtle-wow.org/warlock) | DS/Ruin (7/21/23) • SM/Ruin (30/0/21) • Master Conjurer Firestone (0/31/20) |
| **Hunter** | [🎮 Turtle WoW Hunter Calculator](https://talents.turtle-wow.org/hunter) | Marksmanship (20/31/0) • Melee Survival (0/21/30 Carve/Lacerate) |
| **Priest** | [🎮 Turtle WoW Priest Calculator](https://talents.turtle-wow.org/priest) | Holy/Disc (21/30/0) • Holy Smite DPS (31/20/0) • Shadow Mana Battery (13/0/38) |
| **Paladin** | [🎮 Turtle WoW Paladin Calculator](https://talents.turtle-wow.org/paladin) | Holy Healer (31/20/0) • Retribution Burst (11/0/40) • Prot Tank (0/31/20) |
| **Shaman** | [🎮 Turtle WoW Shaman Calculator](https://talents.turtle-wow.org/shaman) | Resto Chain Heal (0/12/39) • Shaman Tank "Tankhance" (19/32/0) • Elemental (30/0/21) |
| **Druid** | [🎮 Turtle WoW Druid Calculator](https://talents.turtle-wow.org/druid) | Feral Tank/DPS (14/32/5) • Balance Moonkin (31/0/20) • Moonglow Resto (24/0/27) |

---

## 1. Warrior Talent Specs

🔗 **Interactive Calculator**: [https://talents.turtle-wow.org/warrior](https://talents.turtle-wow.org/warrior)

### Spec 1: Dual-Wield Fury/Prot Tank (0/31/20) - Max Raid Threat BiS

The definitive high-end raid tanking spec in Turtle-WoW. Dual-wields fast weapons in Defensive Stance to generate enormous Rage and Threat Per Second (TPS), allowing DPS to go all-out without threat throttling.

- **Calculator Template**: [Warrior DW Fury/Prot (0/31/20)](https://talents.turtle-wow.org/warrior)

```text
Arms (0 points):
(None)

Fury (31 points):
- 5/5 Cruelty (+5% Melee Critical Strike)
- 5/5 Unbridled Wrath (Chance to gain 1 additional rage on melee swing)
- 5/5 Dual Wield Specialization (+25% Off-Hand weapon damage)
- 5/5 Enrage (+25% Melee damage bonus for 12s after being hit by a crit)
- 1/1 Death Wish (+20% Physical damage, Fear immunity for 30s)
- 5/5 Flurry (+30% Attack Speed for next 3 swings on critical strike)
- 4/5 Blood Craze or Impale
- 1/1 Bloodthirst (Instant strike for 45% Attack Power damage + heals for 30 HP per hit)

Protection (20 points):
- 5/5 Shield Specialization (Gain 1 rage when blocking, +5% block chance)
- 2/2 Anticipation (+10 Defense Skill) or 5/5 Toughness (+10% Armor from items)
- 1/1 Last Stand (+30% Maximum Health for 20s - essential emergency button)
- 5/5 Defiance (+15% Additional Threat in Defensive Stance)
- 1/1 Improved Taunt (Reduces Taunt cooldown by 2 seconds)
- 3/3 Improved Sunder Armor (Reduces Sunder Armor rage cost by 3)
```

- **Stat Priority**: Hit Cap (6% with 305+ Weapon Skill, 9% without) > Weapon Skill (305+) > Crit > Strength > Agility > Stamina > Armor.
- **Rotation**: Stay in **Defensive Stance**. Bloodthirst on CD > Revenge on proc > Heroic Strike queue on main hand swing > Devastate / Sunder Armor (maintain 5 stacks). Pop *Death Wish* + *Diamond Flask* at the start of boss encounters for immediate snap aggro.
- **Turtle WoW 1.18.1 Additions**:
  - **Thunder Clap in Defensive Stance**: Usable directly in Defensive Stance, giving tanks reliable multi-target AoE threat.
  - **Defensive Tactics**: Stance dance rage retention allows swapping to Berserker Stance for Berserker Rage/Pummel and returning without losing rage.

---

### Spec 2: Deep Protection Mitigation Tank (11/5/35) - Progression & Hardcore

Maximum physical and magical mitigation with high block value scaling. Uses 1H Weapon + Shield with Shield Slam for snap threat and Concussion Blow for dungeon control.

- **Calculator Template**: [Warrior Deep Protection (11/5/35)](https://talents.turtle-wow.org/warrior)

```text
Arms (11 points):
- 5/5 Deflection (+5% Parry)
- 5/5 Tactical Mastery (Retain up to 25 Rage when switching stances)
- 1/1 Anger Management (Generates 1 rage every 3 seconds)

Fury (5 points):
- 5/5 Cruelty (+5% Melee Critical Strike)

Protection (35 points):
- 5/5 Shield Specialization (+5% Block Chance, 1 Rage on block)
- 5/5 Toughness (+10% Armor from items)
- 1/1 Last Stand (Emergency health boost)
- 2/2 Improved Bloodrage (Generates 5 immediate extra rage)
- 5/5 Defiance (+15% Threat in Defensive Stance)
- 2/2 Improved Taunt (Taunt CD reduced by 2s)
- 3/3 Improved Sunder Armor
- 1/1 Concussion Blow (5-second physical stun)
- 1/1 One-Handed Weapon Specialization (+10% 1H Damage)
- 1/1 Shield Slam (High damage/threat slam based on shield block value + 50% dispel)
```

- **Stat Priority**: Defense (440 to reach uncrittable cap) > Hit (9%) > Stamina > Armor > Shield Block Value > Agility.
- **Rotation**: Shield Slam on CD > Revenge on proc > Thunder Clap (for AoE / attack speed debuff) > Sunder Armor > Shield Block (maintain active against physical crushing blows).

---

### Spec 3: Dual-Wield Fury DPS (17/34/0) - #1 Physical DPS Spec

The highest sustained physical melee damage spec. Combines Deep Wounds bleeding and Impale critical multiplier from Arms with Bloodthirst and Flurry attack speed in Fury.

- **Calculator Template**: [Warrior Fury DPS (17/34/0)](https://talents.turtle-wow.org/warrior)

```text
Arms (17 points):
- 3/3 Rend
- 2/2 Deep Wounds (Crits cause 60% average weapon damage bleed over 12s)
- 2/2 Impale (+20% Critical Strike Damage bonus)
- 2/2 Overpower (+50% Crit chance on Overpower against dodging targets)
- 5/5 Tactical Mastery (Retain rage when stance dancing for Overpower)
- 1/1 Anger Management

Fury (34 points):
- 5/5 Cruelty (+5% Crit)
- 5/5 Unbridled Wrath
- 5/5 Dual Wield Specialization
- 5/5 Enrage
- 1/1 Death Wish
- 5/5 Flurry (+30% Attack Speed)
- 2/2 Weapon Mastery or Improved Berserker Rage
- 1/1 Bloodthirst
```

- **Stat Priority**: Hit Cap (6% with 305+ skill / 9%) > Weapon Skill (305+) > Crit > Strength > Attack Power > Agility.
- **Rotation**: Bloodthirst on CD > Whirlwind on CD > Overpower on target dodge > Heroic Strike (queue when Rage > 40).

---

## 2. Rogue Talent Specs

🔗 **Interactive Calculator**: [https://talents.turtle-wow.org/rogue](https://talents.turtle-wow.org/rogue)

### Spec 4: Combat Swords Rogue (19/32/0) - Sustained Raiding BiS

Maximum sustained physical DPS using fast off-hand and slow high-damage main-hand swords.

- **Calculator Template**: [Rogue Combat Swords (19/32/0)](https://talents.turtle-wow.org/rogue)

```text
Assassination (19 points):
- 5/5 Malice (+5% Crit)
- 3/3 Ruthlessness (60% chance to add 1 combo point after finisher)
- 1/1 Relentless Strikes (Finishing moves have 20% chance per CP to restore 25 Energy)
- 5/5 Lethality (+30% Crit damage bonus for Sinister Strike, Gouge, Backstab)
- 5/5 Murder (+2% Damage vs Humanoids, Giants, Beasts, Dragonkin)

Combat (32 points):
- 2/2 Improved Sinister Strike (Reduces SS energy cost by 5)
- 3/3 Improved Gouge or Lightning Reflexes
- 5/5 Precision (+5% Chance to Hit with melee weapons)
- 5/5 Dual Wield Specialization (+50% Off-Hand weapon damage)
- 1/1 Blade Flurry (+20% Attack Speed & strikes an additional nearby enemy)
- 5/5 Sword Specialization (5% chance to gain an extra swing on hit with swords)
- 2/2 Weapon Expertise (+5 Weapon Skill with Swords, Daggers, Fists)
- 1/1 Adrenaline Rush (+100% Energy regeneration rate for 15s)
```

- **Stat Priority**: Hit Cap (9% yellow, 14%+ dual-wield white) > Agility > Attack Power > Crit.
- **Rotation**: Sinister Strike to build 5 Combo Points > Slice and Dice (100% uptime mandatory!) > Eviscerate / Rupture (if SnD has >10s remaining).
- **Turtle WoW 1.18.1 Features**:
  - **Combo Point Persistence**: Combo points are preserved when switching targets.
  - **Rogue Tanking**: Access to *Flourish* (parry finisher) and *Agitating Poison* (high threat generation).

---

### Spec 5: Combat Daggers Rogue (15/31/5) - High Burst Boss DPS

Position-dependent high-burst spec requiring Backstab from behind the boss.

- **Calculator Template**: [Rogue Combat Daggers (15/31/5)](https://talents.turtle-wow.org/rogue)

```text
Assassination (15 points):
- 5/5 Malice (+5% Crit)
- 3/3 Ruthlessness
- 1/1 Relentless Strikes
- 5/5 Lethality (+30% Crit damage bonus)
- 1/1 Vigor (+10 Max Energy)

Combat (31 points):
- 3/3 Improved Gouge
- 5/5 Precision (+5% Hit)
- 5/5 Dagger Specialization (+5% Crit with Daggers)
- 5/5 Dual Wield Specialization
- 1/1 Blade Flurry
- 2/2 Weapon Expertise
- 2/2 Improved Sprint
- 1/1 Adrenaline Rush

Subtlety (5 points):
- 5/5 Opportunity (+20% Damage to Backstab, Ambush, Garrote)
```

- **Rotation**: Backstab to 5 CP > Slice and Dice (keep active) > Backstab > Eviscerate.

---

## 3. Mage Talent Specs

🔗 **Interactive Calculator**: [https://talents.turtle-wow.org/mage](https://talents.turtle-wow.org/mage)

### Spec 6: Ignite Fire Mage (17/31/3) - AQ40 & Naxxramas #1 Caster DPS

High rolling Ignite damage. When multiple Fire Mages crit, they stack a single massive rolling Ignite DoT on the boss that ticks for tens of thousands of damage.

- **Calculator Template**: [Mage Ignite Fire (17/31/3)](https://talents.turtle-wow.org/mage)

```text
Arcane (17 points):
- 2/2 Arcane Subtlety (Reduces target spell resistance by 10, -40% threat on Arcane)
- 3/3 Arcane Focus (+6% Hit on Arcane)
- 5/5 Arcane Concentration (Clearcasting - 10% chance on spell hit to enter Freecast)
- 1/1 Evocation (Channel 60% mana restore)
- 2/2 Improved Counterspell
- 3/3 Arcane Meditation (+15% Mana regen while casting)

Fire (31 points):
- 5/5 Improved Fireball (Reduces Fireball cast time by 0.5s)
- 5/5 Ignite (Spell crits ignite target for 40% additional damage over 4s)
- 1/1 Pyroblast
- 2/2 Flame Throwing (+6yd Spell Range)
- 2/2 Burning Soul (-70% Pushback on cast, -30% Fire Threat)
- 3/3 Critical Mass (+6% Spell Crit with Fire spells)
- 5/5 Fire Power (+10% Fire Spell Damage)
- 1/1 Combustion (Each fire damage spell cast increases spell crit by 10% until 3 crits)

Frost (3 points):
- 3/3 Elemental Precision (+6% Spell Hit for Fire and Frost spells)
```

- **Stat Priority**: Spell Hit (16% cap, 10% from gear with 3/3 Elemental Precision) > Spell Damage > Spell Crit > Intellect.
- **Rotation**: Fireball spam > Scorch (apply and maintain 5 stacks of Improved Scorch for +15% Fire vulnerability) > Pyroblast on Combustion pull > Fire Blast on movement.
- **Turtle WoW 1.18.1 Tuning**: Ignite operates on a 4-second rolling window with full stack preservation.

---

### Spec 7: Arcane Power Frost Mage (28/0/23) - MC & BWL Progression

Maximum burst frost damage for Molten Core and Blackwing Lair where mobs have 100% Fire Immunity.

- **Calculator Template**: [Mage Arcane Power Frost (28/0/23)](https://talents.turtle-wow.org/mage)

```text
Arcane (28 points):
- 2/2 Arcane Subtlety
- 5/5 Arcane Concentration (Clearcasting)
- 1/1 Evocation
- 5/5 Arcane Mind (+10% Total Mana)
- 3/3 Arcane Instability (+3% Spell Damage & +3% Spell Crit)
- 1/1 Arcane Power (+30% Spell Damage, +30% Mana Cost for 15s)

Frost (23 points):
- 3/3 Elemental Precision (+6% Spell Hit)
- 5/5 Ice Shards (+100% Spell Critical Damage bonus)
- 3/3 Frostbite
- 3/3 Piercing Ice (+6% Frost Damage)
- 1/1 Cold Snap (Instantly resets all Frost cooldowns)
- 2/2 Arctic Reach (+20% Range)
- 5/5 Ice Floes or Channeling
- 1/1 Ice Block
```

- **Rotation**: Frostbolt spam > Pop Arcane Power + Trinket during burst phases > Cold Snap for double Ice Block/burst.

---

## 4. Warlock Talent Specs

🔗 **Interactive Calculator**: [https://talents.turtle-wow.org/warlock](https://talents.turtle-wow.org/warlock)

### Spec 8: DS/Ruin Warlock (7/21/23) - Top Raiding Shadow DPS

Sacrifices the Succubus minion via *Demonic Sacrifice* to gain a permanent +15% Shadow Damage multiplier, combined with *Ruin* for +100% critical strike bonus.

- **Calculator Template**: [Warlock DS/Ruin (7/21/23)](https://talents.turtle-wow.org/warlock)

```text
Affliction (7 points):
- 5/5 Improved Corruption (Instant cast Corruption)
- 2/2 Suppression (+4% Hit with Affliction spells)

Demonology (21 points):
- 5/5 Demonic Embrace (+15% Total Stamina)
- 2/2 Master Conjurer (+20% Healthstone heal, +30% Firestone damage & Spellstone crit)
- 3/3 Fel Intellect / Fel Stamina
- 1/1 Demonic Sacrifice (Sacrifice Succubus: +15% Shadow Damage bonus)

Destruction (23 points):
- 5/5 Improved Shadow Bolt (Shadow Bolt crits grant +20% Shadow damage for 4 hits)
- 5/5 Bane (Reduces Shadow Bolt cast time by 0.5s)
- 5/5 Devastation (+5% Spell Crit with Destruction spells)
- 1/1 Shadowburn (Instant cast emergency burst strike)
- 1/1 Ruin (+100% Critical Strike Damage bonus for Destruction spells)
```

- **Stat Priority**: Spell Hit (16% cap) > Shadow Spell Damage > Spell Crit > Stamina.
- **Rotation**: Curse of Shadows / Curse of Elements (assigned by raid lead) > Shadow Bolt spam > Life Tap to manage mana.
- **Turtle WoW 1.18.1 Rework**:
  - **Master Conjurer**: Combined talent merging Improved Healthstone, Firestone, and Spellstone into a single 2-point talent in Row 2 Demonology.
  - **Firestone / Spellstone**: Firestone grants flat +Fire Spell Damage; Spellstone grants +Spell Crit.

---

### Spec 9: SM/Ruin Affliction Warlock (30/0/21) - Utility & Multi-DoT

Provides *Nightfall* instant Shadow Bolt procs and *Shadow Mastery* (+10% Shadow damage) without needing to sacrifice your demon pet.

- **Calculator Template**: [Warlock SM/Ruin (30/0/21)](https://talents.turtle-wow.org/warlock)

```text
Affliction (30 points):
- 5/5 Improved Corruption
- 5/5 Fel Concentration (70% pushback resistance on Drain Life)
- 2/2 Nightfall (4% chance on Corruption/Drain Life ticks to grant instant Shadow Bolt)
- 1/1 Siphon Life
- 5/5 Shadow Mastery (+10% Shadow Spell Damage)

Destruction (21 points):
- 5/5 Improved Shadow Bolt
- 5/5 Bane
- 5/5 Devastation
- 1/1 Shadowburn
- 1/1 Ruin (+100% Critical Strike Damage bonus)
```

---

## 5. Hunter Talent Specs

🔗 **Interactive Calculator**: [https://talents.turtle-wow.org/hunter](https://talents.turtle-wow.org/hunter)

### Spec 10: Marksmanship PvE Hunter (20/31/0) - Raid Ranged DPS

Maximum ranged physical damage using Aimed Shot and Multi-Shot weave between auto-shots.

- **Calculator Template**: [Hunter Marksmanship (20/31/0)](https://talents.turtle-wow.org/hunter)

```text
Beast Mastery (20 points):
- 5/5 Improved Aspect of the Hawk (+30% Ranged attack speed proc)
- 5/5 Thick Hide
- 5/5 Unleashed Fury (+20% Pet Damage)
- 5/5 Ferocity (+15% Pet Critical Strike)

Marksmanship (31 points):
- 5/5 Efficiency (Reduces Shot mana cost by 10%)
- 5/5 Lethal Shots (+5% Ranged Critical Strike)
- 5/5 Mortal Shots (+30% Ranged Critical Damage bonus)
- 3/3 Barrage (+15% Multi-Shot damage)
- 2/2 Ranged Weapon Specialization (+5% Ranged Weapon Damage)
- 1/1 Trueshot Aura (Raid Attack Power aura)
```

- **Stat Priority**: Hit Cap (9%) > Agility > Ranged Attack Power > Crit.
- **Rotation**: Auto-Shot > Aimed Shot > Auto-Shot > Multi-Shot > Auto-Shot. Never clip auto-shot cast timer!
- **Turtle WoW 1.18.1 Baseline QoL**:
  - *Aimed Shot*, *Trueshot Aura*, and *Steady Shot* learned baseline at level 20 via trainers, freeing talent points.

---

### Spec 11: Melee Survival Hunter (0/21/30) - Turtle-WoW Signature Melee Spec

A fully viable close-quarters melee combat spec fighting alongside your pet with massive 2H weapon scaling.

- **Calculator Template**: [Hunter Melee Survival (0/21/30)](https://talents.turtle-wow.org/hunter)

```text
Marksmanship (21 points):
- 5/5 Lethal Shots (+5% Crit)
- 5/5 Efficiency
- 5/5 Mortal Shots (+30% Crit damage)
- 1/1 Aimed Shot / Rapid Fire enhancements

Survival (30 points):
- 5/5 Deflection (+5% Parry)
- 3/3 Savage Strikes (+20% Crit on Raptor Strike and Mongoose Bite)
- 2/2 Survivalist (+10% Total Health)
- 1/1 Carve (Instant 2H melee swing hitting up to 3 nearby enemies)
- 3/3 Surefooted (+3% Hit chance, +15% Movement Impairing resist)
- 5/5 Killer Instinct (+3% Crit)
- 1/1 Counterattack (Instant melee strike after parrying)
- 1/1 Trapping in Combat (Capstone: Traps can be laid during active combat encounters)
```

- **Stat Priority**: Hit Cap (9%) > Agility > Strength > Attack Power > Crit.
- **Rotation**: Raptor Strike on swing > Carve on CD > Mongoose Bite on CD > Lacerate (maintain bleed) > Lay Explosive/Immolation Trap in combat.

---

## 6. Priest Talent Specs

🔗 **Interactive Calculator**: [https://talents.turtle-wow.org/priest](https://talents.turtle-wow.org/priest)

### Spec 12: Holy / Discipline Raid Healer (21/30/0) - The Gold Standard

Provides *Meditation* (+15% mana regen while casting), *Inner Focus* (free instant cast with +25% crit), and *Divine Spirit* alongside massive Holy healing bonuses.

- **Calculator Template**: [Priest Holy/Disc (21/30/0)](https://talents.turtle-wow.org/priest)

```text
Discipline (21 points):
- 5/5 Unbreakable Will (+15% Stun, Fear, Silence resist)
- 3/3 Improved Power Word: Shield
- 3/3 Meditation (+15% Mana regeneration continues while casting)
- 1/1 Inner Focus (Next spell has 100% reduced mana cost and +25% Critical chance)
- 1/1 Divine Spirit (+50 Spirit to target)

Holy (30 points):
- 5/5 Holy Specialization (+5% Holy Spell Crit)
- 5/5 Divine Fury (Reduces Greater Heal & Smite cast time by 0.5s)
- 3/3 Improved Renew
- 1/1 Desperate Prayer (Instant self-heal panic button)
- 5/5 Spiritual Guidance (+25% of total Spirit added as Bonus Healing)
- 5/5 Spiritual Healing (+10% Healing done by all spells)
- 3/3 Inspiration (Critical heals grant target +25% Armor for 15s)
```

- **Stat Priority**: +Healing > Spirit > MP5 > Intellect > Spell Crit.
- **Rotation**: Flash Heal (for emergency triage) > Greater Heal (Rank 1 or 4 for efficient tank healing) > Renew (on taking damage) > Prayer of Healing (for party AOE damage).
- **Turtle WoW 1.18.1 Rework**:
  - **Lightwell**: Reworked to not break upon taking damage, scaling fully with +healing power.
  - **Inner Fire**: Grants **Spell Damage** instead of Attack Power, boosting caster scaling.

---

### Spec 13: Shadow Priest (13/0/38) - Raid Debuffer & Mana Battery

Applies *Shadow Weaving* (+15% Shadow damage taken by boss for all Warlocks in raid) and *Vampiric Embrace* (heals party for 25% and restores 2% mana of shadow damage dealt).

- **Calculator Template**: [Priest Shadow (13/0/38)](https://talents.turtle-wow.org/priest)

```text
Discipline (13 points):
- 5/5 Unbreakable Will
- 3/3 Meditation
- 1/1 Inner Focus

Shadow (38 points):
- 5/5 Spirit Tap
- 5/5 Darkness (+10% Shadow Spell Damage)
- 5/5 Shadow Focus (+10% Spell Hit on Shadow spells)
- 1/1 Mind Flay
- 2/2 Shadow Reach (+20% Range)
- 5/5 Shadow Weaving (Stacks 5 times to +15% Shadow damage taken)
- 1/1 Silence
- 1/1 Vampiric Embrace (Party heals for 25% + restores 2% mana of shadow spell damage)
- 1/1 Shadowform (+15% Physical damage reduction, +15% Shadow damage bonus, +15% in-combat mana regen)
```

- **Rotation**: Shadow Word: Pain > Mind Flay (channel) > Mind Blast on cooldown > Pain Spike on movement.

---

## 7. Paladin Talent Specs

🔗 **Interactive Calculator**: [https://talents.turtle-wow.org/paladin](https://talents.turtle-wow.org/paladin)

### Spec 14: Holy PvE Raid Healer (31/20/0) - Infinite Mana Tank Healer

*Illumination* refunds 100% of the mana cost whenever Flash of Light or Holy Light critically strikes, granting Holy Paladins near infinite mana pools on raid bosses.

- **Calculator Template**: [Paladin Holy Healer (31/20/0)](https://talents.turtle-wow.org/paladin)

```text
Holy (31 points):
- 5/5 Divine Intellect (+10% Intellect)
- 5/5 Spiritual Focus (70% pushback resistance on Flash/Holy Light)
- 3/3 Healing Light (+12% Healing done by Holy Light & Flash of Light)
- 5/5 Illumination (100% Mana refund on spell critical heals)
- 1/1 Divine Favor (Next Holy Light or Flash of Light is a guaranteed 100% Critical Strike)
- 1/1 Holy Shock (Instant holy damage/heal strike)

Protection (20 points):
- 5/5 Redoubt (+30% Block chance after being hit by a critical strike)
- 5/5 Toughness (+10% Armor)
- 1/1 Blessing of Kings (+10% All Stats to target)
- 3/3 Improved Righteous Fury (+50% Threat on Holy spells)
- 5/5 Shield Specialization (+30% Block Value)
```

- **Stat Priority**: Spell Crit (to trigger Illumination mana refunds) > +Healing > Intellect > MP5.
- **Rotation**: Flash of Light spam on assigned tank > Holy Light with Divine Favor during high burst damage > Keep *Blessing of Kings / Light / Salvation* active.
- **Turtle WoW 1.18.1 Features**:
  - **Hand Spells**: Freedom, Protection, and Sacrifice are converted to "Hand" spells, allowing them to be cast without removing class Greater Blessings.
  - **Extended Blessings**: Greater Blessings have longer duration to reduce rebuffing overhead.

---

### Spec 15: Retribution Paladin (11/0/40) - 2H Melee Burst DPS

Uses 2H Weapon with *Crusader Strike*, *Holy Strike*, *Seal of Command* procs, and *Consecration* for holy melee damage.

- **Calculator Template**: [Paladin Retribution (11/0/40)](https://talents.turtle-wow.org/paladin)

```text
Holy (11 points):
- 5/5 Divine Strength (+10% Strength)
- 1/1 Consecration (Holy ground AOE damage)

Retribution (40 points):
- 5/5 Benediction (Reduces mana cost of Judgements/Seals by 15%)
- 2/2 Improved Judgement (Reduces Judgement CD by 2s)
- 1/1 Seal of Command (Chance on melee hit to inflict 70% weapon damage as Holy)
- 5/5 Conviction (+5% Melee Critical Strike)
- 1/1 Sanctity Aura (+10% Holy Damage to party)
- 3/3 Two-Handed Weapon Specialization (+6% 2H Weapon Damage)
- 5/5 Vengeance (+15% Physical & Holy damage for 8s after a critical strike)
- 1/1 Repentance (6-second humanoid incapacitate)
```

- **Turtle WoW 1.18.1 Additions**:
  - **Crusader Strike & Holy Strike**: Integrated as core rotational strikes sharing a 6s cooldown. Crusader Strike provides offensive holy damage and refreshes Judgements; Holy Strike provides holy burst with healing/mana sustain.
  - **Righteous Defense / Hand of Reckoning**: Baseline taunt ability for tanking viability.

---

## 8. Shaman Talent Specs

🔗 **Interactive Calculator**: [https://talents.turtle-wow.org/shaman](https://talents.turtle-wow.org/shaman)

### Spec 16: Restoration Raid Healer (0/12/39) - Chain Heal Master

The ultimate raid healing spec. *Chain Heal* bounces between injured allies while *Mana Tide Totem* restores massive mana to your caster group.

- **Calculator Template**: [Shaman Restoration (0/12/39)](https://talents.turtle-wow.org/shaman)

```text
Enhancement (12 points):
- 5/5 Shield Specialization (+5% Block, +25% Block Value)
- 5/5 Thundering Strikes (+5% Melee/Spell Crit)

Restoration (39 points):
- 5/5 Improved Healing Wave (Reduces Healing Wave cast time by 0.5s)
- 5/5 Tidal Focus (Reduces healing spell mana cost by 5%)
- 5/5 Tidal Mastery (+5% Critical strike chance with healing spells)
- 1/1 Nature's Swiftness (Next nature spell is instant cast)
- 3/3 Healing Grace (Reduces threat generated by healing spells by 15%)
- 2/2 Improved Chain Heal (+10% Healing done by Chain Heal)
- 1/1 Mana Tide Totem (Restores 290 Mana every 3 seconds for 12 seconds to party)
```

- **Stat Priority**: +Healing > MP5 > Intellect > Spell Crit.
- **Rotation**: Chain Heal (Rank 1 for efficiency, Rank 3 for burst) > Nature's Swiftness + Healing Wave (emergency tank save) > Drop *Windfury Totem*, *Mana Spring Totem*, and *Grace of Air*.

---

### Spec 17: Shaman Tank "Tankhance" (19/32/0) - Turtle-WoW Custom Tank

A fully realized mail/shield tank utilizing high threat nature damage, shield mitigation stacks, and short-cast fire spells.

- **Calculator Template**: [Shaman Tankhance (19/32/0)](https://talents.turtle-wow.org/shaman)

```text
Elemental (19 points):
- 5/5 Convection (Reduces Shock & Molten Blast mana cost by 10%)
- 5/5 Concussion (+5% Damage to Shocks and Lightning)
- 1/1 Elemental Focus (Clearcasting on spell hit)
- 5/5 Call of Thunder (+6% Spell Crit)

Enhancement (32 points):
- 5/5 Shield Specialization (+5% Block, +25% Block Value)
- 5/5 Anticipation / Toughness
- 5/5 Thundering Strikes (+5% Crit)
- 1/1 Earthen Bulwark (Generates stacking damage mitigation charges on melee swings)
- 5/5 Flurry (+30% Attack Speed)
- 1/1 Stormstrike (Extra melee attack causing next 2 Nature damage spells to deal +20%)
```

- **Stat Priority**: Defense (440 cap) > Hit (9%) > Stamina > Armor > Shield Block Value > Agility.
- **Rotation**: Rockbiter Weapon active > Molten Blast on pull / add spawn > Earth Shock on CD > Stormstrike on CD > Lightning Shield for threat, swap to Water Shield when Clearcasting procs for mana recovery.

---

## 9. Druid Talent Specs

🔗 **Interactive Calculator**: [https://talents.turtle-wow.org/druid](https://talents.turtle-wow.org/druid)

### Spec 18: Feral Tank & Cat DPS (14/32/5) - Heart of the Wild Hybrid

Combines massive health and armor in Dire Bear Form with high physical DPS in Cat Form using *Heart of the Wild* (+20% Stamina in Bear, +20% Strength in Cat).

- **Calculator Template**: [Druid Feral Tank/DPS (14/32/5)](https://talents.turtle-wow.org/druid)

```text
Balance (14 points):
- 1/1 Nature's Grasp
- 5/5 Natural Weapons (+10% Physical Damage)
- 1/1 Omen of Clarity (Chance on melee hit to enter Clearcasting state)

Feral (32 points):
- 5/5 Ferocity (Reduces Maul, Swipe, Claw, Rake cost by 5)
- 2/2 Feral Instinct (+15% Threat in Bear Form, improves stealth)
- 1/1 Feral Charge (Interrupts spell casting and roots target for 4s)
- 3/3 Sharpened Claws (+6% Melee Crit)
- 3/3 Predatory Strikes (+150% Attack Power based on level)
- 2/2 Blood Frenzy
- 2/2 Primal Fury (Generates 5 extra rage on Bear crits, +1 combo point on Cat crits)
- 1/1 Faerie Fire (Feral) (Reduces target armor and prevents stealth)
- 5/5 Heart of the Wild (+20% Intellect, +20% Stamina in Bear, +20% Strength in Cat)
- 1/1 Leader of the Pack (+3% Melee & Ranged Critical Strike to party)

Restoration (5 points):
- 5/5 Furor (Gain 10 Rage when shifting into Bear, 40 Energy in Cat)
```

- **Stat Priority (Bear Tank)**: Armor (to 75% hard cap) > Stamina > Agility (for Dodge) > Hit (9%) > Defense.
- **Stat Priority (Cat DPS)**: Hit (9%) > Strength > Agility > Attack Power > Crit.
- **Rotation (Bear Tank)**: Maul on every swing > Swipe for multi-target threat > Faerie Fire (Feral) on pull.
- **Rotation (Cat DPS)**: Shred from behind > Rake (maintain buffed bleed) > Ferocious Bite at 5 Combo Points.
- **Turtle WoW 1.18.1 Enhancements**:
  - **Windfury Compatibility**: Windfury functions as an aura, allowing Feral Druids in Cat and Bear forms to benefit from extra attacks.
  - **Bleed Scaling**: Rake buffed to make bleed playstyles competitive without total reliance on Wolfshead Helm powershifting.

---

### Spec 19: Balance Moonkin (31/0/20) - Reworked Caster DPS

High burst Arcane and Nature damage providing spell critical aura and sustained mana returns to the caster group.

- **Calculator Template**: [Druid Balance Moonkin (31/0/20)](https://talents.turtle-wow.org/druid)

```text
Balance (31 points):
- 5/5 Nature's Grasp
- 5/5 Natural Weapons
- 1/1 Omen of Clarity (Clearcasting)
- 3/3 Nature's Reach (+20% Range)
- 5/5 Vengeance (+100% Critical Strike Damage bonus for Starfire, Wrath, Moonfire)
- 1/1 Nature's Grace (Spell criticals reduce cast time of next spell by 0.5s)
- 5/5 Moonfury (+10% Damage to Starfire, Wrath, Moonfire)
- 1/1 Moonkin Form (Learned at Level 30: +3% Spell Crit to party, regenerates mana on spell hits)

Restoration (20 points):
- 5/5 Improved Mark of the Wild
- 5/5 Furor
- 3/3 Reflection (+15% Mana regen while casting)
- 5/5 Tranquil Spirit (Reduces mana cost of Healing Touch by 10%)
- 1/1 Insect Swarm (Baseline / Talented nature DoT with hit reduction)
```

- **Stat Priority**: Spell Hit (16% cap) > Spell Damage > Spell Crit > Intellect.
- **Rotation**: Starfire / Wrath cycle > Moonfire (maintain DoT) > Insect Swarm > Hurricane (no 1-min cooldown) for multi-mob AoE.

---

[⬅ Prev: 17. Tier Sets & Endgame Gear](./17_tier_items_and_endgame_gear.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 00. Self Commands ➡](./00_self_and_player_commands.md)
