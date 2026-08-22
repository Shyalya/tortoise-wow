#ifndef DC_COMPAT_INSTANCESCRIPT_H
#define DC_COMPAT_INSTANCESCRIPT_H

// Instance data IS the instance script on this core. The AzerothCore faces the
// module asks for (GetBossState, GetCompletedEncounterMask, GetPersistentData,
// ProcessEvent, GetTeamIdInInstance) sit on InstanceData itself as virtuals
// with honest defaults - see Maps/InstanceData.h for what each default means.
// An alias, not a subclass: Map::GetInstanceData() hands out whatever script
// the instance actually runs, and a subclass here would never be that type.
#include "Maps/InstanceData.h"

using InstanceScript = InstanceData;

enum EncounterState : uint8
{
    NOT_STARTED = 0,
    IN_PROGRESS = 1,
    FAIL        = 2,
    DONE        = 3,
    SPECIAL     = 4
};

// The instanced-map downcast, AzerothCore spelling.
using InstanceMap = DungeonMap;

#endif
