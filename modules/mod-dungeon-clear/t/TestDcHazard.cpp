/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <vector>
#include "Ai/Dungeon/DungeonClear/Data/DcHazardRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DcNavPenaltyRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"

// Pure tests for the hazard-emitter table and its geometry predicates. No map
// data or live game state required, so these run in every build.

namespace
{
    // The five Arcatraz Sentinel (20869) spawns on map 552, from the `creature`
    // table. Kept here so the route-penalty cases below assert against the real
    // coordinates rather than restating the boxes.
    constexpr float kSentinelA[3] = { 255.498f, 158.914f, 22.362f };
    constexpr float kSentinelB[3] = { 253.942f, 131.881f, 22.395f };
    constexpr float kSentinelC[3] = { 264.287f, -61.321f, 22.453f };
    constexpr float kSentinelD[3] = { 336.514f,  27.427f, 48.426f };
    constexpr float kSentinelE[3] = { 395.413f,  18.195f, 48.296f };
}

TEST(DcHazardRegistry, ReportsMapsWithEmitters)
{
    EXPECT_TRUE(DcHazardRegistry::HasEmitters(552));    // The Arcatraz
    EXPECT_FALSE(DcHazardRegistry::HasEmitters(0));
    EXPECT_FALSE(DcHazardRegistry::HasEmitters(554));   // The Mechanar — no rows

    // Scholomance carries a GROUND pool and no creature emitter, so the
    // creature-only probe must say no for it...
    EXPECT_FALSE(DcHazardRegistry::HasEmitters(289));
    EXPECT_TRUE(DcHazardRegistry::HasGroundHazards(289));
    EXPECT_FALSE(DcHazardRegistry::HasGroundHazards(552));

    // ...and the combined probe — the one every live predicate and the vacate
    // trigger actually gate on — must say yes for both maps. Gating on
    // HasEmitters alone is what would make the retreat inert in Scholomance.
    EXPECT_TRUE(DcHazardRegistry::HasAnyHazard(289));
    EXPECT_TRUE(DcHazardRegistry::HasAnyHazard(552));
    EXPECT_FALSE(DcHazardRegistry::HasAnyHazard(554));
    EXPECT_FALSE(DcHazardRegistry::HasAnyHazard(0));

    // Maraudon is the first map to carry BOTH kinds at once — the Creeping
    // Sludge's permanent Poison Shock sphere and the Noxious Cloud pool both
    // slimes drop.
    EXPECT_TRUE(DcHazardRegistry::HasEmitters(349));
    EXPECT_TRUE(DcHazardRegistry::HasGroundHazards(349));
    EXPECT_TRUE(DcHazardRegistry::HasAnyHazard(349));

    // The Shattered Halls is the mirror image of Scholomance for the THIRD kind:
    // its only hazard is a gameobject trap (the flame-gauntlet Blaze), so both
    // the creature probe and the ground-pool probe must say no for it while the
    // combined probe — the one the vacate trigger gates on — says yes.
    EXPECT_FALSE(DcHazardRegistry::HasEmitters(540));
    EXPECT_FALSE(DcHazardRegistry::HasGroundHazards(540));
    EXPECT_TRUE(DcHazardRegistry::HasTrapHazards(540));
    EXPECT_TRUE(DcHazardRegistry::HasAnyHazard(540));

    // ...and no other map carries a trap row today.
    EXPECT_FALSE(DcHazardRegistry::HasTrapHazards(289));
    EXPECT_FALSE(DcHazardRegistry::HasTrapHazards(349));
    EXPECT_FALSE(DcHazardRegistry::HasTrapHazards(552));
    EXPECT_FALSE(DcHazardRegistry::HasTrapHazards(0));
}

TEST(DcHazardShatteredHallsTest, BlazeIsKeyedOnBothMapAndGameObjectEntry)
{
    DcTrapHazard const* blaze = DcHazardRegistry::FindTrap(540, 181915);
    ASSERT_NE(blaze, nullptr);
    EXPECT_EQ(blaze->mapId, 540u);
    EXPECT_EQ(blaze->goEntry, 181915u);

    // The retreat flees the CAST spell's radius (30979 "Flames", 3.0yd from
    // Spell.dbc EffectRadiusIndex 15), not the trap's 2yd trigger circle: a bot
    // standing 2.8yd off still eats the splash when the melee on top of the
    // Blaze sets it off.
    EXPECT_FLOAT_EQ(blaze->vacateRadius, 3.5f);
    // ...and the padded keep-out drives camp/standoff placement.
    EXPECT_FLOAT_EQ(blaze->radius, 5.0f);

    EXPECT_EQ(DcHazardRegistry::FindTrap(540, 181914), nullptr);  // right map, wrong GO
    EXPECT_EQ(DcHazardRegistry::FindTrap(289, 181915), nullptr);  // right GO, wrong map
}

