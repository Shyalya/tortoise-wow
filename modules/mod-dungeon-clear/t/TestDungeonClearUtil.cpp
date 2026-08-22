/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/Util/DcDoorIndex.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"
#include "PlayerbotAIConfig.h"
#include "Player.h"
#include "WorldSession.h"
#include "ObjectGuid.h"
#include "Group.h"
#include "PlayerbotAI.h"
#include "AiObjectContext.h"
#include "Value.h"
#include "World.h"
#include "WorldMock.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/DcRunState.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "ScriptMgr.h"
#include "ScriptDefines/MiscScript.h"
#include "ScriptDefines/PlayerScript.h"
#include "ScriptDefines/WorldObjectScript.h"
#include "ScriptDefines/UnitScript.h"
#include "ScriptDefines/AllCommandScript.h"
#include "ScriptDefines/GroupScript.h"
#include "ScriptDefines/GlobalScript.h"
#include "ScriptDefines/AllMapScript.h"
#include "TestMap.h"

class DungeonClearWorldMock : public WorldMock
{
public:
    MOCK_METHOD(SQLQueryHolderCallback&, AddQueryHolderCallback, (SQLQueryHolderCallback&& callback), (override));
#ifdef MOD_PLAYERBOTS
    MOCK_METHOD(char const*, GetPlayerbotsDBRevision, (), (const, override));
#endif
};

class DungeonClearTestBase : public ::testing::Test
{
protected:
    static void EnsureScriptRegistriesInitialized()
    {
        static bool initialized = false;
        if (!initialized)
        {
            ScriptRegistry<MiscScript>::InitEnabledHooksIfNeeded(MISCHOOK_END);
            ScriptRegistry<WorldObjectScript>::InitEnabledHooksIfNeeded(WORLDOBJECTHOOK_END);
            ScriptRegistry<UnitScript>::InitEnabledHooksIfNeeded(UNITHOOK_END);
            ScriptRegistry<PlayerScript>::InitEnabledHooksIfNeeded(PLAYERHOOK_END);
            ScriptRegistry<CommandSC>::InitEnabledHooksIfNeeded(ALLCOMMANDHOOK_END);
            ScriptRegistry<GroupScript>::InitEnabledHooksIfNeeded(GROUPHOOK_END);
            ScriptRegistry<GlobalScript>::InitEnabledHooksIfNeeded(GLOBALHOOK_END);
            ScriptRegistry<AllMapScript>::InitEnabledHooksIfNeeded(ALLMAPHOOK_END);
            initialized = true;
        }
    }

    void SetUp() override
    {
        EnsureScriptRegistriesInitialized();
        TestMap::EnsureDBC();
        testMap = new TestMap();

        originalWorld = sWorld.release();
        worldMock = new ::testing::NiceMock<DungeonClearWorldMock>();
        sWorld.reset(worldMock);

        static std::string emptyString;
        ON_CALL(*worldMock, GetDataPath()).WillByDefault(::testing::ReturnRef(emptyString));
        ON_CALL(*worldMock, GetRealmName()).WillByDefault(::testing::ReturnRef(emptyString));
        ON_CALL(*worldMock, GetDefaultDbcLocale()).WillByDefault(::testing::Return(LOCALE_enUS));
        ON_CALL(*worldMock, getRate(::testing::_)).WillByDefault(::testing::Return(1.0f));
        ON_CALL(*worldMock, getBoolConfig(::testing::_)).WillByDefault(::testing::Return(false));
        ON_CALL(*worldMock, getIntConfig(::testing::_)).WillByDefault(::testing::Return(0));
        ON_CALL(*worldMock, getFloatConfig(::testing::_)).WillByDefault(::testing::Return(0.0f));
        ON_CALL(*worldMock, GetPlayerSecurityLimit()).WillByDefault(::testing::Return(SEC_PLAYER));
    }

    void TearDown() override
    {
        delete testMap;
        testMap = nullptr;

        IWorld* currentWorld = sWorld.release();
        delete currentWorld;
        worldMock = nullptr;

        sWorld.reset(originalWorld);
        originalWorld = nullptr;
    }

    IWorld* originalWorld = nullptr;
    ::testing::NiceMock<DungeonClearWorldMock>* worldMock = nullptr;
    TestMap* testMap = nullptr;
};

namespace
{
class TestPlayer : public Player
{
public:
    using Player::Player;

