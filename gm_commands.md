# GM Commands & Turtle-WoW Content Teleport Reference

> **Scope:** local Turtle-WoW 1.18.1 (MaNGOS core + PlayerBots module) from this repo.
> **Your account:** `sam` — GM rank **4 (`SEC_ADMINISTRATOR`)**, the highest in-game rank.
> All GM commands below are available to you. Chat commands are typed in-game (in the chat box, prefix with `.` or `!` depending on your client `/console` setting — default is `.`).

---

## 1. Security / rank reference

From `src/shared/Common.h` (`enum AccountTypes`):

| Rank | Name | Notes |
| ------ | ------ | ------- |
| 0 | `SEC_PLAYER` | normal player |
| 1 | `SEC_OBSERVER` | can teleport / go / appear |
| 2 | `SEC_MODERATOR` | can kick/ban/summon |
| 3 | `SEC_DEVELOPER` | most item/spawn/mod commands |
| **4** | **`SEC_ADMINISTRATOR`** | **you** — everything except console-only |
| 5 | `SEC_SIGMACHAD` | internal |
| 6 | `SEC_CONSOLE` | server console only (`quit`, etc.) |

GM commands run from the in-game chat box. If a command needs a higher rank than you have, the server silently ignores it.

---

## 2. Core GM command tree

Extracted directly from `src/game/Chat/Chat.cpp` (`commandTable[]`). Each entry: `command` → minimum rank.

### Movement / teleport

| Command | Rank | What it does |
| --------- | ------ | -------------- |
| `.tele <name>` | Observer (1) | Teleport to a named location (see §4 for the full Turtle-WoW list) |
| `.tele add <name>` | Developer (3) | Save your current position as a teleport target |
| `.tele del <name>` | Developer (3) | Delete a teleport target |
| `.tele name <name>` | Developer (3) | List teleport targets matching a name |
| `.tele group <name>` | Developer (3) | Teleport your whole group |
| `.go xyz <x> <y> <z> <map>` | Observer (1) | Teleport by coordinates |
| `.go creature <id>` / `.go object <id>` | Observer (1) | Teleport to a creature / object GUID |
| `.go target` / `.go taxinode <id>` / `.go trigger <id>` | Observer (1) | Teleport to current target / taxi node / trigger |
| `.go graveyard <id>` | Observer (1) | Teleport to a graveyard |
| `.go corpse` | Developer (3) | Teleport to your corpse |
| `.go forward <dist>` / `.go up <dist>` | Observer (1) | Nudge in facing direction / straight up |
| `.appear <player>` / `.goname <player>` | Observer (1) | Teleport **you** to another player |
| `.summon <player>` | Moderator (2) | Teleport another player to **you** |
| `.groupgo` / `.groupsummon` | Moderator (2) | Teleport whole group to you |
| `.cometome` | Developer (3) | Pull target to you |

### Character / self

| Command | Rank | What it does |
| --------- | ------ | -------------- |
| `.levelup <n>` | Developer (3) | Gain levels |
| `.additem <id/name> [count]` | Developer (3) | Add an item |
| `.additemset <id>` | Developer (3) | Add a full item set |
| `.deleteitem <id> [count]` | Developer (3) | Remove an item |
| `.maxskill` / `.setskill <id> <val>` | Developer (3) | Max / set a profession skill |
| `.learn <spell>` / `.unlearn <spell>` | Developer (3) | Learn / unlearn a spell |
| `.unlearnoffline <player> <spell>` | Developer (3) | Unlearn a spell on an offline player |
| `.start` | Developer (3) | Reset character to starting state |
| `.save` / `.saveall` | Developer (3) / Admin (4) | Save self / all characters |
| `.bank` | Developer (3) | Open your bank anywhere |
| `.mailbox` | Developer (3) | Open a mailbox anywhere |
| `.mount [id]` | Developer (3) | Summon a mount |
| `.rename <player>` | Moderator (2) | Rename a character |
| `.revive` | Developer (3) | Revive self at corpse |
| `.unstuck <player>` | Moderator (2) | Free a stuck player |
| `.die` / `.respawn` | Developer (3) | Kill self / respawn nearby objects |
| `.repairitems` | Developer (3) | Repair all equipped items |
| `.taxicheat` / `.explorecheat` | Developer (3) | Unlock all flight paths / explored areas |
| `.waterwalk` / `.hover` | Developer (3) / Admin (4) | Walk on water / hover |
| `.demorph` | Moderator (2) | Remove shapeshift/morph |
| `.god` / `.aura <spell>` / `.unaura` | Developer (3) | God mode / apply / remove an aura |
| `.cooldown` | Developer (3) | Clear spell cooldowns |
| `.replenish` | Developer (3) | Fill health/mana |
| `.fear` / `.damage <n>` / `.aoedamage` | Developer (3) | Combat helpers |
| `.mute` / `.unmute` / `.pausingmute` | Moderator (2) | Chat mute |
| `.kick <player>` | Moderator (2) | Disconnect a player |
| `.ban` / `.unban` / `.baninfo` / `.banlist` | Moderator (2)+ | Account bans |
| `.freeze` / `.unfreeze` | Moderator (2) | Freeze a player in place |
| `.whispers <on/off>` | Moderator (2) | Toggle GM whisper visibility |
| `.pinfo <player>` | Moderator (2) | Player info dump |
| `.combatstop` | Moderator (2) | End combat for target |

