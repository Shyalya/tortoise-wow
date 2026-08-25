/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearActions.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Creature.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSplineInitArgs.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Position.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/Data/BossPullbackRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DcEventDoorRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearApproach.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearApproachIo.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcDoorPolicy.h"
#include "Ai/Dungeon/DungeonClear/Util/DcMovement.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPathWorker.h"
#include "Ai/Dungeon/DungeonClear/Util/DcSocialQuarantine.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/LongRangePathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/StridedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/SwimPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearStateValues.h"
#include "Playerbots.h"
#include "DcActionShared.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBreadcrumb.h"

using namespace DcActionShared;

namespace
{
    // MoveTo-returned-false counter. Raised from 2 to 8 because dedup
    // (IsDuplicateMove returning true while the bot is making real progress
    // on the prior move) was tripping the original threshold during normal
    // operation. The position-based detector below is the authoritative
    // "actually stuck" signal; this counter survives only as a backup for
    // the case where the bot is stationary AND MoveTo keeps refusing.
    constexpr uint32 DC_STUCK_LIMIT = 8;

    // Position-based stuck detection (DC_STUCK_DISPLACEMENT / DC_STUCK_TICK_LIMIT)
    // now lives in DungeonClearTuning.h — it is shared with the door-blocked
    // walk-in, which glides the same escort spline and needs the same wedge
    // recovery. See that header for the per-tick threshold rationale.

    // Distance from the bot to its next polyline hop above which the follower
    // cursor is treated as stale and force-re-anchored (Resnap). During clean
    // gliding the next hop is only ~one polyline step ahead (~4-8yd), so a
    // larger gap means the tank was displaced off its cursor — almost always
    // by a trash chase (EngageDirect walk + combat MoveChase). Unlike the
    // perpendicular IsOffPath check, this also catches ALONG-track
    // displacement (chased forward past the cursor), which would otherwise
    // make NextHop target a point behind the tank and walk it backward.
    constexpr float DC_REANCHOR_DISTANCE = 12.0f;

    // The creature store (Map::GetCreatureBySpawnIdStore) only contains
    // creatures in LOADED grids; grids stream in within ~MAX_VISIBILITY_DISTANCE
    // (250yd) of a moving player. Beyond this distance, a boss simply not being
    // in the store means its grid hasn't loaded yet — NOT that it isn't spawned.
    // So Advance keeps walking toward the boss's static spawn coords to load the
    // grid en route instead of stalling. Kept comfortably under 250yd so the
    // grid is certainly resident by the time we'd declare the boss truly missing.
    constexpr float DC_BOSS_GRID_LOADED_RANGE = 150.0f;

    // FINAL-approach shortcut: once the boss is loaded, visible, and this close,
    // walk straight at its LIVE position (per-tick re-path) instead of riding the
    // corridor glide the last few yards — snappier on a boss that steps around
    // near engage range.
    //
    // This is DELIBERATELY short. The long-path itself now targets the boss's
    // EFFECTIVE (live) coords (see EnsureLongPath below), so the corridor already
    // tracks a wandering/patrolling boss the whole way in — the old "tank parks at
    // the static spawn anchor and idles" failure this branch was widened to 80yd
    // for no longer exists. A wide pursuit range was actively harmful: from far
    // out the straight-line MoveTo follows a DIFFERENT route than the LOS-screened,
    // centered corridor, so as boss-LOS flickered behind room pillars the bot
    // oscillated between the two routes — pursuit dragging it off the corridor,
    // the off-line rejoin yanking it back (the Scholomance "boss-approach dance" on
    // the way to Jandice Barov). Kept to a true final-approach range, the straight
    // shot is in the boss's own open room where it ~matches the corridor end, so
    // the two no longer fight. LOS-gated either way; out of range / LOS the
    // wall-screened long-path drives.
    constexpr float DC_DIRECT_PURSUIT_RANGE = 35.0f;

    // The long-path can complete (cursor reaches the polyline end) while the bot
    // is still outside DC_ENGAGE_RANGE of the boss: the navmesh route dead-ends
    // short (boss on a ledge / across a gap, wall-screened route that can't close
    // the last yards). NextHop reports done, Advance rebuilds an identical
    // 0-point path, and because the bot isn't moving the position-based stuck
    // counter never fires — a silent forever-loop (observed: WC Lady Anacondra
    // spun here ~3 min until the log was cut). For this many consecutive
    // done-but-not-engaged ticks Advance tries a straight final-approach MoveTo
    // (PathGenerator may close a few yards Detour's chunk builder gave up on);
    // past it the boss is declared unreachable and we stall for `dc skip`.
    constexpr uint32 DC_DONE_NOT_ENGAGED_LIMIT = 15;

    // Consecutive direct-pursuit ticks that issued no movement (MoveTo returned
    // false and the bot is neither moving nor waiting on an in-flight move)
    // before Advance abandons the LIVE-boss direct-pursuit shortcut and falls
    // through to the wall-screened long-path. The direct-pursuit MoveTo bee-lines
    // the boss's live poly through the raw PathGenerator, which can fail to
    // resolve a path (Z -> INVALID_HEIGHT, or a winding route past its 74-hop
    // cap) and then silently returns false every tick — the bot never moves, so
    // the position-based stuck counter can't catch it. A short grace absorbs a
    // transient miss (boss mid-step, grid still settling); past it we hand off
    // to the long-path (LongRangePathfinder, no hop cap), which carries its own
    // dead-end -> stall escalation. ~5 ticks ≈ a couple of seconds.
    constexpr uint32 DC_PURSUIT_FAIL_LIMIT = 5;

    // Recovery moves run when the bot is wedged off the navmesh or has
    // failed to make progress for DC_STUCK_TICK_LIMIT consecutive ticks.
    // Single-player server only — the teleport blink is visible to other
    // players. Flip to false to disable both shims and keep the legacy
    // "stall and wait for `dc skip`" behavior.
    constexpr bool DC_ALLOW_RECOVERY_MOVES = true;
    // 5yd offsets for the FARFROMPOLY-START recovery; small enough that
    // the bot doesn't significantly mis-position, large enough to clear
    // the off-mesh poly the bot may have wedged on.
    constexpr float DC_RECOVERY_OFFSET = 5.0f;

    // --- Submerged swim legs (Tier A) ------------------------------------
    // 3D proximity at which the swim cursor treats a point as reached.
    constexpr float DC_SWIM_POINT_REACHED = 3.0f;
    // If the bot is farther than this from the current swim point, the leg is
    // stale (teleport / knockback / leftover from a prior run) — drop it.
    constexpr float DC_SWIM_OFFLEG_MAX = 50.0f;
    // Abandon a swim leg that makes no closing progress for this long.
    constexpr uint32 DC_SWIM_STUCK_MS = 6000;


    // Short label for the active movement generator, for advance telemetry.
    // Names the types the dungeon-clear follower drives (ESCORT/POINT) or
    // fights against (CHASE/FOLLOW = combat/leader movement overriding the
    // escort spline); the caller also prints the raw enum value alongside.
    char const* MoveGenTypeName(MovementGeneratorType t)
    {
        switch (t)
        {
            case IDLE_MOTION_TYPE:   return "IDLE";
            case CHASE_MOTION_TYPE:  return "CHASE";
            case POINT_MOTION_TYPE:  return "POINT";
            case FOLLOW_MOTION_TYPE: return "FOLLOW";
            case ESCORT_MOTION_TYPE: return "ESCORT";
            case HOME_MOTION_TYPE:   return "HOME";
            case NULL_MOTION_TYPE:   return "NULL";
            default:                 return "OTHER";
        }
    }


    // A DungeonClearApproach::Observation pre-loaded with this TU's DC_* thresholds
    // and an all-inactive state (the struct's defaults: posStuck 0, !canPursue,
    // pathReachable, !hopDone, ... -> DecideApproach returns the terminal
    // MoveToFallback). The tail phases that own a regression-prone THRESHOLD
    // decision (the posStuck tick limit, the pursuit-fail latch, the dead-end
    // escalation budget) fill in just their own state fields and consult
    // DecideApproach, so those thresholds live in one engine-free, gtested place
    // instead of inline `>=`/`<` comparisons that could drift from the spec. The
    // plain-boolean rungs (jump / glide / off-line / window / unreachable /
    // off-path) keep their flags inline — there is no threshold for the pure
    // function to own there.
    DungeonClearApproach::Observation MakeApproachObs()
    {
        DungeonClearApproach::Observation o;
        o.stuckTickLimit      = DC_STUCK_TICK_LIMIT;
        o.pursuitFailLimit    = DC_PURSUIT_FAIL_LIMIT;
        o.doneNotEngagedLimit = DC_DONE_NOT_ENGAGED_LIMIT;
        return o;
    }


    // Capture hook for the orchestration replay harness. When the run has
    // RecordDecisions on (off by default, an addon-toggleable per-run flag),
    // appends one (observation -> verdict) line to the capture file — a freeze
    // reproduced with capture on becomes a JSONL fixture the gtest suite pins
    // forever. Execute calls this ONCE per tick with the verdict that OWNED the
    // tick and the observation as-completed-through-that-owning stage, so every
    // acted-on decision is a whole-tick, replayable fixture (the old staged
    // callers each recorded a mostly-default, stage-local observation — nav F10).
    void MaybeRecord(Player* bot, DungeonClearApproach::Observation const& o,
                     DungeonClearApproach::Verdict v)
    {
        if (bot && DcSettings::GetBool(bot, "RecordDecisions"))
            DungeonClearApproachIo::Record(bot->GetObjectGuid().GetRawValue(),
                                           getMSTime(), o, v);
    }


