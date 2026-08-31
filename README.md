
# Tortoise-WoW

This is an unofficial, community driven, restoration of the 1.18.1 patch of Turtle-WoW, with some additions for solo play.
This project is not to be used for profit or to misrepresent itself, or anyone using it, as the original creators
This project targets version 1.18.1 build 7272

## About this fork

A fork of **[Penqle/tortoise-wow](https://github.com/Penqle/tortoise-wow)** running a
small private server with **~1000 playerbots** permanently online. Upstream is merged in
periodically; everything below is what this fork adds on top.

**Playerbots are the foundation of this fork, not a side feature.** Upstream still lists
them as planned; here they are what the server is built around, and running a thousand of
them permanently is what shapes everything else. Load like that reaches code paths a
few dozen players never touch — stale cached pointers, an unsynchronised battleground
queue, navmesh tiles unloaded under a running query. Most of the fixes below started as
something that went wrong in game and was traced back to its cause, which is why the
commit messages read like bug reports rather than feature notes.

### Playerbots

Integrated from [r-o-sh's branch](https://github.com/r-o-sh/tortoise-wow/tree/playerbots-integration-gh),
which vendors [ike3's playerbots][20] under `src/modules/PlayerBots/`. Build with
`-DBUILD_PLAYERBOTS=ON`; activation is gated by `AiPlayerbot.Enabled`.

Fixes made while running them:

| Area | What was wrong |
| --- | --- |
| Battlegrounds | Bots never queued, never entered, and dropped the flag on a PvP trinket. Three separate bugs, including a call to `HandleBattlefieldPortOpcode` where `HandleBattleFieldPortOpcode` was meant — different function, same name but for one letter's case |
| Dungeon finder | Filled a waiting group with bots and held them to the role they were given; shamans no longer land on the tank slot, and a bot whose tree does not fit its role gets respecced |
| Druids | Never learned bear form, so a tank druid stayed in caster shape. Now learned at 10/16/40, with a backfill for existing bots |
| Healers | Heal range was 125 yards — three times what any heal can reach — so healers walked away instead of healing. The second healer in a group picked a target already at full health and did nothing at all |
| Targeting | Stealth breaks a bot's current target again; hunters no longer try to tame shapeshifted druids |
| Summoning | Works without a meeting stone, reports why it failed, and no longer drops the bot under the world |
| Group loot | Bots vote instead of letting every countdown expire |
| Talent specs | Premade specs generated for the talent rate the config actually ships — the stock vanilla links are all rejected by Turtle's reworked trees |
| Target values | Cached a raw `Unit*` for up to a second. If the creature died inside that window the next read followed a freed pointer — crash in `AttackAction::IsTargetValid`. The guid is carried alongside now and cached reads resolve through the object accessor |
| Battleground queue | `BattleGroundQueue` declares a `recursive_mutex`, but all five acquisitions were left commented out during the ACE migration. A thousand bots queueing from parallel map threads tore the `std::map` apart. Restored |
| Anticheat on bot sessions | `m_antiCheat` is only assigned during a network login, so bot sessions carried a null pointer for life — and seven call sites in `MovementHandler` dereference it unchecked, one of which the bot module calls directly. Every session now starts with the `NullSessionAnticheat` the core already ships |
| Dungeon fill | A role that cannot be filled is counted as covered, but the queue count does not follow — so with no tank available the group stopped at four and could never form, the matcher wanting exactly one tank, one healer and three damage. The player waited without being told anything. The level window is asymmetric now (a bot above the waiting player still works, one below misses and dies), a tank can be taken out of a bot-only run, and an unfilled role is logged |
| Spec selection | Warriors come out 125 fury against 35 protection where the configured weights say 50:50 — and on a bot realm the protection warriors are the tank supply. Fixed on the way: an off-by-one that gave the first path an extra slot, a talent tree called from nowhere that read a config field nothing fills, and a role switch whose result was computed and discarded. The remaining skew is logged rather than guessed at |
| Strategy rebuilds | `Engine::Init()` discards and rebuilds every strategy's triggers, and it ran once per strategy in a list rather than once per change — 105 million trigger initialisations an hour, near 29,000 a second, inside 4.4 billion allocations. One flag was passed the wrong way round: `initMode` means "hold back", the parameter it was handed means "do it now" |
| Custom strategies | `+custom::learned` is in the default strategy list, so every bot asked the database twice on every rebuild for action lines that ten characters out of a thousand actually have. The cache meant to prevent that is written by no code path in the tree. Results are remembered now, the empty ones included |
| Stability | The bot logger passed finished text to `vfprintf` as a format string; any bot name containing `%` aborted the server on MSVC |

### Automated dungeon clearing

`modules/mod-dungeon-clear/` drives a bot party through an instance on its own:
navigate, pull, fight, loot, move on. It exists because a thousand bots that
never enter a dungeon only exercise half the server, and because clearing one
end to end is the hardest thing to ask of bot navigation.

**Rosters are data.** Which creatures count as bosses, and in what order, comes
from `data/dc_roster.txt` and is applied with `.reload config` — no rebuild:

```
credit <entry> [<entry> ...]     add creature entries to the credit list
order  <mapId> <entry> <index>   place an entry in that map's order
drop   <entry> [<entry> ...]     take entries out again
```

`drop` exists for rares. A rare in the credit list becomes a required kill, and
the party then waits for something that is usually not in the instance at all.

**Routes are recorded, not written.** When a boss dies, the path the party
actually walked is stored as an anchor route under `src/Routes/`, and every
later run follows it instead of recomputing. A route the stuck-recovery ladder
runs out on is discarded and relearned by whoever gets through next. A route
that must not be touched again — a ledge with deep water either side, say —
takes the word `pinned` in its file header and is then exempt from both
replacement and discard.

Legs between anchors are pathed rather than walked in a straight line. That
sounds like a detail and is not: anchors sit 15–20 yards apart, and a chord
across a curve leaves any strip narrower than the error.

**Set-pieces are declarative.** Where a dungeon needs more than navigation —
lighting the four Fires of Aku'mai in order, each followed by a wave — the
sequence is a list of steps (`MoveTo`, `UseGameObject`, `Gossip`, `WaitForSpawn`,
`WaitForGameObjectState`, `KillCreature`, `ClearRadius`) in
`Data/Events/`.

**Runs are reproducible.** `.dc test start <dungeon> [seed=N]` starts a
monitored run; the party composition is drawn from that seed and written into
the log, so a comp that trips a bug can be replayed exactly. The panel draws
where every bot stands, one point per bot per second.

### Server features

All off by default, all in `mangosd.conf`:

| Feature | Config keys | Also required |
| --- | --- | --- |
| Zone-restricted world buffs on a timer | `AutoWorldBuff.*` | – |
| Hourly donation points | `AutoDonationPoints.*` | `sql/logon/donation_point_progress.sql` on the **login** database |
| Beginners guild for new characters | `BeginnersGuilds`, `BeginnersGuildHorde/Alliance` | the guilds must exist; the shipped ids are placeholders |
| Guild bank in every capital | `GuildBank.NpcEntriesAlliance/Horde` | nothing — the gossip trigger ships as a migration |
| Dungeon finder fills with bots | `LFT.BotFill.Enable`, `.DelaySeconds`, `.LevelRangeBelow/Above`, `.SeedRuns`, `.SeedDungeons`, `.SeedTeleport` | – |
| Solo dungeon resurrection, leech limits | `SoloDungeonRepopAlive.Enable`, `Leech.*` | – |
| Keep navmesh tiles loaded | `MMapTileUnload` | off by default; `removeTile` zeroes `tile->polys` and Detour reads it unvalidated, so a surviving polyRef resolves to `nullptr + index` |

Playerbot keys live in `src/modules/PlayerBots/playerbot/aiplayerbot.conf.dist.in`, the
rest in `src/mangosd/mangosd.conf.dist.in`. A config generated from an older checkout
will not contain them — regenerate it or copy the blocks across.

### Class, spell and item fixes

| | |
| --- | --- |
| Flurry | Never spent its charges above rank 1 |
| Shield Specialization | Granted one rage on every rank, because all five ranks trigger the same fixed-amount spell |
| Sweeping Strikes | Moved fully to a spell script, multiproc fixed |
| Embrace of the Viper | Both set bonuses were dead. The five-piece heal had neither condition nor cooldown; the six-piece did nothing at all and now applies a poison |
| Wild Regeneration | Checked health before the hit landed instead of after, so it refused exactly the hit it was meant to catch |
| Alterac items | Four effects that existed only as developer notes, now implemented |
| Disenchanting | Restored the disenchant ids this database had lost, plus 3450 items that never had one |
| Mage talents | A wide pass over 21 talents and spells — Ignite, Combustion, Amplify/Dampen Magic, Improved Blizzard, Arcane Meditation, Master of Elements, Magic Absorption, Arctic Reach, Hot Streak, Icicles and more. Taken from [faemwow/tortoise-wow](https://github.com/faemwow/tortoise-wow) |
| Mana gain modifiers | `SPELL_AURA_MOD_MANA_GAIN_PERCENT` was never applied when a spell restored mana, so the modifier did nothing for any class. Now applied to both the amount and the threat it generates |
| Damage on creatures | `Unit::DealDamage` branched on `!IsPlayer() && addThreat`, so a creature taking damage that carries no threat fell into the player-only half and was cast to `Player*` — durability loss on a creature, and an uncaught exception |
| Shatter | Read its crit bonus from five hardcoded per-rank values instead of the spell modifier |
| Healing Touch | `OnFinish` followed `mod->ownerAura`, a raw pointer captured when the modifier was applied. An aura expiring mid-cast left it dangling; `SpellModifier::spellId` carries the same id and is used instead |
| Guild bank | Money column was signed and parsing unchecked — deposits could overflow into a negative balance |

### Content and data

Ship as migrations, so a fresh setup gets them automatically:

- Graveyard coverage for The Barrens, Arathi, and the dungeon sub-zones Turtle splits up.
  Without it, releasing near the Crossroads guards puts the ghost on its own corpse, where
  it dies again immediately
- Eighteen trainers nobody could talk to, Survival's missing artisan rank, guard directions
  to the Survival trainer, and a trainer for Alah'Thalas
- The Syndicate quartermaster, which stocked one item out of thirteen
- Hellador Swiftluck, who pointed at equipment that does not exist
- The guild bank gossip trigger, and the PvP trinket no longer dropping the flag

Two are deliberately manual, in `sql/tools/`, because both depend on per-server data:

- `graveyards_turtle_dungeons.sql` — the five Turtle-built dungeons with no graveyard on
  their map. Run `tools/dbc/add_worldsafelocs.py` first; it references WorldSafeLocs ids a
  stock DBC does not have, which stops at 174
- `playerbot_bypass_crossroads.sql` — routes bots around a guard 21 yards from a travel
  node. Rewrites travel graph links by id, so check your own node ids first

### Build and documentation

- Release builds on MSVC ship debug symbols, so a crash dump is readable
- Eluna is integrated as a pinned Git submodule, built by default, and controlled at
  runtime by `Eluna.Enabled`. See `docs/ELUNA.md` for checkout, configuration,
  architecture, compatibility, and update guidance
- `INSTALL-LINUX.md` and `INSTALL-WINDOWS.md` are start-to-finish walkthroughs, including
  the OpenSSL 3 legacy provider, the database procedure that actually works, and reading a
  crash dump
- The **world database is in this repository** — `sql/base` holds 190 files, 131 MB, plus
  the migrations under `sql/database_updates`. Only client data (maps, DBC, vmaps, mmaps)
  has to be extracted from a game client, with the tools under `tools/`

Several of the fixes below are also kept as standalone patches, each one
self-contained, so they can be lifted onto any compatible tree without taking
the rest of this fork with them. Ask if you want one.

Work from other forks is pulled in where it fits and credited in the commit —
the mage pass comes from [faemwow/tortoise-wow](https://github.com/faemwow/tortoise-wow),
whose repository is also worth a look if you would rather run this in Docker or
build it with Nix.

> **Note on the client:** the core must be built with `-DALLOW_TURTLE_ADDONS=ON`, otherwise
> the client crashes with "interface corrupt" on entering the world.

Everything below is upstream's own documentation and applies to this fork as well.

## Client Version

> [!CAUTION]
> The client version targeted is the unmodified 1.18.1.7272 with 2026-04-12 hotfixes client, the final client version of Turtle-WoW.
> Any client that does not match the above specifications will likely have a myriad of issues.
> Several of the Turtle-WoW successor servers do **not** offer the correct client version for this project.
> Use the [`dbc_verifier.py`](tools/dbc_verification/dbc_verifier.py) script to verify your extracted DBC files are the correct versions.
>
> You only need to use the `mapextractor` tool to extract all DBC files quickly, not the full vmap extract and build.
> A full SHA-256 manifest can be found in the [`DBC verification`](tools/dbc_verification/) folder.
> This manifest was retrieved from <https://launcher.turtlecraft.gg/api/manifest/EU> on 2026-07-14.

## Additions

Additions will be added as the core code reaches feature completion

#### Current Additions

- **Autoscale** - Rudimentary toggleable dungeon/raid auto scaling system, found in mangosd.conf
- **Leech** - Basic toggleable leech system designed for solo play, found in mangosd.conf
- **Additional Talent Points** - Mostly used for testing, found in tw_char.characters
- **[Playerbots][20]** *(this fork)* - Integrated from [r-o-sh's branch](https://github.com/r-o-sh/tortoise-wow/tree/playerbots-integration-gh). Not an experiment: ~1000 of them run permanently and the fork is built around them. Upstream still lists this as planned.
- **[Eluna][19]** *(this branch)* - Lua scripting through a pinned submodule. The custom Turtle WoW MaNGOS core uses Eluna's VMaNGOS compatibility backend without becoming a VMaNGOS core. Enable it at build time with `BUILD_ELUNA` and at runtime with `Eluna.Enabled`; see `docs/ELUNA.md`.

## Operating Systems

- **[Windows][15]**, 32 bit and 64 bit. Windows Server 2008 (or newer) or Windows 8 (or newer) is recommended.
- **Linux**, 32 bit and 64 bit. [Ubuntu 22.04 LTS][14] is recommended. Other distributions with similar package versions will work, too.
Of course, newer versions should work, too. In the case of Windows, matching
server versions will work, too.

## Dependencies

- **[Git][1] / [Github for Windows][2]**: This version control software allows you to get the source files in the first place.
- **[MySQL][3]** / **[MariaDB][4]**: These databases are used to store content and user data.
- **[ACE][5]**: aka Adaptive Communication Environment, provides us with a solid cross-platform framework for abstracting operating system specific details.
- **[Recast][21]**: In order to create navigation data from the client's map files, Recast is used to do the dirty work. It provides functions for rendering, pathing, etc.
- **[G3D][6]**: This engine provides the basic framework for handling 3D data and is used to handle basic map data.
- **[Stormlib][7]**: Provides an abstraction layer for reading from the client's data files.
- **[Zlib][8]/[Zlib for Windows][9]** provides compression algorithms used in both MPQ archive handling and the client/server protocol.
- **[Bzip2][10]/[Bzip2 for Windows][11]** provides compression algorithms used in MPQ archives.
- **[OpenSSL][12]/[OpenSSL for Windows][13]** provides encryption algorithms used when authenticating clients.

To build this project follow any MaNGOS/MaNGOS Zero build guide, with the addition of ACE

## Database Setup

1. Manually import sql/create_databases.sql
2. Manually import all sql scripts in the sql/base folder
3. Run mangosd to automatically import and track updates

This will be streamlined once the core is more up to date

> **Caveat for this fork:** step 3 relies on the DB auto-updater
> (`Database.AutoUpdate.Enabled` in mangosd.conf). That works on a database
> built up through the auto-updater from the start. On a database that was
> instead restored from a full dump, the `migrations` table won't line up with
> the files in `sql/database_updates/`, and enabling the auto-updater makes it
> try to replay old migrations until one fails on a duplicate key — the server
> then refuses to start. If that applies to you, keep it disabled and apply new
> migration files by hand, recording each one afterwards:
>
> ```sql
> INSERT INTO migrations (Name, Hash, AppliedAt)
> VALUES ('20260726112016_world', 'manual', NOW());
> ```

## Contributing

**For this fork:** improvements to the core itself are best directed at
[upstream](https://github.com/Penqle/tortoise-wow) rather than here — this fork
exists to run a private server and only tracks upstream plus the additions
listed at the top.

Upstream's note follows:

> Contributions are welcome, but I may be slow to review and merge PRs
>
> See `CONTRIBUTING.md` for ways to get started.

[1]: http://git-scm.com/ "Git - Distributed version control system"
[2]: http://windows.github.com/ "github - windows client"
[3]: https://dev.mysql.com/downloads/ "MySQL - The world's most popular open source database"
[4]: https://mariadb.org/download/ "MariaDB - An enhanced, drop-in replacement for MySQL"
[5]: http://www.dre.vanderbilt.edu/~schmidt/ACE.html "ACE - The ADAPTIVE Communication Environment"
[6]: http://sourceforge.net/projects/g3d/ "G3D - G3D Innovation Engine"
[7]: http://zezula.net/en/mpq/stormlib.html "Stormlib - A library for reading data from MPQ archives"
[8]: http://www.zlib.net/ "Zlib"
[9]: http://gnuwin32.sourceforge.net/packages/zlib.htm "Zlib for Windows"
[10]: http://www.bzip.org/ "Bzip2"
[11]: http://gnuwin32.sourceforge.net/packages/bzip2.htm "Bzip2 for Windows"
[12]: http://www.openssl.org/ "OpenSSL - The Open Source toolkit for SSL/TLS"
[13]: http://slproweb.com/products/Win32OpenSSL.html "OpenSSL for Windows"
[14]: http://www.ubuntu.com/ "Ubuntu - The world's most popular free OS"
[15]: http://windows.microsoft.com/ "Microsoft Windows"
[19]: https://github.com/ElunaLuaEngine/Eluna
[20]: https://github.com/ike3/mangosbot-bots
[21]: http://github.com/memononen/recastnavigation "Recast - Navigation-mesh Toolset for Games"