### World / spawning / debug

| Command | Rank | What it does |
| --------- | ------ | -------------- |
| `.gm on/off` / `.gm list` / `.gm ingame` | Observer (1)+ | Toggle GM mode / list GMs online |
| `.gm visible` / `.gm chat` / `.gm options` | Moderator (2)+ | GM visibility / announce chat / options |
| `.npc add <id>` / `.npc delete` / `.npc spawn` … | Developer (3) | Spawn / edit creatures & vendors |
| `.gobject add <id>` / `.gobject delete` / `.gobject activate` … | Developer (3) | Spawn / edit world objects |
| `.cast <spell>` / `.castcustom` | Developer (3) | Cast a spell (self/target) |
| `.debug` … | Developer (3) | Debugging toolkit (los, gps, guid, distance, etc.) |
| `.gps` / `.guid` / `.distance` / `.los` | Observer (1)+ | Position / GUID / distance / line-of-sight info |
| `.respawn` | Developer (3) | Respawn nearby objects |
| `.movegens` | Developer (3) | List movement generators on target |
| `.anticheat` … | Developer (3) | Anti-cheat tools |
| `.sus` … | Developer (3) | Suspicious-activity tools |
| `.trigger active` / `.trigger near` | Observer (1) | List active / nearby triggers |
| `.pet` … | Developer (3) | Pet commands |
| `.unit` … / `.wp` … | Developer (3) / Admin (4) | Unit debug / waypoint editor |

### Server / administrative

| Command | Rank | What it does |
| --------- | ------ | -------------- |
| `.server info` / `.server corpses` | Player (0) / Admin (4) | Server info / corpse count |
| `.server restart [+mm]` / `.server shutdown [+mm]` | Admin (4) | Timed restart / shutdown |
| `.server resetallraids` | Admin (4) | Force-reset all raid instances |
| `.server idlerestart` / `.idleshutdown` | Admin (4) | Restart/shutdown when idle |
| `.instance` … | Admin (4) | Instance / reset management |
| `.reload` … | Admin (4) | Hot-reload config, tables, scripts |
| `.reset` … | Developer (3) | Reset quests / achievements / talents |
| `.quest` … / `.event` … | Developer (3) / Admin (4) | Quest & world-event control |
| `.honor` … | Admin (4) | Honor / PvP rank tools |
| `.lookup` … | Moderator (2) | Look up items / spells / creatures by name |
| `.list` … | Admin (4) | List creatures / items / objects |
| `.account` … | Player (0) | Account self-service |
| `.broadcast` / `.notify` / `.announce` | Admin (4) | Server-wide messages |
| `.ticket` … | Moderator (2) | GM ticket handling |
| `.ban` / `.unban` | Moderator (2)+ | (see above) |
| `.bg` … / `.bgtest` | Admin (4) | Battleground control |
| `.worldstate` … / `.variable` … | Admin (4) | World-state flags |
| `.pdump` … | Admin (4) | Character `.pdump` import/export |
| `.character` … | Developer (3) | Character database ops |
| `.guild` … | Developer (3) | Guild ops |
| `.service` … / `.send` … | Admin (4) / Developer (3) | Services / mail-send |
| `.warEffort` … / `.wareffortget` / `.wareffortset` | Developer (3) / Admin (4) | War Effort event |
| `.cartographer` | Player (0) | Toggle map reveal |
| `.cinematic` | Admin (4) | Play a Turtle-WoW cinematic |
| `.dbghard` | Admin (4) | Fast debug helper |
| `.commands` | Player (0) | List all commands you can use |
| `.help <cmd>` | Observer (1) | In-game help for a command |

