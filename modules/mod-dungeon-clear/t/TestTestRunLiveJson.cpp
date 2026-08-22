/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "TestRun/DcTestRunLiveJson.h"

using DcTestRunLive::BotPos;
using DcTestRunLive::Build;
using DcTestRunLive::PlanSnapshot;
using DcTestRunLive::RunSnapshot;
using DcTestRunLive::StatusEntry;

namespace
{
    RunSnapshot Sample(std::string const& runId, std::string const& dungeon)
    {
        RunSnapshot s;
        s.runId = runId;
        s.dungeon = dungeon;
        s.dungeonName = "The " + dungeon;
        s.stage = "monitoring";
        s.state = "clearing";
        s.level = 20;
        s.elapsedS = 42;
        s.bossesKilled = 1;
        s.bossesTotal = 4;
        return s;
    }
}

// ---- empty registry --------------------------------------------------------------

TEST(DcTestRunLiveJsonTest, EmptyIsInactiveWithEmptyRuns)
{
    std::string const json = Build(1700000000ull, {});
    EXPECT_EQ(json, "{\"active\":false,\"ts\":1700000000,\"plans\":[],\"runs\":[]}");
}

// ---- ts passthrough --------------------------------------------------------------

TEST(DcTestRunLiveJsonTest, TimestampIsVerbatim)
{
    std::string const json = Build(1234567890ull, {});
    EXPECT_NE(json.find("\"ts\":1234567890"), std::string::npos);
}

// ---- ordering + active flag ------------------------------------------------------

TEST(DcTestRunLiveJsonTest, TwoRunsPreserveOrderAndAreActive)
{
    std::vector<RunSnapshot> runs = {Sample("tr-1", "deadmines"), Sample("tr-2", "wailing")};
    std::string const json = Build(1700000000ull, runs);

    EXPECT_NE(json.find("\"active\":true"), std::string::npos);

    std::size_t const first = json.find("tr-1");
    std::size_t const second = json.find("tr-2");
    ASSERT_NE(first, std::string::npos);
    ASSERT_NE(second, std::string::npos);
    EXPECT_LT(first, second);

    // Per-run keys present.
    EXPECT_NE(json.find("\"dungeon\":\"deadmines\""), std::string::npos);
    EXPECT_NE(json.find("\"bossesKilled\":1"), std::string::npos);
    EXPECT_NE(json.find("\"bossesTotal\":4"), std::string::npos);
}

// ---- runs carry their owning plan ------------------------------------------------

TEST(DcTestRunLiveJsonTest, RunPlanIdEmittedAndEmptyForAdHoc)
{
    RunSnapshot planChild = Sample("tr-1", "deadmines");
    planChild.planId = "tp-9";
    RunSnapshot adHoc = Sample("tr-2", "wailing");

    std::string const json = Build(1700000000ull, {planChild, adHoc});
    EXPECT_NE(json.find("\"runId\":\"tr-1\",\"planId\":\"tp-9\""), std::string::npos);
    EXPECT_NE(json.find("\"runId\":\"tr-2\",\"planId\":\"\""), std::string::npos);
}

// ---- plans array -----------------------------------------------------------------

TEST(DcTestRunLiveJsonTest, PlansArrayCarriesProgressAndKeepsFileActive)
{
    PlanSnapshot p;
    p.planId = "tp-9";
    p.dungeon = "old-hillsbrad";
    p.total = 20;
    p.launched = 9;
    p.succeeded = 6;
    p.failed = 1;
    p.activeNow = 2;
    p.concurrent = 5;
    p.state = "running";
    p.elapsedS = 900;

    // A plan with zero runs in flight (backoff window) must still read active.
    std::string const json = Build(1700000000ull, {}, {p});
    EXPECT_NE(json.find("\"active\":true"), std::string::npos);
    EXPECT_NE(json.find("\"plans\":[{\"planId\":\"tp-9\",\"dungeon\":\"old-hillsbrad\""
                        ",\"total\":20,\"launched\":9,\"succeeded\":6,\"failed\":1"
                        ",\"active\":2,\"concurrent\":5,\"heroic\":false"
                        ",\"state\":\"running\",\"elapsedS\":900}]"),
              std::string::npos);
    EXPECT_NE(json.find("\"runs\":[]"), std::string::npos);
}

