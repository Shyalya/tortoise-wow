/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARMATH_H
#define _PLAYERBOT_DUNGEONCLEARMATH_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "G3D/Vector3.h"
#include "Position.h"

namespace DungeonClearMath
{
    // Sentinel "no rejoin crumb" returned by FindTrailRejoin.
    inline constexpr std::size_t TrailRejoinNone = static_cast<std::size_t>(-1);

    // Sentinel "no member needs healing" returned by SelectHealTarget.
    inline constexpr std::size_t HealTargetNone = static_cast<std::size_t>(-1);

    // One candidate heal target for SelectHealTarget. `healthPct` is the member's
    // current health (0..100); `isLeaderTank` marks the elected dungeon-clear
    // leader tank so it can be favoured. Deliberately carries NO line-of-sight or
    // range flag: heal-reposition selection must survive LOS loss (the stock
    // `party member to heal` value drops out-of-LOS members entirely, which is the
    // root of the "healer stops healing when the tank is dragged out of sight"
    // bug). The caller resolves members to candidates from live game state.
    struct HealCandidate
    {
        float healthPct;
        bool  isLeaderTank;
    };

    // Pure heal-target selector (LOS-blind). Among members BELOW `hpFloor` (the
    // "actually needs healing" gate), pick the one with the lowest tank-biased
    // score: `healthPct - (isLeaderTank ? tankBias : 0)`. The bias only breaks the
    // choice toward the tank among members that already need healing — a healthy
    // tank above the floor is never selected — so a hurt tank being kited out of
    // sight is preferred over an equally/slightly-more-hurt DPS, while a healthy
    // tank never steals the pick from a hurt DPS. Returns the chosen index, or
    // HealTargetNone when nobody is below the floor. Separated from the group
    // iteration so the choice is unit-testable.
    std::size_t SelectHealTarget(std::vector<HealCandidate> const& members,
                                 float hpFloor, float tankBias);

    // Pure standoff-point generator. Produces candidate 2D points on a circle of
    // radius `standoffRadius` around `center`, ORDERED so the first is on the side
    // the bot already stands (shortest move / most likely to round the same corner)
    // and the rest fan out alternately to either side. The caller snaps each to the
    // navmesh and keeps the first that has line of sight and a clear path to the
    // center. `ringPoints` extra points are generated past the first (so the result
    // has `ringPoints + 1` entries). Z is left at the center's Z; the caller
    // re-grounds via navmesh snap. With a degenerate bot-on-center input the bias
    // direction falls back to +X. Nothing here is heal-specific: the healer LOS
    // reposition and the contribution-gated combat regroup both sample a role-range
    // ring around a fight anchor with it (see DcActionShared::FindStandoffPoint).
    std::vector<Position> StandoffCandidates(Position const& center,
                                             Position const& bot,
                                             float standoffRadius,
                                             std::uint32_t ringPoints);

    // Deprecated heal-specific alias for StandoffCandidates, kept so the existing
    // heal-reposition call site and t/TestDungeonClearMath / t/TestHealReposition
    // compile unchanged. Prefer StandoffCandidates in new code.
    inline std::vector<Position> HealStandoffCandidates(Position const& target,
                                                        Position const& bot,
                                                        float standoffRadius,
                                                        std::uint32_t ringPoints)
    {
        return StandoffCandidates(target, bot, standoffRadius, ringPoints);
    }

    // One forward hostile for the Dynamic-pull aggro estimate. `chainEligible` is
    // pre-resolved by the caller from game state: true only when this mob is
    // navmesh-reachable from the pull target with a clear line of sight and no
    // closed door between (the far-targets scan ignores LOS, so this gate keeps a
    // mob through a wall / a floor away / behind a door from counting as one that
    // would aggro). It is ignored for mobs that share the target's own pack. `z`
    // carries the mob's world height so the estimate can reject mobs on another
    // floor (a ledge/ramp directly above or below) instead of counting them by
    // plan-view distance alone — see `zTolerance` on EstimateAggroCount.
    //
    // `aggroReach` is this mob's real aggro radius (Creature::GetAggroRange +
    // GetCombatReach, level-diff scaled — see DungeonClearUtil::ClassifyPull-
    // Advanced) measured against the squishiest party member. It is the distance
    // at which the mob proximity-aggros the party fighting at the camp spot, so it
    // self-tunes per zone/level instead of a single hand-tuned chain radius.
    //
    // `packId` is an OPTIONAL engine-pack identity (0 = none) the caller resolves
    // from what actually pulls together: a creature formation group, or a
    // creature_linked_respawn link. Mobs sharing a non-zero `packId` pull as one
    // atomic unit regardless of spacing or height (you cannot pull half a
    // formation), so a counted member drags its whole pack into the estimate.
    // `patroller` marks a DB-authored waypoint mover (GetDefaultMovementType() ==
    // WAYPOINT_MOTION_TYPE). A human tank waits a patrol out instead of committing
    // the heavier pull for a pack that would be a clean small Leeroy moments later;
    // the patrol-wait gate re-runs the estimate with lone patrollers excluded (see
    // `excludeLonePatrollers` on EstimateAggroCount) to detect that case. Random
    // wanderers are deliberately NOT patrollers — their return is unpredictable.
    // `elite` marks an elite-tier body (Creature::isElite(): elite / rare-elite /
    // worldboss rank). It tunes only the WEIGHT a counted mob adds to the pull-
    // weight tally (see EstimateAggroCount's weightThirdsOut), never seeding,
    // assist, or pack membership — a normal mob is a third of an elite because the
    // MaxLeeroyMobs ceiling was set thinking in elites, and a big room of weak
    // trash should not force cautious one-cluster-at-a-time Advanced pulls.
    struct DynPullMob
    {
        float         x;
        float         y;
        float         z;
        bool          chainEligible;
        std::uint32_t packId = 0;
        float         aggroReach = 0.0f;
        bool          patroller = false;
        bool          elite = false;
    };

