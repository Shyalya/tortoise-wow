/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearGroundHazardsValue.h"

#include "CellImpl.h"
#include "DynamicObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Data/DcHazardRegistry.h"

namespace
{
    // Same reach as the creature hazards value: every consumer (camp anchors,
    // standoff rings, skirt legs, the vacate retreat) works within a pull's reach
    // of the bot, and one sight distance comfortably covers the widest registered
    // keep-out plus a camp drag.
    float GroundHazardRange() { return sPlayerbotAIConfig.sightDistance; }

    // Range-only check for the dynamic-object sweep. There is no stock
    // "any world object in range" predicate that takes a WorldObject*, and the
    // real filter (spell id -> registry row) is cheaper applied after the sweep
    // than inside the grid visitor.
    //
    // 2D-with-no-bounding-radius on purpose: a DynamicObject has no combat reach,
    // and the z separation that actually matters is the per-row zBand applied
    // later by the geometry.
    struct AnyDynamicObjectInRangeCheck
    {
        AnyDynamicObjectInRangeCheck(WorldObject const* source, float range)
            : _source(source), _range(range)
        {
        }

        bool operator()(WorldObject* obj) const
        {
            return obj && _source->IsWithinDist(obj, _range, /*is3D*/ false,
                                                /*incOwnRadius*/ false, /*incTargetRadius*/ false);
        }

        WorldObject const* _source;
        float _range;
    };
}

DungeonClearGroundHazardsValue::DungeonClearGroundHazardsValue(PlayerbotAI* botAI)
    // 500ms mirrors the creature hazards value. A pool never moves, but its
    // 20s lifetime means a longer cache would keep steering the party around one
    // that has already expired — and a cache that survived a teleport would be a
    // silent hazard miss in the other direction.
    : ObjectGuidListCalculatedValue(botAI, DcKey::GroundHazards, 500),
      range(GroundHazardRange())
{
}

std::list<ObjectGuid> DungeonClearGroundHazardsValue::Calculate()
{
    std::list<ObjectGuid> results;
    if (!bot)
        return results;

    // Cheap early-out: no rows for this map, no sweep. Keeps the extra grid
    // visitor off every dungeon that does not register a pool.
    if (!DcHazardRegistry::HasGroundHazards(bot->GetMapId()))
        return results;

    std::list<WorldObject*> found;
    AnyDynamicObjectInRangeCheck check(bot, range);
    Acore::WorldObjectListSearcher<AnyDynamicObjectInRangeCheck> searcher(
        bot, found, check, TYPEMASK_DYNAMICOBJECT);
    Cell::VisitObjects(bot, searcher, range);

    for (WorldObject* obj : found)
    {
        if (!obj || !obj->IsDynamicObject())
            continue;

        DynamicObject* dynObj = obj->ToDynObject();
        if (!dynObj)
            continue;

        // GetSpellId() is the CAST spell (17742), not the per-tick spell it
        // triggers — which is what DcGroundHazard rows are keyed on.
        if (!DcHazardRegistry::FindGround(bot->GetMapId(), dynObj->GetSpellId()))
            continue;

        results.push_back(dynObj->GetObjectGuid());
    }

    return results;
}