    // Try a small offset move when the bot wedges on geometry off the
    // navmesh (PATHFIND_FARFROMPOLY_START). Walks four cardinal offsets;
    // picks the first one whose PathGenerator probe returns a usable
    // path (NORMAL or INCOMPLETE — even partial is enough for recovery).
    //
    // Returns true if a recovery move was issued. False means none of
    // the offsets looked recoverable; caller stalls normally.
    bool TryFarFromPolyRecovery(Player* bot)
    {
        if (!bot)
            return false;
        float const x = bot->GetPositionX();
        float const y = bot->GetPositionY();
        float const z = bot->GetPositionZ();

        // FLOOR-BANDED rescue first. The symmetric column search below asks
        // for the poly NEAREST the bot's own Z - a bot standing ON the
        // phantom terrain deck (map 36 carries the Westfall surface as
        // valid-looking mesh ~200y above the mine) is answered with the deck
        // itself and never comes down. The next boss's spawn Z is ground
        // truth for the target floor: when we are far above it, search a
        // band around IT and only accept a hit well below us.
        {
            PlayerbotAI* const recAI = GET_PLAYERBOT_AI(bot);
            AiObjectContext* const recCtx = recAI ? recAI->GetAiObjectContext() : nullptr;
            std::optional<DungeonBossInfo> const nb =
                recCtx ? recCtx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get()
                       : std::optional<DungeonBossInfo>{};
            // +25/band 40, not +70/60: at the ENTRANCE the phantom deck sits
            // only ~35y over the real floor (Z 89-95 over 60) - the original
            // +70 gate never saw exactly the spot every run kept regressing
            // to (live tr-20260823-010638-1: tank parked at Z 95.5 on
            // (-16,-391) with the gate silent). The >25y-below acceptance
            // keeps stairs/ramps safe.
            if (nb.has_value() && z > nb->z + 25.0f)
            {
                NavmeshSnap::Result const floorHit = NavmeshSnap::SnapColumn(
                    bot->GetMap(), x, y, nb->z, /*halfHeight*/ 40.0f, /*radius*/ 8.0f);
                if (floorHit.ok && z - floorHit.z > 25.0f)
                {
                    bot->GetMotionMaster()->Clear();
                    bot->NearTeleportTo(floorHit.x, floorHit.y, floorHit.z,
                                        bot->GetOrientation(),
                                        /*casting*/ false, /*vehicle*/ false, /*withPet*/ true);
                    LOG_INFO("playerbots.dungeonclear",
                             "[DC:{}] far-from-poly recovery: floor-banded snap {:.0f}y "
                             "down off the phantom deck",
                             bot->GetName(), z - floorHit.z);
                    return true;
                }
            }
        }

        // Vertical rescue FIRST: the bot may be parked ON AIR far above the
        // mesh - live, stock movement grounded the DC leader on the
        // OVERWORLD height (Z=216) over the Deadmines entrance, 150y above
        // the real floor, and 5yd cardinal hops can never reach that. Ask
        // the mesh for the nearest poly in a tall column under our XY and
        // near-teleport onto it.
        {
            NavmeshSnap::Result const column = NavmeshSnap::SnapColumn(bot->GetMap(), x, y, z);
            if (column.ok && std::fabs(column.z - z) > 8.0f)
            {
                bot->GetMotionMaster()->Clear();
                bot->NearTeleportTo(column.x, column.y, column.z, bot->GetOrientation(),
                                    /*casting*/ false, /*vehicle*/ false, /*withPet*/ true);
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] far-from-poly recovery: column snap {:.0f}y vertically onto the mesh",
                         bot->GetName(), std::fabs(column.z - z));
                return true;
            }
        }

        struct Offset { float dx, dy; };
        Offset const offsets[] = {
            {DC_RECOVERY_OFFSET, 0.0f},
            {-DC_RECOVERY_OFFSET, 0.0f},
            {0.0f, DC_RECOVERY_OFFSET},
            {0.0f, -DC_RECOVERY_OFFSET},
        };
        for (Offset const& o : offsets)
        {
            float const nx = x + o.dx;
            float const ny = y + o.dy;
            float nz = z;
            bot->UpdateAllowedPositionZ(nx, ny, nz);
            PathGenerator gen(bot);
            gen.CalculatePath(nx, ny, nz, /*forceDest*/ false);
            PathType const t = gen.GetPathType();
            // Accept anything that produced a real point path — we just
            // need to budge onto a polygon. The chunked rebuild on the
            // next tick handles the actual route from the new position.
            if (t & PATHFIND_NOPATH)
                continue;
            if (t & PATHFIND_FARFROMPOLY_START)
                continue;  // didn't actually help — still off the mesh

            // A nudge is a SIDESTEP. Reject a probe whose generated path is far
            // longer than the offset itself: on a ramp the blind axis probes
            // point up/down the slope, and PathGenerator answers one of them by
            // leaving the incline, running back along the corridor and
            // returning — tens of yards of travel issued as a 5yd budge, which
            // is the "long walk" half of the ramp ping-pong. Without this the
            // recovery is what strands the tank, not what frees it.
            float const straight = std::sqrt((nx - x) * (nx - x) + (ny - y) * (ny - y));
            if (straight > 0.0f)
            {
                Movement::PointsArray const& pts = gen.GetPath();
                float pathLen = 0.0f;
                for (size_t i = 1; i < pts.size(); ++i)
                {
                    G3D::Vector3 const d = pts[i] - pts[i - 1];
                    pathLen += d.length();
                }
                if (pathLen > straight * DC_NUDGE_MAX_DETOUR_RATIO)
                {
                    LOG_DEBUG("playerbots.dungeonclear",
                              "[DC:{}] nudge probe ({:+.0f},{:+.0f}) rejected: {:.1f}yd path for a "
                              "{:.1f}yd sidestep (limit {}x) -> trying the next offset",
                              bot->GetName(), o.dx, o.dy, pathLen, straight,
                              DC_NUDGE_MAX_DETOUR_RATIO);
                    continue;
                }
            }

            MotionMaster* mm = bot->GetMotionMaster();
            if (mm)
                mm->MovePoint(0, nx, ny, nz, FORCED_MOVEMENT_NONE, 0.0f, 0.0f, /*generatePath*/ true, false);
            return true;
        }
        return false;
    }


    // Forward-recovery: try a cheap polyline Resnap first — the bot is
    // often only a few yards off the planned corridor (sticky-trash
    // detour, follower bump, micro-knockback) and reusing the existing
    // path is faster and visually less disruptive than rebuilding. If
    // Resnap fails, invalidate the cache and reset the follower so the
    // next Advance tick rebuilds the route from the bot's current poly.
    //
    // The v1 design used a back-teleport (NearTeleportTo to the previous
    // segment) for this case, but that hurt as often as it helped — the
    // bot would teleport backward, re-run the same builder from the same
    // point, and get the same wrong route. Returning false here yields the
    // tick without issuing movement so Advance can re-enter cleanly.
    //
    // Returns true when Resnap kept us on the existing path; false when
    // a full rebuild is needed (in which case the cache/state are reset).
    // `allowResnap` false forces the invalidate-and-rebuild path even when the bot
    // could still be snapped onto the cached polyline — the caller uses it once
    // repeated resnaps have failed to restore progress (see DC_MAX_RESNAP_ATTEMPTS).
    bool TriggerStrideRebuild(Player* bot, AiObjectContext* ctx, DcApproachState& appr,
                              bool allowResnap = true)
    {
        ChunkedPathfinder::Result const& path =
            ctx->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Get();
        DungeonFollowerState& follower =
            ctx->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get();
        if (allowResnap && path.reachable && !path.segments.empty() &&
            DungeonPathFollower::Resnap(bot, path, follower))
            return true;

        appr.longPathExpiresMs = 0;
        ctx->GetValue<uint32>(DcKey::CurrentHop)->Set(0u);
        follower = DungeonFollowerState{};
        return false;
    }


    // Begin a swim leg from the bot's current position to (bx,by,bz). Gated on
    // SwimEnable, SwimMaxRange, and water actually lying between. Stores the leg
    // in "dungeon clear swim state"; DriveActiveSwim issues the spline next tick.
    // Returns true iff a leg was started.
    bool TryBeginSwim(Player* bot, AiObjectContext* context,
                      uint32 targetEntry, float bx, float by, float bz)
    {
        if (!bot || !DcSettings::GetBool(bot, "SwimEnable"))
            return false;

        G3D::Vector3 const start(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        G3D::Vector3 const goal(bx, by, bz);
        if ((goal - start).length() > DcSettings::GetFloat(bot, "SwimMaxRange"))
            return false;
        if (!SwimPathfinder::WaterBetween(bot, start, goal))
            return false;

        SwimPathfinder::Result res = SwimPathfinder::Build(bot, start, goal);
        if (!res.ok || res.points.empty())
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] swim build failed: {}", bot->GetName(), res.failureReason);
            return false;
        }

        DungeonClearSwimState& swim =
            context->GetValue<DungeonClearSwimState&>(DcKey::SwimState)->Get();
        swim.Reset();
        swim.active = true;
        swim.points = std::move(res.points);
        swim.cursor = 0;
        swim.targetEntry = targetEntry;
        swim.buildStart = start;
        // Arm the closing-distance watchdog at the initial distance to the first
        // point (swim.Reset() above cleared it), so the stale clock runs from now.
        swim.progressWatch.TickClosing((swim.points.front() - start).length(),
                                       /*minClose*/ 0.5f, getMSTime());

        DcMovement::ResolveEscortConflict(bot);  // drop any stale navmesh glide before swimming
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] swim leg started: {} pts toward ({:.1f},{:.1f},{:.1f})",
                 bot->GetName(), swim.points.size(), bx, by, bz);
        return true;
    }


    // The pack the pull pipeline is walking TO, for BystanderSpheres' `exclude`.
    // It is the glide's destination, never an obstacle to it: pull-idle above the
    // commit range yields the tick to Advance precisely so the tank can close on
    // it, and a glide that truncates on its own destination cannot.
    //
    // nullptr unless an advanced pull is the active mode (heroic always is). That
    // gate is not just semantics — it keeps the sticky pull-target value's
    // corridor scan off runs with no pull pipeline to serve, so a difficulty that
    // never asked for advanced pulls pays nothing for this.
    Unit* PullDestinationPack(PlayerbotAI* botAI, AiObjectContext* context)
    {
        if (!botAI || !context || !context->GetValue<bool>(DcKey::PullMode)->Get())
            return nullptr;
        return DcTargeting::GetPullTarget(botAI);
    }

    // Drive an in-progress swim leg. Returns true if a leg is active and owned
    // the tick (caller must return true); false if no leg is active or the leg
    // just completed (caller falls through to normal navmesh navigation).
    bool DriveActiveSwim(Player* bot, PlayerbotAI* botAI, AiObjectContext* context,
                         DcApproachState& appr,
                         uint32 targetEntry,
                         float engageDist, float engageRange)
    {
        DungeonClearSwimState& swim =
            context->GetValue<DungeonClearSwimState&>(DcKey::SwimState)->Get();
        if (!swim.active)
            return false;

        // Target changed since the leg was built — invalidate.
        if (swim.targetEntry != targetEntry)
        {
            swim.Reset();
            return false;
        }

        // Arrived at the boss area — hand back to the engage/ladder logic.
        if (engageDist <= engageRange)
        {
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] swim leg complete (within engage range)", bot->GetName());
            swim.Reset();
            DcMovement::ResolveEscortConflict(bot);
            return false;
        }

        G3D::Vector3 const botPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

        // Advance the cursor past points already reached (3D proximity).
        while (swim.cursor < swim.points.size() &&
               (botPos - swim.points[swim.cursor]).length() <= DC_SWIM_POINT_REACHED)
            ++swim.cursor;

        // Consumed the whole leg but still short of engage range — hand back to
        // the navmesh planner from here (the far mesh island may now reach the
        // boss; if not, the dead-end logic re-evaluates and may re-swim).
        if (swim.cursor >= swim.points.size())
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] swim leg consumed -> handing back to navmesh", bot->GetName());
            swim.Reset();
            DcMovement::ResolveEscortConflict(bot);
            appr.longPathExpiresMs = 0;
            return false;
        }

        float const distToPoint = (botPos - swim.points[swim.cursor]).length();

        // Off-leg: bot is implausibly far from the current point (teleport,
        // knockback, stale leg) — drop it and let navigation rebuild.
        if (distToPoint > DC_SWIM_OFFLEG_MAX)
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] swim leg abandoned: {:.0f}yd off the leg", bot->GetName(), distToPoint);
            swim.Reset();
            DcMovement::ResolveEscortConflict(bot);
            appr.longPathExpiresMs = 0;
            return false;
        }

        // Progress watchdog (closing distance to the current point). Displacement
        // can't see a non-moving bot underwater, so the shared watchdog tracks the
        // nearest approach; a leg making no headway for DC_SWIM_STUCK_MS is
        // abandoned. The wrap-safe stale check stays here (getMSTimeDiff).
        uint32 const now = getMSTime();
        if (!swim.progressWatch.TickClosing(distToPoint, /*minClose*/ 0.5f, now) &&
            getMSTimeDiff(swim.progressWatch.lastProgressMs, now) > DC_SWIM_STUCK_MS)
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] swim leg wedged (no progress {}ms) -> abandoning",
                     bot->GetName(), getMSTimeDiff(swim.progressWatch.lastProgressMs, now));
            swim.Reset();
            DcMovement::ResolveEscortConflict(bot);
            StallDungeonClear(botAI,
                "Tried to swim across but got stuck underwater. Use 'dc skip' to move on.");
            return true;
        }

        // Leave a healthy in-flight escort glide alone (same re-issue discipline
        // as the long-path drive — keying on splineRunning, not the LastMovement
        // wait, so the next window chains seamlessly when the spline finalizes).
        MotionMaster* mm = bot->GetMotionMaster();
        bool const splineRunning =
            bot->movespline && !bot->movespline->Finalized() && bot->isMoving();
        if (splineRunning)
        {
            SetPhase(context, "swimming");
            ClearStall(context);
            return true;
        }

        if (!mm)
            return false;

        // Build the spline window from the cursor: [live pos, remaining swim
        // points...] with SUBMERGED Z used verbatim (no UpdateAllowedPositionZ).
        Movement::PointsArray points;
        points.push_back(botPos);
        for (size_t i = swim.cursor;
             i < swim.points.size() && points.size() < DungeonPathFollower::MAX_SPLINE_WINDOW_POINTS;
             ++i)
            points.push_back(swim.points[i]);

        // SplinePath handles stand-up / cast-interrupt / MoveSplinePath and the
        // NORMAL-priority LastMovement record (and refuses a <2-point window).
        if (!DcMovement::SplinePath(botAI, points))
        {
            swim.Reset();
            return false;
        }
        SetPhase(context, "swimming");
        ClearStall(context);
        return true;
    }

}

DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::TryEngageHold(AdvanceState const& st)
{
    DungeonBossInfo const* next = st.next;
    Creature* const liveBoss = st.liveBoss;
    float const engageDist = st.engageDist;
    bool const atBoss = st.atBoss;

    // Travel objectives have no engage handoff. Keep navigating to the anchor;
    // DungeonClearAtObjectiveTrigger (rel 30, outranks Advance) takes over once
    // the tank is inside the arrival radius. Holding here on the boss engage
    // range — which is wider than the arrival radius — would strand the tank
    // short of the objective forever (the at-boss trigger that would normally
    // release the hold is gated off for non-Boss anchors).
    if (next->kind != DungeonAnchorKind::Boss)
        return Step::Continue;

    if (atBoss)
    {
        ChunkedPathfinder::Result const& currentPath =
            AI_VALUE(ChunkedPathfinder::Result&, DcKey::LongPath);
        DungeonFollowerState const& followerNow =
            AI_VALUE(DungeonFollowerState&, DcKey::FollowerState);
        bool anchoredHopsPending = false;
        if (currentPath.reachable && !currentPath.segments.empty())
        {
            // Only inspect segments still ahead of the follower's cursor.
            // Anchored segments already walked past don't need to gate
            // the engage handoff.
            //
            // DungeonClearAtBossTrigger::IsActive runs the SAME test — it is the
            // rung this hold is waiting for, so the two must agree or the tank
            // parks at the boss forever (it did: the trigger's copy started at
            // segment 0, see the note there). Keep them in step.
            for (size_t i = followerNow.segmentIdx; i + 1 < currentPath.segments.size(); ++i)
            {
                PathSegment const& seg = currentPath.segments[i];
                if (seg.anchored && bot->GetDistance(seg.ex, seg.ey, seg.ez) > seg.arriveRadius)
                {
                    anchoredHopsPending = true;
                    break;
                }
            }
        }
        if (!anchoredHopsPending)
        {
            // Surface WHY we're holding: the at-boss trigger only pulls once the
            // party is ready and no loot is pending. When it doesn't fire, this
            // is the line that explains the otherwise-silent idle at the boss.
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] within engage range of {} ({:.0f}yd, live={}) -> holding "
                      "for at-boss [partyReady={} availLoot={} canLoot={}]",
                      bot->GetName(), next->name, engageDist, liveBoss ? 1 : 0,
                      IsBetweenPullsReady(bot, context) ? 1 : 0,
                      AI_VALUE(bool, DcKey::Stock::HasAvailableLoot) ? 1 : 0,
                      AI_VALUE(bool, DcKey::Stock::CanLoot) ? 1 : 0);
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
            ClearStall(context);
            // Parked at the boss waiting for the at-boss pull — not navigating,
            // so clear the nav phase (status reads this as "idle / holding").
            SetPhase(context, "");
            return Step::ReturnFalse;
        }
    }
    return Step::Continue;
}

