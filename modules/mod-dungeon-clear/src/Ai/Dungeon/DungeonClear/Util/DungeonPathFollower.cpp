/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonPathFollower.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "Player.h"

namespace
{
    bool IsJumpSegment(PathSegment const& seg)
    {
        return seg.jumpDown || seg.jumpGap;
    }

    bool IsLastPointOfSegment(PathSegment const& seg, uint32 pointIdx)
    {
        return !seg.polyline.empty() && pointIdx + 1 == seg.polyline.size();
    }

    // Returns the polyline point at (segIdx, pointIdx) or nullopt if out
    // of range. Skips empty segments by walking forward.
    std::optional<G3D::Vector3> PointAt(ChunkedPathfinder::Result const& path, uint32 segIdx, uint32 pointIdx)
    {
        if (segIdx >= path.segments.size())
            return std::nullopt;
        std::vector<G3D::Vector3> const& poly = path.segments[segIdx].polyline;
        if (pointIdx >= poly.size())
            return std::nullopt;
        return poly[pointIdx];
    }

    // Returns the polyline point that comes BEFORE (segIdx, pointIdx),
    // crossing segment boundaries as needed. Used by IsOffPath to anchor
    // the previous end of the current line segment.
    std::optional<G3D::Vector3> PrevPoint(ChunkedPathfinder::Result const& path, uint32 segIdx, uint32 pointIdx)
    {
        if (pointIdx > 0)
            return PointAt(path, segIdx, pointIdx - 1);
        // pointIdx == 0; walk back to the previous non-empty segment's last point.
        for (uint32 i = segIdx; i > 0; --i)
        {
            std::vector<G3D::Vector3> const& prevPoly = path.segments[i - 1].polyline;
            if (!prevPoly.empty())
                return prevPoly.back();
        }
        return std::nullopt;
    }

    // Returns the polyline point that comes AFTER (segIdx, pointIdx),
    // crossing segment boundaries as needed.
    std::optional<G3D::Vector3> NextPoint(ChunkedPathfinder::Result const& path, uint32 segIdx, uint32 pointIdx)
    {
        std::optional<G3D::Vector3> p = PointAt(path, segIdx, pointIdx + 1);
        if (p.has_value())
            return p;
        // No further point in this segment — walk forward to the next non-empty segment's first point.
        for (uint32 i = segIdx + 1; i < path.segments.size(); ++i)
        {
            std::vector<G3D::Vector3> const& poly = path.segments[i].polyline;
            if (!poly.empty())
                return poly.front();
        }
        return std::nullopt;
    }

    // Squared 2D distance from (px, py) to the line segment a→b. Used so
    // we can compare without taking square roots in the hot path.
    float Dist2ToSegment2DSq(float px, float py, G3D::Vector3 const& a, G3D::Vector3 const& b)
    {
        float const ex = b.x - a.x;
        float const ey = b.y - a.y;
        float const len2 = ex * ex + ey * ey;
        if (len2 <= 1e-6f)
        {
            float const dx = px - a.x;
            float const dy = py - a.y;
            return dx * dx + dy * dy;
        }
        float t = ((px - a.x) * ex + (py - a.y) * ey) / len2;
        t = std::max(0.0f, std::min(1.0f, t));
        float const cx = a.x + t * ex;
        float const cy = a.y + t * ey;
        float const dx = px - cx;
        float const dy = py - cy;
        return dx * dx + dy * dy;
    }

    // Advances (segIdx, pointIdx) by one polyline point, skipping over
    // any empty segments. Returns false when there is nothing left.
    bool AdvanceCursor(ChunkedPathfinder::Result const& path, uint32& segIdx, uint32& pointIdx)
    {
        if (segIdx >= path.segments.size())
            return false;
        ++pointIdx;
        while (segIdx < path.segments.size() && pointIdx >= path.segments[segIdx].polyline.size())
        {
            ++segIdx;
            pointIdx = 0;
        }
        return segIdx < path.segments.size();
    }
}

