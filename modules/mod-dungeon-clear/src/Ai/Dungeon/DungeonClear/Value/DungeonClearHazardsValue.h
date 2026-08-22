/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARHAZARDSVALUE_H
#define _PLAYERBOT_DUNGEONCLEARHAZARDSVALUE_H

#include "NearestUnitsValue.h"

class PlayerbotAI;
class Unit;

// Live hazard emitters (DcHazardRegistry rows) near the bot, cached at 500ms.
//
// Shaped on DungeonClearFarTargetsValue, but AcceptUnit is deliberately NOT
// AttackersValue::IsPossibleTarget. A hazard is dangerous precisely BECAUSE the
// party is not fighting it, and the registered emitters fail every "is this a
// valid attack target" predicate for different reasons: the Arcatraz Sentinel is
// rooted under a permanent feign-death aura, and the Defender/Warder Corpses
// carry UNIT_DYNFLAG_DEAD and a neutral faction. Reusing DcKey::FarTargets here
// would silently drop the corpse bombs entirely.
//
// Membership is therefore registry lookup only: entry is on the table for this
// map. Nothing about liveness, hostility or attackability enters into it.
class DungeonClearHazardsValue : public NearestUnitsValue
{
public:
    DungeonClearHazardsValue(PlayerbotAI* botAI);

protected:
    void FindUnits(std::list<Unit*>& targets) override;
    bool AcceptUnit(Unit* unit) override;
};

#endif