    void UpdateObjectVisibility(bool /*forced*/ = true, bool /*fromUpdate*/ = false) override { }

    void ForceInitValues(ObjectGuid::LowType guidLow = 1)
    {
        Object::_Create(guidLow, uint32(0), HighGuid::Player);
    }

    void SetTestMap(Map* map)
    {
        WorldObject::SetMap(map);
    }
};

template <class T>
class ManualSetRefValue : public UntypedValue, public Value<T&>
{
public:
    ManualSetRefValue(PlayerbotAI* botAI, T defaultValue, std::string const name = "value")
        : UntypedValue(botAI, name), value(defaultValue), defaultValue(defaultValue)
    {
    }

    virtual ~ManualSetRefValue() {}

    T& Get() override { return value; }
    T& LazyGet() override { return value; }
    T& RefGet() override { return value; }
    void Set(T& val) override { value = val; }
    void Update() override {}
    void Reset() override { value = defaultValue; }

protected:
    T value;
    T defaultValue;
};

class MockAiObjectContext : public AiObjectContext
{
public:
    MockAiObjectContext() : AiObjectContext(nullptr), ai(nullptr) {}

    ~MockAiObjectContext() override
    {
        for (auto const& kv : values)
            delete kv.second;
    }

    void SetAI(PlayerbotAI* ai)
    {
        this->ai = ai;
    }

    UntypedValue* GetUntypedValue(std::string const name) override
    {
        auto it = values.find(name);
        if (it != values.end())
            return it->second;
        return nullptr;
    }

    template <class T>
    void SetValue(std::string const& name, T val)
    {
        auto it = values.find(name);
        if (it != values.end())
        {
            delete it->second;
        }
        values[name] = new ManualSetValue<T>(ai, val, name);
    }

    template <class T>
    void SetRefValue(std::string const& name, T val)
    {
        auto it = values.find(name);
        if (it != values.end())
        {
            delete it->second;
        }
        values[name] = new ManualSetRefValue<T>(ai, val, name);
    }

private:
    std::unordered_map<std::string, UntypedValue*> values;
    PlayerbotAI* ai;
};

class MockPlayerbotAI : public PlayerbotAI
{
public:
    MockPlayerbotAI(Player* bot, AiObjectContext* ctx)
    {
        this->bot = bot;
        this->aiObjectContext = ctx;
    }
};


class DungeonClearUtilTest : public DungeonClearTestBase
{
protected:
    void SetUp() override
    {
        DungeonClearTestBase::SetUp();
        // Setup a minimal player session
        session = new WorldSession(1, "test", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING,
            0, LOCALE_enUS, 0, false, false, 0);
        session->InitRBACDataForTest();

        player = new TestPlayer(session);
        player->ForceInitValues();
        player->SetTestMap(testMap);
        session->SetPlayer(player);
        player->SetSession(session);
    }

    void TearDown() override
    {
        // Intentional leak of session and player to bypass database dependencies in destructors.
        session = nullptr;
        player = nullptr;
        DungeonClearTestBase::TearDown();
    }

    WorldSession* session = nullptr;
    TestPlayer* player = nullptr;
};

// Test clamping of RestMinHpPct based on almostFullHealth config
TEST_F(DungeonClearUtilTest, RestMinHpPctClamping)
{
    uint32 originalValue = sPlayerbotAIConfig.almostFullHealth;

    // Below ceiling (90.0) -> should return the configured value
    sPlayerbotAIConfig.almostFullHealth = 80;
    EXPECT_FLOAT_EQ(DcPartyState::RestMinHpPct(), 80.0f);

    // Above ceiling (90.0) -> should clamp to 90.0f
    sPlayerbotAIConfig.almostFullHealth = 95;
    EXPECT_FLOAT_EQ(DcPartyState::RestMinHpPct(), 90.0f);

    // Restore
    sPlayerbotAIConfig.almostFullHealth = originalValue;
}

// Test clamping of RestMinMpPct based on highMana config
TEST_F(DungeonClearUtilTest, RestMinMpPctClamping)
{
    uint32 originalValue = sPlayerbotAIConfig.highMana;

    // Below ceiling (75.0) -> should return the configured value
    sPlayerbotAIConfig.highMana = 60;
    EXPECT_FLOAT_EQ(DcPartyState::RestMinMpPct(), 60.0f);

    // Above ceiling (75.0) -> should clamp to 75.0f
    sPlayerbotAIConfig.highMana = 85;
    EXPECT_FLOAT_EQ(DcPartyState::RestMinMpPct(), 75.0f);

    // Restore
    sPlayerbotAIConfig.highMana = originalValue;
}

// Test party readiness and waiting descriptions for a solo player (no group)
TEST_F(DungeonClearUtilTest, SoloPlayerReadyChecks)
{
    // Solo player should always be ready
    EXPECT_TRUE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f));

    // Describing not-ready players or looting players should return empty string for solo
    EXPECT_EQ(DcPartyState::DescribePartyNotReady(player, 90.0f, 75.0f, 30.0f), "");
    EXPECT_EQ(DcPartyState::DescribePartyLooting(player), "");
    EXPECT_FALSE(DcPartyState::IsAnyPartyMemberLooting(player));
}

