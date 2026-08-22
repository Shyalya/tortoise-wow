/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCPULLDECISION_H
#define _PLAYERBOT_DCPULLDECISION_H

#include <cstdint>

// Pure decision core for the Dynamic-pull governor
// (DcPullPlanner::UpdateDynamicPullMode). Mirrors what DungeonClearApproach does
// for the advance phase ladder: isolates the DECISION — "given the resolved
// observation, what pull mode/decision should stand this tick?" — from the
// EFFECT (the apply() that flips `dungeon clear pull mode`, seeds the camp, and
// manages the per-target latch), which stays in the planner.
//
// The governor's arbitration is the regression-prone part: the no-target grace,
// the in-combat / non-Idle lock, the same-pack UPGRADE-only latch + re-check
// throttle, and the patrol-wait gate's three outcomes (hold at commit, walk in
// as provisional Leeroy, or commit). Each of those is a threshold/boolean graph
// the gtest suite can pin at every edge instead of a live raid.
//
// Engine-free on purpose: no Player/Unit/PlayerbotAI. The planner resolves all
// game state (target presence, combat, pull phase, the ClassifyPullAdvanced
// verdict + counts, commit range, and the latch gates ShouldDropPullVerdict /
// ShouldWaitForPatrol) into a flat observation; this maps it to a verdict.
namespace DcPullDecision
{
    // One verdict per governor outcome. The numeric pull "decision" the addon
    // reads (0 none, 1 Leeroy, 2 Advanced, 3 patrol-hold) is noted per verdict;
    // the planner translates a verdict back into apply(mode, decision).
    enum class PullVerdict : std::uint8_t
    {
        NoOp,              // latched/throttled — change nothing this tick
        HoldNoTarget,      // target lost but within the verdict grace — keep as-is
        DropToLeeroy,      // no target, no/expired standing verdict -> mode off, decision 0
        Leeroy,            // commit Leeroy: mode off, decision 1
        ApproachAsLeeroy,  // patrol-contended pack, still approaching -> provisional Leeroy (off, 1)
        PatrolWaitHold,    // at commit range, waiting a lone patrol out -> mode off, decision 3
        Advanced,          // commit Advanced: mode on, decision 2
    };

    // Flat data resolved by the planner. Booleans only — the planner does the
    // Unit/grid/navmesh reads, the latch math, and the throttle clock; the
    // fields below are their outcomes.
    struct PullObservation
    {
        // --- top-level gates ---
        bool hasTarget   = false;  // DcTargeting::GetPullTarget resolved a target
        bool inCombat    = false;  // bot->IsInCombat()
        bool phaseIdle   = true;   // pull.phase == DcPullPhase::Idle

        // --- no-target branch ---
        bool hasStandingVerdict  = false;  // pull.decisionTarget not empty
        bool verdictGraceExpired = false;  // ShouldDropPullVerdict() said drop

        // --- same-pack latch / throttle ---
        bool sameTarget        = false;  // pull.decisionTarget == target GUID
        bool currentlyAdvanced = false;  // current `dungeon clear pull mode` bool
        bool recheckElapsed    = true;   // throttle window passed (sameTarget && !advanced only)

        // --- classification (valid once ClassifyPullAdvanced has run) ---
        bool advanced          = false;  // ClassifyPullAdvanced verdict
        bool patrolWaitEnabled = false;  // DcSettings PullPatrolWait
        bool atCommitRange     = false;  // tank within PullCommitRange of the pack
        bool patrolContended   = false;  // fullCount > ceiling && reducedCount <= ceiling
        bool patrolWaitExpired = false;  // at commit: ShouldWaitForPatrol() said proceed
    };

    // Returns the verdict the governor should apply (or NoOp to leave the
    // standing decision untouched). Total: every input combination yields a
    // verdict.
    PullVerdict DecidePull(PullObservation const& o);
}

#endif  // _PLAYERBOT_DCPULLDECISION_H
