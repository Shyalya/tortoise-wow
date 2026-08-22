/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearHazardsValue.h"

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Data/DcHazardRegistry.h"

namespace
{
    // Emitters only matter for points the party might actually stand on, and
    // every consumer (camp anchors, standoff rings, skirt legs) works within a
    // pull's reach of the bot. One sight distance is comfortably wider than the
    // largest registered keep-out radius plus a camp drag, and keeps the grid
    // visitor much cheaper than the 4x FarTargets sweep — this runs on every
    // bot, on every map with a row.
    float HazardRange() { return sPlayerbotAIConfig.sightDistance; }
}

DungeonClearHazardsValue::DungeonClearHazardsValue(PlayerbotAI* botAI)
    // 500ms mirrors FarTargets. The registered emitters are rooted or inert, so
    // even this is generous — but a cache that can go stale across a teleport
    // would be a silent hazard miss, and 500ms bounds that to one tick's worth.
    : NearestUnitsValue(botAI, DcKey::Hazards, HazardRange(), /*ignoreLos*/ true, 500)
{
}

void DungeonClearHazardsValue::FindUnits(std::list<Unit*>& targets)
{
    // AnyUnitInObjectRangeCheck, not AnyUnfriendly*: the corpse bombs sit on a
    // neutral faction and an unfriendly-only searcher never returns them.
    Acore::AnyUnitInObjectRangeCheck check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, check);
    Cell::VisitObjects(bot, searcher, range);
}

bool DungeonClearHazardsValue::AcceptUnit(Unit* unit)
{
    if (!unit || !unit->IsCreature())
        return false;

    return DcHazardRegistry::Find(unit->GetMapId(), unit->GetEntry()) != nullptr;
}