// Test nullptr robustness for utility methods
TEST_F(DungeonClearUtilTest, NullptrSafetyChecks)
{
    EXPECT_FALSE(DcPartyState::IsPartyReady(nullptr, 90.0f, 75.0f, 30.0f));
    EXPECT_EQ(DcPartyState::DescribePartyNotReady(nullptr, 90.0f, 75.0f, 30.0f), "");
    EXPECT_EQ(DcPartyState::DescribePartyLooting(nullptr), "");
    EXPECT_FALSE(DcPartyState::IsAnyPartyMemberLooting(nullptr));
    EXPECT_FALSE(DcEngageGeometry::IsReachable(nullptr, 0.0f, 0.0f, 0.0f));
    EXPECT_FALSE(DcEngageGeometry::IsLevelReachable(nullptr, nullptr));
    EXPECT_FALSE(DcEngageGeometry::IsDisplayedDead(nullptr));
    EXPECT_EQ(DcTargeting::FindBlockingTrash(nullptr, {}, 10.0f, 1.0f, {}), nullptr);
    EXPECT_EQ(DcTargeting::FindBlockingTrashCorridor(nullptr, {}, 10.0f, 5.0f, {}), nullptr);
}
}

class DungeonClearGroupTest : public DungeonClearTestBase
{
protected:
    void SetUp() override
    {
        DungeonClearTestBase::SetUp();
        // Set up the leader (bot tank)
        session = new WorldSession(1, "leader_sess", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING,
            0, LOCALE_enUS, 0, false, false, 0);
        session->InitRBACDataForTest();
        player = new TestPlayer(session);
        player->ForceInitValues(1);
        player->SetTestMap(testMap);
        player->SetName("Tank");
        session->SetPlayer(player);
        player->SetSession(session);
        player->SetMapId(1);
        player->Relocate(0.0f, 0.0f, 0.0f);
        player->setDeathState(DeathState::Alive);
        player->SetUInt32Value(UNIT_FIELD_HEALTH, 100);
        player->SetUInt32Value(UNIT_FIELD_MAXHEALTH, 100);
        player->setPowerType(POWER_MANA);
        player->SetMaxPower(POWER_MANA, 100);
        player->SetPower(POWER_MANA, 100);

        // Set up 4 members
        for (int i = 0; i < 4; ++i)
        {
            std::string name = "Member" + std::to_string(i + 1);
            sessions[i] = new WorldSession(2 + i, name.c_str(), 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING,
                0, LOCALE_enUS, 0, false, false, 0);
            sessions[i]->InitRBACDataForTest();
            members[i] = new TestPlayer(sessions[i]);
            members[i]->ForceInitValues(2 + i);
            members[i]->SetTestMap(testMap);
            members[i]->SetName(name);
            sessions[i]->SetPlayer(members[i]);
            members[i]->SetSession(sessions[i]);
            
            // Default to same map, close, alive, full HP, mana-user with full mana
            members[i]->SetMapId(1);
            members[i]->Relocate(1.0f * (i + 1), 0.0f, 0.0f); // within 5yd range
            members[i]->setDeathState(DeathState::Alive);
            members[i]->SetUInt32Value(UNIT_FIELD_HEALTH, 100);
            members[i]->SetUInt32Value(UNIT_FIELD_MAXHEALTH, 100);
            members[i]->setPowerType(POWER_MANA);
            members[i]->SetMaxPower(POWER_MANA, 100);
            members[i]->SetPower(POWER_MANA, 100);
        }

        // Create group and link all. LIFO linking means members will be iterated in reverse order:
        // members[3], members[2], members[1], members[0], player
        group = new Group();
        player->SetGroup(group, 0);
        for (int i = 0; i < 4; ++i)
        {
            members[i]->SetGroup(group, 0);
        }
    }

