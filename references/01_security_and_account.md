# 01. Security Ranks & Accounts

> **Quick Navigation**: [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [🎯 Bot Macro Book (13_bot_whispers_and_macros.md)](./13_bot_whispers_and_macros.md) • [⚔️ Class Strategies (14_bot_strategies_and_tactics.md)](./14_bot_strategies_and_tactics.md) • [🗺️ Teleport Directory (03_teleports_and_movement.md)](./03_teleports_and_movement.md)

---

## 📚 Complete 19-Module Reference Library

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

### Category A: Essential Account Permissions & Character Auditing (Top Priority)

#### 👑 Macro 1: Grant Full Administrator Permissions (Level 4 Administrator)

Sets the target account's security rank to Administrator (Level 4), unlocking all commands, spawns, reloads, and bot controls:

```lua
.account set gmlevel Sam 4
```

#### 🔍 Macro 2: Target Player & Account Security Audit

Instantly inspects the selected player's account username, GUID, security rank, IP address, and mute status:

```lua
.pinfo
.guid
```

#### 👥 Macro 3: List All Characters on Account

Lists all character names, classes, races, levels, and GUIDs belonging to the specified account:

```lua
.account characters Sam
```

#### 🔑 Macro 4: Create Game Account & Set Password

Creates a new player account with credentials:

```lua
.account create NewPlayer Secret123
```

---

### Category B: GM Support & Stealth Patrol (Middle Priority)

#### 👁️ Macro 5: Visible GM Support Mode

Enables GM tag, makes character visible, opens whispers for player tickets, and normalizes speed:

```lua
.gm on
.gm visible on
.whispers on
.modify speed 1.0
```

#### 🕵️ Macro 6: Stealth Patrol Mode (Invisible + Flight + 250% Speed)

Enables GM invisibility, silences incoming whispers, and turns on high-speed flying:

```lua
.gm on
.gm visible off
.whispers off
.hover on
.modify speed 2.5
```

#### 📋 Macro 7: Active GM Roster & Online Staff List

Displays all registered GM accounts and lists staff currently logged into the realm:

```lua
.gm ingame
.gm list
```

---

### Category C: Session Duty Teardown & Reset (Bottom Section)

#### 🚪 Macro 8: Duty Teardown & Normal Mode Reset

Disables flying, restores normal visibility, turns off GM mode, and resets movement speed back to standard 1.0:

```lua
.hover off
.gm visible on
.gm off
.modify speed 1.0
```

---

## 2. Complete Account Commands Directory (`.account`)

Verified against CMaNGOS core command handlers (`src/game/Chat/Chat.cpp`):

### ⚡ Essential Account Administration (Top Priority)

- `.account set gmlevel <username> <level>`
  - **Handler**: `HandleAccountSetGmLevelCommand` (Line 275)
  - **Security**: Administrator (4)
  - **What it does**: Sets the account security permission level (`0` = Player, `1` = Observer, `2` = Moderator, `3` = Developer, `4` = Administrator, `5` = SigmaChad, `6` = Console).
  - **Example**: `.account set gmlevel Sam 4`

- `.account create <username> <password>`
  - **Handler**: `HandleAccountCreateCommand` (Line 6301)
  - **Security**: Administrator (4)
  - **What it does**: Creates a brand new player account in `tw_logon.account`.
  - **Example**: `.account create PlayerOne Secret123`

- `.account set password <username> <new_password> <new_password>`
  - **Handler**: `HandleAccountSetPasswordCommand` (Line 327)
  - **Security**: Developer (3)
  - **What it does**: Forcibly resets the password for any account without needing the old password.
  - **Example**: `.account set password Sam NewPass123 NewPass123`

- `.account characters [username]`
  - **Handler**: `HandleAccountCharactersCommand` (Line 5406)
  - **Security**: Developer (3)
  - **What it does**: Lists all character names, GUIDs, levels, classes, and races under an account.
  - **Example**: `.account characters Sam`

- `.account password <old_password> <new_password> <new_password>`
  - **Handler**: `HandleAccountPasswordCommand` (Line 6482)
  - **Security**: Player (0)
  - **What it does**: Changes your own logged-in account password.
  - **Example**: `.account password OldPass123 NewPass456 NewPass456`

---

### 🔍 Account Inspection & Telemetry (Middle Priority)

- `.account`
  - **Handler**: `HandleAccountCommand` (Line 6283)
  - **Security**: Player (0)
  - **What it does**: Displays your account name, account ID, and current security level.
  - **Example**: `.account`

- `.account email <old_email> <new_email> <new_email>`
  - **Handler**: `HandleAccountEmailCommand` (Line 18895)
  - **Security**: Player (0)
  - **What it does**: Updates the registered email address for the active account.
  - **Example**: `.account email old@mail.com new@mail.com new@mail.com`

- `.account twofa <username> <base32_key | off>`
  - **Handler**: `HandleAccountFaCommand` (Line 388)
  - **Security**: Administrator (4)
  - **What it does**: Configures or disables Two-Factor Authentication (TOTP) for an account.
  - **Example**: `.account twofa Sam JBSWY3DPEHPK3PXP`

- `.account getname <character_guid>`
  - **Handler**: `HandleAccountGetNameCommand` (Line 19324)
  - **Security**: Observer (1)
  - **What it does**: Resolves the account username that owns the specified character GUID.
  - **Example**: `.account getname 10045`

---

### 🗑️ Low-Level Maintenance (Bottom Section)

- `.account delete <username>`
  - **Handler**: `HandleAccountDeleteCommand` (Line 6363)
  - **Security**: Console (6)
  - **What it does**: Permanently deletes an account and unlinks all associated characters.
  - **Example**: `.account delete SpammerAccount`

---

## 3. GM State & Session Commands (`.gm` / `.whispers`)

Verified against `src/game/Chat/Chat.cpp:214`:

- `.gm [on | off]`
  - **Handler**: `HandleGMCommand`
  - **Security**: Moderator (2)
  - **What it does**: Toggles GM status flag on your character.
  - **Example**: `.gm on`

- `.gm visible [on | off]`
  - **Handler**: `HandleGMVisibleCommand`
  - **Security**: Moderator (2)
  - **What it does**: Toggles GM invisibility. When off, players and mobs cannot see or detect you.
  - **Example**: `.gm visible off`

- `.whispers [on | off]`
  - **Handler**: `HandleWhispersCommand`
  - **Security**: Moderator (2)
  - **What it does**: Toggles whether regular players can send you private whispers.
  - **Example**: `.whispers off`

- `.gm ingame`
  - **Handler**: `HandleGMOnlineListCommand`
  - **Security**: Moderator (2)
  - **What it does**: Lists all currently online Game Masters.
  - **Example**: `.gm ingame`

- `.gm list`
  - **Handler**: `HandleGMListFullCommand`
  - **Security**: Developer (3)
  - **What it does**: Lists all GM accounts registered in the database.
  - **Example**: `.gm list`

- `.gm chat [on | off]`
  - **Handler**: `HandleSetGMChatCommand`
  - **Security**: Administrator (4)
  - **What it does**: Toggles the official GM badge icon next to your name in public chat channels.
  - **Example**: `.gm chat on`

- `.gm socials [on | off]`
  - **Handler**: `HandleGMSocialsCommand`
  - **Security**: Moderator (2)
  - **What it does**: Toggles GM social privacy restrictions (who lists and friend requests).
  - **Example**: `.gm socials on`

- `.gm options`
  - **Handler**: `HandleGMOptionsCommand`
  - **Security**: Administrator (4)
  - **What it does**: Displays active GM session options.
  - **Example**: `.gm options`

- `.gm setview <view_id>`
  - **Handler**: `HandleSetViewCommand`
  - **Security**: Developer (3)
  - **What it does**: Switches client camera view ID.
  - **Example**: `.gm setview 1`

---

## 4. Security Levels Reference (Levels 0 through 6)

| Level | C++ Enum Constant | Target Role | Capabilities & Scope |
| :---: | :--- | :--- | :--- |
| **0** | `SEC_PLAYER` | Normal Player | Player gameplay, `.account password`, `.account email`, `.save`, `.unstuck`, and party bot triggers (`/p`). |
| **1** | `SEC_OBSERVER` | Observer / QA | Read-only inspection (`.gps`, `.pinfo`, `.distance`, `.guid`, `.lookup`). |
| **2** | `SEC_MODERATOR` | Moderator | Player moderation (`.kick`, `.freeze`, `.mute`, `.ban`), GM tickets (`.ticket`), player summoning (`.summon`, `.appear`). |
| **3** | `SEC_DEVELOPER` | Game Master / Dev | Level boost (`.levelup`), item spawning (`.additem`), spell learning (`.learn`), bot management (`.bot`, `.rndbot`). |
| **4** | `SEC_ADMINISTRATOR` | Server Admin | Full server administration (`.account set gmlevel`), table reloads (`.reload`), restart countdowns (`.server restart`). |
| **5** | `SEC_SIGMACHAD` | Super Admin | Elevated administrative overrides and engine debugging. |
| **6** | `SEC_CONSOLE` | Server Console | Server terminal only (account deletion, permanent character purge, emergency shutdown). |

---

<p align="center">
  [🏠 Master Portal (gm_commands.md)](../gm_commands.md) • [📖 Project Readme](../README.md) • [Next: 02. Server Operations & Reloads ➡](./02_server_and_reloads.md)
</p>
