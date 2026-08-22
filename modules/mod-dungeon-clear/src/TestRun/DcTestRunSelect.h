/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTRUNSELECT_H
#define _PLAYERBOT_DCTESTRUNSELECT_H

#include <cstddef>
#include <string>
#include <vector>

// Pure resolver for `.dc test stop <selector>`. Given the selector text and a
// snapshot of the active runs (runId + dungeon token, in registry-index order),
// decide which run indices the stop applies to — engine-free so the precedence
// (bare selector / "all" / exact runId / dungeon token) is unit-testable in
// isolation, mirroring DcTestRunVerdict.
//
// Precedence (first match wins):
//   1. ""     — the single active run when exactly one exists; NoRuns when
//               none, Ambiguous when more than one (the caller lists them).
//   2. "all"  — every active run.
//   3. runId  — an exact runId match (wins over a same-string dungeon token).
//   4. token  — every run whose dungeon token equals the selector (1 or many).
//   5. else   — NotFound.
//
// Also here: NextWatchIndex, the cycle rule behind `.dc test watch next`.

namespace DcTestRunSelect
{
    struct RunRef
    {
        std::string runId;
        std::string dungeon;   // registry token
    };

    enum class Kind
    {
        All,        // "all" or the implicit single run — indices filled
        Matched,    // runId / dungeon-token match — indices filled
        NoRuns,     // nothing active
        Ambiguous,  // bare selector with >1 run active
        NotFound    // selector matched no runId or dungeon token
    };

    struct Result
    {
        Kind kind;
        std::vector<std::size_t> indices;   // into the passed-in runs vector
    };

    Result Resolve(std::string const& selector, std::vector<RunRef> const& runs);

    // "Nothing to hop to" / "not watching anything", for NextWatchIndex.
    inline constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    // Cycle rule for `.dc test watch next`: step to the run after the one the
    // watcher is already on, wrapping at the end.
    //
    //   count   — how many runs are watchable RIGHT NOW (tank in world), in
    //             registry order; the caller builds that set.
    //   current — index of the run the camera is on, kNone when on none.
    //
    // Returns kNone when there is nowhere to go — no watchable run at all, or
    // the only one is the one already up (hopping to yourself would teleport
    // the watcher out of the instance and back in for nothing). A watcher on no
    // run starts at the first, so one command both starts and cycles.
    //
    // A `current` outside the set (the run the watcher sat on has ended) is
    // treated as "on none" — the cycle restarts at the first live run.
    std::size_t NextWatchIndex(std::size_t count, std::size_t current);
}

#endif  // _PLAYERBOT_DCTESTRUNSELECT_H
