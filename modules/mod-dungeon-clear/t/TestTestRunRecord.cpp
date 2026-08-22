/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cstdlib>
#include <string>

#include "TestRun/DcTestRunRecord.h"

using DcTestRunRecord::CompEntry;
using DcTestRunRecord::EscapeJson;
using DcTestRunRecord::Record;
using DcTestRunRecord::StatusEntry;
using DcTestRunRecord::ToJsonl;

namespace
{
    Record SampleRecord()
    {
        Record r;
        r.runId = "tr-test-1";
        r.planId = "tp-test-1";
        r.dungeon = "deadmines";
        r.dungeonName = "The Deadmines";
        r.mapId = 36;
        r.instanceId = 174;
        r.level = 18;
        r.comp.push_back({"Botname", "warrior", "prot pve", "tank", 1234, 18});
        r.startedAtMs = 1000;
        r.endedAtMs = 813000;
        r.durationS = 812;
        r.result = "success";
        r.disableReason = "All bosses cleared!";
        r.bossesTotal = 6;
        r.bossesKilled = 6;
        r.bossTimeline.push_back({142, 644, "Rhahk'Zor", "mask"});
        r.statusTimeline.push_back({3, "moving", "En route"});
        r.pauses.push_back({222, "a closed door is blocking the path"});
        return r;
    }

    // Count occurrences of a substring.
    std::size_t Count(std::string const& hay, std::string const& needle)
    {
        std::size_t n = 0;
        for (std::size_t at = hay.find(needle); at != std::string::npos;
             at = hay.find(needle, at + needle.size()))
            ++n;
        return n;
    }
}

// ---- escaping --------------------------------------------------------------------

TEST(DcTestRunRecordTest, EscapesQuotesBackslashesAndControlChars)
{
    EXPECT_EQ(EscapeJson("plain"), "plain");
    EXPECT_EQ(EscapeJson("say \"hi\""), "say \\\"hi\\\"");
    EXPECT_EQ(EscapeJson("a\\b"), "a\\\\b");
    EXPECT_EQ(EscapeJson("line\nbreak"), "line\\nbreak");
    // Split literal: a plain "\x01b" would greedily parse as hex 0x1B.
    EXPECT_EQ(EscapeJson(std::string("nul\x01" "byte")), "nul\\u0001byte");
}

// ---- serializer shape ------------------------------------------------------------

TEST(DcTestRunRecordTest, SerializesKnownFields)
{
    std::string const line = ToJsonl(SampleRecord());
    EXPECT_EQ(line.front(), '{');
    EXPECT_EQ(line.back(), '}');
    EXPECT_NE(line.find("\"runId\":\"tr-test-1\""), std::string::npos);
    EXPECT_NE(line.find("\"planId\":\"tp-test-1\""), std::string::npos);
    EXPECT_NE(line.find("\"dungeon\":\"deadmines\""), std::string::npos);
    EXPECT_NE(line.find("\"mapId\":36"), std::string::npos);
    EXPECT_NE(line.find("\"result\":\"success\""), std::string::npos);
    EXPECT_NE(line.find("\"disableReason\":\"All bosses cleared!\""), std::string::npos);
    EXPECT_NE(line.find("\"name\":\"Rhahk'Zor\""), std::string::npos);
    EXPECT_NE(line.find("\"bossesKilled\":6"), std::string::npos);
    EXPECT_NE(line.find("\"heroic\":false"), std::string::npos);
    // One line — the sink is JSONL.
    EXPECT_EQ(line.find('\n'), std::string::npos);
}

// ---- wipe post-mortem ------------------------------------------------------------

TEST(DcTestRunRecordTest, WipeOnABossSerializesTheBoss)
{
    Record r = SampleRecord();
    r.result = "wipe";
    r.failReason = "party wiped on Ghaz'an (last standing: Botname)";
    r.wipeOnBoss = true;
    r.wipeOpponentEntry = 18105;
    r.wipeOpponent = "Ghaz'an";

    std::string const line = ToJsonl(r);
    EXPECT_NE(line.find("\"wipeOnBoss\":true"), std::string::npos);
    EXPECT_NE(line.find("\"wipeOpponentEntry\":18105"), std::string::npos);
    EXPECT_NE(line.find("\"wipeOpponent\":\"Ghaz'an\""), std::string::npos);
}

TEST(DcTestRunRecordTest, WipeToTrashKeepsTheBossFlagFalse)
{
    Record r = SampleRecord();
    r.result = "wipe";
    r.wipeOnBoss = false;
    r.wipeOpponentEntry = 17826;
    r.wipeOpponent = "Bog Giant";

    std::string const line = ToJsonl(r);
    EXPECT_NE(line.find("\"wipeOnBoss\":false"), std::string::npos);
    EXPECT_NE(line.find("\"wipeOpponent\":\"Bog Giant\""), std::string::npos);
}

