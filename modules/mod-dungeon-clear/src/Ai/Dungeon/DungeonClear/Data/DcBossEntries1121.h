#ifndef DC_BOSS_ENTRIES_1121_H
#define DC_BOSS_ENTRIES_1121_H

#include "Define.h"

// Which creature entries count as dungeon bosses on a 1.12 core.
//
// Upstream builds its boss index from DungeonEncounter.dbc. That DBC arrived
// with Wrath; a 1.12 client has no encounter data at all, and creature rank
// cannot stand in for it - Deadmines has VanCleef and his trash both at rank 1.
//
// This list is the curated one from the Kith project's kith_boss table (133
// bosses across the classic instances, Molten Core and Onyxia included),
// baked in as data rather than read from a table so the module works on a
// realm that never imported it. Everything else the index needs - map, spawn
// coordinates, name - joins in from the spawn and template data at load.
//
// Ordering: DungeonEncounter carried an explicit per-dungeon order; this list
// does not. The index numbers bosses by entry within a map, which can order a
// route differently than the dungeon intends - the router still paths to
// whatever is alive and reachable, so the cost is a detour, not a stall.

inline constexpr uint32 DC_BOSS_ENTRIES_1121[] = {
639,646,647,1666,1696,1717,1853,2748,3654,3669,3670,3674,3872,3914,3927,3974,3975,4275,4278,4279,4421,4424,4543,4829,4831,4842,4854,4887,5709,5710,5712,5715,5719,5720,5721,5722,5775,6228,6229,6235,6487,6488,6910,7023,7206,7228,7267,7271,7291,7355,7356,7357,7358,7604,7800,8127,8443,8580,8983,9016,9017,9024,9030,9033,9156,9196,9218,9236,9237,9568,9816,9938,10184,10220,10264,10339,10363,10429,10430,10432,10433,10435,10437,10440,10504,10507,10508,10516,10558,10584,10596,10811,10812,10813,10901,10997,11143,11488,11489,11490,11492,11496,11502,11517,11518,11520,11622,11982,12018,12056,12057,12098,12118,12119,12129,12201,12203,12236,12237,12258,12259,12264,13280,13282,13601,14321,14323,14325,14326,14327,14354,40068,2000092
};

#endif
