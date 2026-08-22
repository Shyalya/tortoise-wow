#!/usr/bin/env python3
"""
dungeon_model.py - build a top-down spatial MODEL of a dungeon (or a region of
one) straight from the spawn tables, to debug dungeon-clear navigation / aggro /
room-clear issues without guessing.

It renders an ASCII map (north up, west left), groups spawns into packs, and --
when centred on a boss -- overlays the three rings the dungeon-clear code reasons
about and reports the "dead band" between them (the class of bug where a pack is
KEPT as room-trash yet UNREACHABLE by the aggro skirt, so the tank fixates and
loops). That dead band is what made the Jammal'an room clear deadlock.

Pure game data (acore_world: `creature` (+ `creature_template`), optionally
`gameobject`, and `instance_encounters` for boss detection). Read-only.

OFFLINE DEV TOOL — not part of the running module: not compiled in, not invoked
by any hook/tick/command, run by hand. See tools/README.md.

Examples:
  # Whole-dungeon overview: list every boss and its surrounding pack.
  python3 tools/dungeon_model.py --map 109 --list-bosses

  # Model the region around a boss, with the DC rings + dead-band report.
  python3 tools/dungeon_model.py --map 109 --around 5710 --boss

  # Model an explicit area; include gameobjects (doors/levers). Use --center=...
  # (with '='), as the leading minus of a coord otherwise looks like a flag.
  python3 tools/dungeon_model.py --map 109 --center=-467,95,-91 --radius 70 --gos

  # Override the rings (aggro depends on level; read the real values off the
  # room-trash 'sphere=' and skirt 'r=' log lines for the run you are debugging).
  python3 tools/dungeon_model.py --map 109 --around 5710 --boss --aggro 27 --pad 6

DB defaults match the other tools (mysql CLI: 127.0.0.1 / acore / acore /
acore_world); override with --host/--user/--pass/--db.

NOTE: this models STATIC spawns only. Summoned/phased mobs (Weaver & Dreamscythe
before Jammal'an dies, Avatar of Hakkar, pool spawns) and the navmesh itself are
NOT here -- treat it as the skeleton, then confirm the live picture in-game/logs.
"""
import argparse
import math
import subprocess
import sys
from collections import defaultdict

# 5-man instance maps -> name (same table the other tools use), for headers.
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

# Obviously friendly/neutral filler factions (rabbits, friendly NPCs) to drop.
SKIP_FACTIONS = {35, 31, 7, 12, 55, 114, 188}