> Full handler implementations live in `src/game/Chat/Commands/*.cpp`. For exact sub-flags of any command (e.g. `.npc`, `.reload`, `.lookup`), type `.help <command>` in-game — the server prints the real subcommand list for your rank.

---

## 3. PlayerBots commands (`.bot` / `.rndbot`)

The PlayerBots module adds the `.bot` family (and `.rndbot` for random-bot ops). Source: `src/modules/PlayerBots/playerbot/PlayerbotMgr.cpp`.

> `.rndbot` = same verbs but targets the random-bot population. `.bot` targets a specific named bot.

| Command | Example | What it does |
| --------- | --------- | -------------- |
| `.bot list` | `.bot list` | List active player bots |
| `.bot help <cmd>` | `.bot help add` | Help for a bot subcommand |
| `.bot group` | `.bot group` | Spawn 4 complementary-class bots at your level and group them |
| `.bot create <opts>` | `.bot create level=60 class=mage race=human` | Create a new bot character |
| `.bot add <name>` | `.bot add Dunpriest` | Add a bot to your group (brings it online) |
| `.bot login <name>` | `.bot login Dunpriest` | Alias of `add` |
| `.bot remove <name>` | `.bot remove Dunpriest` | Remove bot from group (stays in world) |
| `.bot logout <name>` / `.bot rm <name>` | `.bot rm Dunpriest` | Log the bot out |
| `.bot delete <name>` | `.bot delete Dunpriest` | Permanently delete the bot character |
| `.bot gear <bot>` / `.bot equip <bot>` | `.bot gear Dunpriest` | Equip best gear from cache |
| `.bot train <bot>` / `.bot learn <bot>` | `.bot train Dunpriest` | Train spells at a trainer |
| `.bot enchants <bot>` | `.bot enchants Dunpriest` | Apply enchants to its gear |
| `.bot food` / `.bot drink` / `.bot potions` / `.bot consumables` / `.bot reagents` | `.bot potions Dunpriest` | Buy consumables (aliases: `pots`, `consums`, `regs`, `reg`) |
| `.bot prepare <bot>` / `.bot prep <bot>` | `.bot prepare Dunpriest` | Full prep: gear + food + pots + reagents |
| `.bot refresh <bot>` | `.bot refresh Dunpriest` | Refresh gear & items |
| `.bot ammo <bot>` | `.bot ammo Dunpriest` | Buy ammo |
| `.bot pet <bot>` | `.bot pet Dunpriest` | Summon/dismiss pet |
| `.bot levelup <bot>` / `.bot level <bot>` | `.bot level Dunpriest` | Level the bot up |
| `.bot random <bot>` | `.bot random Dunpriest` | Randomize appearance/gear |
| `.bot init <bot>` | `.bot init Dunpriest` | Initialize default actions |
| `.bot summon <bot>` / `.bot recall <bot>` / `.bot come <bot>` | `.bot summon Dunpriest` | Summon the bot to you |
| `.bot self <player>` | `.bot self Sam` | Enable self-bot mode for a player |
| `.bot always <player>` | `.bot always Sam` | Keep offline AI active for a player |
| `.bot spoof <bot>` | `.bot spoof Dunpriest` | Route following commands as that bot |
| `.bot debug <bot> <cmd>` | `.bot debug Dunpriest stats` | Run a debug command on the bot (GM only) |
| `.bot c <bot> <cmd>` | `.bot c Dunpriest /say hi` | Run a chat command as the bot |
| `.bot w <bot> <msg>` | `.bot w Dunpriest hello` | Whisper as the bot |
| `.bot p <msg>` / `.bot g <msg>` / `.bot r <msg>` | `.bot p buffs` | Party / guild / raid message as the bot (no arg = info) |
| `.bot rl <bot>` | `.bot rl Dunpriest` | Transfer raid leadership |
| `.bot do <bot> <action>` | `.bot do Dunpriest stats` | Run an action sync |
| `.bot cmd <bot> do <action>` | `.bot cmd Dunpriest do quests` | Run an action async (use with `record`) |
| `.bot record <bot> enable | disable` | `.bot record Dunpriest enable` | Capture async command output |
| `.bot reload` | `.bot reload` | Reload playerbot config (GM only) |
| `.bot tweak` | `.bot tweak` | Cycle tweak value for testing (GM only) |
| `.rndbot init` | `.rndbot init` | Randomize the first available random bot |
| `.rndbot remove <bot>` | `.rndbot remove SomeBot` | Remove a random bot from the server |
| `.rndbot reset` | `.rndbot reset` | Reset all random bots & clear event cache |