// Loot yield (with commit-timeout). Step aside through the WHOLE loot lifecycle
// so the loot system can pick up a nearby corpse: "has available loot" is true
// only while a corpse is ~3-15yd away and flips FALSE at ~3yd (when "can loot"
// flips TRUE). Advance (engine relevance 15) outranks the loot actions (open
// loot is 8), so yielding on only one flag let advance win the tick at the 3yd
// boundary and fire a boss-bound spline before open-loot ran — the
// corpse<->boss oscillation. Yielding while EITHER flag is set keeps advance out
// of the way until the loot is actually picked up.
//
// We also hold while ANY follower still has a corpse to pick up, so the tank
// doesn't push to the next pull the instant its own loot is done and leave the
// party scrambling to catch up. IsAnyPartyMemberLooting reads each follower's
// own loot flags cross-context (same pattern as the party-tank lookup); the
// shared commit-timeout below bounds the total wait.
//
// The timeout stops us waiting forever on loot the party can't finish
// (group-loot rolls pending, bags full): after DC_LOOT_YIELD_TIMEOUT_MS we
// force-advance; when no one is looting any more the flags clear and the timer
// resets (so the next pull gets a fresh full window).
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::TryLootYield(AdvanceState const& /*st*/)
{
    // Before reading the flags, drop any loot we already gave up on from the
    // stock stack/target (see StripSkippedLoot). Running here — at advance's
    // relevance, above the loot pipeline — means stock can't re-pick a skipped
    // corpse this tick, so the flags below and the timeout's give-up stay in
    // sync and the yield doesn't re-arm on something we just abandoned.
    DcLootPolicy::StripSkippedLoot(botAI);
    // Proactively skip a corpse with nothing takeable for us (un-finishable
    // group-roll/reserved loot, or below DungeonClear.LootMinQuality) BEFORE we
    // walk to it, so it never arms the yield at all — the event-driven analogue
    // of the camp/timeout cutoffs below, which only fire after a wasted walk.
    DcLootPolicy::MaybeSkipUnworthyLoot(botAI);
    // Fast-skip a corpse we've been camped on too long (un-lootable) before it
    // can burn the full yield timeout below; followers do the same in their
    // follow-tank yield, which is what actually shortens IsAnyPartyMemberLooting.
    DcLootPolicy::MaybeGiveUpCampedLoot(botAI, DC_LOOT_CAMP_TIMEOUT_MS, DC_LOOT_GIVEUP_TTL_MS);
    uint32& lootYieldStart =
        context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get().lootYieldStartMs;
    std::string lootHolder;
    bool const partyLooting = DcPartyState::IsAnyPartyMemberLooting(bot, &lootHolder);
    bool const lootYield =
        AI_VALUE(bool, DcKey::Stock::HasAvailableLoot) || AI_VALUE(bool, DcKey::Stock::CanLoot) ||
        partyLooting;
    if (lootYield)
    {
        uint32 const now = getMSTime();
        if (lootYieldStart == 0)
            lootYieldStart = now;

        if (now - lootYieldStart >= DC_LOOT_YIELD_TIMEOUT_MS)
        {
            // Waited long enough — give up on THIS corpse so we stop re-arming
            // the yield on it (the corpse<->path ping-pong), then advance past.
            // GiveUpCurrentLoot blacklists our committed loot; StripSkippedLoot
            // next tick removes it so the flags clear. Don't reset lootYieldStart
            // here: keep it expired so we keep advancing until the flags drop.
            DcLootPolicy::GiveUpCurrentLoot(botAI, DC_LOOT_GIVEUP_TTL_MS);
            // Throttled: this branch runs every tick while the flags stay up
            // (run 32 logged it 14x back to back). Name WHO keeps them up —
            // own passive corpse flags vs. a follower camped on a corpse the
            // tank's give-up cannot clear (IsAnyPartyMemberLooting reads the
            // follower's flags, GiveUpCurrentLoot only clears our own).
            uint32 const over = now - lootYieldStart;
            if (over < DC_LOOT_YIELD_TIMEOUT_MS + 1500 || (over % 8000) < 1500)
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] loot-yield timed out after {}ms -> giving up on corpse, advancing "
                         "[avail={} can={} party={} holder={}]",
                         bot->GetName(), over,
                         AI_VALUE(bool, DcKey::Stock::HasAvailableLoot) ? 1 : 0,
                         AI_VALUE(bool, DcKey::Stock::CanLoot) ? 1 : 0,
                         partyLooting ? 1 : 0, lootHolder);
        }
        else
        {
            // DIAG(vis): run 32's tank sat in this yield for whole route TTLs
            // with the reason invisible (debug-only). ~1 line / 8s at info.
            uint32 const held = now - lootYieldStart;
            if ((held % 8000) < 1500)
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] advance yielding {}ms: loot in progress "
                         "[avail={} can={} party={} holder={}]",
                         bot->GetName(), held,
                         AI_VALUE(bool, DcKey::Stock::HasAvailableLoot) ? 1 : 0,
                         AI_VALUE(bool, DcKey::Stock::CanLoot) ? 1 : 0,
                         partyLooting ? 1 : 0, lootHolder);
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
            return Step::ReturnFalse;
        }
    }
    else
    {
        lootYieldStart = 0;  // not looting -> reset the commit timer
    }
    return Step::Continue;
}

// Between-pulls rest: yield so food/drink can run and stragglers catch up.
// The multiplier suppresses wander actions during the wait.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::TryBetweenPullsRest(AdvanceState const& /*st*/)
{
    // From the context, NOT st.appr: this gate runs in the PRE-ROUTE part of the
    // ladder, well before Execute fills st.appr, so st.appr is still null here
    // (TryLootYield above reaches its own state the same way).
    DcApproachState& appr =
        context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get();

    // Scripted-stage muster: the pull trigger is standing down while the party
    // drinks to the MUSTER floors — but the ordinary floors here are lower, so
    // this gate stayed green in the gap and advance walked the tank into the
    // very room the stage was about to pull (the muster-window scout face-pull:
    // tp-20260806-212646-1, 32 unplanned rotunda pulls, 19 run-fatal). The
    // symmetry rule below already says it: not ready to fight what is in front
    // of us means not ready to walk into it — the muster is that, one band
    // higher. Read-only latch view; the pull trigger stays the latch's owner.
    if (DcPartyState::IsScriptedMusterHolding(bot, context))
    {
        if (++appr.partyNotReadyTicks == 1)
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] advance yielding: scripted-stage muster holds the tank",
                      bot->GetName());
        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
        return Step::ReturnFalse;
    }

    if (IsBetweenPullsReady(bot, context))
    {
        appr.partyNotReadyTicks = 0;
        return Step::Continue;
    }

    // Debounce. Halting means StopBot(Hold), which cancels the escort spline, so
    // a single-tick trip (a follower momentarily at PartyMaxSpread while the tank
    // glides) would cost a full stop and re-issue — the micro-stutter. Ride out a
    // brief trip; a real wait trips every tick and halts within the budget.
    //
    // But ride out ONLY a glide that is already in flight. Falling through with no
    // escort running lets the ladder below LAUNCH A FRESH 35-38yd window, and that is
    // not "a few ticks of extra travel" — it is a five-second committed glide bought
    // with a three-tick grace. A party that flickers ready/not-ready every few seconds
    // then chains those windows into unlimited travel while the status panel says
    // "waiting". Live in tr-20260804-153254-2: the tank covered 97 yards — y=14.8 to
    // y=112.2, one gap of 34.87yd — entirely inside a "waiting on Toogo" yield, and
    // ran straight through the Sunblade Mage Guard it had voted LEEROY on one second
    // earlier at 20.1yd. The blocking-trash trigger was standing down on this very
    // same IsBetweenPullsReady gate for the whole window, so nothing engaged the pack;
    // it tagged two members and held the run in combat from 68yd back for 8 minutes.
    //
    // The gate must be symmetric: if we are not ready to FIGHT what is in front of us,
    // we are not ready to walk into it either. Riding an in-flight glide keeps the
    // anti-stutter property (DoRideLiveGlide claims the tick without re-issuing);
    // refusing to start a new one is what closes the ratchet.
    if (++appr.partyNotReadyTicks <= DC_PARTY_YIELD_DEBOUNCE_TICKS)
    {
        MotionMaster const* const mm = bot->GetMotionMaster();
        bool const glideInFlight =
            bot->movespline && !bot->movespline->Finalized();
        if (glideInFlight)
            return Step::Continue;   // ride it out; a real wait halts it next tick
        return Step::ReturnFalse;    // standing still already — do not commit a new window
    }

    // Name the limiting member/reason with the SAME thresholds the gate used, so
    // this line says whether it was spread, HP/mana, or the rest latch instead of
    // leaving all three indistinguishable behind "party not ready / resting".
    // DIAG(vis): run 32's tank stood in THIS gate for whole route TTLs with
    // the limiting member named only on debug. First halt past the debounce
    // logs at info, then every ~20 ticks.
    if (appr.partyNotReadyTicks == DC_PARTY_YIELD_DEBOUNCE_TICKS + 1 ||
        appr.partyNotReadyTicks % 20 == 0)
    {
        DcPartyState::SpreadGate const gate = DcPartyState::GetSpreadGate(bot, context);
        DcPartyState::RestGate const rest = DcPartyState::GetRestGate(bot, context);
        std::string const why = DcPartyState::DescribePartyNotReady(
            bot, rest.minHp, rest.minMp,
            gate.maxSpread, gate.anchor, gate.maxTankGap);
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] advance yielding after {} ticks: party not ready / resting{}",
                 bot->GetName(), appr.partyNotReadyTicks,
                 why.empty() ? " (resting)" : (" — " + why));
    }
    DcMovement::StopBot(bot, DcMovement::Stop::Hold);
    return Step::ReturnFalse;
}

// If this boss has no live spawn at all (and not even a corpse), stall so the
// player can `dc skip` instead of being forced to re-enable the mode. Bosses
// that legitimately despawn after kill are handled by the
// InstanceScript::GetBossState probe in NextDungeonBossValue — they never reach
// here.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::TryBossNotPresentStall(AdvanceState const& st)
{
    DungeonBossInfo const* next = st.next;

    // Travel objectives are not creatures — "not in the creature store" is their
    // normal state. Arrival is owned by DungeonClearAtObjectiveTrigger (which
    // outranks Advance); never stall the approach to an objective.
    if (next->kind != DungeonAnchorKind::Boss)
        return Step::Continue;

    // A boss a pending event must SUMMON (e.g. RFD's Tuten'kash via the gong) is
    // legitimately absent until the event runs — "not in the creature store" is
    // its normal pre-summon state, so don't paint "Blocked"/stall on it. The gong
    // event (relevance 31) handles the hold + rings; once the third ring summons
    // him this returns false and the normal not-present guard applies again.
    if (DcTargeting::HasPendingSummonEvent(bot, context, next->entry))
        return Step::Continue;

    if (!DcTargeting::IsCreaturePresentOnMap(bot, next->entry))
    {
        // "Not present" only means "not spawned" once we're close enough that
        // the boss's grid is certainly loaded. While we're still far, the grid
        // simply hasn't streamed in yet (see DC_BOSS_GRID_LOADED_RANGE). Hard-
        // stalling here froze the tank at the edge of a large room and it never
        // walked in to load the grid -> deadlock; and because this returns
        // before EnsureLongPath, with zero DC-channel output. Fall through and
        // let Advance path toward the boss's static spawn coords instead.
        float const distToBoss = bot->GetDistance(next->x, next->y, next->z);
        if (distToBoss <= DC_BOSS_GRID_LOADED_RANGE)
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] {} not in creature store at {:.0f}yd (<={:.0f}, grid "
                     "loaded) -> stalling: genuinely not spawned",
                     bot->GetName(), next->name, distToBoss, DC_BOSS_GRID_LOADED_RANGE);
            StallDungeonClear(botAI,
                "Can't reach " + next->name + ": not spawned on this map. Use 'dc skip' to move to the next boss.");
            return Step::ReturnFalse;
        }
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] {} not in creature store but {:.0f}yd away (>{:.0f}) "
                  "-> advancing to stream its grid in",
                  bot->GetName(), next->name, distToBoss, DC_BOSS_GRID_LOADED_RANGE);
        // fall through to the normal advance below
    }
    return Step::Continue;
}

// ==== Tier A — pre-path observation + effects (stuck / pursuit) ===========

