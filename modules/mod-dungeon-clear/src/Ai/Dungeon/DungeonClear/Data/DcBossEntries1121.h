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
// does not. Maps present in DC_BOSS_ORDER_1121 below get the authored order
// (and any door bosses the curated list lacks); every other map falls back to
// ascending-entry numbering. The fallback is NOT harmless in door dungeons:
// live, a Deadmines party routed to Mr. Smite first (lowest reachable index)
// straight past Rhahk'Zor's closed door and walked off the navmesh - add an
// order block when a dungeon misroutes.

inline constexpr uint32 DC_BOSS_ENTRIES_1121[] = {
639,646,647,1666,1696,1717,1853,2748,3654,3669,3670,3674,3872,3914,3927,3974,3975,4275,4278,4279,4421,4424,4543,4829,4831,4842,4854,4887,5709,5710,5712,5715,5719,5720,5721,5722,5775,6228,6229,6235,6487,6488,6910,7023,7206,7228,7267,7271,7291,7355,7356,7357,7358,7604,7800,8127,8443,8580,8983,9016,9017,9024,9030,9033,9156,9196,9218,9236,9237,9568,9816,9938,10184,10220,10264,10339,10363,10429,10430,10432,10433,10435,10437,10440,10504,10507,10508,10516,10558,10584,10596,10811,10812,10813,10901,10997,11143,11488,11489,11490,11492,11496,11502,11517,11518,11519,11520,11622,11982,12018,12056,12057,12098,12118,12119,12129,12201,12203,12236,12237,12258,12259,12264,13280,13282,13601,14321,14323,14325,14326,14327,14354,40068,61961,61963,2000092,63129,63130,63131,63132,63133,62037,62038,62056,62057,62067,62069,62070,62071,62072
};

// Per-dungeon encounter order, plus the door bosses the curated kith_boss
// list skipped (it carried tactics bosses; the router also needs the bosses
// whose death opens doors). Entries listed here count as bosses even when
// absent from DC_BOSS_ENTRIES_1121. `order` is 1-based; the mask bit is
// order-1, so keep every dungeon's orders inside 1..32.
struct DcBossOrderRow
{
    uint16 mapId;
    uint32 entry;
    uint8 order;
};

inline constexpr DcBossOrderRow DC_BOSS_ORDER_1121[] = {
    // The Deadmines (map 36): Rhahk'Zor -> Sneed's Shredder (Sneed rides it)
    // -> Gilnid -> Mr. Smite -> Captain Greenskin -> VanCleef -> Cookie.
    { 36,  644, 1 },  // Rhahk'Zor (opens the first door)
    { 36,  642, 2 },  // Sneed's Shredder (door)
    { 36, 1763, 3 },  // Gilnid (door)
    // Turtle's custom Deadmines wing (the alchemy lab), between the foundry
    // and the Iron Clad Door. Both carry the boss signature - fixed level and
    // several times the surrounding trash's health - while the Chemist (61959),
    // Mixologist (61960) and Manufactured Golem (61962) around them are lab
    // guards and stay trash.
    { 36, 61961, 4 },  // Jared Voss (-85,-526,54)
    { 36, 61963, 5 },  // Masterpiece Harvester (-67,-570,51)
    { 36,  646, 6 },  // Mr. Smite
    { 36,  647, 7 },  // Captain Greenskin
    { 36,  639, 8 },  // Edwin VanCleef
    { 36,  645, 9 },  // "Cookie"
};

#endif
