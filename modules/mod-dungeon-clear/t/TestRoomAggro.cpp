/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "Ai/Dungeon/DungeonClear/Data/RoomAggroRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcEngageGeometry.h"

// --- Find -----------------------------------------------------------------

TEST(RoomAggroRegistryTest, FindsFlaggedBoss)
{
    RoomAggroBoss const* b = RoomAggroRegistry::Find(557, 18341);  // Pandemonius
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->bossEntry, 18341u);
    EXPECT_FLOAT_EQ(b->radius, 70.0f);
}

// Pandemonius mirrors boss_pandemonius::PullRoom(): the three room-add entries
// AND the ROOM_EXIT(-145) < Y < ROOM_ENTERANCE(-50) corridor. The Y band keeps
// the room-clear off the adds standing behind him toward Tavarok (Y <= -145),
// which the script never force-pulls.
TEST(RoomAggroRegistryTest, PandemoniusHasRoomAddWhitelistAndYBand)
{
    RoomAggroBoss const* b = RoomAggroRegistry::Find(557, 18341);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->memberEntries,
              (std::vector<uint32>{18309, 18311, 18313}));
    ASSERT_TRUE(b->hasYBand);
    EXPECT_FLOAT_EQ(b->minY, -145.0f);
    EXPECT_FLOAT_EQ(b->maxY, -50.0f);

    // An add in the corridor is room trash; the same add behind the boss
    // (Y <= -145) or in front of the entrance (Y >= -50) is outside the pull.
    EXPECT_TRUE(RoomAggroRegistry::InRoomBand(*b, -97.0f));   // mid-room
    EXPECT_FALSE(RoomAggroRegistry::InRoomBand(*b, -160.0f)); // behind, toward Tavarok
    EXPECT_FALSE(RoomAggroRegistry::InRoomBand(*b, -30.0f));  // in front of the room
    // Strict bounds, matching the script's < / > guards.
    EXPECT_FALSE(RoomAggroRegistry::InRoomBand(*b, -145.0f));
    EXPECT_FALSE(RoomAggroRegistry::InRoomBand(*b, -50.0f));
}

// A boss with no Y band (every row but Pandemonius) admits any world Y.
TEST(RoomAggroRegistryTest, NoYBandAdmitsAnyWorldY)
{
    RoomAggroBoss boss{555, 18667, 100.0f, {}};  // Blackheart — no band
    ASSERT_FALSE(boss.hasYBand);
    EXPECT_TRUE(RoomAggroRegistry::InRoomBand(boss, 0.0f));
    EXPECT_TRUE(RoomAggroRegistry::InRoomBand(boss, -9999.0f));
    EXPECT_TRUE(RoomAggroRegistry::InRoomBand(boss, 9999.0f));
}

TEST(RoomAggroRegistryTest, UnflaggedBossReturnsNull)
{
    // Right map, wrong entry.
    EXPECT_EQ(RoomAggroRegistry::Find(557, 99999), nullptr);
    // Right entry, wrong map (registry is keyed by both).
    EXPECT_EQ(RoomAggroRegistry::Find(0, 18341), nullptr);
}

TEST(RoomAggroRegistryTest, MultiWingMapIsKeyedByEntry)
{
    // SM Cathedral shares map 189 with other wings; only the cathedral bosses
    // are flagged.
    EXPECT_NE(RoomAggroRegistry::Find(189, 3976), nullptr);  // Mograine
    EXPECT_NE(RoomAggroRegistry::Find(189, 3977), nullptr);  // Whitemane
    EXPECT_EQ(RoomAggroRegistry::Find(189, 6487), nullptr);  // (some other 189 npc)
}