// Position-based stuck bookkeeping. Samples world position every tick (so
// lastPos stays current) and, once the bot has gone DC_STUCK_TICK_LIMIT
// consecutive ticks without real displacement while supposedly moving, raises
// posStuckTicks — DecideApproach turns that into StuckRecover. Runs every tick
// regardless of the eventual verdict; the recovery EFFECT is in DoStuckRecover.
void DungeonClearAdvanceAction::FillStuckObs(AdvanceState& st, DungeonClearApproach::Observation& obs)
{
    DcApproachState& appr = *st.appr;
    uint32& rebuildAttempts = appr.rebuildAttempts;
    Position& lastPos = appr.lastPos;

    // Position-based stuck check via the shared route-glide watchdog. Sample the
    // current world position; a wedge is a tick that is moving yet barely shifted
    // since the previous one. The (0,0,0) lastPos is the not-yet-sampled sentinel
    // — no real dungeon map has a (0,0,0) walkable point — so the first tick reads
    // as "not moving" to the watchdog (no false wedge before a baseline exists).
    Position const cur(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    bool const lastPosValid =
        lastPos.m_positionX != 0.0f || lastPos.m_positionY != 0.0f || lastPos.m_positionZ != 0.0f;
    float const moved = lastPosValid ? cur.GetExactDist(lastPos) : 0.0f;
    bool const moving = lastPosValid && bot->isMoving();
    uint32 const posStuck =
        appr.routeGlideWatch.TickDisplacement(moving, moved, DC_STUCK_DISPLACEMENT);
    // Clearing the recovery ladder's counters needs PROGRESS, not motion. This used to
    // read `moving && moved >= DC_STUCK_DISPLACEMENT` — any single tick that displaced
    // half a yard re-armed both counters — which makes the whole escalation
    // (resnap -> forced rebuild -> navmesh nudge -> stall) unreachable for the exact
    // failure it exists to catch: a bot that is SHUTTLING. Live in
    // tr-20260804-153254-2, where the tank was dragged back and forth over 29yd of the
    // Kael'thas corridor by a stale combat flag for eight minutes: 87 of 87 posStuck
    // events logged `resnapAttempts=1 rebuildAttempts=0`, the ladder never left rung 1,
    // and the run hung silently instead of stalling with a `dc skip` prompt.
    //
    // Net progress is "did I get any nearer the objective than I have ever been on
    // this approach" — the closing-distance watchdog, re-armed on a boss change. A
    // shuttle can never satisfy it (its near end only ties the best, its far end is
    // worse), while genuinely resumed travel satisfies it on the very next tick. It
    // cannot mis-fire on a boss that WANDERS away either: the counters are only ever
    // INCREMENTED on a posStuck tick (moving with ~0 displacement), which a bot that is
    // actually travelling never produces.
    if (appr.recoveryProgressWatch.TickClosing(st.engageDist, DC_STUCK_DISPLACEMENT, getMSTime()))
    {
        rebuildAttempts = 0;
        appr.resnapAttempts = 0;
        appr.nudgeAttempts = 0;   // a nudge that bought ground costs nothing
    }

    // Per-tick advance telemetry — the three signals the spline-issue lines
    // can't show on their own: did the bot physically move since the last
    // Advance tick (posDelta), which generator is in control right now, and
    // is combat movement involved. Read against the timestamps of the
    // "spline issued" / "re-anchor" / "off-path" lines, this disambiguates
    // the pacing wedge: a posDelta ~0 right after a spline issuance means the
    // spline was issued but never travelled; a CHASE/FOLLOW gen here means
    // combat/leader movement is overriding the escort spline; an ESCORT gen
    // with posDelta ~0 means the spline launched but wedged against geometry.
    {
        MotionMaster* const tmm = bot->GetMotionMaster();
        MovementGeneratorType const gen =
            tmm ? tmm->GetCurrentMovementGeneratorType() : NULL_MOTION_TYPE;
        float const posDelta = lastPosValid ? cur.GetExactDist(lastPos) : -1.0f;
        // Throttle: this per-tick line exists to diagnose the pacing WEDGE, so log
        // it only when something looks wrong — barely moving (posDelta < 0.5yd)
        // while supposedly travelling — or on a 5s heartbeat. Healthy gliding
        // (~1.4yd/tick) no longer emits one line per tick.
        uint32 const nowMs = getMSTime();
        bool const suspicious = posDelta >= 0.0f && posDelta < 0.5f;
        if (suspicious || (nowMs - appr.lastTickLogMs) >= 5000)
        {
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] advance tick: posDelta={:.2f}yd moving={} gen={}({}) combat={}",
                      bot->GetName(), posDelta, bot->isMoving() ? 1 : 0,
                      MoveGenTypeName(gen), static_cast<uint32>(gen),
                      bot->IsInCombat() ? 1 : 0);
            appr.lastTickLogMs = nowMs;
        }
    }

    lastPos = cur;
    obs.posStuckTicks = posStuck;
}

// StuckRecover effect: halt the wedged glide and escalate
// Resnap -> rebuild -> navmesh-nudge -> stall.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::DoStuckRecover(AdvanceState& st)
{
    DungeonBossInfo const* next = st.next;
    DcApproachState& appr = *st.appr;
    uint32& rebuildAttempts = appr.rebuildAttempts;

    appr.routeGlideWatch.stuckTicks = 0;
    // Wedged and replanning — surface "recovering" to the status poll.
    SetPhase(context, "recovering");

    // The bot was moving but not progressing — a continuous-spline glide
    // wedged against geometry. Halt it so the recovery below re-issues
    // movement from a standstill instead of fighting the stuck spline.
    DcMovement::ResolveEscortConflict(bot);

    // First-line recovery: try a Resnap onto the existing polyline
    // (cheap; handles the "knocked sideways but path is still good"
    // case). On failure, invalidate the long-path cache and reset
    // the follower so the next tick rebuilds from the bot's current
    // position. Strides are short enough that a rebuild from here
    // usually picks a different sequence of stride endpoints and
    // routes around whatever was wedging us.
    // Resnap only proves the bot's position can be snapped ONTO the polyline, never
    // that it can walk ALONG it — so a bot wedged against geometry beside a route
    // that is still perfectly valid re-snaps successfully every single time. Left
    // uncounted, that pinned the ladder on its first rung forever (live: nine
    // consecutive "resnapped onto existing route (rebuildAttempts=0)" lines over
    // ~24s on the Durnholde terraces, reaching neither the rebuild nor the nudge,
    // until an unrelated rebuild happened to land and freed it). Count consecutive
    // resnaps and, once they stop helping, force the invalidate-and-rebuild path.
    // Real displacement clears the counter in FillStuckObs, so a transient drift
    // still gets the cheap rung.
    bool const allowResnap = appr.resnapAttempts < DC_MAX_RESNAP_ATTEMPTS;
    bool const resnapped = TriggerStrideRebuild(bot, context, appr, allowResnap);
    LOG_INFO("playerbots.dungeonclear",
             "[DC:{}] posStuck ({} ticks <{}yd) -> {} (resnapAttempts={} rebuildAttempts={})",
             bot->GetName(), DC_STUCK_TICK_LIMIT, DC_STUCK_DISPLACEMENT,
             resnapped ? "resnapped onto existing route" : "forcing rebuild",
             resnapped ? appr.resnapAttempts + 1u : 0u,
             rebuildAttempts + (resnapped ? 0u : 1u));
    if (resnapped)
    {
        // Resnap MAY have fixed us without burning a rebuild — leave the
        // rebuild-attempt counter alone so the navmesh-nudge escalation only
        // triggers on true geometric wedges, not on transient drifts.
        ++appr.resnapAttempts;
        return Step::ReturnFalse;
    }
    appr.resnapAttempts = 0;
    ++rebuildAttempts;

    // After three consecutive rebuilds without forward progress, try a
    // small navmesh-nudge: the bot may be on a poly the chunked builder
    // can't reach (off-corridor, layered geometry seam). The 5yd offset
    // probes are deliberately tiny so we don't significantly mis-position.
    if (rebuildAttempts >= 3)
    {
        rebuildAttempts = 0;
        // The nudge needs a budget of its own or it is not an escalation at all.
        // TryFarFromPolyRecovery succeeds trivially whenever the bot is ON the
        // navmesh — a 5yd probe from a walkable poly always paths — so it reset
        // rebuildAttempts and reported "recovered" on every pass, and the stall
        // beneath it could never be reached. Live in tr-20260818-073620-14: the
        // ladder climbed to its top rung nine times over nine minutes on the
        // Blackrock Spire ramp, nudged nine times, and never once stalled or
        // told the player. The tank paced the same 5yd box the whole time.
        //
        // Count consecutive nudges; the recovery-progress watchdog clears the
        // counter as soon as one actually buys ground (see FillStuckObs), so a
        // nudge that works still costs nothing.
        if (DC_ALLOW_RECOVERY_MOVES && appr.nudgeAttempts < DC_MAX_NUDGE_ATTEMPTS &&
            TryFarFromPolyRecovery(bot))
        {
            ++appr.nudgeAttempts;
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] stuck ladder: navmesh-nudge {} of {} near {}",
                     bot->GetName(), appr.nudgeAttempts, DC_MAX_NUDGE_ATTEMPTS, next->name);
            DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tRepathing around " + next->name + " \xe2\x80\x94 nudging onto the navmesh.");
            return Step::ReturnTrue;
        }
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] stuck ladder exhausted near {} ({} nudge(s) bought no ground) -> stalling",
                 bot->GetName(), next->name, appr.nudgeAttempts);
        appr.nudgeAttempts = 0;
        StallDungeonClear(botAI,
            "Stuck near " + next->name + " — not making forward progress. "
            "I'll try to clear nearby mobs; use 'dc skip' if it persists.");
        return Step::ReturnFalse;
    }
    return Step::ReturnFalse;
}

// Pursuit gate. Fills canPursue (a LIVE, visible boss past DC_ENGAGE_RANGE but
// within LOS and DC_DIRECT_PURSUIT_RANGE) and the give-up latch value. When the
// boss isn't pursuable this tick it resets the closing-distance watchdog so a
// later pursuit starts with a fresh baseline. The Pursue EFFECT is DoPursue.
void DungeonClearAdvanceAction::FillPursuitObs(AdvanceState& st, DungeonClearApproach::Observation& obs)
{
    Creature* const liveBoss = st.liveBoss;
    float const engageDist = st.engageDist;
    DcApproachState& appr = *st.appr;

    bool const canPursue =
        liveBoss && engageDist <= DC_DIRECT_PURSUIT_RANGE && bot->IsWithinLOSInMap(liveBoss);
    if (!canPursue)
        appr.pursuitWatch.Reset();  // fresh closing baseline for a later pursuit

    obs.canPursue = canPursue;
    // Latch = consecutive ticks that failed to close on the boss (nav F11). Read
    // from last tick's DoPursue sample; DecideApproach selects Pursue while it is
    // under the limit.
    obs.pursuitFailTicks = appr.pursuitWatch.stuckTicks;
}

// Pursue effect: walk straight at the boss's current position with a per-tick
// re-path (MoveTo dedups, so a roughly-stationary boss gets one smooth glide; a
// wandering boss is re-targeted as it moves — the same way combat chase tracks a
// target). This is what stops the tank parking at the static spawn anchor and
// waiting for the boss to wander back.
//
// The give-up latch is now the shared closing-distance watchdog (nav F11): a tick
// that fails to get DC_STUCK_DISPLACEMENT nearer the boss is a no-progress tick.
// This subsumes the old MoveTo-refusal counter (a frozen bot — Z->INVALID_HEIGHT,
// or a route past the raw 74-hop cap — never moves, so it never closes) AND now
// also catches a bot that IS moving but not gaining (bee-line grinding a corner,
// LOS-flicker steering it sideways) — the non-moving/ not-closing blind spot the
// old counter couldn't see. After DC_PURSUIT_FAIL_LIMIT no-closing ticks this
// returns Step::Continue: pursuit abdicates and Execute hands the tick to the
// wall-screened long-path (LongRangePathfinder targets the same live boss, no hop
// cap). The latch stays closed until engage range / boss change so the long-path
// can travel.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::DoPursue(AdvanceState& st)
{
    DungeonBossInfo const* next = st.next;
    float const bossX = st.bossX, bossY = st.bossY, bossZ = st.bossZ;
    float const engageDist = st.engageDist;
    DcApproachState& appr = *st.appr;

    // Sample closing progress BEFORE issuing this tick's move (engageDist is
    // start-of-tick, reflecting prior ticks' movement). The first pursuit tick
    // arms the baseline and reads as progress.
    appr.pursuitWatch.TickClosing(engageDist, DC_STUCK_DISPLACEMENT, getMSTime());
    if (appr.pursuitWatch.stuckTicks >= DC_PURSUIT_FAIL_LIMIT)
    {
        // Not closing on the boss for the whole budget — a doomed bee-line. Hand
        // off without issuing another (the long-path drives from here).
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] direct pursuit of {} not closing ({:.0f}yd, {} ticks) -> "
                 "long-path fallback (latched until engage range / boss change)",
                 bot->GetName(), next->name, engageDist, DC_PURSUIT_FAIL_LIMIT);
        return Step::Continue;
    }

    // DcMoveTo drops any stale long-path escort glide (so it doesn't keep driving
    // the bot toward the spawn anchor) before steering at the live boss.
    bool const chasing = DcMoveTo(next->mapId, bossX, bossY, bossZ,
                                /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                                /*exact_waypoint*/ false, MovementPriority::MOVEMENT_NORMAL);
    bool const moveAlive = chasing || bot->isMoving() ||
                           IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL);

    appr.stuckCount = 0;
    ClearStall(context);
    SetPhase(context, "pursuing");
    LOG_DEBUG("playerbots.dungeonclear",
              "[DC:{}] pursuing live {} at {:.0f}yd (LOS, noClose={}/{}) -> MoveTo {}",
              bot->GetName(), next->name, engageDist,
              appr.pursuitWatch.stuckTicks, DC_PURSUIT_FAIL_LIMIT,
              chasing ? "issued" : (moveAlive ? "in flight" : "noop"));
    // Own the tick when a move is alive; else yield (a move that is in flight but
    // wedging in place is caught by the posStuck/route-glide watchdog above).
    return moveAlive ? Step::ReturnTrue : Step::ReturnFalse;
}

// ==== Tier B — path-level observation + effects (unreachable / off-path) ===

// Fills the long-path reachability fields. When the route is unreachable it also
// computes the escape inputs (async-in-flight, off-mesh wedge, and — gated on
// SwimEnable — whether water lies between) so the captured verdict distinguishes
// PlanRouteWait / FarFromPolyRecover / Swim / Stall honestly; those raycasts run
// only on the rare unreachable tick. When the route IS reachable it maintains the
// off-path tick counter (IsOffPath side effect) and, past the tick budget, tries
// a cheap Resnap — obs.offPath is set ONLY when that Resnap fails (a rebuild is
// required); a successful Resnap keeps the cursor on the route and falls through
// to the hop rungs, exactly as the old ladder's continue did.
void DungeonClearAdvanceAction::FillPathObs(AdvanceState& st, DungeonClearApproach::Observation& obs)
{
    float const bossX = st.bossX, bossY = st.bossY, bossZ = st.bossZ;
    DcApproachState& appr = *st.appr;
    ChunkedPathfinder::Result const& path = *st.path;
    DungeonFollowerState& follower = *st.follower;

    obs.pathReachable = path.reachable;
    obs.allowRecoveryMoves = DC_ALLOW_RECOVERY_MOVES;

    if (!path.reachable)
    {
        obs.asyncPending = appr.pendingPathJob != 0;
        obs.startFarFromPoly = path.startFarFromPoly;
        // Water is only consulted when async isn't pending and the off-mesh nudge
        // isn't taken (DecideApproach's unreachable ladder). Compute it only there.
        if (!obs.asyncPending && !(obs.allowRecoveryMoves && obs.startFarFromPoly))
            obs.waterBetween =
                DcSettings::GetBool(bot, "SwimEnable") &&
                SwimPathfinder::WaterBetween(
                    bot, G3D::Vector3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()),
                    G3D::Vector3(bossX, bossY, bossZ));
        return;  // off-path is meaningless while unreachable
    }

    if (DungeonPathFollower::IsOffPath(bot, path, follower) &&
        follower.offPathTicks >= DungeonPathFollower::OFF_PATH_TICK_LIMIT)
    {
        st.offPathTicks = follower.offPathTicks;  // Resnap zeroes it; carry for the log
        if (!DungeonPathFollower::Resnap(bot, path, follower))
            obs.offPath = true;
        else
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] off-path {} ticks -> Resnapped to seg {} pt {}",
                      bot->GetName(), st.offPathTicks, follower.segmentIdx, follower.pointIdx);
    }
}

