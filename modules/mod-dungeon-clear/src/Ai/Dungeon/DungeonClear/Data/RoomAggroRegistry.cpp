/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "RoomAggroRegistry.h"

#include <algorithm>

namespace
{
    // Tier-1 MVP scope (deployment-files/docs/mod-dungeon-clear_room-aggro-bosses
    // _plan.md). Radii are read straight from the script that force-pulls the
    // room, verified against the live source: SM Cathedral CATHEDRAL_PULL_RANGE
    // 80, Pandemonius ROOM_PULL_RANGE 70, Blackheart/Grand-Champions
    // CallForHelp(100), Dagran CallForHelp(VISIBLE_RANGE==166), and the SmartAI
    // CALL_FOR_HELP radii for Jammal'an (90), Pusillin (50), Krik'thir (40).
    //
    // memberEntries is left EMPTY (any hostile in radius) on purpose: the plan
    // endorses this over-approximation as the safe side (CallForHelp only wakes
    // assist-capable, non-evading friendlies, so clearing every hostile in the
    // radius is a strict superset), and it avoids hard-coding member entry IDs
    // we cannot verify per realm. The boss itself and any other encounter boss
    // are excluded at the use site (DcTargeting::IsDungeonBossEntry), and the
    // boss-aggro-sphere exclusion (IsRoomTrash) keeps the gate from livelocking
    // on a mob glued to the boss. A precise whitelist can be added later as a
    // pure optimisation without changing behaviour.
    RoomAggroBoss const kRoomAggroBosses[] =
    {
        // SM Cathedral (189): Mograine -> Whitemane. The grid AttackStart fires
        // on Mograine's engage; both encounter entries are flagged so the room
        // gate holds whichever the boss index selects.
        { 189, 3976, 80.0f, {} },   // Highlord Mograine
        { 189, 3977, 80.0f, {} },   // Inquisitor Whitemane

        // Scholomance (289): the linked pair Marduk Blackpool / Vectus. They do
        // not pre-aggro but are wired together in SmartAI (each "On Aggro -> Set
        // Data 3" and "On Data Set 3 -> Attack Start"), so pulling EITHER pulls
        // BOTH. They stand ~18yd apart in a chamber of ~32 Scholomance Students;
        // an AoE that wakes one wakes the pair while the room is still up. The DC
        // roster collapses the two into ONE boss anchored on Vectus (see
        // BossRosterRegistry, map 289), so 10432 is the live tracked boss (room
        // centre, nearest the close trash) and 10433 (Marduk, off the roster) is
        // listed here ONLY so the room-trash value's partner check excludes it
        // from being chased as a faction-15 "student". Radius 55 covers the
        // student room; the RoomClearTimeout valve handles far stragglers.
        //
        // CRITICAL: the whole room — students AND the two bosses — stands NEUTRAL
        // (yellow), not hostile, until struck. An EXPLICIT memberEntries whitelist
        // is what makes this work: (1) it scopes "the room" to the students alone,
        // so the unrelated HOSTILE packs that happen to sit within 55yd (Diseased
        // Ghouls to the east, Plagued Hatchlings to the north, Risen Adepts) are
        // NOT counted as room trash — without it those hostiles were the only
        // things the value could see, and as they cleared the count flicked to 0
        // and the boss was pulled with the students still up; and (2) a non-empty
        // whitelist signals DungeonClearRoomTrashValue to clear these entries even
        // though they are neutral (the hostile gate is bypassed for explicit
        // members only — see that file). Marduk is whitelisted identically so the
        // partner row behaves the same if it is ever the tracked boss.
        { 289, 10432, 55.0f, { 10475 } }, // Scholomance — Vectus (tracked boss)
        { 289, 10433, 55.0f, { 10475 } }, // Scholomance — Marduk Blackpool (partner)

        { 555, 18667, 100.0f, {} }, // Shadow Labyrinth — Blackheart the Inciter
        { 109, 5710,   90.0f, {} }, // Sunken Temple — Jammal'an the Prophet
        // Mana-Tombs — Pandemonius. boss_pandemonius::PullRoom() gathers the three
        // room adds within ROOM_PULL_RANGE(70) but force-pulls ONLY those with
        // ROOM_EXIT(-145) < Y < ROOM_ENTERANCE(-50): the Scavengers/Crypt
        // Raiders/Sorcerers BEHIND him (toward Tavarok, Y <= -145) never join the
        // fight. Mirror that exactly — entry whitelist + the Y corridor — or the
        // room-clear fixates on the unreachable behind-the-boss adds, orbits him,
        // and burns the full RoomClearTimeout before giving up (live: kept=2..7
        // flickering on 18309/18311/18313, far=108+, never 0).
        { 557, 18341, 70.0f, { 18309, 18311, 18313 }, true, -145.0f, -50.0f },
        { 601, 28684,  40.0f, {} }, // Azjol-Nerub — Krik'thir the Gatewatcher
        { 230, 9019,  166.0f, {} }, // Blackrock Depths — Emperor Dagran Thaurissan
        { 429, 14354,  50.0f, {} }, // Dire Maul East — Pusillin

        // Deadmines (36) — Gilnid, the goblin foundry.
        //
        // THE RADIUS IS NOT READ FROM A SCRIPT HERE, because there is no script to
        // read: Gilnid's whole SmartAI is a Molten Metal cast, an out-of-combat
        // yell, and a door open on death. He force-pulls nothing. This row rests on
        // the SCHOLOMANCE precedent above — "does not pre-aggro, but the room and
        // the boss cannot be separated by ordinary means" — and 60yd is the
        // measured spawn envelope of the foundry, not a script constant.
        //
        // What makes the room undecomposable: 19 ELITES at level 18, against a
        // level-18 party, standing 7.1-57.6yd from Gilnid. At a same-level elite's
        // ~20yd aggro radius, 17 of the 19 form ONE connected chain — single-linkage
        // clustering finds no pack boundary anywhere in the room, so waking any of
        // them can cascade through the lot. The dynamic pull governor cannot see
        // this: it sizes a pack with 6yd spread / 10yd assist windows, so on a room
        // whose mobs sit 15-20yd apart it reports "estimated 2 aggro" and returns
        // LEEROY every time (424 LEEROY vs 4 ADVANCED across tp-20260815-171416-1).
        // The party then walks in, collects the room, and Gilnid — who is simply the
        // next roster target — joins on top of it.
        //
        // Live (tp-20260815-171416-1): 5 of 20 runs wiped "on Gilnid" with the
        // status line alternating fighting_boss / fighting_trash every 1-2 seconds
        // (tr-...-10: Gilnid, Craftsman, Engineer, Remote-Controlled Golem, Craftsman
        // in fourteen seconds). Four of those five logged EVERY death as ON BOSS
        // with no trash on the sheet at all — Gilnid lands the killing blows, so the
        // record reads like a boss-difficulty problem when it is a room problem.
        //
        // 60 sweeps all 19 (50 would leave the three western-most at 52.0/55.3/57.6).
        // It also takes in the adjacent mine trash — 2 Defias Taskmaster, 7 Strip
        // Miner, 1 Overseer, 1 Miner — which costs nothing in practice: the clear
        // approaches the foundry from the west THROUGH that tunnel, so those are
        // already dead when the gate is evaluated, and RoomClearTimeout is the valve
        // for any straggler that is not.
        //
        // No skirtRadius / pullOutRadius override. The computed boss sphere already
        // does the right thing at both ends: the two Craftsmen at 7.1 and 9.2yd fall
        // INSIDE it and are excluded from room trash, so they come with the boss as
        // a normal 1-boss + 2-adds pull, and everything past it is ordinary
        // clearable room trash.
        { 36, 1763, 60.0f, {} }, // Deadmines — Gilnid (goblin foundry)

        // NOTE: Tendris Warpwood (11489) is deliberately NOT a room-aggro boss.
        // The radius-around-the-boss model is the wrong tool for him: live runs
        // showed it clearing the Eldreth ghosts standing behind him (which don't
        // assist the pull) while the mobs that actually matter — the Warpwood
        // treants in the entrance room ~200yd south, on a higher floor — sit far
        // outside any sane boss radius. That room is cleared instead by an
        // anchored ClearRadius objective co-located with crystal generator 1
        // (DireMaulEvents 429/11 / BossRosterRegistry OBJ(8)), ordered first.

        // Trial of the Champion (650) — Grand Champions, CallForHelp(100) on
        // unmount. Both faction rosters flagged; the other live champions are
        // themselves encounter bosses, so they fall out via the boss exclusion
        // and only genuine arena trash (if any) counts as the room.
        { 650, 34701, 100.0f, {} }, // Colosos
        { 650, 34702, 100.0f, {} }, // Jaelyne Evensong / Ambrose Boltspark
        { 650, 34703, 100.0f, {} }, // Lana Stouthammer / Mokra
        { 650, 34705, 100.0f, {} }, // Marshal Jacob Alerius
        { 650, 35569, 100.0f, {} }, // Eressea Dawnsinger
        { 650, 35570, 100.0f, {} }, // Zul'tore
        { 650, 35571, 100.0f, {} }, // Runok Wildmane
        { 650, 35572, 100.0f, {} }, // Mokra the Skullcrusher
        { 650, 35617, 100.0f, {} }, // Visceri

        // NOTE: The Mechanar (554) Mechano-Lord Capacitus had a room-aggro entry
        // here to pre-clear his SE Driller pack, back when the clear reached him
        // LAST on floor 1 (up the corridor from the dead Iron-Hand), which put that
        // pack right on the pull approach. The boss order now runs Gyro-Kill ->
        // Capacitus -> Iron-Hand, so the tank approaches the pit from the NW and the
        // SE pack falls on the post-Capacitus walk down to Iron-Hand — plain trash-
        // clear handles it once the boss is dead. Entry removed (see MechanarEvents).

        // The Mechanar (554) Nethermancer Sepethrea (floor 2). Her chamber holds
        // THREE elite trash groups the party must clear before the pull, and all
        // three should be dragged back WEST toward the entrance (the elevator
        // landing / Nethermancer door at ~x268) — NOT fought in place, because she
        // summons roaming Raging Flames and the fight wants the open floor. From the
        // world-DB spawns + a navmesh probe (TestMechanarElevatorProbe,
        // MechanarSepethreaRoomProbe):
        //   * Pack A  — 2 Astromage + 2 Centurion, ~(273,-23), 60-67yd from her.
        //   * Robots  — 2 Tempest-Forge Destroyers (one stationary, one roaming),
        //               ~(291,29) & (294,-15), 39-43yd out.
        //   * Pack B  — 2 Centurion + 2 Astromage, ~(309,13), only 17-19yd out —
        //               right on her ~18yd aggro line.
        // radius 70 covers the farthest (Pack A at 67yd); the whole set reads hostile
        // so no whitelist is needed. The catch is Pack B: it sits INSIDE her ~28yd
        // exclusion sphere, so the ordinary room-clear would treat it as "comes with
        // the boss" and leave it up. pullOutRadius 14 shrinks the room-trash exclusion
        // to 14yd (below Pack B's 17yd), KEEPING Pack B as clearable room trash, and
        // — because a non-zero pullOutRadius also forces advanced pull-to-camp — the
        // tank drags Pack B out from ~35yd west instead of meleeing it in place. See
        // RoomAggroBoss::pullOutRadius for the coupling that makes this safe.
        //
        // skirtRadius 40 WIDENS the fight standoff (her raw ~28yd sphere is too tight):
        // Pack B sits only 17yd out, so a camp cleared to the generic ~25yd from her
        // still landed the kill on top of her aggro/CallForHelp and woke her while the
        // party was still on the trash. The wider skirt drags Pack B's camp out toward
        // the ~x268 entrance landing before the fight, clear of her and of the open
        // floor her Raging Flames roam. It does NOT reclassify Pack B (membership uses
        // pullOutRadius 14, not the skirt).
        { 554, 19221, 70.0f, {}, false, 0.0f, 0.0f, /*pullOutRadius*/ 14.0f,
          /*skirtRadius*/ 40.0f },
    };
}

