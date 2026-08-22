/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTRUNVERDICT_H
#define _PLAYERBOT_DCTESTRUNVERDICT_H

#include <cstdint>

// Pure verdict kernel for the automated dungeon test harness (`.dc test`).
// Each monitoring tick the manager distills the live run into an Observation
// and asks this kernel whether the run is still going, has succeeded, or has
// failed — and why. Extracted engine-free so the precedence ladder is
// unit-testable in isolation, mirroring DcWaitAtBossDecision / DcSmartRest.
//
// Precedence (first match wins):
//   1. The run's own disable funnel fired (DisableDungeonClear) — that verdict
//      is authoritative: all-cleared reason = Success, anything else (party
//      death, dc off, left the dungeon) = FailDisabled.
//   2. Operator abort / leader gone / GM logged out — FailAborted.
//   3. The whole party dead past its grace — FailPartyWiped. This is NOT
//      redundant with (1): the death bailout that would disable the run lives
//      in the `dungeon clear` strategy, which is installed on the non-combat
//      and combat engines only, so a corpse (BOT_STATE_DEAD) never ticks it.
//      One death with a survivor still routes through (1) — the survivor fires
//      it — but a full wipe leaves nobody to, and without this rung the run
//      just sits there until the no-progress net calls it a livelock, which is
//      both the wrong diagnosis and ten minutes of a test slot wasted.
//   4. Pause outlasting its grace — a paused run is waiting for human input,
//      which a test run by definition never gets. The grace period exists so
//      the door-blocked auto-resume can win the race before we call it.
//   5. Stall outlasting its grace — the stall ladder gets time to recover.
//   6. No boss/objective progress for too long — the silent-livelock net.
//   7. Overall wall-clock cap.

namespace DcTestRun
{
    struct Limits
    {
        std::uint32_t pauseGraceMs = 60 * 1000;
        // Long enough that a battle rez or a soulstone gets to un-wipe the run
        // before it is called, short enough that a real wipe ends promptly.
        std::uint32_t wipeGraceMs = 15 * 1000;
        std::uint32_t stallGraceMs = 120 * 1000;
        std::uint32_t noProgressMs = 600 * 1000;
        std::uint32_t overallTimeoutMs = 7200 * 1000;
    };

    enum class Verdict : std::uint8_t
    {
        Continue,
        Success,
        FailDisabled,        // disable fired with a non-all-cleared reason
        FailPartyWiped,      // every member on the leader's map is dead
        FailPausedTimeout,
        FailStalledTimeout,
        FailNoProgress,
        FailOverallTimeout,
        FailAborted          // .dc test stop, GM logout, leader/bot missing
    };

    struct Observation
    {
        bool disableFired = false;
        bool disableAllCleared = false;
        bool abortRequested = false;
        bool leaderMissing = false;
        bool gmOnline = true;
        bool partyWiped = false;
        bool paused = false;
        bool stalled = false;
        std::uint32_t wipedForMs = 0;
        std::uint32_t pausedForMs = 0;
        std::uint32_t stalledForMs = 0;
        std::uint32_t sinceProgressMs = 0;
        std::uint32_t elapsedMs = 0;
    };

    Verdict Classify(Observation const& o, Limits const& l);

    // Stable machine token for the JSONL record ("success", "paused_timeout",
    // ...). "continue" is never written; it means keep monitoring.
    char const* VerdictName(Verdict v);

    inline bool IsTerminal(Verdict v) { return v != Verdict::Continue; }
    inline bool IsSuccess(Verdict v) { return v == Verdict::Success; }
}

#endif  // _PLAYERBOT_DCTESTRUNVERDICT_H