// Unreachable effect. Distinguishes an EXPECTED empty path (async build still in
// flight — hold quietly) from a genuine failure, attempts an off-mesh nudge and a
// swim, then stalls for the stalled-fallback / `dc skip`. Its internal branching
// mirrors DecideApproach's unreachable ladder, so the effect and the captured
// verdict agree.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::DoLongPathUnreachable(AdvanceState& st)
{
    DungeonBossInfo const* next = st.next;
    float const bossX = st.bossX, bossY = st.bossY, bossZ = st.bossZ;
    DcApproachState& appr = *st.appr;
    ChunkedPathfinder::Result const& path = *st.path;

    // Async pathfinding (DungeonClear.AsyncPathfinding): a build is still in
    // flight — almost always right after a boss change, where EnsureLongPath
    // cleared the cache and handed the heavy A* to the worker. The empty
    // path is EXPECTED here, not a routing failure, so hold position quietly
    // and wait (the result lands within a tick or a few) instead of crying
    // "no navigable route" to the party. Mirrors the between-pulls rest
    // yield: no stall reason set, so the stalled-fallback never fires; the
    // multiplier suppresses wander while we wait.
    if (appr.pendingPathJob != 0)
    {
        SetPhase(context, "planning route");
        DcMovement::StopBot(bot, DcMovement::Stop::Soft);
        return Step::ReturnFalse;
    }

    // Bot wedged off the navmesh — try a small offset to land on a
    // walkable poly. Common cause: stuck-teleport recovery landed
    // on a ledge that pad's mmap tile-boundary; another cause is
    // bot getting knocked back onto unwalkable geometry.
    if (DC_ALLOW_RECOVERY_MOVES && path.startFarFromPoly)
    {
        if (TryFarFromPolyRecovery(bot))
        {
            // Don't say anything in party chat — this should be
            // invisible recovery. Force a rebuild so the next tick
            // picks up the new (hopefully on-mesh) position.
            SetPhase(context, "recovering");
            appr.longPathExpiresMs = 0;
            return Step::ReturnTrue;
        }
    }

    // No navmesh route at all. Before stalling, try a swim: the target may
    // sit behind a submerged tunnel the navmesh can't span (only a surface
    // sheet exists over deep water). Gated on water lying between, so a
    // genuinely land-locked failure still falls through to the stall.
    if (TryBeginSwim(bot, context, next->entry, bossX, bossY, bossZ))
    {
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] no navmesh route to {} -> swimming", bot->GetName(), next->name);
        SetPhase(context, "swimming");
        return Step::ReturnTrue;
    }

    // Short-range mesh-blind push. The tile-wise long-range router refuses
    // corridors the stock PathGenerator can still thread - live
    // (tr-20260824-091007-1, 5/8): the Deadmines ship deck holds Greenskin
    // and VanCleef at Z 40 over the Z 12 quay, the gangplank is walkable
    // in-game, and the chunked builder reports "no navigable route". Within
    // a short leash, hand the move to stock MoveTo and let its own A*
    // climb; the leash and dz gates keep a genuinely unreachable target
    // falling through to the stall exactly as before.
    {
        float const airDist = bot->GetDistance(bossX, bossY, bossZ);
        if (airDist < 60.0f && std::fabs(bossZ - bot->GetPositionZ()) < 40.0f)
        {
            if (DcMoveTo(next->mapId, bossX, bossY, bossZ))
            {
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] no long-range route to {} at {:.0f}yd -> "
                         "short-range stock push",
                         bot->GetName(), next->name, airDist);
                SetPhase(context, "moving");
                return Step::ReturnTrue;
            }
        }
    }

    // The chunked builder couldn't produce any segment. Failure
    // reason is carried through from PathGenerator's path type
    // (NOPATH, FARFROMPOLY_START, etc.). The stalled-fallback action
    // takes over from here, picking off whatever reachable hostiles
    // remain to potentially unblock the route.
    StallDungeonClear(botAI,
        "Can't path to " + next->name + ": " +
        (path.failureReason.empty() ? "no navigable route" : path.failureReason) +
        ". I'll try to clear intervening mobs; if that doesn't help, 'dc skip' to move on.");
    return Step::ReturnFalse;
}

// OffPathRebuild effect: the off-path Resnap in FillPathObs failed (drift too
// large to index-jump). Halt any stale spline glide so the rebuilt path isn't
// shadowed by the old route next tick, and reset the follower for a fresh build.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::DoOffPathRebuild(AdvanceState& st)
{
    DcApproachState& appr = *st.appr;
    DungeonFollowerState& follower = *st.follower;

    LOG_INFO("playerbots.dungeonclear",
             "[DC:{}] off-path {} ticks, Resnap FAILED (>{}yd) -> rebuild",
             bot->GetName(), st.offPathTicks, DungeonPathFollower::RESNAP_RADIUS);
    SetPhase(context, "recovering");
    DcMovement::ResolveEscortConflict(bot);
    appr.longPathExpiresMs = 0;
    follower = DungeonFollowerState{};
    return Step::ReturnFalse;
}

// Post-combat re-anchor. NextHop only fast-forwards the cursor past points the
// tank passed within POINT_REACHED; a trash chase displaces it well off those
// points — often FORWARD along the route — leaving the cursor stale and behind,
// so the tank would walk backward to it. If the next hop is implausibly far for
// normal gliding, re-anchor onto the nearest visible route point (Resnap is
// LOS-gated, so it won't snap across a wall) and re-fetch the hop. This never
// terminates the tick; it only mutates the cursor/hop for the phases below.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::TryReanchorStaleCursor(AdvanceState& st)
{
    ChunkedPathfinder::Result const& path = *st.path;
    DungeonFollowerState& follower = *st.follower;
    DungeonPathFollower::Hop& hop = st.hop;

    if (hop.isDone || hop.isJump)
        return Step::Continue;

    // Staleness is an ALONG-ROUTE question, so measure it in plan view — the
    // same geometry NextHop's arrival test uses. A 3D distance here charges the
    // bot for navmesh Z float, and on a ramp that float is 2-3.5yd: enough to
    // push a hop that is 10yd away horizontally over the 12yd limit and fire a
    // re-anchor every single tick. Each re-anchor is a Resnap + a fresh
    // NextHop, and the resulting cursor churn is what turned a ramp into the
    // 444-event resnap storm in tr-20260818-073620-14. Vertical mismatch is a
    // real condition, but it is a FLOOR problem with its own handling below,
    // not a reason to re-anchor.
    float const staleDist = bot->GetExactDist2d(hop.point.x, hop.point.y);

    // DIRECTION, not just distance. The distance rule alone leaves a hole the
    // width of itself: the off-line rejoin below fires at OFF_PATH_THRESHOLD
    // (6yd of PERPENDICULAR deviation) and walks the bot to hop.point, but a bot
    // carried straight PAST its cursor along the same corridor reads a small
    // perpendicular deviation while its hop sits behind it. Between 6 and 12yd of
    // along-track staleness nothing caught that, so the rejoin issued a MoveTo
    // BACKWARD — glide forward, cursor lags, walk back to it, re-anchor, glide
    // forward again. That is the short back-and-forth the tank does on approach,
    // and it is the exact failure DC_REANCHOR_DISTANCE's own comment describes;
    // it was simply gated too high to catch it.
    //
    // A hop behind the bot is never worth walking to at ANY distance — the route
    // is one-way — so direction re-anchors on its own, no threshold.
    bool const behind = DungeonPathFollower::HopIsBehind(bot, path, follower, hop);
    if (staleDist > DC_REANCHOR_DISTANCE || behind)
    {
        bool const reanchored = DungeonPathFollower::Resnap(bot, path, follower);
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] re-anchor: next hop {:.1f}yd (limit {}yd, behind={}) -> {}",
                  bot->GetName(), staleDist, DC_REANCHOR_DISTANCE, behind,
                  reanchored ? "Resnapped + refetched hop" : "Resnap failed, falling through");
        if (reanchored)
            hop = DungeonPathFollower::NextHop(bot, path, follower);
    }
    return Step::Continue;
}

// ==== Tier C — hop-cluster observation + effects ==========================

