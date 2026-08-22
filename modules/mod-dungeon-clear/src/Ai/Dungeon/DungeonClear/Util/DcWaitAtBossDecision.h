/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCWAITATBOSSDECISION_H
#define _PLAYERBOT_DCWAITATBOSSDECISION_H

#include <cstdint>

// Pure decision kernel for Wait at Boss — the pre-pull hold. When the
// DungeonClear.WaitAtBoss setting is on, the run auto-enters the existing
// paused state at the exact moment the tank would otherwise commit a boss
// pull, and waits for the human to resume (the addon's Pause/Resume button or
// `dc pause`) before pulling. This lets the party set up — assignments, CC
// marks, cooldowns — instead of the tank rushing in unannounced.
//
// The gate sits in DungeonClearEngageBossAction, right before EngageDirect:
// by then every other pre-pull condition has already passed (engage-range
// standoff, room-aggro pre-clear, no closed door between, loot done, Smart
// Rest top-off released), so a resume pulls immediately — the wait is never
// stacked under some other hold the player can't see.
//
// Each boss pauses ONCE per run: the pause site stamps the boss's GUID into
// DcRunState::waitedBossGuid, and a stamped GUID never pauses again. Stamping
// at PAUSE time (not resume time) means every resume path — the manual
// toggle, `dc go`, the door auto-resume — works untouched: on resume the
// trigger re-fires, the stamp matches, and the pull commits. The stamp lives
// outside OnResume's pause-cluster clear and is only dropped by a full
// Reset(), so a wipe (death disables the run) followed by a fresh `dc on`
// waits again — a new run earns a new heads-up.
//
// Extracted engine-free (GUIDs travel as raw uint64) so it is unit-testable
// in isolation, mirroring DcSmartRestDecision / DecideCombatRegroup. The
// caller builds Inputs from live state and applies the verdict.

namespace DcWaitAtBossDecision
{
    struct Inputs
    {
        bool enabled = false;        // DungeonClear.WaitAtBoss (the master gate)
        bool nextIsBoss = false;     // next anchor kind == DungeonAnchorKind::Boss
        bool paused = false;         // run already paused (any origin) — never stack
        bool inCombat = false;       // tank already fighting — pull is moot
        std::uint64_t bossGuid = 0;         // live boss about to be engaged (raw GUID)
        std::uint64_t lastWaitedGuid = 0;   // DcRunState::waitedBossGuid stamp
    };

    struct Result
    {
        bool shouldAutoPause = false;
    };

    // Pause iff the feature is on, the very next engage is a real boss we can
    // name (bossGuid != 0), the run isn't already paused or fighting, and this
    // boss hasn't been waited on before in this run. Objectives never pause —
    // set-piece events flow uninterrupted.
    Result Decide(Inputs const& in);
}

#endif  // _PLAYERBOT_DCWAITATBOSSDECISION_H
