/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_TARGETING_H
#define _DC_TARGETING_H

#include <vector>

#include "ObjectGuid.h"
#include "MoveSplineInitArgs.h"
#include "Position.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"

class Player;
class Unit;
class Creature;
class GameObject;
// InstanceScript is an alias of InstanceData on this core (compat shim);
// a class forward-declaration would shadow the alias with an empty type.
class InstanceData;
using InstanceScript = InstanceData;
namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
class PlayerbotAI;
struct DungeonBossInfo;

class DcTargeting
{
public:
    // Returns the closest hostile, alive unit from `possibleTargets` that
    // sits within `range` of the bot AND within `halfAngle` radians of the
    // direction from bot to boss. Nullptr if none. Geometry-only — used as
    // the fallback when corridor computation isn't available.
    static Unit* FindBlockingTrash(Player* bot,
                                   DungeonBossInfo const& boss,
                                   float range,
                                   float halfAngle,
                                   GuidVector const& possibleTargets);

    // Returns the closest hostile, alive unit from `possibleTargets` whose
    // 2D distance to the path polyline is within its blocking band, considering
    // only the segment of the path within the first maxLookAhead yards from the
    // bot. LOS-checked. Nullptr if none. With DungeonClear.DynamicAggroRange on,
    // each candidate's band is its real aggro range (clamped to the trash
    // floor/cap); off, every candidate uses the fixed `corridorWidth`.
    static Unit* FindBlockingTrashCorridor(Player* bot,
                                           Movement::PointsArray const& corridor,
                                           float maxLookAhead,
                                           float corridorWidth,
                                           GuidVector const& possibleTargets);

    // Scans `candidates` for the closest hostile alive unit whose 2D distance
    // to any segment of the supplied path polyline (within the first
    // `maxLookAhead` yards of forward travel) is within its blocking band.
    // LOS-checked. Nullptr if none. Replaces the single-segment
    // FindBlockingTrashCorridor for long routes that fan across multiple chunks.
    // With DungeonClear.DynamicAggroRange on the band is each candidate's real
    // aggro range (clamped to the trash floor/cap); off it is `corridorWidth`.
    static Unit* FindBlockingTrashOnPath(Player* bot,
                                         std::vector<PathSegment> const& segments,
                                         float maxLookAhead,
                                         float corridorWidth,
                                         GuidVector const& candidates);

    // EN-ROUTE PACK SWEEP. The pack whose aggro sphere the next `maxLookAhead`
    // yards of route ENTERS FIRST, or nullptr.
    //
    // The companion to FindBlockingTrashOnPath, and deliberately a different
    // question. That one asks what is standing ON the path — a corridor band,
    // nearest in-LOS candidate wins — which in a hall with rooms off it targets the
    // mob on the centre line and walks the tank through the aggro of everything
    // flanking it. This one asks whose aggro the walk is about to enter, in ROUTE
    // ORDER, so a room gets pulled from its threshold instead of woken in passing.
    //
    // The reference geometry is Stormwind Stockade (34), issue #17: the first boss
    // sits 105yd dead ahead of the entrance up the central axis, and the cells
    // flanking that axis hold elites 10-21yd off the route line against a ~25yd
    // aggro reach — every one of them inside aggro of the corridor itself. The tank
    // targeted the mob ON the line, walked to it, and arrived with three cells in
    // tow. En-route AVOIDANCE cannot fix that: there is no clear line to detour to,
    // so TruncateWindowAtSphere correctly declines and the tank walks through
    // anyway. The pack has to be pulled, not dodged.
    //
    // Shares BystanderSpheres / FirstViolatedSphereOnPolyline with that avoidance,
    // so detection and evasion can never disagree about what "inside aggro" means.
    // Adds vetoes the avoidance has no need of, because a sphere it merely walks
    // around is one this WALKS TO: encounter and room-aggro bosses (the dedicated
    // at-boss / pre-clear paths own those), a closed door in between,
    // level-reachability — and, most importantly, an en-route LINE OF SIGHT test.
    // The sphere set is LOS-blind by design; a mob behind a solid wall never aggros
    // no matter how far its sphere reaches over the corridor, so it must be shown
    // to see some point on the walk before it is worth walking to.
    //
    // NORMAL difficulty only (DcEngageGeometry::EnRouteSweepApplies — heroic is
    // the avoidance's job, not this one), and nullptr there — so callers may call
    // unconditionally. Costs one grid search plus at most kSweepProbeBudget
    // reachability/LOS probes; leader-only call sites, and the pull side sits
    // behind the 250ms sticky value.
    static Unit* FindEnRouteAggroPack(Player* bot, AiObjectContext* ctx,
                                      std::vector<PathSegment> const& segments,
                                      float maxLookAhead);