// Scholomance's linked pair are both flagged so the room-trash value tracks the
// room around the live boss (Vectus) AND excludes the off-roster partner
// (Marduk) from being chased as a faction-15 student. Unlike every other
// room-aggro boss, their room is scoped to an EXPLICIT whitelist (Scholomance
// Students, 10475) because the whole chamber stands neutral until struck — the
// whitelist both keeps unrelated hostile packs out of "the room" and signals the
// value to clear the neutral students (see DungeonClearRoomTrashValue).
TEST(RoomAggroRegistryTest, ScholomanceLinkedPairBothFlagged)
{
    RoomAggroBoss const* vectus = RoomAggroRegistry::Find(289, 10432);
    RoomAggroBoss const* marduk = RoomAggroRegistry::Find(289, 10433);
    ASSERT_NE(vectus, nullptr);
    ASSERT_NE(marduk, nullptr);
    EXPECT_EQ(RoomAggroRegistry::Find(289, 10475), nullptr);  // Student is room, not boss

    // Both rows carry the student-only whitelist.
    ASSERT_EQ(vectus->memberEntries.size(), 1u);
    EXPECT_EQ(vectus->memberEntries[0], 10475u);
    EXPECT_EQ(marduk->memberEntries, vectus->memberEntries);

    // The whitelist scopes the room: a Scholomance Student inside the radius and
    // outside the boss sphere is room trash; an unrelated hostile pack (e.g. a
    // Diseased Ghoul, 10495) at the same distance is NOT.
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*vectus, 10475, 45.0f, 31.0f));
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*vectus, 10495, 45.0f, 31.0f));
}

// Mechano-Lord Capacitus (Mechanar, 554) no longer has a room-aggro entry: the
// boss order was changed to Gyro-Kill -> Capacitus -> Iron-Hand, so the tank
// approaches his pit from the NW and the SE Driller pack falls on the post-boss
// walk down to Iron-Hand (plain trash-clear), removing the reason for the
// pre-clear. Assert the entry is gone so a re-add is a deliberate choice.
TEST(RoomAggroRegistryTest, MechanarCapacitusHasNoRoomAggroEntry)
{
    EXPECT_EQ(RoomAggroRegistry::Find(554, 19219), nullptr);
}

// Gilnid (Deadmines, 36) is flagged NOT because he force-pulls his room — his whole
// script is a Molten Metal cast, a yell and a door — but because the goblin foundry
// cannot be decomposed into packs: 19 level-18 ELITES standing 7.1-57.6yd from him,
// 17 of which chain together at a same-level elite's ~20yd aggro radius. The pull
// governor's 6yd-spread / 10yd-assist windows read that as "estimated 2" and Leeroy
// the room, then Gilnid joins the pile (tp-20260815-171416-1: 5 of 20 runs wiped
// there, status alternating boss/trash every 1-2s). radius 60 covers all 19; 50
// would strand the three western-most at 52.0 / 55.3 / 57.6yd.
TEST(RoomAggroRegistryTest, DeadminesGilnidPreClearsTheFoundry)
{
    RoomAggroBoss const* gilnid = RoomAggroRegistry::Find(36, 1763);
    ASSERT_NE(gilnid, nullptr);
    EXPECT_FLOAT_EQ(gilnid->radius, 60.0f);
    // Any hostile in the room — the foundry is a mixed Craftsman/Engineer floor and
    // the safe over-approximation is the documented default for a row like this.
    EXPECT_TRUE(gilnid->memberEntries.empty());
    EXPECT_FALSE(gilnid->hasYBand);
    // No overrides: the computed boss sphere already excludes the two Craftsmen at
    // 7.1/9.2yd (they come with the boss) and admits everything past it as trash.
    EXPECT_FLOAT_EQ(gilnid->pullOutRadius, 0.0f);
    EXPECT_FLOAT_EQ(gilnid->skirtRadius, 0.0f);
    EXPECT_FLOAT_EQ(RoomAggroRegistry::SkirtOverride(36, 1763), 0.0f);
    // The room members are room, not bosses.
    EXPECT_EQ(RoomAggroRegistry::Find(36, 1731), nullptr);  // Goblin Craftsman
    EXPECT_EQ(RoomAggroRegistry::Find(36, 622), nullptr);   // Goblin Engineer
}

