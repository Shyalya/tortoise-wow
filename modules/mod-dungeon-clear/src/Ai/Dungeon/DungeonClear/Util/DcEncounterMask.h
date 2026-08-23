/*
 * mod-dungeon-clear - DcEncounterMask.h  (Tortoise port)
 *
 * The completed-encounter mask, module-owned. Upstream reads
 * Map::GetCompletedEncounterMask, fed by KillRewarder + DungeonEncounter.dbc.
 * This core has neither, and the InstanceData face grown for the port
 * honestly returns 0 - which silently meant NO boss ever counted as killed
 * (live: runs sat at 0/N with dead bosses on the floor and the router kept
 * aiming at corpses). The module tracks the bits itself: the
 * UNITHOOK_ON_UNIT_DEATH script sets the boss's encounterIndex bit for its
 * map instance, and every former GetCompletedEncounterMask() reader asks
 * here by Map*.
 *
 * Instance ids get recycled by the core; the registry stores the mapId next
 * to the mask and treats a mapId mismatch as a fresh instance. Entries whose
 * instance no longer runs are dropped by Sweep() on the module's world tick.
 */

#ifndef MOD_DC_ENCOUNTER_MASK_H
#define MOD_DC_ENCOUNTER_MASK_H

#include "Platform/Define.h"

class Map;

namespace DcEncounterMask
{
    // Set bit `encounterIndex` for the map's instance (no-op outside dungeons
    // or for indices >= 32).
    void OnBossKilled(Map const* map, uint32 encounterIndex);

    // The instance's completed mask; 0 for null/non-dungeon maps and unknown
    // instances.
    uint32 Get(Map const* map);

    // Drop entries whose instance the MapManager no longer runs. Internally
    // throttled - call freely from the module's world tick.
    void Sweep();
}

#endif