DungeonPathFollower::Hop DungeonPathFollower::NextHop(Player* bot, ChunkedPathfinder::Result const& path,
                                                     DungeonFollowerState& state)
{
    Hop hop;
    if (!bot || path.segments.empty())
    {
        hop.isDone = true;
        return hop;
    }

    // Skip over empty segments at the start.
    while (state.segmentIdx < path.segments.size() &&
           state.pointIdx >= path.segments[state.segmentIdx].polyline.size())
    {
        ++state.segmentIdx;
        state.pointIdx = 0;
    }

    // Advance past any points the bot has already reached. Cheap loop —
    // typically zero iterations per tick, but catches the "engine spline
    // sped through several polyline points between Advance ticks" case.
    while (state.segmentIdx < path.segments.size())
    {
        std::optional<G3D::Vector3> cur = PointAt(path, state.segmentIdx, state.pointIdx);
        if (!cur.has_value())
        {
            hop.isDone = true;
            return hop;
        }
        // Cylinder, not sphere: horizontal arrival with a separate (loose)
        // vertical guard. A 3D radius conflates "still walking towards it" with
        // "standing directly underneath it on a ramp", and the latter never
        // resolves — see POINT_REACHED.
        if (!PointIsReached(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), *cur))
        {
            hop.point = *cur;
            PathSegment const& seg = path.segments[state.segmentIdx];
            hop.isJump = IsJumpSegment(seg) && IsLastPointOfSegment(seg, state.pointIdx);
            return hop;
        }
        // Reached this point — advance cursor.
        if (!AdvanceCursor(path, state.segmentIdx, state.pointIdx))
        {
            hop.isDone = true;
            return hop;
        }
    }

    hop.isDone = true;
    return hop;
}

bool DungeonPathFollower::PointIsReached(float botX, float botY, float botZ, G3D::Vector3 const& p)
{
    float const dx = botX - p.x;
    float const dy = botY - p.y;
    if (dx * dx + dy * dy > POINT_REACHED * POINT_REACHED)
        return false;
    return std::fabs(botZ - p.z) <= POINT_REACHED_Z;
}

bool DungeonPathFollower::PointIsVerticallyStranded(float botX, float botY, float botZ,
                                                    G3D::Vector3 const& p)
{
    float const dx = botX - p.x;
    float const dy = botY - p.y;
    if (dx * dx + dy * dy > POINT_REACHED * POINT_REACHED)
        return false;  // not there in plan view — ordinary "still walking"
    return std::fabs(botZ - p.z) > POINT_REACHED_Z;
}

bool DungeonPathFollower::SkipStrandedPoint(Player* bot, ChunkedPathfinder::Result const& path,
                                            DungeonFollowerState& state, G3D::Vector3& skipped)
{
    if (!bot || path.segments.empty() || state.segmentIdx >= path.segments.size())
        return false;

    std::optional<G3D::Vector3> const cur = PointAt(path, state.segmentIdx, state.pointIdx);
    if (!cur.has_value())
        return false;

    if (!PointIsVerticallyStranded(bot->GetPositionX(), bot->GetPositionY(),
                                   bot->GetPositionZ(), *cur))
        return false;

    // Never skip the last point of a jump segment — that leg is a MoveJump the
    // caller still has to drive, and a vertical gap is its NORMAL state.
    PathSegment const& seg = path.segments[state.segmentIdx];
    if (IsJumpSegment(seg) && IsLastPointOfSegment(seg, state.pointIdx))
        return false;

    skipped = *cur;
    if (!AdvanceCursor(path, state.segmentIdx, state.pointIdx))
        return false;  // route exhausted — let the hop-done ladder own it
    state.offPathTicks = 0;
    return true;
}

float DungeonPathFollower::RouteDeviation(Player* bot, ChunkedPathfinder::Result const& path,
                                          DungeonFollowerState const& state)
{
    if (!bot || path.segments.empty() || state.segmentIdx >= path.segments.size())
        return 0.0f;

    std::optional<G3D::Vector3> cur = PointAt(path, state.segmentIdx, state.pointIdx);
    if (!cur.has_value())
        return 0.0f;

    float const px = bot->GetPositionX();
    float const py = bot->GetPositionY();
    float dist2 = std::numeric_limits<float>::max();

    if (std::optional<G3D::Vector3> prev = PrevPoint(path, state.segmentIdx, state.pointIdx))
        dist2 = std::min(dist2, Dist2ToSegment2DSq(px, py, *prev, *cur));
    if (std::optional<G3D::Vector3> next = NextPoint(path, state.segmentIdx, state.pointIdx))
        dist2 = std::min(dist2, Dist2ToSegment2DSq(px, py, *cur, *next));
    if (dist2 == std::numeric_limits<float>::max())
    {
        // No prev/next — single-point path. Fall back to distance to point.
        float const dx = px - cur->x;
        float const dy = py - cur->y;
        dist2 = dx * dx + dy * dy;
    }

    return std::sqrt(dist2);
}