TEST(DcTestRunLiveJsonTest, HeroicFlagsSerializeTrue)
{
    RunSnapshot s = Sample("tr-1", "ramparts");
    s.heroic = true;
    PlanSnapshot p;
    p.planId = "tp-9";
    p.dungeon = "ramparts";
    p.heroic = true;

    std::string const json = Build(1700000000ull, {s}, {p});
    // Both the plan and the run objects carry their own heroic:true.
    EXPECT_NE(json.find("\"concurrent\":0,\"heroic\":true"), std::string::npos);
    EXPECT_NE(json.find("\"heroic\":true,\"elapsedS\":"), std::string::npos);
}

// ---- recent entries are escaped --------------------------------------------------

TEST(DcTestRunLiveJsonTest, RecentEntriesAreJsonEscaped)
{
    RunSnapshot s = Sample("tr-1", "deadmines");
    StatusEntry e;
    e.t = 10;
    e.state = "stalled";
    e.detail = "can't reach \"Boss\"\nretrying";  // quote + newline must escape
    s.recent.push_back(e);

    std::string const json = Build(1700000000ull, {s});
    EXPECT_NE(json.find("\\\"Boss\\\""), std::string::npos);
    EXPECT_NE(json.find("\\n"), std::string::npos);
    EXPECT_EQ(json.find('\n'), std::string::npos);  // no raw newline leaked
    EXPECT_NE(json.find("\"t\":10"), std::string::npos);
}

// ---- no bots: empty array + mapId still emitted ----------------------------------

TEST(DcTestRunLiveJsonTest, NoBotsEmitsEmptyArrayAndDefaultMap)
{
    RunSnapshot s = Sample("tr-1", "deadmines");  // leaves mapId=-1, bots empty
    std::string const json = Build(1700000000ull, {s});
    EXPECT_NE(json.find("\"mapId\":-1"), std::string::npos);
    EXPECT_NE(json.find("\"bots\":[]"), std::string::npos);
}

// ---- bot positions: compact keys, 1-decimal coords, no timeline pollution --------

TEST(DcTestRunLiveJsonTest, BotPositionsAreCompactAndOutsideRecent)
{
    RunSnapshot s = Sample("tr-1", "ragefire");
    s.mapId = 389;
    BotPos tank{"tank", 1, -123.456f, 42.0f, -60.19f, true};
    tank.name = "Bruggle";
    BotPos healer{"healer", 5, 10.0f, -5.5f, 1.0f, false};
    healer.name = "Sistina";
    s.bots.push_back(tank);
    s.bots.push_back(healer);

    std::string const json = Build(1700000000ull, {s});

    EXPECT_NE(json.find("\"mapId\":389"), std::string::npos);
    // Short keys + 1-decimal rounding (not the full float noise).
    EXPECT_NE(json.find("\"role\":\"tank\",\"name\":\"Bruggle\",\"cls\":1,\"x\":-123.5,\"y\":42.0,\"z\":-60.2,\"alive\":true"),
              std::string::npos);
    EXPECT_NE(json.find("\"role\":\"healer\",\"name\":\"Sistina\",\"cls\":5,\"x\":10.0,\"y\":-5.5,\"z\":1.0,\"alive\":false"),
              std::string::npos);
    // Positions live in their own array, never spilled into the human-readable
    // "recent" timeline (which stays empty for this snapshot).
    EXPECT_NE(json.find("\"bots\":["), std::string::npos);
    EXPECT_NE(json.find("\"recent\":[]"), std::string::npos);
}

// The dashboard labels each bot row with its character name, so the key has to
// be there even for a snapshot taken before a name was resolved — an absent key
// would render "undefined" rather than fall back to the role.
TEST(DcTestRunLiveJsonTest, BotNameIsAlwaysEmittedEvenWhenUnknown)
{
    RunSnapshot s = Sample("tr-1", "ragefire");
    s.bots.push_back(BotPos{"dps", 4, 0.f, 0.f, 0.f, true});

    std::string const json = Build(1700000000ull, {s});

    EXPECT_NE(json.find("\"role\":\"dps\",\"name\":\"\""), std::string::npos);
}

// ---- live "why is it sitting there" fields ---------------------------------
//
// The recent[] timeline only records state CHANGES, so a run wedged in one
// state for ten minutes has nothing in it. These fields are read live off the
// tank each heartbeat and are what the dashboard shows for a stuck run.

TEST(DcTestRunLiveJsonTest, LiveStallAndBossAreEmitted)
{
    RunSnapshot s = Sample("tr-1", "wailing-caverns");
    s.stall = "can't reach Mutanus";
    s.bossName = "Mutanus the Devourer";
    s.sinceProgressS = 412;
    s.inCombat = true;

    std::string const json = Build(1700000000ull, {s});

    EXPECT_NE(json.find("\"stall\":\"can't reach Mutanus\""), std::string::npos);
    EXPECT_NE(json.find("\"bossName\":\"Mutanus the Devourer\""), std::string::npos);
    EXPECT_NE(json.find("\"sinceProgressS\":412"), std::string::npos);
    EXPECT_NE(json.find("\"inCombat\":true"), std::string::npos);
}

