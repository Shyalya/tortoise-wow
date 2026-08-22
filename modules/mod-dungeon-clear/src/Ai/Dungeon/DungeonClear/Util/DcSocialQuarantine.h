/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_SOCIAL_QUARANTINE_H
#define _DC_SOCIAL_QUARANTINE_H

#include <cstdint>

namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
class Player;

// Applies DcSocialQuarantineRegistry (and the scripted-pull plan's own pack
// volumes) to the live map: every pack the run has decided must not join the
// current fight is held at REACT_DEFENSIVE, and put back to REACT_AGGRESSIVE the
// moment it stops being one.
//
// See DcSocialQuarantineRegistry.h for what those two react states buy and why
// this is a hammer that should stay rare. In one line: REACT_AGGRESSIVE is the
// first thing Creature::CanAssistTo tests and the gate CreatureAI::
// MoveInLineOfSight passes before it will AttackStart, so dropping it removes a
// pack from cross-pack calls for help AND from proximity aggro, while leaving
// CreatureGroup::MemberEngagingTarget (which does not test it) intact — so the
// pack still fights as a formation the instant anything actually attacks it.
//
// WHERE THE ZONES COME FROM. Two sources, one applier:
//   * THE SCRIPTED-PULL PLAN. Every ScriptedPullStage on this map gated on the
//     run's current boss contributes its pack cylinder, EXCEPT the stage that is
//     due/in flight — that one is released, because it is the pack the tank is
//     about to walk into. This is what makes a five-formation room behave like
//     five separate rooms: exactly one pack is awake at a time, and the T=0
//     CallAssistance the pulled pack issues from its own spawn finds nobody
//     eligible to recruit. No new data — the plan's own volumes are reused, so
//     the two can never drift.
//   * DcSocialQuarantineRegistry. Hand-authored zones for packs with no pull row,
//     which is how a BOSS's neighbours are handled (a boss fight has no scripted
//     stage to derive from).
//
// STATE LIVES ON THE CREATURES, deliberately: "did we quarantine this one" is
// answered by reading its react state, not by a side table. That means no
// per-instance bookkeeping to leak, nothing to get out of step when a bot dies
// mid-plan or a run is torn down mid-room, and no shared container for two maps
// updating on different threads to race on. The cost is the invariant that every
// zone must be authored over creatures that are REACT_AGGRESSIVE by default — a
// gtest asserts each row is plain Sunblade trash rather than anything a script
// owns.
namespace DcSocialQuarantine
{
    // Re-assert the quarantine for `bot`'s map. Leader-only; cheap no-op on a map
    // with no zones and no scripted-pull plan. Idempotent — safe to call every
    // tick, and safe to call more than once per tick.
    void Update(Player* bot, AiObjectContext* context);

    // Put every zone member on the map back to REACT_AGGRESSIVE, whatever boss is
    // pending. Called when the run stops (dc off / mode disabled), so a room is
    // never left modified behind a party that has walked away from it.
    void ReleaseAll(Player* bot);
}

#endif  // _DC_SOCIAL_QUARANTINE_H
