/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCPULLCONTEXT_H
#define _PLAYERBOT_DCPULLCONTEXT_H

#include <vector>

#include "ObjectGuid.h"
#include "Position.h"

// Advanced-pull (LOS pull-to-camp) sub-phase. The leader's private control state
// AND the cross-context signal followers read to decide when to hold + go passive
// at camp. Forming/Advancing/Returning are the "holding" phases that keep the
// party passive at camp; Idle and Engage release them. See
// DcLeaderSignal::IsPullPhaseHolding.
enum class DcPullPhase : uint32
{
    Idle      = 0,
    Forming   = 1,
    Advancing = 2,
    Returning = 3,
    Engage    = 4,
};

// The Dynamic-pull governor's standing verdict for the current pack, persisted so
// followers (roll-in gate) and the addon status panel can read it. The underlying
// values are WIRE-STABLE: DcStatusPublisher sends `decision` verbatim in the addon
// status payload, and it is passed as the `decision` arg to
// DungeonClearMath::ShouldRollInForLeeroy (which keys on Leeroy == 1). Do NOT
// renumber — extend with new codes at the end only.
enum class DcPullDecisionCode : uint32
{
    None       = 0,  // no standing verdict (still scouting / no target resolved)
    Leeroy     = 1,  // straight face-pull; the party rolls in on the tank
    Advanced   = 2,  // LOS pull-to-camp maneuver; the camp machinery owns the party
    PatrolHold = 3,  // hold at commit range, waiting a lone patrol out before pulling
};

// All transient state for one advanced/dynamic pull run, owned as a single value
// (DungeonClearPullContextValue, "dungeon clear pull context") so the whole pull
// FSM resets in lockstep through exactly one Reset(). This replaced seven loose
// ManualSetValue globals whose resets were scattered and provably incomplete —
// the "stale latch survives pause/skip/resume" bug class. Add a new pull-run
// field HERE (never as a separate value) so it can never be forgotten by a reset.
//
// Leader-owned. Followers read `phase`/`camp` cross-context through the leader's
// copy of this value (DcLeaderSignal::GetLeaderPullInfo / GetLeaderCampHold).
struct DcPullContext
{
    // --- FSM sequencing ---------------------------------------------------
    DcPullPhase phase      = DcPullPhase::Idle;  // current sub-phase
    uint32      phaseSince = 0;                  // getMSTime() at last transition
                                                 // (Forming dwell + per-leg watchdogs)