// Nethermancer Sepethrea (Mechanar, 554) IS a room-aggro boss: her chamber holds
// three elite trash groups pre-cleared and dragged back toward the entrance before
// the pull. radius 70 covers the farthest (Pack A ~67yd); pullOutRadius 14 shrinks
// the room-trash exclusion so Pack B — parked ~17yd out, inside her ~28yd real
// aggro sphere — is KEPT as clearable trash instead of coming with the boss.
TEST(RoomAggroRegistryTest, MechanarSepethreaHasPullOutRoom)
{
    RoomAggroBoss const* sep = RoomAggroRegistry::Find(554, 19221);
    ASSERT_NE(sep, nullptr);
    EXPECT_FLOAT_EQ(sep->radius, 70.0f);
    EXPECT_TRUE(sep->memberEntries.empty());   // any hostile in the chamber
    EXPECT_FALSE(sep->hasYBand);
    EXPECT_GT(sep->pullOutRadius, 0.0f);        // the coupled shrink is set
    EXPECT_LT(sep->pullOutRadius, 17.0f);       // below Pack B's nearest member (17.3yd)
    // The fight-standoff skirt is WIDENED past her raw ~28yd sphere so the dragged
    // pack is fought clear of her aggro/CallForHelp (and the open floor her Raging
    // Flames roam), yet stays DECOUPLED from the exclusion (pullOutRadius).
    EXPECT_GT(sep->skirtRadius, 28.0f);
    EXPECT_GT(sep->skirtRadius, sep->pullOutRadius);
}

// SkirtOverride returns the row's widened skirt for a flagged boss and 0 elsewhere,
// so RoomAggroSphereRadius (which maxes the computed sphere against it) widens ONLY
// for the flagged boss. Pandemonius carries no override -> 0 (computed sphere alone).
TEST(RoomAggroRegistryTest, SkirtOverrideOnlyForFlaggedRow)
{
    EXPECT_FLOAT_EQ(RoomAggroRegistry::SkirtOverride(554, 19221),
                    RoomAggroRegistry::Find(554, 19221)->skirtRadius);
    EXPECT_FLOAT_EQ(RoomAggroRegistry::SkirtOverride(557, 18341), 0.0f);  // Pandemonius: none
    EXPECT_FLOAT_EQ(RoomAggroRegistry::SkirtOverride(554, 12345), 0.0f);  // not a room boss
}

// The widened skirt must NOT change Pack B's room-trash membership: that reads
// pullOutRadius (14yd), never the skirt. Pack B at 17.3yd is still KEPT even though
// it now sits well inside the 40yd skirt.
TEST(RoomAggroRegistryTest, MechanarSepethreaSkirtDoesNotReclassifyPackB)
{
    RoomAggroBoss const* sep = RoomAggroRegistry::Find(554, 19221);
    ASSERT_NE(sep, nullptr);
    ASSERT_GT(sep->skirtRadius, 17.3f);   // Pack B is inside the skirt
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*sep, 19510, 17.3f, sep->pullOutRadius));
}

// The pullOutRadius is what KEEPS Pack B as room trash. Pack B's nearest member
// sits ~17.3yd from Sepethrea: inside her computed ~28yd exclusion sphere (would be
// dropped as "comes with the boss") but OUTSIDE the 14yd pull-out radius the value
// passes as bossSafeRadius when the row carries one — so it stays clearable and the
// advanced pull drags it out. This mirrors what DungeonClearRoomTrashValue does with
// room->pullOutRadius; IsRoomTrash itself just takes the effective radius.
TEST(RoomAggroRegistryTest, MechanarSepethreaPullOutKeepsFrontPack)
{
    RoomAggroBoss const* sep = RoomAggroRegistry::Find(554, 19221);
    ASSERT_NE(sep, nullptr);
    // With the shrunk pull-out radius: Pack B (17.3yd) is KEPT.
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*sep, 19510, 17.3f, sep->pullOutRadius));
    // With the real ~28yd exclusion sphere it would be dropped (the old behaviour
    // the shrink exists to override).
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*sep, 19510, 17.3f, 28.0f));
    // Something genuinely glued to her (inside 14yd) still comes with the boss.
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(*sep, 19510, 10.0f, sep->pullOutRadius));
    // Pack A far out (67yd) is still inside the 70yd room radius.
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(*sep, 19168, 67.0f, sep->pullOutRadius));
}