// Fills the hop-cluster fields in ladder order (hopDone > jump > ride > offLine
// > window), returning as soon as an owning rung's field is set so the costlier
// probes below it (RouteDeviation, BuildSplineWindow) are skipped exactly as the
// old short-circuiting ladder skipped them. The escalation counter is advanced
// here (the one per-tick side effect); the swim-vs-stall water probe runs only
// once the final-approach budget is spent, so the captured Swim/Stall verdict is
// honest without a per-tick raycast. RouteDeviation and the built spline window
// are carried in st for the matching effect handlers.
void DungeonClearAdvanceAction::FillHopObs(AdvanceState& st, DungeonClearApproach::Observation& obs)
{
    float const bossX = st.bossX, bossY = st.bossY, bossZ = st.bossZ;
    float const engageDist = st.engageDist, engageRange = st.engageRange;
    DcApproachState& appr = *st.appr;
    ChunkedPathfinder::Result const& path = *st.path;
    DungeonFollowerState& follower = *st.follower;
    DungeonPathFollower::Hop const& hop = st.hop;

    obs.hopDone = hop.isDone;
    obs.hopIsJump = hop.isJump;

    if (hop.isDone)
    {
        // Route completed. Inside engage range this is a benign rebuild-and-yield
        // and OnEnteredEngageRange already reset the watchdog; only when we're
        // still SHORT of the boss does the dead-end escalation advance (the silent
        // forever-loop guard). Via the shared closing-distance watchdog (nav F11):
        // each hop-done tick that fails to get DC_STUCK_DISPLACEMENT nearer the
        // boss is a no-progress tick. This is more patient than the old pure tick
        // counter — a final-approach MoveTo that IS slowly closing keeps its
        // budget, and only a genuine dead-end (0-point path, bot not moving) or a
        // boss stepping out of reach exhausts it. Match the old ordering: it only
        // advances after the engageDist<engageRange case is ruled out.
        if (engageDist >= engageRange)
        {
            appr.finalApproachWatch.TickClosing(engageDist, DC_STUCK_DISPLACEMENT, getMSTime());
            // Water escape (Swim vs Stall) is consulted only once the budget is
            // spent; probe it there so the captured verdict is honest, gated on
            // SwimEnable so it matches the effect when swimming is off.
            if (appr.finalApproachWatch.stuckTicks >= obs.doneNotEngagedLimit)
                obs.waterBetween =
                    DcSettings::GetBool(bot, "SwimEnable") &&
                    SwimPathfinder::WaterBetween(
                        bot, G3D::Vector3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()),
                        G3D::Vector3(bossX, bossY, bossZ));
        }
        obs.doneNotEngagedTicks = appr.finalApproachWatch.stuckTicks;
        return;  // hopDone outranks jump / ride / off-line / window
    }

    if (hop.isJump)
        return;  // jump outranks ride / off-line / window

    // A healthy in-flight continuous-spline glide (ESCORT generator active AND
    // moving) just rides — deliberately NOT IsWaitingForLastMove, whose
    // window-sized delay was the mid-path "frozen for seconds" freeze.
    MotionMaster* const mm = bot->GetMotionMaster();
    obs.splineRunning =
        bot->movespline && !bot->movespline->Finalized() && bot->isMoving();
    if (obs.splineRunning)
    {
        // Mid-glide hazard interrupt: the window cap (AdvanceWindowYards) still
        // leaves a blind hop the length of the window, and a PATROL can walk
        // into it after launch. Probe the remaining window against the bystander
        // avoid-spheres on a throttle; on a hit, halt the glide and fall through
        // so this same tick re-plans (the rebuilt window then truncates at the
        // hazard below). Interrupt only for something NEW — the sphere behind
        // the last interrupt is latched and skipped, so the tank can never
        // ping-pong stop/launch against a pack it already decided to route
        // around. Gated so normal difficulty pays nothing.
        //
        // The probe asks the SAME question the truncation below answers
        // (TruncateWindowAtSphere), not merely "is a sphere violated": halting a
        // healthy glide for a hazard the re-plan will then decline to truncate
        // for is a dead stop bought for nothing, and back-to-back dead stops are
        // exactly the step-pause the tank was reported doing on approach.
        bool interrupt = false;
        uint32 const nowMs = getMSTime();
        if (DcSettings::GetFloat(bot, "AdvanceWindowYards") > 0.0f &&
            DcSettings::GetBool(bot, "PullEnRouteAvoid") &&
            nowMs - appr.glideHazardProbeMs >= DC_GLIDE_HAZARD_PROBE_MS)
        {
            appr.glideHazardProbeMs = nowMs;
            std::vector<G3D::Vector3> remaining =
                DungeonPathFollower::BuildSplineWindow(
                    bot, path, follower, DcSettings::GetFloat(bot, "AdvanceWindowYards"));
            if (remaining.size() >= 2)
            {
                G3D::Vector3 const& end = remaining.back();
                std::vector<DcEngageGeometry::AvoidSphere> const spheres =
                    DcEngageGeometry::BystanderSpheres(
                        bot, Position(end.x, end.y, end.z, 0.0f),
                        PullDestinationPack(botAI, context));
                size_t legIdx = 0;
                int idx = -1;
                bool const honoured = DcEngageGeometry::TruncateWindowAtSphere(
                    remaining, spheres, DC_AVOID_MIN_GLIDE, DC_AVOID_EDGE_BACKOFF,
                    legIdx, idx);
                if (honoured &&
                    spheres[static_cast<size_t>(idx)].guid != appr.glideHazardIgnore)
                {
                    appr.glideHazardIgnore = spheres[static_cast<size_t>(idx)].guid;
                    interrupt = true;
                    DC_PULL_INFO("[DC:{}] mid-glide hazard: sphere {} (r={:.1f}) "
                                 "entered the committed window at leg {} -> halting "
                                 "glide to re-plan",
                                 bot->GetName(),
                                 spheres[static_cast<size_t>(idx)].guid.ToString(),
                                 spheres[static_cast<size_t>(idx)].r, legIdx);
                }
            }
        }
        if (!interrupt)
            return;  // ride outranks off-line / window
        // Escort-aware halt is mandatory: Unit::StopMoving() does not cancel a
        // launched escort spline (see DcMovement.h) — StopMovingOnCurrentPos()
        // does. Getting this wrong reproduces the dc-stop-escort-spline bug class.
        bot->StopMovingOnCurrentPos();
        obs.splineRunning = false;  // fall through to re-plan this tick
    }

    // Off the line? 2D deviation OR — the module's documented metric-mismatch
    // repeat offender — a vertical corridor-band mismatch (RouteDeviation is
    // 2D-only, so a bot knocked onto a different floor directly under/over its
    // route reads deviation ~= 0 and would let a straight escort spline launch
    // through the floor/ceiling).
    st.routeDeviation = DungeonPathFollower::RouteDeviation(bot, path, follower);
    std::optional<G3D::Vector3> const curPt = DungeonPathFollower::CurrentPoint(path, follower);
    bool const vertOff = curPt.has_value() &&
                         std::fabs(bot->GetPositionZ() - curPt->z) > DC_CORRIDOR_Z_BAND;
    obs.offLine = st.routeDeviation > DungeonPathFollower::OFF_PATH_THRESHOLD || vertOff;
    if (obs.offLine)
        return;  // off-line outranks window

    // Normal case: is a >=2-point spline window available? Build it once here and
    // carry it into DoIssueSplineWindow so the launch reuses this exact window.
    // The window length is capped (AdvanceWindowYards; heroic 35 = one
    // DC_CORRIDOR_LOOKAHEAD) so the glide can never outrun the blocking-trash
    // detector between two evaluations; 0 = unbounded, the historical behaviour.
    st.splineWindow = DungeonPathFollower::BuildSplineWindow(
        bot, path, follower, DcSettings::GetFloat(bot, "AdvanceWindowYards"));

    // En-route avoidance on the glide itself (PullEnRouteAvoid): truncate the
    // window at the first bystander aggro sphere any of its legs violates, so
    // the tank glides up to the hazard's THRESHOLD and stops there out of
    // combat — whatever should own the pack (the blocking-trash trigger, or the
    // pull pipeline once it is inside the detection band) then gets a clean tick
    // to do so. Deliberately truncate, not detour: a bend in the long route
    // would need its own navmesh reachability check per bend (the
    // under-the-map seam class of bug); truncation is safe, cheap, and
    // composes.
    //
    // The pack the PULL PIPELINE is walking to is excluded from the sphere set.
    // It is the destination, not a bystander: pull-idle above the commit range
    // yields the tick to this glide precisely so the tank can close on it
    // ("glide closer before committing"), and then the glide refused to move
    // because the destination was in the way. Live Sethekk heroic caught it
    // exactly — a window truncated on the very pack whose pull verdict had just
    // been logged one line earlier.
    //
    // TruncateWindowAtSphere owns the rest of the shaping: it stops the window
    // ON the threshold rather than at the last vertex before it, and it DECLINES
    // a truncation that would leave less than DC_AVOID_MIN_GLIDE of travel —
    // because a sub-2-point window is not a stop, it is the per-point MoveTo
    // crawl, which is both slower than gliding through and no safer.
    if (st.splineWindow.size() >= 2 && DcSettings::GetBool(bot, "PullEnRouteAvoid"))
    {
        G3D::Vector3 const& end = st.splineWindow.back();
        std::vector<DcEngageGeometry::AvoidSphere> const spheres =
            DcEngageGeometry::BystanderSpheres(
                bot, Position(end.x, end.y, end.z, 0.0f),
                PullDestinationPack(botAI, context));
        size_t legIdx = 0;
        int idx = -1;
        size_t const before = st.splineWindow.size();
        bool const honoured = DcEngageGeometry::TruncateWindowAtSphere(
            st.splineWindow, spheres, DC_AVOID_MIN_GLIDE, DC_AVOID_EDGE_BACKOFF,
            legIdx, idx);
        if (idx >= 0)
            DC_PULL_DEBUG("[DC:{}] advance window: leg {} violates bystander "
                          "sphere {} (r={:.1f}) -> {} {} -> {} pts",
                          bot->GetName(), legIdx,
                          spheres[static_cast<size_t>(idx)].guid.ToString(),
                          spheres[static_cast<size_t>(idx)].r,
                          honoured ? "truncating" : "too close to honour, gliding",
                          before, st.splineWindow.size());
    }
    obs.haveSplineWindow = st.splineWindow.size() >= 2;
}

// The long-path completed (cursor reached the polyline end). RebuildAndYield is
// the benign already-in-range case; FinalApproach walks a few straight-line
// MoveTo attempts at the boss; Swim/Stall are the spent-budget dead-end escape
// (the water-gate swim, else a stall for `dc skip`). The escalation counter was
// already advanced in FillHopObs; this handler consumes the verdict.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::DoHopDoneEscalation(
    AdvanceState& st, DungeonClearApproach::Verdict v)
{
    DungeonBossInfo const* next = st.next;
    float const bossX = st.bossX, bossY = st.bossY, bossZ = st.bossZ;
    float const engageDist = st.engageDist, engageRange = st.engageRange;
    DcApproachState& appr = *st.appr;
    DungeonFollowerState& follower = *st.follower;

    if (v == DungeonClearApproach::Verdict::RebuildAndYield)
    {
        // Already within engage range — a benign "anchored hops were still
        // pending at the top" case; rebuild and let the engage hold take over.
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] reached end of path polyline (seg {}) -> forcing rebuild next tick",
                 bot->GetName(), follower.segmentIdx);
        appr.longPathExpiresMs = 0;
        return Step::ReturnFalse;
    }

    // INCOMPLETE-ROUTE HANDOFF. The chunk builder can return a partial route
    // that simply stops short of a boss it cannot reach - live: the Deadmines
    // ship deck (Greenskin/VanCleef ~39y above the quay) produced
    // "complete=0, 42 pts" forever, the party walked that stub to the water's
    // edge and the route was rebuilt every TTL for the rest of the window.
    // Stock's PathGenerator uses the same navmesh but its own search, and it
    // honours Detour off-mesh connections the chunked walker never consults,
    // so on a short hop it can close what the tile-wise builder refuses. This
    // is a handoff, not a shortcut: if stock cannot path it either, nothing
    // happens and the stall stands.
    if (st.path && !st.path->complete)
    {
        float const flat = bot->GetExactDist2d(bossX, bossY);
        // DIAG(mesh), distance-independent: an incomplete route is the whole
        // question, and the party often follows its stub far away from the
        // boss (live: 365yd), so a probe gated on proximity never runs.
        {
            static uint32 s_lastFarProbeMs = 0;
            uint32 const nowFar2 = getMSTime();
            if (nowFar2 - s_lastFarProbeMs > 30000 && bot->FindMap())
            {
                s_lastFarProbeMs = nowFar2;
                NavmeshSnap::Result const at =
                    NavmeshSnap::SnapColumn(bot->FindMap(), bossX, bossY, bossZ, 25.0f, 10.0f);
                // Plain {} only - this core's StringFormat does not implement
                // format specs, so {:.1f} survives verbatim into the journal
                // (every garbled diagnostic in this module has that cause).
                // Round to int here instead.
                LOG_INFO("playerbots.dungeonclear",
                         "[DC-MESH] {} unreachable: meshAtBoss={} snappedZ={} bossZ={} botDist={} botZ={}",
                         next->name, at.ok ? 1 : 0,
                         int(at.ok ? at.z : 0.0f), int(bossZ), int(flat),
                         int(bot->GetPositionZ()));
            }
        }
        // 120yd, not 60: the stub route parks the party wherever it ran out,
        // and live that was 60.8yd from Greenskin - just past the old gate.
        // ...and only once the route stops PAYING. isMoving() was the wrong
        // question twice over: firing while the stub is still being walked
        // cancels the glide every 5s and freezes the run (live: 114yd from
        // Gilnid on a healthy route), but demanding a full stop never fires
        // at all where it matters - on the Deadmines deck the party paces
        // its dead-end stub forever, moving the whole time and arriving
        // nowhere. Net ground covered is the honest measure.
        DcApproachState& happr = *st.appr;
        uint32 const nowHand = getMSTime();
        float const toBoss = bot->GetExactDist(bossX, bossY, bossZ);
        if (happr.handoffSinceMs == 0 || toBoss < happr.handoffBestDist - 3.0f)
        {
            happr.handoffBestDist = toBoss;
            happr.handoffSinceMs = nowHand ? nowHand : 1;
        }
        bool const routeNotPaying =
            happr.handoffSinceMs != 0 && getMSTimeDiff(happr.handoffSinceMs, nowHand) > 25000;
        if (flat < 120.0f && routeNotPaying)
        {
            static uint32 s_lastHandoffMs = 0;
            uint32 const nowHandoff = getMSTime();
            if (nowHandoff - s_lastHandoffMs > 5000)
            {
                s_lastHandoffMs = nowHandoff;
                // Ask for a REACHABLE point near the boss first. The boss's
                // own spot may be exactly the unreachable one (live: 13yd
                // from Greenskin but 12.8y below him - the missing stairs to
                // the upper deck sit between). FindStandoffPoint rings the
                // boss and returns the first candidate that snaps to the
                // mesh, has line of sight and is PATHFIND_NORMAL-reachable
                // from here; walking there is ordinary movement and the pull
                // happens from that spot.
                // DIAG(mesh): is there ANY navmesh at the boss at all? A hit
                // at his own Z means the deck is meshed and only the link is
                // missing (off-mesh connection / anchors can fix that); a
                // miss, or a hit far below him, means the geometry was never
                // meshed and only regenerating map 36 helps.
                {
                    static uint32 s_lastMeshProbeMs = 0;
                    uint32 const nowProbe = getMSTime();
                    if (nowProbe - s_lastMeshProbeMs > 30000 && bot->FindMap())
                    {
                        s_lastMeshProbeMs = nowProbe;
                        NavmeshSnap::Result const at =
                            NavmeshSnap::SnapColumn(bot->FindMap(), bossX, bossY, bossZ, 25.0f, 10.0f);
                        LOG_INFO("playerbots.dungeonclear",
                                 "[DC-MESH] probe at {}: ok={} snappedZ={:.1f} bossZ={:.1f}",
                                 next->name, at.ok ? 1 : 0, at.ok ? at.z : 0.0f, bossZ);
                    }
                }

                float sx = 0.0f, sy = 0.0f, sz = 0.0f;
                Position const bossPos(bossX, bossY, bossZ, 0.0f);
                bool const haveSpot = bot->FindMap() &&
                    FindStandoffPoint(bot->FindMap(), bossPos, /*ringRadius*/ 14.0f,
                                      /*maxRadius*/ 30.0f, sx, sy, sz);
                if (haveSpot && DcMoveTo(next->mapId, sx, sy, sz))
                {
                    LOG_INFO("playerbots.dungeonclear",
                             "[DC:{}] incomplete route to {} at {:.0f}yd (dz {:.0f}) "
                             "-> walking to a reachable spot {:.0f}yd off it",
                             bot->GetName(), next->name, flat, bossZ - bot->GetPositionZ(),
                             bot->GetExactDist(sx, sy, sz));
                    SetPhase(context, "pursuing");
                    return Step::ReturnTrue;
                }
                if (DcMoveTo(next->mapId, bossX, bossY, bossZ))
                {
                    LOG_INFO("playerbots.dungeonclear",
                             "[DC:{}] incomplete route to {} at {:.0f}yd (dz {:.0f}) "
                             "-> handing the last leg to stock pathfinding",
                             bot->GetName(), next->name, flat, bossZ - bot->GetPositionZ());
                    SetPhase(context, "pursuing");
                    return Step::ReturnTrue;
                }
            }
        }
    }

    if (v == DungeonClearApproach::Verdict::FinalApproach)
    {
        // The route dead-ends short of the boss. Rebuilding just produces the
        // same 0-point path (we sit on its terminal poly) and, since the bot
        // isn't moving, posStuck never escalates — the silent forever-loop. Try
        // a straight final-approach MoveTo: PathGenerator may close a few yards
        // the chunk builder gave up on, or the boss may have wandered into reach.
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] path ends {:.0f}yd short of {} (>{:.0f}, attempt {}/{}) "
                 "-> final-approach MoveTo",
                 bot->GetName(), engageDist, next->name, engageRange,
                 appr.finalApproachWatch.stuckTicks, DC_DONE_NOT_ENGAGED_LIMIT);
        bool const pushing = DcMoveTo(next->mapId, bossX, bossY, bossZ,
                                    /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                                    /*exact_waypoint*/ false, MovementPriority::MOVEMENT_NORMAL);
        SetPhase(context, "pursuing");
        appr.longPathExpiresMs = 0;
        return pushing ? Step::ReturnTrue : Step::ReturnFalse;
    }

    // Budget spent (Swim or Stall). Reset the watchdog and take the water-gate
    // swim if one exists (submerged tunnel the surface-sheet navmesh can't
    // descend into), else stall for `dc skip`.
    appr.finalApproachWatch.Reset();
    if (TryBeginSwim(bot, context, next->entry, bossX, bossY, bossZ))
    {
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] route dead-ends short of {} -> swimming the rest",
                 bot->GetName(), next->name);
        SetPhase(context, "swimming");
        return Step::ReturnTrue;
    }

    LOG_INFO("playerbots.dungeonclear",
             "[DC:{}] {} unreachable: route dead-ends {:.0f}yd short after {} approach "
             "attempts -> stalling",
             bot->GetName(), next->name, engageDist, DC_DONE_NOT_ENGAGED_LIMIT);
    StallDungeonClear(botAI,
        "Can't reach " + next->name + ": the route dead-ends short of it "
        "(likely on a ledge or across a gap the navmesh doesn't span). "
        "Use 'dc skip' to move to the next boss.");
    return Step::ReturnFalse;
}

