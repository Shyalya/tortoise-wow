/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARPULLTARGETVALUE_H
#define _PLAYERBOT_DUNGEONCLEARPULLTARGETVALUE_H

#include "ObjectGuid.h"
#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;

// Cached, STICKY identity of the trash pack the pull pipeline is aimed at.
//
// The pull pipeline (the Dynamic governor, the pull trigger, the pull action's
// Idle/Advancing branches and the advance action's camp-trail gate) used to each
// re-derive "the pack we are pulling" via DcTargeting::FindPullTarget — a full
// corridor scan plus a map-wide closed-door collection — 5+ times per
// out-of-combat tick. Expensive, and UNSTABLE: two near-equidistant packs
// alternate as "closest blocker" while the tank glides, churning the
// Leeroy/Advanced verdict and bouncing the party between camp-hold and
// follow-tank; one null read (door-veto flicker, long-path cache mid-rebuild)
// dropped a standing verdict outright.
//
// This value runs the scan at most once per interval and makes the answer
// sticky: while the governor's per-pack latch (DcPullContext::decisionTarget)
// still resolves to a valid pull target (DcTargeting::IsStickyPullTargetValid —
// alive, hostile, not the abort target, within the scan look-ahead plus slack,
// level-reachable, no closed door between), it IS the answer and no scan runs.
// A sticky target is what makes the verdict latch actually latch.
//
// Caches only the GUID (never a Unit*) following the DungeonClearLiveBossValue
// pattern: consumers resolve it through DcTargeting::GetPullTarget, which
// re-resolves via ObjectAccessor every call (positions stay live, no pointer can
// dangle across the interval) and forces a recompute when the cached GUID went
// stale within the interval (pack died), so a commit never aims at a corpse.
//
// 250ms: half the far-targets poll (500ms), so a newly visible pack is picked
// up within one far-targets refresh, while the corridor scan drops from 5-7 per
// tick to at most 4 per second.
class DungeonClearPullTargetValue : public CalculatedValue<ObjectGuid>
{
public:
    DungeonClearPullTargetValue(PlayerbotAI* botAI)
        : CalculatedValue<ObjectGuid>(botAI, DcKey::PullTarget, 250)
    {
    }

protected:
    ObjectGuid Calculate() override;
};

#endif