> Random-bot population (~1000) auto-spawns at world start (`RandomBotAutologin=1`). Use `.rndbot` verbs to manage that pool; use `.bot` verbs for hand-made or specifically-named bots.

---

## 4. Turtle-WoW content teleport reference (the testing shortcut)

This is the list you asked for — every zone/dungeon/raid in **this server's** `game_tele` (647 entries) and `map_template` tables, so you can `.tele` straight to anything without searching.

> `.tele <token>` is case-sensitive to the token as stored. Tokens below are copied verbatim from `tw_world.game_tele`.

### 4A. Custom dungeons (Turtle-WoW originals — not in vanilla 1.12)

| `.tele` token | Dungeon (map name) | Notes |
| --------------- | -------------------- | ------- |
| `stormwindvault` / `stormwindvaultinstanced` | Stormwind Vault (map 35) | Accessible from Stormwind |
| `karazhancrypt` / `karazhancrypts` / `instancedkarazhancrypt` | Karazhan Crypt (map 800) | Pre-raid wing |
| `karazhaninstance` / `Karazhan` / `alphakarazhan` | Karazhan (crypt + access) | Use `Karazhan` for the main entry |
| `crescentgrove` | Crescent Grove (map 802) | |
| `frostmanehollow` / `frostmane` | Frostmane Hollow (map 822) | |
| `hateforgeinstance` | Hateforge Quarry (map 808) | |
| `gilneascity` / `gilneas` | Gilneas City (map 815) | |
| `dragonmawretreat` | Dragonmaw Retreat (map 816) | |
| `windhorncanyon` | Windhorn Canyon (map 820) | |
| `TimbermawHold` | Timbermaw Hold (map 819, **raid**) | See raids below |

> `Stormwrought Ruins` (map 818) is in `map_template` but has **no `game_tele` token** in this DB — reach it via `.go xyz` or `.lookup` if needed.

### 4B. Custom raids (Turtle-WoW originals)

| `.tele` token | Raid (map name) | Map ID |
| --------------- | ---------------- | -------- |
| `ScarletCitadel` / `scarletfortress` / `scarletenclave` | Scarlet Citadel (map 45) | 45 |
| *(Old Scarlet Citadel — map 44)* | Old Scarlet Citadel (map 44) | 44 — token not present; use `.go` |
| `karazhaninstance` / `Karazhan` | Tower of Karazhan (map 814) | 814 |
| `sanctumraid` / `emeraldsanctum` / `emeraldsanctumproper` / `deeptidesanctum` | Emerald Sanctum (map 807) | 807 |
| `TimbermawHold` | Timbermaw Hold (map 819) | 819 |
| `aq20` | Ruins of Ahn'Qiraj (map 509) | 509 (Turtle tuning) |
| `aq40` | Temple of Ahn'Qiraj (map 531) | 531 |

### 4C. New world zones / open areas (Turtle-WoW expansions)

