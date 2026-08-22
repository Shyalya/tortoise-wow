#!/usr/bin/env python3
"""
room_aggro_scan.py - discover dungeon bosses that sit inside a dense room of
static trash, so the dungeon-clear tank can be told to clear the room BEFORE
engaging (some bosses dump their whole room on pull: SM Cathedral, Blackheart).

Pure game data: clusters the `creature` spawn table per map (flood-fill, so the
"room" sizes itself to the actual geometry instead of a fragile fixed radius),
then attaches each kill-creature boss (instance_encounters, creditType=0) to the
trash cluster it stands in. Emits per-boss cluster size + bounding volume, the
raw material for RoomClusterRegistry.

This is a DISCOVERY aid, not the source of truth: the roomWide flag is curated
per boss (wave/event encounters must stay off). Re-run after content changes.

OFFLINE DEV TOOL — not part of the running module. It is not compiled in, not
invoked by any hook/tick/command, and never executed by worldserver; a developer
runs it by hand. It issues a single SELECT against acore_world and writes
nothing, so it cannot affect a running server or a connected client. See
tools/README.md.

The spawn-entry column was renamed `creature.id1` -> `creature.id` upstream;
this tool probes information_schema and uses whichever one the DB has, matching
the SFINAE fallback in src/.../Data/CreatureSpawnEntry.h.

Usage:
  python3 tools/room_aggro_scan.py [--eps 16] [--ztol 12] [--min 6] \
      [--host 127.0.0.1] [--user acore] [--pass acore] [--db acore_world]
"""
import argparse
import subprocess
import sys
from collections import defaultdict

# 5-man dungeon instance maps reachable at 3.3.5a (raids excluded by default;
# the dungeon-clear feature targets 5-mans). mapid -> short name for output.
DUNGEON_MAPS = {
    33: "Shadowfang Keep", 34: "Stormwind Stockade", 36: "Deadmines",
    43: "Wailing Caverns", 47: "Razorfen Kraul", 48: "Blackfathom Deeps",
    70: "Uldaman", 90: "Gnomeregan", 109: "Sunken Temple",
    129: "Razorfen Downs", 189: "Scarlet Monastery", 209: "Zul'Farrak",
    229: "Blackrock Spire", 230: "Blackrock Depths", 289: "Scholomance",
    329: "Stratholme", 349: "Maraudon", 389: "Ragefire Chasm", 429: "Dire Maul",
    540: "Shattered Halls", 542: "Blood Furnace", 543: "Hellfire Ramparts",
    545: "Steamvault", 546: "Underbog", 547: "Slave Pens", 552: "Arcatraz",
    553: "Botanica", 554: "Mechanar", 555: "Shadow Labyrinth",
    556: "Sethekk Halls", 557: "Mana-Tombs", 558: "Auchenai Crypts",
    560: "Old Hillsbrad", 269: "Black Morass", 574: "Utgarde Keep",
    575: "Utgarde Pinnacle", 576: "Nexus", 578: "Oculus", 595: "CoT Stratholme",
    599: "Halls of Stone", 600: "Drak'Tharon", 601: "Azjol-Nerub",
    602: "Halls of Lightning", 604: "Gundrak", 608: "Violet Hold",
    619: "Ahn'kahet", 632: "Forge of Souls", 650: "Trial of the Champion",
    658: "Pit of Saron", 668: "Halls of Reflection", 585: "Magisters' Terrace",
}

# Factions that are obviously friendly/neutral filler (rabbits, friendly NPCs).
SKIP_FACTIONS = {35, 31, 7, 12, 55, 114, 188}