    void TearDown() override
    {
        // Delete the group (unlinks members automatically)
        delete group;

        // Intentional leaks of sessions and players to bypass database dependencies in destructors.
        session = nullptr;
        player = nullptr;
        for (int i = 0; i < 4; ++i)
        {
            sessions[i] = nullptr;
            members[i] = nullptr;
        }
        DungeonClearTestBase::TearDown();
    }

    WorldSession* session = nullptr;
    TestPlayer* player = nullptr;

    WorldSession* sessions[4] = { nullptr };
    TestPlayer* members[4] = { nullptr };

    Group* group = nullptr;
};

// 1. Group is completely ready
TEST_F(DungeonClearGroupTest, GroupReadyScenario)
{
    EXPECT_TRUE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f));
    EXPECT_EQ(DcPartyState::DescribePartyNotReady(player, 90.0f, 75.0f, 30.0f), "");
}

// 2. One member has low health
TEST_F(DungeonClearGroupTest, GroupNotReadyHPScenario)
{
    members[0]->SetUInt32Value(UNIT_FIELD_HEALTH, 50); // 50% HP
    EXPECT_FALSE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f));
    EXPECT_EQ(DcPartyState::DescribePartyNotReady(player, 90.0f, 75.0f, 30.0f), "Waiting on Member1 (low HP)");
}

// 3. One member has low mana
TEST_F(DungeonClearGroupTest, GroupNotReadyManaScenario)
{
    members[1]->SetPower(POWER_MANA, 30); // 30% mana
    EXPECT_FALSE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f));
    EXPECT_EQ(DcPartyState::DescribePartyNotReady(player, 90.0f, 75.0f, 30.0f), "Waiting on Member2 (low mana)");
}

// 4. One member is out of range
TEST_F(DungeonClearGroupTest, GroupNotReadyDistanceScenario)
{
    members[2]->Relocate(50.0f, 0.0f, 0.0f); // 50yd away
    EXPECT_FALSE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f));
    EXPECT_EQ(DcPartyState::DescribePartyNotReady(player, 90.0f, 75.0f, 30.0f), "Waiting on Member3 (out of range)");
}

// 5. Multiple members have issues
TEST_F(DungeonClearGroupTest, GroupMultipleIssuesScenario)
{
    members[0]->SetUInt32Value(UNIT_FIELD_HEALTH, 50); // low HP
    members[1]->SetPower(POWER_MANA, 30); // low mana
    members[2]->Relocate(50.0f, 0.0f, 0.0f); // out of range

    EXPECT_FALSE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f));
    
    // GroupReference iterates in LIFO order: Member3, Member2, Member1
    std::string expected = "Waiting on Member3 (out of range), Member2 (low mana), Member1 (low HP)";
    EXPECT_EQ(DcPartyState::DescribePartyNotReady(player, 90.0f, 75.0f, 30.0f), expected);
}

// 6. Max named count limit (collapse to +N more)
TEST_F(DungeonClearGroupTest, GroupCollapseIssuesScenario)
{
    // Make all 4 members not ready
    members[0]->SetUInt32Value(UNIT_FIELD_HEALTH, 50); // low HP
    members[1]->SetPower(POWER_MANA, 30); // low mana
    members[2]->Relocate(50.0f, 0.0f, 0.0f); // out of range
    members[3]->SetUInt32Value(UNIT_FIELD_HEALTH, 50); // low HP

    EXPECT_FALSE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f));
    
    // MAX_NAMED is 3. In LIFO order: Member4, Member3, Member2 are named, and Member1 collapses to "+1 more"
    std::string expected = "Waiting on Member4 (low HP), Member3 (out of range), Member2 (low mana) +1 more";
    EXPECT_EQ(DcPartyState::DescribePartyNotReady(player, 90.0f, 75.0f, 30.0f), expected);
}

