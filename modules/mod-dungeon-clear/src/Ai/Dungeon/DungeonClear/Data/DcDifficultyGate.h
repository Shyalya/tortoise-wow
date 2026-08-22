/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCDIFFICULTYGATE_H
#define _PLAYERBOT_DCDIFFICULTYGATE_H

#include "DBCEnums.h"
#include "Define.h"

// Difficulty gate shared by the dungeon-events framework (DungeonEvent::gate)
// and the boss-roster patch table (BossRosterPatch::gate). 5-man dungeons run
// at DUNGEON_DIFFICULTY_NORMAL or DUNGEON_DIFFICULTY_HEROIC; a heroic-only
// boss (Sethekk's Anzu, Shattered Halls' Porung) or a heroic-only event gets
// HeroicOnly so it never surfaces on a normal run, and vice versa. Raid
// difficulties are out of scope — the clear engine drives 5-mans.
enum class DcDifficultyGate : uint8
{
    Any,         // both difficulties (the default — most content is shared)
    NormalOnly,
    HeroicOnly,
};

inline bool DcGateMatches(DcDifficultyGate gate, Difficulty difficulty)
{
    switch (gate)
    {
        case DcDifficultyGate::NormalOnly:
            return difficulty == DUNGEON_DIFFICULTY_NORMAL;
        case DcDifficultyGate::HeroicOnly:
            return difficulty == DUNGEON_DIFFICULTY_HEROIC;
        default:
            return true;
    }
}

#endif
