/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_BREADCRUMB_H
#define _DC_BREADCRUMB_H

#include <cstddef>
#include <vector>

#include "AiObjectContext.h"
#include "Player.h"
#include "Position.h"

#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h"

// Drop a breadcrumb of the tank's current position onto the trail followers
// walk up (DungeonClearFollowTankAction's centered trail-follow) and the
// advanced pull walks back to place its camp. Called each forward-advance /
// objective-drive tick; samples only every kSpacing yards of real movement, and
// RESTARTS the trail on a kJump-sized gap (a pull drag-back or a teleport) so
// the stored trail is always spatially contiguous behind the tank —
// independent of the long-path follower cursor, which the drag resets.
//
// Hoisted here (was file-local to DcAdvanceAction.cpp) so the barrel / item-on-GO
// objective drive can record crumbs too: without them followers had no centered
// trail to inherit on the long Old Hillsbrad courtyard->prison-yard leg and fell
// back to wall-hugging Follow().
inline void DcRecordBreadcrumb(AiObjectContext* ctx, Player* bot)
{
    if (!ctx || !bot)
        return;
    constexpr float kSpacing = 4.0f;   // min real movement between samples
    constexpr float kJump = 12.0f;     // gap that means a drag/teleport -> reset
    constexpr size_t kMax = 128;       // history cap (~ kMax*kSpacing yd)
    std::vector<Position>& crumbs =
        ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get().breadcrumbs;
    Position const cur(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    if (crumbs.empty())
    {
        crumbs.push_back(cur);
        return;
    }
    float const d = crumbs.back().GetExactDist2d(&cur);
    if (d < kSpacing)
        return;
    // Discontinuity guard is 3D: a drop-down / ledge can move the tank only a
    // few yards in plan view but a long way vertically. A 2D-only guard treats
    // that as contiguous trail, so a camp later picked across the seam sits on
    // a different floor and the move to it straight-lines through the geometry
    // (the "under the map" symptom). 3D distance catches the vertical jump and
    // restarts the trail so consecutive crumbs are always a straight walk apart.
    if (crumbs.back().GetExactDist(&cur) > kJump)
    {
        // Discontinuity (a drag-back in combat, a drop-down). Wiping the whole
        // trail here starves the next pull's ComputeSafeCamp exactly when it
        // matters most — and the information is almost always still valid: the
        // camp itself sits on previously walked trail. So try to REJOIN: find
        // the latest crumb near where the bot stands now and truncate forward
        // of it, keeping the contiguous prefix. Only a true teleport (no crumb
        // within kRejoinRadius) restarts the trail. kRejoinRadius (6yd) sits
        // under kJump and above DC_PULL_CAMP_ARRIVE (5yd), so a tank standing
        // at camp rejoins the crumb the camp was lifted from. Contiguity holds:
        // the prefix was already pairwise-contiguous and cur is within
        // kRejoinRadius < kJump of the rejoin crumb.
        constexpr float kRejoinRadius = 6.0f;
        std::size_t const j =
            DungeonClearMath::FindTrailRejoin(crumbs, cur, kRejoinRadius);
        if (j != DungeonClearMath::TrailRejoinNone)
            crumbs.resize(j + 1);  // rejoin — drop everything ahead of crumb j
        else
            crumbs.clear();        // true teleport — restart the trail
        crumbs.push_back(cur);
        return;
    }
    crumbs.push_back(cur);
    if (crumbs.size() > kMax)
        crumbs.erase(crumbs.begin());
}

#endif  // _DC_BREADCRUMB_H