// --- IsMemberEntry --------------------------------------------------------

TEST(RoomAggroRegistryTest, EmptyWhitelistMatchesAnyEntry)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {}};
    EXPECT_TRUE(RoomAggroRegistry::IsMemberEntry(boss, 18309));
    EXPECT_TRUE(RoomAggroRegistry::IsMemberEntry(boss, 12345));
}

TEST(RoomAggroRegistryTest, NonEmptyWhitelistMatchesOnlyListed)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {18309, 18311, 18313}};
    EXPECT_TRUE(RoomAggroRegistry::IsMemberEntry(boss, 18311));
    EXPECT_FALSE(RoomAggroRegistry::IsMemberEntry(boss, 99999));
}

// --- IsRoomTrash (radius + boss-aggro-sphere exclusion) -------------------

TEST(RoomAggroRegistryTest, RoomTrashInsideRadiusOutsideBossSphere)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {}};
    // 30yd from the boss, boss aggro sphere 12yd -> clearable room trash.
    EXPECT_TRUE(RoomAggroRegistry::IsRoomTrash(boss, 18309, 30.0f, 12.0f));
}

TEST(RoomAggroRegistryTest, RoomTrashBeyondRadiusExcluded)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {}};
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(boss, 18309, 80.0f, 12.0f));
}

TEST(RoomAggroRegistryTest, RoomTrashInsideBossSphereExcluded)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {}};
    // Glued to the boss (8yd, sphere 12yd) -> comes with the boss, not counted.
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(boss, 18309, 8.0f, 12.0f));
}

TEST(RoomAggroRegistryTest, RoomTrashNonMemberExcluded)
{
    RoomAggroBoss boss{557, 18341, 70.0f, {18309}};
    // In range and outside the sphere, but not on the whitelist.
    EXPECT_FALSE(RoomAggroRegistry::IsRoomTrash(boss, 99999, 30.0f, 12.0f));
}

// --- NeedsRoomAggroSkirt (the skirt chooser) ------------------------------
//
// Boss centred at the origin, aggro sphere radius 10. The bot approaches a
// target and we ask whether the straight 2D chord clips the sphere.

TEST(RoomAggroSkirtTest, ChordCrossingSphereNeedsSkirt)
{
    // Bot at (-30, 5), target at (30, 5): the chord runs left-to-right just 5yd
    // above the boss at the origin — well inside the 10yd sphere.
    EXPECT_TRUE(DcEngageGeometry::NeedsRoomAggroSkirt(
        -30.0f, 5.0f, 30.0f, 5.0f, 0.0f, 0.0f, 10.0f));
}

TEST(RoomAggroSkirtTest, ChordClearOfSphereGoesDirect)
{
    // Same span but 20yd above the boss: the chord never enters the 10yd sphere.
    EXPECT_FALSE(DcEngageGeometry::NeedsRoomAggroSkirt(
        -30.0f, 20.0f, 30.0f, 20.0f, 0.0f, 0.0f, 10.0f));
}

TEST(RoomAggroSkirtTest, TargetOnNearSideGoesDirect)
{
    // Bot and target both well to one side of the boss; the segment's closest
    // approach to the origin is the target endpoint at 25yd, outside the sphere.
    EXPECT_FALSE(DcEngageGeometry::NeedsRoomAggroSkirt(
        -40.0f, 0.0f, -25.0f, 0.0f, 0.0f, 0.0f, 10.0f));
}

