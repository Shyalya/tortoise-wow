/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCPULLDECISIONIO_H
#define _PLAYERBOT_DCPULLDECISIONIO_H

#include <cstdint>
#include <string>
#include <string_view>

#include "DcPullDecision.h"

// Record/replay serialization for the Dynamic-pull governor decision core
// (Tier 1 of the headless-sim plan), mirroring DungeonClearApproachIo. Turns the
// pure DecidePull(PullObservation) boundary into a serializable
// (observation -> verdict) record so a real pull-governor freeze can be frozen
// into a permanent regression fixture under t/fixtures/pull/.
//
// Engine-free (stdlib only): the writer is callable from the live planner AND
// the offline gtest runner, and the test target links the same code the server
// captures with — so a capture round-trips exactly. Format is the shared flat
// JSONL (DcDecisionJson); the verdict is stored by NAME so a fixture stays
// readable and survives enum reordering.
namespace DcPullDecisionIo
{
    using DcPullDecision::PullObservation;
    using DcPullDecision::PullVerdict;

    struct PullDecisionRecord
    {
        std::uint64_t  guid = 0;   // leader-tank GUID (raw)
        std::uint32_t  tick = 0;   // getMSTime() at capture — orders a run
        PullObservation obs;
        PullVerdict    verdict = PullVerdict::NoOp;
    };

    // Verdict <-> stable name. VerdictName is total (never null). FromName returns
    // false on an unknown token (a malformed/old fixture line).
    char const* VerdictName(PullVerdict v);
    bool        VerdictFromName(std::string_view name, PullVerdict& out);

    std::string ToJsonl(PullDecisionRecord const& rec);
    bool        FromJsonl(std::string const& line, PullDecisionRecord& out);

    // Live capture hook. Appends one JSONL line to the process-wide pull-decision
    // capture file (opened lazily, shared across maps, mutex-guarded, flushed per
    // line). Not gated here — the planner gates on DcSettings RecordDecisions.
    void Record(std::uint64_t guid, std::uint32_t tick, PullObservation const& obs,
                PullVerdict verdict);

    // Absolute path the live capture is appended to. Defaults to
    // "dungeonclear_pull_decisions.jsonl" in the worldserver's working directory;
    // overridable with the DUNGEONCLEAR_PULL_DECISIONS_FILE environment variable.
    std::string CapturePath();
}

#endif  // _PLAYERBOT_DCPULLDECISIONIO_H