    // --- maneuver geometry ------------------------------------------------
    Position    camp;                            // party hold / drag-back point;
                                                 // (0,0,0) = unset
    std::vector<Position> breadcrumbs;           // recently-walked trail used to
                                                 // place the camp behind the tank
    float       returnLegStartDist = 0.0f;       // bot->camp distance stamped when
                                                 // the drag-back (Returning) begins;
                                                 // the turn-and-plant gate requires
                                                 // at least half of it covered before
                                                 // the tank may stop and fight. 0 =
                                                 // no leg in progress.
    uint32      plantTicks = 0;                   // consecutive maneuver ticks the
                                                 // pack has been gathered at the
                                                 // tank during the drag-back; the
                                                 // debounce latch for
                                                 // DungeonClearMath::ShouldPlantEarly.
    uint32      packPlantedSince = 0;            // getMSTime() the PULLED MOB was
                                                 // first seen holding
                                                 // UNIT_STATE_NO_COMBAT_MOVEMENT
                                                 // during this drag, 0 = not
                                                 // streaking. A mob in that state has
                                                 // no chase generator, so the drag
                                                 // cannot work by construction; the
                                                 // streak debounces the transient
                                                 // case (a caster that stops moving
                                                 // only while it casts) from the
                                                 // permanent one. See
                                                 // DC_PULL_PLANT_CONFIRM_MS.
    bool        bossPullback = false;           // this pull is a BossPullbackRegistry
                                                 // boss drag, not an ordinary trash
                                                 // pull. (No row uses it today — the
                                                 // table is empty since S1593; see
                                                 // BossPullbackRegistry.cpp.) Set at
                                                 // commit, cleared
                                                 // by Reset(). Two things key on it:
                                                 // the drag legs get a distance-sized
                                                 // watchdog (the haul is ~150yd, far
                                                 // past DC_PULL_LEG_TIMEOUT_MS), and
                                                 // turn-and-plant is suppressed — the
                                                 // whole point is reaching the anchor,
                                                 // so planting early would drop the
                                                 // fight back in the water.
    int32       scriptedStage = -1;              // ORDER of the ScriptedPullRegistry
                                                 // stage this pull is executing, or
                                                 // -1 for an ordinary pull. Set at
                                                 // commit, cleared at the Engage
                                                 // cleanup (per-PULL, like
                                                 // bossPullback — leaving it set
                                                 // would keep the whole pipeline
                                                 // force-enabled and pinned to a
                                                 // finished stage for the rest of
                                                 // the run). Four things key on it:
                                                 // the camp and the tag's stand spot
                                                 // come from the row instead of
                                                 // being computed, the tag/return
                                                 // legs get a distance-sized
                                                 // watchdog (the authored lane is
                                                 // far longer than a corridor pull's
                                                 // few yards), and BOTH the chase
                                                 // leash and turn-and-plant are
                                                 // suppressed — the plan says where
                                                 // the fight happens, so neither is
                                                 // free to move it.
    float       scriptedReturnBest = 0.0f;       // closest the tank has been to the
                                                 // camp on the CURRENT scripted
                                                 // drag-back. A ratchet: the drag
                                                 // is only allowed to close, so
                                                 // losing ground against it is
                                                 // proof something else owns the
                                                 // tank's movement and the leg must
                                                 // be re-issued from scratch. 0 =
                                                 // no scripted leg in progress.
    float       campHoldBest = 0.0f;             // FOLLOWER-owned (like
                                                 // campSafetySince): closest this
                                                 // bot has been to its camp slot on
                                                 // the current recall. The same
                                                 // losing-ground ratchet the tank's
                                                 // drag-back uses — a recall that
                                                 // re-issues one unchanged
                                                 // destination is deduped into a
                                                 // no-op, so only distance can tell
                                                 // us another generator has the
                                                 // bot. 0 = parked / no recall.
    bool        scriptedRecall = false;          // the scripted camp LEASH is
                                                 // currently walking the tank back
                                                 // (see DC_SCRIPTED_PULL_LEASH).
                                                 // A latch, not a per-tick test, so
                                                 // the recall runs all the way to
                                                 // DC_SCRIPTED_PULL_RECALL_HOME
                                                 // instead of stopping the instant
                                                 // the tank re-enters the leash
                                                 // radius and being re-tripped by
                                                 // the next chase step — the
                                                 // in-out oscillation a bare
                                                 // threshold produces. It is also
                                                 // dropped outright when the tank is
                                                 // standing in a ground effect, so
                                                 // the leash never walks it back
                                                 // into what pushed it out.
    uint32      scriptedAtStandMs = 0;           // ms the tank REACHED this stage's
                                                 // stand spot; 0 = not there yet. Two
                                                 // jobs, and the second is why it is a
                                                 // timestamp rather than a bool.
                                                 //
                                                 // LATCH. The walk-to-the-spot leg is
                                                 // done and must not re-issue — the
                                                 // same reason scriptedRecall is a
                                                 // latch. Load-bearing on a BODY-PULL
                                                 // row (ScriptedPullStage::bodyPull):
                                                 // there the tag leg's next move is
                                                 // deliberately AWAY from the spot, out
                                                 // to the pack's aggro edge ~8yd on. A
                                                 // bare "am I within the arrive radius"
                                                 // test flips false a tick or two into
                                                 // that walk and sends the tank back,
                                                 // which is the forward / turn-around /
                                                 // forward shuffle.
                                                 //
                                                 // CLOCK. The tag walk-in creeps closer
                                                 // the longer it goes without aggro,
                                                 // measured from the start of Advancing
                                                 // — which on an ordinary pull is the
                                                 // moment the tank starts closing, and
                                                 // on a scripted stage is up to a
                                                 // minute earlier, at the far end of
                                                 // the walk to the stand spot. See the
                                                 // creep in DcPullActions for what that
                                                 // cost.
    float       scriptedRecallBest = 0.0f;       // closest the tank has been to the
                                                 // camp on the CURRENT leash recall.
                                                 // The third site of the same
                                                 // ratchet as scriptedReturnBest and
                                                 // campHoldBest, and the last one to
                                                 // get it: a recall re-issuing one
                                                 // unchanged destination is deduped
                                                 // into a no-op, so while a chase
                                                 // generator owns the tank the
                                                 // recall is silent AND the
                                                 // standing-still backstop is blind
                                                 // (the bot is moving — outward).
                                                 // Only distance can see it.
    float       scriptedEngageHp = -1.0f;        // summed health % of this stage's
                                                 // pack members still on the party's
                                                 // attacker lists — the camp fight's
                                                 // PROGRESS SIGNATURE, and the only
                                                 // clock the Engage phase has. -1 is
                                                 // "not sampled yet".
    uint32      scriptedEngageSince = 0;         // ms the signature above last
                                                 // CHANGED. A camp fight is bounded
                                                 // by progress rather than by a wall
                                                 // clock: a long legitimate fight
                                                 // moves health every few seconds and
                                                 // can never trip, while a fight that
                                                 // has stopped happening trips on
                                                 // schedule however long the run has
                                                 // otherwise been going.
                                                 // 0 = no recall in flight.
    uint32      scriptedMusterSince = 0;         // ms the MUSTER for the next stage
                                                 // began — the party was short of
                                                 // DC_SCRIPTED_PULL_MUSTER_HP/_MP
                                                 // while a stage was due and not yet
                                                 // armed. 0 = not mustering. Latched
                                                 // rather than recomputed so the wait
                                                 // is bounded (see
                                                 // DC_SCRIPTED_PULL_MUSTER_MS) and so
                                                 // one stage cannot muster twice: the
                                                 // latch stays armed past the timeout
                                                 // and is cleared only by the party
                                                 // topping up or the stage going away.
                                                 // Same contract as the patrol wait.
    bool        scriptedForced = false;          // the scripted stage RAISED the
                                                 // pull-mode bool (a plan runs at
                                                 // any pull setting, exactly like a
                                                 // pull-back). Remembered so the
                                                 // bool and the leader's daze
                                                 // immunity are handed back to the
                                                 // player's setting when the plan
                                                 // ends, and only then.
    bool        losPull    = false;              // this pull targets a RANGED pack
                                                 // and the camp was placed to break
                                                 // line of sight to it (rangers must
                                                 // close to melee). Stamped at commit
                                                 // for the addon status line.
    uint32      campPublishedMs = 0;             // getMSTime() of the last camp
                                                 // write by the PULL machinery
                                                 // (prospective publish, commit,
                                                 // dynamic-upgrade seed, fresh
                                                 // unplanned-aggro camp). Advance's
                                                 // scout camp-trailing runs only
                                                 // while this is STALE (see
                                                 // DC_CAMP_PUBLISH_FRESH_MS), so
                                                 // exactly one owner moves the camp
                                                 // on any tick and it can never
                                                 // silently freeze between them.

