#!/usr/bin/env python3
# Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3.
#
# Tier-2 navmesh test-data slicer (headless-sim plan).
#
# Copies the minimal Detour navmesh slice for ONE dungeon map out of a full
# extracted mmaps directory into the module's gitignored test-data dir, so the
# Tier-2 nav geometry suite (t/TestNavGeometry.cpp) can route against real
# geometry offline.
#
# IMPORTANT: the copied .mmap/.mmtile files are CLIENT-DERIVED (Blizzard) map
# geometry. They are NEVER committed — the destination dir is gitignored. This
# tool is run locally (or by CI from a private cached extract) to materialize the
# slice before running the nav suite.
#
# A dungeon is an INSTANCED map with a small footprint (a handful of 533yd
# tiles), so the default --all copies the whole map cheaply. For a continent map
# (huge) pass explicit --tiles.
#
# Usage:
#   tools/slice_mapdata.py --datadir /path/to/server/data --map 189
#   tools/slice_mapdata.py --datadir ~/azeroth-server/data --map 189 \
#       --tiles 32,32 33,32 --out t/fixtures/mapdata
#
# After slicing, author scenarios in t/fixtures/nav/*.json with real coords.

import argparse
import os
import shutil
import sys
import glob


def human(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.1f}{unit}"
        n /= 1024


def main():
    ap = argparse.ArgumentParser(description="Slice one map's mmaps for the nav test harness.")
    ap.add_argument("--datadir", required=True,
                    help="server data dir containing an 'mmaps/' subdir (the extracted navmesh)")
    ap.add_argument("--map", required=True, type=int, help="map id (e.g. 189 = Scarlet Monastery)")
    default_out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                               "t", "fixtures", "mapdata")
    ap.add_argument("--out", default=default_out,
                    help=f"destination data dir (default: {default_out})")
    ap.add_argument("--tiles", nargs="*", default=None,
                    help="explicit tile list 'xx,yy' (grid coords); default copies ALL tiles for the map")
    args = ap.parse_args()

    src_mmaps = os.path.join(args.datadir, "mmaps")
    if not os.path.isdir(src_mmaps):
        sys.exit(f"error: no mmaps dir at {src_mmaps}")

    dst_mmaps = os.path.join(args.out, "mmaps")
    os.makedirs(dst_mmaps, exist_ok=True)

    map_prefix = f"{args.map:03d}"
    copied = []
    total = 0

    def copy(name):
        nonlocal total
        src = os.path.join(src_mmaps, name)
        if not os.path.isfile(src):
            return False
        shutil.copy2(src, os.path.join(dst_mmaps, name))
        sz = os.path.getsize(src)
        copied.append((name, sz))
        total += sz
        return True

    # The navmesh params file is mandatory.
    if not copy(f"{map_prefix}.mmap"):
        sys.exit(f"error: no {map_prefix}.mmap in {src_mmaps} — is map {args.map} extracted?")

    if args.tiles:
        for t in args.tiles:
            try:
                x, y = (int(p) for p in t.replace(" ", "").split(","))
            except ValueError:
                sys.exit(f"error: bad --tiles entry '{t}', expected 'xx,yy'")
            if not copy(f"{map_prefix}{x:02d}{y:02d}.mmtile"):
                print(f"  warn: tile {x:02d},{y:02d} not found, skipping")
    else:
        for src in glob.glob(os.path.join(src_mmaps, f"{map_prefix}*.mmtile")):
            copy(os.path.basename(src))

    if len(copied) <= 1:
        sys.exit(f"error: copied no tiles for map {args.map} (only the .mmap). "
                 f"Check the map id and that tiles are extracted.")

    print(f"sliced map {args.map} -> {dst_mmaps}")
    for name, sz in sorted(copied):
        print(f"  {name:>18}  {human(sz)}")
    print(f"total: {len(copied)} files, {human(total)}")
    print("\nNote: this slice is client-derived and gitignored — never commit it.")
    print("Next: author t/fixtures/nav/*.json scenarios with real coords from this map.")


if __name__ == "__main__":
    main()