| `.tele` token | Zone (map name) | Map ID |
| --------------- | ----------------- | -------- |
| `AzsharaCrater` / `Azshara` / `CratereAzshara` | Azshara Crater (map 37) | 37 |
| `EmeraldDream` / `EmeraldForest` / `EmeraldMountains` / `EmeraldPlains` / `EmeraldSanctuary` / `EmeraldDreamVerdantFields` | Emerald Dream (map 169) | 169 |
| `eldrethalas` | Eldrethalas (map 804) | 804 |
| `winterveilvale` | Winter Veil Vale (map 813) | 813 |
| `cowlevel` | Secret Cow Level (map 801) | 801 |
| `gnomeshrink` | Gnomeshrink (map 809) | 809 |
| `Quel'Thalas` region / `silvermoonraid` | Quel'Thalas Cut Scene (49) / Silvermoon City Raid (50) | 49 / 50 |

### 4D. New battleground

| `.tele` token | BG (map name) | Map ID |
| ------------- | ------------- | ------ |
| `Thorn Gorge` region (BG) | Thorn Gorge (map 821, type 3) | 821 — enter via the BG queue/portal |

### 4E. Classic dungeons (vanilla, still present — for quick testing)

| `.tele` token | Dungeon | `.tele` token | Dungeon |
| --------------- | --------- | --------------- | --------- |
| `deadmines` | Deadmines | `RazorfenDowns` / `RazorfenKraul` | Razorfen (Downs/Kraul) |
| `ShadowFangKeep` | Shadowfang Keep | `Uldaman` | Uldaman |
| `WailingCaverns` | Wailing Caverns | `Gnomeregan` | Gnomeregan |
| `BlackfathomDeeps` | Blackfathom Deeps | `Maraudon` / `MaraudonOrange` / `MaraudonPurple` | Maraudon |
| `TheSunkenTemple` / `TempleOfAtal'Hakkar` | Sunken Temple | `Zul'Farrak` / `TheAltarOfZul` | Zul'Farrak |
| `DireMaulEast` / `DireMaulNorth` / `DireMaulWest` | Dire Maul | `Scholomance` | Scholomance |
| `Stratholme` | Stratholme | `BlackrockDepths` / `BlackrockSpire` / `BlackrockMountain` | Blackrock (Depths/Spire) |
| `RagefireChasm` | Ragefire Chasm | `Stormwind Stockade` (map 34 — *no tele token; use `.go`)* | Stockade |

### 4F. Classic raids (vanilla, still present)

| `.tele` token | Raid |
| --------------- | ------ |
| `MoltenCore` | Molten Core (map 409) |
| `BlackwingLair` | Blackwing Lair (map 469) |
| `Onyxia'sLair` / `onyxia` | Onyxia's Lair (map 249) |
| `ZulGurubMadness` / `Zul'Gurub` | Zul'Gurub (map 309) |
| `Naxxramas` | Naxxramas (map 533) |
| `RuinsOfAlterac` / `RuinsOfAndorhal` | Misc ruins (quest/content) |

### Quick `.tele` tips

- **List everything:** `.tele name` with no arg prints all tokens you can use, or query the DB directly:

  ```sql
  SELECT name FROM tw_world.game_tele ORDER BY name;
  ```

- **Find by partial name:** `.tele name karazhan` (in-game) or

  ```sql
  SELECT name FROM tw_world.game_tele WHERE name LIKE '%karazhan%';
  ```

- **Coordinates instead:** `.go xyz <x> <y> <z> <mapId>` — grab `mapId` from the tables above.
- **New content with no tele token** (e.g. Stormwrought Ruins, Old Scarlet Citadel): use `.lookup creature`/`.go` or add your own `.tele add <name>` at the spot.

---

## 5. Source / verification notes

- **GM command tree:** `src/game/Chat/Chat.cpp` (`commandTable[]`, lines ~831–951) + sub-tables (`teleCommandTable`, `goCommandTable`, etc. in the same file).
- **Rank enum:** `src/shared/Common.h` (`AccountTypes`).
- **PlayerBots commands:** `src/modules/PlayerBots/playerbot/PlayerbotMgr.cpp` (`m_holderHandlers` / `m_botCommandHandlers` + help strings ~line 3069).
- **Teleport tokens & map names:** live DB `tw_world.game_tele` (647 rows) and `tw_world.map_template` (instance types: 1=dungeon, 2=raid, 3=battleground, 0=world).
- This doc was generated from the actual repo + local DB on 2026-08-17. If the server's `game_tele` table is later updated, re-run the SQL above to refresh §4.