TEST(DcHazardShatteredHallsTest, EveryTrapIsActivelyVacatedAndOvershootsItsHoldBand)
{
    // Same two invariants the ground pools carry, for the same reasons: a trap
    // cannot be fought (there is no unit to target), so a row with no
    // vacateRadius would be avoided during placement and then stood in anyway;
    // and retreatSlack <= holdBand would land the retreat still in danger and
    // thrash. Written as a loop over TrapEntries so a new row cannot slip in
    // without satisfying both.
    std::vector<uint32> const entries = DcHazardRegistry::TrapEntries(540);
    ASSERT_FALSE(entries.empty());
    for (uint32 entry : entries)
    {
        DcTrapHazard const* t = DcHazardRegistry::FindTrap(540, entry);
        ASSERT_NE(t, nullptr);
        EXPECT_GT(t->vacateRadius, 0.0f);
        EXPECT_GT(t->retreatSlack, t->holdBand);

        // The retreat aims vacateRadius + retreatSlack; that point must read
        // clean against this row's OWN placement cylinder or the vacate action
        // rejects every candidate it generates.
        float const aim = t->vacateRadius + t->retreatSlack;
        EXPECT_GT(aim, t->radius);
        EXPECT_FALSE(DcHazardRegistry::PointInside(*t, 0.0f, 0.0f, 0.0f, aim, 0.0f, 0.0f));
        // Standing on the fire does not.
        EXPECT_TRUE(DcHazardRegistry::PointInside(*t, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    }
}

TEST(DcHazardShatteredHallsTest, TrapGeometryUsesTheSamePrimitives)
{
    DcTrapHazard t{540, 181915, /*radius*/ 5.0f, /*zBand*/ 6.0f, /*vacate*/ 3.5f};

    // Inside the keep-out, and just clear of it.
    EXPECT_TRUE(DcHazardRegistry::PointInside(t, 0.0f, 0.0f, 0.0f, 4.5f, 0.0f, 0.0f));
    EXPECT_FALSE(DcHazardRegistry::PointInside(t, 0.0f, 0.0f, 0.0f, 5.5f, 0.0f, 0.0f));

    // The gauntlet corridor sits at z~2 and Nethekurse's chamber at z~-8, ten
    // yards below it: fire up here must not sterilise the route down there.
    EXPECT_FALSE(DcHazardRegistry::PointInside(t, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, -8.0f));

    // A leg whose endpoints are both clear but which walks straight over the
    // Blaze — the case a point-only check misses, and the common one here since
    // the fire lands between the party and the next bound.
    EXPECT_TRUE(DcHazardRegistry::SegmentClips(t, 0.0f, 0.0f, 0.0f,
                                               -30.0f, 0.0f, 0.0f,
                                                30.0f, 0.0f, 0.0f));
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(t, 0.0f, 0.0f, 0.0f,
                                                -30.0f, 20.0f, 0.0f,
                                                 30.0f, 20.0f, 0.0f));
}

TEST(DcHazardShatteredHallsTest, TrapsHaveNoNavPenaltyBoxes)
{
    // A Blaze's position is not known until an archer's arrow picks one of the
    // 20 wandering Flame Arrow anchors, so — exactly like the ground pools —
    // there is nothing to hand-author for the worker-thread router, and the live
    // predicates plus the retreat are the whole defence. If someone adds a
    // volume to map 540 they have either guessed at a dynamic position or they
    // are fencing something unrelated to the fire; either way this test is where
    // they have to argue for it.
    EXPECT_FALSE(DcNavPenaltyRegistry::HasVolumes(540));
}

TEST(DcHazardShatteredHallsTest, TrapEntriesIsMapScoped)
{
    // The live value sweeps BY ENTRY rather than sweeping every gameobject in
    // sight and filtering, because a dungeon floor carries hundreds of doors and
    // torches. That only works if the accessor is honestly map-scoped.
    std::vector<uint32> const onMap = DcHazardRegistry::TrapEntries(540);
    ASSERT_EQ(onMap.size(), 1u);
    EXPECT_EQ(onMap.front(), 181915u);

    EXPECT_TRUE(DcHazardRegistry::TrapEntries(289).empty());
    EXPECT_TRUE(DcHazardRegistry::TrapEntries(0).empty());
}

TEST(DcHazardRegistry, FindIsKeyedOnBothMapAndEntry)
{
    DcHazardEmitter const* sentinel = DcHazardRegistry::Find(552, 20869);
    ASSERT_NE(sentinel, nullptr);
    EXPECT_EQ(sentinel->mapId, 552u);
    EXPECT_EQ(sentinel->creatureEntry, 20869u);
    // 15yd Energy Discharge (36717) + 7yd drift margin.
    EXPECT_FLOAT_EQ(sentinel->radius, 22.0f);

    // The live Sentinel is fought, not vacated — no active-vacate radius.
    EXPECT_FLOAT_EQ(sentinel->vacateRadius, 0.0f);

    // The Destroyed Sentinel (21761) — the summon the party must flee — carries
    // the 15yd vacate pulse, and its camp keep-out radius is the RAW pulse (15),
    // not the padded 22, so a 19yd retreat point clears PointIsHot.
    DcHazardEmitter const* destroyed = DcHazardRegistry::Find(552, 21761);
    ASSERT_NE(destroyed, nullptr);
    EXPECT_FLOAT_EQ(destroyed->vacateRadius, 15.0f);
    EXPECT_FLOAT_EQ(destroyed->radius, 15.0f);

    DcHazardEmitter const* corpse = DcHazardRegistry::Find(552, 21303);
    ASSERT_NE(corpse, nullptr);
    // 8yd SmartAI OOC-LOS trigger + 4yd margin.
    EXPECT_FLOAT_EQ(corpse->radius, 12.0f);
    EXPECT_FLOAT_EQ(corpse->vacateRadius, 0.0f);

    EXPECT_EQ(DcHazardRegistry::Find(552, 99999), nullptr);   // right map, wrong entry
    EXPECT_EQ(DcHazardRegistry::Find(0, 20869), nullptr);     // right entry, wrong map
}

// ---- the ground-pool half -----------------------------------------------
// Scholomance's "Cloud of Disease" (17742): a persistent area aura, i.e. a
// DynamicObject and not a creature, dropped where a Diseased Ghoul (10495) dies.
// 350 nature damage per second in 5yd for 20s.

TEST(DcHazardRegistry, FindGroundIsKeyedOnBothMapAndSpell)
{
    DcGroundHazard const* cloud = DcHazardRegistry::FindGround(289, 17742);
    ASSERT_NE(cloud, nullptr);
    EXPECT_EQ(cloud->mapId, 289u);
    EXPECT_EQ(cloud->spellId, 17742u);

    // The RAW 5yd aura radius drives the retreat...
    EXPECT_FLOAT_EQ(cloud->vacateRadius, 5.0f);
    // ...and the padded keep-out drives camp/standoff placement.
    EXPECT_FLOAT_EQ(cloud->radius, 8.0f);

    EXPECT_EQ(DcHazardRegistry::FindGround(289, 29047), nullptr);  // right map, sibling spell id
    EXPECT_EQ(DcHazardRegistry::FindGround(552, 17742), nullptr);  // right spell, wrong map
}

TEST(DcHazardRegistry, EveryGroundPoolIsActivelyVacated)
{
    // A ground pool cannot be fought — there is no unit to target — so a row with
    // no vacateRadius would be avoided during placement and then stood in anyway
    // the moment a ghoul died under the party. Guard the invariant: every ground
    // row drives the retreat.
    DcGroundHazard const* cloud = DcHazardRegistry::FindGround(289, 17742);
    ASSERT_NE(cloud, nullptr);
    EXPECT_GT(cloud->vacateRadius, 0.0f);

    DcGroundHazard const* noxious = DcHazardRegistry::FindGround(349, 21070);
    ASSERT_NE(noxious, nullptr);
    EXPECT_GT(noxious->vacateRadius, 0.0f);
}

TEST(DcHazardRegistry, GroundPoolRetreatPointClearsItsOwnKeepOut)
{
    // The retreat aims vacateRadius + the row's own retreatSlack. If that lands
    // INSIDE the row's own PointIsHot cylinder, the vacate action rejects every
    // candidate it generates and falls through to its unvalidated last resort.
    // This is the exact trap the Destroyed Sentinel row's comment warns about.
    for (DcGroundHazard const* pool : { DcHazardRegistry::FindGround(289, 17742),
                                        DcHazardRegistry::FindGround(349, 21070),
                                        DcHazardRegistry::FindGround(601, 53400),
                                        DcHazardRegistry::FindGround(601, 59419) })
    {
        ASSERT_NE(pool, nullptr);

        float const aim = pool->vacateRadius + pool->retreatSlack;
        EXPECT_GT(aim, pool->radius);

        // And the aim point really does read clean against the row's geometry.
        EXPECT_FALSE(DcHazardRegistry::PointInside(*pool, 0.0f, 0.0f, 0.0f, aim, 0.0f, 0.0f));
        // The pool centre, where the mob died and the party is standing, does not.
        EXPECT_TRUE(DcHazardRegistry::PointInside(*pool, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    }
}

TEST(DcHazardRegistry, GroundPoolGeometryUsesTheSamePrimitives)
{
    DcGroundHazard g{289, 17742, /*radius*/ 8.0f, /*zBand*/ 6.0f, /*vacate*/ 5.0f};

    // Inside the keep-out, and just clear of it.
    EXPECT_TRUE(DcHazardRegistry::PointInside(g, 0.0f, 0.0f, 0.0f, 7.5f, 0.0f, 0.0f));
    EXPECT_FALSE(DcHazardRegistry::PointInside(g, 0.0f, 0.0f, 0.0f, 8.5f, 0.0f, 0.0f));

    // Scholomance stacks rooms: a pool on the floor below is not a hazard.
    EXPECT_FALSE(DcHazardRegistry::PointInside(g, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 20.0f));

    // A leg whose endpoints are both clear but which walks straight through the
    // pool — the case a point-only check misses.
    EXPECT_TRUE(DcHazardRegistry::SegmentClips(g, 0.0f, 0.0f, 0.0f,
                                               -30.0f, 0.0f, 0.0f,
                                                30.0f, 0.0f, 0.0f));
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(g, 0.0f, 0.0f, 0.0f,
                                                -30.0f, 20.0f, 0.0f,
                                                 30.0f, 20.0f, 0.0f));
}

TEST(DcHazardRegistry, GroundPoolsHaveNoNavPenaltyBoxes)
{
    // A pool's position is not known until a ghoul dies on it, so there is
    // nothing to hand-author for the worker-thread router — unlike the rooted
    // Arcatraz Sentinels. Scholomance must therefore carry no hazard boxes; the
    // live predicates plus the retreat are the whole defence. If someone ever
    // adds a box here they have guessed at a dynamic position.
    EXPECT_FALSE(DcNavPenaltyRegistry::HasVolumes(289));

    // Maraudon has the same prohibition for BOTH of its rows: the Noxious Cloud
    // pool for the reason above, and the Creeping Sludge because — unlike the
    // rooted dormant Sentinels — every one of its 24 spawns wanders
    // (MovementType != 0, wander_distance 1-5), so there is no author-time
    // position to box either.
    EXPECT_FALSE(DcNavPenaltyRegistry::HasVolumes(349));
}

TEST(DcHazardRegistry, PointInsideRespectsRadius)
{
    DcHazardEmitter e{552, 20869, /*radius*/ 22.0f, /*zBand*/ 12.0f};

    // Dead centre, and just inside the rim.
    EXPECT_TRUE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 0.0f, 21.5f, 0.0f, 0.0f));

    // Just outside the rim, and comfortably clear.
    EXPECT_FALSE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 0.0f, 22.5f, 0.0f, 0.0f));
    EXPECT_FALSE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 0.0f, 60.0f, 60.0f, 0.0f));
}