std::optional<G3D::Vector3> DungeonPathFollower::CurrentPoint(ChunkedPathfinder::Result const& path,
                                                              DungeonFollowerState const& state)
{
    if (path.segments.empty() || state.segmentIdx >= path.segments.size())
        return std::nullopt;
    return PointAt(path, state.segmentIdx, state.pointIdx);
}

bool DungeonPathFollower::PointIsBehind(float botX, float botY, float pX, float pY,
                                        float dirX, float dirY)
{
    // Degenerate heading: no opinion. Callers keep their existing behaviour.
    if (dirX * dirX + dirY * dirY <= 0.0001f)
        return false;
    // Negative projection onto the route heading = reaching it means travelling
    // against the route.
    return (pX - botX) * dirX + (pY - botY) * dirY < 0.0f;
}

bool DungeonPathFollower::HopIsBehind(Player* bot, ChunkedPathfinder::Result const& path,
                                      DungeonFollowerState const& state, Hop const& hop)
{
    if (!bot || hop.isDone)
        return false;

    std::optional<G3D::Vector3> const cur =
        PointAt(path, state.segmentIdx, state.pointIdx);
    if (!cur.has_value())
        return false;

    // Route tangent at the cursor: forward to the next point when there is one,
    // else the incoming leg at a path tail.
    std::optional<G3D::Vector3> ref = NextPoint(path, state.segmentIdx, state.pointIdx);
    G3D::Vector3 from = *cur;
    if (!ref.has_value())
    {
        ref = cur;
        std::optional<G3D::Vector3> const prev =
            PrevPoint(path, state.segmentIdx, state.pointIdx);
        if (!prev.has_value())
            return false;
        from = *prev;
    }

    return PointIsBehind(bot->GetPositionX(), bot->GetPositionY(),
                         hop.point.x, hop.point.y,
                         ref->x - from.x, ref->y - from.y);
}

bool DungeonPathFollower::IsOffPath(Player* bot, ChunkedPathfinder::Result const& path, DungeonFollowerState& state)
{
    if (!bot || path.segments.empty() || state.segmentIdx >= path.segments.size() ||
        !PointAt(path, state.segmentIdx, state.pointIdx).has_value())
    {
        state.offPathTicks = 0;
        return false;
    }

    bool const off = RouteDeviation(bot, path, state) > OFF_PATH_THRESHOLD;
    if (off)
        ++state.offPathTicks;
    else
        state.offPathTicks = 0;
    return off;
}

namespace
{
    // Flattens (segIdx, pointIdx) into a single index across the path's
    // polyline. Used only for Resnap's window walk — segment boundaries
    // don't matter to "is this point close to the bot".
    struct FlatIndex
    {
        uint32 seg;
        uint32 pt;
    };

    bool StepFlatForward(ChunkedPathfinder::Result const& path, FlatIndex& idx)
    {
        if (idx.seg >= path.segments.size())
            return false;
        ++idx.pt;
        while (idx.seg < path.segments.size() && idx.pt >= path.segments[idx.seg].polyline.size())
        {
            ++idx.seg;
            idx.pt = 0;
        }
        return idx.seg < path.segments.size();
    }

    float Dist3DSq(float px, float py, float pz, G3D::Vector3 const& q)
    {
        float const dx = px - q.x;
        float const dy = py - q.y;
        float const dz = pz - q.z;
        return dx * dx + dy * dy + dz * dz;
    }

    // Eye-height bump so floor-level polyline points don't false-fail LOS on
    // stairs/slopes. Mirrors StridedPathfinder's LOS_Z_BUMP.
    constexpr float RESNAP_LOS_Z_BUMP = 1.5f;

