/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearFarTargetsValue.h"

#include "AttackersValue.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/Data/DcNeverTargetRegistry.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

namespace
{
    // 4× the default sight distance — broad enough to catch packs at the
    // next pull point in long dungeon corridors (Nexus straight runs, HoL
    // hallway, OK upper ring) while still bounded enough that the grid
    // visitor isn't crossing the whole map.
    float FarRange() { return sPlayerbotAIConfig.sightDistance * 4.0f; }
}

DungeonClearFarTargetsValue::DungeonClearFarTargetsValue(PlayerbotAI* botAI)
    // checkInterval = 500ms. Long enough that the wider grid scan doesn't
    // run every Update tick; short enough that newly-aggroed packs appear
    // in the value within ~one fast turn-and-cast cycle.
    : NearestUnitsValue(botAI, DcKey::FarTargets, FarRange(), /*ignoreLos*/ true, 500)
{
}

void DungeonClearFarTargetsValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, range);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(bot, targets, check);
    Cell::VisitObjects(bot, searcher, range);
}

bool DungeonClearFarTargetsValue::AcceptUnit(Unit* unit)
{
    // Scripted never-dies mobs are not clear targets at all — see
    // DcNeverTargetRegistry. This is the widest of the clear's candidate sets
    // (the corridor scan, the en-route sweep, the room pre-clear and every
    // ClearRadius read it), so dropping them here covers most of the pipeline in
    // one place; the few scans that build their own candidate list repeat the
    // check next to their own IsPossibleTarget gate.
    if (unit && DcNeverTargetRegistry::IsNeverTarget(bot->GetMapId(), unit->GetEntry()))
        return false;

    // PvE dungeons only — skip the PvP attack-chance machinery in
    // PossibleTargetsValue and apply just the core "is this a valid
    // attack target" predicates that AttackersValue maintains.
    return AttackersValue::IsPossibleTarget(unit, bot, range);
}