    // Pure Dynamic-pull estimate: how many mobs aggro if the party Leeroys on top
    // of `mobs[targetIdx]`? The camp spot is the target's position (where a Leeroy
    // fight happens). Returns the estimated body count; the caller compares it to
    // its Leeroy ceiling (count > ceiling => Advanced pull, else Leeroy).
    //   - Seed = the target, its atomic pack (shared non-zero `packId`), and every
    //     other mob that proximity-aggros the camp: within `aggroReach + combat-
    //     Spread` (2D), on the same level (`zTolerance`), and `chainEligible`.
    //     `combatSpread` widens the camp from a point to a disc to model players
    //     drifting to flank/kite during the fight.
    //   - One assist hop: a `chainEligible` mob within `assistRadius` (2D, same
    //     level) of a SEED mob joins via CallForHelp. Proximity aggro from a fixed
    //     camp does not chain, so assisted mobs do NOT seed further proximity or
    //     assist — exactly one ring.
    //   - Formation closure: any pack touched by the set is counted in full.
    // `zTolerance` keeps the estimate honest in multi-level rooms: WotLK inter-
    // floor gaps exceed it, so a mob a ramp above/below never counts. Separated
    // from the game-state resolution in DcPullPlanner::ClassifyPullAdvanced so
    // the logic is unit-testable.
    // `excludeLonePatrollers` (patrol-wait gate): when true, a mob marked
    // `patroller` that is NOT part of an atomic pack (`packId == 0`) is treated as
    // not `chainEligible` — it neither seeds proximity nor assists nor is assisted
    // — modelling "if we let this patrol pass". Formation/linked patrollers
    // (`packId != 0`) are unaffected: you cannot wait out half a formation.
    // `countedOut` (optional) receives the indices of every mob the estimate
    // counted, for diagnostic logging at the call site.
    // `weightThirdsOut` (optional) receives the counted set's PULL WEIGHT, in
    // thirds of an elite: each counted elite adds 3, each counted normal adds 1.
    // This is the value the verdict compares to a x3-scaled ceiling so a normal mob
    // weighs a third of an elite (the ceiling was tuned in elites). The return value
    // stays the raw body count (what `countedOut` enumerates) for diagnostics.
    std::uint32_t EstimateAggroCount(std::vector<DynPullMob> const& mobs,
                                     std::size_t targetIdx, float combatSpread,
                                     float assistRadius, float zTolerance,
                                     bool excludeLonePatrollers = false,
                                     std::vector<std::size_t>* countedOut = nullptr,
                                     std::uint32_t* weightThirdsOut = nullptr);

    // Pull CC-assist grace gate (pure). Decides whether a CC-impaired drag-back
    // should be ABORTED so the party drops passive and piles in to help the tank.
    // `impaired` is the caller's verdict that the leader tank is currently under a
    // pull-ruining control effect (stun / fear / confuse / root / heavy slow).
    // `ccSince` is the timestamp the CURRENT continuous impairment began (0 = not
    // impaired right now). Each tick: while impaired, arm/keep the latch and abort
    // once it has persisted for `graceMs`; while clear, disarm it. A brief micro-CC
    // that clears within the grace therefore never throws the pull away on a
    // flicker, while sustained CC (the pull is failing) releases the party. With
    // `graceMs` == 0 the very first impaired tick aborts. Returns true to abort and
    // writes the updated latch to `ccSinceOut` (which the caller persists in the
    // pull context). Separated from the Unit-state read at the call site so the
    // timing logic is unit-testable.
    bool ShouldAbortPullForCc(bool impaired, std::uint32_t ccSince,
                              std::uint32_t now, std::uint32_t graceMs,
                              std::uint32_t& ccSinceOut);

    // Pack-cannot-follow gate (pure). Decides whether a drag-back should be
    // ABANDONED because the mob being dragged has no combat movement — the caller's
    // `planted` verdict, read off UNIT_STATE_NO_COMBAT_MOVEMENT at the call site.
    // Such a mob has had its chase generator removed, so it holds the ground it was
    // tagged on however far the tank retreats: the drag is not slow, it is
    // impossible, and the tank should turn around and fight it where it stands.
    //
    // Shares the latch/grace contract of ShouldAbortPullForCc exactly (including the
    // now==0 corner and `confirmMs` == 0 firing on the first tick), and is delegated
    // to it — kept as a separate name because the QUESTION is different and the two
    // are tuned independently. The grace is a debounce, not a confidence threshold:
    // the unit state is exact, but some creatures toggle it transiently (planting
    // only for the duration of a cast), and those must not lose their drag.
    bool ShouldAbandonPlantedDrag(bool planted, std::uint32_t plantedSince,
                                  std::uint32_t now, std::uint32_t confirmMs,
                                  std::uint32_t& plantedSinceOut);

    // Camp-safety valve (pure). A held passive follower should trigger the valve
    // when it is in combat below `safetyHpPct` AND that has persisted for
    // `graceMs`. `attackerIsPullTarget` is the caller's verdict that everything
    // currently on the follower belongs to the pack being dragged — in which case
    // this is an ordinary drag taking splash, not a failed pull, and the valve
    // must not fire (raw HP% alone is the wrong question: a follower at 60% being
    // splashed by the dragged pack is a normal drag; at 60% from something ELSE, a
    // second pack found us and the maneuver is genuinely compromised).
    // `safetyHpPct` <= 0 disables the valve outright. `since` is the timestamp the
    // CURRENT continuous qualifying spell began (0 = fine); the latch/grace
    // contract mirrors ShouldAbortPullForCc exactly (including the now==0 corner).
    // `graceMs` == 0 fires on the first qualifying tick. Returns true to trip the
    // valve and writes the updated latch to `sinceOut` (persisted in the
    // follower's DcPullContext::campSafetySince by the caller).
    bool ShouldTripCampSafety(bool inCombat, float healthPct, float safetyHpPct,
                              bool attackerIsPullTarget,
                              std::uint32_t since, std::uint32_t now,
                              std::uint32_t graceMs, std::uint32_t& sinceOut);