TEST(DcHazardRegistry, PointInsideRespectsZBand)
{
    DcHazardEmitter e{552, 20869, /*radius*/ 22.0f, /*zBand*/ 12.0f};

    // Directly overhead but on the floor above — the pulse does not reach, and
    // sterilising the catwalk above a Sentinel would be a real routing loss.
    EXPECT_FALSE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 22.4f, 0.0f, 0.0f, 48.4f));
    // Within the band.
    EXPECT_TRUE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 22.4f, 0.0f, 0.0f, 30.0f));
}

TEST(DcHazardRegistry, SegmentClipsCatchesAPassingLeg)
{
    DcHazardEmitter e{552, 20869, /*radius*/ 22.0f, /*zBand*/ 12.0f};

    // A leg whose ENDPOINTS are both well clear but which passes straight
    // through the emitter. This is the case a point-only test misses, and the
    // reason camp validation walks the polyline rather than checking the anchor.
    EXPECT_TRUE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 0.0f,
                                               -50.0f, 0.0f, 0.0f,
                                                50.0f, 0.0f, 0.0f));

    // Same span, offset far enough sideways to miss the circle.
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 0.0f,
                                                -50.0f, 40.0f, 0.0f,
                                                 50.0f, 40.0f, 0.0f));

    // Passes overhead on the floor above — both endpoints out of band.
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 22.4f,
                                                -50.0f, 0.0f, 48.4f,
                                                 50.0f, 0.0f, 48.4f));

    // Climbing past the emitter: the far end is out of band but the near end is
    // inside it, so the leg still clips and must be rejected.
    EXPECT_TRUE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 22.4f,
                                               -10.0f, 0.0f, 24.0f,
                                                50.0f, 0.0f, 48.4f));
}

