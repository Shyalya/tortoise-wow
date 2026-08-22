/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_SEALEDENCOUNTERREGISTRY_H
#define _PLAYERBOT_SEALEDENCOUNTERREGISTRY_H

#include <cstdint>

#include "Define.h"

// Static registry of SEALED ENCOUNTERS: bosses whose room locks the moment the
// encounter starts, so anyone still outside is shut out of the fight for its whole
// duration.
//
// This is a real, scripted mechanic and not a guess. An instance script can register
// a door as DOOR_TYPE_ROOM against an encounter, and InstanceScript::UpdateDoorState
// then holds it `open &= (state != IN_PROGRESS)` — i.e. closed for exactly as long as
// the fight lasts. Magisters' Terrace does this for Selin Fireheart:
//
//   doorData[] = { GO_SELIN_DOOR (187979),           DATA_SELIN_FIREHEART, DOOR_TYPE_PASSAGE },
//                 { GO_SELIN_ENCOUNTER_DOOR (188065), DATA_SELIN_FIREHEART, DOOR_TYPE_ROOM    }
//
// 188065 is the Assembly Chamber Door, hanging at (215.1, 0.4) — the only way into
// Selin's room. Engage him and it shuts behind the tank. A follower that was still
// walking up the corridor never gets in: it stands at the door for the entire fight
// while the tank solos, which is what a run of this looked like from the outside.
//
// The ordinary readiness gate cannot catch this, and it is worth being precise about
// why rather than just tightening a number. DcPartyState::GetSpreadGate measures
// PartyMaxSpread (25yd) and, in pull mode between maneuvers, measures it against the
// CAMP rather than the tank — with a backstop of maxSpread + max(PullSetback,
// PullMaxDrag) = 60yd of permitted tank gap. Selin's camp is 71.6yd from him, so a
// tank that has walked to the doorway is ~45yd from the camp: comfortably inside every
// tolerance, with the whole party legitimately "set" back at the camp. Every gate
// passes and the door still shuts on four people.
//
// So a sealed encounter needs two things the generic path does not provide:
//
//   MUSTER   Nobody may be outside the room when the engage fires. Not "within N
//            yards of the tank" — that is a proxy, and a bad one here, because the
//            tank crosses the threshold before it engages, so a follower 10yd behind
//            it is 5yd on the WRONG SIDE of the door. The test has to be the volume.
//   CLUMP    A tighter, TANK-anchored spread gate on the final approach, so the party
//            is already at the tank's heels when it crosses instead of leapfrogging up
//            the corridor 25yd at a time. This is what makes the muster satisfiable
//            in a second or two rather than being a long wait inside aggro range.
//
// Both are scoped to the last stretch before the boss (`approachRadius`) so nothing
// about the rest of the run changes.
struct SealedEncounterRow
{
    uint32 mapId{0};
    uint32 bossEntry{0};

    // The sealed volume, axis-aligned, matching the door's own geometry: inside is
    // "will be locked in", outside is "will be locked out".
    float minX{0.0f}, maxX{0.0f};
    float minY{0.0f}, maxY{0.0f};

    // How close to the boss the two gates start applying. Sized to cover the final
    // approach and no more — outside it the run behaves exactly as it always has.
    float approachRadius{0.0f};

    // The CLUMP radius: tank-anchored max spread while inside approachRadius.
    // Achievable by construction — follow-tank trails at min(followDistance, 6yd) —
    // which matters, because a radius the followers cannot reach would hold the tank
    // at the door until the muster timeout instead of gathering anybody.
    float musterSpread{0.0f};
};

// How long the muster may hold the engage before it fires anyway, in ms.
//
// Bounded for the usual reason: a member that cannot path into the room (stuck on
// geometry, mid-rez, feared out of it) must not be able to hold the run open forever.
// On expiry the engage proceeds and whoever is outside stays outside — worse than
// mustering, still better than a dead run. Generous, because the cost of waiting is a
// few seconds and the cost of firing early is a member locked out of the whole fight.
inline constexpr uint32 DC_SEALED_MUSTER_TIMEOUT_MS = 20000;

class SealedEncounterRegistry
{
public:
    // The row for this boss on this map, or nullptr. Cheap linear scan of a tiny
    // table; nullptr is the overwhelmingly common answer and costs one compare.
    static SealedEncounterRow const* Find(uint32 mapId, uint32 bossEntry);

    // Is (x,y) inside the row's sealed volume — i.e. on the locked-IN side?
    // Pure, so it is unit-testable without a world.
    static bool InSealedRoom(SealedEncounterRow const& row, float x, float y);

    // Are the two gates live — is a bot at (x,y,z) close enough to the boss at
    // (bx,by,bz) for this row's muster/clump to apply? Pure, same reason.
    static bool InApproachRange(SealedEncounterRow const& row, float x, float y, float z,
                                float bx, float by, float bz);
};

#endif  // _PLAYERBOT_SEALEDENCOUNTERREGISTRY_H
