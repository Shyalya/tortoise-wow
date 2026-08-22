/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_FIGHTINPLACEREGISTRY_H
#define _PLAYERBOT_FIGHTINPLACEREGISTRY_H

#include "Define.h"

// Static registry of "fight-in-place" rooms: axis-aligned world regions where the
// advanced pull-to-camp maneuver is FORBIDDEN, so the tank walks in and engages the
// pack where it stands instead of dragging it back to an antechamber camp.
//
// Why this exists: a few bosses gate their OWN aggro on the party being physically
// INSIDE their room. Selin Fireheart (Magisters' Terrace, 585) is the canonical
// case — his script overrides CanAIAttack to `who->GetPositionX() > 216.0f`, i.e.
// he only ever attacks targets inside his room. His room-guards (Wretched Skulker /
// Bruiser / Husk, 24688/24689/24690) spawn INSIDE that room at X ~222-232. When the
// dynamic/advanced pull drags those guards back to a camp in the antechamber (X~197),
// the whole fight ends up BELOW the X=216 line: Selin aggros via the guards but
// CanAIAttack rejects every target there, so he stands passive and the run deadlocks
// in a one-sided combat flag until he is force-killed. Fighting the guards in place
// (X>222) instead keeps the party inside the gate, so Selin engages normally.
//
// The fix is deliberately positional (mirror the boss's own CanAIAttack plane) rather
// than entry-based: any mob that lives in the room is unsafe to pull out, and a plain
// region test needs no per-mob bookkeeping. The zone is sized off the live spawn data
// (see FightInPlaceRegistry.cpp) to cover the room while excluding the antechamber and
// the instance's other encounters.
struct FightInPlaceZone
{
    uint32 mapId{0};
    float  minX{0.0f};
    float  maxX{0.0f};
    float  minY{0.0f};
    float  maxY{0.0f};
};

class FightInPlaceRegistry
{
public:
    // True when (mapId, x, y) falls inside a registered fight-in-place room — the
    // signal for the pull pipeline to stand down and let the walk-in engage own the
    // pack. Pure (no game state) so it is unit-testable on its own. Linear scan; the
    // table is tiny.
    static bool IsNoPullZone(uint32 mapId, float x, float y);
};

#endif  // _PLAYERBOT_FIGHTINPLACEREGISTRY_H