    // Pull-mode blocking-trash stand-down gate (pure). In advanced-pull mode the
    // pull pipeline owns the pack it is working, so the blocking-trash trigger
    // stands down for it (engage-trash outranks the pull's deliberate wait band
    // and would otherwise preempt every pull). But a BYSTANDER — a pack the pull
    // never selected — appearing inside the aggro-shaped scan while the pull is
    // IDLE is exactly the case the scan exists for, and silently handing it to a
    // pipeline that is not looking at it left the tank walking into it under
    // Advance. Returns true to STAND DOWN (the pull pipeline owns the tick):
    // always for the pull's own pack, and for anyone once a maneuver is in
    // flight (non-Idle — never thrash it). Returns false only for a bystander
    // while the pull is Idle: the blocking-trash walk-in owns that tick. The
    // game-state read (pack identity vs decision/pull target, phase) stays at
    // the trigger; this carries only the decision so it is unit-testable.
    bool ShouldStandDownForPull(bool packIsPullsOwn, bool pullPhaseIdle);

    // Orphaned-pull release gate (pure). The effective pull mode can be forced
    // OFF while a pull is still standing — a PERSISTENT anchored event takes the
    // tank (DungeonClearPullModeCurrentValue), or a Dynamic verdict drops. The
    // pull FSM is then unreachable: its non-combat driver (DungeonClearPullTrigger)
    // gates on the EFFECTIVE mode, so nothing ever runs the Engage->Idle cleanup,
    // while the followers' camp hold and the party-spread gate read the LATCHED
    // mode and keep obeying a camp nobody can move. Live in heroic Old Hillsbrad
    // (runs tr-20260801-174432-3/-7): an unplanned camp pull during the barrel
    // event left phase=Engage, the party pinned ~100yd back at the frozen camp,
    // the barrel drive holding for a party that had been told to stand there, and
    // stranded-recovery teleporting everyone forward once a minute only for
    // hold-at-camp to walk them straight back. Returns true to dismantle the
    // standing pull (phase -> Idle, camp cleared) so the party reverts to plain
    // follow. `effectiveOn` is the current effective pull mode; `standing` is
    // "there is something to release" (non-Idle phase or a marked camp). Never
    // releases mid-maneuver: `partyInCombat`, a holding phase (Forming/Advancing/
    // Returning — the drag must finish) and `bossPullback` (a pull-back drag runs
    // with pull mode off BY DESIGN) all hold it off.
    //
    // `partyInCombat` IS PARTY-WIDE, and used to be the leader's own flag. That was
    // wrong in the one arrangement this release exists inside: a camp fight. The
    // pack is dragged to the camp and handed to the followers, and the tank in
    // Engage is routinely flag-clear for stretches of it — a scripted stage tags at
    // range, so the pack arrives late and strung out, and threat lands on whoever it
    // reaches first. Reading only the tank there dismantles the camp WHILE the
    // followers are fighting at it: the hold releases, the party reverts to follow,
    // and the tank walks off to form the next pull with the last pack still up. In
    // the MgT rotunda that is the difference between a five-elite fight and a
    // ten-elite one, and 8 of the 9 rotunda losses in tp-20260805-191829-1 died
    // within a few yards of an authored camp.
    bool ShouldReleaseStandingPull(bool effectiveOn, bool standing, bool partyInCombat,
                                   bool holdingPhase, bool bossPullback);

    // Dynamic-verdict drop grace gate (pure). A standing Leeroy/Advanced verdict
    // must survive a TRANSIENT no-target read (door veto flicker, long-path cache
    // mid-rebuild, far-targets poll boundary): dropping it instantly flips the
    // pull-mode bool, releasing the camp hold and stripping daze immunity for a
    // single bad tick, then re-deriving everything — the party lurch. `targetPresent`
    // is the caller's verdict that the pull target resolved this tick. `lostSince`
    // is the timestamp the target first resolved null while the verdict was
    // standing (0 = present). Each tick: while lost, arm/keep the latch and drop
    // once it has persisted for `graceMs`; while present, disarm it. Mirrors
    // ShouldAbortPullForCc's latch/clear contract (including the now==0 corner
    // and graceMs==0 => drop on the first lost tick). Returns true to drop the
    // verdict and writes the updated latch to `lostSinceOut` (persisted in
    // DcPullContext::targetLostSince by the caller).
    bool ShouldDropPullVerdict(bool targetPresent, std::uint32_t lostSince,
                               std::uint32_t now, std::uint32_t graceMs,
                               std::uint32_t& lostSinceOut);

    // Leeroy roll-in gate (pure). True when the party's scout-lag hold should
    // release because the tank is committing to a Leeroy on the verdicted pack:
    // the standing decision is Leeroy (1) and the tank is within
    // `commitRange + lead` (2D) of the live target. `decision` 0 (none, still
    // scouting) and 2 (Advanced — the camp machinery owns the party) never roll
    // in, nor does a dead/unresolvable target. The game-state resolution
    // (decision, live target, distances) stays in
    // DcLeaderSignal::IsLeaderDynamicScouting; this carries only the decision
    // logic so it is unit-testable.
    bool ShouldRollInForLeeroy(std::uint32_t decision, bool targetAlive,
                               float tankToTarget2d, float commitRange, float lead);

    // Patrol-wait gate (pure). A pull is "patrol-contended" when the ONLY thing
    // pushing the aggro estimate over the Leeroy ceiling is one or more lone
    // patrollers: `fullCount > ceiling` but `reducedCount <= ceiling` (the reduced
    // pass excluded them — see EstimateAggroCount's excludeLonePatrollers). For
    // such a pack a human waits the patrol out rather than committing the heavier
    // Advanced maneuver. Returns true to KEEP WAITING (hold the pull), false to
    // proceed (commit the standing verdict). Contended: arm/keep the `waitSince`
    // latch and wait until `waitMs` has elapsed, then proceed with the heavy
    // verdict (don't stall the run for a stationary/slow patrol) — the latch stays
    // armed past the timeout so the wait does not re-fire on the same contention.
    // Not contended (patrol left chain range, or never the cause): clear the latch
    // and proceed at once. `waitMs` == 0 proceeds immediately. Mirrors
    // ShouldAbortPullForCc's by-reference latch/clear contract. The game-state read
    // (the two estimates, the live target/commit distance that decides WHEN to arm)
    // stays in DcPullPlanner::UpdateDynamicPullMode.
    bool ShouldWaitForPatrol(std::uint32_t fullCount, std::uint32_t reducedCount,
                             std::uint32_t ceiling, std::uint32_t waitSince,
                             std::uint32_t now, std::uint32_t waitMs,
                             std::uint32_t& waitSinceOut);

