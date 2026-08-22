/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARGROUNDHAZARDSVALUE_H
#define _PLAYERBOT_DUNGEONCLEARGROUNDHAZARDSVALUE_H

#include "Value.h"

class PlayerbotAI;

// Live ground pools (DcGroundHazard rows) near the bot, cached at 500ms — the
// DynamicObject twin of DungeonClearHazardsValue.
//
// It is a separate value, not an extra filter inside DungeonClearHazardsValue,
// for one hard reason: a persistent area aura is NOT a unit. It has no entry, no
// faction, no health; NearestUnitsValue's whole contract (FindUnits / AcceptUnit,
// then IsWithinLOSInMap on a Unit*) does not apply to it, and the guid it yields
// resolves through ObjectAccessor::GetDynamicObject rather than GetUnit. Mixing
// the two guid kinds into DcKey::Hazards would make every existing consumer
// silently drop the pools — GetUnit on a DynamicObject guid returns nullptr, and
// the `continue` that follows reads exactly like "no hazard here".
//
// Membership is registry lookup only: DynamicObject::GetSpellId() has a
// DcGroundHazard row for this map. No liveness or hostility test — the pool is
// dangerous precisely because there is nothing to fight.
//
// LOS is deliberately ignored (same as the creature value): a pool behind a
// corpse or a doorframe still burns the bot standing in it.
class DungeonClearGroundHazardsValue : public ObjectGuidListCalculatedValue
{
public:
    DungeonClearGroundHazardsValue(PlayerbotAI* botAI);

    // Tortoise port: the guid-list base here hands out std::list, not a
    // vector. Same contents, and every consumer only iterates.
    std::list<ObjectGuid> Calculate() override;

private:
    float range;
};

#endif