    // The trash pack the advanced-pull maneuver should grab, or nullptr. Mirrors
    // the blocking-trash trigger's primary detection (corridor scan along the
    // cached long-path, falling back to the geometric cone) plus the closed-door
    // veto, so the pull aims at the same pack the normal flow would walk in to.
    // Shared by the pull trigger (decide to start) and the pull action (where to
    // run). Reads the bot's AI values via `botAI`.
    static Unit* FindPullTarget(PlayerbotAI* botAI, DungeonBossInfo const& next);

    // Live pull-target resolve through the cached/sticky "dungeon clear pull
    // target" GUID (see DungeonClearPullTargetValue) instead of re-running the
    // corridor scan at every call site — the pull pipeline asks for it 5+ times
    // per out-of-combat tick. Re-resolves the GUID via ObjectAccessor every call
    // (the position stays live, no pointer outlives the tick) and forces one
    // recompute when the cached GUID went stale within the interval (the pack
    // died), so a commit never aims at a corpse. Nullptr when no pull target.
    static Unit* GetPullTarget(PlayerbotAI* botAI);

    // True when `entry` is one of the run's encounter bosses (membership test
    // against the "dungeon bosses" value). Linear scan — the list is ≤ ~20
    // entries; build a set snapshot alongside the vector if profiling ever
    // cares. Keeps bosses out of the pull pipeline: the dedicated at-boss path
    // (engage-range gate, anchor checks, door veto, party-ready gate) owns
    // them, and scripted bosses misbehave when camp-dragged.
    static bool IsDungeonBossEntry(AiObjectContext* ctx, uint32 entry);

    // Validity predicate for KEEPING the sticky pull target between scans:
    // alive, hostile, not a dungeon boss, not the pull context's abort target,
    // within the scan look-ahead plus a slack band, level-reachable, and no
    // closed door between.
    // Deliberately does NOT require it to still be the closest blocker or
    // inside the scan corridor — packs drift while patrolling, and re-running
    // corridor membership is exactly the target-flapping instability the sticky
    // latch removes.
    static bool IsStickyPullTargetValid(Player* bot, AiObjectContext* ctx, Unit* u);

    // Scans every loaded creature on the bot's map for an alive hostile that
    // (a) is not already in combat with someone else and (b) the bot can path
    // to. Returns the closest such unit, or nullptr. Used by the stalled
    // fallback to kill obstacles when no path to the boss exists.
    static Unit* FindNearestReachableHostile(Player* bot);

    // The unit the leader is fighting, from `bot`'s perspective: the nearest live,
    // valid-attack-target among the leader's attackers, falling back to the
    // leader's victim, then nullptr. LOS-blind on purpose — the whole point is to
    // anchor a reconnect on a fight the bot may not yet see. `anchorPos` receives
    // the resolved unit's position, or the leader's own position when nothing
    // resolves (so a caller can still aim at where the fight is). This is the
    // fight-anchor scan the combat regroup samples its standoff ring around; it is
    // the same nearest-attacker→victim ladder DungeonClearAssistCampActionBase
    // uses (kept as a shared, testable helper).
    static Unit* LeaderFightAnchor(Player* bot, Player* leader, Position& anchorPos);

    // Returns a live spawned creature with the given entry on the bot's map, or
    // nullptr if none exists or all are dead.
    static Creature* FindLiveCreatureOnMap(Player* bot, uint32 entry);

