/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_ROUTESWEEPREGISTRY_H
#define _PLAYERBOT_ROUTESWEEPREGISTRY_H

#include "Define.h"

// Static allow-list of maps that run the EN-ROUTE PACK SWEEP: dungeons whose
// halls are narrow enough that a pack beside the route cannot be walked around,
// only pulled.
//
// Two behaviours key on this (both via DcEngageGeometry::EnRouteSweepApplies):
//   * DcTargeting::FindEnRouteAggroPack — target the pack whose aggro sphere the
//     ROUTE enters first, rather than the nearest mob in the corridor band.
//   * DcPullPlanner's Advanced-verdict force — when the target stands inside a
//     neighbour's aggro there is no way to fight it in place without that
//     neighbour joining, so drag it back to a camp instead.
//
// WHY A MAP LIST AND NOT A DIFFICULTY. Both behaviours are general — the failure
// shape (a corridor flanked by rooms whose aggro covers it) exists in most
// classic dungeons, and the first cut of this shipped for every normal-difficulty
// map on that reasoning. The second one is what pulled it back: forcing Advanced
// reshapes how every fight in a dungeon happens, running the full
// Forming/Advancing/Returning FSM on packs that used to be face-pulled. That is
// correct where the geometry demands it and pure wall-clock where it does not,
// and only one dungeon's worth of evidence exists so far. So the list starts at
// the map the evidence came from and widens per dungeon, each on its own run
// data. A row is cheap; an untested global is not.
//
// Deliberately NOT a config key — the config is already bloated, and this is a
// per-dungeon fact rather than an operator preference. Widening it is a one-line
// table edit here.
//
// Heroic is excluded separately and unconditionally (see EnRouteSweepApplies):
// there the same aggro spheres already bend the WALK (PullEnRouteAvoid, on by
// default), heroic rooms are wide enough for that detour to exist, and the
// heroic pull profile is tuned against measured baselines.
class RouteSweepRegistry
{
public:
    // True when `mapId` is on the list. Pure (no game state) so it is unit
    // testable on its own. Linear scan; the table is tiny.
    static bool SweepsRoute(uint32 mapId);
};

#endif  // _PLAYERBOT_ROUTESWEEPREGISTRY_H
