# 09. Moderation, Tickets & Anticheat

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

### 📚 Complete 16-Module Reference Library

| Security & Server | World & Characters | Gameplay & Items | Bots & Modules |
| :--- | :--- | :--- | :--- |
| [01. Security & Accounts](./01_security_and_account.md) | [03. Teleports & Movement](./03_teleports_and_movement.md) | [05. Combat & Spells](./05_combat_spells_and_skills.md) | [11. Playerbot Suite](./11_playerbot_suite.md) |
| [02. Server & Reloads](./02_server_and_reloads.md) | [04. Stats & Visuals](./04_character_stats_and_visuals.md) | [06. Quests & Instances](./06_quests_instances_and_events.md) | [12. RandomBot Manager](./12_randombot_manager.md) |
| [08. Spawning & Editing](./08_spawning_and_world_editing.md) | [07. Items & Economy](./07_items_economy_and_mail.md) | [10. Turtle Custom](./10_turtle_custom_features.md) | [13. Bot Whispers & Macros](./13_bot_whispers_and_macros.md) |
| **[09. Moderation & Anticheat](./09_moderation_tickets_and_anticheat.md)** | [16. Diagnostics & Lookups](./16_developer_and_diagnostics.md) | [15. Dungeon Clear AI](./15_dungeon_clear_module.md) | [14. Bot Strategies & Tactics](./14_bot_strategies_and_tactics.md) |

---

## 1. Categorized In-Game Lua Macros (Top Priority)

Copy and paste these macros directly into your World of Warcraft **Macro UI** (`/m`) or run them directly in chat:

### Category A: Essential GM Moderation & Ticket Support (Top Priority)

#### 🎫 Macro 1: Open GM Support Ticket Queue
Displays all active support tickets submitted by players, highlighting online submitters:
```lua
.ticket list
.ticket onlinelist
```

#### 🔍 Macro 2: Target Player Audit (Account, IP & Security Rank)
Instantly inspects the selected player's username, GUID, IP address, and mute/ban status:
```lua
.pinfo
.guid
```

#### 🚫 Macro 3: 60-Minute Channel Mute
Mutes the targeted player across all public channels, say, yell, and whispers for 60 minutes:
```lua
.mute Spammer 60 Inappropriate Language
```

#### 🧊 Macro 4: Freeze & Unfreeze Target Character
Locks the selected character in place to prevent movement or exploitation during investigation:
```lua
.freeze
.unfreeze
```

#### 🚪 Macro 5: Kick Player from Server
Forcibly disconnects the targeted player from the realm with a reason:
```lua
.kick Spammer AFK Exploitation
```

---

### Category B: Account & IP Disciplinary Bans (Middle Priority)

#### 🔨 Macro 6: 7-Day Account Ban
Applies a 7-day suspension to the specified account:
```lua
.ban account BadGuy 7d Exploiting Game Mechanics
```

#### 🌐 Macro 7: 30-Day IP Address Ban
Bans a specific IP address from connecting to any realm on the logon server:
```lua
.ban ip 192.168.1.100 30d Severe Botting
```

#### 🔓 Macro 8: Unban Account & Restore Access
Removes an active suspension from an account:
```lua
.unban account BadGuy
```

---

### Category C: Character Recovery & Anticheat Telemetry (Bottom Section)

#### ♻️ Macro 9: Deleted Character Recovery Tool
Lists soft-deleted characters for an account and restores character by GUID:
```lua
.character deleted list name Sam
.character deleted restore 123 Sam 1
```

#### 🛡️ Macro 10: Anticheat Telemetry Statistics
Dumps live Warden and movement detection counters:
```lua
.anticheat info
```

---

## 2. Moderation & Disciplinary Commands Reference

Verified against CMaNGOS command handlers (`src/game/Chat/Chat.cpp` & `Commands.cpp`):

### 🛡️ Player Moderation (`.kick` / `.mute` / `.freeze`)
- `.kick <player> [reason]`
  - **Security**: Moderator (2)
  - **What it does**: Forcibly disconnects player from the game world.
  - **Example**: `.kick Spammer AFK Botting`

- `.mute <player> <minutes> [reason]`
  - **Security**: Moderator (2)
  - **What it does**: Mutes player across all chat channels for specified minutes.
  - **Example**: `.mute Spammer 60 Spamming`

- `.unmute <player>`
  - **Security**: Moderator (2)
  - **What it does**: Removes active mute penalty immediately.
  - **Example**: `.unmute Spammer`

- `.freeze`
  - **Security**: Moderator (2)
  - **What it does**: Freezes selected player in place, preventing all movement.
  - **Example**: `.freeze`

- `.unfreeze`
  - **Security**: Moderator (2)
  - **What it does**: Unfreezes targeted player.
  - **Example**: `.unfreeze`

---

### 🎫 Support Ticket Queue (`.ticket`)
- `.ticket list`
  - **Security**: Moderator (2)
  - **What it does**: Lists all active open player support tickets.
  - **Example**: `.ticket list`

- `.ticket onlinelist`
  - **Security**: Moderator (2)
  - **What it does**: Lists tickets submitted by players currently logged into the realm.
  - **Example**: `.ticket onlinelist`

- `.ticket <id>`
  - **Security**: Moderator (2)
  - **What it does**: Displays the full submission text, coordinates, and timestamp for ticket ID.
  - **Example**: `.ticket 12`

- `.ticket close <id>`
  - **Security**: Moderator (2)
  - **What it does**: Resolves and closes ticket ID.
  - **Example**: `.ticket close 12`

---

### 🔨 Disciplinary Bans (`.ban` / `.unban`)
- `.ban account <username> <duration> [reason]`
  - **Security**: Moderator (2)
  - **What it does**: Bans account for duration (e.g. `1d`, `7d`, `permanent`).
  - **Example**: `.ban account BadGuy 7d Exploiting`

- `.ban character <char_name> <duration> [reason]`
  - **Security**: Moderator (2)
  - **What it does**: Bans single character.
  - **Example**: `.ban character BadAlt 1d Harassment`

- `.ban ip <ip> <duration> [reason]`
  - **Security**: Moderator (2)
  - **What it does**: Bans IP address from connecting.
  - **Example**: `.ban ip 192.168.1.100 30d`

- `.unban account <username>`
  - **Security**: Developer (3)
  - **What it does**: Unbans specified account.
  - **Example**: `.unban account BadGuy`

- `.unban ip <ip>`
  - **Security**: Developer (3)
  - **What it does**: Unbans specified IP address.
  - **Example**: `.unban ip 192.168.1.100`

---

<p align="center">
  [⬅ Prev: 08. Spawning & World Editing](./08_spawning_and_world_editing.md) • [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 10. Turtle Custom Features ➡](./10_turtle_custom_features.md)
</p>