    // Live boss creature for `entry`, resolved through the cached
    // "dungeon clear live boss" GUID (O(1) ObjectAccessor lookup) instead of
    // re-scanning the whole creature store on every call — the trigger ladder
    // and advance action ask for it several times per tick. Falls back to a
    // direct store scan when the cache was computed for a different boss (the
    // brief window just after a boss change) or the cached GUID went stale.
    // Returns nullptr when the boss isn't loaded/alive on the map.
    static Creature* GetLiveBoss(Player* bot, AiObjectContext* ctx, uint32 entry);

    // Returns true if at least one spawned creature with the given entry exists
    // on the bot's map (alive or dead). Distinguishes "missing" from "killed".
    static bool IsCreaturePresentOnMap(Player* bot, uint32 entry);

    // --- Event-summoned bosses (e.g. RFD's gong -> Tuten'kash) ------------

    // True when `bossEntry` is the credit boss of an un-retired CONDITIONAL event
    // on the bot's map (DungeonEventRegistry, matched via panelGatesBossEntry) —
    // i.e. a boss that an event must SUMMON before it exists. Such a boss is
    // legitimately absent from the creature store until the event runs, so the
    // approach must not "not-spawned" stall on it.
    static bool HasPendingSummonEvent(Player* bot, AiObjectContext* ctx, uint32 bossEntry);

    // True when the leader is parked at a pending-summon boss's anchor: the boss
    // is the next target, has a pending summon event, is not yet live, and the
    // tank is within the event hold radius of the anchor. While this holds, the
    // dynamic-pull pipeline stands down so the tank stays put to run the event
    // (ring the gong, fight the wave, ring again) instead of wandering off to
    // pull distant trash and never returning. Reads false the instant the boss
    // goes live (normal engage takes over) or the tank leaves the hold radius.
    static bool IsHoldingForSummonEvent(Player* bot, AiObjectContext* ctx,
                                        DungeonBossInfo const& next);

    // Returns the InstanceScript driving the bot's current map, or nullptr
    // if the map is not an instance map or has no script. Used by the
    // next-boss probe to consult authoritative encounter state before
    // falling back to spawn-store creature scanning.
    static InstanceScript* GetInstanceScript(Player* bot);

    // Wipe the run-scoped completion state (cleared anchors / skips / seen-boss
    // and seen-due-event bookkeeping / boss commit) the first time the bot is seen
    // in a NEW instance id, stamping "dungeon clear run instance" so it fires once
    // per re-enter. Boss kills self-reset with the instance via the encounter mask,
    // but these MODULE-side latches live in the bot's context and outlive the
    // instance, so a re-enter would otherwise show a completed objective/event as
    // still "Done". Idempotent (returns true only on the tick it actually resets);
    // call it from EVERY leader-side entry point that reads the completion state —
    // both NextDungeonBossValue (drives navigation, runs only while DC is on) and
    // the boss-list builder (drives the panel, runs on the addon's request even
    // while DC is off) — so whichever fires first clears the latch.
    static bool ResetCompletionLatchesForNewInstance(Player* bot, AiObjectContext* context);

    // --- Pull-back bosses (BossPullbackRegistry) --------------------------

    // True when the next anchor is a PULL-BACK boss, the tank has arrived at its
    // hand-authored anchor, and the boss is alive on the map — i.e. the pull
    // pipeline should now run the tag-and-drag that brings the boss to the party
    // instead of the party walking to the boss.
    //
    // This is the single gate that lets a pull-back boss through the parts of the
    // pull pipeline that normally stand bosses down: the trigger's pull-mode
    // requirement (a pull-back is MANDATORY, so it runs even with the player's
    // pull setting Off) and its at-boss stand-down (which otherwise hands the boss
    // to the walk-in engage). Cheap: a registry Find plus the already-memoised
    // at-boss probe — and the Find misses outright while the table is empty, which
    // it has been since S1593.
    static bool IsPullbackBossDue(Player* bot, AiObjectContext* ctx);

    // --- Room-wide-aggro pre-clear (RoomAggroRegistry) --------------------

    // True when the next boss is a flagged room-aggro boss AND the tank is at
    // its engage range AND room trash still remains to clear (the "dungeon clear
    // room trash remaining" value is non-empty). This is the predicate that both
    // holds the boss pull and routes the trash clear: while it is true the boss
    // gate stands down and the pull pipeline / room-clear action work the room.
    static bool IsRoomClearActive(Player* bot, AiObjectContext* ctx);

