/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <sstream>

#include "TestRun/DcDiagSnapshot.h"

// Capture() needs a live bot and is exercised in-game; these pin the
// SERIALIZER, which is what the dashboard and any post-mortem actually parse.
// A malformed diag object would corrupt the whole JSONL line it is embedded in,
// taking the rest of the run history down with it.

using DcDiag::BossSnapshot;
using DcDiag::MemberSnapshot;
using DcDiag::Snapshot;

namespace
{
    std::string Json(Snapshot const& snap)
    {
        std::ostringstream s;
        s.precision(9);
        DcDiag::AppendJson(s, snap);
        return s.str();
    }

    Snapshot Sample()
    {
        Snapshot snap;
        snap.valid = true;
        snap.capturedAt = "teardown";
        snap.enabled = true;
        snap.stateStr = "stalled";
        snap.phase = "moving";
        snap.stallReason = "can't reach Mutanus the Devourer";
        snap.nextBossEntry = 3654;
        snap.nextBossName = "Mutanus the Devourer";
        snap.stickyBoss = 3654;
        snap.approachTargetEntry = 3654;
        snap.distToTarget = 84.5f;
        snap.mapId = 43;
        snap.partySize = 5;
        snap.aliveCount = 4;
        return snap;
    }
}

// ---- an unresolvable tank still produces a parseable object ----------------

TEST(DcDiagSnapshotTest, InvalidSnapshotClosesTheObject)
{
    Snapshot snap;  // valid == false
    snap.capturedAt = "teardown";

    std::string const json = Json(snap);

    EXPECT_EQ(json, "{\"valid\":false,\"capturedAt\":\"teardown\"}");
}

// ---- the fields a post-mortem actually reads -------------------------------

TEST(DcDiagSnapshotTest, CarriesStallStateAndTarget)
{
    std::string const json = Json(Sample());

    EXPECT_NE(json.find("\"valid\":true"), std::string::npos);
    EXPECT_NE(json.find("\"state\":\"stalled\""), std::string::npos);
    EXPECT_NE(json.find("\"phase\":\"moving\""), std::string::npos);
    EXPECT_NE(json.find("\"stallReason\":\"can't reach Mutanus the Devourer\""),
              std::string::npos);
    EXPECT_NE(json.find("\"nextName\":\"Mutanus the Devourer\""), std::string::npos);
    EXPECT_NE(json.find("\"sticky\":3654"), std::string::npos);
}

// ---- the three target notions disagreeing is called out explicitly ---------

TEST(DcDiagSnapshotTest, TargetMismatchIsSerialized)
{
    Snapshot snap = Sample();
    snap.targetMismatch = true;

    EXPECT_NE(Json(snap).find("\"mismatch\":true"), std::string::npos);
}

// ---- party rows: dead members and off-map members are distinguishable ------

TEST(DcDiagSnapshotTest, PartyRowsCarryLivenessAndDistance)
{
    Snapshot snap = Sample();

    MemberSnapshot dead;
    dead.name = "Healbot";
    dead.online = true;
    dead.alive = false;
    dead.healthPct = 0;
    dead.distToTank = 31.2f;
    snap.members.push_back(dead);

    // Left behind outside the instance: distance to the tank is meaningless
    // and reported as -1 rather than a misleading huge number.
    MemberSnapshot offMap;
    offMap.name = "Straggler";
    offMap.online = true;
    offMap.alive = true;
    offMap.mapId = 0;
    offMap.distToTank = -1.f;
    snap.members.push_back(offMap);

    std::string const json = Json(snap);

    EXPECT_NE(json.find("\"name\":\"Healbot\""), std::string::npos);
    EXPECT_NE(json.find("\"alive\":false"), std::string::npos);
    EXPECT_NE(json.find("\"distToTank\":-1"), std::string::npos);
    EXPECT_NE(json.find("\"size\":5"), std::string::npos);
    EXPECT_NE(json.find("\"alive\":4"), std::string::npos);
}

// ---- doneVia is the field that explains a no_progress verdict --------------
//
// The no-progress watchdog only resets on a "mask" or "anchor" completion. A
// roster whose kills all report "bossState" is a run that was clearing fine
// while the watchdog saw zero progress — so the token must survive into JSON.

TEST(DcDiagSnapshotTest, RosterRecordsWhichCompletionPathFired)
{
    Snapshot snap = Sample();

    BossSnapshot mask;
    mask.entry = 100;
    mask.name = "Rhahk'Zor";
    mask.kind = "boss";
    mask.status = "dead";
    mask.doneVia = "mask";
    snap.roster.push_back(mask);

    BossSnapshot gate;
    gate.entry = 200;
    gate.name = "Gatewatcher";
    gate.kind = "boss";
    gate.status = "dead";
    gate.doneVia = "bossState";
    snap.roster.push_back(gate);

    BossSnapshot pending;
    pending.entry = 300;
    pending.name = "Mutanus the Devourer";
    pending.kind = "boss";
    pending.status = "alive";
    pending.isTarget = true;
    snap.roster.push_back(pending);

    std::string const json = Json(snap);

    EXPECT_NE(json.find("\"doneVia\":\"mask\""), std::string::npos);
    EXPECT_NE(json.find("\"doneVia\":\"bossState\""), std::string::npos);
    EXPECT_NE(json.find("\"doneVia\":\"\""), std::string::npos);
    EXPECT_NE(json.find("\"isTarget\":true"), std::string::npos);
}

