/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearTrapHazardsValue.h"

#include "GameObject.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Data/DcHazardRegistry.h"

namespace
{
    // Same reach as the other two hazard values: every consumer (camp anchors,
    // standoff rings, skirt legs, the vacate retreat) works within a pull's reach
    // of the bot.
    float TrapHazardRange() { return sPlayerbotAIConfig.sightDistance; }
}

DungeonClearTrapHazardsValue::DungeonClearTrapHazardsValue(PlayerbotAI* botAI)
    // 500ms mirrors the other two hazard values. A trap never moves, but a Blaze
    // lives only 60s and new ones land every few seconds while the archers are
    // up, so a longer cache would both steer the party around expired fire and
    // miss fresh fire — wrong in both directions.
    : ObjectGuidListCalculatedValue(botAI, DcKey::TrapHazards, 500),
      range(TrapHazardRange())
{
}

std::list<ObjectGuid> DungeonClearTrapHazardsValue::Calculate()
{
    std::list<ObjectGuid> results;
    if (!bot)
        return results;

    // Cheap early-out: no rows for this map, no sweep. Keeps the extra grid
    // visitor off every dungeon that registers no trap.
    std::vector<uint32> const entries = DcHazardRegistry::TrapEntries(bot->GetMapId());
    if (entries.empty())
        return results;

    // Sweep BY ENTRY, not "every gameobject in range then filter": a dungeon
    // floor carries hundreds of doors, chairs and torches, and the core already
    // offers the multi-entry grid searcher this needs.
    std::list<GameObject*> found;
    bot->GetGameObjectListWithEntryInGrid(found, entries, range);

    for (GameObject* go : found)
    {
        if (!go || !go->IsInWorld())
            continue;

        // Re-check the registry on the resolved object rather than trusting the
        // entry sweep alone — same staleness guard the other two values' walkers
        // apply, and it keeps the (map, entry) key honest.
        if (!DcHazardRegistry::FindTrap(go->GetMapId(), go->GetEntry()))
            continue;

        results.push_back(go->GetObjectGuid());
    }

    return results;
}