TEST(DcHazardRegistry, SegmentClipsCatchesAZBandStraddle)
{
    DcHazardEmitter e{552, 20869, /*radius*/ 22.0f, /*zBand*/ 12.0f};

    // BOTH endpoints out of band, but on OPPOSITE sides — the leg descends
    // straight through the emitter's z. A naive "both out => clean" test waves
    // this through. Real geometry: Arcatraz has a z48 upper tier, the z22 floor
    // the Sentinels sit on, and Zereketh's z-10 chamber below.
    EXPECT_TRUE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 22.4f,
                                               0.0f, 0.0f, 48.0f,
                                               0.0f, 0.0f, -10.0f));

    // Same straddle, but offset far enough sideways to miss the circle entirely.
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 22.4f,
                                                40.0f, 0.0f, 48.0f,
                                                40.0f, 0.0f, -10.0f));

    // Both out of band on the SAME side is still clean — this is the floor-above
    // case, and rejecting it would sterilise the catwalk over an emitter.
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 22.4f,
                                                -50.0f, 0.0f, 48.0f,
                                                 50.0f, 0.0f, 50.0f));
}

TEST(DcHazardRegistry, ZeroRadiusEmitterIsInert)
{
    DcHazardEmitter e{552, 20869, /*radius*/ 0.0f, /*zBand*/ 12.0f};
    EXPECT_FALSE(DcHazardRegistry::PointInside(e, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_FALSE(DcHazardRegistry::SegmentClips(e, 0.0f, 0.0f, 0.0f,
                                                -50.0f, 0.0f, 0.0f,
                                                 50.0f, 0.0f, 0.0f));
}

// ---- the route half -----------------------------------------------------
// The nav-penalty boxes that keep the long-range router off the Sentinels.
// These live in DcNavPenaltyRegistry but are authored as part of the hazard
// feature, so they are asserted here alongside it.

TEST(DcHazardRegistry, PenalizesEverySentinelSpawn)
{
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(552, kSentinelA[0], kSentinelA[1], kSentinelA[2]), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(552, kSentinelB[0], kSentinelB[1], kSentinelB[2]), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(552, kSentinelC[0], kSentinelC[1], kSentinelC[2]), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(552, kSentinelD[0], kSentinelD[1], kSentinelD[2]), 1.0f);
    EXPECT_GT(DcNavPenaltyRegistry::PenaltyAt(552, kSentinelE[0], kSentinelE[1], kSentinelE[2]), 1.0f);
}

TEST(DcHazardRegistry, SentinelBoxesStayOffTheRestOfTheInstance)
{
    // Mellichar's arena floor centroid — the objective anchor the finale parks
    // the party on. An over-wide box here would tax the one spot the run MUST
    // stand on, so this is the regression guard on box size.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 445.9f, -161.5f, 42.56f), 1.0f);

    // The three walk-in bosses.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 273.607f, -122.980f, -10.040f), 1.0f);  // Zereketh
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 137.234f,  128.506f,  22.5245f), 1.0f); // Dalliah
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 136.200f,  168.310f,  22.5245f), 1.0f); // Soccothrates

    // The two Containment Core security fields — the party has to stand on the
    // door line to walk through when they open.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 199.827f, 117.488f, 23.877f), 1.0f);
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(552, 199.911f, 102.009f, 23.694f), 1.0f);

    // Same coordinates, different map => no volume applies.
    EXPECT_FLOAT_EQ(DcNavPenaltyRegistry::PenaltyAt(554, kSentinelA[0], kSentinelA[1], kSentinelA[2]), 1.0f);
}

