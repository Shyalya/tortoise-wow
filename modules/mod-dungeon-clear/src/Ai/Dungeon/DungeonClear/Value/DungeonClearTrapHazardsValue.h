/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARTRAPHAZARDSVALUE_H
#define _PLAYERBOT_DUNGEONCLEARTRAPHAZARDSVALUE_H

#include "Value.h"

class PlayerbotAI;

// Live ground TRAPS (DcTrapHazard rows) near the bot, cached at 500ms — the
// GameObject twin of DungeonClearHazardsValue / DungeonClearGroundHazardsValue.
//
// It is a third value rather than a filter inside either of the other two for
// the same hard reason the pools needed their own: a GAMEOBJECT_TYPE_TRAP is
// neither a Unit nor a DynamicObject. Its guid resolves through
// ObjectAccessor::GetGameObject alone; fed to GetUnit or GetDynamicObject it
// comes back nullptr, and the `continue` that follows reads exactly like "no
// hazard here" — a silent miss, not an error.
//
// Membership is registry lookup only: GameObject::GetEntry() has a DcTrapHazard
// row for this map. No state test beyond that. In particular the trap's loot
// state is NOT consulted: a Blaze cycles GO_READY -> GO_ACTIVATED -> GO_READY
// every two seconds for its whole 60s life, and "it happens to be mid-cast right
// now" is not the difference between safe and unsafe ground.
//
// LOS is deliberately ignored (same as the other two): fire behind a corpse or a
// doorframe still burns the bot standing in it.
class DungeonClearTrapHazardsValue : public ObjectGuidListCalculatedValue
{
public:
    DungeonClearTrapHazardsValue(PlayerbotAI* botAI);

    // Tortoise port: the guid-list base here hands out std::list, not a
    // vector. Same contents, and every consumer only iterates.
    std::list<ObjectGuid> Calculate() override;

private:
    float range;
};

#endif
