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
| `.dc test <dungeon>` | | GM: teleport the GM and grouped bots to the entrance and enable; dungeon names may contain spaces. Blackrock Spire also accepts `lower` or `upper`. |

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
| Per-run settings panel | Supported |

## Notes

- Tank must be a bot (or self-bot). Play a follower if you want hands-on.
- Strategies install only inside dungeon and raid maps (performance gate).
- Dynamic pulls charge only up to `PullDynamicMaxLeeroyMobs`; larger packs pause
  the run instead of silently aggroing the room. Advanced pulls use each class's
  configured pull ability when available.
- The party waits for `RestHealth` / `RestMana` before advancing. A wipe pauses
  the run; `PreventBotRelease = 1` leaves bot corpses for resurrection, while
  `PreventBotRelease = 0` releases the online dead bot members. It resumes
  automatically only after every online, in-world group member is alive in the
  same map and instance, out of combat, and rested. Disconnected members are
  excluded from the active recovery roster so they cannot deadlock a bot-only
  run; an online member in another instance still blocks resume until returning.
- `LootQualityMin` filters ordinary low-quality loot while retaining quest,
  crafting, consumable, forced-loot, and equipment-use items.
- The addon Settings page can override `PreventBotRelease`, loot quality, rest
  thresholds, chest handling, and Dynamic's maximum pack size for the current
  run; reset any setting to return to the server default.
- The tank pauses briefly when a living party member exceeds `PartyMaxSpread`,
  allowing the normal follower behavior to catch up before the next pull.
- Classic boss rosters cover every vanilla five-player map plus Onyxia, Zul'Gurub,
  Molten Core, Blackwing Lair, AQ20, AQ40, and Naxxramas. Scripted events and
  summon-only encounters remain dependent on the matching world database/scripts.