    // Scripted-stage MUSTER gate (pure). A ScriptedPullRegistry stage is a planned
    // fight against a hand-counted pack, and the party should arrive at it topped
    // up — the way it arrives at a boss — rather than merely "not resting".
    //
    // The ordinary between-pulls floors do not deliver that. They are
    // min(90, AiPlayerbot.AlmostFullHealth) HP and min(75, AiPlayerbot.HighMana)
    // mana, which on stock config is 85/65, and 65% healer mana is thin for a
    // five-elite heroic pack that contains its own healer. Live
    // (tr-20260805-191834-3): the party reported "Shannon (low mana), Erinerice
    // (low HP)" at 12:25, the gate released at 12:30, the stage armed, and the tank
    // was dead 22 seconds into the fight with the whole party following inside 22
    // more.
    //
    // Returns true to KEEP HOLDING (do not arm the stage), false to proceed.
    // BOUNDED, and that bound is the point: the muster floors sit ABOVE what stock
    // bots eat/drink back up to, so an unbounded wait on a bot with no water in
    // its bags is a run that never continues. Arm/keep the `waitSince` latch while
    // the party is short, proceed once `waitMs` elapses, and leave the latch armed
    // so the same muster cannot re-fire. `toppedUp` (the caller's party read
    // against the muster floors) or `!stagePending` clears the latch and proceeds
    // at once; `waitMs` == 0 proceeds immediately. Same by-reference latch/clear
    // contract as ShouldWaitForPatrol. The party read stays in DcPartyState.
    //
    // `minMs` is the substance floor: once the muster has ARMED (the party was
    // genuinely below the floors for at least one tick), it holds for at least
    // min(minMs, waitMs) even if the percentages close sooner. An instantaneous
    // percentage test releases the tick one AoE heal crosses the line — live
    // musters ran 1-5s and no one ever drank (tp-20260806-212646-1). A party
    // already at the floors when the stage comes due still arms nothing.
    bool ShouldMusterForScriptedStage(bool stagePending, bool toppedUp,
                                      std::uint32_t waitSince, std::uint32_t now,
                                      std::uint32_t waitMs, std::uint32_t minMs,
                                      std::uint32_t& waitSinceOut);

    // Turn-and-plant gate (pure). A human tank dragging a pack back to camp turns
    // and fights the moment the pack is glued to it, rather than sprinting the
    // whole leg back-turned (the single biggest visual bot-tell, and why the
    // daze-immunity cheat exists). True when the drag-back should stop early and
    // the tank turn to fight: the pack is gathered (EVERY live attacker distance
    // <= `glueRadius`) for >= `glueTicksNeeded` consecutive maneuver ticks, this
    // is not an LOS-break pull (`losPull` — those must reach the corner), there is
    // something chasing (empty `attackerDists` => evade/fizzle, never a plant),
    // and at least the first HALF of the return leg is covered
    // (`distToCamp <= legStartDist / 2`, keeping the neighbour-pack clearance the
    // camp was measured for). `plantTicks` is the per-pull debounce latch: it is
    // incremented while the gather condition holds and reset to 0 the moment it
    // breaks, mirroring ShouldAbortPullForCc's by-reference latch contract — so a
    // single-tick noise spike can never trip an early plant. The game-state read
    // (attacker distances, leg progress) stays in DungeonClearPullManeuverAction;
    // this carries only the decision so it is unit-testable.
    bool ShouldPlantEarly(std::vector<float> const& attackerDists, float glueRadius,
                          std::uint32_t glueTicksNeeded, bool losPull,
                          float distToCamp, float legStartDist,
                          std::uint32_t& plantTicks);

    // Tag walk-in stop distance (pure), in yards from the target.
    //
    // The core only re-checks a pack's aggro on MOVEMENT, and its notice test is the
    // plain centre-to-centre distance vs Creature::GetAggroRange. So the tank must
    // GLIDE to a point strictly inside `aggroRange` — arriving is what crosses the
    // threshold — and it stops 2yd in.
    //
    // `closingMs` is HOW LONG THE TANK HAS BEEN CLOSING, and the creep it drives is a
    // backstop: a tank parked exactly at the edge is never re-evaluated (no relocation
    // -> no MoveInLineOfSight), so after `graceMs` the stop point steps inward at
    // `creepYardsPerSec` until something notices. It rarely runs at all.
    //
    // `creepFloor` bounds it — the closest the creep may bring the stop point. 0 lets
    // it run to body contact, which is right for an ordinary pull: the pack it ends up
    // touching is the pack it came for. A SCRIPTED stage passes a real floor, because
    // it is standing in a room full of formations the plan has deliberately left up
    // and the yards between the aggro edge and the pack's feet are the ones that reach
    // them.
    //
    // `forceTagOut` comes back true when even the edge is inside melee reach — a
    // much-higher-level tank against the core's 5yd minimum aggro, where closing can
    // never cross the threshold and the caller must swing to start the fight instead.
    //
    // The game-state read (GetAggroRange, both combat reaches, and WHICH event
    // `closingMs` is measured from — phase start for an ordinary pull, stand-spot
    // arrival for a scripted stage) stays in DungeonClearPullAction.
    float PullTagStopDistance(float aggroRange, float meleeReach,
                              std::uint32_t closingMs, std::uint32_t graceMs,
                              float creepYardsPerSec, float creepFloor,
                              bool& forceTagOut);

    // Threat-lead follower-release gate (pure). After the leader tank enters
    // combat a real group gives it a beat to gather and establish AoE threat
    // before DPS pile in; this holds a follower's fight assist for `leadMs` after
    // the leader's CURRENT combat began (`combatSinceMs`; 0 = leader not in
    // combat). Healers release immediately (`isHealer` — a withheld heal is a
    // wipe and heals don't rip threat the way DPS openers do). DPS release once
    // the lead has elapsed, with THREE bypasses: `alreadyInCombat` (this follower
    // is itself already flagged in combat — it can't over-aggro by moving into
    // sight and MUST be driven onto the tank's fight rather than left with nothing
    // to do, where reviving stock follow-master would drift it to the human), the
    // tank's HP below `panicHpPct` (it is LOSING the fight — pile in; <= 0 disables
    // the bypass), and `leadMs` == 0 (feature off). The game-state read (leader
    // combat stamp, healer role, tank HP, this bot's combat flag) stays in
    // DcLeaderSignal::IsLeaderFightAssistWanted.
    bool ShouldReleaseFollower(bool isHealer, bool alreadyInCombat,
                               std::uint32_t combatSinceMs,
                               std::uint32_t now, std::uint32_t leadMs,
                               float tankHealthPct, float panicHpPct);