// ---- free text must not break the enclosing JSONL line ---------------------

TEST(DcDiagSnapshotTest, EscapesQuotesAndNewlines)
{
    Snapshot snap = Sample();
    snap.stallReason = "said \"stuck\"\nand gave up";
    snap.pathFailureReason = "no poly at\ttarget";

    std::string const json = Json(snap);

    EXPECT_NE(json.find("\\\"stuck\\\""), std::string::npos);
    EXPECT_NE(json.find("\\n"), std::string::npos);
    EXPECT_EQ(json.find('\n'), std::string::npos);   // no raw newline leaked
    EXPECT_EQ(json.find('\t'), std::string::npos);   // nor a raw tab
}

// ---- the log one-liner names the wedge ------------------------------------

TEST(DcDiagSnapshotTest, SummarizeNamesTheWedge)
{
    Snapshot snap = Sample();
    snap.doorStalled = true;
    snap.doorStalledForMs = 45000;
    snap.pathReachable = false;

    std::string const line = DcDiag::Summarize(snap);

    EXPECT_NE(line.find("state=stalled"), std::string::npos);
    EXPECT_NE(line.find("UNREACHABLE"), std::string::npos);
    EXPECT_NE(line.find("DOOR-STALLED 45s"), std::string::npos);
}

TEST(DcDiagSnapshotTest, SummarizeHandlesInvalidSnapshot)
{
    Snapshot snap;
    EXPECT_NE(DcDiag::Summarize(snap).find("unresolvable"), std::string::npos);
}

// ---- combat blame ----------------------------------------------------------
//
// The freeze these fields exist for: two members flagged in combat with no
// victim, no attackers and full health for minutes, while nothing in the record
// named the unit responsible. Capture() reads the live CombatManager and is
// exercised in-game; what is pinned here is that every input the phantom-combat
// hatch weighs survives into the record, and that the two ways a flagged member
// can look "unheld" never collapse into one another.

namespace
{
    using DcDiag::CombatHolderSnapshot;

    CombatHolderSnapshot LegitimateHolder()
    {
        CombatHolderSnapshot h;
        h.name = "Selin Fireheart";
        h.entry = 24723;
        h.isCreature = true;
        h.alive = true;
        h.sameMap = true;
        h.reachable = true;
        h.reachChecked = true;
        h.legitimate = true;
        h.dist = 71.2f;
        h.healthPct = 100;
        return h;
    }
}

TEST(DcDiagSnapshotTest, CombatHoldersAreSerializedForFlaggedMembersOnly)
{
    Snapshot snap = Sample();

    MemberSnapshot held;
    held.name = "Xomja";
    held.online = true;
    held.alive = true;
    held.inCombat = true;
    held.holderRefCount = 1;
    held.combatHolders.push_back(LegitimateHolder());
    snap.members.push_back(held);

    // Out of combat: no blame block at all. An empty holder list here would be
    // indistinguishable from "flagged and held by nothing", which is the single
    // most misleading thing this record could say.
    MemberSnapshot clean;
    clean.name = "Yidama";
    clean.online = true;
    clean.alive = true;
    snap.members.push_back(clean);

    std::string const json = Json(snap);

    EXPECT_NE(json.find("\"name\":\"Selin Fireheart\""), std::string::npos);
    EXPECT_NE(json.find("\"entry\":24723"), std::string::npos);
    EXPECT_NE(json.find("\"legitimate\":true"), std::string::npos);
    EXPECT_NE(json.find("\"holderRefs\":1"), std::string::npos);
    // Exactly one member carries the block — the flagged one.
    EXPECT_EQ(json.find("\"combatHolders\":"),
              json.rfind("\"combatHolders\":"));
}

// A holder excluded by a cheap guard never gets a pathfind, so `reachable`
// false must not read as "the navmesh could not reach it" — that points a
// reader at the geometry when the answer is the leash.
TEST(DcDiagSnapshotTest, UntestedReachabilityIsDistinctFromUnreachable)
{
    Snapshot snap = Sample();

    MemberSnapshot held;
    held.name = "Xomja";
    held.online = true;
    held.inCombat = true;
    held.holderRefCount = 1;

    CombatHolderSnapshot evading = LegitimateHolder();
    evading.name = "Wretched Husk";
    evading.entry = 24690;
    evading.evading = true;
    evading.reachable = false;
    evading.reachChecked = false;
    evading.legitimate = false;
    held.combatHolders.push_back(evading);
    snap.members.push_back(held);

    std::string const json = Json(snap);
    EXPECT_NE(json.find("\"reachable\":false"), std::string::npos);
    EXPECT_NE(json.find("\"reachChecked\":false"), std::string::npos);

    std::string const line = DcDiag::SummarizeCombat(snap);
    EXPECT_NE(line.find("path-not-tested"), std::string::npos);
    EXPECT_EQ(line.find("UNREACHABLE"), std::string::npos);
}

