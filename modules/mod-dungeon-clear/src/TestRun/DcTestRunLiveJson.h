/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTRUNLIVEJSON_H
#define _PLAYERBOT_DCTESTRUNLIVEJSON_H

#include <cstdint>
#include <string>
#include <vector>

// Pure builder for the live-heartbeat sidecar the dashboard polls
// (dc_testrun_live.json). With concurrent runs and plans the file carries:
//
//   {"active":<bool>,"ts":<unixS>,"plans":[ {…per-plan keys…}, … ],
//    "runs":[ {…per-run keys…}, … ]}
//
// "active" is true whenever at least one run OR plan is present (a plan in a
// between-runs backoff window with zero children is still active); the
// dashboard treats a stale ts (or active:false) as "no live run". Keys are
// only ever added, so an older app.js keeps rendering the runs array
// unchanged. Engine-free (takes plain snapshots the managers gather under
// their own locks) so the schema is unit-testable in isolation.

namespace DcTestRunLive
{
    struct StatusEntry
    {
        std::uint32_t t = 0;
        std::string state;
        std::string detail;
    };

    // One party member's live position for the dashboard map overlay. Kept as a
    // compact numeric record (short keys, 1-decimal coords) and emitted in its
    // own "bots" array — deliberately NOT folded into the human-readable
    // "recent" timeline, so streaming per-tick coordinates never floods the
    // status lines the dashboard/logs render as text.
    struct BotPos
    {
        std::string role;              // "tank" / "healer" / "dps"
        std::uint8_t classId = 0;
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        bool alive = true;
        std::uint8_t hp = 0;           // health %, 0 when dead
        bool inCombat = false;
        // Mana %, or -1 for a class with no mana pool (warrior/rogue/DK) so the
        // dashboard can omit the bar entirely rather than draw a permanent 0%.
        std::int16_t mp = -1;
        // Character name, so the dashboard can label the row with the bot the
        // reader sees in-game instead of a generic TANK/HEAL/DPS token. Last
        // field on purpose: the members above are positionally aggregate-
        // initialised in the tests, and appending keeps those call sites valid.
        std::string name;
    };

    // One active `.dc test plan` campaign, for the dashboard's plan progress
    // bars (DcTestPlanManager gathers these alongside the run snapshots).
    struct PlanSnapshot
    {
        std::string planId;
        std::string dungeon;
        std::uint32_t total = 0;
        std::uint32_t launched = 0;
        std::uint32_t succeeded = 0;
        std::uint32_t failed = 0;
        std::uint32_t activeNow = 0;
        std::uint32_t concurrent = 0;
        bool heroic = false;
        std::string state;             // "running" | "backoff" | "draining"
                                       // | "waiting for test driver"
        std::uint32_t elapsedS = 0;
    };

    struct RunSnapshot
    {
        std::string runId;
        std::string planId;            // "" for ad-hoc runs
        std::string dungeon;
        std::string dungeonName;
        std::string stage;
        std::string state;             // last publisher state token
        std::uint32_t level = 0;
        bool heroic = false;
        std::uint32_t elapsedS = 0;
        std::uint32_t bossesKilled = 0;
        std::uint32_t bossesTotal = 0;
        std::int32_t mapId = -1;           // party's current map; -1 = none resolved

        // Live "why is it sitting there" fields. A run watched on the dashboard
        // is most often waiting on exactly one of these: a stall the ladder has
        // not cleared, or a boss it cannot reach. Empty when not applicable.
        std::string stall;                 // current DcKey::StallReason
        std::string bossName;              // current target boss/objective
        std::uint32_t sinceProgressS = 0;  // age of the no-progress watchdog
        bool inCombat = false;             // any member in combat

        // Wipe in progress: every member on the leader's map is a corpse and
        // the wipe grace timer is running. Without this the live card shows a
        // party doing nothing for 15s with no hint why. wipeOpponent names what
        // took them down (empty if they died out of combat); wipeOnBoss says
        // whether that was an encounter boss or a trash pack.
        bool wiped = false;
        bool wipeOnBoss = false;
        std::string wipeOpponent;

        std::vector<BotPos> bots;          // per-member positions (may be empty)
        std::vector<StatusEntry> recent;   // already trimmed to <=8 by the caller
    };

    // tsS = unix seconds stamped by the caller (engine time source stays out of
    // the pure builder). runs and plans both empty →
    // {"active":false,"ts":tsS,"plans":[],"runs":[]}.
    std::string Build(std::uint64_t tsS, std::vector<RunSnapshot> const& runs,
                      std::vector<PlanSnapshot> const& plans = {});
}

#endif  // _PLAYERBOT_DCTESTRUNLIVEJSON_H
