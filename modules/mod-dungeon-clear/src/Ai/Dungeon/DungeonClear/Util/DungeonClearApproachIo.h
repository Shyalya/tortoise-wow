/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARAPPROACHIO_H
#define _PLAYERBOT_DUNGEONCLEARAPPROACHIO_H

#include <cstdint>
#include <string>
#include <string_view>

#include "DungeonClearApproach.h"

// Record/replay serialization for the boss-approach decision core (review
// report point #3, the orchestration replay harness). The orchestration that
// actually fails in the field — Advance::Execute's phase ladder — had zero
// automated coverage because it needs a live bot on a live map. This module is
// the seam that fixes that: it turns the pure DecideApproach(Observation)
// boundary into a serializable (observation -> verdict) record so a real
// problem run can be frozen into a permanent regression fixture.
//
// Engine-free on purpose (stdlib only, no Player/Map): the writer is callable
// from the live action AND the offline gtest runner, and the test target links
// the same code the server captures with — so a capture round-trips exactly.
//
// Format is a hand-rolled flat JSONL (one self-contained JSON object per line,
// no nested objects, no third-party dep). The verdict is stored by NAME so a
// fixture stays readable and survives enum reordering.
namespace DungeonClearApproachIo
{
    using DungeonClearApproach::Observation;
    using DungeonClearApproach::Verdict;

    // One captured decision: which bot, when, the observation the action fed the
    // pure function, and the verdict the live code acted on. Replaying asserts
    // DecideApproach(obs) still equals verdict.
    struct DecisionRecord
    {
        std::uint64_t guid    = 0;   // bot GUID (raw) — keys lines by bot
        std::uint32_t tick    = 0;   // getMSTime() at capture — orders a run
        Observation   obs;
        Verdict       verdict = Verdict::MoveToFallback;
    };

    // Verdict <-> stable name. VerdictName is total (never null). FromName
    // returns false on an unknown token (a malformed/old fixture line).
    char const* VerdictName(Verdict v);
    bool        VerdictFromName(std::string_view name, Verdict& out);

    // Serialize one record to a single JSONL line (no trailing newline). Floats
    // are written at full round-trip precision so a replayed observation lands
    // on the same side of every threshold the capture did.
    std::string ToJsonl(DecisionRecord const& rec);

    // Parse one JSONL line produced by ToJsonl. Returns false on a blank line, a
    // line missing the verdict, or an unknown verdict token. Absent scalar
    // fields keep the Observation's struct default (forward-compatible with a
    // fixture written before a field existed).
    bool FromJsonl(std::string const& line, DecisionRecord& out);

    // Live capture hook. Appends one JSONL line for (guid, tick, obs, verdict)
    // to the process-wide capture file, opened lazily on first call and shared
    // across maps (mutex-guarded, flushed per line). Not gated here — the caller
    // decides whether recording is on (DcSettings RecordDecisions). Writes to
    // CapturePath(); a failed open is silently skipped (capture is diagnostic).
    void Record(std::uint64_t guid, std::uint32_t tick, Observation const& obs,
                Verdict verdict);

    // Absolute path the live capture is appended to. Defaults to
    // "dungeonclear_decisions.jsonl" in the worldserver's working directory;
    // overridable with the DUNGEONCLEAR_DECISIONS_FILE environment variable so a
    // soak run can aim it at a scratch location without a recompile.
    std::string CapturePath();
}

#endif