// Anchor-declared jumps: use JumpTo (MotionMaster::MoveJump) instead of MoveTo.
// Required for dungeon drop-downs the mmap doesn't model (OK upper->lower,
// Pinnacle Skadi catwalk, AN spider tunnels, etc.).
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::DoJumpLeg(AdvanceState& st)
{
    DungeonBossInfo const* next = st.next;
    DungeonPathFollower::Hop const& hop = st.hop;

    bool const jumped = JumpTo(next->mapId, hop.point.x, hop.point.y, hop.point.z,
                               MovementPriority::MOVEMENT_NORMAL);
    LOG_DEBUG("playerbots.dungeonclear",
              "[DC:{}] jump leg -> ({:.1f},{:.1f},{:.1f}) {}",
              bot->GetName(), hop.point.x, hop.point.y, hop.point.z,
              jumped ? "issued" : "JumpTo refused (higher-prio move in flight), retry");
    if (!jumped)
    {
        // JumpTo can return false if a previous move with equal/higher
        // priority is still in flight. Don't count this as a stall —
        // try again next tick. Position-based stuck detection covers
        // the case where the jump truly never lands.
        return Step::ReturnFalse;
    }
    ClearStall(context);
    SetPhase(context, "moving");
    return Step::ReturnTrue;
}

// A healthy in-flight continuous-spline glide just rides: NextHop already
// advanced the cursor past the glided-over points, so re-issuing would
// StopMoving + Launch a fresh escort and hitch.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::DoRideLiveGlide(AdvanceState& st)
{
    st.appr->stuckCount = 0;
    ClearStall(context);
    SetPhase(context, "moving");
    return Step::ReturnTrue;
}

// Re-entry leg must be a GENERATED path. After a trash chase the tank ends well
// off the planned line; the Resnap re-anchored the cursor to the nearest VISIBLE
// forward route point, but the bot is still physically off the corridor. The
// escort spline's opening leg is a STRAIGHT segment to that point — BotCanSee
// only cleared a thin eye-ray, so the floor-walking straight line still cuts
// across wall corners / the inside of a bend (the "snaps back through the wall
// after combat" report). Rejoin with a PathGenerator-built route; the continuous
// glide resumes once RouteDeviation drops back under the on-corridor threshold.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::DoOffLineRejoin(AdvanceState& st)
{
    DungeonBossInfo const* next = st.next;
    DcApproachState& appr = *st.appr;
    DungeonFollowerState& follower = *st.follower;
    DungeonPathFollower::Hop const& hop = st.hop;

    // DcMoveTo cancels any stale straight spline so it can't shadow the pathed re-entry.
    bool const rejoining =
        DcMoveTo(next->mapId, hop.point.x, hop.point.y, hop.point.z,
                 /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                 /*exact_waypoint*/ false, MovementPriority::MOVEMENT_NORMAL);
    LOG_DEBUG("playerbots.dungeonclear",
              "[DC:{}] off-line {:.1f}yd -> rejoining route via generated path to "
              "({:.1f},{:.1f},{:.1f}) (seg {} pt {}, moved={})",
              bot->GetName(), st.routeDeviation, hop.point.x, hop.point.y, hop.point.z,
              follower.segmentIdx, follower.pointIdx, rejoining);
    appr.stuckCount = 0;
    ClearStall(context);
    SetPhase(context, "moving");
    // Own the tick whether or not MoveTo issued: a false return is the benign
    // duplicate / waiting-on-last-move case (the pathed re-entry is already in
    // flight), and we must never fall through to launch the straight escort
    // spline while the bot is still off the line.
    return Step::ReturnTrue;
}