    // --- bystander-detour borrow (approach, above commit range) -----------
    // The tank's walk out to the chosen pack normally belongs to Advance. When a
    // BYSTANDER pack's aggro sphere sits on that line the pull borrows the tick
    // and orbits it instead (DcEngageGeometry::EnRoutePackAvoidPoint), which is
    // what stops a correctly-sized heroic pull arriving with two extra packs
    // stuck to it. Advance's wedge ladder is idle while we hold the tick, so the
    // borrow runs on a no-progress clock — see DungeonClearMath::
    // ShouldKeepAvoidDetour, which owns the arm/restamp/give-up rule.
    ObjectGuid  avoidLegTarget;                  // pack the clock below belongs to;
                                                 // a different pack restarts it
    uint32      avoidSinceMs   = 0;              // getMSTime() of the last tick that
                                                 // closed real distance; 0 = not
                                                 // borrowing
    float       avoidBestDist  = 0.0f;           // closest the tank has been to
                                                 // avoidLegTarget on this detour
    bool        avoidGaveUp    = false;          // the borrow expired for THIS pack:
                                                 // don't take the tick again until
                                                 // the pack changes. One-shot on
                                                 // purpose — re-arming would cancel
                                                 // Advance's spline again the moment
                                                 // the tank closed a single yard

