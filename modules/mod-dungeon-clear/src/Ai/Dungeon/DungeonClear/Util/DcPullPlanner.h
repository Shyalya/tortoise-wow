/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_PULL_PLANNER_H
#define _DC_PULL_PLANNER_H

#include <cstdint>
#include <optional>
#include "Position.h"

class Player;
class Unit;
class PlayerbotAI;
namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
// Optional detail from ClassifyPullAdvanced for the patrol-wait gate: the full
// aggro estimate, the same estimate with lone (non-formation) patrollers excluded,
// and the Leeroy ceiling they were compared against. `full > ceiling &&
// reduced <= ceiling` is the patrol-contended condition.
//
// All three counts are in THIRDS OF AN ELITE (elite = 3, normal = 1), the unit the
// verdict comparison works in. `bodyCount` is the same estimate as a plain head
// count — not used by any gate, but it is what a human means by "how many mobs
// will this pull", so it is what the pull telemetry compares against the number
// that actually turned up.
struct DcPullClassification
{
    std::uint32_t fullCount    = 0;
    std::uint32_t reducedCount = 0;
    std::uint32_t ceiling      = 0;
    std::uint32_t bodyCount    = 0;
};

class DcPullPlanner
{
public:
    // Returns a per-bot "camp slot": the shared camp anchor nudged 1-2yd in a
    // deterministic, GUID-derived direction so a party holding at camp fans out
    // instead of stacking on one identical coordinate (the bot-like single-point
    // look). The offset is run through PathGenerator and the navmesh-snapped
    // endpoint is returned, so the slot is guaranteed to sit inside the zone
    // geometry; if the probe can't produce a real ground path near camp (camp
    // against a wall / on a ledge) it falls back to the exact camp position. The
    // result is stable across ticks for a given (bot, camp), so MoveTo dedups it
    // and the follower settles on one spot instead of jittering.
    static Position ComputeCampSlot(Player* bot, Position const& camp);

    // Clamped heal-approach point for a held ranged healer during an advanced
    // pull. Returns a navmesh-snapped point at ~85% of healRange from healTarget
    // along the target->camp direction, i.e. on the CAMP side of the heal target.
    // This lets the healer close the gap to reach heal range while guaranteeing it
    // never crosses to the threat side of the tank (the pack sits opposite the
    // camp on a drag-back), so it can't overshoot past the tank into the mobs.
    // Falls back to the camp position when the camp is already within heal range
    // of the target, when inputs are degenerate, or on a bad navmesh probe.
    static Position ComputeHealApproach(Player* bot, Unit* healTarget,
                                        Position const& camp, float healRange);

    // --- Dynamic pull (auto Leeroy vs Advanced) -----------------------------
    // Classifies the pull on `target`: true => use the careful Advanced pull-to-
    // camp; false => Leeroy it. Estimates how many mobs would aggro if the party
    // fought on top of the target — every mob whose own (level-scaled) aggro
    // radius + PullCombatSpread reaches the camp spot, plus one CallForHelp assist
    // hop — gated to mobs in line of sight, on the same floor, with no closed door
    // between (the far-targets scan ignores LOS). True when that estimate exceeds
    // PullDynamicMaxLeeroyMobs. Reach comes from the real creature aggro radius
    // (vs the lowest-level party member), so it self-tunes per zone/level. The pure
    // count is DungeonClearMath::EstimateAggroCount. See the Dynamic Pull plan doc.
    // `out` (optional) receives the full/reduced/ceiling counts for the patrol-wait
    // gate (DungeonClearMath::ShouldWaitForPatrol).
    static bool ClassifyPullAdvanced(PlayerbotAI* botAI, Unit* target,
                                     DcPullClassification* out = nullptr);

