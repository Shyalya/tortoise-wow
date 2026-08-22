#!/usr/bin/env python3
"""
room_aggro_audit.py — keep RoomAggroRegistry honest against the game data.

A room-wide-aggro boss force-pulls the surrounding room on engage. There are two
authoritative sources for that mechanic, and this tool emits both, joined to the
maps the creatures actually spawn on, then DIFFs them against the curated
RoomAggroRegistry (RoomAggroRegistry.cpp) so a content update that adds/changes
such an encounter surfaces instead of silently going unhandled:

  (a) SmartAI (data-driven): smart_scripts rows with action SET_IN_COMBAT_WITH_ZONE
      (38) or CALL_FOR_HELP (39) on an aggro/combat-start event, radius =
      action_param1. Joined to `creature` so each gets its spawn map(s).

  (b) C++ idioms: a grep of the dungeon script dirs for CallForHelp(...) /
      GetCreatureListWithEntryInGrid + a combat-start call. The C++ side cannot
      be auto-joined to a map/entry (the entry lives in a script enum), so it is
      printed for manual review, not diffed.

The spawn-cluster heuristic (room_aggro_scan.py) is the third, weakest signal —
a geometry cross-check only; this tool supersedes it as the source of truth.

OFFLINE DEV TOOL — not part of the running module: not compiled in, not invoked
by any hook/tick/command, run by hand. Reads acore_world and src/, writes
nothing. See tools/README.md.

NOTE on scope: whole-instance pulls (action 38 / CallForHelp with no bounding
entry list, i.e. SetInCombatWithZone on the boss itself) are OUT of scope — a
whole zone can't be pre-cleared. They are flagged [zone?] so a reviewer can
exclude them by decision rather than registering them.

Usage:
  python3 tools/room_aggro_audit.py [--core /path/to/azerothcore-wotlk] \\
      [--host 127.0.0.1] [--user acore] [--pass acore] [--db acore_world]
"""
import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict

# 5-man dungeon maps (mirror of room_aggro_scan.py). mapid -> short name.
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

# SMART_ACTION ids.
ACTION_SET_IN_COMBAT_WITH_ZONE = 38
ACTION_CALL_FOR_HELP = 39

# C++ script roots that hold 5-man dungeon scripts.
CPP_DIRS = [
    "src/server/scripts/EasternKingdoms",
    "src/server/scripts/Kalimdor",
    "src/server/scripts/Outland",
    "src/server/scripts/Northrend",
]


def mysql(args, sql):
    out = subprocess.run(
        ["mysql", "-h", args.host, "-u", args.user, "-p" + args.password,
         args.db, "-N", "-B", "-e", sql],
        capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("mysql failed: " + out.stderr.strip())
    return [line.split("\t") for line in out.stdout.splitlines() if line]


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


def smartai_scan(args):
    """action 38/39 on combat events -> {(map, entry): (action, radius, name)}."""
    sql = (
        "SELECT ss.entryorguid, ss.action_type, ss.action_param1, ct.name "
        "FROM smart_scripts ss "
        "JOIN creature_template ct ON ct.entry = ss.entryorguid "
        "WHERE ss.source_type = 0 "
        f"AND ss.action_type IN ({ACTION_SET_IN_COMBAT_WITH_ZONE},{ACTION_CALL_FOR_HELP}) "
        "AND ss.entryorguid > 0"
    )
    by_entry = {}
    for f in mysql(args, sql):
        if len(f) < 4:
            continue
        entry, action, radius, name = int(f[0]), int(f[1]), f[2], f[3]
        by_entry[entry] = (action, radius, name)
    if not by_entry:
        return {}

    # Map each entry to the dungeon map(s) it spawns on. DISTINCT because a
    # single entry usually has many spawn rows and only the (entry, map) pair
    # matters here.
    entry_col = spawn_entry_column(args)
    entries = ",".join(str(e) for e in by_entry)
    rows = mysql(args, f"SELECT DISTINCT {entry_col}, map FROM creature "
                       f"WHERE {entry_col} IN ({entries})")
    result = {}
    for f in rows:
        entry, mapid = int(f[0]), int(f[1])
        if mapid not in DUNGEON_MAPS:
            continue
        action, radius, name = by_entry[entry]
        result[(mapid, entry)] = (action, radius, name)
    return result


def parse_registry(core):
    """Pull {(map, entry): radius} from RoomAggroRegistry.cpp."""
    path = os.path.join(
        core, "modules/mod-dungeon-clear/src/Ai/Dungeon/DungeonClear/Data/"
        "RoomAggroRegistry.cpp")
    flagged = {}
    if not os.path.exists(path):
        print(f"WARNING: registry not found at {path}", file=sys.stderr)
        return flagged
    row = re.compile(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*([\d.]+)f")
    with open(path) as fh:
        for line in fh:
            m = row.search(line)
            if m:
                flagged[(int(m.group(1)), int(m.group(2)))] = float(m.group(3))
    return flagged


def cpp_idiom_grep(core):
    """Lines in the dungeon script dirs that force room aggro in C++."""
    hits = []
    pat = re.compile(r"CallForHelp\s*\(|GetCreatureListWithEntryInGrid|"
                     r"SetInCombatWithZone")
    for d in CPP_DIRS:
        root = os.path.join(core, d)
        if not os.path.isdir(root):
            continue
        for dirpath, _, files in os.walk(root):
            for fn in files:
                if not fn.endswith(".cpp"):
                    continue
                fp = os.path.join(dirpath, fn)
                try:
                    with open(fp, errors="ignore") as fh:
                        for i, line in enumerate(fh, 1):
                            if pat.search(line):
                                rel = os.path.relpath(fp, core)
                                hits.append((rel, i, line.strip()))
                except OSError:
                    pass
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "../../..")),
        help="azerothcore-wotlk checkout root")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--user", default="acore")
    ap.add_argument("--password", "--pass", dest="password", default="acore")
    ap.add_argument("--db", default="acore_world")
    args = ap.parse_args()

    registry = parse_registry(args.core)
    smart = smartai_scan(args)

    print("=== SmartAI room-aggro (action 38/39) vs RoomAggroRegistry ===")
    print(f"{'MAP':<20}{'ENTRY':>7} {'ACT':>4}{'RADIUS':>8}  {'IN REG':<7}NAME")
    print("-" * 78)
    for (mapid, entry), (action, radius, name) in sorted(smart.items()):
        zone = " [zone?]" if action == ACTION_SET_IN_COMBAT_WITH_ZONE else ""
        in_reg = "yes" if (mapid, entry) in registry else "NO"
        print(f"{DUNGEON_MAPS.get(mapid, str(mapid)):<20}{entry:>7} "
              f"{action:>4}{radius:>8}  {in_reg:<7}{name}{zone}")

    # Registry rows that no SmartAI scan and no C++ idiom backs are fine (they may
    # be C++-driven, which can't be auto-joined), but a registry row whose map no
    # longer hosts the entry is dead weight — flag it.
    print("\n=== Registry rows not seen in the SmartAI scan ===")
    print("(expected for C++-driven bosses — cross-check the C++ grep below)")
    for (mapid, entry), radius in sorted(registry.items()):
        if (mapid, entry) not in smart:
            print(f"  map {mapid} ({DUNGEON_MAPS.get(mapid, '?')}) "
                  f"entry {entry} radius {radius}")

    print("\n=== C++ force-aggro idioms in dungeon script dirs (manual review) ===")
    for rel, line, text in cpp_idiom_grep(args.core):
        print(f"  {rel}:{line}: {text}")


if __name__ == "__main__":
    main()
