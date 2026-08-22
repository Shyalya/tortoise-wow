/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_DOOR_INDEX_H
#define _DC_DOOR_INDEX_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ObjectGuid.h"

class Map;

// Per-map-instance cache of the door-type GameObjects currently spawned.
//
// The door predicates (DcEngageGeometry::ClosedDoorBetween / ClosedDoorNear /
// DistAlongPathToClosedDoor and the blocking-door value) each linearly walked the
// ENTIRE Map::GetGameObjectBySpawnIdStore() — hundreds of chests/traps/doodads —
// on every call, several times per tick on the common scouting state, just to
// pick out the dozen-or-two doors. This caches only "which GOs are doors"; the
// open/shut state (GOState) is still read FRESH per call by the consumer, so door
// freshness is identical to the full-store walk — the past staleness bugs were in
// cached door *verdicts*, never in a cached list of which GOs are doors.
class DcDoorIndex
{
public:
    // Spawn GUIDs of the door-type GameObjects on `map`. Rebuilt when the cached
    // snapshot is older than kRebuildMs OR the GO store size changed since it was
    // built (grids streaming in/out add/remove doors); otherwise the cache is
    // returned. The returned reference is valid until the next Get() rebuild for
    // the same map on the calling (map) thread.
    static std::vector<ObjectGuid> const& Get(Map* map);

    // Pure rebuild decision, factored out for unit testing. Rebuild when never
    // built (builtMs == 0), when the store size differs from the snapshot, or
    // when the snapshot has aged past kRebuildMs (ms-wraparound safe).
    static bool NeedsRebuild(std::uint32_t builtMs, std::uint32_t now,
                             std::size_t storeSizeAtBuild, std::size_t storeSizeNow);

    static constexpr std::uint32_t kRebuildMs = 2000;
};

#endif  // _DC_DOOR_INDEX_H
