# 18. Class Talent Specs & Raid Meta Builds

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [⚡ Self Commands (00_self_and_player_commands.md)](./00_self_and_player_commands.md) • [🎒 Tier Items (17_tier_items_and_endgame_gear.md)](./17_tier_items_and_endgame_gear.md) • [⚔️ Bot Tactics (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md)

---

## 📚 Complete Module Reference Library

| Self & Accounts | Server & World | Gameplay & Progression | Bots & Automation |
| :--- | :--- | :--- | :--- |
| [00. Self & Player Commands](./00_self_and_player_commands.md) | [02. Server & Reloads](./02_server_and_reloads.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [01. Security & Accounts](./01_security_and_account.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | [17. Tier Sets & Endgame Gear](./17_tier_items_and_endgame_gear.md) | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |
| [10. Turtle Custom](./10_turtle_custom_features.md) | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) | **[18. Class Talent Specs](./18_class_talent_specs.md)** | [13. Macro Book](./13_bot_whispers_and_macros.md) |

---

## 1. Warrior Talent Specs

### 🛡️ 1. Dual-Wield Fury/Prot Tank (0/31/20) - Max Raid Threat BiS

The definitive high-end raid tanking spec in Vanilla / Turtle-WoW. Uses Dual-Wield weapons in Defensive Stance to generate colossal rage and threat per second (TPS) so Fury Warriors, Rogues, and Fire Mages can DPS at 100% capacity without pulling aggro.

```
Arms (0 points):
(None)

Fury (31 points):
- 5/5 Cruelty (+5% Melee Critical Strike)
- 5/5 Unbridled Wrath (Chance to gain 1 additional rage on melee swing)
- 5/5 Dual Wield Specialization (+25% Off-Hand weapon damage)
- 5/5 Enrage (+25% Melee damage bonus for 12s after being hit by a critical strike)
- 1/1 Death Wish (+20% Physical damage, Fear immunity for 30s)
- 5/5 Flurry (+30% Attack Speed for next 3 swings on critical strike)
- 4/5 Blood Craze or Impale
- 1/1 Bloodthirst (Instant strike for 45% Attack Power damage + heals for 30 HP per hit)

Protection (20 points):
- 5/5 Shield Specialization (Gain 1 rage when blocking, +5% block chance)
- 2/2 Anticipation (+10 Defense Skill) or 5/5 Toughness (+10% Armor from items)
- 1/1 Last Stand (+30% Maximum Health for 20s - essential panic recovery button)
- 5/5 Defiance (+15% Additional Threat in Defensive Stance)
- 1/1 Improved Taunt (Reduces Taunt cooldown by 2 seconds)
- 3/3 Improved Sunder Armor (Reduces Sunder Armor rage cost by 3)
```

- **Stat Priority**: Hit Cap (6% with +Weapon Skill, 9% without) > Weapon Skill (305+) > Crit > Agility > Strength > Stamina > Armor.
- **Rotation**: Stay in **Defensive Stance**. Bloodthirst on CD > Revenge on proc > Heroic Strike queue on main hand swing > Sunder Armor (maintain 5 stacks). Pop *Death Wish* + *Diamond Flask* at the start of boss encounters for immediate snap aggro.

---

### 🛡️ 2. Deep Protection Mitigation Tank (11/5/35) - Progression & Hardcore

Maximum physical and magic mitigation. Uses 1H Weapon + Shield with Shield Slam for reliable snap threat and Concussion Blow for dungeon trash control.

```
Arms (11 points):
- 5/5 Deflection (+5% Parry)
- 5/5 Tactical Mastery (Retain up to 25 Rage when switching stances)
- 1/1 Anger Management (Generates 1 rage every 3 seconds)

Fury (5 points):
- 5/5 Cruelty (+5% Melee Critical Strike)

Protection (35 points):
- 5/5 Shield Specialization
- 5/5 Toughness (+10% Armor)
- 1/1 Last Stand
- 2/2 Improved Bloodrage (Generates 5 immediate extra rage)
- 5/5 Defiance (+15% Threat in Defensive Stance)
- 2/2 Improved Taunt
- 3/3 Improved Sunder Armor
- 1/1 Concussion Blow (5-second physical stun)
- 1/1 One-Handed Weapon Specialization (+10% 1H Damage)
- 1/1 Shield Slam (High damage/threat slam based on shield block value + 50% dispel)
```

- **Stat Priority**: Defense (440 to reach uncrittable cap) > Hit (9%) > Stamina > Armor > Shield Block Value > Agility.
- **Rotation**: Shield Slam on CD > Revenge on CD > Sunder Armor > Shield Block (maintain active against physical crushing blows).

---

### ⚔️ 3. Dual-Wield Fury DPS (17/34/0) - #1 Physical DPS Spec

The highest sustained melee damage spec in Vanilla WoW. Combines Deep Wounds bleeding and Impale critical multiplier from Arms with Bloodthirst and Flurry attack speed in Fury.

```
Arms (17 points):
- 3/3 Rend
- 2/2 Deep Wounds (Crits cause 60% average weapon damage bleed over 12s)
- 2/2 Impale (+20% Critical Strike Damage bonus)
- 2/2 Overpower (+50% Crit chance on Overpower against dodging targets)
- 5/5 Tactical Mastery (Retain rage when stance dancing for Overpower/Berserker Rage)
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

- **Stat Priority**: Hit Cap (6% / 9%) > Weapon Skill (305+) > Crit > Strength > Attack Power > Agility.
- **Rotation**: Bloodthirst on CD > Whirlwind on CD > Overpower on dodge > Heroic Strike (queue when Rage > 40 to prevent off-hand miss penalty).

---

## 2. Rogue Talent Specs

### 🗡️ 4. Combat Swords Rogue (19/32/0) - Sustained Raiding BiS

Maximum sustained physical DPS using fast off-hand and slow high-damage main-hand swords.

```
Assassination (19 points):
- 5/5 Malice (+5% Crit)
- 3/3 Ruthlessness (60% chance to add 1 combo point after finisher)
- 1/1 Relentless Strikes (Finishing moves have 20% chance per CP to restore 25 Energy)
- 5/5 Lethality (+30% Crit damage bonus for Sinister Strike, Gouge, Backstab)
- 5/5 Murder (+2% Damage vs Humanoid, Giants, Beasts, Dragonkin)

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

---

### 🗡️ 5. Combat Daggers Rogue (15/31/5) - High Burst Boss DPS

Position-dependent high-burst spec requiring Backstab from behind the boss.

```
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

### 🔥 6. Ignite Fire Mage (17/31/3) - AQ40 & Naxxramas #1 Caster DPS

High rolling Ignite damage. When multiple Fire Mages crit, they stack a single massive rolling Ignite DoT on the boss that ticks for tens of thousands of damage.

```
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

---

### ❄️ 7. Arcane Power Frost Mage (28/0/23) - MC & BWL Progression

Maximum burst frost damage for Molten Core and Blackwing Lair where mobs have 100% Fire Immunity.

```
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

### 💀 8. DS/Ruin Warlock (7/21/23 or 9/21/21) - Top Raiding Shadow DPS

Sacrifices the Succubus minion via *Demonic Sacrifice* to gain a permanent +15% Shadow Damage multiplier, combined with *Ruin* for +100% critical strike bonus.

```
Affliction (7 points):
- 5/5 Improved Corruption (Instant cast Corruption)
- 2/2 Suppression (+4% Hit with Affliction spells)

Demonology (21 points):
- 5/5 Demonic Embrace (+15% Total Stamina)
- 5/5 Improved Voidwalker or Imp
- 5/5 Fel Intellect / Fel Stamina
- 1/1 Demonic Sacrifice (Sacrifice Succubus: +15% Shadow Damage bonus permanently until re-summoned)

Destruction (23 points):
- 5/5 Improved Shadow Bolt (Shadow Bolt crits grant +20% Shadow damage to target for 4 hits)
- 5/5 Bane (Reduces Shadow Bolt cast time by 0.5s)
- 5/5 Devastation (+5% Spell Crit with Destruction spells)
- 1/1 Shadowburn (Instant cast emergency burst strike)
- 1/1 Ruin (+100% Critical Strike Damage bonus for Destruction spells)
```

- **Stat Priority**: Spell Hit (16% cap) > Shadow Spell Damage > Spell Crit > Stamina.
- **Rotation**: Curse of Shadows / Curse of Elements (assigned by raid lead) > Shadow Bolt spam > Life Tap to manage mana.

---

### 💀 9. SM/Ruin Affliction Warlock (30/0/21) - Utility & Farming BiS

Provides *Nightfall* instant Shadow Bolt procs and *Shadow Mastery* (+10% Shadow damage) without needing to sacrifice your demon pet.

```
Affliction (30 points):
- 5/5 Improved Corruption
- 5/5 Fel Concentration (70% pushback resistance on Drain Life)
- 2/2 Nightfall (4% chance on Corruption/Drain Life ticks to grant instant cast Shadow Bolt)
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

### 🏹 10. Marksmanship PvE Hunter (20/31/0) - Raid Ranged DPS

Maximum ranged physical damage using Aimed Shot and Multi-Shot weave between auto-shots.

```
Beast Mastery (20 points):
- 5/5 Improved Aspect of the Hawk (+30% Ranged attack speed proc)
- 5/5 Thick Hide
- 5/5 Unleashed Fury (+20% Pet Damage)
- 5/5 Ferocity (+15% Pet Critical Strike)

Marksmanship (31 points):
- 5/5 Efficiency (Reduces Shot mana cost by 10%)
- 5/5 Lethal Shots (+5% Ranged Critical Strike)
- 1/1 Aimed Shot (3-second heavy physical shot)
- 5/5 Mortal Shots (+30% Ranged Critical Damage bonus)
- 3/3 Barrage (+15% Multi-Shot damage)
- 2/2 Ranged Weapon Specialization (+5% Ranged Weapon Damage)
- 1/1 Trueshot Aura (+100 Attack Power to all party members)
```

- **Stat Priority**: Hit Cap (9%) > Agility > Ranged Attack Power > Crit.
- **Rotation**: **Auto-Shot Weave**: Auto-Shot > Aimed Shot > Auto-Shot > Multi-Shot > Auto-Shot. Never clip auto-shot cast timer!

---

## 6. Priest Talent Specs

### ✝️ 11. Holy / Discipline Raid Healer (21/30/0) - The Gold Standard

Provides *Meditation* (+15% mana regen while casting), *Inner Focus* (free instant cast with +25% crit), and *Divine Spirit* alongside massive Holy healing bonuses.

```
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

---

### 🔮 12. Shadow Priest (13/0/38) - Raid Debuffer & Mana Battery

Applies *Shadow Weaving* (+15% Shadow damage taken by boss for all Warlocks in raid) and *Vampiric Embrace* (heals party for 20% of shadow damage dealt).

```
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
- 5/5 Shadow Weaving (Shadow damage has 100% chance to cause target to take +3% Shadow damage, stacks 5 times to +15%)
- 1/1 Silence
- 1/1 Vampiric Embrace (Party heals for 20% of shadow spell damage)
- 1/1 Shadowform (+15% Physical damage reduction, +15% Shadow damage bonus)
```

- **Rotation**: Shadow Word: Pain > Mind Flay (channel) > Mind Blast on cooldown.

---

## 7. Paladin Talent Specs

### 🔨 13. Holy PvE Raid Healer (31/20/0) - Infinite Mana Tank Healer

*Illumination* refunds 100% of the mana cost whenever Flash of Light or Holy Light critically strikes, granting Holy Paladins near infinite mana pools on raid bosses.

```
Holy (31 points):
- 5/5 Divine Strength (+10% Strength) or Divine Intellect (+10% Intellect)
- 5/5 Spiritual Focus (70% pushback resistance on Flash/Holy Light)
- 3/3 Healing Light (+12% Healing done by Holy Light & Flash of Light)
- 5/5 Illumination (100% Mana refund on spell critical heals)
- 1/1 Divine Favor (Next Holy Light or Flash of Light is a guaranteed 100% Critical Strike)
- 1/1 Holy Shock (Instant holy damage/heal strike)

Protection (20 points):
- 5/5 Redoubt (+30% Block chance after being hit by a critical strike)
- 5/5 Toughness (+10% Armor)
- 1/1 Blessing of Kings (+10% All Stats to target - essential raid blessing)
- 3/3 Improved Righteous Fury (+50% Threat on Holy spells)
- 5/5 Shield Specialization (+30% Block Value)
```

- **Stat Priority**: Spell Crit (to trigger Illumination mana refunds) > +Healing > Intellect > MP5.
- **Rotation**: Flash of Light spam on assigned tank > Holy Light with Divine Favor during high burst damage > Keep *Blessing of Kings / Light / Salvation* active.

---

### 🔨 14. Retribution Paladin (11/0/40) - 2H Melee DPS & PvP

Uses 2H Weapon with *Seal of Command* procs and *Consecration* for holy melee damage.

```
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

---

## 8. Shaman Talent Specs

### ⚡ 15. Restoration Raid Healer (0/12/39) - Chain Heal Master

The ultimate raid healing spec. *Chain Heal* bounces between injured allies while *Mana Tide Totem* restores massive mana to your caster group.

```
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

### ⚡ 16. Elemental Shaman (30/0/21) - Caster Burst DPS

Fast lightning bolt casts and guaranteed spell critical bursts using *Elemental Mastery*.

```
Elemental (30 points):
- 5/5 Convection (Reduces Shock & Lightning mana cost by 10%)
- 5/5 Concussion (+5% Damage to Shock and Lightning spells)
- 1/1 Elemental Focus (Clearcasting - 10% chance on spell hit to enter Freecast)
- 5/5 Call of Thunder (+6% Spell Crit to Lightning spells)
- 5/5 Lightning Mastery (Reduces Lightning Bolt & Chain Lightning cast time by 1.0s)
- 1/1 Elemental Mastery (Next Fire, Frost, or Nature spell is guaranteed 100% Critical Strike)

Restoration (21 points):
- 5/5 Tidal Focus
- 5/5 Tidal Mastery (+5% Spell Crit)
- 1/1 Nature's Swiftness
```

---

## 9. Druid Talent Specs

### 🐾 17. Feral Tank & Cat DPS (Heart of the Wild 14/32/5) - Hybrid BiS

Combines massive health and armor in Dire Bear Form with high physical DPS in Cat Form using *Heart of the Wild* (+20% Stamina in Bear, +20% Strength in Cat).

```
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
- **Rotation (Cat DPS)**: Shred from behind > Ferocious Bite at 5 Combo Points > Powershift (re-shifting into Cat with Furor for +40 instant Energy).

---

### 🐾 18. Restoration Moonglow Healer (24/0/27) - Efficient Raid Healing

Uses *Moonglow* (-9% mana cost on Healing Touch) and *Nature's Grace* (reduces cast time by 0.5s on spell crit) for the most mana-efficient healing in long raid fights.

```
Balance (24 points):
- 5/5 Nature's Grasp
- 5/5 Natural Weapons
- 1/1 Omen of Clarity
- 3/3 Nature's Reach (+20% Range)
- 3/3 Moonglow (Reduces mana cost of Healing Touch, Regrowth, Rejuvenation by 9%)
- 1/1 Nature's Grace (Spell criticals reduce cast time of next spell by 0.5s)

Restoration (27 points):
- 5/5 Furor
- 5/5 Improved Healing Touch (Reduces cast time of Healing Touch by 0.5s)
- 3/3 Reflection (+15% Mana regeneration continues while casting)
- 5/5 Tranquil Spirit (Reduces mana cost of Healing Touch by 10%)
- 1/1 Nature's Swiftness (Next nature spell is instant cast)
```

- **Stat Priority**: +Healing > MP5 > Spirit > Intellect.
- **Rotation**: Healing Touch (Rank 4 for continuous tank maintenance) > Rejuvenation > Nature's Swiftness + Max Rank Healing Touch for emergency save.

---

<p align="center">
  [⬅ Prev: 17. Tier Sets & Endgame Gear](./17_tier_items_and_endgame_gear.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 00. Self Commands ➡](./00_self_and_player_commands.md)
</p>