TEST(DcTestRunLiveJsonTest, LiveFieldsDefaultToEmptyNotAbsent)
{
    // An older app.js reads these unconditionally; they must always be present.
    std::string const json = Build(1700000000ull, {Sample("tr-1", "deadmines")});

    EXPECT_NE(json.find("\"stall\":\"\""), std::string::npos);
    EXPECT_NE(json.find("\"bossName\":\"\""), std::string::npos);
    EXPECT_NE(json.find("\"sinceProgressS\":0"), std::string::npos);
    EXPECT_NE(json.find("\"wiped\":false"), std::string::npos);
    EXPECT_NE(json.find("\"wipeOnBoss\":false"), std::string::npos);
    EXPECT_NE(json.find("\"wipeOpponent\":\"\""), std::string::npos);
}

// ---- wipe in progress -------------------------------------------------------
//
// The wipe grace window holds a corpse party for 15s before the verdict lands;
// without these the live card shows a party doing nothing with no hint why.

TEST(DcTestRunLiveJsonTest, WipeOnABossNamesTheBoss)
{
    RunSnapshot s = Sample("tr-1", "underbog");
    s.wiped = true;
    s.wipeOnBoss = true;
    s.wipeOpponent = "Ghaz'an";

    std::string const json = Build(1700000000ull, {s});

    EXPECT_NE(json.find("\"wiped\":true"), std::string::npos);
    EXPECT_NE(json.find("\"wipeOnBoss\":true"), std::string::npos);
    EXPECT_NE(json.find("\"wipeOpponent\":\"Ghaz'an\""), std::string::npos);
}

TEST(DcTestRunLiveJsonTest, WipeToTrashNamesTheMobAndClearsTheBossFlag)
{
    RunSnapshot s = Sample("tr-1", "underbog");
    s.wiped = true;
    s.wipeOnBoss = false;
    s.wipeOpponent = "Bog Giant";

    std::string const json = Build(1700000000ull, {s});

    EXPECT_NE(json.find("\"wiped\":true"), std::string::npos);
    EXPECT_NE(json.find("\"wipeOnBoss\":false"), std::string::npos);
    EXPECT_NE(json.find("\"wipeOpponent\":\"Bog Giant\""), std::string::npos);
}

// ---- per-bot health / combat ------------------------------------------------

TEST(DcTestRunLiveJsonTest, BotHealthAndCombatAreEmitted)
{
    RunSnapshot s = Sample("tr-1", "ragefire");
    BotPos tank{"tank", 1, 0.f, 0.f, 0.f, true};
    tank.hp = 37;
    tank.inCombat = true;
    BotPos corpse{"healer", 5, 0.f, 0.f, 0.f, false};
    corpse.hp = 0;
    s.bots.push_back(tank);
    s.bots.push_back(corpse);

    std::string const json = Build(1700000000ull, {s});

    EXPECT_NE(json.find("\"alive\":true,\"hp\":37,\"inCombat\":true"), std::string::npos);
    EXPECT_NE(json.find("\"alive\":false,\"hp\":0,\"inCombat\":false"), std::string::npos);
}

// ---- per-bot mana ------------------------------------------------------------
// -1 is the contract the dashboard keys off to omit the bar for a class with no
// mana pool; a real 0% must stay distinguishable from it.

TEST(DcTestRunLiveJsonTest, BotManaIsEmittedAndNonManaClassesReportMinusOne)
{
    RunSnapshot s = Sample("tr-1", "ragefire");
    BotPos priest{"heal", 5, 0.f, 0.f, 0.f, true};
    priest.hp = 100;
    priest.mp = 42;
    BotPos rogue{"dps", 4, 0.f, 0.f, 0.f, true};
    rogue.hp = 100;              // leaves mp at the -1 default
    BotPos oom{"dps", 8, 0.f, 0.f, 0.f, true};
    oom.hp = 100;
    oom.mp = 0;
    s.bots.push_back(priest);
    s.bots.push_back(rogue);
    s.bots.push_back(oom);

    std::string const json = Build(1700000000ull, {s});

    EXPECT_NE(json.find("\"inCombat\":false,\"mp\":42"), std::string::npos);
    EXPECT_NE(json.find("\"inCombat\":false,\"mp\":-1"), std::string::npos);
    EXPECT_NE(json.find("\"inCombat\":false,\"mp\":0"), std::string::npos);
}