TEST(DcTestRunRecordTest, WipeFieldsAreAlwaysPresentAndDefaultEmpty)
{
    // The dashboard reads them unconditionally; a run nobody died in must still
    // carry the keys, empty.
    std::string const line = ToJsonl(SampleRecord());
    EXPECT_NE(line.find("\"wipeOnBoss\":false"), std::string::npos);
    EXPECT_NE(line.find("\"wipeOpponentEntry\":0"), std::string::npos);
    EXPECT_NE(line.find("\"wipeOpponent\":\"\""), std::string::npos);
}

// ---- death log + roster ----------------------------------------------------------

TEST(DcTestRunRecordTest, DeathsSerializeWhoDiedAndToWhat)
{
    Record r = SampleRecord();
    r.result = "disabled";
    r.failReason = "run disabled: Bero died and no one left alive can resurrect "
                   "\xe2\x80\x94 killed by Anzu";
    r.deaths.push_back({410, "Bero", "Anzu", 23035, true});
    r.deaths.push_back({413, "Olanne", "Anzu", 23035, true});

    std::string const line = ToJsonl(r);
    EXPECT_NE(line.find("\"deaths\":[{\"t\":410,\"name\":\"Bero\",\"opponent\":\"Anzu\","
                        "\"opponentEntry\":23035,\"onBoss\":true},"
                        "{\"t\":413,\"name\":\"Olanne\",\"opponent\":\"Anzu\","
                        "\"opponentEntry\":23035,\"onBoss\":true}]"),
              std::string::npos);
    EXPECT_EQ(Count(line, "\"name\":\"Olanne\""), 1u);
}

TEST(DcTestRunRecordTest, DeathOutOfCombatCarriesNoOpponent)
{
    // A fall, a hazard — nothing was on the party, and the record must say so
    // rather than borrow the last boss the party fought.
    Record r = SampleRecord();
    r.deaths.push_back({88, "Tzaihran", "", 0, false});

    std::string const line = ToJsonl(r);
    EXPECT_NE(line.find("\"opponent\":\"\",\"opponentEntry\":0,\"onBoss\":false"),
              std::string::npos);
}

TEST(DcTestRunRecordTest, DeathsAndRosterKeysArePresentWhenEmpty)
{
    std::string const line = ToJsonl(SampleRecord());
    EXPECT_NE(line.find("\"bossRoster\":[]"), std::string::npos);
    EXPECT_NE(line.find("\"deaths\":[]"), std::string::npos);
}

TEST(DcTestRunRecordTest, BossRosterSerializesInProgressionOrder)
{
    Record r = SampleRecord();
    r.bossRoster = {"Rhahk'Zor", "Sneed", "Gilnid", "Mr. Smite", "Cookie",
                    "Edwin VanCleef"};

    std::string const line = ToJsonl(r);
    EXPECT_NE(line.find("\"bossRoster\":[\"Rhahk'Zor\",\"Sneed\",\"Gilnid\","
                        "\"Mr. Smite\",\"Cookie\",\"Edwin VanCleef\"]"),
              std::string::npos);
}

TEST(DcTestRunRecordTest, SchemaIsTen)
{
    EXPECT_NE(ToJsonl(SampleRecord()).find("\"schema\":10"), std::string::npos);
}

// The gear ceiling a run was rolled to. Two runs of the same dungeon are only
// comparable at the same ceiling, so it rides out on every record — 0 ilvl
// meaning uncapped, exactly as the playerbots conf encodes it.
TEST(DcTestRunRecordTest, GearCeilingSerializes)
{
    Record r = SampleRecord();
    r.gearIlvl = 141;
    r.gearQuality = 4;

    std::string const line = ToJsonl(r);
    EXPECT_NE(line.find("\"gearIlvl\":141"), std::string::npos);
    EXPECT_NE(line.find("\"gearQuality\":4"), std::string::npos);

    r.gearIlvl = 0;
    EXPECT_NE(ToJsonl(r).find("\"gearIlvl\":0"), std::string::npos);
}

// ---- roster runs (hand-picked real characters) ------------------------------

// A pool run must keep saying roster:false — the dashboard and the plan summary
// both read this to tell "the harness drew this party" from "a human picked it",
// and a run whose comp was randomly rolled must never claim the latter.
TEST(DcTestRunRecordTest, PoolRunsAreNotRosterRuns)
{
    std::string const json = ToJsonl(SampleRecord());
    EXPECT_NE(json.find("\"roster\":false"), std::string::npos);
}

