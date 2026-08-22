/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEAR_CREATURESPAWNENTRY_H
#define _PLAYERBOT_DUNGEONCLEAR_CREATURESPAWNENTRY_H

#include "Define.h"

// Upstream this resolved CreatureData::id against CreatureData::id1 by
// overload ranking, because two mod-playerbots branches named the member
// differently. This core has a third shape: a spawn carries an ARRAY of
// possible entries (creature_id[0..3], one is picked at spawn time), so
// "the entry of this spawn" is only well-defined as the first candidate.
// Callers use it to match a spawn against a boss list; a multi-id spawn is
// never a boss on this core's data, so reading slot 0 is exact in practice.
// The branch dispatch is gone with the branches.
namespace DungeonClear
{
    template <typename T>
    uint32 SpawnEntry(T const& data)
    {
        return data.creature_id[0];
    }
}

#endif
