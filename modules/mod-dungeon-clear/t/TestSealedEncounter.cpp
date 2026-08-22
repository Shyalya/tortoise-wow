/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cmath>

#include "Ai/Dungeon/DungeonClear/Data/FightInPlaceRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/SealedEncounterRegistry.h"

// A SEALED ENCOUNTER is one whose room locks on encounter start — an InstanceScript
// DOOR_TYPE_ROOM door, held `open &= (state != IN_PROGRESS)` for the whole fight.
// Engage with a follower still outside and it spends the fight at a closed door.
//
// Everything here is the geometry the muster gate is only correct because of. Get the
// box wrong and the run either engages with people locked out (too small) or waits
// forever for a member who is already in (too large).

namespace
{
    uint32 constexpr MGT   = 585;
    uint32 constexpr SELIN = 24723;

    SealedEncounterRow const& Selin()
    {
        SealedEncounterRow const* r = SealedEncounterRegistry::Find(MGT, SELIN);
        EXPECT_NE(r, nullptr);
        return *r;
    }
}

TEST(DcSealedEncounterTest, OnlySelinIsRegisteredAndUnknownRowsMiss)
{
    EXPECT_NE(SealedEncounterRegistry::Find(MGT, SELIN), nullptr);
    // Magisters' Terrace's other bosses do NOT seal: Vexallus, Delrissa and
    // Kael'thas all fight in open rooms.
    EXPECT_EQ(SealedEncounterRegistry::Find(MGT, 24744), nullptr);   // Vexallus
    EXPECT_EQ(SealedEncounterRegistry::Find(MGT, 24560), nullptr);   // Delrissa
    EXPECT_EQ(SealedEncounterRegistry::Find(MGT, 24664), nullptr);   // Kael'thas
    // And no other map has a row, so every other dungeon pays one compare.
    EXPECT_EQ(SealedEncounterRegistry::Find(585 + 1, SELIN), nullptr);
    EXPECT_EQ(SealedEncounterRegistry::Find(0, 0), nullptr);
}

TEST(DcSealedEncounterTest, TheSealedRoomIsTheSameBoxAsTheNoPullZone)
{
    // Both boxes are derived from the same two facts — Selin's own CanAIAttack plane
    // (`who->GetPositionX() > 216.0f`) and the Assembly Chamber Door (GO 188065) at
    // X=215.1 — so their agreement is a property, not a coincidence to maintain by
    // hand. Asserted rather than shared as a literal because the two registries
    // answer different questions ("may the pull drag out of here" vs "will the door
    // lock me out") and a future room could need one without the other.
    SealedEncounterRow const& s = Selin();

    // Inside the sealed room => inside the no-pull room, at the corners and centre.
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(s, 216.0f, 0.0f));
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(MGT, 216.0f, 0.0f));
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(s, 242.07f, 0.3f));   // Selin
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(MGT, 242.07f, 0.3f));
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(s, 226.0f, 20.0f));   // west pack
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(MGT, 226.0f, 20.0f));

    // Outside one => outside the other. The doorway's near side and the corridor.
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(s, 215.0f, 0.0f));
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(MGT, 215.0f, 0.0f));
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(s, 170.46f, 0.57f));  // the camp
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(MGT, 170.46f, 0.57f));
}

TEST(DcSealedEncounterTest, TheDoorIsTheEdgeOfTheSealedRoom)
{
    // The whole point of testing the VOLUME rather than a radius around the tank: the
    // tank crosses the threshold BEFORE it engages, so a follower a perfectly
    // tolerable distance behind it is on the WRONG SIDE of a door at X=215.1.
    SealedEncounterRow const& s = Selin();

    // A tank just inside, engaging.
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(s, 217.0f, 0.0f));
    // A follower 10yd behind it — inside the clump radius, and locked out.
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(s, 207.0f, 0.0f));
    // Which is exactly why musterSpread alone could not express the requirement.
    EXPECT_LT(s.musterSpread, s.maxX - 207.0f);
}

