/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_BREADCRUMB_H
#define _DC_BREADCRUMB_H

#include <cmath>
#include <cstddef>
#include <vector>

#include "AiObjectContext.h"
#include "Log.h"
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
    // What counts as a break in the trail. The single 12yd distance this
    // replaces was an under-specified stand-in: it asked "did the tank move
    // far?" when the question is "did the tank move somewhere a follower
    // cannot WALK to?". Measured on Dragonmaw 2026-08-31 it fired 511 times in
    // 25 minutes - 473 of those breaks had under 10yd of vertical in them, i.e.
    // the tank simply ran 14-30yd of flat ground between two samples. Every one
    // of those wiped a healthy trail, so followers had nothing to inherit and
    // fell back to stock Follow(), which is exactly the stranding this trail
    // exists to prevent. Two conditions now, each naming what it catches:
    //
    //   kFloorStep  - a drop/step STEEPER than any walkable ramp. Slope-gated,
    //                 so a real ledge (mostly vertical) breaks the trail while
    //                 a steep ramp (mostly horizontal) stays contiguous.
    //   kRelocation - a genuine drag-back or teleport. Above anything a bot
    //                 covers between two samples at run speed, below the 100yd+
    //                 jumps the instance entry produces.
    constexpr float kFloorStep = 10.0f;
    constexpr float kFloorSlope = 0.75f;
    // One number, one place: the readers' walk-back guard IS this bound.
    constexpr float kRelocation = DungeonClearMath::TrailJumpGuard;
    // 128 crumbs (~512yd) was shorter than a Maraudon leg: followers 480yd
    // behind found "nearest crumb 100+yd away" 63 times in 90 min (2026-09-05)
    // because the tail of the trail had already been dropped. 384 crumbs
    // (~1.5km) costs 4.6 KB per tank; WalkTrailBack stays linear and cheap.
    constexpr size_t kMax = 384;       // history cap (~ kMax*kSpacing yd)
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
    float const gap3d = crumbs.back().GetExactDist(&cur);
    float const drop = std::fabs(crumbs.back().GetPositionZ() - cur.GetPositionZ());
    bool const floorChange = drop > kFloorStep && drop > d * kFloorSlope;
    bool const relocation = gap3d > kRelocation;
    if (floorChange || relocation)
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
        // MEASUREMENT (Dragonmaw 2026-08-31): a wiped trail is the suspected
        // reason followers strand - with no crumbs to inherit they fall back to
        // stock Follow(), which cannot path down the drop the tank just took.
        // Log both outcomes with the gap that caused them, so "the trail broke
        // at the ledge" stops being a story and becomes a count.
        float const dz = crumbs.back().GetPositionZ() - cur.GetPositionZ();
        if (j != DungeonClearMath::TrailRejoinNone)
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] breadcrumb trail REJOINED after a {}yd gap ({}yd of it "
                     "vertical) - kept {} of {} crumbs",
                     bot->GetName(), int(gap3d), int(dz), int(j + 1), int(crumbs.size()));
            crumbs.resize(j + 1);  // rejoin — drop everything ahead of crumb j
        }
        else
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] breadcrumb trail WIPED after a {}yd gap ({}yd of it "
                     "vertical) - {} crumbs lost, followers have nothing to inherit",
                     bot->GetName(), int(gap3d), int(dz), int(crumbs.size()));
            crumbs.clear();        // true teleport — restart the trail
        }
        crumbs.push_back(cur);
        return;
    }
    crumbs.push_back(cur);
    if (crumbs.size() > kMax)
        crumbs.erase(crumbs.begin());
}

#endif  // _DC_BREADCRUMB_H