// 7. Camp-anchored spread (pull mode): the party is pinned at a camp behind the
// scouting tank, so it can never satisfy a tank-anchored spread once the camp
// standoff reaches PartyMaxSpread — that was the tank-waits/party-held deadlock.
// Anchored at the camp, a party that is set where it was told to hold is ready.
TEST_F(DungeonClearGroupTest, GroupSpreadAnchoredAtCampScenario)
{
    // Tank scouted 50yd ahead of the camp; whole party holds within 3yd of it.
    Position const camp(50.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i)
        members[i]->Relocate(50.0f + 0.5f * (i + 1), 0.0f, 0.0f);

    // Tank-anchored: every member is ~50yd out -> the old permanent "catch up".
    EXPECT_FALSE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f));

    // Camp-anchored: the party is set at camp -> ready.
    EXPECT_TRUE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f, &camp));
    EXPECT_EQ(DcPartyState::DescribePartyNotReady(player, 90.0f, 75.0f, 30.0f, &camp), "");
}

// 8. Camp-anchored spread still catches a genuine straggler far from the camp.
TEST_F(DungeonClearGroupTest, GroupSpreadAnchoredStragglerScenario)
{
    Position const camp(50.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i)
        members[i]->Relocate(50.0f + 0.5f * (i + 1), 0.0f, 0.0f);
    members[2]->Relocate(100.0f, 0.0f, 0.0f);  // 50yd past the camp

    EXPECT_FALSE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f, &camp));
    EXPECT_EQ(DcPartyState::DescribePartyNotReady(player, 90.0f, 75.0f, 30.0f, &camp),
              "Waiting on Member3 (out of range)");
}

// 9. Camp-anchored spread with the absolute tank-gap backstop: a camp gone
// stale right where the party stands satisfies the anchored spread forever
// while the tank glides away — the scout-runaway gap. The maxTankGap cap
// catches it; a tank still within the cap stays ready.
TEST_F(DungeonClearGroupTest, GroupSpreadAnchoredStaleCampTankGapScenario)
{
    // Stale camp sitting on the party, tank (player, at origin pre-Relocate)
    // scouted 80yd ahead of everyone.
    Position const camp(1.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i)
        members[i]->Relocate(1.0f + 0.5f * (i + 1), 0.0f, 0.0f);
    player->Relocate(80.0f, 0.0f, 0.0f);

    // Anchored spread alone: the party is "set at camp" -> passes, the runaway.
    EXPECT_TRUE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f, &camp));
    // With the backstop the 77yd+ real gap trips the gate...
    EXPECT_FALSE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f, &camp, 60.0f));
    // ...and every member reports as the limiting reason (LIFO order, capped).
    EXPECT_EQ(DcPartyState::DescribePartyNotReady(player, 90.0f, 75.0f, 30.0f, &camp, 60.0f),
              "Waiting on Member4 (out of range), Member3 (out of range), "
              "Member2 (out of range) +1 more");

    // Tank back inside the cap: ready again (members still set at the camp).
    player->Relocate(40.0f, 0.0f, 0.0f);
    EXPECT_TRUE(DcPartyState::IsPartyReady(player, 90.0f, 75.0f, 30.0f, &camp, 60.0f));
}

class DungeonClearStatusTest : public DungeonClearTestBase
{
protected:
    void SetUp() override
    {
        DungeonClearTestBase::SetUp();
        session = new WorldSession(1, "tank", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING,
            0, LOCALE_enUS, 0, false, false, 0);
        session->InitRBACDataForTest();
        player = new TestPlayer(session);
        player->ForceInitValues(1);
        player->SetTestMap(testMap);
        player->SetName("Tank");
        session->SetPlayer(player);
        player->SetSession(session);
        player->SetMapId(1);
        player->Relocate(0.0f, 0.0f, 0.0f);

        context = new MockAiObjectContext();
        botAI = new MockPlayerbotAI(player, context);
        context->SetAI(botAI);

        // Register default values for context. The run-level state (enabled /
        // paused / pause reason + door / selected boss / leader-fight latches) is
        // one owned struct now — DcRunState, read via DcRun::Of.
        context->SetRefValue<DcRunState>("dungeon clear run state", DcRunState{});
        context->SetValue<std::optional<DungeonBossInfo>>("next dungeon boss", std::nullopt);
        context->SetRefValue<std::unordered_set<uint32>>("dungeon clear skipped", {});
        context->SetRefValue<std::string>("dungeon clear stall reason", "");
        context->SetValue<Unit*>("current target", nullptr);
        context->SetValue<ObjectGuid>("dungeon clear blocking door", ObjectGuid::Empty);
        context->SetValue<bool>("has available loot", false);
        context->SetValue<bool>("can loot", false);
        context->SetValue<std::string>("dungeon clear phase", "idle");
        // Approach FSM state read by BuildStatusPayload (the long-path cache's
        // target entry drives the routeReady display); one owned struct now.
        context->SetRefValue<DcApproachState>("dungeon clear approach state", DcApproachState{});
        // Advanced-pull state read by BuildStatusPayload (the trailing tri-state
        // preference + the live Dynamic verdict).
        context->SetValue<bool>("dungeon clear pull mode", false);
        context->SetValue<uint32>("dungeon clear pull setting", 0);
        // Advanced-pull transient state is one owned struct now (phase + Dynamic
        // verdict live inside it); a default DcPullContext is Idle / no verdict.
        context->SetRefValue<DcPullContext>("dungeon clear pull context", DcPullContext{});
    }