    // Phantom-combat classifier (pure). True when a bot is FLAGGED in combat but has
    // nothing it can actually fight from where it stands: nothing is meleeing it
    // (`hasAttacker`), it has no victim of its own (`hasVictim`), and its combat is
    // not explained by any LEGITIMATE holder (`hasLegitimateHolder`). The caller
    // computes the last from the bot's CombatManager PvE refs: a holder counts as
    // legitimate only if it is alive, non-evading, and PATH-REACHABLE from the bot,
    // and an opaque combat with NO unit references at all (a script-forced state) is
    // also treated as legitimate so it is never cleared. This is the signature of the
    // "a mob spawned across the map / behind a gate and tagged me" deadlock: the core
    // combat flag never drops because the holder is unreachable, and every DC gate
    // that keys off "someone is in combat" (the fight-assist arm, the party-engaged
    // latch) then spins forever. Because legitimacy keys on REACHABILITY rather than
    // distance, a fleeing/kiting party (pursuers always reachable) trips
    // `hasLegitimateHolder` and is never phantom — as does any real fight (which
    // trips hasAttacker/hasVictim). The game-state reads stay in
    // DungeonClearBreakStuckCombatTrigger.
    inline bool IsPhantomCombat(bool inCombat, bool hasAttacker, bool hasVictim,
                                bool hasLegitimateHolder)
    {
        return inCombat && !hasAttacker && !hasVictim && !hasLegitimateHolder;
    }

    // Is a holder that PASSED the legitimacy test actually prosecuting the fight?
    //
    // Reachability alone says the holder COULD come; it does not say it IS coming. In
    // an instance a creature never leashes (the dungeon short-circuit in
    // CanCreatureAttack), so a mob that tagged the party and then stopped keeps its
    // combat reference alive forever from wherever it stands — alive, non-evading,
    // path-reachable, allowed to attack, and completely inert. That reads as a REAL
    // fight to the legitimacy test, so the phantom hatch stands down, while every DC
    // gate that keys off "someone is in combat" spins. Live in tr-20260804-153254-2:
    // Ushkuk and Olanne held by a Sunblade Mage Guard 68-69yd back, 100% HP,
    // attackers=0 victim=-, for the eight minutes until the run was stopped by hand.
    //
    // A holder is prosecuting the fight if it is either already INSIDE engage range
    // (that is a fight whatever the numbers say) or CLOSING on us. `closing` comes
    // from the caller's DcProgressWatchdog::TickClosing over the nearest holder's
    // distance, so this cannot be fooled by the party moving: a chaser or a kited mob
    // improves the closest-ever distance and keeps the hatch inert, while a party that
    // walks away from a stationary holder only makes the distance WORSE. The first
    // sample arms the watchdog and counts as closing, so the streak clock in
    // ShouldBreakStuckCombat only starts once the holder has demonstrably stopped.
    inline bool IsHolderProsecutingFight(bool haveHolder, float holderDist,
                                         float engageRange, bool closing)
    {
        return haveHolder && (holderDist <= engageRange || closing);
    }

    // "Can this follower attack from where it stands?" for the camp-assist handoff,
    // expressed in the SAME metric the stock reach action enforces.
    //
    // This exists because the two halves of that handoff used different metrics and
    // opened a dead band. Stock ReachSpellAction is constructed with
    // GetRange("spell") but tests it as `!IsWithinCombatRange(target, spellRange)`,
    // and Unit::IsWithinCombatRange adds GetCombatReach(bot) + GetCombatReach(target)
    // to the threshold — so stock stops closing at spellRange + combatReachSum. DC
    // previously required `dist <= spellRange - CONTACT_DISTANCE`, roughly
    // combatReachSum + 0.5 yards TIGHTER than stock would ever walk. A ranged bot
    // left in that window is simultaneously "in range, stop moving" (stock) and "out
    // of range, yield to stock" (DC): neither side acts, the bot never engages, and
    // the tank's spread gate waits on it until the run deadlocks.
    //
    // Ranged therefore mirrors IsWithinCombatRange exactly, making DC's engage window
    // the precise complement of stock's keep-closing window — no gap by construction,
    // and self-syncing if SpellDistance is retuned. Melee passes its own
    // reach-inclusive threshold (reachSum + 1.0), which is already WIDER than stock
    // reach-melee's stop point (reachSum + MeleeDistance, default 0.75) and so
    // overlaps rather than gaps — which is why only ranged ever hung.
    inline bool IsWithinAssistAttackRange(bool isMelee, float dist, float meleeRange,
                                          float spellRange, float combatReachSum)
    {
        return isMelee ? dist <= meleeRange : dist <= spellRange + combatReachSum;
    }

    // Stuck-combat fire gate (pure, streak clock by reference). Given whether the bot
    // is CURRENTLY in phantom combat (IsPhantomCombat above), arm/hold/reset a streak
    // clock and report when the phantom state has persisted continuously for
    // `timeoutMs` — the point at which the caller force-clears combat. `sinceMs` is
    // the caller-owned latch (0 = not streaking): armed to `now` on the first phantom
    // tick, cleared to 0 the instant the phantom state breaks (a real target
    // reappeared), so a one-tick target gap can never trip it. The timeout is
    // deliberately LONG (default 15s) so an ENCOUNTER that intentionally holds the
    // party in combat with no reachable enemy (a scripted wave gap, a gauntlet) is
    // never mistaken for a stuck flag. `timeoutMs == 0` disables the gate. Mirrors
    // ShouldWaitForPatrol's by-reference-latch contract.
    inline bool ShouldBreakStuckCombat(bool phantom, std::uint32_t now,
                                       std::uint32_t timeoutMs, std::uint32_t& sinceMs)
    {
        if (!phantom || timeoutMs == 0)
        {
            sinceMs = 0;
            return false;
        }
        if (sinceMs == 0)
            sinceMs = now ? now : 1;   // arm; avoid the 0 "unarmed" sentinel on ms 0
        // `now >= sinceMs` guards the unsigned subtraction: false only on the ms-0
        // arming tick (sinceMs nudged to 1) or a backward clock step / getMSTime wrap,
        // where "no time has elapsed yet" is the right answer.
        return now >= sinceMs && (now - sinceMs) >= timeoutMs;
    }