    // Can the bot see `p` in a straight static-VMAP line? Used to keep Resnap
    // from teleporting the follower's cursor onto a point that is physically
    // close but on the far side of a wall — the classic U-turn / S-crossing
    // failure, where the two arms of the bend come within RESNAP_RADIUS and a
    // naive nearest-point snap skips the entire corridor between them.
    bool BotCanSee(Player* bot, G3D::Vector3 const& p)
    {
        if (!bot)
            return false;
        Map const* map = bot->GetMap();
        if (!map)
            return true;  // no map data — don't block the resnap
        return map->isInLineOfSight(bot->GetPositionX(), bot->GetPositionY(),
                                    bot->GetPositionZ() + RESNAP_LOS_Z_BUMP,
                                    p.x, p.y, p.z + RESNAP_LOS_Z_BUMP,
                                    bot->GetPhaseMask(), LINEOFSIGHT_CHECK_VMAP,
                                    VMAP::ModelIgnoreFlags::Nothing);
    }
}

bool DungeonPathFollower::Resnap(Player* bot, ChunkedPathfinder::Result const& path, DungeonFollowerState& state)
{
    if (!bot || path.segments.empty() || state.segmentIdx >= path.segments.size())
        return false;

    float const px = bot->GetPositionX();
    float const py = bot->GetPositionY();
    float const pz = bot->GetPositionZ();

    // Gather a window of polyline points AT OR AHEAD of the current cursor, then
    // pick the one closest to the bot in 3D that the bot can actually see in a
    // straight line. The LOS gate guards against U-turns whose arms come within
    // RESNAP_RADIUS: a physically-near point on the far (forward) arm is walled
    // off, and snapping to it would skip the whole bend and leave the bot trying
    // to cross the inside wall.
    //
    // FORWARD-ONLY. The escort drives a one-way route, so the cursor must never
    // regress to a point the bot already cleared. We used to also search BEHIND
    // the cursor and pick the nearest visible point in either direction — but on
    // a switchback/loop, an already-walked point on the parallel arm is often the
    // physically nearest one, so after a trash chase (which leaves the cursor
    // stale-behind the bot's real position) the resnap would grab that old point
    // and march the tank back down corridor it had finished. Restricting the
    // search to the cursor and forward eliminates the backtrack: the bot is
    // standing among the points it just swept past, so the nearest forward point
    // is right where it is, and the resume goes the way we're headed.
    //
    // BotCanSee is a static-VMAP raycast — the expensive part of this routine.
    // Rather than raycast every windowed point, collect candidates, sort by
    // distance, and raycast in nearest-first order: the first point that passes
    // LOS is the closest visible one, so we usually run 1–2 raycasts instead of
    // the full window. Forward bias on ties (later flat index wins) keeps the
    // resnap from replaying corridor we already cleared.
    struct Candidate
    {
        uint32 seg;
        uint32 pt;
        G3D::Vector3 pos;
        float dist2;
        uint64 flatIdx;  // seg * 100000 + pt — monotonic along the route
    };

    float const radius2 = RESNAP_RADIUS * RESNAP_RADIUS;
    std::vector<Candidate> candidates;
    candidates.reserve(2 * RESNAP_WINDOW + 1);

    auto consider = [&](uint32 seg, uint32 pt)
    {
        std::optional<G3D::Vector3> p = PointAt(path, seg, pt);
        if (!p.has_value())
            return;
        float const d2 = Dist3DSq(px, py, pz, *p);
        if (d2 > radius2)
            return;  // out of snap range — never a valid pick
        candidates.push_back(Candidate{seg, pt, *p, d2,
                                       static_cast<uint64>(seg) * 100000ULL + pt});
    };

    // Forward walk only (starts at the current point, includes it). No backward
    // walk: snapping behind the cursor is the backtrack we are eliminating.
    FlatIndex fwd{state.segmentIdx, state.pointIdx};
    for (size_t step = 0; step <= RESNAP_WINDOW; ++step)
    {
        consider(fwd.seg, fwd.pt);
        if (!StepFlatForward(path, fwd))
            break;
    }

    if (candidates.empty())
        return false;

    std::sort(candidates.begin(), candidates.end(),
              [](Candidate const& a, Candidate const& b)
              {
                  if (std::fabs(a.dist2 - b.dist2) > 1e-5f)
                      return a.dist2 < b.dist2;
                  return a.flatIdx > b.flatIdx;  // forward bias on ties
              });

    for (Candidate const& cand : candidates)
    {
        if (!BotCanSee(bot, cand.pos))
            continue;
        // Sorted nearest-first: first visible candidate is the closest one.
        state.segmentIdx = cand.seg;
        state.pointIdx = cand.pt;
        state.offPathTicks = 0;
        return true;
    }

    return false;
}

