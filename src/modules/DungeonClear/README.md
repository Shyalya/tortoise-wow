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

In a dungeon with a bot tank in your party:

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
| `.dc test <dungeon>` | | GM: teleport to entrance + enable |

Companion addon: copy `addon/` into the Turtle client `Interface/AddOns/DungeonClear/`.

## Notes

- Tank must be a bot (or self-bot). Play a follower if you want hands-on.
- Strategies install only inside dungeon maps (performance gate).
- Classic event tables cover starter five (DM, SFK, WC, SM, ZF) plus extended
  Classic instances; Turtle GO/NPC ids may need per-dungeon tuning.