    // Flagged-in-combat driving gate (pure, streak clock by reference).
    //
    // THE DISTINCTION: `flagged` is the core combat FLAG (Unit::IsInCombat);
    // `engaged` is whether anyone in the party is ACTUALLY FIGHTING — a victim, or
    // something meleeing/casting at us. The DC driving ladder used to stand down on
    // `flagged` alone, which is right for a real fight and catastrophic for a flag
    // with no fight behind it.
    //
    // Why that state exists at all (Arcatraz heroic, 2026-07-29): the Eredar
    // Soul-Eaters' Entropic Aura (36784) is a hostile area aura with a **45yd**
    // radius against a ~20yd creature aggro radius. That leaves a 25-yard ANNULUS
    // where the party is flagged in combat but nothing has aggroed it — no
    // attacker, no victim, no threat. Playerbots does not flip to the combat engine
    // on the flag (only AttackAction / PullMyTargetAction call ChangeEngine, and
    // both need a chosen target; UpdateAIInternal explicitly tolerates
    // non-combat-engine-while-flagged). So the combat ladder never runs either, and
    // with the driving ladder standing down on the flag the run freezes SILENTLY —
    // and self-locks, because the freeze happens before the party ever reaches
    // aggro range, so the fight it is waiting for can never start. Measured: five
    // frozen tanks at 20.8-34.8yd from the pack; the one that got inside 20yd
    // fought and died instead.
    //
    // The grace is the safety margin in the other direction: a real fight can have
    // a one-tick hole (the target dies and nothing has re-acquired yet), and
    // resuming the drive there would walk the tank out of a live fight. The
    // no-engagement state must therefore persist continuously for `graceMs` before
    // driving resumes. `sinceMs` is the caller-owned latch (0 = not streaking),
    // cleared the instant a real engagement reappears. `graceMs == 0` disables the
    // grace (drive as soon as the flag has nothing behind it).
    //
    // Returns TRUE when the driving ladder may run.
    inline bool MayDriveWhileFlagged(bool flagged, bool engaged, std::uint32_t now,
                                     std::uint32_t graceMs, std::uint32_t& sinceMs)
    {
        if (!flagged)
        {
            sinceMs = 0;
            return true;            // not in combat at all — the ordinary case
        }
        if (engaged)
        {
            sinceMs = 0;
            return false;           // a real fight owns the bot; stay out of it
        }
        if (graceMs == 0)
            return true;
        if (sinceMs == 0)
            sinceMs = now ? now : 1;   // arm; avoid the 0 "unarmed" sentinel on ms 0
        // `now >= sinceMs` guards the unsigned subtraction against a backward clock
        // step / getMSTime wrap, where "no time has elapsed yet" is the right answer.
        return now >= sinceMs && (now - sinceMs) >= graceMs;
    }

    // Bystander-detour borrow watchdog (pure, by-reference latch).
    //
    // Above commit range the tank's approach belongs to Advance (the long-path
    // glide). When a bystander pack's aggro sphere actually sits on the line to
    // the pull target, the pull BORROWS the tick and walks an orbit around it
    // instead — but while it holds the tick, Advance's own wedge/stall ladder is
    // not running, so the borrow must be bounded or a non-converging orbit would
    // freeze the run outright. Bounding it in time is the same "preference, never
    // refusal" rule the detour itself follows: when the borrow stops paying, hand
    // the walk back and let the straight route happen.
    //
    // It is a NO-PROGRESS clock, not a plain deadline: `bestDist` records the
    // closest the tank has been to the pack on this detour, and any tick that
    // beats it by `progressEpsilon` restamps the clock. A long legitimate arc
    // around a big sphere therefore never expires — only an orbit that has
    // genuinely stopped closing burns the budget.
    //
    // `sinceMs == 0` means "not currently borrowing" and arms the clock.
    // `timeoutMs == 0` disables the give-up entirely.
    //
    // The caller owns the latch either side of this: resetting it when the leg's
    // target changes, and deciding what a `false` means. The pull makes it
    // one-shot per pack (DcPullContext::avoidGaveUp) rather than simply retrying
    // once the clock would re-arm — the tank closing one more yard under Advance
    // is enough to satisfy the progress test, so a retrying caller would cancel
    // Advance's spline every fraction of a second and thrash the movement it was
    // supposed to be handing back.
    inline bool ShouldKeepAvoidDetour(std::uint32_t now, float curDist,
                                      std::uint32_t timeoutMs, float progressEpsilon,
                                      std::uint32_t& sinceMs, float& bestDist)
    {
        if (sinceMs == 0 || curDist < bestDist - progressEpsilon)
        {
            sinceMs = now ? now : 1;   // arm; avoid the 0 "unarmed" sentinel on ms 0
            bestDist = curDist;
            return true;
        }
        if (timeoutMs == 0)
            return true;
        // `now >= sinceMs` guards the unsigned subtraction against a backward clock
        // step / getMSTime wrap, where "no time has elapsed" is the right answer.
        return !(now >= sinceMs && (now - sinceMs) >= timeoutMs);
    }

    // --- chase leash -------------------------------------------------------
    // What the walk toward a MOVING trash target should do this tick.
    enum class ChaseVerdict : std::uint32_t
    {
        Follow = 0,   // walk in as planned
        Hold   = 1,   // stand still; let the mob come back to us
        GiveUp = 2,   // the hold ran out — the caller decides what that means
    };