std::vector<G3D::Vector3> DungeonPathFollower::BuildSplineWindow(Player* bot,
    ChunkedPathfinder::Result const& path, DungeonFollowerState const& state,
    float maxYards)
{
    std::vector<G3D::Vector3> window;
    if (!bot || path.segments.empty() || state.segmentIdx >= path.segments.size())
        return window;

    // Element 0 is the live position. MoveSplineInit::Launch overwrites
    // path[0] with the unit's real position anyway, but seeding it keeps the
    // initial spline tangent correct and matches the escort path[0]=start
    // convention.
    window.push_back(G3D::Vector3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()));

    AppendWindowPoints(path, state.segmentIdx, state.pointIdx, maxYards, window);
    return window;
}

void DungeonPathFollower::AppendWindowPoints(ChunkedPathfinder::Result const& path,
                                             uint32 seg, uint32 pt, float maxYards,
                                             std::vector<G3D::Vector3>& window)
{
    if (window.empty())
        return;  // needs the live-position seed to accumulate length from

    float accumulated = 0.0f;
    G3D::Vector3 prev = window.back();
    while (seg < path.segments.size() && window.size() < MAX_SPLINE_WINDOW_POINTS)
    {
        PathSegment const& s = path.segments[seg];
        // A jump leg can't be expressed as a ground spline — deliver the bot
        // to the lip and let the per-hop JumpTo branch take the jump point.
        if (s.jumpDown || s.jumpGap)
            break;

        if (pt < s.polyline.size())
        {
            G3D::Vector3 const& next = s.polyline[pt];
            window.push_back(next);
            ++pt;
            // Cap AFTER pushing: the point that crosses the cap is included, and
            // a cap smaller than the first leg still yields one forward point.
            accumulated += (next - prev).length();
            prev = next;
            if (maxYards > 0.0f && accumulated >= maxYards)
                break;
        }
        else
        {
            ++seg;
            pt = 0;
        }
    }
}

std::optional<G3D::Vector3> DungeonPathFollower::PointBehind(Player* bot,
    ChunkedPathfinder::Result const& path, DungeonFollowerState const& state, float distance)
{
    if (!bot || path.segments.empty() || distance <= 0.0f)
        return std::nullopt;

    // Walk backward from the bot's live position through the polyline points
    // strictly BEHIND the cursor, accumulating 3D distance until we've covered
    // `distance` of cleared route. The cursor (segmentIdx, pointIdx) is the NEXT
    // point to walk to, so PrevPoint(cursor) is the first already-traveled point.
    G3D::Vector3 prev(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    uint32 seg = state.segmentIdx;
    uint32 pt = state.pointIdx;
    float accumulated = 0.0f;
    std::optional<G3D::Vector3> best;  // last point we backed onto

    while (std::optional<G3D::Vector3> p = PrevPoint(path, seg, pt))
    {
        accumulated += (*p - prev).length();
        prev = *p;
        best = p;
        if (accumulated >= distance)
            return best;

        // Step the (seg, pt) cursor back by one point so the next PrevPoint
        // call walks further behind, crossing segment boundaries like PrevPoint
        // itself does.
        if (pt > 0)
        {
            --pt;
        }
        else
        {
            bool stepped = false;
            for (uint32 i = seg; i > 0; --i)
            {
                if (!path.segments[i - 1].polyline.empty())
                {
                    seg = i - 1;
                    pt = static_cast<uint32>(path.segments[seg].polyline.size()) - 1;
                    stepped = true;
                    break;
                }
            }
            if (!stepped)
                break;  // reached the route start
        }
    }

    // Ran out of cleared route before reaching the full setback: return the
    // earliest point we backed onto (some room is better than none), or nullopt
    // if there was nothing behind the cursor at all.
    return best;
}
