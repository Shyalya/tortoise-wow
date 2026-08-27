// meshprobe - fragt das fertige Navigationsnetz direkt, ohne Server.
//
// Warum: [DC-MESH] sagt nur "unerreichbar". Das beantwortet nicht, WO das
// Netz aufhoert - und ohne diese Antwort ist jede Bruecke geraten. Das
// Werkzeug laedt dieselben .mmtile-Dateien wie der Kern und beantwortet
// zwei Fragen: liegt ein Punkt auf dem Netz, und gibt es einen Weg von A
// nach B.
//
// Aufruf:
//   meshprobe <mmaps-dir> <mapId> point   x y z [x y z ...]
//   meshprobe <mmaps-dir> <mapId> path    x1 y1 z1 x2 y2 z2
//
// Koordinaten sind WoW-Koordinaten; die Umrechnung in Recast-Reihenfolge
// (y, z, x) passiert hier, genau wie im Kern.
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

struct MmapTileHeader
{
    unsigned int mmapMagic;
    unsigned int dtVersion;
    unsigned int mmapVersion;
    unsigned int size;
    unsigned int usesLiquids;
};

static dtNavMesh* g_mesh = nullptr;

static bool LoadMap(char const* dir, int mapId)
{
    char fname[512];
    snprintf(fname, sizeof(fname), "%s/%03i.mmap", dir, mapId);
    FILE* fp = fopen(fname, "rb");
    if (!fp) { printf("kein %s\n", fname); return false; }
    dtNavMeshParams params;
    if (fread(&params, sizeof(dtNavMeshParams), 1, fp) != 1) { fclose(fp); return false; }
    fclose(fp);

    g_mesh = dtAllocNavMesh();
    if (dtStatusFailed(g_mesh->init(&params))) { printf("init fehlgeschlagen\n"); return false; }

    int loaded = 0;
    // Dateiname ist %03i%02i%02i mit (map, y, x) - siehe MoveMap.cpp:142.
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
        {
            snprintf(fname, sizeof(fname), "%s/%03i%02i%02i.mmtile", dir, mapId, y, x);
            FILE* f = fopen(fname, "rb");
            if (!f) continue;
            MmapTileHeader h;
            if (fread(&h, sizeof(h), 1, f) != 1) { fclose(f); continue; }
            unsigned char* data = (unsigned char*)dtAlloc(h.size, DT_ALLOC_PERM);
            if (fread(data, h.size, 1, f) != 1) { dtFree(data); fclose(f); continue; }
            fclose(f);
            dtTileRef ref = 0;
            if (dtStatusFailed(g_mesh->addTile(data, h.size, DT_TILE_FREE_DATA, 0, &ref)))
                dtFree(data);
            else
                ++loaded;
        }
    printf("Karte %d: %d Kacheln geladen\n", mapId, loaded);
    return loaded > 0;
}