    // Chase-leash gate (pure, by-reference latch).
    //
    // A pull target is latched by GUID and its position is re-read live every
    // tick, so a target that WALKS — a DB patrol, a wanderer, a mob repositioned
    // by its own script — turns every approach into a pursuit. The tank follows it
    // wherever it goes, and when the route it walks passes behind other packs the
    // tank walks through those packs' aggro arcs and arrives at the camp with the
    // whole room. That is a pursuit nobody chose: the pull was planned against the
    // pack's position AT SELECTION TIME (that is what sized the estimate and where
    // the camp was measured from), and the moment the pack leaves that spot the
    // plan is about ground the mob is no longer standing on.
    //
    // A human tank does not chase a patrol. It waits at the spot it picked, and
    // the patrol — by definition a loop — comes back. This is that rule:
    //
    //   `driftFromAnchor` : how far the target has moved from where it stood when
    //                       we committed to it (2D).
    //   `gapAtAnchor`     : distance from the tank's commit spot to that anchor.
    //   `gapNow`          : distance from the SAME commit spot to the target now.
    //                       Measured from the fixed origin, not the tank's live
    //                       position — from a moving origin the gap always shrinks
    //                       as we walk and the leash could never engage.
    //   `destinationHot`  : the target is currently standing inside ANOTHER pack's
    //                       aggro sphere. Reaching it means waking that pack no
    //                       matter how the route bends, so it is never walkable
    //                       ground however small the drift is.
    //
    // Follow while the target is still near where we picked it (`drift <=
    // leashYards`) OR while it has come at least as close to our commit spot as it
    // was then (`gapNow <= gapAtAnchor` — an inbound patrol rounding a wide loop
    // is exactly what we are waiting for, and must not be held on drift alone).
    // Otherwise the target is RECEDING or standing somewhere we must not follow it
    // to: arm the hold latch and stand still until it comes back, and report
    // GiveUp once the hold has burned `holdMs` so no patrol can stall a run.
    //
    // `leashYards` <= 0 disables the gate outright (Follow, latch cleared) —
    // the historical always-chase behaviour, expressible from config.
    // `holdMs` == 0 gives up on the first receding tick (never holds).
    // Latch/clear contract mirrors ShouldWaitForPatrol: the latch stays armed past
    // the timeout, so GiveUp repeats until the caller re-anchors or drops the
    // target rather than silently re-entering a fresh hold.
    inline ChaseVerdict DecideChase(float driftFromAnchor, float gapAtAnchor,
                                    float gapNow, bool destinationHot,
                                    float leashYards, std::uint32_t now,
                                    std::uint32_t holdMs, std::uint32_t& holdSince)
    {
        if (leashYards <= 0.0f)
        {
            holdSince = 0;
            return ChaseVerdict::Follow;
        }
        if (!destinationHot &&
            (driftFromAnchor <= leashYards || gapNow <= gapAtAnchor))
        {
            holdSince = 0;
            return ChaseVerdict::Follow;
        }
        if (holdMs == 0)
            return ChaseVerdict::GiveUp;
        if (holdSince == 0)
            holdSince = now ? now : 1;   // arm; avoid the 0 "unarmed" sentinel on ms 0
        // `now >= holdSince` guards the unsigned subtraction against a backward
        // clock step / getMSTime wrap, where "no time has elapsed" is right.
        if (now >= holdSince && (now - holdSince) >= holdMs)
            return ChaseVerdict::GiveUp;
        return ChaseVerdict::Hold;
    }

    // Engage-fizzle handoff latch (pure). An advanced-pull "camp fight" ended with
    // the tank out of combat but the pulled pack still ALIVE and IDLE — the drag
    // fizzled (a planted caster evaded home the moment the tank broke LOS at camp).
    // Re-pulling repeats the exact same fizzle, so after `maxFizzles` consecutive
    // fizzles of the same pack the caller hands it to the normal walk-in engage
    // instead. `pulledAliveIdle` is the caller's verdict that the pull target
    // resolved and is alive & out of combat (a fizzle this tick). `sameTarget` is
    // true when this fizzle is of the SAME pack the latch already holds (compared
    // against the OLD latched target, before the caller re-stamps it). On a fizzle
    // of a new pack the count restarts at 1; a pack that died or is still being
    // fought (`pulledAliveIdle == false`) clears the count to 0. `fizzleCount` is
    // the by-reference latch (mirrors ShouldWaitForPatrol's contract) and is left
    // holding the updated consecutive-fizzle count for the caller's diagnostics.
    // Returns true when the pack should be handed off (count reached `maxFizzles`).
    // The game-state read (guid identity, alive/combat) and the fizzleTarget guid
    // bookkeeping stay in DungeonClearPullAction's Engage-cleanup branch.
    bool ShouldHandoffFizzledPull(bool pulledAliveIdle, bool sameTarget,
                                  std::uint32_t maxFizzles, std::uint32_t& fizzleCount);

    // Squared 2D distance from point P to segment (A,B).
    float DistSqToSegment2D(float px, float py,
                            float ax, float ay,
                            float bx, float by);

    // The point on polyline `route` whose XY projection is nearest (px,py), with
    // Z linearly interpolated along the winning segment. False (and `out`
    // untouched) when the polyline holds fewer than two points.
    //
    // 2D on purpose, unlike PathProgressCursor below: the caller is asking
    // "where on this walk do I come closest to that mob", and a mob standing in
    // a side room off a corridor is on the corridor's floor by construction —
    // the consumer (the en-route sweep in DcTargeting) has already restricted
    // candidates to the bot's own Z level via BystanderSpheres, so the storey
    // ambiguity a 3D cursor exists to resolve cannot arise here. Interpolating Z
    // rather than snapping to a vertex matters for the line-of-sight ray the
    // caller then shoots from `out`: a ramp leg's endpoints can sit several
    // yards above and below the point actually nearest the mob.
    bool NearestPointOnPolyline2D(std::vector<G3D::Vector3> const& route,
                                  float px, float py, G3D::Vector3& out);

    // True if the 2D segment (A,B) intersects the axis-aligned box
    // [minX,maxX] x [minY,maxY]. Liang-Barsky slab clip: returns true even
    // when BOTH endpoints lie outside the box but the segment passes through
    // it (the case that matters for a thin door panel a path step straddles).
    bool SegmentIntersectsAABB2D(float ax, float ay, float bx, float by,
                                 float minX, float minY,
                                 float maxX, float maxY);