TEST(DcHazardRegistry, SentinelBoxesDoNotOutrankTheNavmeshShortcutRows)
{
    // A hazard is a "prefer not to" — a navmesh shortcut a player cannot follow
    // is a "this route is wrong". If the two ever tie, a mandatory hazard
    // corridor starts looking as bad as a broken climb and routing gets worse,
    // not better. Keep the hazard tax strictly cheaper.
    float const hazard = DcNavPenaltyRegistry::PenaltyAt(552, kSentinelA[0], kSentinelA[1], kSentinelA[2]);
    float const shortcut = DcNavPenaltyRegistry::PenaltyAt(229, -126.1f, -390.3f, 44.4f);
    EXPECT_LT(hazard, shortcut);
}

// ===== The Eredar room's 45yd auras are cleared, not avoided =====
//
// The three multispawn points roll Eredar Soul-Eater (20879, a harmless 45yd
// slow) or Eredar Deathbringer (20880, a 45yd 450/750-per-2s damage pulse).
// Avoidance is the wrong tool at that width: sized honestly it refuses every
// route through the wing, and sized below caster range it is a lie — at 30yd you
// still take full damage. The party crosses and kills them instead
// (ArcatrazEvents.cpp event 2). These guard against the registry re-growing a
// keep-out row and re-introducing the pathing deadlock.

TEST(DcHazardArcatrazTest, EredarRoomIsNotRegisteredAsEmitters)
{
    EXPECT_EQ(DcHazardRegistry::Find(552, 20879), nullptr);
    EXPECT_EQ(DcHazardRegistry::Find(552, 20880), nullptr);
    // Heroic templates can never be GetEntry(), so a row for either would be dead
    // either way — assert it stays absent so nobody "fixes" the above by adding
    // these instead.
    EXPECT_EQ(DcHazardRegistry::Find(552, 21595), nullptr);
    EXPECT_EQ(DcHazardRegistry::Find(552, 21594), nullptr);
}

// ===== Maraudon's slimes =====
//
// The instance's biggest source of wipes, and the first map to need both emitter
// kinds at once:
//
//   * Creeping Sludge (12222) carries the PERMANENT addon aura 22638 "Poison
//     Shock" — PERIODIC_TRIGGER_SPELL every 2000ms firing 22595 for 181-221 nature
//     in 5.0yd, ticking idle as well as in combat. It is a creature row.
//   * Both slimes drop 21070 "Noxious Cloud" — a PERSISTENT_AREA_AURA, 151 nature
//     per second in 5.0yd for 20s — in combat AND on death. It is a pool row, and
//     one row covers both casters because the key is the spell.

TEST(DcHazardMaraudonTest, CreepingSludgeIsFledAndStayedAwayFrom)
{
    DcHazardEmitter const* sludge = DcHazardRegistry::Find(349, 12222);
    ASSERT_NE(sludge, nullptr);
    EXPECT_EQ(sludge->mapId, 349u);
    EXPECT_EQ(sludge->creatureEntry, 12222u);
    // 5yd Poison Shock (22595) + 3yd drift margin for placement...
    EXPECT_FLOAT_EQ(sludge->radius, 8.0f);
    // ...and the RAW 5yd pulse for the retreat.
    EXPECT_FLOAT_EQ(sludge->vacateRadius, 5.0f);

    // The invariant that matters, and the one tr-20260815-134844-3/-5 was lost to
    // when this row carried vacateRadius 0. Melee reach is 3D < 4.75yd against a
    // 5.0yd pulse, so "in melee" and "in the aura" are the same place: there is no
    // stance from which a melee bot trades with this mob for free. It moves at
    // 2.0 yd/s, so nobody has to — the party leaves it standing and shoots it.
    //
    // holdBand must therefore be WIDER than melee reach, or the bot's own
    // MoveChase simply walks it back in and it oscillates through the aura.
    EXPECT_GT(sludge->holdBand, 4.75f);
    EXPECT_FLOAT_EQ(sludge->holdBand, 6.0f);
    EXPECT_FLOAT_EQ(sludge->retreatSlack, 9.0f);
}