def mysql(args, sql):
    out = subprocess.run(
        ["mysql", "-h", args.host, "-u", args.user, "-p" + args.password,
         args.db, "-N", "-B", "-e", sql],
        capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("mysql failed: " + out.stderr.strip())
    return out.stdout


def spawn_entry_column(args):
    """`creature.id1` on older schemas, `creature.id` after the upstream rename."""
    found = set(mysql(args,
        "SELECT COLUMN_NAME FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'creature' "
        "AND COLUMN_NAME IN ('id', 'id1')").split())
    for candidate in ("id1", "id"):
        if candidate in found:
            return candidate
    sys.exit("creature table has neither an `id` nor an `id1` column — "
             "is " + args.db + " really a world DB?")


def fetch(args):
    entry = spawn_entry_column(args)
    sql = (
        f"SELECT c.guid, c.map, c.{entry}, ct.name, ct.rank, ct.type, ct.faction, "
        "c.position_x, c.position_y, c.position_z, "
        # EXISTS, not a LEFT JOIN: a boss that is an encounter on both normal and
        # heroic has two instance_encounters rows and a join would emit it twice.
        "EXISTS (SELECT 1 FROM instance_encounters ie "
        f"        WHERE ie.creditEntry = c.{entry} AND ie.creditType = 0) AS is_boss "
        "FROM creature c "
        f"JOIN creature_template ct ON ct.entry = c.{entry} "
        "WHERE c.map IN (" + ",".join(str(m) for m in DUNGEON_MAPS) + ")"
    )
    rows = []
    for line in mysql(args, sql).splitlines():
        f = line.split("\t")
        if len(f) < 11:
            continue
        rows.append(dict(
            guid=int(f[0]), map=int(f[1]), entry=int(f[2]), name=f[3],
            rank=int(f[4]), ctype=int(f[5]), faction=int(f[6]),
            x=float(f[7]), y=float(f[8]), z=float(f[9]), is_boss=int(f[10])))
    return rows


def cluster(points, eps, ztol):
    """Union-find flood fill: connect spawns within eps (2D) and ztol (z)."""
    n = len(points)
    parent = list(range(n))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        parent[find(a)] = find(b)

    # grid bucket for O(n) neighbour lookup
    cell = eps
    buckets = defaultdict(list)
    for i, p in enumerate(points):
        buckets[(int(p["x"] // cell), int(p["y"] // cell))].append(i)
    eps2 = eps * eps
    for i, p in enumerate(points):
        cx, cy = int(p["x"] // cell), int(p["y"] // cell)
        for gx in (cx - 1, cx, cx + 1):
            for gy in (cy - 1, cy, cy + 1):
                for j in buckets.get((gx, gy), ()):
                    if j <= i:
                        continue
                    q = points[j]
                    if abs(p["z"] - q["z"]) > ztol:
                        continue
                    if (p["x"] - q["x"]) ** 2 + (p["y"] - q["y"]) ** 2 <= eps2:
                        union(i, j)
    groups = defaultdict(list)
    for i in range(n):
        groups[find(i)].append(i)
    return list(groups.values())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--eps", type=float, default=16.0)
    ap.add_argument("--ztol", type=float, default=12.0)
    ap.add_argument("--min", type=int, default=6)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--user", default="acore")
    ap.add_argument("--password", "--pass", dest="password", default="acore")
    ap.add_argument("--db", default="acore_world")
    args = ap.parse_args()

    rows = fetch(args)
    by_map = defaultdict(list)
    for r in rows:
        by_map[r["map"]].append(r)

    print(f"{'MAP':<22}{'BOSS':<30}{'ROOM':>5}{'XSPAN':>7}{'YSPAN':>7}{'ZSPAN':>7}")
    print("-" * 78)
    for mapid in sorted(by_map):
        rs = by_map[mapid]
        trash = [r for r in rs
                 if not r["is_boss"] and r["rank"] in (0, 1)
                 and r["ctype"] != 8 and r["faction"] not in SKIP_FACTIONS]
        bosses = [r for r in rs if r["is_boss"]]
        if not trash or not bosses:
            continue
        groups = cluster(trash, args.eps, args.ztol)
        # member index -> group members
        for b in sorted(bosses, key=lambda r: r["name"]):
            best = None
            for g in groups:
                # boss attaches to a cluster if any member is within eps*2 of it
                near = [trash[i] for i in g
                        if (trash[i]["x"] - b["x"]) ** 2
                        + (trash[i]["y"] - b["y"]) ** 2 <= (args.eps * 2) ** 2
                        and abs(trash[i]["z"] - b["z"]) <= args.ztol * 2]
                if near and (best is None or len(g) > len(best)):
                    best = g
            if not best or len(best) < args.min:
                continue
            xs = [trash[i]["x"] for i in best]
            ys = [trash[i]["y"] for i in best]
            zs = [trash[i]["z"] for i in best]
            print(f"{DUNGEON_MAPS.get(mapid, str(mapid)):<22}"
                  f"{b['name'][:29]:<30}{len(best):>5}"
                  f"{max(xs)-min(xs):>7.0f}{max(ys)-min(ys):>7.0f}"
                  f"{max(zs)-min(zs):>7.0f}")


if __name__ == "__main__":
    main()
