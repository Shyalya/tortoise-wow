/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSMARTREST_H
#define _PLAYERBOT_DCSMARTREST_H

#include <string>

namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
class Player;

// Engine glue for Smart Rest (the hysteresis rest latch — see
// DcSmartRestDecision.h for the semantics). This is the only place the feature
// touches game objects: it snapshots the live group, reads the thresholds via
// DcSettings, runs the pure kernel against the latch fields stored in the
// leader's DcRunState, and offers a cross-bot read for followers.
namespace DcSmartRest
{
    // DungeonClear.SmartRest, per-run override -> conf -> default. Followers
    // resolve the run owner through FindLeaderTank inside DcSettings, so every
    // member of a run sees the same effective value.
    bool Enabled(Player* bot);

    // Evaluate the kernel against the latch in DcRun::Of(leaderCtx) and write
    // the verdict back. Returns the post-update latch. LEADER-ONLY by design —
    // called from the leader's between-pulls gate; if `leader` is not the
    // run's resolved party tank this only reads (a follower's own DcRunState
    // stays at defaults and must never grow a private latch). Idempotent
    // within a tick: same live snapshot + stored latch => same verdict, so the
    // strict and loose memo slots may both call it safely.
    bool UpdateLatch(Player* leader, AiObjectContext* leaderCtx);

    // Cross-bot read for followers (triggers/multiplier): reads the latch off
    // the resolved party tank's own context — the same pattern the ZF
    // event-regroup check in RestTargetIfActive already uses. Never updates.
    bool IsLatched(Player* leaderTank);

    // "Smart Rest: waiting on Neko (mana 34%), Bib (hp 61%) +1 more" — names
    // the members still below their release bar, capped like
    // DcPartyState::DescribePartyNotReady. Empty when nothing blocks.
    std::string DescribeWait(Player* leader);
}

#endif  // _PLAYERBOT_DCSMARTREST_H
