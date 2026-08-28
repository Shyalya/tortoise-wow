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
639,646,647,1666,1696,1717,1853,2748,3653,3654,3669,3670,3671,3673,3674,3886,3887,3914,3927,3974,3975,4274,4275,4278,4279,4421,4424,4543,4829,4831,4842,4854,4887,5709,5710,5712,5715,5719,5720,5721,5722,5775,6228,6229,6235,6487,6488,6910,7023,7206,7228,7267,7271,7291,7355,7356,7357,7358,7604,7800,8127,8443,8580,8983,9016,9017,9024,9030,9033,9156,9196,9218,9236,9237,9568,9816,9938,10184,10220,10264,10339,10363,10429,10430,10432,10433,10435,10437,10440,10504,10507,10508,10516,10558,10584,10596,10811,10812,10813,10901,10997,11143,11488,11489,11490,11492,11496,11502,11517,11518,11519,11520,11622,11982,12018,12056,12057,12098,12118,12119,12129,12201,12203,12236,12237,12258,12259,12264,13280,13282,13601,14321,14323,14325,14326,14327,14354,40068,61961,61963,61965,61968,61969,2000092,63129,63130,63131,63132,63133,62037,62038,62056,62057,62067,62069,62070,62071,62072
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
    // Shadowfang Keep (map 33). Eleven bosses, of which five were missing from
    // the credit list entirely - Razorclaw, Silverlaine, Fenrus, Sever and
    // Turtle's own Prelate Ironmane - so the roster read six.
    //
    // Order measured against the navmesh from the entrance (-228.2, 2111.4,
    // 76.9) with tools/meshprobe.cpp. Those numbers hold for the REGENERATED
    // mesh: with the old one the whole upper keep (Odo, Deathsworn Captain,
    // Fenrus, Nandos, Arugal) had no path from the entrance at all, nor from
    // Springvale, the highest point reachable below - the courtyard stairs were
    // missing from it. Map 33 now carries the same mmapSettings line Turtle
    // uses for Scarlet Monastery, which merges stair steps into walkable
    // surface, and the mesh was rebuilt with it.
    // Order given by the server owner, who knows the place; mesh distance from
    // the entrance only decided nothing here. Sever (14682) is dropped from the
    // roster entirely - the DB spawns it on map 33, but it is Scourge Invasion
    // furniture and not part of the dungeon.
    //
    // Steps 3 and 4 are interchangeable in practice: whether Silverlaine or
    // Ironmane comes first depends on whether the party cuts through the house
    // on the way to Razorclaw.
    { 33, 3914, 1 },   // Rethilgore            147yd
    { 33, 3886, 2 },   // Razorclaw the Butcher 259
    { 33, 3887, 3 },   // Baron Silverlaine     343
    { 33, 61969, 4 },  // Prelate Ironmane      443   (Turtle custom, up the stairs right)
    { 33, 4278, 5 },   // Commander Springvale  454
    { 33, 4279, 6 },   // Odo the Blindwatcher  601   (gateway to the upper keep)
    // Deathsworn Captain (3872) is out of the roster as well: pool 1601 with
    // chance 30, so he stands in about one instance in three. Waiting on a rare
    // makes two runs out of three impossible to finish, and a real group could
    // not clear him either when he is not there.
    { 33, 4274, 7 },   // Fenrus the Devourer   1039
    { 33, 3927, 8 },   // Wolf Master Nandos    1311
    { 33, 4275, 9 },   // Archmage Arugal       1380

    // Wailing Caverns (map 43). Without these rows the map got no order at
    // all and the indices fell out in build order - Verdan 0, so the party
    // went for him FIRST. He is the one boss with no navmesh path from the
    // entrance (measured with tools/meshprobe.cpp: no complete corridor from
    // (-158,132,-74), and the only start that has one is Lord Serpentis's
    // shelf, 69yd away). Verdan sits BEHIND the drop, so every run opened by
    // searching 1100-1600yd for a boss it could not reach until somebody
    // stumbled into him.
    //
    // The keys leave room for the event objectives, which carry their own:
    // the drop to Serpentis is 5 and the hole-drop/escort are 7 (see
    // WailingCavernsEvents.cpp), so Verdan takes 7 (index 6) and Mutanus 8.
    { 43, 3671, 1 },  // Lady Anacondra   (221yd from the entrance; 4 pool spawns, one alive)
    { 43, 3653, 2 },  // Kresh            (298yd)
    { 43, 3669, 3 },  // Lord Cobrahn     (897yd)
    { 43, 3670, 4 },  // Lord Pythas      (1193yd)
    { 43, 3674, 5 },  // Skum             (western water; 360yd on from Pythas)
    { 43, 61965, 6 }, // Vangros          (Turtle custom, 693yd on from SKUM - and no
                      //                   corridor at all from Pythas, which is why he
                      //                   comes after Skum and not before)
    { 43, 61968, 7 }, // Zandara Windhoof (Turtle custom, 1229yd)
    //  key 8 (index 7) is the drop-to-Serpentis objective — see
    //  WailingCavernsEvents.cpp. Everything below it is only reachable from
    //  the shelf that drop lands on.
    { 43, 3673, 9 },  // Lord Serpentis   (on the landing shelf)
    { 43, 5775, 10 }, // Verdan the Everliving (69yd off Serpentis's shelf, and
                      //                        unreachable from anywhere else)
    //  key 11 (index 10) is the hole-drop and the Naralex escort.
    { 43, 3654, 12 }, // Mutanus the Devourer (summoned at the end of the escort)

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
