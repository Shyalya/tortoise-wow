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
Full 9-piece Dreadnaught, Thunderfury, The Face of Death shield, Drake Fang Talisman, Styleen's Impeding Scarab, Band of Accuria, and Ring of the Dreadnaught:
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
.additem 22818 1
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
.additem 21700 1
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
.additem 21700 1
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
Full 9-piece Cryptstalker, Nerubian Slavemaker, Eye of Nerub (2H Polearm), Kiss of the Spider, Drake Fang Talisman, Band of Accuria, and Ring of the Fallen God:
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
.additem 23037 1
.additem 22954 1
.additem 19406 1
.additem 21700 1
.repairitems
```

### 🔥 Fire / Frost Mage (Caster DPS BiS)
Full 9-piece Frostfire, Atiesh Greatstaff of the Guardian (Mage), Neltharion's Tear, Restrained Essence of Sapphiron, Band of Unnatural Forces, Ring of the Fallen God, and Gem of Trapped Innocents:
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
.additem 21700 1
.additem 23023 1
.additem 22820 1
.repairitems
```

### 💀 Affliction / Destruction Warlock (Shadow DPS BiS)
Full 9-piece Plagueheart, Atiesh Greatstaff of the Guardian (Warlock), Neltharion's Tear, Restrained Essence of Sapphiron, Band of Unnatural Forces, and Wand of the Whispering Dead:
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
.additem 23023 1
.additem 22820 1
.repairitems
```

### ✝️ Holy Priest (Raid Healer BiS)
Full 9-piece Vestments of Faith, Atiesh Greatstaff of the Guardian (Priest), Eye of the Dead, Rejuvenating Gem, Ring of Faith, Band of Servitude, and Wand of the Whispering Dead:
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
.additem 23001 1
.additem 19395 1
.additem 23023 1
.additem 22820 1
.repairitems
```