int main(int argc, char** argv)
{
    if (argc < 5) { printf("meshprobe <mmaps> <map> point|path <koordinaten...>\n"); return 1; }
    char const* dir = argv[1];
    int mapId = atoi(argv[2]);
    std::string mode = argv[3];
    if (!LoadMap(dir, mapId)) return 1;

    dtNavMeshQuery query;
    query.init(g_mesh, 4096);
    dtQueryFilter filter;
    // Der Kern fragt mit NAV_GROUND|NAV_WATER und schliesst NAV_STEEP_SLOPES
    // aus. Genau das laesst sich hier nachstellen: inc=/exc= auf der
    // Befehlszeile.
    unsigned short inc = 0xFFFF, exc = 0;
    for (int i = 4; i < argc; ++i)
    {
        if (!strncmp(argv[i], "inc=", 4)) inc = (unsigned short)strtoul(argv[i] + 4, nullptr, 0);
        if (!strncmp(argv[i], "exc=", 4)) exc = (unsigned short)strtoul(argv[i] + 4, nullptr, 0);
    }
    filter.setIncludeFlags(inc);
    filter.setExcludeFlags(exc);
    printf("Filter inc=0x%x exc=0x%x\n", inc, exc);

    auto toRecast = [](float x, float y, float z, float* out)
    { out[0] = y; out[1] = z; out[2] = x; };

    if (mode == "point")
    {
        // Grosszuegiger Suchkasten: uns interessiert, ob in der NAEHE Netz
        // liegt und auf welcher Hoehe - nicht, ob der Punkt exakt trifft.
        float ext[3] = { 5.0f, 60.0f, 5.0f };
        for (int i = 4; i + 2 < argc; i += 3)
        {
            if (strchr(argv[i], '='))   // inc=/exc=, keine Koordinate
                break;
            float wx = atof(argv[i]), wy = atof(argv[i + 1]), wz = atof(argv[i + 2]);
            float p[3]; toRecast(wx, wy, wz, p);
            dtPolyRef ref = 0; float nearest[3] = {0,0,0};
            query.findNearestPoly(p, ext, &filter, &ref, nearest);
            if (!ref)
                printf("(%.0f %.0f %.0f) KEIN NETZ im Umkreis\n", wx, wy, wz);
            else
                printf("(%.0f %.0f %.0f) Netz bei z=%.1f  dz=%+.1f  poly=%llu\n",
                       wx, wy, wz, nearest[1], nearest[1] - wz,
                       (unsigned long long)ref);
        }
    }
    else if (mode == "path")
    {
        if (argc < 10) { printf("path braucht 6 Zahlen\n"); return 1; }
        float ax = atof(argv[4]), ay = atof(argv[5]), az = atof(argv[6]);
        float bx = atof(argv[7]), by = atof(argv[8]), bz = atof(argv[9]);
        float a[3], b[3]; toRecast(ax, ay, az, a); toRecast(bx, by, bz, b);
        float ext[3] = { 5.0f, 60.0f, 5.0f };
        dtPolyRef ra = 0, rb = 0; float na[3], nb[3];
        query.findNearestPoly(a, ext, &filter, &ra, na);
        query.findNearestPoly(b, ext, &filter, &rb, nb);
        if (!ra || !rb) { printf("Start oder Ziel liegt nicht auf dem Netz (a=%llu b=%llu)\n",
                                 (unsigned long long)ra, (unsigned long long)rb); return 0; }
        dtPolyRef path[1024]; int n = 0;
        dtStatus st = query.findPath(ra, rb, na, nb, &filter, path, &n, 1024);
        bool complete = n > 0 && path[n - 1] == rb;
        printf("Weg: %d Polygone, Status %s, %s\n", n,
               dtStatusFailed(st) ? "FEHLER" : "ok",
               complete ? "ERREICHT" : "NUR TEILWEISE (Netz endet dazwischen)");
        if (n > 0)
        {
            // Wo endet der Teilweg? Das ist die Kante, die uns interessiert.
            float end[3];
            query.closestPointOnPoly(path[n - 1], nb, end, nullptr);
            printf("letzter erreichbarer Punkt: x=%.1f y=%.1f z=%.1f\n", end[2], end[0], end[1]);
        }
    }
    else if (mode == "route")
    {
        // Wie "path", gibt aber die Eckpunkte des Weges aus - fertig als
        // .route-Datei. Damit laesst sich ein Weg, den das Netz hergibt und
        // unser Wegsucher nicht findet, von aussen einsetzen: der Follower
        // laeuft Anker fuer Anker, ohne noch einmal zu suchen.
        if (argc < 10) { printf("route braucht 6 Zahlen\n"); return 1; }
        float ax = atof(argv[4]), ay = atof(argv[5]), az = atof(argv[6]);
        float bx = atof(argv[7]), by = atof(argv[8]), bz = atof(argv[9]);
        float a[3], b[3]; toRecast(ax, ay, az, a); toRecast(bx, by, bz, b);
        float ext[3] = { 5.0f, 60.0f, 5.0f };
        dtPolyRef ra = 0, rb = 0; float na[3], nb[3];
        query.findNearestPoly(a, ext, &filter, &ra, na);
        query.findNearestPoly(b, ext, &filter, &rb, nb);
        if (!ra || !rb) { printf("Start oder Ziel nicht auf dem Netz\n"); return 1; }
        dtPolyRef polys[2048]; int n = 0;
        query.findPath(ra, rb, na, nb, &filter, polys, &n, 2048);
        if (n <= 0 || polys[n - 1] != rb) { printf("kein vollstaendiger Weg\n"); return 1; }
        float pts[2048 * 3]; unsigned char flags[2048]; dtPolyRef refs[2048]; int np = 0;
        query.findStraightPath(na, nb, polys, n, pts, flags, refs, &np, 2048, 0);
        float len = 0.0f;
        for (int i = 1; i < np; ++i)
        {
            float dx = pts[i * 3 + 2] - pts[(i - 1) * 3 + 2];
            float dy = pts[i * 3 + 0] - pts[(i - 1) * 3 + 0];
            len += std::sqrt(dx * dx + dy * dy);
        }
        printf("ANKER %d LAENGE %d\n", np, (int)len);
        for (int i = 0; i < np; ++i)
            printf("%.2f %.2f %.2f\n", pts[i * 3 + 2], pts[i * 3 + 0], pts[i * 3 + 1]);
    }
    return 0;
}