TEST(DcHazardMaraudonTest, SludgeRetreatOvershootsItsOwnHoldBand)
{
    // The retreat must land somewhere the trigger will NOT re-fire, or the bot
    // flees on arrival and never stops. That means aim > hold, with margin, and the
    // aim point also has to clear the placement keep-out the action screens against.
    DcHazardEmitter const* sludge = DcHazardRegistry::Find(349, 12222);
    ASSERT_NE(sludge, nullptr);

    float const hold = sludge->vacateRadius + sludge->holdBand;      // 11 — still in danger
    float const aim  = sludge->vacateRadius + sludge->retreatSlack;  // 14 — where it runs to
    EXPECT_GT(aim, hold);
    EXPECT_GT(aim, sludge->radius);
    EXPECT_GE(aim - hold, 2.0f);  // arrival margin, so a yard of snap-back is survivable
}

TEST(DcHazardMaraudonTest, EveryVacateRowOvershootsItsHoldBand)
{
    // Same invariant across the whole table, both kinds. A row with
    // retreatSlack <= holdBand retreats to a point that still reads in-danger:
    // the trigger re-fires, the action re-plots, and the bot thrashes in place
    // until something kills it.
    for (DcHazardEmitter const* e : { DcHazardRegistry::Find(552, 21761),
                                      DcHazardRegistry::Find(349, 12222),
                                      DcHazardRegistry::Find(574, 23997) })
    {
        ASSERT_NE(e, nullptr);
        ASSERT_GT(e->vacateRadius, 0.0f);
        EXPECT_GT(e->retreatSlack, e->holdBand);
    }

    for (DcGroundHazard const* g : { DcHazardRegistry::FindGround(289, 17742),
                                     DcHazardRegistry::FindGround(349, 21070),
                                     DcHazardRegistry::FindGround(601, 53400),
                                     DcHazardRegistry::FindGround(601, 59419) })
    {
        ASSERT_NE(g, nullptr);
        ASSERT_GT(g->vacateRadius, 0.0f);
        EXPECT_GT(g->retreatSlack, g->holdBand);
    }
}

TEST(DcHazardMaraudonTest, FoughtEmittersKeepTheThinDefaultBands)
{
    // Rows that are NOT actively fled must not have grown a hold band by copy-paste
    // — the bands only mean anything alongside a vacateRadius, and a stray wide one
    // here would read as intent that isn't there.
    for (DcHazardEmitter const* e : { DcHazardRegistry::Find(552, 20869),
                                      DcHazardRegistry::Find(552, 21303),
                                      DcHazardRegistry::Find(552, 21304) })
    {
        ASSERT_NE(e, nullptr);
        EXPECT_FLOAT_EQ(e->vacateRadius, 0.0f);
        EXPECT_FLOAT_EQ(e->holdBand, 2.0f);
        EXPECT_FLOAT_EQ(e->retreatSlack, 6.0f);
    }

    // And the Destroyed Sentinel keeps the THIN hold band on purpose: it is
    // unattackable, so once the party is past it nothing pulls anyone back and the
    // run should carry onward rather than be pinned at the rim.
    DcHazardEmitter const* destroyed = DcHazardRegistry::Find(552, 21761);
    ASSERT_NE(destroyed, nullptr);
    EXPECT_FLOAT_EQ(destroyed->holdBand, 2.0f);
}

TEST(DcHazardMaraudonTest, NoxiousSlimeIsNotACreatureEmitter)
{
    // 12221 "Noxious Slime" has a NULL creature_template_addon auras column — it
    // emits nothing on its own and runs at normal speed. Its whole threat is the
    // Noxious Cloud pool, which is keyed on the spell and so already covers it.
    // A creature row here would fence off a mob that is not emitting.
    EXPECT_EQ(DcHazardRegistry::Find(349, 12221), nullptr);
}

TEST(DcHazardMaraudonTest, NoxiousCloudCoversBothSlimesThroughOneSpellRow)
{
    DcGroundHazard const* cloud = DcHazardRegistry::FindGround(349, 21070);
    ASSERT_NE(cloud, nullptr);
    EXPECT_EQ(cloud->mapId, 349u);
    EXPECT_EQ(cloud->spellId, 21070u);

    // Same 5yd aura as Scholomance's Cloud of Disease, so the same 8/5 split and
    // the same 3yd gap to the 11yd retreat aim point.
    EXPECT_FLOAT_EQ(cloud->vacateRadius, 5.0f);
    EXPECT_FLOAT_EQ(cloud->radius, 8.0f);

    EXPECT_EQ(DcHazardRegistry::FindGround(349, 17742), nullptr);  // right map, Scholomance's spell
    EXPECT_EQ(DcHazardRegistry::FindGround(289, 21070), nullptr);  // right spell, wrong map
}