TEST(DcTestRunRecordTest, RosterRunSerializesMarkedAgainstDetectedRole)
{
    Record r = SampleRecord();
    r.roster = true;
    r.compSeed = 0;  // a roster IS the comp; nothing to replay from a seed
    r.comp.clear();

    CompEntry tank;
    tank.name = "Olanne";
    tank.className = "warrior";
    tank.spec = "arms";
    tank.role = "tank";        // what the human marked
    tank.detectedRole = "dps"; // what the talents say
    tank.roleMismatch = true;
    tank.guid = 1234;
    tank.level = 70;
    tank.fromMap = 1;
    tank.fromX = -1.5f;
    tank.fromY = 2.5f;
    tank.fromZ = 3.5f;
    tank.fromO = 1.f;
    r.comp.push_back(tank);

    std::string const json = ToJsonl(r);
    EXPECT_NE(json.find("\"roster\":true"), std::string::npos);
    EXPECT_NE(json.find("\"role\":\"tank\""), std::string::npos);
    EXPECT_NE(json.find("\"detectedRole\":\"dps\""), std::string::npos);
    EXPECT_NE(json.find("\"roleMismatch\":true"), std::string::npos);
    // Origin position is what makes a manual recall possible after the run.
    EXPECT_NE(json.find("\"from\":{\"map\":1"), std::string::npos);
}

// ---- pull log --------------------------------------------------------------------

TEST(DcTestRunRecordTest, PullsSerializePredictedAgainstObserved)
{
    Record r = SampleRecord();
    // A well-called Leeroy, then the pull that ate the run: three predicted,
    // eight turned up.
    r.pulls.push_back({120, 17690, 2, 6, 15, 2, 2, false, false});
    r.pulls.push_back({164, 17691, 3, 9, 15, 8, 8, true, true});

    std::string const line = ToJsonl(r);
    EXPECT_NE(line.find("\"pulls\":[{\"t\":120,\"entry\":17690,\"predicted\":2,"
                        "\"predictedThirds\":6,\"ceilingThirds\":15,\"observed\":2,"
                        "\"observedElites\":2,\"advanced\":false,\"wipedHere\":false},"
                        "{\"t\":164,\"entry\":17691,\"predicted\":3,"
                        "\"predictedThirds\":9,\"ceilingThirds\":15,\"observed\":8,"
                        "\"observedElites\":8,\"advanced\":true,\"wipedHere\":true}]"),
              std::string::npos);
}

TEST(DcTestRunRecordTest, PullKeysArePresentWhenEmpty)
{
    // Off/On pull modes write no entries; the keys must still be there so a
    // consumer never has to distinguish "absent" from "none".
    std::string const line = ToJsonl(SampleRecord());
    EXPECT_NE(line.find("\"pulls\":[]"), std::string::npos);
    EXPECT_NE(line.find("\"pullsElided\":0"), std::string::npos);
}

TEST(DcTestRunRecordTest, HeroicSerializesTrue)
{
    Record r = SampleRecord();
    r.heroic = true;
    EXPECT_NE(ToJsonl(r).find("\"heroic\":true"), std::string::npos);
}

TEST(DcTestRunRecordTest, FreeTextReasonsAreEscapedInPlace)
{
    Record r = SampleRecord();
    r.failReason = "boss said \"no\"\nand left";
    std::string const line = ToJsonl(r);
    EXPECT_NE(line.find("\"failReason\":\"boss said \\\"no\\\"\\nand left\""),
              std::string::npos);
    EXPECT_EQ(line.find('\n'), std::string::npos);
}

// ---- status-timeline cap ---------------------------------------------------------

TEST(DcTestRunRecordTest, ShortStatusTimelineIsKeptWhole)
{
    Record r = SampleRecord();
    r.statusTimeline.clear();
    for (std::uint32_t i = 0; i < 100; ++i)
        r.statusTimeline.push_back({i, "moving", ""});
    std::string const line = ToJsonl(r);
    EXPECT_EQ(Count(line, "\"state\":\"moving\""), 100u);
    EXPECT_EQ(line.find("elided"), std::string::npos);
}

TEST(DcTestRunRecordTest, LongStatusTimelineKeepsHeadAndTail)
{
    Record r = SampleRecord();
    r.statusTimeline.clear();
    for (std::uint32_t i = 0; i < 500; ++i)
        r.statusTimeline.push_back({i, "moving", ""});
    std::string const line = ToJsonl(r);
    // 50 head + 150 tail survive; the elision marker declares the rest.
    EXPECT_EQ(Count(line, "\"state\":\"moving\""),
              DcTestRunRecord::kStatusHead + DcTestRunRecord::kStatusTail);
    EXPECT_NE(line.find("300 transitions elided"), std::string::npos);
    // Tail is the LAST entries: t=499 present, a mid value dropped.
    EXPECT_NE(line.find("{\"t\":499,"), std::string::npos);
    EXPECT_EQ(line.find("{\"t\":200,"), std::string::npos);
}

// ---- capture path ----------------------------------------------------------------

TEST(DcTestRunRecordTest, DefaultCapturePath)
{
    // The env override is exercised operationally; here just pin the default.
    if (!std::getenv("DC_TESTRUNS_FILE"))
        EXPECT_EQ(DcTestRunRecord::CapturePath(), "dc_testruns.jsonl");
}