// The summary has to say WHICH guard saved a holder, because "held by a real
// fight" and "held by something that can never touch us" look identical from
// every other field.
TEST(DcDiagSnapshotTest, SummarizeCombatNamesHoldersAndTheirVerdicts)
{
    Snapshot snap = Sample();
    snap.inCombatCount = 1;

    MemberSnapshot held;
    held.name = "Xomja";
    held.online = true;
    held.inCombat = true;
    held.botState = "noncombat";
    held.holderRefCount = 1;
    // tr-20260803-211838-7, verbatim: Selin holds three members from 60-75yd, alive
    // and perfectly path-reachable, while his own CanAIAttack (`X > 216`) forbids him
    // touching a party camped at X=165. Reachability calls that a real fight; the
    // holder's own script calls it impossible. It is PHANTOM, and the run sat wedged
    // for 334s because the verdict used to say otherwise.
    CombatHolderSnapshot boss = LegitimateHolder();
    boss.canAttackMe = false;
    boss.legitimate = false;
    held.combatHolders.push_back(boss);
    snap.members.push_back(held);

    std::string const line = DcDiag::SummarizeCombat(snap);

    EXPECT_NE(line.find("Xomja"), std::string::npos);
    EXPECT_NE(line.find("engine=noncombat"), std::string::npos);
    EXPECT_NE(line.find("Selin Fireheart(24723)"), std::string::npos);
    EXPECT_NE(line.find("reachable"), std::string::npos);
    EXPECT_NE(line.find("CANNOT-ATTACK-ME"), std::string::npos);
    EXPECT_NE(line.find("-> phantom"), std::string::npos);
    EXPECT_EQ(line.find("-> LEGITIMATE"), std::string::npos);
}

// Zero refs is the hatch's "opaque/forced combat" branch, not a phantom — and
// it must not read as "we found nothing", which is the same words for a very
// different state.
TEST(DcDiagSnapshotTest, SummarizeCombatDistinguishesNoRefsFromNoFight)
{
    Snapshot snap = Sample();
    snap.inCombatCount = 1;

    MemberSnapshot held;
    held.name = "Zeeron";
    held.online = true;
    held.inCombat = true;
    held.botState = "noncombat";
    held.holderRefCount = 0;
    snap.members.push_back(held);

    EXPECT_NE(DcDiag::SummarizeCombat(snap).find("NOTHING (no combat refs)"),
              std::string::npos);

    Snapshot idle = Sample();
    idle.inCombatCount = 0;
    EXPECT_NE(DcDiag::SummarizeCombat(idle).find("nobody in the party is flagged"),
              std::string::npos);
}

// The one-line wedge summary has to surface the two states that explain a
// stuck-in-combat freeze, or nobody grepping the log will ever open the JSON.
TEST(DcDiagSnapshotTest, SummarizeFlagsPhantomAndOffEngineMembers)
{
    Snapshot snap = Sample();

    MemberSnapshot phantom;
    phantom.name = "Xomja";
    phantom.online = true;
    phantom.inCombat = true;
    phantom.botState = "noncombat";
    phantom.phantomCombat = true;
    snap.members.push_back(phantom);

    // Flagged, but genuinely fighting on the combat engine: neither token.
    MemberSnapshot fighting;
    fighting.name = "Zeeron";
    fighting.online = true;
    fighting.inCombat = true;
    fighting.botState = "combat";
    snap.members.push_back(fighting);

    std::string const line = DcDiag::Summarize(snap);
    EXPECT_NE(line.find("PHANTOM-COMBAT=1"), std::string::npos);
    EXPECT_NE(line.find("FLAGGED-OFF-COMBAT-ENGINE=1"), std::string::npos);
}

TEST(DcDiagSnapshotTest, PhantomVerdictUsesPveRefsOnly)
{
    EXPECT_FALSE(DcDiag::IsLegitimatePvECombatHolder(true, true));
    EXPECT_TRUE(DcDiag::IsLegitimatePvECombatHolder(false, true));

    // A legitimate PvP ref must not rescue phantom PvE refs. With no PvE refs,
    // the trigger preserves its opaque-combat exception.
    EXPECT_FALSE(DcDiag::HasLegitimatePvECombatHolder(true, false));
    EXPECT_TRUE(DcDiag::HasLegitimatePvECombatHolder(false, false));
    EXPECT_TRUE(DcDiag::HasLegitimatePvECombatHolder(true, true));
}