// Normal case: hand the whole upcoming polyline run (built in FillHopObs) to the
// core as ONE EscortMovementGenerator spline so the bot glides continuously
// instead of stopping dead at every ~8yd polyline point and idling until the next
// tick (the "step, pause 2-3s, step" stutter). The escort generator builds a
// LINEAR spline, preserving the LOS-screened polyline's wall-safety without stops.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::DoIssueSplineWindow(AdvanceState& st)
{
    // SplinePath handles the stand-up / cast-interrupt / MoveSplinePath ritual and
    // the NORMAL-priority LastMovement record (sized to the window travel time, for
    // priority arbitration only). The window (>=2 points, window[0] the live
    // position) was produced in FillHopObs.
    Movement::PointsArray points(st.splineWindow.begin(), st.splineWindow.end());
    bool const splined = DcMovement::SplinePath(botAI, points);
    // DIAG(step): does the glide actually get issued? The stall fingerprint
    // (route complete, cursor 0/0, no refusal line, no movement) cannot tell
    // "no window built" from "window issued and ignored" without this.
    {
        static uint32 s_lastStepLogMs = 0;
        uint32 const nowStep = getMSTime();
        if (nowStep - s_lastStepLogMs > 4000)
        {
            s_lastStepLogMs = nowStep;
            LOG_INFO("playerbots.dungeonclear",
                     "[DC-STEP] {} spline window: {} pts, issued={} , moving={} , "
                     "at ({:.0f},{:.0f},{:.0f})",
                     bot->GetName(), points.size(), splined ? 1 : 0,
                     bot->isMoving() ? 1 : 0,
                     bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        }
    }
    if (splined)
    {
        st.appr->stuckCount = 0;
        ClearStall(context);
        SetPhase(context, "moving");
        return Step::ReturnTrue;
    }
    // SplinePath refused (rare): Continue so the caller falls through to the
    // per-point MoveTo fallback, exactly as the old ladder did.
    return Step::Continue;
}

// Terminal phase: the next leg is a jump or a lone anchor tail (window < 2
// points), so spline issuance isn't possible. Issue the single short hop —
// short enough that the engine's per-MoveTo re-pathfind never trips
// PATHFIND_SHORT, the same wall-safety the spline path preserves. Always handles
// the tick (the bottom of the ladder); only escalates to a stall after several
// consecutive MoveTo no-ops.
DungeonClearAdvanceAction::Step DungeonClearAdvanceAction::DoMoveToFallback(AdvanceState& st)
{
    DungeonBossInfo const* next = st.next;
    DcApproachState& appr = *st.appr;
    DungeonFollowerState& follower = *st.follower;
    DungeonPathFollower::Hop const& hop = st.hop;
    uint32& stuck = appr.stuckCount;

    LOG_DEBUG("playerbots.dungeonclear",
              "[DC:{}] spline window <2 pts -> per-point MoveTo fallback to "
              "({:.1f},{:.1f},{:.1f}) (seg {} pt {})",
              bot->GetName(), hop.point.x, hop.point.y, hop.point.z,
              follower.segmentIdx, follower.pointIdx);
    bool const moved = DcMoveTo(next->mapId, hop.point.x, hop.point.y, hop.point.z,
                              /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                              /*exact_waypoint*/ false, MovementPriority::MOVEMENT_NORMAL);
    if (!moved)
    {
        // MoveTo returned false. Benign in the common case (duplicate
        // move queued / waiting on last move). Only treat as a real
        // stall after several consecutive failures.
        if (++stuck >= DC_STUCK_LIMIT)
        {
            // Force a fresh chunked rebuild — the cached path's first
            // segment may be unreachable from our actual current poly.
            appr.longPathExpiresMs = 0;
            follower = DungeonFollowerState{};
            StallDungeonClear(botAI,
                "Stuck near " + next->name + " — I have a path but movement isn't progressing. "
                "I'll try to clear nearby mobs; use 'dc skip' if it persists.");
            return Step::ReturnFalse;
        }
        return Step::ReturnFalse;
    }

    stuck = 0;
    ClearStall(context);
    SetPhase(context, "moving");
    return Step::ReturnTrue;
}

bool DungeonClearAdvanceAction::Execute(Event& /*event*/)
{
    // SOCIAL QUARANTINE upkeep, before every guard below — including the ones that
    // bail. This rung and the pull FSM between them tick in every state the leader
    // can be in outside a boss fight, and the quarantine has to track the approach
    // rather than the maneuver: it must already be in force when the party walks
    // into a room, and it must be RELEASED the moment the boss it was gated on
    // dies, whether or not the tick that notices goes on to move anybody.
    // Idempotent and cheap (one enum compare per DB-spawned creature); a no-op on
    // any map with no zones and no scripted-pull plan. See DcSocialQuarantine.h.
    DcSocialQuarantine::Update(bot, context);

    // Hard pause guard. The engine builds its action queue from the triggers
    // that fired at the START of the tick; on the tick the door-blocked action
    // auto-pauses, `advance` was already queued (paused was still false then) and
    // would otherwise execute right after door-blocked sets the flag — issuing a
    // fresh long escort glide that carries the tank straight through the door it
    // just parked at. The trigger's IsEnabled gate can't catch an already-queued
    // action, so re-check here and bail before issuing any movement. (Confirmed
    // from a capture: PARK -> auto-pausing -> "advance tick" -> "spline issued".)
    if (DcRun::Of(context).paused)
        return false;

    // Hard PULL-OWNERSHIP guard, for the same already-queued-action reason as the
    // pause guard above: DungeonClearIdleTrigger stands this rung down for the
    // whole maneuver, but a trigger cannot un-queue an action, and the tick right
    // after a ranged tag is exactly when a stale basket gets its turn. What it
    // does with that tick is glide the tank at the BOSS — forward into the room
    // the pull is dragging out of. See DcActionShared::PullOwnsTheTank.
    if (PullOwnsTheTank(bot, context, "advance"))
        return false;

    // Breadcrumb trail + camp upkeep (seed when unset, trail it forward while
    // scouting). Body lives in DcPullPlanner::MaintainScoutCamp so every rung that
    // drives the leader can keep the camp with the tank — see the header comment
    // there for why leaving it here alone deadlocked the objective drive.
    DcPullPlanner::MaintainScoutCamp(botAI, context);

    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next.has_value())
    {
        // Mode stays enabled so `dc skip` is still reachable, but there is
        // nothing to skip from at this point — the next-boss value is empty
        // because every remaining boss is dead or already skipped.
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] advance: no next boss (all dead/skipped) -> stalling",
                 bot->GetName());
        StallDungeonClear(botAI,
            "Can't find a next boss: all remaining bosses are marked dead or skipped — try 'dc bosses' to inspect.");
        return false;
    }

    // All per-approach counters/latches + the long-path cache state live in one
    // owned struct (see DcApproachState); the local references below alias its
    // fields so the phase logic reads/writes one place and resets in lockstep.
    DcApproachState& appr =
        context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get();

    // Effective boss position: a wandering/patrolling boss is rarely at its
    // static DB spawn coords, so prefer its LIVE creature position whenever it
    // is loaded on the map. Engage-range gating, the at-boss handoff, and the
    // final-approach pursuit below all key off this, so the tank chases where
    // the boss actually is instead of parking at the spawn anchor. Falls back to
    // the static coords when the creature isn't loaded (far grid not streamed in
    // yet — see DC_BOSS_GRID_LOADED_RANGE).
    //
    // EXCEPTION — a PULL-BACK boss (BossPullbackRegistry). There the whole point
    // is that the boss's live position is somewhere the party must never walk:
    // Ghaz'an swims in the Underbog lake, ~150yd of path from his anchor and over
    // a 47yd pit. Routing at him would march the party into the water, which is
    // the wipe this exists to stop. Navigate to the hand-authored ANCHOR instead,
    // and suppress the live-boss handle entirely so direct pursuit (FillPursuitObs
    // / DoPursue, which bee-lines at the creature's current position) can never
    // arm. Fetching the boss is the engage action's job, not the route's.
    bool const pullback =
        BossPullbackRegistry::Find(bot->GetMapId(), next->entry) != nullptr;
    Creature* const liveBoss =
        pullback ? nullptr : DcTargeting::GetLiveBoss(bot, context, next->entry);
    float const bossX = liveBoss ? liveBoss->GetPositionX() : next->x;
    float const bossY = liveBoss ? liveBoss->GetPositionY() : next->y;
    float const bossZ = liveBoss ? liveBoss->GetPositionZ() : next->z;
    float const engageDist = bot->GetDistance(bossX, bossY, bossZ);

    // Hand-off distance: the boss's real aggro bubble (+reaches/margin) when it
    // is loaded, else the static fallback. Shrinking this for a small-aggro boss
    // lets the smooth long-path/direct-pursuit glide carry the tank most of the
    // way in before the engage pull takes over — collapsing the stutter-creep
    // the old fixed 22yd hand-off produced. Must match the trigger ladder's
    // BossEngageRange so action and triggers agree on "are we at the boss".
    float const engageRange =
        DcEngageGeometry::BossEngageRange(bot, context, *next, DC_ENGAGE_RANGE);

    // "At the boss" for the route->engage handoff: close enough AND on the
    // boss's own floor. Distinct from engageDist < engageRange (pure 3D), which
    // is true while the tank passes UNDER an upper-floor boss en route to the
    // ramp — honoring it there stops the tank dead under the boss forever. Must
    // match the trigger ladder, which gates on the same predicate.
    bool const atBoss =
        DcTickMemoAccess::AtBossEngage(bot, context, *next);

    // Back inside engage range: clear the dead-end escalation counter and the
    // direct-pursuit give-up latch so a boss that wanders back out can be
    // re-pursued cleanly (the counters themselves live on appr and are consumed
    // by FillHopObs/DoHopDoneEscalation and DoPursue below).
    if (engageDist < engageRange)
        appr.OnEnteredEngageRange();

    // Bundle the per-tick approach state for the extracted phase steps below.
    AdvanceState st;
    st.next = &*next;
    st.liveBoss = liveBoss;
    st.bossX = bossX;
    st.bossY = bossY;
    st.bossZ = bossZ;
    st.engageDist = engageDist;
    st.engageRange = engageRange;
    st.atBoss = atBoss;

    // An active submerged swim leg owns the tick outright: it drives a raw 3D
    // escort spline through a tunnel the navmesh can't model (the floor under
    // liquid is discarded at mmap-build time), so NONE of the navmesh-bound
    // logic below must run while it is active. Crucially this runs BEFORE the
    // phase ladder: mid-tunnel the boss is often unloaded, which would trip
    // TryBossNotPresentStall and abort the swim. It self-clears on arrival
    // (engage range), on consuming the leg, or on going stale, then falls
    // through to normal navigation.
    if (DriveActiveSwim(bot, botAI, context, appr, next->entry,
                        engageDist, engageRange))
        return true;

    // Phase ladder. Each step either handles the tick (and Execute returns the
    // carried bool) or falls through to the next. The pre-route rungs come
    // first; the counter-coupled tail (stuck recovery / direct pursuit /
    // long-path drive / hop cluster) follows after the boss-change bookkeeping.
    // Loot yield runs BEFORE engage-hold. Both hold identically (StopBot(Hold)),
    // but TryLootYield also runs the loot give-up cutoffs (StripSkippedLoot /
    // MaybeSkipUnworthyLoot / MaybeGiveUpCampedLoot + the yield-timeout give-up).
    // If engage-hold ran first it would short-circuit those the moment the tank
    // reached the boss — and the at-boss TRIGGER gates on the STRICT
    // IsBetweenPullsReady (requireNoLoot), so a pending-but-unfinishable corpse
    // by the boss would block the pull forever while the give-up that clears
    // `has available loot` never got a tick: the tank parked at the boss jittering
    // (loot-walk vs hold) until the boss died by other means. Loot first lets the
    // cutoffs clear the corpse and reopen the pull.
    // RAMP SCAN (diagnostic, 1x/60s), independent of what the router thinks
    // of its route. The ship deck fails in two shapes - incomplete route, or
    // a complete route that is never walked - and a scan bound to the first
    // shape simply never ran in the second. The situation itself is the
    // trigger: boss well above us and no approach for 30s. Reports every
    // grid cell whose mesh height sits between the two levels; those cells
    // are the stairway an anchor chain has to follow.
    if (st.next && st.appr)
    {
        static uint32 s_rampProbeMs = 0;
        static float  s_rampBest = 0.0f;
        static uint32 s_rampBestMs = 0;
        uint32 const nowRamp = getMSTime();
        float const botZnow = bot->GetPositionZ();
        float const toBossNow = bot->GetExactDist(st.bossX, st.bossY, st.bossZ);
        if (s_rampBestMs == 0 || toBossNow < s_rampBest - 3.0f)
        {
            s_rampBest = toBossNow;
            s_rampBestMs = nowRamp ? nowRamp : 1;
        }
        bool const noApproach = getMSTimeDiff(s_rampBestMs, nowRamp) > 30000;
        // 6y, not 15: the party now climbs to within ~10y of the deck
        // before stalling, and the old gate silenced the scan exactly there.
        if (noApproach && st.bossZ - botZnow > 6.0f && bot->FindMap() &&
            nowRamp - s_rampProbeMs > 60000)
        {
            s_rampProbeMs = nowRamp;
            Map* const rmap = bot->FindMap();
            float const cx = (bot->GetPositionX() + st.bossX) * 0.5f;
            float const cy = (bot->GetPositionY() + st.bossY) * 0.5f;
            std::string found;
            uint32 hits = 0;
            for (int ix = -6; ix <= 6 && hits < 24; ++ix)
                for (int iy = -6; iy <= 6 && hits < 24; ++iy)
                {
                    float const px = cx + float(ix) * 8.0f;
                    float const py = cy + float(iy) * 8.0f;
                    NavmeshSnap::Result const r = NavmeshSnap::SnapColumn(
                        rmap, px, py, (botZnow + st.bossZ) * 0.5f, 30.0f, 6.0f);
                    if (!r.ok)
                        continue;
                    if (r.z > botZnow + 6.0f && r.z < st.bossZ - 4.0f)
                    {
                        found += " (" + std::to_string(int(px)) + "," +
                                 std::to_string(int(py)) + "," +
                                 std::to_string(int(r.z)) + ")";
                        ++hits;
                    }
                }
            LOG_INFO("playerbots.dungeonclear",
                     "[DC-RAMP] botZ={} bossZ={} bot=({},{}) centre=({},{}) {} mid-level cells:{}",
                     int(botZnow), int(st.bossZ), int(bot->GetPositionX()),
                     int(bot->GetPositionY()), int(cx), int(cy), hits,
                     found.empty() ? std::string(" none") : found);
        }
    }

    if (Step s = TryLootYield(st); s != Step::Continue)
        return s == Step::ReturnTrue;
    if (Step s = TryEngageHold(st); s != Step::Continue)
        return s == Step::ReturnTrue;
    if (Step s = TryBetweenPullsRest(st); s != Step::Continue)
        return s == Step::ReturnTrue;
    if (Step s = TryBossNotPresentStall(st); s != Step::Continue)
        return s == Step::ReturnTrue;

    // Bookkeeping: on a boss change wipe the per-approach counters so a stale
    // count from the previous pull doesn't bleed into the new approach. The
    // sticky engage-trash target isn't part of the approach struct — reset it
    // alongside the counter reset.
    if (appr.lastTargetEntry != next->entry)
    {
        appr.OnBossChange(next->entry);
        context->GetValue<ObjectGuid>(DcKey::EngageTrashTarget)->Set(ObjectGuid::Empty);
        // The stall reason (if any) was about the boss we just left — drop it so
        // the panel can't keep reporting "Can't reach <old boss>" now that we're
        // committed to a new target. NextDungeonBossValue also clears it on the
        // commit change for the case where Advance is parked in a loot/rest yield
        // and never reaches this bookkeeping; clearing here covers the live path.
        ClearStall(context);
    }

    // Single-observation approach tail (fable2 T2.2 / nav F10). ONE Observation
    // is assembled across three lazy stages that mirror the action's cost
    // deferral — Tier A (pre-path: stuck + pursuit shortcut) is decided before
    // the long-path is built, Tier B (reachability / off-path) after
    // EnsureLongPath, Tier C (the hop cluster) after NextHop. The pure
    // DecideApproach is the SOLE owner of the ladder order: a stage claims the
    // tick only when its verdict is not the terminal MoveToFallback (the ladder's
    // fall-through), so the rung order lives in exactly one place instead of being
    // re-stated by the Execute ladder. The owning verdict + the observation as
    // completed through that stage is captured ONCE, so every acted-on tick is a
    // whole-tick, replayable fixture (the old staged callers each recorded only a
    // mostly-default, stage-local observation).
    st.appr = &appr;

    DungeonClearApproach::Observation obs = MakeApproachObs();
    obs.engageDist  = engageDist;
    obs.engageRange = engageRange;

    // --- Tier A: pre-path (stuck, then direct pursuit). Decided in two steps so
    // pursuit's canPursue bookkeeping (it clears the give-up latch when the boss
    // isn't pursuable) never runs on a stuck-recover tick — the old ladder ran the
    // pursuit rung strictly after stuck-recover returned. ---
    FillStuckObs(st, obs);
    if (DungeonClearApproach::Verdict const vStuck = DungeonClearApproach::DecideApproach(obs);
        vStuck == DungeonClearApproach::Verdict::StuckRecover)
    {
        MaybeRecord(bot, obs, vStuck);
        return DoStuckRecover(st) == Step::ReturnTrue;
    }

    FillPursuitObs(st, obs);
    if (DungeonClearApproach::Verdict const vA = DungeonClearApproach::DecideApproach(obs);
        vA == DungeonClearApproach::Verdict::Pursue)
    {
        Step const s = DoPursue(st);
        if (s != Step::Continue)
        {
            MaybeRecord(bot, obs, vA);
            return s == Step::ReturnTrue;
        }
        // Pursuit abdicated this tick (give-up latch tripped). Refresh the latch
        // field so the ladder below sees the CLOSED latch (else the still-true
        // canPursue would re-select Pursue) and hand off to the long-path.
        obs.pursuitFailTicks = appr.pursuitWatch.stuckTicks;
    }

    // --- Tier B: resolve the long-path toward the boss's EFFECTIVE position
    // (live creature coords when loaded, else the static spawn anchor). ---
    DungeonBossInfo effectiveTarget = *next;
    effectiveTarget.x = bossX;
    effectiveTarget.y = bossY;
    effectiveTarget.z = bossZ;
    EnsureLongPath(bot, context, appr, effectiveTarget);
    ChunkedPathfinder::Result const& path =
        AI_VALUE(ChunkedPathfinder::Result&, DcKey::LongPath);
    DungeonFollowerState& follower =
        context->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get();
    st.path = &path;
    st.follower = &follower;

    FillPathObs(st, obs);
    if (DungeonClearApproach::Verdict const vB = DungeonClearApproach::DecideApproach(obs);
        vB != DungeonClearApproach::Verdict::MoveToFallback)
    {
        MaybeRecord(bot, obs, vB);
        if (vB == DungeonClearApproach::Verdict::OffPathRebuild)
            return DoOffPathRebuild(st) == Step::ReturnTrue;
        return DoLongPathUnreachable(st) == Step::ReturnTrue;
    }

    // --- Tier C: the hop cluster. One NextHop call advances the follower cursor,
    // so the resulting hop is carried through in st (never recomputed). ---
    st.hop = DungeonPathFollower::NextHop(bot, path, follower);

    // Stranded cursor: the bot has arrived at its own hop in plan view but sits
    // outside the vertical band, so walking cannot close the gap and NextHop
    // cannot advance. Left alone this is a silent forever-loop — posStuck
    // resnaps onto the same point, the rebuild re-derives it from the same
    // position, and the tank paces under it (tr-20260818-073620-14, nine
    // minutes at (-54,-366,76) on the Blackrock Spire ramp). Step the cursor
    // past it and re-fetch, so the tick has somewhere real to go.
    if (!st.hop.isDone && !st.hop.isJump)
    {
        G3D::Vector3 skipped;
        if (DungeonPathFollower::SkipStrandedPoint(bot, path, follower, skipped))
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] stranded cursor: standing {:.1f}yd under/over route point "
                     "({:.1f},{:.1f},{:.1f}) -> skipped to seg {} pt {}",
                     bot->GetName(), bot->GetPositionZ() - skipped.z,
                     skipped.x, skipped.y, skipped.z, follower.segmentIdx, follower.pointIdx);
            st.hop = DungeonPathFollower::NextHop(bot, path, follower);
        }
    }

    TryReanchorStaleCursor(st);  // mutates st.hop / cursor; never terminates the tick

    // Sync the legacy "current hop" telemetry — `dc status` and a few tests
    // still read it. Map the flattened polyline cursor onto its segment index.
    context->GetValue<uint32>(DcKey::CurrentHop)->Set(follower.segmentIdx);

    FillHopObs(st, obs);
    DungeonClearApproach::Verdict const vC = DungeonClearApproach::DecideApproach(obs);
    MaybeRecord(bot, obs, vC);
    switch (vC)
    {
        case DungeonClearApproach::Verdict::RebuildAndYield:
        case DungeonClearApproach::Verdict::FinalApproach:
        case DungeonClearApproach::Verdict::Swim:
        case DungeonClearApproach::Verdict::Stall:
            return DoHopDoneEscalation(st, vC) == Step::ReturnTrue;
        case DungeonClearApproach::Verdict::JumpLeg:
            return DoJumpLeg(st) == Step::ReturnTrue;
        case DungeonClearApproach::Verdict::RideLiveGlide:
            return DoRideLiveGlide(st) == Step::ReturnTrue;
        default:
            break;  // OffLineRejoin / IssueSplineWindow / MoveToFallback: below the
                    // movement gate.
    }

    // The remaining movement rungs sit below the IsMovingAllowed gate (unchanged
    // from the old ladder, where it sat between ride and off-line-rejoin).
    if (!IsMovingAllowed())
        return false;

    if (vC == DungeonClearApproach::Verdict::OffLineRejoin)
        return DoOffLineRejoin(st) == Step::ReturnTrue;

    // IssueSplineWindow, then the terminal per-point MoveTo (window < 2 points, or
    // a SplinePath that refused). DoIssueSplineWindow returns Continue when the
    // spline could not be launched, so the fallback owns the tick just as before.
    if (vC == DungeonClearApproach::Verdict::IssueSplineWindow)
        if (Step s = DoIssueSplineWindow(st); s != Step::Continue)
            return s == Step::ReturnTrue;

    return DoMoveToFallback(st) == Step::ReturnTrue;
}

