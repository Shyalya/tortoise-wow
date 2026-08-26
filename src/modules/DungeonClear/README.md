# Tortoise Dungeon Clear

Optional autonomous dungeon-clearing AI for playerbots, inspired by
[jrad7/mod-dungeon-clear](https://github.com/jrad7/mod-dungeon-clear).

## Build

```
cmake -S . -B build -DBUILD_PLAYERBOTS=ON -DBUILD_DUNGEON_CLEAR=ON ...
cmake --build build --target mangosd -j$(nproc)
```

Copy `conf/dungeon_clear.conf.dist` keys into `mangosd.conf` (or load the file
if your Config setup includes it).

## Usage

In a dungeon or raid with a bot tank in your party:

| Command | Party chat | Effect |
|---------|------------|--------|
| `.dc on` | `dc on` | Start clear (tank drives) |
| `.dc off` | `dc off` | Stop |
| `.dc pause` | `dc pause` | Soft pause / resume |
| `.dc skip` | `dc skip` | Skip current objective |
| `.dc pull` | `dc pull` | Cycle Dynamic / Leeroy / Advanced |
| `.dc status` | `dc status` | Status line |
| `.dc bosses` | `dc bosses` | Boss list |
| `.dc go <name>` | | Route to named boss |
| `.dc test <dungeon> [seed]` | | GM: teleport to entrance + enable; dungeon names may contain spaces. Blackrock Spire also accepts `lower` or `upper`. |

## Companion addon

Port of [mod-dungeon-clear-addon](https://github.com/jrad7/mod-dungeon-clear-addon)
for the **Turtle WoW** client (`Interface: 1800`).

1. Copy `src/modules/DungeonClear/addon/` to the client as
   `Interface/AddOns/DungeonClear/` (folder name must be exactly `DungeonClear`).
2. Enable the addon at character select (allow out-of-date if asked).
3. Type `/dc` in-game for the panel.

The panel talks to the server over addon messages (`prefix DC`, payload
`CMD\t<sub>[\t<param>]`). While a clear is running the server pushes live
`STATUS` / `BOSS` updates; opening the panel also requests them.

| Feature | Status |
|---------|--------|
| On / Off / Pause / Skip / Go / Pull | Supported |
| Live status + boss list | Supported |
| Tiny mode / minimap button | Supported |
| Spectate camera | Not on Tortoise yet (button stays disabled) |
| Per-run settings panel | Not on Tortoise yet (empty sync) |

## Notes

- Tank must be a bot (or self-bot). Play a follower if you want hands-on.
- Strategies install only inside dungeon and raid maps (performance gate).
- Classic boss rosters cover every vanilla five-player map plus Onyxia, Zul'Gurub,
  Molten Core, Blackwing Lair, AQ20, AQ40, and Naxxramas. Scripted events and
  summon-only encounters remain dependent on the matching world database/scripts.
