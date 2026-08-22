/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcRegroupDecision.h"

using DcRegroupDecision::DecideCombatRegroup;
using DcRegroupDecision::RegroupInputs;
using DcRegroupDecision::RegroupVerdict;

namespace
{
    // A "healthy" baseline: DPS in a normal fight with a visible attacker, tank in
    // LOS and well inside the tether. Verdict None. Individual tests flip one field.
    RegroupInputs BaseDps()
    {
        RegroupInputs in;
        in.isHealer = false;
        in.isMelee = true;
        in.casting = false;
        in.ccd = false;
        in.hasVisibleAttacker = true;
        in.hasHurtHealTarget = false;
        in.tankLos = true;
        in.tankDist2d = 10.0f;
        in.healRange = 30.0f;
        in.hardTether = 40.0f;
        in.slack = 8.0f;
        return in;
    }

    RegroupInputs BaseHealer()
    {
        RegroupInputs in = BaseDps();
        in.isHealer = true;
        in.isMelee = false;
        in.hasVisibleAttacker = false;  // healer role doesn't read attackers
        return in;
    }
}

// ---- short-circuits: casting / CC'd always None, even past the tether ----------

TEST(DcCombatRegroupTest, CastingIsNone)
{
    RegroupInputs in = BaseDps();
    in.hasVisibleAttacker = false;  // would otherwise Reconnect
    in.casting = true;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::None);
}

TEST(DcCombatRegroupTest, CcdIsNone)
{
    RegroupInputs in = BaseDps();
    in.hasVisibleAttacker = false;  // would otherwise Reconnect
    in.ccd = true;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::None);
}

TEST(DcCombatRegroupTest, CastingBeatsHardTether)
{
    // Casting wins even over the outer tether — never clip a cast.
    RegroupInputs in = BaseDps();
    in.casting = true;
    in.tankDist2d = 100.0f;  // way past the tether
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::None);
}

TEST(DcCombatRegroupTest, CcdBeatsHardTether)
{
    RegroupInputs in = BaseHealer();
    in.ccd = true;
    in.tankDist2d = 100.0f;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::None);
}

// ---- hard tether precedence over the DPS / healer clauses ----------------------

TEST(DcCombatRegroupTest, HardTetherForDpsWithVisibleAttacker)
{
    // A DPS that "can contribute" (visible attacker) still reconnects once it drifts
    // past the tether — the drifted-into-nowhere safety net.
    RegroupInputs in = BaseDps();
    in.hasVisibleAttacker = true;
    in.tankDist2d = 41.0f;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::HardTether);
}

TEST(DcCombatRegroupTest, HardTetherForHealerWithHurtTarget)
{
    // A healer with a hurt heal target (normally None — HealReposition owns it)
    // still hard-tethers past the outer distance.
    RegroupInputs in = BaseHealer();
    in.hasHurtHealTarget = true;
    in.tankDist2d = 55.0f;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::HardTether);
}

TEST(DcCombatRegroupTest, ExactlyAtTetherIsNotHardTether)
{
    // Boundary: strictly greater than the tether fires it; equal does not.
    RegroupInputs in = BaseDps();
    in.tankDist2d = 40.0f;  // == hardTether
    EXPECT_NE(DecideCombatRegroup(in), RegroupVerdict::HardTether);
}

// ---- DPS clause: attacker visibility is the whole test -------------------------

TEST(DcCombatRegroupTest, DpsVisibleAttackerIsNone)
{
    EXPECT_EQ(DecideCombatRegroup(BaseDps()), RegroupVerdict::None);
}

TEST(DcCombatRegroupTest, DpsNoAttackerReconnects)
{
    RegroupInputs in = BaseDps();
    in.hasVisibleAttacker = false;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::Reconnect);
}

TEST(DcCombatRegroupTest, RangedDpsNoAttackerReconnects)
{
    // Ranged DPS uses the same clause; role (melee vs ranged) never changes the
    // verdict, only the action's standoff radius.
    RegroupInputs in = BaseDps();
    in.isMelee = false;
    in.hasVisibleAttacker = false;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::Reconnect);
}

TEST(DcCombatRegroupTest, MeleeChasingRunnerStaysNone)
{
    // A melee chasing a fleeing mob keeps a visible attacker the whole chase, even
    // dragged 30yd out — None until the hard tether. No yank-off-the-kill.
    RegroupInputs in = BaseDps();
    in.hasVisibleAttacker = true;
    in.tankDist2d = 30.0f;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::None);
}

// A DPS never consults the heal-target / LOS / heal-range fields.
TEST(DcCombatRegroupTest, DpsIgnoresHealerFields)
{
    RegroupInputs in = BaseDps();
    in.hasVisibleAttacker = true;
    in.hasHurtHealTarget = true;   // irrelevant to DPS
    in.tankLos = false;            // irrelevant to DPS
    in.tankDist2d = 39.0f;         // still inside tether
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::None);
}

// ---- healer clause: HealReposition ownership + pre-positioning ------------------

// The HealReposition ownership invariant: a healer NEVER reconnects while it has a
// hurt heal target, no matter how bad LOS / range look (short of the hard tether).
TEST(DcCombatRegroupTest, HealerWithHurtTargetNeverReconnects)
{
    RegroupInputs in = BaseHealer();
    in.hasHurtHealTarget = true;
    in.tankLos = false;
    in.tankDist2d = 39.0f;  // inside tether
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::None);
}

TEST(DcCombatRegroupTest, HealerPreposNoLosReconnects)
{
    // Nobody hurt yet, healer parked out of LOS of the tank => pre-position.
    RegroupInputs in = BaseHealer();
    in.hasHurtHealTarget = false;
    in.tankLos = false;
    in.tankDist2d = 10.0f;  // in range, but no LOS
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::Reconnect);
}

TEST(DcCombatRegroupTest, HealerPreposOutOfRangeReconnects)
{
    // In LOS but beyond heal range less slack (30 - 8 = 22): reconnect.
    RegroupInputs in = BaseHealer();
    in.tankLos = true;
    in.tankDist2d = 25.0f;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::Reconnect);
}

TEST(DcCombatRegroupTest, HealerPreposInRangeAndLosIsNone)
{
    // In LOS and within heal range less slack (< 22): can heal when damage lands.
    RegroupInputs in = BaseHealer();
    in.tankLos = true;
    in.tankDist2d = 15.0f;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::None);
}

TEST(DcCombatRegroupTest, HealerPreposSlackBoundary)
{
    // healRange - slack = 22. Just inside is None, just outside Reconnects.
    RegroupInputs in = BaseHealer();
    in.tankLos = true;
    in.tankDist2d = 21.9f;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::None);
    in.tankDist2d = 22.1f;
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::Reconnect);
}

TEST(DcCombatRegroupTest, HealerZeroSlackUsesFullRange)
{
    RegroupInputs in = BaseHealer();
    in.slack = 0.0f;
    in.tankLos = true;
    in.tankDist2d = 29.0f;  // within full heal range (30)
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::None);
    in.tankDist2d = 31.0f;  // beyond it
    EXPECT_EQ(DecideCombatRegroup(in), RegroupVerdict::Reconnect);
}