### 🔨 Holy / Retribution Paladin (Support & Healer BiS)
Full 9-piece Redemption Armor, The End of Dreams (Kel'Thuzad 1H Mace), Shield of the Fallen God, Eye of the Dead, Rejuvenating Gem, and Ring of Redemption:
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
.additem 21701 1
.additem 23001 1
.additem 19395 1
.repairitems
```

### ⚡ Restoration / Elemental Shaman (Healer & Caster BiS)
Full 9-piece The Earthshatterer, The End of Dreams, Shield of the Fallen God, Eye of the Dead, Neltharion's Tear, and Ring of the Earthshatterer:
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
.additem 21701 1
.additem 23001 1
.additem 19379 1
.repairitems
```

### 🐾 Restoration / Feral Druid (Healer & Tank BiS)
Full 9-piece Dreamwalker Raiment, Atiesh Greatstaff of the Guardian (Druid), Eye of the Dead, Rejuvenating Gem, Ring of the Dreamwalker, and Band of Servitude:
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
.additem 23001 1
.additem 19395 1
.repairitems
```

---

## 2. Legendary & Artifact Weapons Directory

| Item ID | Item Name | Slot | Quality | Unique Effects & Description |
| :---: | :--- | :---: | :---: | :--- |
| **`19019`** | **Thunderfury, Blessed Blade of the Windseeker** | 1H Sword | Legendary | Chance on hit: 300 Nature blast + chain lightning debuff (-25 Nature resist, -20% attack speed). Highest threat weapon in the game. |
| **`17182`** | **Sulfuras, Hand of Ragnaros** | 2H Mace | Legendary | Chance on hit: 273–333 Fireball + 5 Fire damage shield. 223–372 Physical damage (80.4 DPS). |
| **`22589`** | **Atiesh, Greatstaff of the Guardian (Mage)** | 2H Staff | Legendary | +28 Spell Power to group, +2% Spell Crit to group, Karazhan teleport portal on 1-min cooldown. |
| **`22630`** | **Atiesh, Greatstaff of the Guardian (Warlock)** | 2H Staff | Legendary | +33 Spell Damage & Healing to group, +2% Spell Hit to group, Karazhan teleport portal. |
| **`22631`** | **Atiesh, Greatstaff of the Guardian (Priest)** | 2H Staff | Legendary | +62 Healing to group, +11 MP5 to group, Karazhan teleport portal. |
| **`22632`** | **Atiesh, Greatstaff of the Guardian (Druid)** | 2H Staff | Legendary | +11 Feral Attack Power to group, +44 Healing to group, +11 MP5 to group, Karazhan teleport portal. |
| **`22691`** | **Corrupted Ashbringer** | 2H Sword | Epic | 259–389 Damage (90.0 DPS), +2% Crit, +2% Hit, Chance on Hit: Life Steal 185–215. Triggers Scarlet Monastery lore event. |
| **`13262`** | **Ashbringer (Purified Blade)** | 2H Sword | Legendary | 201–247 Damage (74.7 DPS), Holy burst damage proc. The legendary weapon of Highlord Alexandros Mograine. |
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
- **`22423`** : Dreadnaught Wristguards (Wrists)
- **`23059`** : Ring of the Dreadnaught (Finger)

### 🔨 Paladin: Redemption Armor
- **`22424`** : Redemption Tunic (Chest)
- **`22425`** : Redemption Legguards (Legs)
- **`22426`** : Redemption Headpiece (Head)
- **`22427`** : Redemption Spaulders (Shoulders)
- **`22428`** : Redemption Boots (Feet)
- **`22429`** : Redemption Handguards (Hands)
- **`22430`** : Redemption Girdle (Waist)
- **`22431`** : Redemption Wristguards (Wrists)
- **`23066`** : Ring of Redemption (Finger)

### 🏹 Hunter: Cryptstalker Armor
- **`22436`** : Cryptstalker Tunic (Chest)
- **`22437`** : Cryptstalker Legguards (Legs)
- **`22438`** : Cryptstalker Headpiece (Head)
- **`22439`** : Cryptstalker Spaulders (Shoulders)
- **`22440`** : Cryptstalker Boots (Feet)
- **`22441`** : Cryptstalker Handguards (Hands)
- **`22442`** : Cryptstalker Girdle (Waist)
- **`22443`** : Cryptstalker Wristguards (Wrists)
- **`23067`** : Ring of the Cryptstalker (Finger)

### 🗡️ Rogue: Bonescythe Armor
- **`22476`** : Bonescythe Breastplate (Chest)
- **`22477`** : Bonescythe Legplates (Legs)
- **`22478`** : Bonescythe Helmet (Head)
- **`22479`** : Bonescythe Pauldrons (Shoulders)
- **`22480`** : Bonescythe Sabatons (Feet)
- **`22481`** : Bonescythe Gauntlets (Hands)
- **`22482`** : Bonescythe Waistguard (Waist)
- **`22483`** : Bonescythe Wristguards (Wrists)
- **`23060`** : Ring of the Bonescythe (Finger)

### ✝️ Priest: Vestments of Faith
- **`22512`** : Robe of Faith (Chest)
- **`22513`** : Leggings of Faith (Legs)
- **`22514`** : Circlet of Faith (Head)
- **`22515`** : Shoulderpads of Faith (Shoulders)
- **`22516`** : Sandals of Faith (Feet)
- **`22517`** : Gloves of Faith (Hands)
- **`22518`** : Belt of Faith (Waist)
- **`22519`** : Bindings of Faith (Wrists)
- **`23061`** : Ring of Faith (Finger)

### ⚡ Shaman: The Earthshatterer
- **`22464`** : Earthshatterer Tunic (Chest)
- **`22465`** : Earthshatterer Legguards (Legs)
- **`22466`** : Earthshatterer Headpiece (Head)
- **`22467`** : Earthshatterer Spaulders (Shoulders)
- **`22468`** : Earthshatterer Boots (Feet)
- **`22469`** : Earthshatterer Handguards (Hands)
- **`22470`** : Earthshatterer Girdle (Waist)
- **`22471`** : Earthshatterer Wristguards (Wrists)
- **`23065`** : Ring of the Earthshatterer (Finger)

### 🔥 Mage: Frostfire Regalia
- **`22496`** : Frostfire Robe (Chest)
- **`22497`** : Frostfire Leggings (Legs)
- **`22498`** : Frostfire Circlet (Head)
- **`22499`** : Frostfire Shoulderpads (Shoulders)
- **`22500`** : Frostfire Sandals (Feet)
- **`22501`** : Frostfire Gloves (Hands)
- **`22502`** : Frostfire Belt (Waist)
- **`22503`** : Frostfire Bindings (Wrists)
- **`23062`** : Ring of the Frostfire (Finger)

### 💀 Warlock: Plagueheart Raiment
- **`22504`** : Plagueheart Robe (Chest)
- **`22505`** : Plagueheart Leggings (Legs)
- **`22506`** : Plagueheart Circlet (Head)
- **`22507`** : Plagueheart Shoulderpads (Shoulders)
- **`22508`** : Plagueheart Sandals (Feet)
- **`22509`** : Plagueheart Gloves (Hands)
- **`22510`** : Plagueheart Belt (Waist)
- **`22511`** : Plagueheart Bindings (Wrists)
- **`23063`** : Ring of the Plagueheart (Finger)

### 🐾 Druid: Dreamwalker Raiment
- **`22488`** : Dreamwalker Tunic (Chest)
- **`22489`** : Dreamwalker Legguards (Legs)
- **`22490`** : Dreamwalker Headpiece (Head)
- **`22491`** : Dreamwalker Spaulders (Shoulders)
- **`22492`** : Dreamwalker Boots (Feet)
- **`22493`** : Dreamwalker Handguards (Hands)
- **`22494`** : Dreamwalker Girdle (Waist)
- **`22495`** : Dreamwalker Wristguards (Wrists)
- **`23064`** : Ring of the Dreamwalker (Finger)

---

## 4. Temple of Ahn'Qiraj Tier 2.5 Armor Sets (5 Pieces)

### 🛡️ Warrior: Conqueror's Battlegear
- **`21331`** : Conqueror's Breastplate (Chest)
- **`21332`** : Conqueror's Legguards (Legs)
- **`21329`** : Conqueror's Crown (Head)
- **`21330`** : Conqueror's Spaulders (Shoulders)
- **`21333`** : Conqueror's Greaves (Feet)

### 🔨 Paladin: Avenger's Battlegear
- **`21389`** : Avenger's Breastplate (Chest)
- **`21390`** : Avenger's Legguards (Legs)
- **`21387`** : Avenger's Crown (Head)
- **`21388`** : Avenger's Pauldrons (Shoulders)
- **`21391`** : Avenger's Greaves (Feet)

### 🏹 Hunter: Striker's Garb
- **`21366`** : Striker's Hauberk (Chest)
- **`21368`** : Striker's Leggings (Legs)
- **`21365`** : Striker's Diadem (Head)
- **`21367`** : Striker's Pauldrons (Shoulders)
- **`21370`** : Striker's Footguards (Feet)

### 🗡️ Rogue: Deathdealer's Embrace
- **`21360`** : Deathdealer's Vest (Chest)
- **`21362`** : Deathdealer's Leggings (Legs)
- **`21359`** : Deathdealer's Helm (Head)
- **`21361`** : Deathdealer's Spaulders (Shoulders)
- **`21364`** : Deathdealer's Boots (Feet)

### ✝️ Priest: Garments of the Oracle
- **`21351`** : Vestments of the Oracle (Chest)
- **`21352`** : Trousers of the Oracle (Legs)
- **`21349`** : Tiara of the Oracle (Head)
- **`21350`** : Mantle of the Oracle (Shoulders)
- **`21357`** : Footwraps of the Oracle (Feet)

### ⚡ Shaman: Stormcaller's Garb
- **`21374`** : Stormcaller's Hauberk (Chest)
- **`21375`** : Stormcaller's Leggings (Legs)
- **`21372`** : Stormcaller's Diadem (Head)
- **`21373`** : Stormcaller's Pauldrons (Shoulders)
- **`21376`** : Stormcaller's Footguards (Feet)

### 🔥 Mage: Enigma Regalia
- **`21344`** : Enigma Robes (Chest)
- **`21346`** : Enigma Leggings (Legs)
- **`21343`** : Enigma Circlet (Head)
- **`21345`** : Enigma Shoulderpads (Shoulders)
- **`21347`** : Enigma Boots (Feet)

### 💀 Warlock: Doomcaller's Attire
- **`21335`** : Doomcaller's Robes (Chest)
- **`21337`** : Doomcaller's Trousers (Legs)
- **`21334`** : Doomcaller's Circlet (Head)
- **`21336`** : Doomcaller's Mantle (Shoulders)
- **`21338`** : Doomcaller's Footwraps (Feet)

### 🐾 Druid: Genesis Raiment
- **`21354`** : Genesis Vest (Chest)
- **`21356`** : Genesis Trousers (Legs)
- **`21353`** : Genesis Helm (Head)
- **`21355`** : Genesis Shoulderpads (Shoulders)
- **`21358`** : Genesis Boots (Feet)

---

## 5. Blackwing Lair & Molten Core Tier 2 Armor Sets (8 Pieces)

### 🛡️ Warrior: Battlegear of Wrath
- **`16960`** : Breastplate of Wrath (Chest)
- **`16963`** : Legplates of Wrath (Legs - Ragnaros)
- **`16962`** : Helm of Wrath (Head - Onyxia)
- **`16964`** : Pauldrons of Wrath (Shoulders)
- **`16965`** : Sabatons of Wrath (Feet)
- **`16961`** : Gauntlets of Wrath (Hands)
- **`16966`** : Waistguard of Wrath (Waist)
- **`16959`** : Bracelets of Wrath (Wrists)

### 🔨 Paladin: Judgement Armor
- **`16958`** : Judgement Breastplate (Chest)
- **`16954`** : Judgement Legplates (Legs - Ragnaros)
- **`16955`** : Judgement Crown (Head - Onyxia)
- **`16953`** : Judgement Spaulders (Shoulders)
- **`16957`** : Judgement Sabatons (Feet)
- **`16956`** : Judgement Gauntlets (Hands)
- **`16952`** : Judgement Belt (Waist)
- **`16951`** : Judgement Bindings (Wrists)

### 🏹 Hunter: Dragonstalker Armor
- **`16942`** : Dragonstalker's Breastplate (Chest)
- **`16938`** : Dragonstalker's Legguards (Legs - Ragnaros)
- **`16939`** : Dragonstalker's Helm (Head - Onyxia)
- **`16937`** : Dragonstalker's Spaulders (Shoulders)
- **`16941`** : Dragonstalker's Greaves (Feet)
- **`16940`** : Dragonstalker's Gauntlets (Hands)
- **`16936`** : Dragonstalker's Belt (Waist)
- **`16935`** : Dragonstalker's Bracers (Wrists)

### 🗡️ Rogue: Bloodfang Armor
- **`16905`** : Bloodfang Chestpiece (Chest)
- **`16909`** : Bloodfang Pants (Legs - Ragnaros)
- **`16908`** : Bloodfang Hood (Head - Onyxia)
- **`16910`** : Bloodfang Spaulders (Shoulders)
- **`16906`** : Bloodfang Boots (Feet)
- **`16907`** : Bloodfang Gloves (Hands)
- **`16911`** : Bloodfang Belt (Waist)
- **`16832`** : Bloodfang Bracers (Wrists)

### ✝️ Priest: Vestments of Transcendence
- **`16923`** : Robes of Transcendence (Chest)
- **`16921`** : Leggings of Transcendence (Legs - Ragnaros)
- **`16922`** : Halo of Transcendence (Head - Onyxia)
- **`16924`** : Pauldrons of Transcendence (Shoulders)
- **`16919`** : Boots of Transcendence (Feet)
- **`16920`** : Handguards of Transcendence (Hands)
- **`16925`** : Belt of Transcendence (Waist)
- **`16926`** : Bindings of Transcendence (Wrists)

### ⚡ Shaman: The Ten Storms
- **`16949`** : Breastplate of The Ten Storms (Chest)
- **`16946`** : Legplates of The Ten Storms (Legs - Ragnaros)
- **`16947`** : Helmet of The Ten Storms (Head - Onyxia)
- **`16945`** : Epaulets of The Ten Storms (Shoulders)
- **`16948`** : Greaves of The Ten Storms (Feet)
- **`16944`** : Gauntlets of The Ten Storms (Hands)
- **`16950`** : Belt of The Ten Storms (Waist)
- **`16943`** : Bracers of The Ten Storms (Wrists)

### 🔥 Mage: Netherwind Regalia
- **`16915`** : Netherwind Robes (Chest)
- **`16914`** : Netherwind Pants (Legs - Ragnaros)
- **`16818`** : Netherwind Crown (Head - Onyxia)
- **`16916`** : Netherwind Mantle (Shoulders)
- **`16912`** : Netherwind Boots (Feet)
- **`16913`** : Netherwind Gloves (Hands)
- **`16917`** : Netherwind Belt (Waist)
- **`16918`** : Netherwind Bindings (Wrists)

### 💀 Warlock: Nemesis Raiment
- **`16931`** : Nemesis Robes (Chest)
- **`16929`** : Nemesis Leggings (Legs - Ragnaros)
- **`16930`** : Nemesis Skullcap (Head - Onyxia)
- **`16932`** : Nemesis Spaulders (Shoulders)
- **`16927`** : Nemesis Boots (Feet)
- **`16928`** : Nemesis Gloves (Hands)
- **`16933`** : Nemesis Belt (Waist)
- **`16934`** : Nemesis Bracers (Wrists)

### 🐾 Druid: Stormrage Raiment
- **`16903`** : Stormrage Chestguard (Chest)
- **`16901`** : Stormrage Legguards (Legs - Ragnaros)
- **`16900`** : Stormrage Cover (Head - Onyxia)
- **`16902`** : Stormrage Pauldrons (Shoulders)
- **`16898`** : Stormrage Boots (Feet)
- **`16899`** : Stormrage Handguards (Hands)
- **`16904`** : Stormrage Belt (Waist)
- **`16897`** : Stormrage Bracers (Wrists)

---

## 6. Molten Core Tier 1 Armor Sets (8 Pieces)

| Class | Chest | Legs | Head | Shoulders | Feet | Hands | Waist | Wrists |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Warrior (Might)** | `16865` | `16867` | `16866` | `16868` | `16862` | `16863` | `16864` | `16861` |
| **Paladin (Lawbringer)** | `16853` | `16855` | `16854` | `16856` | `16859` | `16860` | `16858` | `16857` |
| **Hunter (Giantstalker)** | `16845` | `16847` | `16846` | `16848` | `16851` | `16852` | `16850` | `16849` |
| **Rogue (Nightslayer)** | `16820` | `16822` | `16821` | `16823` | `16826` | `16827` | `16825` | `16824` |
| **Priest (Prophecy)** | `16815` | `16817` | `16816` | `16818` | `16811` | `16812` | `16814` | `16819` |
| **Shaman (Earthfury)** | `16841` | `16843` | `16842` | `16844` | `16839` | `16840` | `16838` | `16837` |
| **Mage (Arcanist)** | `16798` | `16800` | `16799` | `16801` | `16802` | `16795` | `16797` | `16796` |
| **Warlock (Felheart)** | `16809` | `16810` | `16808` | `16807` | `16804` | `16805` | `16806` | `16803` |
| **Druid (Cenarion)** | `16833` | `16835` | `16834` | `16836` | `16829` | `16830` | `16831` | `16828` |

---

## 7. Zul'Gurub 5-Piece Class Sets

| Class & Set Name | Chest | Shoulders | Wrists | Belt | Trinket |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Warrior (Vindicator's Battlegear)** | `19822` | `19823` | `19824` | `19825` | `19951` |
| **Paladin (Freethinker's Armor)** | `19826` | `19827` | `19828` | `19829` | `19952` |
| **Hunter (Predator's Armor)** | `19830` | `19831` | `19832` | `19833` | `19953` |
| **Rogue (Madcap's Outfit)** | `19834` | `19835` | `19836` | `19837` | `19954` |
| **Priest (Confessor's Raiment)** | `19841` | `19842` | `19843` | `19838` | `19956` |
| **Shaman (Augur's Regalia)** | `19839` | `19840` | `19844` | `19845` | `19957` |
| **Mage (Illusionist's Attire)** | `19846` | `19847` | `19848` | `19849` | `19958` |
| **Warlock (Demoniac's Threads)** | `19850` | `19851` | `19852` | `19853` | `19959` |
| **Druid (Haruspex's Garb)** | `19854` | `19855` | `19856` | `19857` | `19960` |

---

## 8. Ruins of Ahn'Qiraj (AQ20) 3-Piece Class Sets

| Class & Set Name | Ring | Cloak | Weapon / Off-Hand |
| :--- | :---: | :---: | :---: |
| **Warrior (Battlegear of Unending Strength)** | `21404` | `21405` | `21406` (2H Sickle) |
| **Paladin (Battlegear of Eternal Justice)** | `21407` | `21408` | `21409` (1H Mace) |
| **Hunter (Trappings of the Unseen Path)** | `21401` | `21402` | `21403` (2H Polearm) |
| **Rogue (Emblems of Veiled Shadows)** | `21398` | `21399` | `21400` (1H Dagger) |
| **Priest (Finery of Infinite Wisdom)** | `21410` | `21411` | `21412` (1H Mace) |
| **Shaman (Gift of the Gathering Storm)** | `21413` | `21414` | `21415` (1H Mace) |
| **Mage (Trappings of Vaulted Secrets)** | `21416` | `21417` | `21418` (1H Dagger) |
| **Warlock (Implements of Unspoken Names)** | `21419` | `21420` | `21421` (1H Dagger) |
| **Druid (Symbols of Unending Life)** | `21422` | `21423` | `21424` (1H Mace) |

---

## 9. Dungeon Sets 1 & 2 (Tier 0 & Tier 0.5 Upgrades)

### Dungeon Set 2 (Tier 0.5 Epic Upgrade)
- **Warrior (Battlegear of Heroism)**: Helm `21999`, Shoulders `22001`, Chest `21997`, Wrists `21996`, Gloves `21998`, Belt `22000`, Legs `22002`, Boots `22003`
- **Paladin (Soulforge Armor)**: Helm `22086`, Shoulders `22088`, Chest `22087`, Wrists `22091`, Gloves `22089`, Belt `22092`, Legs `22090`, Boots `22093`
- **Hunter (Beastmaster Armor)**: Helm `22060`, Shoulders `22061`, Chest `22062`, Wrists `22016`, Gloves `22017`, Belt `22018`, Legs `22063`, Boots `22064`
- **Rogue (Darkmantle Armor)**: Helm `22005`, Shoulders `22007`, Chest `22004`, Wrists `22009`, Gloves `22006`, Belt `22010`, Legs `22008`, Boots `22011`
- **Priest (Vestments of the Virtuous)**: Helm `22081`, Shoulders `22083`, Chest `22082`, Wrists `22084`, Gloves `22080`, Belt `22085`, Legs `22079`, Boots `22078`
- **Shaman (The Five Thunders)**: Helm `22095`, Shoulders `22096`, Chest `22097`, Wrists `22098`, Gloves `22100`, Belt `22099`, Legs `22101`, Boots `22102`
- **Mage (Sorcerer's Regalia)**: Helm `22065`, Shoulders `22066`, Chest `22067`, Wrists `22068`, Gloves `22070`, Belt `22069`, Legs `22071`, Boots `22072`
- **Warlock (Deathmist Raiment)**: Helm `22074`, Shoulders `22075`, Chest `22073`, Wrists `22077`, Gloves `22076`, Belt `22078`, Legs `22079`, Boots `22080`
- **Druid (Feralheart Raiment)**: Helm `22106`, Shoulders `22107`, Chest `22108`, Wrists `22109`, Gloves `22111`, Belt `22110`, Legs `22112`, Boots `22113`

---

## 10. PvP Grand Marshal & High Warlord Epic Equipment (Rank 14)

### ⚔️ Rank 14 Epic Weapons
- **`18876`** : Grand Marshal's Claymore (Alliance 2H Sword)
- **`18877`** : High Warlord's Greatsword (Horde 2H Sword)
- **`18874`** : Grand Marshal's Longsword (Alliance 1H Sword)
- **`18875`** : High Warlord's Blade (Horde 1H Sword)
- **`18838`** : Grand Marshal's Dirk (Alliance 1H Dagger)
- **`18840`** : High Warlord's Razor (Horde 1H Dagger)
- **`18865`** : Grand Marshal's Stave (Alliance 2H Staff)
- **`18866`** : High Warlord's War Staff (Horde 2H Staff)
- **`18867`** : Grand Marshal's Bullseye (Alliance Ranged Crossbow)
- **`18869`** : High Warlord's Street Sweeper (Horde Ranged Gun)
- **`18825`** : Grand Marshal's Aegis (Alliance Shield)
- **`18826`** : High Warlord's Shield (Horde Shield)

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
- **`18203`** : **Eskhandar's Left Claw** (1H Fist Weapon - 1.50 speed, Slow & Bleed proc)
- **`17070`** : **Fang of the Mystics** (1H Main Hand Mace - +40 Healing, +10 MP5)
- **`18204`** : **Unmelting Ice Girdle** (Plate Belt - +14 Str, +14 Sta, +14 Agi, +1% Dodge)
- **`18202`** : **Puissant Cape** (Cloak - +12 Sta, +1% Hit, +40 Attack Power)
- **`18208`** : **Drape of Benediction** (Cloak - +31 Healing, +10 Intellect)
- **`18205`** : **Snowblind Shoes** (Cloth Boots - +10 Sta, +10 Int, +32 Spell Damage)

### 👿 Lord Kazzak (Tainted Scar, Blasted Lands)
- **`17112`** : **Empyrean Demolisher** (1H Mace - 2.80 speed, Chance on Hit: +20% Attack Speed)
- **`17111`** : **Amberseal Keeper** (2H Staff - +44 Spell Damage, +20 Intellect, +5 MP5)
- **`17113`** : **Flayed Doomguard Belt** (Leather Belt - +14 Str, +13 Sta, +12 Agi, +1% Crit)
- **`17110`** : **Ring of Entropic Power** (Ring - +22 Spell Damage, +1% Spell Hit)
- **`17114`** : **Infernal Headcage** (Mail Helm - +20 Str, +15 Sta, +15 Agi, +1% Crit)

### 🌲 Dragons of Nightmare (Taerar, Ysondre, Lethon, Emeriss)
- **`20577`** : **Nightmare Blade** (1H Sword - 2.70 speed, +70 Armor, +32 Attack Power)
- **`20579`** : **Staff of Rampant Growth** (2H Staff - +15 Int, +15 Sta, +40 Healing, +11 MP5)
- **`20580`** : **Hammer of Bestial Fury** (2H Mace - +65 Feral Attack Power, +13 Str, +12 Sta)
- **`20581`** : **Polished Ironwood Crossbow** (Crossbow - 3.10 speed, +14 Attack Power, +1% Hit)
- **`20582`** : **Dragonspur Wraps** (Leather Wrists - +15 Str, +15 Sta, +1% Hit)
- **`20583`** : **Green Dragonscale Gauntlets** (Mail Gloves - +12 Sta, +2% Crit, +28 Attack Power)
- **`20584`** : **Trance Stone** (Off-Hand Caster - +10 Int, +8 Sta, +25 Spell Damage)
- **`20585`** : **Belt of the Dark Bog** (Leather Belt - +14 Sta, +8 Int, +29 Spell Damage)

---

## 12. BiS Weapons Directory (Organized by Weapon Type)

### ⚔️ One-Handed Swords
| Item ID | Item Name | Boss / Source | Speed | DPS | Stats & Effects |
| :---: | :--- | :--- | :---: | :---: | :--- |
| **`23054`** | **Gressil, Dawn of Ruin** | Kel'Thuzad (Naxx) | 2.70 | 73.1 | +15 Stamina, +14 Attack Power, Equip: +1% Crit. |
| **`23577`** | **The Hungering Cold** | Kel'Thuzad (Naxx) | 1.50 | 73.0 | +140 Armor, +14 Stamina, Equip: +6 Swords Skill, +1.50s Fast Off-hand. |
| **`23014`** | **Iblis, Blade of the Fallen Seraph** | Gluth / Patchwerk (Naxx) | 1.60 | 66.9 | +7 Stamina, Equip: +1% Hit, +1% Crit, +26 Attack Power. |
| **`19351`** | **Chromatically Tempered Sword (CTS)** | Chromaggus (BWL) | 2.60 | 58.5 | +14 Strength, +14 Agility, +7 Stamina. |
| **`19363`** | **Maladath, Runed Blade of the Black Flight** | Broodlord Lashlayer (BWL) | 2.20 | 56.4 | +4 Agility, Equip: +1% Parry, +4 Swords Skill. |
| **`21650`** | **Ancient Qiraji Ripper** | Fankriss (AQ40) | 2.80 | 58.4 | +11 Stamina, Equip: +1% Crit, +20 Attack Power. |
| **`17075`** | **Vis'kag the Bloodletter** | Onyxia | 2.60 | 48.3 | Chance on hit: 240 Fatal Wound burst. |
| **`18832`** | **Brutality Blade** | Garr (MC) | 2.50 | 45.4 | +9 Strength, +9 Agility, Equip: +1% Crit. |
| **`19865`** | **Warblade of the Hakkari (Main)** | Hakkar (ZG) | 2.60 | 40.8 | +28 Attack Power, Equip: Set bonus +6 Swords Skill. |
| **`19866`** | **Warblade of the Hakkari (Off)** | Bloodlord Mandokir (ZG) | 1.70 | 40.9 | +40 Attack Power, Equip: +1% Crit. |
| **`21268`** | **Ravencrest's Legacy** | AQ Scepter Quest | 2.80 | 58.4 | +13 Str, +9 Agi, +14 Sta. |

### 🗡️ Daggers
| Item ID | Item Name | Boss / Source | Speed | DPS | Stats & Effects |
| :---: | :--- | :--- | :---: | :---: | :--- |
| **`22802`** | **Kingsfall** | Kel'Thuzad (Naxx) | 1.80 | 73.1 | +16 Agility, Equip: +1% Crit, +1% Hit, +28 Attack Power. |
| **`23044`** | **Harbinger of Doom** | Gluth / Patchwerk (Naxx) | 1.60 | 66.9 | +8 Stamina, Equip: +1% Hit, +1% Crit, +26 Attack Power. |
| **`21126`** | **Death's Sting** | C'Thun (AQ40) | 1.80 | 66.4 | +10 Stamina, Equip: +38 Attack Power, +3 Daggers Skill. |
| **`19346`** | **Dragonfang Blade** | Vaelastrasz (BWL) | 1.80 | 58.3 | +16 Agility, Equip: +36 Attack Power. |
| **`21649`** | **Perforator** | Viscidus (AQ40) | 1.80 | 58.3 | +10 Stamina, Equip: +1% Crit, +24 Attack Power. |
| **`19859`** | **Fang of the Faceless** | Hakkar (ZG) | 1.90 | 41.6 | Equip: +1% Crit, +28 Attack Power. |
| **`18816`** | **Perdition's Blade** | Ragnaros (MC) | 1.80 | 51.7 | Chance on hit: 40–56 Fire burst damage. |
| **`18805`** | **Core Hound Tooth** | Majordomo Chest (MC) | 1.60 | 45.0 | +9 Stamina, Equip: +1% Crit, +20 Attack Power. |

### 🔨 Two-Handed Weapons (Swords, Maces, Axes & Polearms)
| Item ID | Item Name | Type | Boss / Source | Speed | DPS | Stats & Effects |
| :---: | :--- | :---: | :--- | :---: | :---: | :--- |
| **`22815`** | **Might of Menethil** | 2H Mace | Kel'Thuzad (Naxx) | 3.80 | 95.3 | +20 Strength, +46 Stamina, Equip: +2% Crit. (Highest 2H DPS) |
| **`23037`** | **The Eye of Nerub** | 2H Polearm | Naxxramas Trash | 3.70 | 85.9 | +28 Agility, +2% Hit, +2% Crit, +60 Attack Power. |
| **`22807`** | **Severance** | 2H Axe | Patchwerk (Naxx) | 3.60 | 85.8 | +43 Strength, +18 Stamina, Equip: +2% Crit. |
| **`21134`** | **Dark Edge of Insanity** | 2H Axe | C'Thun (AQ40) | 3.50 | 86.6 | +35 Strength, +19 Agility, +25 Stamina, Chance on Hit: 3-sec Disorient. |
| **`19364`** | **Ashkandi, Greatsword of the Brotherhood** | 2H Sword | Nefarian (BWL) | 3.50 | 81.9 | +33 Stamina, Equip: +86 Attack Power. |
| **`19334`** | **The Untamed Blade** | 2H Sword | Razorgore (BWL) | 3.40 | 66.8 | Chance on Hit: +300 Strength for 8 seconds. |
| **`17076`** | **Bonereaver's Edge** | 2H Sword | Ragnaros (MC) | 3.40 | 60.6 | +16 Stamina, Chance on Hit: Armor penetration stack (700 armor reduction per stack). |
| **`19169`** | **Nightfall** | 2H Axe | Blacksmithing Crafted | 3.50 | 53.6 | Chance on Hit: Spell Damage Taken +15% on target. Essential raid debuff weapon. |
| **`12784`** | **Arcanite Reaper** | 2H Axe | Blacksmithing Crafted | 3.80 | 53.7 | +62 Attack Power, +13 Stamina. Iconic PvP burst axe. |

### 🏹 Ranged Weapons & Bows
| Item ID | Item Name | Type | Boss / Source | Speed | DPS | Stats & Effects |
| :---: | :--- | :---: | :--- | :---: | :---: | :--- |
| **`22812`** | **Nerubian Slavemaker** | Crossbow | Kel'Thuzad (Naxx) | 3.20 | 56.6 | +24 Attack Power, +1% Crit. (Highest ranged physical DPS). |
| **`22811`** | **Soulstring** | Bow | Four Horsemen (Naxx) | 2.90 | 50.7 | +6 Stamina, +16 Attack Power, +1% Crit. |
| **`19361`** | **Ashjre'thul, Crossbow of Smiting** | Crossbow | Chromaggus (BWL) | 3.40 | 45.6 | +7 Stamina, Equip: +36 Attack Power. |
| **`18713`** | **Rhok'delar, Longbow of the Ancient Keepers** | Bow | Hunter Epic Quest | 2.90 | 44.0 | +17 Agility, Equip: +1% Crit. |
| **`17069`** | **Striker's Mark** | Bow | Magmadar (MC) | 2.50 | 39.0 | Equip: +1% Hit, +22 Attack Power. |
| **`17072`** | **Blastershot Launcher** | Gun | Golemagg (MC) | 2.60 | 39.4 | +6 Stamina, Equip: +1% Crit. |
| **`19853`** | **Gurubashi Dwarf Destroyer** | Gun | Bloodlord Mandokir (ZG) | 2.80 | 40.0 | +30 Attack Power. |

### 🔮 Staves, Caster 1H & Off-Hands
| Item ID | Item Name | Slot | Boss / Source | Stats & Effects |
| :---: | :--- | :---: | :--- | :--- |
| **`22800`** | **Brimstone Staff** | 2H Staff | Naxxramas | +18 Stamina, +18 Intellect, +84 Spell Damage, +2% Spell Crit, +1% Spell Hit. |
| **`19378`** | **Staff of the Shadow Flame** | 2H Staff | Nefarian (BWL) | +29 Stamina, +24 Intellect, +84 Spell Damage, +2% Spell Crit. |
| **`22988`** | **The End of Dreams** | 1H Mace | Kel'Thuzad (Naxx) | +14 Intellect, +14 Stamina, +52 Spell Dmg, +94 Healing, +7 MP5. |
| **`23049`** | **Sapphiron's Left Eye** | Off-Hand | Sapphiron (Naxx) | +8 Intellect, +8 Stamina, +26 Spell Dmg, +1% Spell Crit, +1% Spell Hit. |
| **`19360`** | **Lok'amir il Romathis** | 1H Mace | Nefarian (BWL) | +18 Stamina, +18 Intellect, +84 Healing, +29 Spell Dmg. |
| **`19344`** | **Claw of the Chromaggus** | 1H Dagger | Chromaggus (BWL) | +17 Intellect, +7 Stamina, +64 Spell Dmg, +4 MP5. |
| **`17103`** | **Azuresong Mageblade** | 1H Sword | Golemagg (MC) | +12 Intellect, +7 Stamina, +40 Spell Dmg, +1% Spell Crit. |
| **`19315`** | **Therazane's Touch** | Off-Hand | BWL Trash | +44 Healing, +12 MP5. |
| **`22820`** | **Wand of the Whispering Dead** | Wand | Kel'Thuzad (Naxx) | +16 Spell Damage, +1% Spell Hit. Highest spell DPS wand. |
| **`23009`** | **Doomfinger** | Wand | Naxxramas Trash | +16 Shadow Spell Damage, +1% Spell Crit. |

### 🛡️ Shields & Armor Off-Hands
| Item ID | Item Name | Armor | Block | Boss / Source | Stats & Effects |
| :---: | :--- | :---: | :---: | :--- | :--- |
| **`22818`** | **The Face of Death** | 3210 | 92 | Kel'Thuzad (Naxx) | +14 Strength, +24 Stamina, +7 Defense, +1% Block, +19 Block Value. |
| **`21701`** | **Shield of the Fallen God** | 3098 | 84 | C'Thun (AQ40) | +17 Stamina, +15 Intellect, +44 Healing, +15 Spell Dmg, +6 MP5. |
| **`19349`** | **Elementium Reinforced Bulwark** | 2893 | 78 | Chromaggus (BWL) | +23 Stamina, +7 Defense, +19 Block Value. |
| **`19348`** | **Red Dragonscale Protector** | 2750 | 70 | Vaelastrasz (BWL) | +18 Stamina, +10 Intellect, +37 Healing, +5 MP5. |
| **`17066`** | **Drillborer Disk** | 2539 | 65 | Garr (MC) | +10 Stamina, +15 Defense, Chance on Block: 70–90 Arcane reflection. |
| **`19862`** | **Aegis of the Blood God** | 2400 | 60 | Mandokir (ZG) | +14 Stamina, +7 Defense, +30 Block Value. |
| **`19321`** | **The Immovable Object** | 2121 | 54 | AV PvP Vendor | +15 Stamina, +1% Block, +15 Block Value. |

---

## 13. BiS Endgame Trinkets Directory

### ⚔️ Physical / Melee & Ranged Trinkets
- **`22954`** : **Kiss of the Spider** (Maexxna - Naxx) : +1% Hit, On-Use: +20% Attack Speed for 15 seconds. (BiS Physical Trinket).
- **`19406`** : **Drake Fang Talisman (DFT)** (Ebonroc - BWL) : +56 Attack Power, +2% Hit, +1% Dodge.
- **`21670`** : **Badge of the Swarmguard** (Battleguard Sartura - AQ40) : On-Use: Each physical attack adds +200 Armor Penetration (stacks up to 6 times for 1,200 Armor reduction).
- **`23206`** : **Mark of the Champion (Physical)** (Kel'Thuzad Quest) : +150 Attack Power against Undead and Demons.
- **`20130`** : **Diamond Flask** (Warrior Level 50 Class Quest) : On-Use: +75 Strength and restores 9 Health every 5 seconds for 1 minute. (Snapshotting heal scale).
- **`13965`** : **Blackhand's Breadth** (UBRS Quest) : +2% Critical Strike.
- **`19991`** : **Devilsaur Eye** (Hunter Quest) : +2% Hit, On-Use: +150 Attack Power for 20s.
- **`21180`** : **Earthstrike** (Cenarion Hold Logistics Rep) : On-Use: +280 Attack Power for 20 seconds.
- **`19289`** : **Darkmoon Card: Maelstrom** (Elementals Deck) : Chance on hit: 200–300 Nature damage strike.
- **`19287`** : **Darkmoon Card: Heroism** (Warlords Deck) : Chance on melee hit: Restores 120–180 Health.

### 🔮 Caster & Spell Damage Trinkets
- **`19379`** : **Neltharion's Tear** (Nefarian - BWL) : +44 Spell Damage, +2% Spell Hit. (BiS Caster Trinket).
- **`23046`** : **The Restrained Essence of Sapphiron** (Sapphiron - Naxx) : +40 Spell Damage, On-Use: +130 Spell Damage for 20 seconds.
- **`23207`** : **Mark of the Champion (Caster)** (Kel'Thuzad Quest) : +85 Spell Damage against Undead and Demons.
- **`19950`** : **Zandalarian Hero Charm** (Heart of Hakkar Quest) : On-Use: +204 Spell Damage, diminishing with each spell cast.
- **`12930`** : **Briarwood Reed** (Jed Runewatcher - UBRS) : +29 Spell Damage.
- **`21625`** : **Scarab Brooch** (Viscidus - AQ40) : On-Use: Healing spells apply a shield for 15% of the amount healed for 30s.
- **`19957`** : **Gri'lek's Charm of Valor** (ZG Shaman Trinket) : Reduces mana cost of all shocks by 75%.
- **`19958`** : **Wushoolay's Charm of Nature** (ZG Mage Trinket) : Increases Lightning Bolt cast speed and spell crit.

### ✝️ Healer & MP5 Trinkets
- **`23001`** : **Eye of the Dead** (Sapphiron - Naxx) : On-Use: Reduces mana cost of next 5 healing spells by 150 and increases healing done by 450.
- **`19395`** : **Rejuvenating Gem** (Ebonroc/Firemaw/Flamegor - BWL) : +66 Healing, +9 MP5. (Universal BiS Healer Trinket).
- **`18467`** : **Royal Seal of Eldre'Thalas** (Dire Maul Class Book) : +33 Healing, +4 MP5.
- **`19288`** : **Darkmoon Card: Blue Dragon** (Beasts Deck) : 2% chance on cast to allow 100% mana regen to continue while casting for 15 sec.
- **`19956`** : **Hazza'rah's Charm of Destruction** (ZG Priest Trinket) : Reduces cast time of Greater Heal by 40% for 15s.

### 🛡️ Tank Mitigation & Avoidance Trinkets
- **`19431`** : **Styleen's Impeding Scarab** (Flamegor - BWL) : +13 Defense, +24 Block Value, +5% Chance to Block.
- **`17064`** : **Shard of the Scale** (Onyxia) : +16 MP5 / Regen.
- **`19947`** : **Nat Pagle's Broken Reel** (ZG Fishing) : On-Use: +10% Chance to Hit for 15s.
- **`17983`** : **Force of Will** (BRD Angerforge) : +10 Defense, +1% Dodge, On-hit: 25 Armor absorb.
- **`11815`** : **Hand of Justice** (BRD Emperor) : +20 Attack Power, 2% Chance on melee hit to gain an extra attack.

---

## 14. BiS Rings, Amulets & Off-Set Equipment

### 💍 Rings & Bands
- **`21700`** : **Ring of the Fallen God** (C'Thun Eye Quest) : +6 Strength, +5 Agility, +8 Stamina, +1% Hit, +1% Crit. (Universal BiS physical ring).
- **`17063`** : **Band of Accuria** (Ragnaros - MC) : +16 Agility, +2% Hit, +1% Crit.
- **`18821`** : **Quick Strike Ring** (Magmadar - MC) : +5 Strength, +8 Agility, +1% Crit, +30 Attack Power.
- **`23038`** : **Band of Unnatural Forces** (Loatheb - Naxx) : +14 Stamina, +12 Intellect, +27 Spell Dmg, +1% Spell Crit, +1% Spell Hit.
- **`21702`** : **Ring of the God-Slayer** (C'Thun) : +27 Spell Damage, +6 MP5.
- **`19382`** : **Pure Elementium Band** (Nefarian - BWL) : +10 Stamina, +9 Intellect, +44 Healing.
- **`19434`** : **Band of Dark Dominion** (BWL Trash) : +13 Stamina, +33 Shadow Spell Damage.
- **`19384`** : **Master Dragonslayer's Ring** (Nefarian Head Quest) : +14 Stamina, +48 Attack Power, +1% Hit.
- **`19397`** : **Ring of Blackrock** (Firemaw - BWL) : +19 Stamina, +9 Defense.

### 📿 Amulets & Necklaces
- **`19377`** : **Prestor's Talisman of Conspicuity** (Nefarian Head Quest) : +30 Agility, +1% Hit.
- **`23053`** : **Stormrage's Talisman of Seething** (Kel'Thuzad - Naxx) : +12 Strength, +12 Agility, +12 Stamina, +26 Attack Power, +2% Crit.
- **`23023`** : **Gem of Trapped Innocents** (Kel'Thuzad - Naxx) : +7 Stamina, +9 Intellect, +15 Spell Dmg, +2% Spell Crit.
- **`18814`** : **Choker of the Fire Lord** (Ragnaros - MC) : +7 Stamina, +7 Intellect, +34 Spell Damage.
- **`19426`** : **Onyxia Tooth Pendant** (Onyxia Head Quest) : +12 Agility, +9 Stamina, +1% Hit, +1% Crit, +10 Fire Resist.
- **`18404`** : **Onyxia Blood Talisman** (Onyxia Head Quest - Tank) : +15 Fire Resist, +1% Parry, +1% Defense.
- **`21504`** : **Charm of the Shifting Sands** (AQ20 Head Quest) : +12 Stamina, +25 Spell Damage, +1% Spell Hit.

### 🧣 Cloaks & Capes
- **`23045`** : **Shroud of Dominion** (Sapphiron - Naxx) : +11 Stamina, +50 Attack Power, +1% Crit.
- **`23050`** : **Cloak of the Necro-Knight** (Sapphiron - Naxx) : +8 Stamina, +7 Intellect, +27 Spell Dmg, +1% Spell Crit.
- **`21710`** : **Cloak of the Fallen God** (C'Thun) : +11 Stamina, +37 Spell Damage, +1% Spell Penetration.
- **`21701`** : **Cloak of the Golden Hive** (Princess Huhuran - AQ40) : +10 Stamina, +13 Agility, +1% Hit, +1% Crit.
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
- **`13506`** : **Flask of Petrification** : Turns you to stone, protecting from all physical/spell damage for 1 min (prevents wiping in hardcore).

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
- **`12455`** : **Juju Might** : +40 Attack Power for 10 minutes.
- **`12450`** : **Juju Flurry** : +3% Attack Speed for 20 seconds.
- **`12457`** : **Juju Ember** : +15 Fire Resistance for 10 minutes.
- **`12459`** : **Juju Chill** : +15 Frost Resistance for 10 minutes.
- **`12820`** : **Winterfall Firewater** : +35 Melee Attack Power and increases character scale for 20 min.
- **`18422`** : **Head of Onyxia** : Starts quest to trigger *Rallying Cry of the Dragonslayer* in Stormwind/Orgrimmar.
- **`19003`** : **Head of Nefarian** : Starts quest to trigger *Rallying Cry of the Dragonslayer*.
- **`19802`** : **Heart of Hakkar** : Starts quest to trigger *Spirit of Zandalar* on Yojamba Isle.

---

## 16. Permanent Weapon & Armor Enchantments Directory

### 🗡️ Weapon Enchants (Formulas & Scrolls)
- **`16252`** : **Formula: Enchant Weapon - Crusader** (Chance on hit: +100 Strength & restores 75–125 HP).
- **`18259`** : **Formula: Enchant Weapon - Spell Power** (+30 Spell Damage).
- **`18260`** : **Formula: Enchant Weapon - Healing Power** (+55 Healing).
- **`18251`** : **Formula: Enchant Weapon - Agility (+15)** (+15 Agility to 1H Weapon).
- **`18257`** : **Formula: Enchant 2H Weapon - Agility (+25)** (+25 Agility to 2H Weapon).
- **`16254`** : **Formula: Enchant Weapon - Lifestealing** (Chance on hit: 30 Life drain).
- **`16253`** : **Formula: Enchant Weapon - Unholy Weapon** (Chance on hit: Shadow debuff -15 AP).
- **`16246`** : **Formula: Enchant Chest - Greater Stats** (+4 All Stats).
- **`16243`** : **Formula: Enchant Chest - Major Health** (+100 Health).
- **`16215`** : **Formula: Enchant Gloves - Greater Agility** (+7 Agility).
- **`18251`** : **Formula: Enchant Gloves - Superior Agility** (+15 Agility).

### 📜 Head & Leg Arcanums / ZG Enchants
- **`18329`** : **Arcanum of Focus** (+8 Spell Damage & Healing to Head/Legs).
- **`18330`** : **Arcanum of Rapidity** (+1% Attack Speed to Head/Legs).
- **`18331`** : **Arcanum of Protection** (+1% Dodge to Head/Legs).
- **`11642`** : **Lesser Arcanum of Voracity** (+8 Strength, Agility, Stamina, Intellect, or Spirit).
- **`19782`** : **Presence of Might** (Warrior ZG Enchant: +10 Stamina, +7 Defense, +15 Shield Block Value).
- **`19783`** : **Presence of Sight** (Mage/Warlock ZG Enchant: +18 Spell Damage, +1% Spell Hit).
- **`19784`** : **Syncretist's Sigil** (Paladin ZG Enchant: +10 Stamina, +7 Defense, +24 Healing).
- **`19785`** : **Prophetic Aura** (Priest ZG Enchant: +24 Healing, +4 Stamina, +4 MP5).
- **`19786`** : **Vodouisant's Vigilant Embrace** (Shaman ZG Enchant: +15 Intellect, +13 Spell Dmg/Healing).
- **`19787`** : **Falcon's Call** (Hunter ZG Enchant: +24 Ranged AP, +10 Stamina, +1% Hit).
- **`19788`** : **Death's Embrace** (Rogue ZG Enchant: +28 Attack Power, +1% Dodge).
- **`19789`** : **Animist's Caress** (Druid ZG Enchant: +10 Stamina, +10 Intellect, +24 Healing).

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
- **Loadout**: Thunderfury (`19019`) + The Face of Death (`22818`) or Elementium Reinforced Bulwark (`19349`).
- **Why**: Pushes crushing blows completely off the combat table using *Shield Block* and achieves 440+ Defense to become uncrittable.

---

### 🌿 2. Elemental Resistance Gear Requirements

| Encounter / Raid | Required Resistance | Target Cap | Mandatory Consumables |
| :--- | :---: | :---: | :--- |
| **Emerald Sanctum (Solnius / Erennius)** | **Nature Resistance** | **250+ NR** | Greater Nature Protection Potion (`13458`), Juju Chill (`12459`), Bramblewood / Sandstalker gear. |
| **Scarlet Citadel & Hateforge Quarry** | **Fire Resistance** | **220+ FR** | Greater Fire Protection Potion (`13457`), Juju Ember (`12457`), Elementium Threaded Cloak (`19386`), Onyxia Tooth Pendant (`19426`). |
| **Lower Karazhan Halls & Naxxramas** | **Shadow Resistance** | **200+ SR** | Greater Shadow Protection Potion (`13459`), Band of Dark Dominion (`19434`), Cloak of the Necro-Knight (`23050`). |

---

### 🔮 3. Caster & Healer Scaling in Extended Encounters

1. **Spell Hit Cap (16% Mandatory)**:
   - Bosses in Emerald Sanctum and Scarlet Citadel have high spell resist levels.
   - Fire Mages must equip **Neltharion's Tear** (`19379`) and **Band of Unnatural Forces** (`23038`) alongside 3/3 Elemental Precision to eliminate spell resists on rolling *Ignite* stacks.
   - Warlocks must prioritize **Neltharion's Tear** and **Sapphiron's Left Eye** (`23049`) to ensure *Curse of Shadows* and *Shadow Bolts* never miss.

2. **Healer MP5 vs +Healing Balance**:
   - Turtle-WoW custom raid encounters are longer than standard Vanilla bosses.
   - Pure +Healing gear will run you out of mana. Balance high healing power with **Rejuvenating Gem** (`19395`), **Eye of the Dead** (`23001`), **Mageblood Potion** (`20007`), and **Nightfin Soup** (`13931`).

---

### 🚀 4. Ready-to-Use Raid Preparation Lua Macros

#### 🌿 Macro: 1-Click Nature Resistance Gear Swap (Emerald Sanctum)
```lua
.additem 13458 5 -- 5x Greater Nature Protection Potions
.additem 12459 20 -- 20x Juju Chill (+15 Frost/Nature resist)
.additem 13513 2 -- 2x Flask of Chromatic Resistance (+25 All Resistances)
```

#### 🔥 Macro: 1-Click Fire Resistance Gear Swap (Scarlet Citadel)
```lua
.additem 13457 5 -- 5x Greater Fire Protection Potions
.additem 12457 20 -- 20x Juju Ember (+15 Fire Resistance)
.additem 19386 1 -- Elementium Threaded Cloak (+Fire Resist)
.additem 19426 1 -- Onyxia Tooth Pendant (+10 Fire Resist)
```

#### 📦 Macro: Complete 1-Click Raid Consumable Restock
```lua
.additem 13512 2 -- 2x Flask of Supreme Power
.additem 13510 2 -- 2x Flask of the Titans
.additem 13452 10 -- 10x Elixir of the Mongoose
.additem 13454 10 -- 10x Greater Arcane Elixir
.additem 13444 20 -- 20x Major Mana Potions
.additem 13446 20 -- 20x Major Healing Potions
.additem 5634 10 -- 10x Free Action Potions (FAP)
.additem 20008 10 -- 10x Living Action Potions (LAP)
.additem 3387 5 -- 5x Limited Invulnerability Potions (LIP)
.additem 13928 20 -- 20x Grilled Squid (+10 Agility)
.additem 20452 20 -- 20x Smoked Desert Dumplings (+20 Strength)
.repairitems
```

---

<p align="center">
  [⬅ Prev: 16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 18. Class Talent Specs ➡](./18_class_talent_specs.md)
</p>