    // --- CC-assist gate ---------------------------------------------------
    uint32      ccSince    = 0;                  // getMSTime() the tank's CURRENT
                                                 // continuous drag-ruining CC
                                                 // (stun/fear/confuse/root/heavy
                                                 // slow) began; 0 = not CC'd. The
                                                 // grace latch for "tank CC'd mid
                                                 // drag -> abort pull, party piles
                                                 // in" (DungeonClearMath::Should
                                                 // AbortPullForCc).

    // --- camp-safety valve -------------------------------------------------
    bool        partyReleased = false;           // the camp-safety valve released
                                                 // the party from its passive hold
                                                 // WITHOUT tearing the pull down —
                                                 // the tank keeps dragging to camp
                                                 // while the followers fight back.
                                                 // Read by GetLeaderCampHold (the
                                                 // party is no longer passive) and
                                                 // ReapStrandedPassives (strip DC
                                                 // passive at once, no release
                                                 // delay). Set by SafetyRelease,
                                                 // cleared when the next maneuver
                                                 // starts (see Transition).
    uint32      campSafetySince = 0;             // getMSTime() a HELD FOLLOWER's
                                                 // current qualifying "in combat
                                                 // below the safety HP floor with
                                                 // a non-pull attacker" spell
                                                 // began; 0 = fine. Lives in the
                                                 // FOLLOWER's own copy of this
                                                 // value (the leader's copy never
                                                 // uses it) — the grace latch for
                                                 // DungeonClearMath::
                                                 // ShouldTripCampSafety. Re-armed
                                                 // fresh on each new passive hold
                                                 // (ApplyFollowerPassive).

    // --- per-target latches -----------------------------------------------
    ObjectGuid  abortTarget;                     // pack a pull gave up on; don't re-pull
    ObjectGuid  tagTarget;                        // "already tagged, hold for aggro" /
                                                  // pull-spell-opener dedupe (was
                                                  // "dungeon clear last pull target")

    // --- engage-fizzle latch ------------------------------------------------
    ObjectGuid  pullTarget;                      // pack THIS pull tagged (stamped at
                                                 // commit and on unplanned scout
                                                 // aggro; NOT cleared by EnterEngage).
                                                 // Read at the Engage cleanup to
                                                 // detect a fizzled drag: the target
                                                 // still alive and idle after the
                                                 // "camp fight" means it never came.
    ObjectGuid  fizzleTarget;                    // pack whose pulls keep fizzling
    uint32      fizzleCount = 0;                 // consecutive fizzles of fizzleTarget;
                                                 // at DC_PULL_FIZZLE_MAX the pack is
                                                 // handed to the walk-in engage via
                                                 // abortTarget instead of re-pulled