RoomAggroBoss const* RoomAggroRegistry::Find(uint32 mapId, uint32 bossEntry)
{
    for (RoomAggroBoss const& b : kRoomAggroBosses)
        if (b.mapId == mapId && b.bossEntry == bossEntry)
            return &b;
    return nullptr;
}

float RoomAggroRegistry::SkirtOverride(uint32 mapId, uint32 bossEntry)
{
    RoomAggroBoss const* b = Find(mapId, bossEntry);
    return b ? b->skirtRadius : 0.0f;
}

bool RoomAggroRegistry::IsMemberEntry(RoomAggroBoss const& boss, uint32 entry)
{
    if (boss.memberEntries.empty())
        return true;
    return std::find(boss.memberEntries.begin(), boss.memberEntries.end(), entry) !=
           boss.memberEntries.end();
}

bool RoomAggroRegistry::InRoomBand(RoomAggroBoss const& boss, float worldY)
{
    if (!boss.hasYBand)
        return true;
    return worldY > boss.minY && worldY < boss.maxY;
}

bool RoomAggroRegistry::IsRoomTrash(RoomAggroBoss const& boss, uint32 entry,
                                    float distToBoss, float bossSafeRadius)
{
    if (!IsMemberEntry(boss, entry))
        return false;
    if (distToBoss > boss.radius)
        return false;
    if (distToBoss <= bossSafeRadius)
        return false;
    return true;
}
