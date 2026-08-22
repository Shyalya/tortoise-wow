/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSOCIALQUARANTINEREGISTRY_H
#define _PLAYERBOT_DCSOCIALQUARANTINEREGISTRY_H

#include <cstdint>
#include <vector>

#include "Define.h"

// Static registry of SOCIAL QUARANTINE ZONES: packs that must be taken OUT of the
// social-aggro graph while a named boss is the run's objective, because no pull
// style, stand spot or camp can keep them out of the fight.
//
// This is the fourth member of the positional-override family and the only one
// that changes the MOBS rather than the party. FightInPlaceRegistry forbids a
// pull; BossPullbackRegistry mandates one; ScriptedPullRegistry authors a
// sequence of them. All three try to win the room with geometry. This one is for
// the rooms where geometry has already lost.
//
// WHAT IT DOES, exactly: every live member of a zone is held at REACT_DEFENSIVE
// while the zone is in force, and put back to REACT_AGGRESSIVE when it is not.
// Two stock predicates key on that one flag, and they are precisely the two ways
// a bystander pack joins a fight it was not pulled into:
//
//   * Creature::CanAssistTo() returns false at its FIRST line for anything that is
//     not REACT_AGGRESSIVE. That is the gate every call for help runs through —
//     Creature::CallAssistance (the T=0 call each formation member issues from its
//     own spawn via Unit::Attack, and the re-call it issues every
//     CreatureFamilyAssistancePeriod thereafter), AssistDelayEvent::Execute, and
//     Creature::CallForHelp. A quarantined pack cannot be RECRUITED.
//   * CreatureAI::MoveInLineOfSight requires REACT_AGGRESSIVE before it will
//     AttackStart. A quarantined pack does not notice a tank walking past it, and
//     does not notice a bot that a boss's untankable add chased into its bubble.
//
// WHAT IT DOES NOT DO, and this is why REACT_DEFENSIVE rather than REACT_PASSIVE:
// CreatureGroup::MemberEngagingTarget does NOT test react state, so a quarantined
// pack that is attacked directly still fights back AS A FORMATION, exactly as it
// would have. Quarantine removes CROSS-pack recruitment and PROXIMITY aggro; it
// never makes a pack harmless or unpullable. Walk up and hit one and the whole
// formation answers — which is what lets the ordinary route-trash machinery clear
// a quarantined pack later without knowing anything about this registry.
//
// WHY IT IS A BIG HAMMER AND SHOULD STAY RARE. It reaches into stock creature
// behaviour and turns part of it off. That is a real cost: the room stops being
// the room Blizzard shipped, and anything downstream that reasons about "which
// packs are awake" is now reasoning about a modified world. A zone is only
// justified where the alternative has been tried and MEASURED to be impossible:
//
//   THE MAGISTERS' TERRACE ROTUNDA is the case it was built for. Five formations
//   stand in one open circle with 12-23yd between neighbouring members, against a
//   flat CreatureFamilyAssistanceRadius that adds both combat reaches on top. Two
//   of those pairs are INSIDE that allowance at spawn — east/north-east by 1.05yd
//   and centre/north-east by 0.26 — and the call that binds them is issued at T=0
//   from each mob's own spawn position, before anything has moved. There is no
//   stand spot, no opener and no ordering that can separate them; four sessions of
//   re-measuring the geometry established that, and the run data says the same
//   thing in deaths (tp-20260807-203840-1: every single trash wipe in that room is
//   an UNPLANNED pull of 9-14 elites where the plan asked for 4-5).
//
// The zones below are therefore data, not policy: each one names a formation that
// a plan has proven it cannot avoid waking, and the boss whose approach the plan
// is running. Outside that boss's window the zone does not exist and the room is
// stock.
struct DcQuarantineZone
{
    uint32 mapId{0};
    // The zone is in force only while this boss is the run's next objective — the
    // same gate ScriptedPullStage::bossEntry uses, and for the same reason: it is
    // what retires the zone. Once the boss is dead the next objective is a
    // different one, every member is put back to REACT_AGGRESSIVE, and the pack is
    // ordinary route trash again.
    uint32 bossEntry{0};
    char const* name{nullptr};

    // The pack, as a cylinder: everything in `entries` within `radius` (2D) and
    // `zBand` (vertical half-band) of (x,y,z). Same shape and the same authoring
    // discipline as ScriptedPullStage's pack volume — size it to hold its own
    // formation's spawns and to exclude every neighbour, and check it against
    // `acore_world.creature` rather than eyeballing it.
    float x{0.0f}, y{0.0f}, z{0.0f};
    float radius{0.0f};
    float zBand{0.0f};

    // Only these creature entries are quarantined. NEVER optional here, unlike the
    // scripted-pull volume's filter: this list is also what keeps the zone off
    // props, critters and anything a script owns. A zone with an empty filter is
    // rejected at load (see the gtests).
    std::vector<uint32> entries;
};

class DcSocialQuarantineRegistry
{
public:
    // Every zone in force on `mapId` for `bossEntry`. Pure (no game state), so it
    // is unit-testable on its own. Linear scan; the table is tiny.
    static std::vector<DcQuarantineZone const*> Zones(uint32 mapId, uint32 bossEntry);

    // True iff `mapId` has any zone at all. Cheap early-out for the per-tick
    // caller so a map with no rows pays one bool and nothing else.
    static bool HasRows(uint32 mapId);

    // Every zone on the map regardless of boss — what a RELEASE has to walk, since
    // a run can stop (dc off, wipe, logout) with any boss still pending.
    static std::vector<DcQuarantineZone const*> AllZones(uint32 mapId);

    // The whole table, for the registry-integrity gtests.
    static std::vector<DcQuarantineZone> const& AllRows();

    // True iff (x,y,z) is inside the zone's cylinder.
    static bool InZone(DcQuarantineZone const& z, float x, float y, float zz);

    // True iff `entry` is one of the zone's members.
    static bool IsZoneEntry(DcQuarantineZone const& z, uint32 entry);
};

#endif  // _PLAYERBOT_DCSOCIALQUARANTINEREGISTRY_H