def mysql(args, sql):
    out = subprocess.run(
        ["mysql", "-h", args.host, "-u", args.user, "-p" + args.password,
         args.db, "-N", "-B", "-e", sql],
        capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("mysql failed: " + out.stderr.strip())
    rows = []
    for line in out.stdout.splitlines():
        if line:
            rows.append(line.split("\t"))
    return rows


def spawn_entry_column(args):
    """`creature.id1` on older schemas, `creature.id` after the upstream rename."""
    found = {r[0] for r in mysql(args,
        "SELECT COLUMN_NAME FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'creature' "
        "AND COLUMN_NAME IN ('id', 'id1')") if r}
    for candidate in ("id1", "id"):
        if candidate in found:
            return candidate
    sys.exit("creature table has neither an `id` nor an `id1` column — "
             "is " + args.db + " really a world DB?")


def fetch_creatures(args, mapid):
    entry = spawn_entry_column(args)
    sql = (
        f"SELECT c.guid, c.{entry}, ct.name, ct.rank, ct.faction, ct.unit_flags, "
        "c.position_x, c.position_y, c.position_z, "
        # EXISTS, not a LEFT JOIN: a boss that is an encounter on both normal and
        # heroic has two instance_encounters rows, and a join would emit that
        # spawn twice -- double-counting it in the packs and ring reports.
        "EXISTS (SELECT 1 FROM instance_encounters ie "
        f"        WHERE ie.creditEntry = c.{entry} AND ie.creditType = 0) AS is_boss "
        f"FROM creature c JOIN creature_template ct ON ct.entry = c.{entry} "
        f"WHERE c.map = {mapid}")
    rows = []
    for f in mysql(args, sql):
        if len(f) < 10:
            continue
        rows.append(dict(
            kind="C", guid=int(f[0]), entry=int(f[1]), name=f[2],
            rank=int(f[3]), faction=int(f[4]), flags=int(f[5]),
            x=float(f[6]), y=float(f[7]), z=float(f[8]), is_boss=int(f[9])))
    return rows


def fetch_gos(args, mapid):
    sql = ("SELECT g.guid, g.id, gt.name, gt.type, "
           "g.position_x, g.position_y, g.position_z "
           "FROM gameobject g JOIN gameobject_template gt ON gt.entry = g.id "
           f"WHERE g.map = {mapid}")
    rows = []
    for f in mysql(args, sql):
        if len(f) < 7:
            continue
        rows.append(dict(
            kind="G", guid=int(f[0]), entry=int(f[1]), name=f[2],
            gotype=int(f[3]), x=float(f[4]), y=float(f[5]), z=float(f[6])))
    return rows


def dist2d(a, b):
    return math.hypot(a["x"] - b["x"], a["y"] - b["y"])


def cluster(points, eps, ztol):
    """Union-find flood fill: connect spawns within eps (2D) and ztol (z)."""
    n = len(points)
    parent = list(range(n))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for i in range(n):
        for j in range(i + 1, n):
            if (abs(points[i]["z"] - points[j]["z"]) <= ztol
                    and dist2d(points[i], points[j]) <= eps):
                parent[find(i)] = find(j)

    groups = defaultdict(list)
    for i in range(n):
        groups[find(i)].append(points[i])
    return list(groups.values())


# --- rendering ------------------------------------------------------------

def render(center, region, rings, gos, width, packs):
    """Top-down ASCII grid. WoW axes: +X = north (up), +Y = west (left)."""
    pts = list(region) + list(gos)
    if center:
        pts = pts + [center]
    if not pts:
        return ["(nothing in region)"]

    # Bounds, padded by the largest ring so rings are visible.
    pad = max(rings, default=0.0) + 4.0
    xs = [p["x"] for p in pts]
    ys = [p["y"] for p in pts]
    cx = center["x"] if center else (max(xs) + min(xs)) / 2
    cy = center["y"] if center else (max(ys) + min(ys)) / 2
    minx = min(min(xs), cx - pad)
    maxx = max(max(xs), cx + pad)
    miny = min(min(ys), cy - pad)
    maxy = max(max(ys), cy + pad)

    spanx = max(maxx - minx, 1.0)
    spany = max(maxy - miny, 1.0)
    # Uniform yards-per-cell; rows get half resolution for the ~2:1 char aspect
    # so circles stay roughly round and yards read the same on both axes.
    ypc = spany / (width - 1)             # cols map to Y (west-east)
    height = max(3, int(spanx / ypc / 2.0) + 1)

    def cell(x, y):
        col = int(round((maxy - y) / spany * (width - 1)))   # west(+Y) -> left
        row = int(round((maxx - x) / spanx * (height - 1)))  # north(+X) -> top
        return row, col

    grid = [[" "] * width for _ in range(height)]

    # Rings first (lowest priority), drawn parametrically so they read as circles.
    for r in rings:
        steps = max(60, int(2 * math.pi * r / ypc))
        for s in range(steps):
            th = 2 * math.pi * s / steps
            rr, cc = cell(cx + r * math.cos(th), cy + r * math.sin(th))
            if 0 <= rr < height and 0 <= cc < width and grid[rr][cc] == " ":
                grid[rr][cc] = "."

    # Gameobjects.
    for g in gos:
        rr, cc = cell(g["x"], g["y"])
        if 0 <= rr < height and 0 <= cc < width:
            grid[rr][cc] = "+"

    # Packs: one letter each (boss packs upper-cased by the caller's label).
    for pk in packs:
        ch = pk["label"]
        for m in pk["members"]:
            rr, cc = cell(m["x"], m["y"])
            if 0 <= rr < height and 0 <= cc < width:
                # Don't let a trash glyph bury a boss glyph already placed.
                if grid[rr][cc] not in ("@",):
                    grid[rr][cc] = ch

    # The reference marker last (highest priority).
    if center:
        rr, cc = cell(center["x"], center["y"])
        if 0 <= rr < height and 0 <= cc < width:
            grid[rr][cc] = "@"

    out = ["    +" + "-" * width + "+   (north up, west left)"]
    for row in grid:
        out.append("    |" + "".join(row) + "|")
    out.append("    +" + "-" * width + "+")
    out.append(f"    X (north+): {maxx:.0f} top .. {minx:.0f} bottom   "
               f"Y (west+): {maxy:.0f} left .. {miny:.0f} right   "
               f"~{ypc:.1f} yd/col")
    return out


# --- analysis -------------------------------------------------------------

def bearing(center, p):
    return math.degrees(math.atan2(p["y"] - center["y"], p["x"] - center["x"]))


def hostile(p):
    return p["faction"] not in SKIP_FACTIONS


def pack_centroid(members):
    n = len(members)
    return dict(x=sum(m["x"] for m in members) / n,
                y=sum(m["y"] for m in members) / n,
                z=sum(m["z"] for m in members) / n)


def label_packs(packs_raw, center):
    """Assign a glyph + summary to each pack, nearest-to-center first."""
    packs = []
    for members in packs_raw:
        c = pack_centroid(members)
        d = math.hypot(c["x"] - center["x"], c["y"] - center["y"]) if center else 0.0
        packs.append(dict(members=members, centroid=c, dist=d))
    packs.sort(key=lambda p: p["dist"])
    letters = "abcdefghijklmnopqrstuvwxyz"
    for i, pk in enumerate(packs):
        # A pack that IS the boss spawn gets '@'/upper; rest get a..z (then 'o').
        is_boss = any(m.get("is_boss") for m in pk["members"])
        pk["label"] = "@" if is_boss else (letters[i] if i < len(letters) else "o")
        pk["is_boss"] = is_boss
    return packs


def main():
    ap = argparse.ArgumentParser(
        description="Top-down spatial model of a dungeon region for DC debugging.")
    ap.add_argument("--map", type=int, required=True, help="instance map id")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--around", type=int,
                   help="centre on the first spawn of this creature entry")
    g.add_argument("--center", help="centre on explicit coords; pass as "
                   "--center=x,y,z (the '=' avoids a negative x looking like a flag)")
    ap.add_argument("--radius", type=float, default=100.0,
                    help="region radius (2D yd) around the centre (default 100)")
    ap.add_argument("--list-bosses", action="store_true",
                    help="just list this map's bosses (instance_encounters) + exit")
    ap.add_argument("--boss", action="store_true",
                    help="treat the centre as a boss: draw the DC rings "
                         "(exclusion/skirt/room) and report the dead band")
    ap.add_argument("--aggro", type=float, default=27.0,
                    help="DC room-trash EXCLUSION radius ~ aggro+reaches+margin "
                         "(default 27; read the log 'sphere=' for the real value)")
    ap.add_argument("--pad", type=float, default=6.0,
                    help="RoomAggroPathPadding added for the skirt AVOID radius "
                         "(default 6; skirt 'r=' minus 'sphere=' in the logs)")
    ap.add_argument("--room", type=float, default=90.0,
                    help="RoomAggroRegistry room radius (default 90)")
    ap.add_argument("--rings", help="extra comma rings to draw, e.g. '15,40'")
    ap.add_argument("--gos", action="store_true", help="include gameobjects (+)")
    ap.add_argument("--all-factions", action="store_true",
                    help="keep friendly/neutral filler factions too")
    ap.add_argument("--eps", type=float, default=12.0, help="pack link radius 2D")
    ap.add_argument("--ztol", type=float, default=6.0, help="pack link z tolerance")
    ap.add_argument("--width", type=int, default=78, help="ASCII grid width")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--user", default="acore")
    ap.add_argument("--password", "--pass", dest="password", default="acore")
    ap.add_argument("--db", default="acore_world")
    args = ap.parse_args()

    mapname = DUNGEON_MAPS.get(args.map, f"map {args.map}")
    creatures = fetch_creatures(args, args.map)
    if not args.all_factions:
        creatures = [c for c in creatures if hostile(c)]

    if args.list_bosses:
        bosses = [c for c in creatures if c["is_boss"]]
        print(f"# {mapname} (map {args.map}) - {len(bosses)} boss spawn(s)")
        for b in sorted(bosses, key=lambda c: c["entry"]):
            print(f"  {b['entry']:>6}  {b['name']:<32} "
                  f"({b['x']:.1f}, {b['y']:.1f}, {b['z']:.1f})  guid {b['guid']}")
        print("\nRun again with --around <entry> --boss to model a boss room.")
        return

    # Resolve the centre.
    center = None
    if args.around is not None:
        match = [c for c in creatures if c["entry"] == args.around]
        if not match:
            sys.exit(f"entry {args.around} has no spawn on map {args.map}")
        center = match[0]
    elif args.center:
        try:
            x, y, z = (float(v) for v in args.center.split(","))
        except ValueError:
            sys.exit("--center must be 'x,y,z'")
        center = dict(kind="ref", entry=0, name="(point)", x=x, y=y, z=z,
                      faction=0, rank=0, flags=0, is_boss=0)
    else:
        sys.exit("give --around <entry>, --center x,y,z, or --list-bosses")

    # Region filter (2D radius around the centre), excluding the centre itself.
    region = [c for c in creatures
              if math.hypot(c["x"] - center["x"], c["y"] - center["y"]) <= args.radius
              and c["guid"] != center.get("guid")]
    gos = []
    if args.gos:
        gos = [g for g in fetch_gos(args, args.map)
               if math.hypot(g["x"] - center["x"], g["y"] - center["y"]) <= args.radius]

    rings = []
    if args.boss:
        rings = [args.aggro, args.aggro + args.pad, args.room]
    if args.rings:
        rings += [float(v) for v in args.rings.split(",")]

    packs = label_packs(cluster(region, args.eps, args.ztol), center)

    print(f"# {mapname} (map {args.map}) - model around "
          f"{center['name']} ({center['x']:.1f}, {center['y']:.1f}, {center['z']:.1f})"
          f"  r={args.radius:.0f}yd")
    print(f"#   {len(region)} spawns in region, {len(packs)} pack(s)"
          + (f", {len(gos)} gameobject(s)" if args.gos else ""))
    print()
    for line in render(center, region, rings, gos, args.width, packs):
        print(line)
    print()

    # Pack table.
    print("  pack  n  centroid                 dist  bearing  dz     entries")
    for pk in packs:
        c = pk["centroid"]
        d = pk["dist"]
        brg = bearing(center, c)
        dz = c["z"] - center["z"]
        ents = sorted(set(f"{m['name']}" for m in pk["members"]))
        tag = " BOSS" if pk["is_boss"] else ""
        print(f"   {pk['label']:>3} {len(pk['members']):>3}  "
              f"({c['x']:>6.1f},{c['y']:>6.1f},{c['z']:>6.1f})  {d:>5.1f}  "
              f"{brg:>6.0f}  {dz:>5.1f}   {', '.join(ents)[:42]}{tag}")

    # Dead-band report -- the bug class that deadlocked Jammal'an.
    if args.boss:
        excl = args.aggro
        skirt = args.aggro + args.pad
        print()
        print(f"  RINGS: exclusion(room-trash) {excl:.0f}yd  |  "
              f"skirt(avoid) {skirt:.0f}yd  |  room {args.room:.0f}yd")
        band = []
        for pk in packs:
            if pk["is_boss"]:
                continue
            for m in pk["members"]:
                d = math.hypot(m["x"] - center["x"], m["y"] - center["y"])
                if excl < d <= skirt:
                    band.append((d, m, pk["label"]))
        if band:
            print(f"  ⚠ DEAD BAND ({excl:.0f}-{skirt:.0f}yd): {len(band)} spawn(s) "
                  f"KEPT as room-trash but UNREACHABLE by the skirt -- the tank "
                  f"will fixate + loop here:")
            for d, m, lbl in sorted(band):
                print(f"      [{lbl}] {m['name']:<26} ({m['x']:.1f},{m['y']:.1f}) "
                      f"{d:.1f}yd from boss")
            print("    -> these should 'come with the boss' (the exclusion ring "
                  "must match the skirt ring; see DungeonClearRoomTrashValue).")
        else:
            print(f"  ✓ no spawns in the {excl:.0f}-{skirt:.0f}yd dead band.")

    print()
    print("  legend: @ centre/boss   a..z trash pack   + gameobject   . ring")


if __name__ == "__main__":
    main()
