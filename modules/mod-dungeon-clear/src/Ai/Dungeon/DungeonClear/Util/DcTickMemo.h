/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_TICK_MEMO_H
#define _DC_TICK_MEMO_H

#include <cstdint>

namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
class Player;
struct DungeonBossInfo;
struct ScriptedPullStage;

// Within-tick memo of a few predicates the DC trigger ladder evaluates more than
// once per engine tick (each internally a group walk / geometry pass / pathfinder
// probe whose answer cannot change between two reads in the same tick):
//
//   - DcEngageGeometry::IsAtBossEngage  — at-boss / blocking-trash / pull triggers
//     plus Advance's own read (4 calls; runs a PathGenerator probe off the
//     same-floor fast path).
//   - DcPartyState::IsBetweenPullsReady — at-boss / blocking-trash / room-trash /
//     stalled / pull triggers (strict, requireNoLoot=true), and the advance/event
//     actions (loose, requireNoLoot=false). Each is a full party walk.
//   - ScriptedPullRegistry::DueStage — the pull-target value, the effective
//     pull-mode value, the pull trigger's fight-in-place exception and the pull
//     action's commit branch all ask for it (4+ calls), and each miss runs one
//     entry-filtered volume scan PER STAGE with a reachability probe on every
//     candidate. Cheap-gated to the one map and the arm radius, but inside that
//     window it is the most expensive thing in the ladder.
//
// This is NOT a cross-tick cache (that was the door-verdict staleness-bug class):
// the 50ms window can never span two AI ticks (>=100ms apart), so it deduplicates
// strictly WITHIN a tick while guaranteeing every tick recomputes. Owned as a
// per-bot value ("dungeon clear tick memo"); leader-only consumers.
struct DcTickMemo
{
    std::uint32_t stampMs = 0;          // getMSTime() of the tick that filled it
    // Cached answers, valid only while stampMs is current: -1 unset / 0 / 1.
    std::int8_t atBossEngage = -1;
    std::int8_t betweenPullsReadyLoose = -1;   // requireNoLoot == false
    std::int8_t betweenPullsReadyStrict = -1;  // requireNoLoot == true
    // Due scripted-pull stage ORDER: -2 unset, -1 none due, >= 0 the stage's order.
    // (-1 is a meaningful answer here, so the "unset" sentinel has to be distinct.)
    std::int32_t scriptedStage = -2;

    static constexpr std::uint32_t kMemoWindowMs = 50;

    // True while a memo stamped at `stampMs` is still within the current tick's
    // window. ms-wraparound safe (uses getMSTimeDiff semantics).
    static bool MemoValid(std::uint32_t stampMs, std::uint32_t now);

    // If the window has elapsed since `stampMs` (or it was never stamped), clear
    // every cached field and re-stamp to `now` — opening a fresh tick. Call once
    // before reading/writing a field.
    void EnsureFresh(std::uint32_t now);
};

// Memoised accessors. Each resolves the per-bot memo value, refreshes it for the
// current tick, and computes its predicate at most once per tick. Null bot/ctx
// fall back to a direct (unmemoised) computation.
class DcTickMemoAccess
{
public:
    static bool AtBossEngage(Player* bot, AiObjectContext* ctx,
                             DungeonBossInfo const& next);
    static bool BetweenPullsReady(Player* bot, AiObjectContext* ctx,
                                  bool requireNoLoot);
    // The ScriptedPullRegistry stage due this tick, or nullptr. Call this rather
    // than ScriptedPullRegistry::DueStage from anything on the per-tick path; the
    // registry entry point is the uncached one. (Rows are static table entries, so
    // the pointer is stable for the process lifetime.)
    static ScriptedPullStage const* ScriptedStage(Player* bot, AiObjectContext* ctx);
};

#endif  // _DC_TICK_MEMO_H
