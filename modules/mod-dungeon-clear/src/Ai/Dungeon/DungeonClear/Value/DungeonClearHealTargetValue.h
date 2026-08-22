/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARHEALTARGETVALUE_H
#define _PLAYERBOT_DUNGEONCLEARHEALTARGETVALUE_H

#include "ObjectGuid.h"
#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;

// The party member a healer should reposition toward — the most-hurt member,
// chosen WITHOUT any line-of-sight filter and biased toward the elected leader
// tank (DungeonClear.HealRepositionTankBias).
//
// This exists because the stock `party member to heal` value LOS-filters its
// candidates (PartyMemberToHeal::Check requires IsWithinLOSInMap): the instant
// the tank is dragged out of the healer's sight it vanishes from that value, so
// every heal action AND the stock `reach party member to heal` reposition lose
// it, and the healer concludes "nobody I can see needs healing" and stands while
// the tank dies. This value keeps the out-of-LOS target visible so the
// heal-reposition trigger/action can walk the healer back into line of sight,
// after which the stock heal stack re-acquires the tank on its own.
//
// Only members below DungeonClear.HealRepositionHpFloor count (the "needs
// healing" gate); returns empty when nobody does. Caches the GUID only (never a
// Unit*), resolved live at use; 200ms keeps the most-hurt pick fresh (~5/sec)
// without re-iterating the group every tick.
class DungeonClearHealTargetValue : public CalculatedValue<ObjectGuid>
{
public:
    DungeonClearHealTargetValue(PlayerbotAI* botAI)
        : CalculatedValue<ObjectGuid>(botAI, DcKey::HealTarget, 200)
    {
    }

protected:
    ObjectGuid Calculate() override;
};

#endif