    void TearDown() override
    {
        // Deleting botAI deletes context automatically
        delete botAI;

        // Intentional leaks
        session = nullptr;
        player = nullptr;
        DungeonClearTestBase::TearDown();
    }

    WorldSession* session = nullptr;
    TestPlayer* player = nullptr;
    MockAiObjectContext* context = nullptr;
    MockPlayerbotAI* botAI = nullptr;
};

// Test status push when mod-dungeon-clear is disabled
TEST_F(DungeonClearStatusTest, StatusDisabled)
{
    std::string expected = "STATUS\t0\t0\tNone\t\t0\toff\t\t0\t0";
    EXPECT_EQ(DcStatusPublisher::BuildStatusPayload(botAI), expected);
}

// Test status push when paused with no recorded reason: detail falls back to a
// generic hold so the panel never shows an empty paused state.
TEST_F(DungeonClearStatusTest, StatusPaused)
{
    DcRun::Of(context).enabled = true;
    DcRun::Of(context).paused = true;

    std::string expected = "STATUS\t1\t0\tNone\t\t0\tpaused\tholding position\t0\t0";
    EXPECT_EQ(DcStatusPublisher::BuildStatusPayload(botAI), expected);
}

// Test status push when paused with a recorded reason (manual hold or a door
// the tank can't open): the reason rides in the detail field for the panel.
TEST_F(DungeonClearStatusTest, StatusPausedWithReason)
{
    DcRun::Of(context).enabled = true;
    DcRun::Of(context).paused = true;
    DcRun::Of(context).pauseReason = "a closed door is blocking the path";

    std::string expected =
        "STATUS\t1\t0\tNone\t\t0\tpaused\ta closed door is blocking the path\t0\t0";
    EXPECT_EQ(DcStatusPublisher::BuildStatusPayload(botAI), expected);
}

// A stall with a flagged blocking door is only reported as "door_blocked"
// when the door GO resolves AND still reads closed RIGHT NOW — the cached
// blocking-door value and the stall string both outlive the door opening, and
// trusting them kept the panel shouting "Blocked" at an open gate. Here the
// GUID resolves to nothing (no GO exists in the test map), so the truthful
// report is the generic stall, not a door claim we can't verify.
TEST_F(DungeonClearStatusTest, StatusStalledDoorUnverifiedFallsBackToStalled)
{
    DcRun::Of(context).enabled = true;
    context->SetRefValue<std::string>("dungeon clear stall reason", "door_blocked");
    context->SetValue<ObjectGuid>("dungeon clear blocking door", ObjectGuid(uint64(12345)));

    std::string expected = "STATUS\t1\t0\tNone\tdoor_blocked\t0\tstalled\t\t0\t0";
    EXPECT_EQ(DcStatusPublisher::BuildStatusPayload(botAI), expected);
}

// Test status push when looting
TEST_F(DungeonClearStatusTest, StatusLooting)
{
    DcRun::Of(context).enabled = true;
    context->SetValue<bool>("has available loot", true);

    std::string expected = "STATUS\t1\t0\tNone\t\t0\tlooting\tCollecting loot.\t0\t0";
    EXPECT_EQ(DcStatusPublisher::BuildStatusPayload(botAI), expected);
}

// Test status push when fighting trash
TEST_F(DungeonClearStatusTest, StatusFightingTrash)
{
    DcRun::Of(context).enabled = true;
    player->SetUnitFlag(UNIT_FLAG_IN_COMBAT); // make bot IsInCombat() return true

    // Set up a mock target player named "Goblin"
    WorldSession* targetSession = new WorldSession(9, "goblin", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING,
        0, LOCALE_enUS, 0, false, false, 0);
    targetSession->InitRBACDataForTest();
    TestPlayer* target = new TestPlayer(targetSession);
    target->ForceInitValues(9);
    target->SetTestMap(testMap);
    target->SetName("Goblin");

    context->SetValue<Unit*>("current target", target);

    std::string expected = "STATUS\t1\t0\tNone\t\t0\tfighting_trash\tFighting Goblin.\t0\t0";
    EXPECT_EQ(DcStatusPublisher::BuildStatusPayload(botAI), expected);
}