TEST(DcSealedEncounterTest, ApproachRangeCoversTheDoorwayButNotTheScriptedPullCamp)
{
    // The gates must arm over the final walk-in and nothing else. Selin spawns at
    // (242.07, 0.3, 1.84).
    SealedEncounterRow const& s = Selin();
    float constexpr kBx = 242.07f, kBy = 0.3f, kBz = 1.84f;

    // Armed at the doorway and across the staging chamber in front of it, which is
    // where the party needs to start closing up.
    EXPECT_TRUE(SealedEncounterRegistry::InApproachRange(s, 216.0f, 0.0f, -2.9f, kBx, kBy, kBz));
    EXPECT_TRUE(SealedEncounterRegistry::InApproachRange(s, 209.58f, 18.97f, -2.05f, kBx, kBy, kBz));
    EXPECT_TRUE(SealedEncounterRegistry::InApproachRange(s, 199.0f, 0.0f, -2.35f, kBx, kBy, kBz));

    // NOT armed at the scripted-pull camp (170.46, 0.57), 71.6yd out — so the two
    // guard-pack stages keep running under the ordinary gates, with the party
    // legitimately camped a long way back, exactly as before.
    EXPECT_FALSE(SealedEncounterRegistry::InApproachRange(s, 170.46f, 0.57f, -2.72f, kBx, kBy, kBz));
    // Nor at either stand spot's far side, nor back at the antechamber.
    EXPECT_FALSE(SealedEncounterRegistry::InApproachRange(s, 134.14f, -14.36f, -2.61f, kBx, kBy, kBz));

    // 3D, so a party on another floor cannot arm the gates from under the boss.
    EXPECT_FALSE(SealedEncounterRegistry::InApproachRange(s, 216.0f, 0.0f, -60.0f, kBx, kBy, kBz));
}

TEST(DcSealedEncounterTest, TheClumpIsAchievableAndTheMusterIsBounded)
{
    SealedEncounterRow const& s = Selin();

    // The clump has to sit above what the followers' OWN rules make them hold at, or
    // the gate is a stall rather than a muster: the tank waits for a party that, by
    // its own rule, is already close enough. Two rules bound it, both ~6yd:
    //   * follow-tank trails at min(followDistance, 6yd);
    //   * the scout lag clamps itself to max(2, spread - (kTrailArrival + 2)) and a
    //     held follower settles ~kTrailArrival(4) beyond that, so with this override
    //     in force it rests at about 2+4 = 6yd behind a stopped tank.
    // tr-20260803-134213-2 is the deadlock this bound prevents: 365+ ticks of
    // "advance yielding: party not ready" against "scout-lag: holding at trail point
    // (18.2yd behind tank)" — and the lag was clamping against the 25yd SETTING
    // rather than this override, which is fixed in
    // DcPartyState::LeaderEffectiveMaxSpread.
    EXPECT_GT(s.musterSpread, 6.0f);
    // And below the generic PartyMaxSpread default (25), or it would not be tightening
    // anything.
    EXPECT_LT(s.musterSpread, 25.0f);

    // The wait is BOUNDED: a member that cannot path in (stuck, mid-rez, feared out
    // of the room) must not be able to hold the run open forever.
    EXPECT_GT(DC_SEALED_MUSTER_TIMEOUT_MS, 0u);
    EXPECT_LE(DC_SEALED_MUSTER_TIMEOUT_MS, 60000u);
}

// Azjol-Nerub's Anub'arak (map 601). instance_azjol_nerub registers three
// DOOR_TYPE_ROOM doors on DATA_ANUBARAK, and boss_anub_arak schedules
// EVENT_CLOSE_DOORS 5s after the pull — whose whole body is the
// BossAI::_JustEngagedWith() that shuts them. Anyone still in the north corridor
// is locked out for the fight.

TEST(SealedEncounterTest, AnubarakArenaIsSealed)
{
    SealedEncounterRow const* row = SealedEncounterRegistry::Find(601, 29120);
    ASSERT_NE(row, nullptr) << "Anub'arak's arena must be registered as sealed";

    // The arena floor (one flat surface at z 224.07-224.29 on the live navmesh).
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(*row, 551.0f, 248.3f));  // the boss
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(*row, 550.4f, 254.7f));  // the doors
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(*row, 530.0f, 240.0f));  // SW rim
    EXPECT_TRUE(SealedEncounterRegistry::InSealedRoom(*row, 570.0f, 274.0f));  // NE rim

    // The north corridor the party comes down, and the Prime Guard pull, are OUT.
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, 551.0f, 300.0f));
    EXPECT_FALSE(SealedEncounterRegistry::InSealedRoom(*row, 542.0f, 341.4f));

    // approachRadius must reach up into the corridor mouth but stop short of the
    // Prime Guards, so their pull runs under the ordinary gates.
    EXPECT_TRUE(SealedEncounterRegistry::InApproachRange(*row, 551.0f, 290.0f, 226.0f,
                                                         551.0f, 248.3f, 224.0f));
    EXPECT_FALSE(SealedEncounterRegistry::InApproachRange(*row, 542.0f, 341.4f, 240.9f,
                                                          551.0f, 248.3f, 224.0f));
}

