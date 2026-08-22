/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARFARTARGETSVALUE_H
#define _PLAYERBOT_DUNGEONCLEARFARTARGETSVALUE_H

#include "NearestUnitsValue.h"

class PlayerbotAI;
class Unit;

// Wider variant of `possible targets` for the corridor blocking-trash scan.
// Default `possible targets` is capped at sightDistance (~100yd) and is
// computed every tick on every bot — too narrow to see packs at the far
// end of a long dungeon corridor, and too expensive to widen globally.
//
// This value uses a ~400yd range, ignores LOS at scan time (corridor scan
// applies its own LOS check after geometry filtering), and only polls at
// 500ms. AcceptUnit skips the PvP attack-chance logic from PossibleTargetsValue
// (irrelevant for dungeon NPCs) and just verifies the unit is alive, hostile,
// and a legitimate attack target.
class DungeonClearFarTargetsValue : public NearestUnitsValue
{
public:
    DungeonClearFarTargetsValue(PlayerbotAI* botAI);

protected:
    void FindUnits(std::list<Unit*>& targets) override;
    bool AcceptUnit(Unit* unit) override;
};

#endif