    // --- dynamic verdict (pull setting == 2) ------------------------------
    DcPullDecisionCode decision = DcPullDecisionCode::None;  // standing pull verdict
    ObjectGuid  decisionTarget;                  // pack the verdict applies to
    uint32      decisionSince  = 0;              // last re-classification timestamp
    uint32      patrolWaitSince = 0;             // getMSTime() the current pull first
                                                 // read patrol-contended (a lone
                                                 // patroller is the only thing over
                                                 // the Leeroy ceiling) while the tank
                                                 // holds at commit range; 0 = not
                                                 // waiting. Timeout latch for
                                                 // DungeonClearMath::ShouldWaitForPatrol
                                                 // (decision == 3 surfaces it). Reset
                                                 // when the pack changes.
    uint32      targetLostSince = 0;             // getMSTime() the pull target first
                                                 // resolved null while a Dynamic
                                                 // verdict was standing; 0 = present.
                                                 // Grace latch for DungeonClearMath::
                                                 // ShouldDropPullVerdict.

    // --- verdict telemetry (read-only for everyone but the governor) -------
    // What the classifier PREDICTED for the pack the standing verdict applies to,
    // kept so an observer can line it up against how many mobs actually turned
    // up. Nothing in the pull FSM reads these — they exist because "the tank
    // over-pulls in heroic" is two different bugs (the estimate is wrong / the
    // ceiling is wrong) that look identical from the outside, and only the
    // predicted-vs-observed delta tells them apart. Refreshed on every verdict
    // resolve (a Leeroy->Advanced upgrade re-stamps them); the harness samples
    // the last values standing before the pull closes.
    //
    // `decisionSeq` is the thing an observer actually keys on: a monotone counter
    // bumped once per NEW pack, so "a new pull started" is detectable even when
    // the same pack is re-pulled after a fizzle (where decisionTarget alone never
    // changes) and across the Dynamic verdict being dropped and re-taken.
    uint32      decisionSeq        = 0;  // ++ per new pack latched
    uint32      decisionTargetEntry = 0; // creature entry of decisionTarget
    uint32      predictedThirds    = 0;  // DcPullClassification::fullCount
    uint32      predictedCount     = 0;  // DcPullClassification::bodyCount
    uint32      predictedCeiling   = 0;  // DcPullClassification::ceiling

    void Reset() { *this = DcPullContext{}; }

    // Tear down the standing Dynamic verdict and every latch that feeds it.
    //
    // The governor (DcPullPlanner::UpdateDynamicPullMode) is the ONLY writer of
    // these fields, so anything that stops the governor from running FREEZES the
    // verdict — it does not clear it. That is exactly how tr-20260817-100413-43/44/45
    // wedged: DungeonClearPullModeCurrentValue stands the pull system down while a
    // persistent anchored event drives, and it early-returned BEFORE the governor
    // call, leaving `decision == PatrolHold` latched from the tick before. The pull
    // trigger keeps its rung live on that code by design ("off-but-held"), so the
    // pull action re-planted the tank every tick at DcRel::Pull (35) — above
    // DcRel::AtObjective (30) — and the event's own steps never got another tick.
    // The patrol-wait timeout could not save it either: ShouldWaitForPatrol is only
    // ever evaluated inside the governor.
    //
    // So a stand-down must clear the verdict, not merely stop reporting it. Cheap
    // and idempotent: safe to call on every tick the stand-down holds.
    void ClearDynamicVerdict()
    {
        decision        = DcPullDecisionCode::None;
        decisionTarget  = ObjectGuid::Empty;
        decisionSince   = 0;
        patrolWaitSince = 0;
        targetLostSince = 0;
    }