    // Index of the route-polyline vertex the bot has walked up to — its PROGRESS
    // CURSOR along that route. Measured in 3D on purpose.
    //
    // A 2D-nearest pick reads the wrong FLOOR wherever a dungeon stacks rooms
    // over one another. Shadowfang Keep's tower is the worst case: the navmesh
    // column above the Fenrus room carries surfaces at Z = 88, 94, 96, 102, 105,
    // 114, 129 (the floor the tank is standing on), 139, 156 and 168, because the
    // staircase up to Wolf Master Nandos climbs almost directly overhead. The
    // route vertex on the landing 10yd UP sat 1.81yd from the tank in 2D while
    // the tank's own floor vertex sat 1.98yd away — so a 2D cursor snapped a
    // storey up, and both door consumers then read the corridor as if the tank
    // were already at the top of the stairs: the blocking-door scan started from
    // up there (synthesising a bot -> overhead bee-line straight through the
    // ceiling) and flagged Arugal's Lair 27yd ABOVE the tank, while
    // DistAlongPathToClosedDoor called that door "8.0yd along path" when the real
    // walk was the whole staircase. The tank parked at the foot of the stairs on
    // a door it was nowhere near and — Arugal's Lair being script-only since the
    // voidwalker fix — auto-paused the run instead of walking up to kill Nandos,
    // whose death is the only thing that ever opens it (runs
    // tr-20260802-101606-12 / -16 / -17).
    //
    // Returns 0 for an empty route; callers guard emptiness themselves.
    std::size_t PathProgressCursor(std::vector<G3D::Vector3> const& route,
                                   float botX, float botY, float botZ);

    // Index of the LATEST crumb within `rejoinRadius` (3D) of `cur`, or
    // TrailRejoinNone if none qualifies. Used by the breadcrumb recorder: on a
    // >kJump discontinuity (a drag-back / drop-down), rather than wiping the
    // whole trail, rejoin at the most recent crumb near where the bot now stands
    // and truncate everything ahead of it. Latest-wins so a trail that loops near
    // itself rejoins at the most recent pass, keeping the walked-distance
    // semantics the camp walk-back relies on intact. 3D on purpose — a crumb
    // directly above/below (different floor) must not count as a rejoin.
    std::size_t FindTrailRejoin(std::vector<Position> const& crumbs,
                                Position const& cur, float rejoinRadius);

    // The one shared breadcrumb-trail walk-back primitive (Kernel A). One step of
    // a backward walk, produced by WalkTrailBack for each crumb newest -> oldest.
    // `index` is the crumb's index in the source vector. `along` is the
    // accumulated 3D walked distance from the anchor to `crumb` (the far, away-
    // from-anchor end of this segment); `alongPrev` is that distance at the near
    // end `segStart`. `PointAt(d)` returns the point exactly `d` yards back along
    // THIS segment (linear interpolation, `d` clamped to [alongPrev, along]) — the
    // ce2e89e scout-lag interpolation fix, now available to every walk-back caller
    // instead of one. Positions carry Z, so the walk's jump guard (see
    // WalkTrailBack) and any distance test the caller runs off these fields catch a
    // vertical seam a 2D measure would miss (the seam-undermap / trail-dance bug
    // class: "the arrived? test and the target must share a metric").
    struct TrailStep
    {
        std::size_t index;
        float       alongPrev;
        float       along;
        Position    segStart;   // near end (toward the anchor)
        Position    crumb;      // this crumb (far end, away from the anchor)

        Position PointAt(float d) const
        {
            float const span = along - alongPrev;   // == this segment's 3D length
            if (span <= 0.0001f)
                return crumb;
            float t = (d - alongPrev) / span;
            if (t < 0.0f)
                t = 0.0f;
            if (t > 1.0f)
                t = 1.0f;
            return Position(
                segStart.GetPositionX() + (crumb.GetPositionX() - segStart.GetPositionX()) * t,
                segStart.GetPositionY() + (crumb.GetPositionY() - segStart.GetPositionY()) * t,
                segStart.GetPositionZ() + (crumb.GetPositionZ() - segStart.GetPositionZ()) * t);
        }
    };

    // Walk `crumbs` newest -> oldest starting from `anchor`, accumulating 3D
    // segment length, and call `visit(step)` for each crumb. Stop when a segment
    // exceeds `jumpGuard`: a gap that large is a drag / teleport / drop-down seam,
    // and nothing beyond it is contiguously "behind" the anchor (measuring the
    // segment in 3D makes the guard catch a vertical drop a 2D measure treats as
    // contiguous — the "camp/trail lands on the wrong floor" bug). `visit` returns
    // false to stop the walk early (e.g. the caller accepted a point). Returns the
    // total distance walked (the last visited step's `along`, or 0 for an empty /
    // immediately-discontinuous trail). This is the single primitive behind
    // ComputeSafeCamp, ComputeTrailCamp, GetLeaderScoutTrailPoint and
    // GetLeaderScoutTrail; each supplies its own acceptance predicate (which may
    // probe reachability off the map) as `visit`, so the accounting, the jump
    // guard, and the interpolation live in exactly one tested place.
    template <class Visit>
    inline float WalkTrailBack(std::vector<Position> const& crumbs, Position const& anchor,
                               float jumpGuard, Visit&& visit)
    {
        Position prev = anchor;
        float along = 0.0f;
        for (std::size_t i = crumbs.size(); i-- > 0; )
        {
            Position const& c = crumbs[i];
            float const seg = prev.GetExactDist(&c);
            Position const segStart = prev;
            prev = c;
            if (seg > jumpGuard)
                break;
            float const alongPrev = along;
            along += seg;
            TrailStep const step{ i, alongPrev, along, segStart, c };
            if (!visit(step))
                break;
        }
        return along;
    }

    // The shared jump-guard threshold for WalkTrailBack: a 3D segment longer than
    // this is a trail discontinuity. Was re-declared as a local `kJumpGuard = 12`
    // in each of the four walk-back clones; hoisted here so the four agree by
    // construction.
    inline constexpr float TrailJumpGuard = 12.0f;
}

#endif
