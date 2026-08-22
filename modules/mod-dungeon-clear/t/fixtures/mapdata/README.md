# mapdata — navmesh slices for the Tier-2 nav geometry suite

This directory holds **client-derived (Blizzard) navmesh data** sliced out of a
full server `mmaps/` extract. It is **gitignored and never committed** (see
`.gitignore`). The Tier-2 suite (`t/TestNavGeometry.cpp`) loads from here and
`GTEST_SKIP`s when it is empty, so a clean checkout still builds and passes.

## Layout

```
mapdata/
  mmaps/
    189.mmap          # navmesh params for map 189 (Scarlet Monastery)
    1893232.mmtile    # one tile
    ...
```

## Producing a slice

```bash
# from the module root
tools/slice_mapdata.py --datadir ~/azeroth-server/data --map 189
```

`--datadir` points at the server data dir that contains the extracted `mmaps/`.
For an instanced dungeon map the default copies all of its (few) tiles.

## CI

CI restores this dir from a **private cached artifact** (not the repo). When the
cache is absent the nav suite skips; Tier 1 always runs. Do not wire a step that
commits these files.