// --- DcDoorIndex::NeedsRebuild (door-list cache policy) -------------------

TEST(DcDoorIndexTest, RebuildsWhenNeverBuilt)
{
    // builtMs == 0 is the "never built" sentinel -> always rebuild.
    EXPECT_TRUE(DcDoorIndex::NeedsRebuild(0, 1000, 100, 100));
}

TEST(DcDoorIndexTest, NoRebuildWhenFreshAndSameSize)
{
    // Built 500ms ago, store size unchanged, under the 2000ms TTL -> reuse.
    EXPECT_FALSE(DcDoorIndex::NeedsRebuild(1000, 1500, 200, 200));
}

TEST(DcDoorIndexTest, RebuildsOnStoreSizeDelta)
{
    // Grids streamed in/out: store grew/shrank -> rebuild even if fresh.
    EXPECT_TRUE(DcDoorIndex::NeedsRebuild(1000, 1100, 200, 201));
    EXPECT_TRUE(DcDoorIndex::NeedsRebuild(1000, 1100, 201, 200));
}

TEST(DcDoorIndexTest, RebuildsOnTtlExpiry)
{
    // Same size but the snapshot has aged past kRebuildMs (2000ms).
    EXPECT_FALSE(DcDoorIndex::NeedsRebuild(1000, 1000 + DcDoorIndex::kRebuildMs - 1, 50, 50));
    EXPECT_TRUE(DcDoorIndex::NeedsRebuild(1000, 1000 + DcDoorIndex::kRebuildMs, 50, 50));
}

// --- DcTickMemo::MemoValid (within-tick window) --------------------------

TEST(DcTickMemoTest, ZeroStampIsNeverValid)
{
    // stampMs == 0 is the "never filled" sentinel.
    EXPECT_FALSE(DcTickMemo::MemoValid(0, 0));
    EXPECT_FALSE(DcTickMemo::MemoValid(0, 1000));
}

TEST(DcTickMemoTest, WithinWindowIsValidBoundaryInclusive)
{
    EXPECT_TRUE(DcTickMemo::MemoValid(1000, 1000));                                  // same instant
    EXPECT_TRUE(DcTickMemo::MemoValid(1000, 1000 + DcTickMemo::kMemoWindowMs));      // exactly at the edge
}

TEST(DcTickMemoTest, PastWindowIsStale)
{
    // One ms past the 50ms window — and a full AI tick (>=100ms) later.
    EXPECT_FALSE(DcTickMemo::MemoValid(1000, 1000 + DcTickMemo::kMemoWindowMs + 1));
    EXPECT_FALSE(DcTickMemo::MemoValid(1000, 1100));
}

TEST(DcTickMemoTest, MsWraparoundIsHandled)
{
    // stamp just below the uint32 ceiling, now just after wrap: getMSTimeDiff
    // semantics give a small positive delta, so a near-stamp stays valid.
    std::uint32_t const stamp = 0xFFFFFFF0u;     // ceiling - 15
    EXPECT_TRUE(DcTickMemo::MemoValid(stamp, 0x00000005u));    // 20ms across the wrap
    EXPECT_FALSE(DcTickMemo::MemoValid(stamp, 0x00000040u));   // ~80ms across the wrap
}

TEST(DcTickMemoTest, EnsureFreshResetsFieldsOnNewTick)
{
    DcTickMemo m;
    m.EnsureFresh(1000);            // opens a tick
    m.atBossEngage = 1;
    m.betweenPullsReadyStrict = 0;
    m.EnsureFresh(1000 + 10);       // same tick (<=50ms) -> fields survive
    EXPECT_EQ(m.atBossEngage, 1);
    EXPECT_EQ(m.betweenPullsReadyStrict, 0);
    m.EnsureFresh(1000 + 200);      // a new tick -> all fields cleared to -1
    EXPECT_EQ(m.atBossEngage, -1);
    EXPECT_EQ(m.betweenPullsReadyStrict, -1);
    EXPECT_EQ(m.betweenPullsReadyLoose, -1);
}
