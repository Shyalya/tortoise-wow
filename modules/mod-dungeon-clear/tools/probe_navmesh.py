#!/usr/bin/env python3
"""Probe AzerothCore Detour mmtiles: dump navmesh vertices/polys near a WoW
point so we can see what Z-levels of mesh exist (e.g. a shelf and the floor a
hole drops to). Read-only; client-derived mmaps, never committed.

WoW<->Recast: recast = (wowY, wowZ, wowX); wow = (recast.z, recast.x, recast.y).
Tile layout: MmapTileHeader(56 bytes) then Detour navData (dtMeshHeader 100B,
then vertCount*3 floats, then polys...)."""
import struct, sys, glob, math, argparse

MMAP_HDR = 56
MESH_HDR = 100  # 15 ints + 3 + 6 + 1 floats

def parse_tile(path):
    with open(path, 'rb') as f:
        data = f.read()
    # dtMeshHeader starts at MMAP_HDR
    h = struct.unpack_from('<15i9f', data, MMAP_HDR)
    magic, version, tx, ty, layer, userId, polyCount, vertCount = h[:8]
    if magic != (ord('D') << 24 | ord('N') << 16 | ord('A') << 8 | ord('V')):
        return None, None
    off = MMAP_HDR + MESH_HDR
    verts = struct.unpack_from('<%df' % (vertCount * 3), data, off)
    pts = []
    for i in range(vertCount):
        rx, ry, rz = verts[3*i], verts[3*i+1], verts[3*i+2]
        # wow = (recast.z, recast.x, recast.y)
        pts.append((rz, rx, ry))  # wowX, wowY, wowZ
    # polys follow verts; dtPoly = 32 bytes
    poff = off + vertCount * 12
    polys = []
    for i in range(polyCount):
        base = poff + i * 32
        pv = struct.unpack_from('<6H', data, base + 4)       # verts[6]
        vc, areatype = struct.unpack_from('<BB', data, base + 30)
        ptype = areatype >> 6
        if ptype != 0:                  # skip off-mesh connections
            continue
        corners = [pts[pv[k]] for k in range(vc)]
        polys.append(corners)
    return pts, polys

def point_in_poly2d(x, y, corners):
    inside = False
    n = len(corners)
    j = n - 1
    for i in range(n):
        xi, yi = corners[i][0], corners[i][1]
        xj, yj = corners[j][0], corners[j][1]
        if ((yi > y) != (yj > y)) and \
           (x < (xj - xi) * (y - yi) / (yj - yi + 1e-12) + xi):
            inside = not inside
        j = i
    return inside

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mmaps', required=True)
    ap.add_argument('--map', type=int, required=True)
    ap.add_argument('--x', type=float, required=True)
    ap.add_argument('--y', type=float, required=True)
    ap.add_argument('--r', type=float, default=20.0, help='2D radius (yd)')
    ap.add_argument('--column', action='store_true',
                    help='report navmesh surfaces directly below (x,y)')
    a = ap.parse_args()

    files = sorted(glob.glob('%s/%03d*.mmtile' % (a.mmaps, a.map)))
    allpts = []
    allpolys = []
    for fp in files:
        p, polys = parse_tile(fp)
        if p:
            allpts.extend(p)
            allpolys.extend(polys)

    if a.column:
        hits = []
        for corners in allpolys:
            if point_in_poly2d(a.x, a.y, corners):
                z = sum(c[2] for c in corners) / len(corners)
                hits.append(z)
        hits.sort(reverse=True)
        print('navmesh surfaces in the column at (%.2f, %.2f): %d'
              % (a.x, a.y, len(hits)))
        for z in hits:
            print('  Z = %.2f' % z)
        return

    print('tiles=%d verts=%d' % (len(files), len(allpts)))

    near = [(x, y, z) for (x, y, z) in allpts
            if math.hypot(x - a.x, y - a.y) <= a.r]
    near.sort(key=lambda t: t[2])
    print('verts within %.1fyd of (%.2f, %.2f): %d' % (a.r, a.x, a.y, len(near)))
    if not near:
        return
    # Z histogram (1yd buckets)
    buckets = {}
    for (x, y, z) in near:
        b = round(z)
        buckets.setdefault(b, []).append((x, y, z))
    print('\nZ-level histogram (bucket -> count, nearest vert to query XY):')
    for b in sorted(buckets):
        grp = buckets[b]
        c = min(grp, key=lambda t: math.hypot(t[0]-a.x, t[1]-a.y))
        d = math.hypot(c[0]-a.x, c[1]-a.y)
        print('  Z~%-7d n=%-4d  nearest=(%.2f, %.2f, %.2f)  2Ddist=%.2f'
              % (b, len(grp), c[0], c[1], c[2], d))

if __name__ == '__main__':
    main()