TEST(DcHazardMaraudonTest, SludgeSphereRejectsACampAndTheWalkToIt)
{
    // What the creature row actually buys: a camp planted inside the sphere is
    // rejected, and so is a clean camp whose drag-back walks the pack through one.
    DcHazardEmitter const* sludge = DcHazardRegistry::Find(349, 12222);
    ASSERT_NE(sludge, nullptr);

    EXPECT_TRUE(DcHazardRegistry::PointInside(*sludge, 0.0f, 0.0f, -50.0f, 6.0f, 0.0f, -50.0f));
    EXPECT_FALSE(DcHazardRegistry::PointInside(*sludge, 0.0f, 0.0f, -50.0f, 12.0f, 0.0f, -50.0f));

    // Maraudon stacks its wings — the Noxious Slime tier sits ~30yd below the
    // sludge tier — so a sludge on the floor below must not sterilise the walkway
    // above it.
    EXPECT_FALSE(DcHazardRegistry::PointInside(*sludge, 0.0f, 0.0f, -50.0f, 0.0f, 0.0f, -80.0f));

    // Both endpoints clear, the leg straight through: the case a point-only check
    // misses.
    EXPECT_TRUE(DcHazardRegistry::SegmentClips(*sludge, 0.0f, 0.0f, -50.0f,
                                               -30.0f, 0.0f, -50.0f,
                                                30.0f, 0.0f, -50.0f));
}

// ===== the retreat's detour bound =====
//
// "A path exists" cannot tell a point 6yd away from a point 6yd away THROUGH A
// WALL — both are PATHFIND_NORMAL, the second one just leaves the room and comes
// back. tr-20260815-154816-5 committed to one of those and walked it for 47
// seconds, carrying the tank ~60yd across the cavern with twelve sludges behind
// and wiping the party strung out over 100yd. These pin the numbers that stop it.

TEST(DcHazardVacateDetourTest, RejectsTheLongWayRoundAWall)
{
    // The forcing case: a Creeping Sludge retreat aims vacate+slack = 14yd, so a
    // candidate typically sits ~14yd out. Around a doorway or a pillar the real
    // walk is a few yards longer and must still be taken...
    float const bound = DcDetourBound(14.0f, DC_VACATE_DETOUR_RATIO, DC_VACATE_DETOUR_SLACK);
    EXPECT_GE(bound, 20.0f);
    EXPECT_GT(bound, 18.0f);   // a 4yd corner detour survives

    // ...but the way round a wall does not.
    EXPECT_LT(bound, 60.0f);
    EXPECT_LT(bound, 30.0f);
}

TEST(DcHazardVacateDetourTest, SlackCarriesTheShortRangeCase)
{
    // At close range a pure ratio is far too strict — a bot 2yd from its aim point
    // rounding any corner blows past 1.5x — so the slack term has to dominate
    // there, and the ratio only takes over further out.
    EXPECT_FLOAT_EQ(DcDetourBound(2.0f, DC_VACATE_DETOUR_RATIO, DC_VACATE_DETOUR_SLACK), 10.0f);
    EXPECT_GT(DcDetourBound(2.0f, DC_VACATE_DETOUR_RATIO, DC_VACATE_DETOUR_SLACK),
              2.0f * DC_VACATE_DETOUR_RATIO);

    // The crossover, and beyond it the ratio is the binding term.
    float const crossover = DC_VACATE_DETOUR_SLACK / (DC_VACATE_DETOUR_RATIO - 1.0f);
    EXPECT_FLOAT_EQ(crossover, 16.0f);
    EXPECT_FLOAT_EQ(DcDetourBound(40.0f, DC_VACATE_DETOUR_RATIO, DC_VACATE_DETOUR_SLACK), 60.0f);
}

TEST(DcHazardVacateDetourTest, IsStricterThanTheTrashTargetingGate)
{
    // The two gates are the same shape but not the same job. Trash targeting can
    // afford a long approach — the tank is going to walk there anyway. A retreat
    // cannot: its whole purpose is to open a few yards NOW, so anything but a
    // short hop defeats it. Guard against someone unifying the constants.
    EXPECT_LT(DC_VACATE_DETOUR_RATIO, DC_TRASH_DETOUR_RATIO);
    EXPECT_LT(DC_VACATE_DETOUR_SLACK, DC_TRASH_DETOUR_SLACK);
    EXPECT_LT(DcDetourBound(14.0f, DC_VACATE_DETOUR_RATIO, DC_VACATE_DETOUR_SLACK),
              DcDetourBound(14.0f, DC_TRASH_DETOUR_RATIO, DC_TRASH_DETOUR_SLACK));
}

TEST(DcHazardVacateDetourTest, CommitIsCappedShorterThanTheMarchItReplaced)
{
    // The commitment exists so the retreat stops re-plotting its spline every
    // tick, not so it can stop looking. 47s of unsupervised ride is what the
    // uncapped version produced; a valid bounded walk is a few seconds.
    EXPECT_LE(DC_VACATE_COMMIT_MAX_MS, 5000u);
    EXPECT_GE(DC_VACATE_COMMIT_MAX_MS, 1000u);
}

TEST(DcHazardArcatrazTest, RegisteredEmittersStillRejectLegs)
{
    // Every surviving emitter is a genuine route hazard: a leg through the
    // Sentinel's pulse must still be refused. Guards the SegmentClips path that
    // the removed loiter-only branch used to short-circuit.
    DcHazardEmitter const* e = DcHazardRegistry::Find(552, 20869);
    ASSERT_NE(e, nullptr);
    EXPECT_TRUE(DcHazardRegistry::SegmentClips(*e, 0, 0, 22, -40, 0, 22, 40, 0, 22));
}

