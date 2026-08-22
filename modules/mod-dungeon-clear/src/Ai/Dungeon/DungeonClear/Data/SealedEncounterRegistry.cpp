/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "SealedEncounterRegistry.h"

#include <cmath>

namespace
{
    // --- Magisters' Terrace (585) — Selin Fireheart -----------------------------
    //
    // The volume is Selin's room, and it is deliberately the SAME box as this room's
    // FightInPlaceRegistry zone: [216,260] x [-45,45]. Both are derived from the same
    // two facts — Selin's own CanAIAttack plane (`who->GetPositionX() > 216.0f`) and
    // the Assembly Chamber Door hanging at X=215.1 — so agreement is not a
    // coincidence to be maintained by hand. It is asserted in
    // t/TestSealedEncounter.cpp rather than shared as a literal, because the two
    // registries answer different questions ("may the pull drag out of here" vs
    // "will the door lock me out") and a future room could easily need one and not
    // the other.
    //
    // approachRadius 45yd from the boss. Selin spawns at (242.07, 0.3), so 45yd
    // reaches back to X~197 — the staging chamber in front of the doorway, which is
    // where the party needs to start closing up. It does NOT reach the scripted-pull
    // camp at (170.46, 0.57), 71.6yd out, so the guard-pack stages run under the
    // ordinary gates exactly as before and only the final walk-in is affected.
    //
    // musterSpread 10yd. Follow-tank trails at min(followDistance, 6yd), so the party
    // sits inside this by construction while moving and the clump costs nothing in the
    // healthy case; it only bites on a genuine straggler. Tighter would fight
    // follow-tank's own spacing and turn every approach into a stutter.
    // --- Azjol-Nerub (601) — Anub'arak -----------------------------------------
    //
    // instance_azjol_nerub registers THREE DOOR_TYPE_ROOM doors on DATA_ANUBARAK
    // (192396 / 192397 / 192398, all at x 550-552, y 252-256), and
    // boss_anub_arak::JustEngagedWith schedules EVENT_CLOSE_DOORS at 5s, whose
    // only body is `BossAI::_JustEngagedWith()` — the SetBossState(IN_PROGRESS)
    // that shuts them. Five seconds after the pull the arena is sealed, and a
    // straggler still in the north corridor is out for the whole fight.
    //
    // The volume is the arena itself, read off the live 601 navmesh: one flat
    // floor at z 224.07-224.29 filling a rough circle x 528-572, y 236-276, fed
    // by a single corridor from the north (x 544-560, climbing to z ~230 by
    // y 320) and leaving by a second to the south. [526,574] x [234,278] is that
    // floor with 2yd of slack and nothing else — it agrees with the encounter's
    // own BossBoundaryData, a CircleBoundary at (550.6, 253.6) r 32.
    //
    // approachRadius 45yd from Anub'arak's spawn (551.0, 248.3, 224.0) reaches
    // back to y ~293, i.e. up into the mouth of the north corridor, which is
    // where the party needs to be closing up. It does NOT reach the Anub'ar
    // Prime Guards at y 341, so that pull runs under the ordinary gates.
    //
    // musterSpread 10yd, the same number and the same reasoning as Selin's.
    SealedEncounterRow const kRows[] =
    {
        // mapId  boss   minX    maxX    minY    maxY   approach  muster
        {   585, 24723, 216.0f, 260.0f, -45.0f, 45.0f,    45.0f,  10.0f },
        {   601, 29120, 526.0f, 574.0f, 234.0f, 278.0f,   45.0f,  10.0f },
    };
}

SealedEncounterRow const* SealedEncounterRegistry::Find(uint32 mapId, uint32 bossEntry)
{
    for (SealedEncounterRow const& r : kRows)
        if (r.mapId == mapId && r.bossEntry == bossEntry)
            return &r;
    return nullptr;
}

bool SealedEncounterRegistry::InSealedRoom(SealedEncounterRow const& row, float x, float y)
{
    return x >= row.minX && x <= row.maxX && y >= row.minY && y <= row.maxY;
}

bool SealedEncounterRegistry::InApproachRange(SealedEncounterRow const& row,
                                             float x, float y, float z,
                                             float bx, float by, float bz)
{
    // 3D, so a party passing on another floor of a multi-level instance cannot arm
    // the gates from below or above the boss.
    float const dx = x - bx;
    float const dy = y - by;
    float const dz = z - bz;
    return (dx * dx + dy * dy + dz * dz) <= (row.approachRadius * row.approachRadius);
}
