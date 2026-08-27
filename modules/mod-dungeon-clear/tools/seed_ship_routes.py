# -*- coding: utf-8 -*-
"""Die drei Schiffs-Beine der Todesminen aus dem Netz herausrechnen.

Gemessen (meshprobe): Das Navigationsnetz TRAEGT den Weg aufs Oberdeck -
55 Polygone, mit genau dem Filter, mit dem der Kern fragt, und ueber die
Rampe, auf der auch die Piraten stehen (Z 17 -> 19 -> 22 -> 24 -> 27 -> 28
-> 31 -> 39). Unser eigener Wegsucher findet ihn trotzdem nicht und meldet
"unreachable"; deshalb steht seit Stunden jede Gruppe bei 6/10 oder 7/10
unter dem Schiff im Wasser.

Ein Anker-Weg umgeht die Suche: der Follower laeuft Anker fuer Anker,
ohne noch einmal zu fragen. Diese Anker sind keine Erfindung - sie sind die
Eckpunkte genau des Korridors, den Detour selbst liefert. Gelaufen wird also
der Weg, den auch ein Spieler nimmt.

Geschrieben werden .route (wird zur Laufzeit gelesen, kein Neubau noetig)
und .cpp (damit die Route im Repo mitfaehrt und die
Kuerzestes-gewinnt-Regel etwas zum Vergleichen hat)."""
import subprocess, sys, os

PROBE = '/tmp/meshprobe'
MMAPS = os.path.expanduser('~/turtle/data/mmaps')
OUT = os.path.expanduser('~/tortoise-playerbots/modules/mod-dungeon-clear/src/Routes')

# (Bossnummer, Name, Start = wo der vorige Boss faellt, Ziel = Bossposition)
LEGS = [
    (647, 'Captain Greenskin', (-23, -797, 20), (-69, -808, 41)),
    (639, 'Edwin VanCleef',    (-69, -808, 41), (-87, -820, 39)),
    (645, 'Cookie',            (-87, -820, 39), (-68, -854, 17)),
]

for entry, name, a, b in LEGS:
    cmd = [PROBE, MMAPS, '36', 'route',
           str(a[0]), str(a[1]), str(a[2]), str(b[0]), str(b[1]), str(b[2]),
           'inc=0x9', 'exc=0x10']
    out = subprocess.run(cmd, capture_output=True, text=True).stdout.splitlines()
    hdr = [l for l in out if l.startswith('ANKER')]
    if not hdr:
        print('%s: kein Weg im Netz - uebersprungen' % name)
        continue
    count, length = int(hdr[0].split()[1]), int(hdr[0].split()[3])
    pts = []
    for l in out[out.index(hdr[0]) + 1:]:
        p = l.split()
        if len(p) == 3:
            pts.append(tuple(float(v) for v in p))
    if len(pts) < 3:
        print('%s: nur %d Anker - uebersprungen' % (name, len(pts)))
        continue

    rp = os.path.join(OUT, 'Route_36_%d.route' % entry)
    with open(rp, 'w') as f:
        f.write('# map 36 boss %d len %d\n' % (entry, length))
        for x, y, z in pts:
            f.write('%.4g %.4g %.4g\n' % (x, y, z))

    cp = os.path.join(OUT, 'Route_36_%d.cpp' % entry)
    with open(cp, 'w') as f:
        f.write('// DERIVED from the navmesh itself, not from a live clear.\n')
        f.write('// Map 36, boss %d (%s), %d anchors over %dyd.\n' % (entry, name, len(pts), length))
        f.write('//\n')
        f.write('// The mesh carries this path - a plain Detour query walks it in 55\n')
        f.write('// polygons using the very filter the core queries with - but the\n')
        f.write('// module\'s own chunked builder returns an incomplete route and the\n')
        f.write('// party ends up in the water beside the ship. These anchors are the\n')
        f.write('// corner points of that Detour corridor, so following them walks the\n')
        f.write('// same ramp a player walks. A live clear that does better replaces\n')
        f.write('// this file through the ordinary shortest-wins rule.\n')
        f.write('#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"\n')
        f.write('\n')
        f.write('void RegisterRecordedRoute36_%d()\n{\n' % entry)
        f.write('    DungeonClearRouteRegistry::Register(36, DUNGEON_DIFFICULTY_NORMAL, %d,\n        {\n' % entry)
        for x, y, z in pts:
            f.write('            { %.2ff, %.2ff, %.2ff },\n' % (x, y, z))
        f.write('        });\n}\n')
    print('%s: %d Anker, %dyd -> %s' % (name, len(pts), length, os.path.basename(rp)))