// --- Utgarde Keep (574): Ingvar's thrown axe ------------------------------
// boss_ingvar_the_plunderer's phase-2 "Throw Axe" (42749) summons the Ingvar
// Throw Dummy (23997) at a RANDOM party member's feet. The dummy carries the
// permanent creature_template_addon aura 42750 (PERIODIC_TRIGGER_SPELL, 1000ms)
// firing 42751 — 1750-2250 shadow in 5yd — until the script despawns it ~10s
// later. It is UNIT_FLAG_NOT_SELECTABLE with NullCreatureAI, so there is nothing
// to target and nothing to interrupt: leaving is the only answer, which makes it
// a threat-2 emitter by construction.
TEST(DcHazardUtgardeKeepTest, IngvarThrowDummyIsAVacateEmitter)
{
    DcHazardEmitter const* axe = DcHazardRegistry::Find(574, 23997);
    ASSERT_NE(axe, nullptr) << "Ingvar's Throw Dummy (23997) is not registered";
    EXPECT_EQ(axe->mapId, 574u);
    EXPECT_EQ(axe->creatureEntry, 23997u);

    // It must be FLED, not merely avoided in placement — a vacateRadius of 0
    // would leave the party standing in ~2000 dps that nothing can be done about.
    EXPECT_FLOAT_EQ(axe->vacateRadius, 5.0f) << "the raw 42751 radius";

    // "Leave, then carry on" bands, NOT Maraudon's wide stay-out pair: the party
    // is mid-encounter with a boss it must keep tanking and the dummy deletes
    // itself in ~10s, so a wide hold band would walk the melee off Ingvar.
    EXPECT_FLOAT_EQ(axe->holdBand, 2.0f);
    EXPECT_FLOAT_EQ(axe->retreatSlack, 6.0f);

    // The retreat's aim point must clear this row's own placement radius, or
    // PointIsHot rejects the landing spot and the bot re-plots forever.
    float const aim = axe->vacateRadius + axe->retreatSlack;
    EXPECT_GT(aim, axe->radius);
    EXPECT_GE(aim - (axe->vacateRadius + axe->holdBand), 2.0f)
        << "arrival margin, so a yard of snap-back is survivable";

    // Modest placement radius on purpose: the axe lands on the floor the party is
    // actively fighting on, so an over-wide keep-out sterilises Ingvar's arena.
    EXPECT_LE(axe->radius, 8.0f);
    EXPECT_GE(axe->radius, axe->vacateRadius);

    EXPECT_TRUE(DcHazardRegistry::HasEmitters(574));
    EXPECT_TRUE(DcHazardRegistry::HasAnyHazard(574));
    // Utgarde Keep has no ground pool and no trap — the axe is a creature.
    EXPECT_FALSE(DcHazardRegistry::HasGroundHazards(574));
    EXPECT_FALSE(DcHazardRegistry::HasTrapHazards(574));
}

// --- Azjol-Nerub: Hadronox's Acid Cloud -----------------------------------
// A PERSISTENT_AREA_AURA pool like Scholomance's, but with the two properties
// that make it the worst one on the clear: it lasts NINETY seconds (Spell.dbc
// DurationIndex 23) at 707 nature/sec normal and 1414 heroic, and it is cast at
// a RANDOM party member inside 100yd rather than under the boss — so it lands on
// top of somebody and then outlives the fight it was cast in.

TEST(DcHazardRegistry, AzjolNerubRegistersAcidCloudOnBothDifficulties)
{
    EXPECT_TRUE(DcHazardRegistry::HasAnyHazard(601));
    EXPECT_TRUE(DcHazardRegistry::HasGroundHazards(601));
    // No creature emitters and no traps on this map — the gate has to be
    // HasAnyHazard, never HasEmitters, or the vacate is inert here.
    EXPECT_FALSE(DcHazardRegistry::HasEmitters(601));

    // boss_hadronox only ever CASTS 53400; spelldifficulty_dbc maps it to 59419
    // on heroic and the DynamicObject then reports 59419 from GetSpellId(). A row
    // for 53400 alone leaves the retreat inert on the difficulty where the pool
    // does double damage.
    DcGroundHazard const* normal = DcHazardRegistry::FindGround(601, 53400);
    DcGroundHazard const* heroic = DcHazardRegistry::FindGround(601, 59419);
    ASSERT_NE(normal, nullptr) << "Acid Cloud (normal) must be registered";
    ASSERT_NE(heroic, nullptr) << "Acid Cloud (heroic 59419) must be registered too";

    for (DcGroundHazard const* g : { normal, heroic })
    {
        EXPECT_EQ(g->mapId, 601u);
        // vacateRadius is the RAW 5yd aura, not the padded keep-out — otherwise
        // the retreat's own aim point fails PointIsHot and every candidate is
        // rejected (the Destroyed Sentinel trap).
        EXPECT_FLOAT_EQ(g->vacateRadius, 5.0f);
        EXPECT_GT(g->radius, g->vacateRadius);
        // Azjol-Nerub is a vertical shaft: the platform (z ~733), Hadronox's
        // ledge (z ~675), the pit floor (z ~648) and the lower kingdom (z ~289)
        // stack in the same column, and the tightest gap is 27yd. The z band must
        // stay well inside that so a pool on one deck cannot fence off the next.
        EXPECT_LT(g->zBand, 27.0f);
    }
}

