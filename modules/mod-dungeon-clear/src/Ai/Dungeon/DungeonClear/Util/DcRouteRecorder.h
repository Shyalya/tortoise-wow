/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCROUTERECORDER_H
#define _PLAYERBOT_DCROUTERECORDER_H

#include "Common.h"

#include <string>
#include <vector>

class Player;
class Map;

// Passive route recorder.
//
// Every successful clear walks a usable path to each boss and, until now, threw
// it away: Advance recomputes the route from the navmesh on every TTL and keeps
// nothing. Meanwhile DungeonClearRouteRegistry — the hand-authored anchor table
// that Advance PREFERS over the long-range router (snap-only, no A*) — shipped
// exactly one route in the whole repo.
//
// This closes that loop. While a dc run is driving, the leader's position is
// sampled cheaply; when a boss dies, the samples since the previous boss are
// thinned to ~15yd anchors and appended to a per-map source file under
// modules/mod-dungeon-clear/routes/. Those files are ordinary C++ appenders in
// the same shape as the authored ones, so a recorded route is repo content:
// commit it and every later run — here and on any other server running the
// module — walks the proven path instead of re-deriving it.
//
// Deliberately passive: recording never influences the run it observes. A route
// only becomes live once its generated file is compiled in.
namespace DcRouteRecorder
{
    // Sample the leader's position for the run it drives. Cheap (a distance
    // check plus a push_back at most once per ~4yd); called from the module's
    // world-tick reaper for the current run leader.
    void Sample(Player* leader);

    // A boss died on `map`: close the leg, thin it, and write/refresh the
    // per-map route file. `bossEntry`/`bossName` name the leg's destination.
    void OnBossKilled(Map* map, uint32 bossEntry, std::string const& bossName);

    // Drop everything recorded for an instance (run ended / instance gone).
    void Forget(uint32 instanceId);

    // Where the generated appenders live, relative to the server binary's
    // working directory. Configurable so a packaged server can point it at its
    // own writable location; the default is the in-tree module folder.
    std::string OutputDir();
}

#endif  // _PLAYERBOT_DCROUTERECORDER_H