    // The ONLY way to transition into Engage (used by DcSetPullPhase and
    // DcLeaderSignal::AbortLeaderPull, which live in different TUs). Entering
    // Engage ends the pull — camp arrival, CC-abort, evade release, return-leg
    // wedge, or a forced abort — and the tag latch is per-PULL, not per-run: the
    // next pull of the same still-alive pack must be free to tag it again.
    // Without the clear, a re-pull of an evaded pack sits in "already tagged,
    // hold for aggro" until the leg watchdog dumps it to the normal walk-in.
    // abortTarget is deliberately NOT touched here: its lifecycle (set on tag
    // timeout/wedge, cleared at the Engage->Idle cleanup) prevents pull livelock
    // on a problem pack.
    void EnterEngage(uint32 nowMs)
    {
        phase = DcPullPhase::Engage;
        phaseSince = nowMs;
        tagTarget = ObjectGuid::Empty;
        // Per-PULL for the same reason tagTarget is: the next pull of this stage — a
        // re-pull after a fizzle, or the next stage of the plan — has to walk to a
        // stand spot again, and a latch left standing would skip that walk entirely
        // and start the tag leg from wherever the last camp fight ended.
        scriptedAtStandMs = 0;
    }

    // The single phase-write entry point. Every phase change goes through here so
    // the transition invariants live next to their comment: Engage is special-
    // cased through EnterEngage (it clears the per-pull tag latch); every other
    // phase just stamps phase + phaseSince. DcSetPullPhase is a thin context
    // adapter over this — there is no phase-write logic outside this header.
    void Transition(DcPullPhase p, uint32 nowMs)
    {
        if (p == DcPullPhase::Engage)
        {
            EnterEngage(nowMs);
            return;
        }
        // Entering a holding phase from Idle/Engage starts a NEW maneuver: any
        // standing safety release belonged to the previous one. Deliberately NOT
        // cleared by EnterEngage — a valve fire during Advancing aborts INTO
        // Engage, and the flag must survive that transition so the reaper skips
        // the graceful release delay (a safety release is always immediate).
        if (phase == DcPullPhase::Idle || phase == DcPullPhase::Engage)
            partyReleased = false;
        phase = p;
        phaseSince = nowMs;
    }

    // Camp-safety valve outcome (pure — gtested via TestPullDecisions). A held
    // passive follower took real damage from something that is NOT the pack being
    // dragged; the party must be freed to fight back. What happens to the PULL
    // depends on the phase:
    //   Returning (dragging)  keep the drag — the camp is the destination, and
    //   Forming   (marking)   abandoning mid-drag fights wherever the party
    //                         happens to be standing (the over-pull the drag
    //                         exists to prevent). Release the party only.
    //   Advancing             abort as before: the tank is still walking TOWARD
    //                         the pack; a drag-back from a pull that never tagged
    //                         is meaningless.
    //   Idle / Engage         no-op — nothing is holding the party.
    // Returns true when the pull itself was aborted (phase forced to Engage).
    bool SafetyRelease(uint32 nowMs)
    {
        switch (phase)
        {
            case DcPullPhase::Forming:
            case DcPullPhase::Returning:
                partyReleased = true;
                return false;
            case DcPullPhase::Advancing:
                partyReleased = true;  // immediate release — see Transition
                EnterEngage(nowMs);
                return true;
            default:
                return false;
        }
    }

    // --- camp ownership (see campPublishedMs) -----------------------------
    // A camp is "set" once any coordinate is non-zero; (0,0,0) is the unset
    // sentinel. Folds the five hand-rolled origin checks into one predicate.
    bool HasCamp() const
    {
        return camp.GetPositionX() != 0.0f || camp.GetPositionY() != 0.0f ||
               camp.GetPositionZ() != 0.0f;
    }

    // Move the camp AND stamp pull-machinery ownership in one step: a camp write
    // can never forget the freshness stamp that keeps Advance's scout camp-
    // trailing from wrestling the same camp on the same tick (the silent camp-
    // freeze bug class). Every PULL-side camp MOVE goes through here.
    void PublishCamp(Position const& p, uint32 nowMs)
    {
        camp = p;
        campPublishedMs = nowMs;
    }

    // Re-assert pull ownership of the CURRENT (unchanged) camp — "no change is
    // still an ownership decision": a successful camp computation that the
    // hysteresis keeps must still hold the camp against Advance's trailing.
    void TouchCampOwnership(uint32 nowMs) { campPublishedMs = nowMs; }
};

#endif
