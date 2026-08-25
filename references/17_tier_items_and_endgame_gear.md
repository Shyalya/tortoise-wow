# 17. Tier Sets & Endgame Equipment Directory

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [⚡ Self Commands (00_self_and_player_commands.md)](./00_self_and_player_commands.md) • [🎒 Items & Economy (07_items_economy_and_mail.md)](./07_items_economy_and_mail.md) • [🎯 Talent Specs (18_class_talent_specs.md)](./18_class_talent_specs.md) • [🤖 Bot Suite (11_playerbot_suite.md)](./11_playerbot_suite.md)

---

### 📚 Complete Module Reference Library

| Self & Accounts | Server & World | Gameplay & Progression | Bots & Automation |
| :--- | :--- | :--- | :--- |
| [00. Self & Player Commands](./00_self_and_player_commands.md) | [02. Server & Reloads](./02_server_and_reloads.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [01. Security & Accounts](./01_security_and_account.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| [09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md) | [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | **[17. Tier Sets & Endgame Gear](./17_tier_items_and_endgame_gear.md)** | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |
| [10. Turtle Custom](./10_turtle_custom_features.md) | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) | **[18. Class Talent Specs](./18_class_talent_specs.md)** | [13. Macro Book](./13_bot_whispers_and_macros.md) |

---

## 1. 1-Click BiS Class Loadouts (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`) or chat box to instantly equip or populate your bags with the definitive Best-in-Slot endgame setup for your class/spec:

### 🛡️ Protection Warrior (Main Tank BiS)
Full 9-piece Dreadnaught, Thunderfury, The Face of Death shield, Drake Fang Talisman, Styleen's Impeding Scarab, Band of Accuria, and Prestor's Talisman:
```lua
.additem 22416 1
.additem 22417 1
.additem 22418 1
.additem 22419 1
.additem 22420 1
.additem 22421 1
.additem 22422 1
.additem 22423 1
.additem 23059 1
.additem 19019 1
.additem 23043 1
.additem 19406 1
.additem 19431 1
.additem 17063 1
.additem 19377 1
.repairitems
```

### ⚔️ Dual-Wield Fury/Prot Tank Warrior (Max Threat BiS)
Thunderfury (MH), The Hungering Cold (OH), Drake Fang Talisman, Kiss of the Spider, Band of Accuria, Ring of the Fallen God, and Prestor's Talisman:
```lua
.additem 22416 1
.additem 22418 1
.additem 22419 1
.additem 22420 1
.additem 19019 1
.additem 23577 1
.additem 19406 1
.additem 22954 1
.additem 17063 1
.additem 21709 1
.additem 19377 1
.additem 22811 1
.repairitems
```

### ⚔️ Fury Warrior (Dual-Wield Melee DPS BiS)
Gressil (Kel'Thuzad MH), The Hungering Cold (Kel'Thuzad OH), Kiss of the Spider, Drake Fang Talisman, Band of Accuria, Ring of the Fallen God, and Prestor's Talisman:
```lua
.additem 22418 1
.additem 22419 1
.additem 22420 1
.additem 23054 1
.additem 23577 1
.additem 22954 1
.additem 19406 1
.additem 21709 1
.additem 17063 1
.additem 19377 1
.additem 22811 1
.repairitems
```

### 🔨 2-Handed Arms / Slam Warrior (Two-Handed Physical DPS BiS)
Might of Menethil (2H Mace), Dreadnaught gear, Kiss of the Spider, Drake Fang Talisman, Band of Accuria, Ring of the Fallen God, Prestor's Talisman, and Soulstring:
```lua
.additem 22416 1
.additem 22417 1
.additem 22418 1
.additem 22419 1
.additem 22420 1
.additem 22421 1
.additem 22798 1
.additem 22954 1
.additem 19406 1
.additem 21709 1
.additem 17063 1
.additem 19377 1
.additem 22811 1
.repairitems
```

### 🗡️ Combat / Daggers Rogue (Physical DPS BiS)
Full 9-piece Bonescythe, Kingsfall (Kel'Thuzad Dagger), Harbinger of Doom, Kiss of the Spider, Badge of the Swarmguard, Band of Accuria, and Nerubian Slavemaker:
```lua
.additem 22476 1
.additem 22477 1
.additem 22478 1
.additem 22479 1
.additem 22480 1
.additem 22481 1
.additem 22482 1
.additem 22483 1
.additem 23060 1
.additem 22802 1
.additem 23044 1
.additem 22954 1
.additem 21670 1
.additem 22812 1
.repairitems
```

### 🏹 Marksmanship Hunter (Ranged DPS BiS)
Full 9-piece Cryptstalker, Nerubian Slavemaker, The Eye of Nerub (2H Polearm), Kiss of the Spider, Drake Fang Talisman, Band of Accuria, and Ring of the Fallen God:
```lua
.additem 22436 1
.additem 22437 1
.additem 22438 1
.additem 22439 1
.additem 22440 1
.additem 22441 1
.additem 22442 1
.additem 22443 1
.additem 23067 1
.additem 22812 1
.additem 23039 1
.additem 22954 1
.additem 19406 1
.additem 21709 1
.repairitems
```

### 🔥 Fire / Frost Mage (Caster DPS BiS)
Full 9-piece Frostfire, Atiesh Greatstaff of the Guardian (Mage), Neltharion's Tear, Restrained Essence of Sapphiron, Band of Unnatural Forces, Ring of the Fallen God, Gem of Trapped Innocents, and Wand of the Whispering Dead:
```lua
.additem 22496 1
.additem 22497 1
.additem 22498 1
.additem 22499 1
.additem 22500 1
.additem 22501 1
.additem 22502 1
.additem 22503 1
.additem 23062 1
.additem 22589 1
.additem 19379 1
.additem 23046 1
.additem 23038 1
.additem 21709 1
.additem 23057 1
.additem 23009 1
.repairitems
```

### 💀 Affliction / Destruction Warlock (Shadow DPS BiS)
Full 9-piece Plagueheart, Atiesh Greatstaff of the Guardian (Warlock), Neltharion's Tear, Restrained Essence of Sapphiron, Band of Unnatural Forces, Gem of Trapped Innocents, and Wand of the Whispering Dead:
```lua
.additem 22504 1
.additem 22505 1
.additem 22506 1
.additem 22507 1
.additem 22508 1
.additem 22509 1
.additem 22510 1
.additem 22511 1
.additem 23063 1
.additem 22630 1
.additem 19379 1
.additem 23046 1
.additem 23038 1
.additem 23057 1
.additem 23009 1
.repairitems
```

### ✝️ Holy Priest (Raid Healer BiS)
Full 9-piece Vestments of Faith, Atiesh Greatstaff of the Guardian (Priest), Eye of the Dead, Rejuvenating Gem, Ring of Faith, Gem of Trapped Innocents, and Wand of the Whispering Dead:
```lua
.additem 22512 1
.additem 22513 1
.additem 22514 1
.additem 22515 1
.additem 22516 1
.additem 22517 1
.additem 22518 1
.additem 22519 1
.additem 23061 1
.additem 22631 1
.additem 23047 1
.additem 19395 1
.additem 23057 1
.additem 23009 1
.repairitems
```

### 🔨 Holy / Retribution Paladin (Support & Healer BiS)
Full 9-piece Redemption Armor, The End of Dreams (Kel'Thuzad 1H Mace), Shield of Condemnation, Eye of the Dead, Rejuvenating Gem, and Ring of Redemption:
```lua
.additem 22424 1
.additem 22425 1
.additem 22426 1
.additem 22427 1
.additem 22428 1
.additem 22429 1
.additem 22430 1
.additem 22431 1
.additem 23066 1
.additem 22988 1
.additem 22819 1
.additem 23047 1
.additem 19395 1
.repairitems
```

### ⚡ Restoration / Elemental Shaman (Healer & Caster BiS)
Full 9-piece The Earthshatterer, The End of Dreams, Shield of Condemnation, Eye of the Dead, Neltharion's Tear, and Ring of the Earthshatterer:
```lua
.additem 22464 1
.additem 22465 1
.additem 22466 1
.additem 22467 1
.additem 22468 1
.additem 22469 1
.additem 22470 1
.additem 22471 1
.additem 23065 1
.additem 22988 1
.additem 22819 1
.additem 23047 1
.additem 19379 1
.repairitems
```

### 🐾 Restoration / Feral Druid (Healer & Tank BiS)
Full 9-piece Dreamwalker Raiment, Atiesh Greatstaff of the Guardian (Druid), Eye of the Dead, Rejuvenating Gem, Ring of the Dreamwalker, and Shroud of Dominion:
```lua
.additem 22488 1
.additem 22489 1
.additem 22490 1
.additem 22491 1
.additem 22492 1
.additem 22493 1
.additem 22494 1
.additem 22495 1
.additem 23064 1
.additem 22632 1
.additem 23047 1
.additem 19395 1
.additem 23045 1
.repairitems
```

---

## 2. Legendary & Artifact Weapons Directory

| Item ID | Item Name | Slot | Quality | Unique Effects & Description |
| :---: | :--- | :---: | :---: | :--- |
| **`19019`** | **Thunderfury, Blessed Blade of the Windseeker** | 1H Sword | Legendary | Chance on hit: 300 Nature blast + chain lightning debuff (-25 Nature resist, -20% attack speed). Highest threat weapon in the game. |
| **`17182`** | **Sulfuras, Hand of Ragnaros** | 2H Mace | Legendary | Chance on hit: 273–333 Fireball + 5 Fire damage shield. 223–372 Physical damage (80.4 DPS, 3.70 speed). |
| **`22589`** | **Atiesh, Greatstaff of the Guardian (Mage)** | 2H Staff | Legendary | +28 Spell Power to group, +2% Spell Crit to group, Karazhan teleport portal on 1-min cooldown. |
| **`22630`** | **Atiesh, Greatstaff of the Guardian (Warlock)** | 2H Staff | Legendary | +33 Spell Damage & Healing to group, +2% Spell Hit to group, Karazhan teleport portal. |
| **`22631`** | **Atiesh, Greatstaff of the Guardian (Priest)** | 2H Staff | Legendary | +62 Healing to group, +11 MP5 to group, Karazhan teleport portal. |
| **`22632`** | **Atiesh, Greatstaff of the Guardian (Druid)** | 2H Staff | Legendary | +11 Feral Attack Power to group, +44 Healing to group, +11 MP5 to group, Karazhan teleport portal. |
| **`22691`** | **Corrupted Ashbringer** | 2H Sword | Epic | 259–389 Damage (90.0 DPS, 3.60 speed), +2% Crit, +2% Hit, Chance on Hit: Life Steal 185–215. Triggers Scarlet Monastery lore event. |
| **`13262`** | **Ashbringer (Purified Blade)** | 2H Sword | Legendary | 201–247 Damage (74.7 DPS, 3.00 speed), Holy burst damage proc. Legendary blade of Highlord Alexandros Mograine. |
| **`17782`** | **Talisman of Binding Shard** | Neck | Legendary | +10 Fire Resist, +10 Nature Resist, +10 Frost Resist, +10 Shadow Resist, +10 Arcane Resist. Rare legendary necklace. |
| **`82001`** | **Cape of Alexandros** | Cloak | Rare | Tattered cloak that once covered the back of the Ashbringer (Turtle-WoW exclusive). |
| **`82002`** | **Tabard of the Ashbringer** | Tabard | Epic | Filled with holy radiant light (Turtle-WoW exclusive). |

---

## 3. Complete Naxxramas Tier 3 Armor Sets (9 Pieces)

### 🛡️ Warrior: Dreadnaught Battlegear
- **`22416`** : Dreadnaught Breastplate (Chest)
- **`22417`** : Dreadnaught Legplates (Legs)
- **`22418`** : Dreadnaught Helmet (Head)
- **`22419`** : Dreadnaught Pauldrons (Shoulders)
- **`22420`** : Dreadnaught Sabatons (Feet)
- **`22421`** : Dreadnaught Gauntlets (Hands)
- **`22422`** : Dreadnaught Waistguard (Waist)
- **`22423`** : Dreadnaught Bracers (Wrists)
- **`23059`** : Ring of the Dreadnaught (Finger)

### 🔨 Paladin: Redemption Armor
- **`22428`** : Redemption Helm (Head)
- **`22429`** : Redemption Spaulders (Shoulders)
- **`22425`** : Redemption Tunic (Chest)
- **`22424`** : Redemption Bracers (Wrists)
- **`22426`** : Redemption Gloves (Hands)
- **`22431`** : Redemption Belt (Waist)
- **`22427`** : Redemption Pants (Legs)
- **`22430`** : Redemption Boots (Feet)
- **`23066`** : Ring of Redemption (Finger)

### 🏹 Hunter: Cryptstalker Armor
- **`22438`** : Cryptstalker Headpiece (Head)
- **`22439`** : Cryptstalker Spaulders (Shoulders)
- **`22436`** : Cryptstalker Tunic (Chest)
- **`22443`** : Cryptstalker Wristguards (Wrists)
- **`22441`** : Cryptstalker Handguards (Hands)
- **`22442`** : Cryptstalker Girdle (Waist)
- **`22437`** : Cryptstalker Legguards (Legs)
- **`22440`** : Cryptstalker Boots (Feet)
- **`23067`** : Ring of the Cryptstalker (Finger)

### 🗡️ Rogue: Bonescythe Armor
- **`22478`** : Bonescythe Helmet (Head)
- **`22479`** : Bonescythe Pauldrons (Shoulders)
- **`22476`** : Bonescythe Breastplate (Chest)
- **`22483`** : Bonescythe Bracers (Wrists)
- **`22481`** : Bonescythe Gauntlets (Hands)
- **`22482`** : Bonescythe Waistguard (Waist)
- **`22477`** : Bonescythe Legplates (Legs)
- **`22480`** : Bonescythe Sabatons (Feet)
- **`23060`** : Bonescythe Ring (Finger)

### ✝️ Priest: Vestments of Faith
- **`22514`** : Circlet of Faith (Head)
- **`22515`** : Mantle of Faith (Shoulders)
- **`22512`** : Robe of Faith (Chest)
- **`22519`** : Bindings of Faith (Wrists)
- **`22517`** : Gloves of Faith (Hands)
- **`22518`** : Belt of Faith (Waist)
- **`22513`** : Leggings of Faith (Legs)
- **`22516`** : Sandals of Faith (Feet)
- **`23061`** : Ring of Faith (Finger)

### ⚡ Shaman: The Earthshatterer
- **`22466`** : Earthshatter Headpiece (Head)
- **`22467`** : Earthshatter Spaulders (Shoulders)
- **`22464`** : Earthshatter Tunic (Chest)
- **`22471`** : Earthshatter Wristguards (Wrists)
- **`22469`** : Earthshatter Handguards (Hands)
- **`22470`** : Earthshatter Belt (Waist)
- **`22465`** : Earthshatter Legguards (Legs)
- **`22468`** : Earthshatter Boots (Feet)
- **`23065`** : Ring of the Earthshatterer (Finger)

### 🔥 Mage: Frostfire Regalia
- **`22498`** : Frostfire Circlet (Head)
- **`22499`** : Frostfire Shoulderpads (Shoulders)
- **`22496`** : Frostfire Robe (Chest)
- **`22503`** : Frostfire Bindings (Wrists)
- **`22501`** : Frostfire Gloves (Hands)
- **`22502`** : Frostfire Belt (Waist)
- **`22497`** : Frostfire Leggings (Legs)
- **`22500`** : Frostfire Sandals (Feet)
- **`23062`** : Frostfire Ring (Finger)

### 💀 Warlock: Plagueheart Raiment
- **`22506`** : Plagueheart Circlet (Head)
- **`22507`** : Plagueheart Shoulderpads (Shoulders)
- **`22504`** : Plagueheart Robe (Chest)
- **`22511`** : Plagueheart Bracers (Wrists)
- **`22509`** : Plagueheart Gloves (Hands)
- **`22510`** : Plagueheart Belt (Waist)
- **`22505`** : Plagueheart Pants (Legs)
- **`22508`** : Plagueheart Sandals (Feet)
- **`23063`** : Plagueheart Ring (Finger)

### 🐾 Druid: Dreamwalker Raiment
- **`22490`** : Dreamwalker Headpiece (Head)
- **`22491`** : Dreamwalker Spaulders (Shoulders)
- **`22488`** : Dreamwalker Tunic (Chest)
- **`22495`** : Dreamwalker Bracers (Wrists)
- **`22493`** : Dreamwalker Handguards (Hands)
- **`22494`** : Dreamwalker Belt (Waist)
- **`22489`** : Dreamwalker Legguards (Legs)
- **`22492`** : Dreamwalker Boots (Feet)
- **`23064`** : Ring of The Dreamwalker (Finger)

---

## 4. Temple of Ahn'Qiraj Tier 2.5 Armor Sets (5 Pieces)

### 🛡️ Warrior: Conqueror's Battlegear
- **`21329`** : Conqueror's Crown (Head)
- **`21330`** : Conqueror's Spaulders (Shoulders)
- **`21331`** : Conqueror's Breastplate (Chest)
- **`21332`** : Conqueror's Legguards (Legs)
- **`21333`** : Conqueror's Greaves (Feet)

### 🔨 Paladin: Avenger's Battlegear
- **`21387`** : Avenger's Crown (Head)
- **`21391`** : Avenger's Pauldrons (Shoulders)
- **`21389`** : Avenger's Chestplate (Chest)
- **`21390`** : Avenger's Leggings (Legs)
- **`21388`** : Avenger's Sabatons (Feet)

### 🏹 Hunter: Striker's Garb
- **`21366`** : Striker's Diadem (Head)
- **`21367`** : Striker's Pauldrons (Shoulders)
- **`21370`** : Striker's Hauberk (Chest)
- **`21368`** : Striker's Leggings (Legs)
- **`21365`** : Striker's Footguards (Feet)

### 🗡️ Rogue: Deathdealer's Embrace
- **`21360`** : Deathdealer's Helm (Head)
- **`21361`** : Deathdealer's Spaulders (Shoulders)
- **`21364`** : Deathdealer's Vest (Chest)
- **`21362`** : Deathdealer's Leggings (Legs)
- **`21359`** : Deathdealer's Boots (Feet)

### ✝️ Priest: Garments of the Oracle
- **`21348`** : Coronet of the Oracle (Head)
- **`21350`** : Mantle of the Oracle (Shoulders)
- **`21351`** : Raiments of the Oracle (Chest/Robe)
- **`21352`** : Pants of the Oracle (Legs)
- **`21349`** : Footwraps of the Oracle (Feet)

### ⚡ Shaman: Stormcaller's Garb
- **`21372`** : Stormcaller's Diadem (Head)
- **`21376`** : Stormcaller's Epaulets (Shoulders)
- **`21374`** : Stormcaller's Hauberk (Chest)
- **`21375`** : Stormcaller's Legplates (Legs)
- **`21373`** : Stormcaller's Greaves (Feet)

### 🔥 Mage: Enigma Regalia
- **`21347`** : Enigma Circlet (Head)
- **`21345`** : Enigma Shoulderpads (Shoulders)
- **`21343`** : Enigma Robes (Chest/Robe)
- **`21346`** : Enigma Leggings (Legs)
- **`21344`** : Enigma Boots (Feet)

### 💀 Warlock: Doomcaller's Attire
- **`21337`** : Doomcaller's Circlet (Head)
- **`21335`** : Doomcaller's Mantle (Shoulders)
- **`21334`** : Doomcaller's Robes (Chest/Robe)
- **`21336`** : Doomcaller's Trousers (Legs)
- **`21338`** : Doomcaller's Footwraps (Feet)

### 🐾 Druid: Genesis Raiment
- **`21353`** : Genesis Circlet (Head)
- **`21354`** : Genesis Mantle (Shoulders)
- **`21357`** : Genesis Vest (Chest)
- **`21356`** : Genesis Trousers (Legs)
- **`21355`** : Genesis Slippers (Feet)

---

## 5. Blackwing Lair & Molten Core Tier 2 Armor Sets (8 Pieces)

### 🛡️ Warrior: Battlegear of Wrath
- **`16963`** : Helm of Wrath (Head - Onyxia)
- **`16961`** : Pauldrons of Wrath (Shoulders)
- **`16966`** : Breastplate of Wrath (Chest - Nefarian)
- **`16959`** : Bracelets of Wrath (Wrists)
- **`16964`** : Gauntlets of Wrath (Hands)
- **`16960`** : Waistband of Wrath (Waist)
- **`16962`** : Legplates of Wrath (Legs - Ragnaros)
- **`16965`** : Sabatons of Wrath (Feet)

### 🔨 Paladin: Judgement Armor
- **`16955`** : Judgement Helm (Head - Onyxia)
- **`16953`** : Judgement Spaulders (Shoulders)
- **`16958`** : Judgement Breastplate (Chest - Nefarian)
- **`16951`** : Judgement Bracers (Wrists)
- **`16956`** : Judgement Gloves (Hands)
- **`16952`** : Judgement Belt (Waist)
- **`16954`** : Judgement Legplates (Legs - Ragnaros)
- **`16957`** : Judgement Boots (Feet)

### 🏹 Hunter: Dragonstalker Armor
- **`16939`** : Dragonstalker's Helm (Head - Onyxia)
- **`16937`** : Dragonstalker's Spaulders (Shoulders)
- **`16942`** : Dragonstalker's Breastplate (Chest - Nefarian)
- **`16935`** : Dragonstalker's Bracers (Wrists)
- **`16940`** : Dragonstalker's Gauntlets (Hands)
- **`16936`** : Dragonstalker's Belt (Waist)
- **`16938`** : Dragonstalker's Legguards (Legs - Ragnaros)
- **`16941`** : Dragonstalker's Greaves (Feet)

### 🗡️ Rogue: Bloodfang Armor
- **`16908`** : Bloodfang Hood (Head - Onyxia)
- **`16832`** : Bloodfang Spaulders (Shoulders)
- **`16905`** : Bloodfang Chestpiece (Chest - Nefarian)
- **`16911`** : Bloodfang Belt (Waist)
- **`16907`** : Bloodfang Gloves (Hands)
- **`16906`** : Bloodfang Boots (Feet)
- **`16909`** : Bloodfang Pants (Legs - Ragnaros)
- **`16910`** : Bloodfang Bracers (Wrists)

### ✝️ Priest: Vestments of Transcendence
- **`16922`** : Halo of Transcendence (Head - Onyxia)
- **`16924`** : Pauldrons of Transcendence (Shoulders)
- **`16923`** : Robes of Transcendence (Chest - Nefarian)
- **`16926`** : Bindings of Transcendence (Wrists)
- **`16920`** : Handguards of Transcendence (Hands)
- **`16925`** : Belt of Transcendence (Waist)
- **`16921`** : Leggings of Transcendence (Legs - Ragnaros)
- **`16919`** : Boots of Transcendence (Feet)

### ⚡ Shaman: The Ten Storms
- **`16947`** : Visor of Ten Storms (Head - Onyxia)
- **`16945`** : Epaulets of Ten Storms (Shoulders)
- **`16950`** : Raiments of Ten Storms (Chest - Nefarian)
- **`16943`** : Bindings of Ten Storms (Wrists)
- **`16948`** : Gauntlets of Ten Storms (Hands)
- **`16944`** : Sash of Ten Storms (Waist)
- **`16946`** : Legplates of Ten Storms (Legs - Ragnaros)
- **`16949`** : Greaves of Ten Storms (Feet)

### 🔥 Mage: Netherwind Regalia
- **`16914`** : Netherwind Pants (Legs - Ragnaros)
- **`16915`** : Netherwind Robes (Chest - Nefarian)
- **`16916`** : Netherwind Mantle (Shoulders)
- **`16917`** : Netherwind Gloves (Hands)
- **`16918`** : Netherwind Boots (Feet)
- **`16818`** : Netherwind Belt (Waist)
- **`16912`** : Netherwind Bindings (Wrists)
- **`16913`** : Netherwind Crown (Head - Onyxia)

### 💀 Warlock: Nemesis Raiment
- **`16930`** : Nemesis Skullcap (Head - Onyxia)
- **`16932`** : Nemesis Spaulders (Shoulders)
- **`16931`** : Nemesis Robes (Chest - Nefarian)
- **`16934`** : Nemesis Bracers (Wrists)
- **`16928`** : Nemesis Gloves (Hands)
- **`16933`** : Nemesis Belt (Waist)
- **`16929`** : Nemesis Leggings (Legs - Ragnaros)
- **`16927`** : Nemesis Boots (Feet)

### 🐾 Druid: Stormrage Raiment
- **`16900`** : Stormrage Cover (Head - Onyxia)
- **`16902`** : Stormrage Pauldrons (Shoulders)
- **`16903`** : Stormrage Chestguard (Chest - Nefarian)
- **`16897`** : Stormrage Bracers (Wrists)
- **`16899`** : Stormrage Handguards (Hands)
- **`16904`** : Stormrage Belt (Waist)
- **`16901`** : Stormrage Legguards (Legs - Ragnaros)
- **`16898`** : Stormrage Boots (Feet)

---

## 6. Molten Core Tier 1 Armor Sets (8 Pieces)

| Class | Chest | Legs | Head | Shoulders | Feet | Hands | Waist | Wrists |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Warrior (Might)** | `16865` | `16867` | `16866` | `16868` | `16862` | `16863` | `16864` | `16861` |
| **Paladin (Lawbringer)** | `16853` | `16855` | `16854` | `16856` | `16859` | `16860` | `16858` | `16857` |
| **Hunter (Giantstalker)** | `16845` | `16847` | `16846` | `16848` | `16849` | `16852` | `16851` | `16850` |
| **Rogue (Nightslayer)** | `16820` | `16822` | `16821` | `16823` | `16824` | `16826` | `16827` | `16825` |
| **Priest (Prophecy)** | `16815` | `16814` | `16813` | `16816` | `16811` | `16812` | `16817` | `16819` |
| **Shaman (Earthfury)** | `16841` | `16843` | `16842` | `16844` | `16837` | `16839` | `16838` | `16840` |
| **Mage (Arcanist)** | `16798` | `16796` | `16795` | `16797` | `16800` | `16801` | `16802` | `16799` |
| **Warlock (Felheart)** | `16809` | `16810` | `16808` | `16807` | `16803` | `16805` | `16806` | `16804` |
| **Druid (Cenarion)** | `16833` | `16835` | `16834` | `16836` | `16829` | `16831` | `16828` | `16830` |

---

## 7. Zul'Gurub 5-Piece Class Sets

| Class & Set Name | Chest | Shoulders | Wrists | Belt | Class Trinket |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Warrior (Vindicator's Battlegear)** | `19822` | `19823` | `19824` | `19823` | `19951` (Gri'lek's Might) |
| **Paladin (Freethinker's Armor)** | `19825` | `19826` | `19827` | `19826` | `19952` (Gri'lek's Valor) |
| **Hunter (Predator's Armor)** | `19831` | `19831` | `19833` | `19832` | `19953` (Renataki's Beasts) |
| **Rogue (Madcap's Outfit)** | `19834` | `19835` | `19836` | `19835` | `19954` (Renataki's Trickery) |
| **Priest (Confessor's Raiment)** | `19841` | `19841` | `19843` | `19842` | `19957` (Hazza'rah's Destruction) |
| **Shaman (Augur's Regalia)** | `19828` | `19829` | `19830` | `19829` | `19955` (Wushoolay's Nature) |
| **Mage (Illusionist's Attire)** | `20034` | `19845` | `19846` | `19845` | `19959` (Hazza'rah's Magic) |
| **Warlock (Demoniac's Threads)** | `20033` | `19849` | `19848` | `19849` | `19958` (Hazza'rah's Healing/Shadow) |
| **Druid (Haruspex's Garb)** | `19838` | `19839` | `19840` | `19839` | `19956` (Wushoolay's Spirits) |

---

## 8. Ruins of Ahn'Qiraj (AQ20) 3-Piece Class Sets

| Class & Set Name | Weapon / Off-Hand | Ring / Signet | Cloak / Drape |
| :--- | :---: | :---: | :---: |
| **Warrior (Unyielding Strength)** | `21392` (Sickle) | `21393` (Signet) | `21394` (Drape) |
| **Paladin (Eternal Justice)** | `21395` (Blade) | `21396` (Ring) | `21397` (Cape) |
| **Shaman (Gathering Storm)** | `21398` (Hammer) | `21399` (Ring) | `21400` (Cloak) |
| **Hunter (Unseen Path)** | `21401` (Scythe) | `21402` (Signet) | `21403` (Cloak) |
| **Rogue (Veiled Shadows)** | `21404` (Dagger) | `21405` (Band) | `21406` (Cloak) |
| **Druid (Unending Life)** | `21407` (Mace) | `21408` (Band) | `21409` (Cloak) |
| **Priest (Infinite Wisdom)** | `21410` (Gavel) | `21411` (Ring) | `21412` (Shroud) |
| **Mage (Vaulted Secrets)** | `21413` (Blade) | `21414` (Band) | `21415` (Drape) |
| **Warlock (Unspoken Names)** | `21416` (Kris) | `21417` (Ring) | `21418` (Shroud) |

---

## 9. Dungeon Sets 1 & 2 (Tier 0 & Tier 0.5 Upgrades)

### Dungeon Set 2 (Tier 0.5 Epic Upgrade)
- **Warrior (Battlegear of Heroism)**: Helm `21999`, Shoulders `22001`, Chest `21997`, Wrists `21996`, Gloves `21998`, Belt `21994`, Legs `22000`, Boots `21995`
- **Paladin (Soulforge Armor)**: Helm `22091`, Shoulders `22093`, Chest `22089`, Wrists `22088`, Gloves `22090`, Belt `22086`, Legs `22092`, Boots `22087`
- **Hunter (Beastmaster Armor)**: Helm `22013`, Shoulders `22016`, Chest `22060`, Wrists `22011`, Gloves `22015`, Belt `22010`, Legs `22017`, Boots `22061`
- **Rogue (Darkmantle Armor)**: Helm `22005`, Shoulders `22008`, Chest `22009`, Wrists `22004`, Gloves `22006`, Belt `22002`, Legs `22007`, Boots `22003`
- **Priest (Vestments of the Virtuous)**: Crown `22080`, Mantle `22082`, Robe `22083`, Wrists `22079`, Gloves `22081`, Belt `22078`, Legs `22085`, Boots `22084`
- **Shaman (The Five Thunders)**: Helm `22097`, Shoulders `22101`, Chest `22102`, Wrists `22095`, Gloves `22099`, Belt `22098`, Legs `22100`, Boots `22096`
- **Mage (Sorcerer's Regalia)**: Crown `22065`, Mantle `22068`, Robes `22069`, Wrists `22063`, Gloves `22066`, Belt `22062`, Legs `22067`, Boots `22064`
- **Warlock (Deathmist Raiment)**: Mask `22074`, Mantle `22073`, Robe `22075`, Wrists `22071`, Gloves `22077`, Belt `22070`, Legs `22072`, Boots `22076`
- **Druid (Moonheart Raiment - Turtle Custom)**: Cowl `22109`, Spaulders `22112`, Vest `22113`, Bracers `22108`, Gloves `22110`, Belt `22106`, Kilt `22111`, Boots `22107`

---

## 10. PvP Grand Marshal & High Warlord Equipment (Rank 14)

### ⚔️ Rank 14 Epic Weapons
- **`18876`** : **Veteran's Claymore** (Alliance 2H Sword - 83.2 DPS, 3.80 speed)
- **`18877`** : **Veteran's Greatsword** (Horde 2H Sword - 83.2 DPS, 3.80 speed)
- **`18830`** : **Veteran's Sunderer** (Alliance/Horde 2H Axe - 83.2 DPS, 3.80 speed)
- **`18831`** : **Veteran's Battle Axe** (Alliance/Horde 2H Axe - 83.2 DPS, 3.80 speed)
- **`18867`** : **Veteran's Battle Hammer** (Alliance 2H Mace - 83.2 DPS, 3.80 speed)
- **`18868`** : **Veteran's Pulverizer** (Horde 2H Mace - 83.2 DPS, 3.80 speed)
- **`18869`** : **Veteran's Glaive** (Alliance 2H Polearm - 83.2 DPS, 3.80 speed)
- **`18871`** : **Veteran's Pig Sticker** (Horde 2H Polearm - 83.2 DPS, 3.80 speed)
- **`18873`** : **Executioner's Stave** (Alliance 2H Staff - 61.5 DPS, 3.00 speed)
- **`18874`** : **Executioner's War Staff** (Horde 2H Staff - 61.5 DPS, 3.00 speed)
- **`12584`** : **Veteran's Longsword** (Alliance 1H Sword - 65.5 DPS, 2.90 speed)
- **`16345`** : **Veteran's Blade** (Horde 1H Sword - 65.5 DPS, 2.90 speed)
- **`18827`** : **Veteran's Handaxe** (Alliance 1H Axe - 65.5 DPS, 2.90 speed)
- **`18828`** : **Veteran's Cleaver** (Horde 1H Axe - 65.5 DPS, 2.90 speed)
- **`18865`** : **Veteran's Punisher** (Alliance 1H Mace - 65.5 DPS, 2.90 speed)
- **`18866`** : **Veteran's Bludgeon** (Horde 1H Mace - 65.5 DPS, 2.90 speed)
- **`18838`** : **Veteran's Dirk** (Alliance 1H Dagger - 65.5 DPS, 2.00 speed)
- **`18840`** : **Veteran's Razor** (Horde 1H Dagger - 65.5 DPS, 2.00 speed)
- **`18833`** : **Veteran's Bullseye** (Alliance Bow - 50.8 DPS, 1.80 speed)
- **`18835`** : **Veteran's Recurve** (Horde Bow - 50.8 DPS, 1.80 speed)
- **`18836`** : **Veteran's Repeater** (Alliance Crossbow - 50.9 DPS, 2.90 speed)
- **`18837`** : **Veteran's Crossbow** (Horde Crossbow - 50.9 DPS, 2.90 speed)
- **`18855`** : **Veteran's Hand Cannon** (Alliance Gun - 50.9 DPS, 2.90 speed)
- **`18860`** : **Veteran's Street Sweeper** (Horde Gun - 50.9 DPS, 2.90 speed)
- **`18825`** : **Veteran's Aegis** (Alliance Shield - 3106 Armor, 59 Block)
- **`18826`** : **Veteran's Shield Wall** (Horde Shield - 3106 Armor, 59 Block)

### 🛡️ Rank 12–13 Field Marshal / Warlord Epic Armor
- **Warrior Plate Set**: Helm `16542`, Shoulders `16544`, Chest `16541`, Gloves `16543`, Legs `16545`, Boots `16548`
- **Paladin Plate Set**: Helm `16474`, Shoulders `16476`, Chest `16471`, Gloves `16472`, Legs `16475`, Boots `16473`
- **Hunter Mail Set**: Helm `16565`, Shoulders `16567`, Chest `16566`, Gloves `16568`, Legs `16571`, Boots `16569`
- **Rogue Leather Set**: Helm `16561`, Shoulders `16563`, Chest `16562`, Gloves `16560`, Legs `16558`, Boots `16559`
- **Priest Cloth Set**: Helm `16533`, Shoulders `16536`, Chest `16535`, Gloves `16539`, Legs `16534`, Boots `16540`
- **Mage Cloth Set**: Helm `16533`, Shoulders `16536`, Chest `16535`, Gloves `16539`, Legs `16534`, Boots `16540`
- **Warlock Cloth Set**: Helm `16533`, Shoulders `16536`, Chest `16535`, Gloves `16539`, Legs `16534`, Boots `16540`
- **Druid Leather Set**: Helm `16549`, Shoulders `16552`, Chest `16551`, Gloves `16550`, Legs `16554`, Boots `16555`

---

## 11. World Boss Epic Drops (Azuregos, Kazzak, Nightmare Dragons)

### 🐉 Azuregos (Azshara)
- **`18202`** : **Eskhandar's Left Claw** (1H Fist Weapon Off-Hand - 1.50 speed, Slow & Bleed proc)
- **`18203`** : **Eskhandar's Right Claw** (1H Fist Weapon Main Hand - 1.50 speed, Attack Speed proc)
- **`17070`** : **Fang of the Mystics** (1H Main Hand Mace - +40 Healing, +10 MP5)
- **`18547`** : **Unmelting Ice Girdle** (Plate Belt - +14 Str, +14 Sta, +14 Agi, +1% Dodge)
- **`18541`** : **Puissant Cape** (Cloak - +12 Sta, +1% Hit, +40 Attack Power)
- **`18208`** : **Drape of Benediction** (Cloak - +31 Healing, +10 Intellect)
- **`19131`** : **Snowblind Shoes** (Cloth Boots - +10 Sta, +10 Int, +32 Spell Damage)

### 👿 Lord Kazzak (Tainted Scar, Blasted Lands)
- **`17112`** : **Empyrean Demolisher** (1H Mace - 2.80 speed, Chance on Hit: +20% Attack Speed)
- **`17113`** : **Amberseal Keeper** (2H Staff - +44 Spell Damage, +20 Intellect, +5 MP5)
- **`19134`** : **Flayed Doomguard Belt** (Leather Belt - +14 Str, +13 Sta, +12 Agi, +1% Crit)
- **`19133`** : **Fel Infused Leggings** (Cloth Legs - +21 Sta, +20 Int, +34 Spell Damage)
- **`17110`** : **Seal of the Archmagus** (Ring - +11 Int, +11 Sta, +6 MP5)
- **`18546`** : **Infernal Headcage** (Mail Helm - +20 Str, +15 Sta, +15 Agi, +1% Crit)

### 🌲 Dragons of Nightmare (Taerar, Ysondre, Lethon, Emeriss)
- **`20577`** : **Nightmare Blade** (1H Sword - 2.70 speed, +70 Armor, +32 Attack Power)
- **`20581`** : **Staff of Rampant Growth** (2H Staff - +15 Int, +15 Sta, +40 Healing, +11 MP5)
- **`20580`** : **Hammer of Bestial Fury** (2H Mace - +65 Feral Attack Power, +13 Str, +12 Sta)
- **`20599`** : **Polished Ironwood Crossbow** (Crossbow - 3.10 speed, +14 Attack Power, +1% Hit)
- **`20615`** : **Dragonspur Wraps** (Leather Wrists - +15 Str, +15 Sta, +1% Hit)
- **`20296`** : **Green Dragonscale Gauntlets** (Mail Gloves - +12 Sta, +2% Crit, +28 Attack Power)
- **`20582`** : **Trance Stone** (Off-Hand Caster - +10 Int, +8 Sta, +25 Spell Damage)
- **`20625`** : **Belt of the Dark Bog** (Leather Belt - +14 Sta, +8 Int, +29 Spell Damage)
- **`20617`** : **Ancient Corroded Leggings** (Mail Legs - +22 Agi, +15 Sta, +40 AP)
- **`20621`** : **Boots of the Endless Moor** (Mail Boots - +18 Sta, +18 Int, +26 Healing)

---

## 12. BiS Weapons Directory (Organized by Weapon Type)

### 🔨 Two-Handed Weapons (Swords, Maces, Axes & Polearms)
| Item ID | Item Name | Type | Boss / Source | Speed | DPS | Stats & Effects |
| :---: | :--- | :---: | :--- | :---: | :---: | :--- |
| **`22798`** | **Might of Menethil** | 2H Mace | Kel'Thuzad (Naxx) | 3.80 | 95.3 | +20 Strength, +46 Stamina, Equip: +2% Crit. (Highest 2H Physical DPS in Vanilla) |
| **`22691`** | **Corrupted Ashbringer** | 2H Sword | Four Horsemen (Naxx) | 3.60 | 90.0 | +2% Crit, +2% Hit, Chance on Hit: Life Steal 185–215. Triggers Scarlet Monastery lore event. |
| **`13262`** | **Ashbringer (Purified Blade)** | 2H Sword | Legendary Quest | 3.00 | 74.7 | 201–247 Damage, Holy burst proc. Highlord Alexandros Mograine's blade. |
| **`22815`** | **Severance** | 2H Axe | Patchwerk (Naxx) | 3.60 | 85.8 | +43 Strength, +18 Stamina, Equip: +2% Crit. (Top Raid 2H Axe) |
| **`23039`** | **The Eye of Nerub** | 2H Polearm | Naxxramas Trash | 3.70 | 85.9 | +28 Agility, +2% Hit, +2% Crit, +60 Attack Power. (BiS Hunter Melee Stat Stick) |
| **`22813`** | **Claymore of Unholy Might** | 2H Sword | Naxxramas Trash | 3.60 | 85.8 | +82 Attack Power, +2% Critical Strike. |
| **`22809`** | **Maul of the Redeemed Crusader** | 2H Mace | Naxxramas Trash | 3.60 | 85.8 | +30 Strength, +24 Stamina, +1% Crit, +1% Hit. |
| **`21134`** | **Dark Edge of Insanity** | 2H Axe | C'Thun (AQ40) | 3.50 | 86.6 | +35 Strength, +19 Agility, +25 Stamina, Chance on Hit: 3-sec Disorient. |
| **`21679`** | **Kalimdor's Revenge** | 2H Sword | Twin Emperors (AQ40) | 3.20 | 81.9 | +24 Strength, +18 Stamina, Chance on Hit: 239–317 Nature blast. |
| **`19364`** | **Ashkandi, Greatsword of the Brotherhood** | 2H Sword | Nefarian (BWL) | 3.50 | 81.9 | +33 Stamina, Equip: +86 Attack Power. |
| **`19334`** | **The Untamed Blade** | 2H Sword | Razorgore (BWL) | 3.40 | 66.8 | Chance on Hit: +300 Strength for 8 seconds. Massive burst proc. |
| **`19354`** | **Draconic Avenger** | 2H Axe | BWL Trash | 3.20 | 66.9 | +21 Strength, +18 Stamina, +1% Parry. |
| **`19358`** | **Draconic Maul** | 2H Mace | BWL Trash | 3.50 | 66.7 | +27 Strength, +19 Stamina, +1% Critical Strike. |
| **`19357`** | **Herald of Woe** | 2H Mace | Ebonroc (BWL) | 3.40 | 73.1 | +28 Strength, +1% Crit, Chance on Hit: Stun target for 3 sec. |
| **`17182`** | **Sulfuras, Hand of Ragnaros** | 2H Mace | Ragnaros (MC) | 3.70 | 80.4 | Chance on hit: 273–333 Fireball + 5 Fire shield. +12 Str, +12 Sta, +30 FR. |
| **`17076`** | **Bonereaver's Edge** | 2H Sword | Ragnaros (MC) | 3.40 | 60.6 | +16 Stamina, Chance on Hit: Armor penetration stack (700 armor reduction per stack). |
| **`17104`** | **Spinal Reaper** | 2H Axe | Ragnaros (MC) | 3.40 | 68.4 | +34 Strength, +1% Crit, Equip: Gain 150 Attack Power upon killing an enemy. |
| **`18822`** | **Obsidian Edged Blade** | 2H Sword | Garr / Magmadar (MC) | 3.40 | 60.6 | +42 Strength, +4 Swords Skill. Excellent human/non-human starter 2H. |
| **`19854`** | **Zin'rokh, Destroyer of Worlds** | 2H Sword | Hakkar (ZG) | 3.80 | 60.4 | +28 Stamina, Equip: +72 Attack Power. Iconic slow PvP cleaver. |
| **`19874`** | **Halberd of Smiting** | 2H Polearm | Bloodlord Mandokir (ZG) | 3.50 | 58.7 | Chance on Hit: Decapitate for extra physical burst damage. |
| **`21492`** | **Manslayer of the Qiraji** | 2H Sword | General Rajaxx (AQ20) | 3.60 | 58.6 | +21 Strength, +15 Stamina, +1% Crit, +1% Hit. |
| **`19169`** | **Nightfall** | 2H Axe | Blacksmithing Crafted | 3.50 | 53.6 | Chance on Hit: Spell Damage Taken +15% on target. Essential raid debuff weapon. |
| **`12784`** | **Arcanite Reaper** | 2H Axe | Blacksmithing Crafted | 3.80 | 53.7 | +62 Attack Power, +13 Stamina. Iconic PvP burst axe. |
| **`19323`** | **The Unstoppable Force** | 2H Mace | Alterac Valley Vendor | 3.80 | 61.4 | +19 Strength, +15 Stamina, +1% Crit, Chance on Hit: Stun/Knockdown for 1s. |
| **`18538`** | **Treant's Bane** | 2H Axe | Dire Maul Tribute | 3.80 | 58.0 | +25 Strength, +15 Stamina, +2% Critical Strike. |
| **`12592`** | **Blackblade of Shahram** | 2H Sword | General Drakkisath (UBRS) | 3.50 | 54.7 | Chance on Hit: Summons the Spirit of Shahram to cast powerful raid buffs/nukes. |
| **`18876`** | **Veteran's Claymore** | 2H Sword | Alliance Rank 14 PvP | 3.80 | 83.2 | +41 Strength, +18 Stamina, +1% Critical Strike. (Grand Marshal model) |
| **`18877`** | **Veteran's Greatsword** | 2H Sword | Horde Rank 14 PvP | 3.80 | 83.2 | +41 Strength, +18 Stamina, +1% Critical Strike. (High Warlord model) |
| **`18830`** | **Veteran's Sunderer** | 2H Axe | Rank 14 PvP | 3.80 | 83.2 | +41 Strength, +18 Stamina, +1% Critical Strike. |
| **`18867`** | **Veteran's Battle Hammer** | 2H Mace | Alliance Rank 14 PvP | 3.80 | 83.2 | +41 Strength, +18 Stamina, +1% Critical Strike. |
| **`18868`** | **Veteran's Pulverizer** | 2H Mace | Horde Rank 14 PvP | 3.80 | 83.2 | +41 Strength, +18 Stamina, +1% Critical Strike. |
| **`18869`** | **Veteran's Glaive** | 2H Polearm | Alliance Rank 14 PvP | 3.80 | 83.2 | +41 Strength, +18 Stamina, +1% Critical Strike. |
| **`18871`** | **Veteran's Pig Sticker** | 2H Polearm | Horde Rank 14 PvP | 3.80 | 83.2 | +41 Strength, +18 Stamina, +1% Critical Strike. |
| **`55348`** | **Kirel'narak, the Death Sentence** | 2H Staff | Emerald Sanctum (Custom) | 3.20 | 60.9 | +25 Int, +25 Sta, +92 Spell Damage, +2% Spell Hit. (ilvl 96 Custom BiS Staff) |
| **`55127`** | **Shar'tateth, the Shattered Edge** | 2H Axe | Emerald Sanctum (Custom) | 3.80 | 100.0 | +38 Strength, +30 Stamina, +2% Crit, +1% Hit. (ilvl 92 Custom 2H Axe) |
| **`55116`** | **Raka'shishi, Spear of the Adrift Hunt** | 2H Polearm | Emerald Sanctum (Custom) | 3.70 | 100.0 | +40 Agility, +24 Stamina, +2% Crit, +2% Hit. (ilvl 92 Custom 2H Polearm) |
| **`55100`** | **Checkmate** | 2H Mace | Scarlet Citadel (Custom) | 3.70 | 93.5 | +32 Strength, +28 Stamina, +2% Crit, +1% Hit. (ilvl 88 Custom 2H Mace) |
| **`55277`** | **The End of All Ambitions** | 2H Sword | Lower Karazhan (Custom) | 3.50 | 84.6 | +30 Strength, +22 Agility, +22 Stamina, +1% Crit. (ilvl 83 Custom 2H Sword) |

---

### ⚔️ One-Handed Swords
| Item ID | Item Name | Boss / Source | Speed | DPS | Stats & Effects |
| :---: | :--- | :--- | :---: | :---: | :--- |
| **`23054`** | **Gressil, Dawn of Ruin** | Kel'Thuzad (Naxx) | 2.70 | 73.1 | +15 Stamina, +14 Attack Power, Equip: +1% Crit. |
| **`23577`** | **The Hungering Cold** | Kel'Thuzad (Naxx) | 1.50 | 73.0 | +140 Armor, +14 Stamina, Equip: +6 Swords Skill, +1.50s Fast Off-hand. |
| **`23014`** | **Iblis, Blade of the Fallen Seraph** | Gluth / Patchwerk (Naxx) | 1.60 | 66.9 | +7 Stamina, Equip: +1% Hit, +1% Crit, +26 Attack Power. |
| **`19352`** | **Chromatically Tempered Sword (CTS)** | Chromaggus (BWL) | 2.60 | 58.5 | +14 Strength, +14 Agility, +7 Stamina. |
| **`19351`** | **Maladath, Runed Blade of the Black Flight** | Broodlord Lashlayer (BWL) | 2.20 | 56.4 | +4 Agility, Equip: +1% Parry, +4 Swords Skill. |
| **`21650`** | **Ancient Qiraji Ripper** | Fankriss (AQ40) | 2.80 | 58.4 | +11 Stamina, Equip: +1% Crit, +20 Attack Power. |
| **`17075`** | **Vis'kag the Bloodletter** | Onyxia | 2.60 | 48.3 | Chance on hit: 240 Fatal Wound burst. |
| **`18832`** | **Brutality Blade** | Garr (MC) | 2.50 | 45.4 | +9 Strength, +9 Agility, Equip: +1% Crit. |
| **`19865`** | **Warblade of the Hakkari (Main)** | Hakkar (ZG) | 2.60 | 40.8 | +28 Attack Power, Equip: Set bonus +6 Swords Skill. |
| **`19866`** | **Warblade of the Hakkari (Off)** | Bloodlord Mandokir (ZG) | 1.70 | 40.9 | +40 Attack Power, Equip: +1% Crit. |
| **`21520`** | **Ravencrest's Legacy** | AQ Scepter Quest | 2.80 | 58.4 | +13 Str, +9 Agi, +14 Sta. |
| **`18348`** | **Quel'Serrar** | Onyxia / Foror's Compendium | 2.00 | 52.5 | +12 Stamina, Chance on Hit: +300 Armor & +13 Defense. Legendary tank 1H sword. |
| **`1728`**  | **Teebu's Blazing Longsword** | World Drop (BoE) | 2.90 | 44.8 | Chance on Hit: 150–250 Fire burst damage. Glowing fiery blade. |
| **`12584`** | **Veteran's Longsword** | Alliance Rank 14 PvP | 2.90 | 65.5 | +28 Strength, +7 Stamina, +1% Crit. (Grand Marshal 1H Sword) |
| **`16345`** | **Veteran's Blade** | Horde Rank 14 PvP | 2.90 | 65.5 | +28 Strength, +7 Stamina, +1% Crit. (High Warlord 1H Sword) |

---

### 🗡️ Daggers
| Item ID | Item Name | Boss / Source | Speed | DPS | Stats & Effects |
| :---: | :--- | :--- | :---: | :---: | :--- |
| **`22802`** | **Kingsfall** | Kel'Thuzad (Naxx) | 1.80 | 73.1 | +16 Agility, Equip: +1% Crit, +1% Hit, +28 Attack Power. |
| **`23044`** | **Harbinger of Doom** | Gluth / Patchwerk (Naxx) | 1.60 | 66.9 | +8 Stamina, Equip: +1% Hit, +1% Crit, +26 Attack Power. |
| **`21126`** | **Death's Sting** | C'Thun (AQ40) | 1.80 | 66.4 | +10 Stamina, Equip: +38 Attack Power, +3 Daggers Skill. |
| **`19346`** | **Dragonfang Blade** | Vaelastrasz (BWL) | 1.80 | 58.3 | +16 Agility, Equip: +36 Attack Power. |
| **`19347`** | **Claw of Chromaggus** | Chromaggus (BWL) | 1.80 | 58.3 | +17 Intellect, +7 Stamina, +64 Spell Damage, +4 MP5. |
| **`21244`** | **Blessed Qiraji Pugio** | AQ40 Quest | 1.70 | 58.5 | +6 Intellect, +7 Stamina, +72 Spell Damage, +1% Spell Crit, +1% Spell Hit. |
| **`19859`** | **Fang of the Faceless** | Hakkar (ZG) | 1.90 | 41.6 | Equip: +1% Crit, +28 Attack Power. |
| **`18816`** | **Perdition's Blade** | Ragnaros (MC) | 1.80 | 51.7 | Chance on hit: 40–56 Fire burst damage. |
| **`18805`** | **Core Hound Tooth** | Majordomo Chest (MC) | 1.60 | 45.0 | +9 Stamina, Equip: +1% Crit, +20 Attack Power. |
| **`22804`** | **Maexxna's Fang** | Maexxna (Naxx) | 1.80 | 65.6 | +8 Agility, +8 Stamina, +36 Attack Power, +1% Hit. |
| **`18838`** | **Veteran's Dirk** | Alliance Rank 14 PvP | 2.00 | 65.5 | +18 Agility, +8 Stamina, +1% Crit. |
| **`18840`** | **Veteran's Razor** | Horde Rank 14 PvP | 2.00 | 65.5 | +18 Agility, +8 Stamina, +1% Crit. |

---

### 🏹 Ranged Weapons & Bows
| Item ID | Item Name | Type | Boss / Source | Speed | DPS | Stats & Effects |
| :---: | :--- | :---: | :--- | :---: | :---: | :--- |
| **`22812`** | **Nerubian Slavemaker** | Crossbow | Kel'Thuzad (Naxx) | 3.20 | 56.6 | +24 Attack Power, +1% Crit. (Highest ranged physical DPS in Vanilla). |
| **`22811`** | **Soulstring** | Bow | Four Horsemen (Naxx) | 2.90 | 50.7 | +6 Stamina, +16 Attack Power, +1% Crit. |
| **`21459`** | **Crossbow of Imminent Doom** | Crossbow | Kurinnaxx (AQ20) | 3.10 | 45.5 | +7 Agility, +5 Stamina, +18 Attack Power. |
| **`21616`** | **Huhuran's Stinger** | Bow | Princess Huhuran (AQ40) | 2.70 | 45.6 | +18 Agility, +1% Hit. |
| **`23557`** | **Larvae of the Great Worm** | Gun | Ouro (AQ40) | 3.00 | 45.7 | +18 Attack Power, +1% Crit. |
| **`19361`** | **Ashjre'thul, Crossbow of Smiting** | Crossbow | Chromaggus (BWL) | 3.40 | 45.6 | +7 Stamina, Equip: +36 Attack Power. |
| **`18713`** | **Rhok'delar, Longbow of the Ancient Keepers** | Bow | Hunter Epic Quest | 2.90 | 44.0 | +17 Agility, Equip: +1% Crit. |
| **`17069`** | **Striker's Mark** | Bow | Magmadar (MC) | 2.50 | 39.0 | Equip: +1% Hit, +22 Attack Power. |
| **`17072`** | **Blastershot Launcher** | Gun | Golemagg (MC) | 2.60 | 39.4 | +6 Stamina, Equip: +1% Crit. |
| **`19853`** | **Gurubashi Dwarf Destroyer** | Gun | Bloodlord Mandokir (ZG) | 2.80 | 40.0 | +30 Attack Power. |
| **`20599`** | **Polished Ironwood Crossbow** | Crossbow | Dragons of Nightmare | 3.10 | 41.3 | +14 Attack Power, +1% Hit. |
| **`18833`** | **Veteran's Bullseye** | Bow | Alliance Rank 14 PvP | 1.80 | 50.8 | +14 Agility, +7 Stamina. |
| **`18835`** | **Veteran's Recurve** | Bow | Horde Rank 14 PvP | 1.80 | 50.8 | +14 Agility, +7 Stamina. |
| **`18836`** | **Veteran's Repeater** | Crossbow | Alliance Rank 14 PvP | 2.90 | 50.9 | +18 Attack Power, +1% Crit. |
| **`18837`** | **Veteran's Crossbow** | Crossbow | Horde Rank 14 PvP | 2.90 | 50.9 | +18 Attack Power, +1% Crit. |
| **`18855`** | **Veteran's Hand Cannon** | Gun | Alliance Rank 14 PvP | 2.90 | 50.9 | +18 Attack Power, +1% Crit. |
| **`18860`** | **Veteran's Street Sweeper** | Gun | Horde Rank 14 PvP | 2.90 | 50.9 | +18 Attack Power, +1% Crit. |

---

### 🔮 Staves, Caster 1H & Off-Hands
| Item ID | Item Name | Slot | Boss / Source | Stats & Effects |
| :---: | :--- | :---: | :--- | :--- |
| **`22800`** | **Brimstone Staff** | 2H Staff | Naxxramas | +18 Stamina, +18 Intellect, +84 Spell Damage, +2% Spell Crit, +1% Spell Hit. |
| **`22799`** | **Soulseeker** | 2H Staff | Kel'Thuzad (Naxx) | +24 Intellect, +24 Stamina, +85 Spell Damage, +2% Spell Crit, +1% Spell Hit. |
| **`22801`** | **Spire of Twilight** | 2H Staff | Thaddius (Naxx) | +25 Intellect, +25 Stamina, +111 Healing, +37 Spell Damage, +12 MP5. |
| **`19356`** | **Staff of the Shadow Flame** | 2H Staff | Nefarian (BWL) | +29 Stamina, +24 Intellect, +84 Spell Damage, +2% Spell Crit. |
| **`17113`** | **Amberseal Keeper** | 2H Staff | Lord Kazzak | +44 Spell Damage, +20 Intellect, +5 MP5. |
| **`20581`** | **Staff of Rampant Growth** | 2H Staff | Dragons of Nightmare | +15 Intellect, +15 Stamina, +40 Healing, +11 MP5. |
| **`22988`** | **The End of Dreams** | 1H Mace | Kel'Thuzad (Naxx) | +14 Intellect, +14 Stamina, +52 Spell Dmg, +94 Healing, +7 MP5. |
| **`23049`** | **Sapphiron's Left Eye** | Off-Hand | Sapphiron (Naxx) | +8 Intellect, +8 Stamina, +26 Spell Dmg, +1% Spell Crit, +1% Spell Hit. |
| **`19360`** | **Lok'amir il Romathis** | 1H Mace | Nefarian (BWL) | +18 Stamina, +18 Intellect, +84 Healing, +29 Spell Dmg. |
| **`17103`** | **Azuresong Mageblade** | 1H Sword | Golemagg (MC) | +12 Intellect, +7 Stamina, +40 Spell Dmg, +1% Spell Crit. |
| **`17070`** | **Fang of the Mystics** | 1H Mace | Azuregos | +40 Healing, +10 MP5, +10 Intellect. |
| **`19315`** | **Therazane's Touch** | Off-Hand | BWL Trash | +44 Healing, +12 MP5. |
| **`20582`** | **Trance Stone** | Off-Hand | Dragons of Nightmare | +10 Intellect, +8 Stamina, +25 Spell Damage. |
| **`23009`** | **Wand of the Whispering Dead** | Wand | Kel'Thuzad (Naxx) | +16 Spell Damage, +1% Spell Hit. (Highest spell DPS wand). |
| **`22820`** | **Wand of Fates** | Wand | Sapphiron (Naxx) | +7 Stamina, +7 Intellect, +31 Healing, +11 Spell Damage. |
| **`22821`** | **Doomfinger** | Wand | Naxxramas Trash | +16 Shadow Spell Damage, +1% Spell Crit. |

---

### 🛡️ Shields & Tank Off-Hands
| Item ID | Item Name | Armor | Block | Boss / Source | Stats & Effects |
| :---: | :--- | :---: | :---: | :--- | :--- |
| **`23043`** | **The Face of Death** | 3494 | 66 | Sapphiron (Naxx) | +14 Strength, +24 Stamina, +7 Defense, +1% Block, +19 Block Value. |
| **`22819`** | **Shield of Condemnation** | 3425 | 68 | Kel'Thuzad (Naxx) | +15 Intellect, +17 Stamina, +44 Healing, +15 Spell Dmg, +6 MP5. |
| **`22818`** | **The Plague Bearer** | 3106 | 59 | Gluth (Naxx) | +12 Strength, +21 Stamina, +1% Block, +15 Block Value. |
| **`23075`** | **Death's Bargain** | 3106 | 59 | Naxxramas Trash | +10 Strength, +20 Stamina, +1% Dodge, +1% Parry. |
| **`21610`** | **Wormscale Blocker** | 3035 | 57 | Ouro (AQ40) | +16 Stamina, +14 Intellect, +31 Healing, +11 Spell Dmg. |
| **`21269`** | **Blessed Qiraji Bulwark** | 2964 | 55 | AQ40 Quest | +20 Stamina, +8 Defense, +15 Block Value, +1% Block. |
| **`19349`** | **Elementium Reinforced Bulwark** | 2893 | 54 | Chromaggus (BWL) | +23 Stamina, +7 Defense, +19 Block Value. |
| **`19348`** | **Red Dragonscale Protector** | 2853 | 51 | Vaelastrasz (BWL) | +18 Stamina, +10 Intellect, +37 Healing, +5 MP5. |
| **`17066`** | **Drillborer Disk** | 2539 | 46 | Garr (MC) | +10 Stamina, +15 Defense, Chance on Block: 70–90 Arcane reflection. |
| **`17106`** | **Malistar's Defender** | 2822 | 52 | Ragnaros (MC) | +15 Intellect, +14 Stamina, +22 Healing, +8 Spell Dmg. |
| **`19862`** | **Aegis of the Blood God** | 2575 | 47 | Mandokir (ZG) | +14 Stamina, +7 Defense, +30 Block Value. |
| **`21485`** | **Buru's Skull Fragment** | 2575 | 47 | Buru the Gorger (AQ20) | +10 Stamina, +15 Block Value, +1% Block, +6 MP5. |
| **`19321`** | **The Immovable Object** | 2468 | 44 | AV PvP Vendor | +15 Stamina, +1% Block, +15 Block Value. |
| **`18825`** | **Veteran's Aegis** | 3106 | 59 | Alliance Rank 14 PvP | +15 Strength, +23 Stamina, +1% Block, +15 Block Value. |
| **`18826`** | **Veteran's Shield Wall** | 3106 | 59 | Horde Rank 14 PvP | +15 Strength, +23 Stamina, +1% Block, +15 Block Value. |

---

## 13. BiS Endgame Trinkets Directory

### ⚔️ Physical / Melee & Ranged Trinkets
- **`22954`** : **Kiss of the Spider** (Maexxna - Naxx) : +1% Hit, On-Use: +20% Attack Speed for 15 seconds. (BiS Physical Trinket).
- **`19406`** : **Drake Fang Talisman (DFT)** (Ebonroc - BWL) : +56 Attack Power, +2% Hit, +1% Dodge.
- **`21670`** : **Badge of the Swarmguard** (Battleguard Sartura - AQ40) : On-Use: Each physical attack adds +200 Armor Penetration (stacks up to 6 times for 1,200 Armor reduction).
- **`23206`** : **Mark of the Champion (Physical)** (Kel'Thuzad Quest) : +150 Attack Power against Undead and Demons.
- **`20130`** : **Diamond Flask** (Warrior Level 50 Class Quest) : On-Use: +75 Strength and restores 9 Health every 5 seconds for 1 minute.
- **`13965`** : **Blackhand's Breadth** (UBRS Quest) : +2% Critical Strike.
- **`19991`** : **Devilsaur Eye** (Hunter Quest) : +2% Hit, On-Use: +150 Attack Power for 20s.
- **`21180`** : **Earthstrike** (Cenarion Hold Logistics Rep) : On-Use: +280 Attack Power for 20 seconds.
- **`19289`** : **Darkmoon Card: Maelstrom** (Elementals Deck) : Chance on hit: 200–300 Nature damage strike.
- **`19287`** : **Darkmoon Card: Heroism** (Warlords Deck) : Chance on melee hit: Restores 120–180 Health.
- **`11815`** : **Hand of Justice** (BRD Emperor) : +20 Attack Power, 2% Chance on melee hit to gain an extra attack.

### 🔮 Caster & Spell Damage Trinkets
- **`19379`** : **Neltharion's Tear** (Nefarian - BWL) : +44 Spell Damage, +2% Spell Hit. (BiS Caster Trinket).
- **`23046`** : **The Restrained Essence of Sapphiron** (Sapphiron - Naxx) : +40 Spell Damage, On-Use: +130 Spell Damage for 20 seconds.
- **`23207`** : **Mark of the Champion (Caster)** (Kel'Thuzad Quest) : +85 Spell Damage against Undead and Demons.
- **`19950`** : **Zandalarian Hero Charm** (Heart of Hakkar Quest) : On-Use: +204 Spell Damage, diminishing with each spell cast.
- **`12930`** : **Briarwood Reed** (Jed Runewatcher - UBRS) : +29 Spell Damage.
- **`21625`** : **Scarab Brooch** (Viscidus - AQ40) : On-Use: Healing spells apply a shield for 15% of the amount healed for 30s.

### ✝️ Healer & MP5 Trinkets
- **`23047`** : **Eye of the Dead** (Sapphiron - Naxx) : On-Use: Reduces mana cost of next 5 healing spells by 150 and increases healing done by 450.
- **`23001`** : **Eye of Diminution** (Sapphiron - Naxx) : On-Use: Reduces threat caused by spells by 35% for 20 seconds.
- **`19395`** : **Rejuvenating Gem** (Ebonroc/Firemaw/Flamegor - BWL) : +66 Healing, +9 MP5. (Universal BiS Healer Trinket).
- **`18467`** : **Royal Seal of Eldre'Thalas** (Dire Maul Class Book) : +33 Healing, +4 MP5.
- **`19288`** : **Darkmoon Card: Blue Dragon** (Beasts Deck) : 2% chance on cast to allow 100% mana regen to continue while casting for 15 sec.

### 🛡️ Tank Mitigation & Avoidance Trinkets
- **`19431`** : **Styleen's Impeding Scarab** (Flamegor - BWL) : +13 Defense, +24 Block Value, +5% Chance to Block.
- **`17064`** : **Shard of the Scale** (Onyxia) : +16 MP5 / Regen.
- **`19947`** : **Nat Pagle's Broken Reel** (ZG Fishing) : On-Use: +10% Chance to Hit for 15s.
- **`11810`** : **Force of Will** (BRD Angerforge) : +10 Defense, +1% Dodge, On-hit: 25 Armor absorb.

---

## 14. BiS Rings, Amulets & Off-Set Equipment

### 💍 Rings & Bands
- **`21709`** : **Ring of the Fallen God** (C'Thun Eye Quest) : +6 Strength, +5 Agility, +8 Stamina, +1% Hit, +1% Crit. (Universal BiS physical ring).
- **`17063`** : **Band of Accuria** (Ragnaros - MC) : +16 Agility, +2% Hit, +1% Crit.
- **`18821`** : **Quick Strike Ring** (Magmadar - MC) : +5 Strength, +8 Agility, +1% Crit, +30 Attack Power.
- **`23038`** : **Band of Unnatural Forces** (Loatheb - Naxx) : +14 Stamina, +12 Intellect, +27 Spell Dmg, +1% Spell Crit, +1% Spell Hit.
- **`21596`** : **Ring of the Godslayer** (C'Thun) : +27 Spell Damage, +6 MP5.
- **`19382`** : **Pure Elementium Band** (Nefarian - BWL) : +10 Stamina, +9 Intellect, +44 Healing.
- **`19434`** : **Band of Dark Dominion** (BWL Trash) : +13 Stamina, +33 Shadow Spell Damage.
- **`19384`** : **Master Dragonslayer's Ring** (Nefarian Head Quest) : +14 Stamina, +48 Attack Power, +1% Hit.
- **`19397`** : **Ring of Blackrock** (Firemaw - BWL) : +19 Stamina, +9 Defense.

### 📿 Amulets & Necklaces
- **`19377`** : **Prestor's Talisman of Connivery** (Nefarian Head Quest) : +30 Agility, +1% Hit.
- **`23053`** : **Stormrage's Talisman of Seething** (Kel'Thuzad - Naxx) : +12 Strength, +12 Agility, +12 Stamina, +26 Attack Power, +2% Crit.
- **`23057`** : **Gem of Trapped Innocents** (Kel'Thuzad - Naxx) : +7 Stamina, +9 Intellect, +15 Spell Dmg, +2% Spell Crit.
- **`18814`** : **Choker of the Fire Lord** (Ragnaros - MC) : +7 Stamina, +7 Intellect, +34 Spell Damage.
- **`18404`** : **Onyxia Tooth Pendant** (Onyxia Head Quest) : +12 Agility, +9 Stamina, +1% Hit, +1% Crit, +10 Fire Resist.
- **`18406`** : **Onyxia Blood Talisman** (Onyxia Head Quest - Tank) : +15 Fire Resist, +1% Parry, +1% Defense.
- **`21504`** : **Charm of the Shifting Sands** (AQ20 Head Quest) : +12 Stamina, +25 Spell Damage, +1% Spell Hit.
- **`21712`** : **Amulet of the Fallen God** (C'Thun Eye Quest - Caster) : +11 Stamina, +6 Intellect, +18 Spell Damage, +1% Spell Crit, +1% Spell Hit.

### 🧣 Cloaks & Capes
- **`23045`** : **Shroud of Dominion** (Sapphiron - Naxx) : +11 Stamina, +50 Attack Power, +1% Crit.
- **`23050`** : **Cloak of the Necropolis** (Sapphiron - Naxx) : +8 Stamina, +7 Intellect, +27 Spell Dmg, +1% Spell Crit.
- **`21710`** : **Cloak of the Fallen God** (C'Thun) : +11 Stamina, +37 Spell Damage, +1% Spell Penetration.
- **`21621`** : **Cloak of the Golden Hive** (Princess Huhuran - AQ40) : +10 Stamina, +13 Agility, +1% Hit, +1% Crit.
- **`19386`** : **Elementium Threaded Cloak** (Chromaggus - BWL) : +169 Armor, +13 Stamina, +7 Agility, +2% Dodge.
- **`19398`** : **Cloak of Firemaw** (Firemaw - BWL) : +12 Stamina, +7 Intellect, +24 Spell Damage.
- **`13340`** : **Cape of the Black Baron** (Baron Rivendare - Strat) : +15 Agility, +20 Attack Power.

---

## 15. Consumables, Flasks, Potions & World Buff Items Directory

### 🧪 Raid Flasks (Persistent Through Death)
- **`13512`** : **Flask of Supreme Power** : +150 Spell Damage for 2 hours.
- **`13510`** : **Flask of the Titans** : +1200 Maximum Health for 2 hours.
- **`13511`** : **Flask of Distilled Wisdom** : +2000 Maximum Mana for 2 hours.
- **`13513`** : **Flask of Chromatic Resistance** : +25 All Elemental & Magic Resistances for 2 hours.
- **`13506`** : **Flask of Petrification** : Turns you to stone, protecting from all physical/spell damage for 1 min.

### 🍷 Combat Elixirs & Potions
- **`13452`** : **Elixir of the Mongoose** : +25 Agility, +2% Crit for 1 hour.
- **`13454`** : **Greater Arcane Elixir** : +35 Spell Damage for 1 hour.
- **`9264`**  : **Elixir of Shadow Power** : +40 Shadow Spell Damage for 30 minutes.
- **`21546`** : **Elixir of Greater Firepower** : +40 Fire Spell Damage for 30 minutes.
- **`17708`** : **Elixir of Frost Power** : +15 Frost Spell Damage for 30 minutes.
- **`9206`**  : **Elixir of Giants** : +25 Strength for 1 hour.
- **`13445`** : **Elixir of Superior Defense** : +450 Armor for 1 hour.
- **`20007`** : **Mageblood Potion** : +12 MP5 for 1 hour.
- **`13455`** : **Greater Stoneshield Potion** : +2000 Armor for 2 minutes.
- **`13444`** : **Major Mana Potion** : Restores 1350 to 2250 Mana immediately.
- **`13446`** : **Major Healing Potion** : Restores 1050 to 1750 Health immediately.
- **`13457`** : **Greater Fire Protection Potion (GFPP)** : Absorbs 1950 to 3250 Fire damage for 1 hour.
- **`13458`** : **Greater Nature Protection Potion (GNPP)** : Absorbs 1950 to 3250 Nature damage for 1 hour.
- **`13459`** : **Greater Shadow Protection Potion (GSPP)** : Absorbs 1950 to 3250 Shadow damage for 1 hour.
- **`13456`** : **Greater Frost Protection Potion (GFstPP)** : Absorbs 1950 to 3250 Frost damage for 1 hour.
- **`13461`** : **Greater Arcane Protection Potion** : Absorbs 1950 to 3250 Arcane damage for 1 hour.
- **`5634`**  : **Free Action Potion (FAP)** : Immune to Stun and Movement Impairing effects for 30s.
- **`20008`** : **Living Action Potion (LAP)** : Clears active Stun/Slow effects and grants immunity for 5s.
- **`3387`**  : **Limited Invulnerability Potion (LIP)** : Immune to physical attacks for 6 seconds.
- **`2459`**  : **Swiftness Potion** : Increases run speed by +50% for 15 seconds.
- **`9172`**  : **Invisibility Potion** : Grants full invisibility for 18 seconds.

### 🍖 Food Buffs, Juju & World Items
- **`13928`** : **Grilled Squid** : +10 Agility for 10 minutes.
- **`20452`** : **Smoked Desert Dumplings** : +20 Strength for 15 minutes.
- **`13931`** : **Nightfin Soup** : +8 MP5 for 10 minutes.
- **`18254`** : **Runn Tum Tuber Surprise** : +10 Intellect for 10 minutes.
- **`12451`** : **Juju Power** : +30 Strength for 30 minutes.
- **`12460`** : **Juju Might** : +40 Attack Power for 10 minutes.
- **`12450`** : **Juju Flurry** : +3% Attack Speed for 20 seconds.
- **`12455`** : **Juju Ember** : +15 Fire Resistance for 10 minutes.
- **`12457`** : **Juju Chill** : +15 Frost Resistance for 10 minutes.
- **`12459`** : **Juju Escape** : +5% Dodge for 10 seconds.
- **`12458`** : **Juju Guile** : +30 Intellect for 30 minutes.
- **`12820`** : **Winterfall Firewater** : +35 Melee Attack Power and increases character scale for 20 min.
- **`18422`** : **Head of Onyxia** : Starts quest to trigger *Rallying Cry of the Dragonslayer* in Stormwind/Orgrimmar.
- **`19003`** : **Head of Nefarian** : Starts quest to trigger *Rallying Cry of the Dragonslayer*.
- **`19802`** : **Heart of Hakkar** : Starts quest to trigger *Spirit of Zandalar* on Yojamba Isle.

---

## 16. Permanent Weapon & Armor Enchantments Directory

### 🗡️ Weapon & Armor Enchant Formulas
- **`16252`** : **Formula: Enchant Weapon - Crusader** (Chance on hit: +100 Strength & restores 75–125 HP).
- **`18259`** : **Formula: Enchant Weapon - Spell Power** (+30 Spell Damage to 1H/2H Weapon).
- **`18260`** : **Formula: Enchant Weapon - Healing Power** (+55 Healing to 1H/2H Weapon).
- **`19445`** : **Formula: Enchant Weapon - Agility** (+15 Agility to 1H Weapon).
- **`22392`** : **Formula: Enchant 2H Weapon - Agility** (+25 Agility to 2H Weapon).
- **`16254`** : **Formula: Enchant Weapon - Lifestealing** (Chance on hit: 30 Life drain).
- **`16248`** : **Formula: Enchant Weapon - Unholy** (Chance on hit: Shadow debuff -15 AP).
- **`16253`** : **Formula: Enchant Chest - Greater Stats** (+4 All Stats).
- **`16221`** : **Formula: Enchant Chest - Major Health** (+100 Health).
- **`16219`** : **Formula: Enchant Gloves - Greater Agility** (+7 Agility).
- **`20731`** : **Formula: Enchant Gloves - Superior Agility** (+15 Agility).

### 📜 Head & Leg Arcanums / ZG Class Enchants
- **`18329`** : **Arcanum of Rapidity** (+1% Attack Speed to Head/Legs).
- **`18330`** : **Arcanum of Focus** (+8 Spell Damage & Healing to Head/Legs).
- **`18331`** : **Arcanum of Protection** (+1% Dodge to Head/Legs).
- **`11642`** : **Lesser Arcanum of Voracity** (+8 Strength, Agility, Stamina, Intellect, or Spirit).
- **`19782`** : **Presence of Might** (Warrior ZG: +10 Stamina, +7 Defense, +15 Shield Block Value).
- **`19783`** : **Syncretist's Sigil** (Paladin ZG: +10 Stamina, +7 Defense, +24 Healing).
- **`19784`** : **Death's Embrace** (Rogue ZG: +28 Attack Power, +1% Dodge).
- **`19785`** : **Falcon's Call** (Hunter ZG: +24 Ranged AP, +10 Stamina, +1% Hit).
- **`19786`** : **Vodouisant's Vigilant Embrace** (Shaman ZG: +15 Intellect, +13 Spell Dmg/Healing).
- **`19787`** : **Presence of Sight** (Mage/Warlock ZG: +18 Spell Damage, +1% Spell Hit).
- **`19788`** : **Hoodoo Hex** (Warlock/Shadow ZG: +10 Stamina, +18 Shadow Spell Damage).
- **`19789`** : **Prophetic Aura** (Priest ZG: +24 Healing, +4 Stamina, +4 MP5).
- **`19790`** : **Animist's Caress** (Druid ZG: +10 Stamina, +10 Intellect, +24 Healing).

---

## 17. How to Optimize & Deploy BiS Gear for Turtle-WoW Latest Content

When jumping into **Turtle-WoW's latest custom endgame raids and dungeons**, standard Vanilla BiS gearing strategies must be adapted for new mechanics, high armor values, and heavy elemental damage profiles:

### 🗺️ The Turtle-WoW Endgame Raid Hierarchy
1. **Emerald Sanctum (Map ID `807` - 40-Man Raid)**: Heavy Nature damage, acid breaths, and dragonkin mechanics. Bosses like *Solnius* and *Erennius* hit hard and require rapid dispels and Nature Resistance.
2. **Scarlet Citadel (Map ID `45` - 40-Man Raid)**: Intense Holy and Fire damage, high-density add pulls, and enrage mechanics.
3. **Lower Karazhan Halls (Map ID `532` - 10-Man Raid)**: Shadow and Arcane mechanics requiring tight interrupts, high individual DPS checks, and crowd control.
4. **Karazhan Crypt (Map ID `800` - 5-Man Dungeon)** & **Hateforge Quarry (Map ID `808` - 5-Man Dungeon)**: High-tier 5-man progression instances dropping pre-raid and Tier 2 equivalent gear.

---

### 🛡️ 1. Tanking Strategy & Weapon Swapping in Custom Raids

#### When to Run Dual-Wield Fury/Prot:
- **Use for**: High-threat single-target boss pulls, threat-sensitive DPS phases, and speed runs.
- **Loadout**: Thunderfury (`19019`) Main Hand + The Hungering Cold (`23577`) Off-Hand.
- **Why**: The Hungering Cold gives **+6 Swords Skill**, raising your weapon skill above the critical **305 cap**. This reduces the glancing blow damage reduction on Level 63 raid bosses from 40% down to just 5%, ensuring maximum Rage generation and 1,200+ TPS.

#### When to Shield Swap to Deep Prot / Dreadnaught:
- **Use for**: Boss enrages, heavy physical cleaves, and crushing blow phases (e.g. *Solnius* enrage, *Patchwerk* hateful strikes).
- **Loadout**: Thunderfury (`19019`) + The Face of Death (`23043`) or Elementium Reinforced Bulwark (`19349`).
- **Why**: Pushes crushing blows completely off the combat table using *Shield Block* and achieves 440+ Defense to become uncrittable.

---

### 🌿 2. Elemental Resistance Gear Requirements

| Encounter / Raid | Required Resistance | Target Cap | Mandatory Consumables |
| :--- | :---: | :---: | :--- |
| **Emerald Sanctum (Solnius / Erennius)** | **Nature Resistance** | **250+ NR** | Greater Nature Protection Potion (`13458`), Juju Chill (`12457`), Bramblewood / Sandstalker gear. |
| **Scarlet Citadel & Hateforge Quarry** | **Fire Resistance** | **220+ FR** | Greater Fire Protection Potion (`13457`), Juju Ember (`12455`), Elementium Threaded Cloak (`19386`), Onyxia Tooth Pendant (`18404`). |
| **Lower Karazhan Halls & Naxxramas** | **Shadow Resistance** | **200+ SR** | Greater Shadow Protection Potion (`13459`), Band of Dark Dominion (`19434`), Cloak of the Necropolis (`23050`). |

---

### 🔮 3. Caster & Healer Scaling in Extended Encounters

1. **Spell Hit Cap (16% Mandatory)**:
   - Bosses in Emerald Sanctum and Scarlet Citadel have high spell resist levels.
   - Fire Mages must equip **Neltharion's Tear** (`19379`) and **Band of Unnatural Forces** (`23038`) alongside 3/3 Elemental Precision to eliminate spell resists on rolling *Ignite* stacks.
   - Warlocks must prioritize **Neltharion's Tear** and **Sapphiron's Left Eye** (`23049`) to ensure *Curse of Shadows* and *Shadow Bolts* never miss.

2. **Healer MP5 vs +Healing Balance**:
   - Turtle-WoW custom raid encounters are longer than standard Vanilla bosses.
   - Pure +Healing gear will run you out of mana. Balance high healing power with **Rejuvenating Gem** (`19395`), **Eye of the Dead** (`23047`), **Mageblood Potion** (`20007`), and **Nightfin Soup** (`13931`).

---

### 🚀 4. Ready-to-Use Raid Preparation Lua Macros

#### 🌿 Macro: 1-Click Nature Resistance Gear Swap (Emerald Sanctum)
```lua
.additem 13458 5
.additem 12457 20
.additem 13513 2
```

#### 🔥 Macro: 1-Click Fire Resistance Gear Swap (Scarlet Citadel)
```lua
.additem 13457 5
.additem 12455 20
.additem 19386 1
.additem 18404 1
```

#### 📦 Macro: Complete 1-Click Raid Consumable Restock
```lua
.additem 13512 2
.additem 13510 2
.additem 13452 10
.additem 13454 10
.additem 13444 20
.additem 13446 20
.additem 5634 10
.additem 20008 10
.additem 3387 5
.additem 13928 20
.additem 20452 20
.repairitems
```

---

<p align="center">
  [⬅ Prev: 16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 18. Class Talent Specs ➡](./18_class_talent_specs.md)
</p>