    // Per-tick governor for Dynamic mode (pull setting == 2). No-op for Off/On
    // (DcPullAction owns the bool there). Out of combat with no pull maneuver in
    // flight, it sizes up the next pull target (ClassifyPullAdvanced), latches the
    // verdict per target GUID (so a single approaching pack isn't re-judged every
    // tick and the party isn't churned between follow/hold), and drives `dungeon
    // clear pull mode` (+ leader daze immunity + camp seed) so the rest of the
    // existing pipeline runs the chosen maneuver. Now invoked from the per-bot
    // DungeonClearPullModeCurrentValue::Calculate (any consumer may read it on any
    // bot), so it self-gates: it no-ops unless the bot is the enabled, non-paused
    // dungeon-clear leader. Publishes the verdict to `dungeon clear pull decision`
    // for the addon.
    static void UpdateDynamicPullMode(PlayerbotAI* botAI, AiObjectContext* context);

    // Picks the advanced-pull camp: a rally point `setback` yards BACK along the
    // already-cleared route, where the tank drags the pulled pack and the party
    // waits. Dungeon mobs have no leash, so the camp is placed a generous fixed
    // distance back for room rather than the minimum that "works". If the point
    // `setback` back is still within `safeRadius` of another pack (any live
    // hostile that is not `target` and not one of `target`'s packmates) the search
    // keeps walking back, up to `maxDrag`, until clear. Cleared route behind the
    // tank is inherently safe ground (we already killed through it). When the
    // cleared route behind is too short (e.g. the first pull near the entrance) it
    // falls back to a straight line away from the target, snapped to the navmesh.
    // Returns nullopt only when there is no usable position at all (no bot/target).
    // The chosen point's clearance is written to `clearanceOut` (FLT_MAX when no
    // other pack exists) and how far back it sits to `dragOut`, for the plan log.
    static std::optional<Position> ComputeSafeCamp(PlayerbotAI* botAI, Unit* target,
                                                   float setback, float safeRadius,
                                                   float maxDrag,
                                                   float& clearanceOut, float& dragOut);

    // Lean, target-less twin of ComputeSafeCamp for the Idle SCOUT phase: returns
    // a point `setback` back along the breadcrumb trail behind the tank (the
    // search gives up at maxDrag), so the camp can TRAIL the moving tank while no
    // pull is committed and the party walks along behind it at a fixed standoff.
    // No clearance test —
    // the tank just walked this ground, so it is by definition clear, and there is
    // no pack to stay clear of yet. Falls back to the farthest contiguous trail
    // point, then to the tank's own position when the trail is too short. Returns
    // nullopt only when there is no bot.
    static std::optional<Position> ComputeTrailCamp(PlayerbotAI* botAI, float setback,
                                                    float maxDrag);

    // Per-tick camp upkeep for the LEADER: lay a breadcrumb and keep the party's
    // camp trailing `PullSetback` behind us while merely scouting (pull phase Idle,
    // out of combat, no fresh pull publish). Hoisted out of
    // DungeonClearAdvanceAction::Execute so it is no longer the exclusive property
    // of the advance rung: any action that DRIVES the leader forward must call it,
    // or the camp goes stale the moment a higher rung takes the tick and the party
    // is left pinned at a spot the tank has walked away from — while the tank waits
    // for a party that has been told to stand there. That deadlock is exactly what
    // froze Old Hillsbrad: the barrel objective (DcObjectiveArriveAction, relevance
    // 30) drove the tank ~76yd off the elevated walkway into the courtyard, advance
    // (15) never ran again, and the party held on the walkway one terrace up.
    // Idempotent within a tick (the breadcrumb has a spacing gate and the camp
    // adoption is forward-only), so calling it from several rungs is safe.
    static void MaintainScoutCamp(PlayerbotAI* botAI, AiObjectContext* context);

    // True when the party is "set" for the tank to pull: every living, on-map,
    // non-leader BOT follower is within `setRadius` of `camp` AND currently
    // running the combat-engine "passive" strategy (so it won't break the pull).
    // Real-player members (no PlayerbotAI) are not waited on. Solo (no group)
    // returns true. Lets the Forming gate hold the tag until the party has
    // actually parked and gone passive, instead of pulling into open ground.
    static bool IsPartySetAtCamp(Player* leader, Position const& camp, float setRadius);

};

#endif  // _DC_PULL_PLANNER_H
