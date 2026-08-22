/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcNeverTargetRegistry.h"

namespace
{
    // ---- the table ------------------------------------------------------
    //
    // The Nexus (576) — Crystalline Frayer (26793), 44 spawns filling the
    // south-west garden the party crosses to reach Ormorok the Tree-Shaper.
    // `npc_crystalline_frayer` (instance_nexus.cpp) makes it unkillable until
    // Ormorok is dead, and then kills every one of them itself:
    //
    //     void JustEngagedWith(Unit*) override
    //     {
    //         _allowDeath = instance->GetBossState(DATA_ORMOROK_EVENT) == DONE;
    //     }
    //     void DamageTaken(Unit*, uint32& damage, ...) override
    //     {
    //         if (damage >= me->GetHealth() && !_allowDeath)
    //         {
    //             damage = 0;            // <-- the killing blow is discarded
    //             EnterSeedPod();
    //         }
    //     }
    //
    // EnterSeedPod parks it for NINETY SECONDS — REACT_PASSIVE, threat cleared,
    // NOT_SELECTABLE | IMMUNE_TO_PC | IMMUNE_TO_NPC, scale 0.6, and an Aura of
    // Regeneration (57056) ticking it back up — and LeaveSeedPod then returns it
    // to full health, REACT_AGGRESSIVE and roaming. Ormorok's death runs
    // `instance_nexus::KillAllFrayers()`, which strips those flags off every
    // frayer and `Unit::Kill`s it outright.
    //
    // So the clear's view of a frayer is binary and needs no instance-data read:
    // while one is ALIVE it cannot be killed, and the moment it can be killed it
    // is already dead. There is no window in which fighting one is progress.
    //
    // The dormant half of that cycle is already invisible to the clear —
    // IsPossibleTarget rejects NOT_SELECTABLE and IMMUNE_TO_PC — which is exactly
    // why filtering only the seed pod would not have fixed anything: the bots
    // wedge on the AWAKE frayer, whose only observable difference from ordinary
    // trash is that its health bar refills every 90 seconds. Hence a flat row
    // rather than an aura test.
    DcNeverTargetRow const kRows[] =
    {
        { 576, 26793 },  // The Nexus — Crystalline Frayer (seed pod; unkillable until Ormorok dies)
    };
}

bool DcNeverTargetRegistry::IsNeverTarget(uint32 mapId, uint32 entry)
{
    for (DcNeverTargetRow const& r : kRows)
        if (r.mapId == mapId && r.entry == entry)
            return true;
    return false;
}
