/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCZONELINE_H
#define _PLAYERBOT_DCZONELINE_H

#include "Define.h"

// AreaTrigger is a typedef of AreaTriggerEntry on this core (compat
// shim); a struct forward-declaration collides with a typedef-name.
struct AreaTriggerEntry;
typedef AreaTriggerEntry AreaTrigger;
class Player;

// The instance ZONE LINE: the map-changing areatrigger volume at a dungeon's
// entrance (and the fall-catcher slabs around its rim), i.e. the one place in a
// dungeon where standing on the wrong yard of floor REMOVES a party member from
// the run.
//
// Why the pull planner has to know about it. An advanced pull drags the pack
// BACK onto already-cleared ground, and at the very first pull of a dungeon the
// only cleared ground behind the tank is the entrance it walked in through — so
// ComputeSafeCamp/ComputeTrailCamp happily plant the camp on, or beyond, the
// exit trigger. Nothing downstream catches it: the navmesh is blind to
// areatriggers, so IsNavReachable says yes, and the party is ordered to walk
// through the zone line to reach camp.
//
// What that costs depends on who is in the party. A pure bot party gets away
// with it — a headless bot sends no CMSG_AREATRIGGER, so the transfer never
// fires (the same asymmetry DcTestAreaTriggers::Arm exists to work around). A
// SELF-BOT is a real client: it reports the trigger, the server teleports it out
// of the instance, and the player is dumped outside mid-run. Hence the fix is a
// placement gate, not a movement one — a camp is never worth the risk, and the
// fallback (camp where the tank stands) is what a human tank does at a door
// anyway.
//
// Pure geometry, no map/nav/game state beyond the ObjectMgr stores, so both
// predicates are safe from any thread and cheap enough to sit beside
// CampBlockedByDoor / CampInHazard on every candidate.
namespace DcZoneLine
{
    // How much clear floor to demand between a camp anchor and the trigger
    // volume. A camp is not a point: bots fan out 1-2yd on their GUID-derived
    // camp slot, healers slide for LOS, and an AoE step-out moves a follower up
    // to ~8yd off the anchor. Every one of those yards has to still be inside
    // the instance, so the anchor buys the whole step-out width.
    inline constexpr float CampMargin = 8.0f;

    // Slop for a WALK across the volume. Much thinner than CampMargin on
    // purpose: passing near a zone line is harmless (only entering it teleports),
    // so this only absorbs the difference between the straight segment we test
    // and the navmesh path the bot actually walks. A camp-sized margin here
    // would veto every candidate whose approach merely skirts the entrance.
    inline constexpr float WalkMargin = 2.0f;

    // --- Pure volume geometry (unit-testable, no Player needed) -------------
    // Mirrors Player::IsInAreaTriggerRadius — sphere when `radius > 0`, else the
    // orientation-rotated box — with ONE deliberate difference: `margin` pads
    // the volume in XY only, never in Z.
    //
    // That asymmetry is load-bearing. A dungeon's fall-catcher triggers are
    // wall-like slabs whose TOP surface sits just under the walkable floor
    // (Shadowfang's "South Fall Target" tops out 2.6yd below the courtyard it
    // runs beside). Padding their height would swallow the floor above them and
    // veto camps across a third of the keep; padding XY only leaves them exactly
    // as thin as they really are while still keeping the party a respectful
    // distance from the edge they hang off.
    bool PointInVolume(AreaTrigger const& at, float x, float y, float z, float margin);

    // True when the straight segment (a)->(b) clips the padded volume. The test
    // for something the party will WALK ALONG: a camp can sit cleanly past a
    // zone line with both endpoints outside it while the leg to it goes straight
    // through — which is precisely the "camp behind the zone line" shape, since
    // the trigger sits in the doorway between the party and that ground.
    bool SegmentClipsVolume(AreaTrigger const& at, float ax, float ay, float az,
                            float bx, float by, float bz, float margin);

    // True when this map has any map-changing areatrigger at all. The early-out
    // every predicate below takes first; on a map with no rows a call costs one
    // hash lookup.
    bool MapHasZoneLines(uint32 mapId);

    // --- Live predicates ---------------------------------------------------
    // True when (x,y,z) sits inside a zone-line volume padded by CampMargin.
    // The test for a point the party will STAND on: a camp anchor, a per-bot
    // camp slot, a trail/scout hold point.
    bool PointIsOverTheLine(Player* bot, float x, float y, float z);

    // True when the straight segment from `bot` to (x,y,z) crosses a zone-line
    // volume padded by WalkMargin — the "camp is clear but the way to it is not"
    // half of the gate.
    bool LegCrossesTheLine(Player* bot, float x, float y, float z);

    // Both halves at once: the one call a placement site makes. True => this
    // candidate would put someone through the zone line, reject it.
    bool WouldCrossTheLine(Player* bot, float x, float y, float z);
}

#endif  // _PLAYERBOT_DCZONELINE_H