TEST(RoomAggroSkirtTest, BotInsidePaddedSphereStillSkirts)
{
    // Recovery case: the bot is standing 4yd from the boss centre, inside the
    // 10yd sphere. The chord's closest point (the bot endpoint) is within the
    // radius, so a skirt/exit waypoint is still demanded — never "go direct".
    EXPECT_TRUE(DcEngageGeometry::NeedsRoomAggroSkirt(
        4.0f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, 10.0f));
}

TEST(RoomAggroSkirtTest, DegenerateZeroLengthChord)
{
    // Bot and target coincide far from the boss -> the (point) chord is 50yd out,
    // outside the sphere -> no skirt. (Exercises the A==B segment path.)
    EXPECT_FALSE(DcEngageGeometry::NeedsRoomAggroSkirt(
        50.0f, 0.0f, 50.0f, 0.0f, 0.0f, 0.0f, 10.0f));
    // And the same coincident point sitting ON the boss -> inside -> skirt.
    EXPECT_TRUE(DcEngageGeometry::NeedsRoomAggroSkirt(
        1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 10.0f));
}

TEST(RoomAggroSkirtTest, NonPositiveRadiusNeverSkirts)
{
    EXPECT_FALSE(DcEngageGeometry::NeedsRoomAggroSkirt(
        -30.0f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_FALSE(DcEngageGeometry::NeedsRoomAggroSkirt(
        -30.0f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, -5.0f));
}

// --- FirstViolatedSphere (the en-route avoidance chooser) -----------------
//
// Same chord test, now over a SET of bystander packs. The contract is
// "nearest violated sphere to the bot", because the tank rounds obstacles one
// at a time and the proven single-sphere orbit does the actual walking.

namespace
{
    using Sphere = DcEngageGeometry::AvoidSphere;

    Sphere At(float x, float y, float r)
    {
        Sphere s;
        s.x = x;
        s.y = y;
        s.r = r;
        return s;
    }
}

TEST(DcEnRouteAvoidTest, NoSpheresMeansNoDetour)
{
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, {}), -1);
}

TEST(DcEnRouteAvoidTest, ClearCorridorMeansNoDetour)
{
    // Two packs well off the line: the walk passes nothing.
    std::vector<Sphere> const s{At(25.f, 40.f, 10.f), At(25.f, -40.f, 10.f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, s), -1);
}

TEST(DcEnRouteAvoidTest, PicksTheViolatorNearestTheBot)
{
    // Three packs sitting ON the line at 10, 25 and 40 yards out. The tank must
    // round the FIRST one it would reach — rounding the far one first would walk
    // it straight through the near one.
    std::vector<Sphere> const s{At(40.f, 0.f, 8.f), At(10.f, 0.f, 8.f), At(25.f, 0.f, 8.f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, s), 1);
}

TEST(DcEnRouteAvoidTest, IgnoresClearSpheresWhenPickingTheNearest)
{
    // The nearest pack (index 0) is off the line; the violator further out is the
    // one to round. "Nearest" is nearest VIOLATOR, not nearest pack.
    std::vector<Sphere> const s{At(5.f, 30.f, 8.f), At(30.f, 0.f, 8.f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, s), 1);
}

TEST(DcEnRouteAvoidTest, DegenerateRadiiAreSkipped)
{
    // A zero/negative radius is "no sphere" (a mob whose aggro range resolved to
    // nothing), not "a sphere at the origin the tank must orbit".
    std::vector<Sphere> const s{At(25.f, 0.f, 0.f), At(25.f, 0.f, -5.f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, s), -1);
}

TEST(DcEnRouteAvoidTest, PackTheTankIsStandingInIsStillRounded)
{
    // Recovery case, inherited from NeedsRoomAggroSkirt: the tank has drifted
    // inside a bystander's aggro sphere. It must be told to leave, not to read
    // "already inside, nothing to avoid" and carry on through.
    std::vector<Sphere> const s{At(3.f, 0.f, 10.f)};
    EXPECT_EQ(DcEngageGeometry::FirstViolatedSphere(0.f, 0.f, 50.f, 0.f, s), 0);
}