    // True when a room-clear is active (IsRoomClearActive) AND the room carries a
    // pullOutRadius (RoomAggroBoss::pullOutRadius > 0). Such a room deliberately
    // shrinks the room-trash exclusion to keep a pack sitting on the boss's aggro
    // edge as clearable trash; that is safe ONLY if the room-clear drags the pack
    // OUT with the advanced pull-to-camp maneuver instead of meleeing it in place
    // inside the boss's wake radius, so the dynamic-pull governor reads this to
    // FORCE the advanced verdict for the room (see DcPullPlanner::UpdateDynamicPull-
    // Mode). Leader/room-scoped; cheap (a registry Find + the IsRoomClearActive
    // probe already run for the pull path).
    static bool RoomClearForcesAdvanced(Player* bot, AiObjectContext* ctx);

    // The widened avoid-sphere skirt (yd) of the room-aggro boss currently being
    // pre-cleared, or 0 when no room-clear is active or that boss carries no skirt
    // override (RoomAggroBoss::skirtRadius). The advanced-pull camp raises its boss
    // clearance and drag cap to this so a pack kept on the boss's aggro edge is
    // DRAGGED OUT and fought outside her aggro, not killed in her wake (the Sepethrea
    // "combat too close, pulls the boss" failure). Leader/room-scoped; cheap.
    static float ActiveRoomSkirt(Player* bot, AiObjectContext* ctx);

    // The nearest remaining room-trash unit (from "dungeon clear room trash
    // remaining"), or nullptr. Nearest-first so the tank clears the room from
    // its edge inward and reaches the boss's own aggro sphere last, minimising
    // the chance of waking the boss while clearing.
    static Unit* NearestRoomTrash(Player* bot, AiObjectContext* ctx);

    // The nearest reachable, attackable hostile within `radius` (2D) of the point
    // (px,py,pz) and within `zBand` vertically — or nullptr. Backs the ClearRadius
    // event step (a POINT-anchored room pre-clear, e.g. Sunken Temple's central
    // circle before Jammal'an): position-based, so by default it clears whatever
    // patrols the area regardless of entry. Excludes encounter and room-aggro
    // bosses, the unreachable, and door-blocked units. Nearest-to-the-BOT so the
    // tank works inward from where it stands. zBand keeps a multi-level chamber's
    // balconies (above) and pit (below) out of a floor-level clear.
    //
    // `entryFilter`, when non-null AND non-empty, narrows the scan to those
    // creature entries (EventStep::entryFilter) — for a volume that overlaps
    // ambient wildlife the clear must not chase (Black Morass's rift waves in
    // open swamp). Null/empty keeps the position-only behaviour.
    // `rankFrom` chooses which candidate wins when several qualify: by default the
    // one nearest the BOT, which is what a room clear wants (fight your way in).
    // Pass a point to rank from there instead — for a plan that has to tag from a
    // FIXED spot the bot is not standing on yet, "nearest to me right now" can
    // easily be the far side of the pack and out of the pull spell's range once
    // the bot arrives (see ScriptedPullRegistry).
    // `exclude` drops one specific unit from the candidate set — for a caller that
    // has already given up on it and wants the next best, not the same answer again.
    // `awayFrom` INVERTS the ranking: the winner is the qualifying candidate FURTHEST
    // from that point, and `rankFrom` is ignored. For a pull whose pack has a
    // neighbour it cannot avoid waking, "which member" stops being a question about
    // reach and becomes one about how far the neighbour has to run — see
    // ScriptedPullStage::avoidX. Filters are identical either way; this only decides
    // which of the qualifying candidates wins.
    static Unit* NearestHostileNearPoint(Player* bot, AiObjectContext* ctx,
                                         float px, float py, float pz,
                                         float radius, float zBand = 20.0f,
                                         std::vector<uint32> const* entryFilter = nullptr,
                                         Position const* rankFrom = nullptr,
                                         ObjectGuid exclude = ObjectGuid::Empty,
                                         Position const* awayFrom = nullptr);

};

#endif  // _DC_TARGETING_H
