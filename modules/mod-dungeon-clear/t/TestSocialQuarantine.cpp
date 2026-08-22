/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DcSocialQuarantineRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/ScriptedPullRegistry.h"

// The SOCIAL QUARANTINE zones — the registry that takes a pack out of the
// cross-pack aggro graph outright because no geometry can keep it out of the
// fight (see DcSocialQuarantineRegistry.h for the two stock predicates it turns
// off, and why that is a hammer rather than a tool).
//
// This is a table of measured facts about map 585, and every one of them is a
// fact a live run cannot check for itself: a cylinder that is a yard too big
// silently disarms a pack the run was supposed to fight, and one that is a yard
// too small silently lets the pack it was authored for join the wipe. Both look
// identical in the log. So the spawn coordinates are duplicated here from
// acore_world.creature and the containment is asserted against them.

namespace
{
    uint32 constexpr MGT      = 585;
    uint32 constexpr DELRISSA = 24560;
    uint32 constexpr SELIN    = 24723;

    uint32 constexpr MAGE_GUARD = 24683;
    uint32 constexpr BLOOD_KNGT = 24684;
    uint32 constexpr MAGISTER   = 24685;
    uint32 constexpr WARLOCK    = 24686;
    uint32 constexpr PHYSICIAN  = 24687;
    uint32 constexpr WITCH      = 24696;
    uint32 constexpr SISTER     = 24697;
    uint32 constexpr SMUGGLER   = 24698;
    uint32 constexpr SENTINEL   = 24777;
    uint32 constexpr BROKEN_SENTINEL = 24808;   // hostile prop, NullCreatureAI

    struct Spawn
    {
        uint32 entry;
        float x, y, z;
    };

    // acore_world.creature, map 585 — Delrissa's two flanking formations, the
    // patrolling sentinel below her, and the neighbours each cylinder has to
    // EXCLUDE. Copied rather than derived, on purpose: this is the check on the
    // registry, so it must not share a source with it.
    std::vector<Spawn> const& WestFlank()   // formation leader 96771
    {
        static std::vector<Spawn> const s = {
            {MAGE_GUARD, 100.69f, 27.39f, -21.32f},   // 96771
            {MAGISTER,    89.61f, 19.97f, -21.25f},   // 96793
            {WARLOCK,     93.08f, 24.62f, -21.25f},   // 96810
            {PHYSICIAN,   92.41f, 14.65f, -21.52f},   // 96821
        };
        return s;
    }

    std::vector<Spawn> const& EastFlank()   // formation leader 96779
    {
        static std::vector<Spawn> const s = {
            {BLOOD_KNGT, 163.72f, 16.78f, -21.46f},   // 96779
            {MAGISTER,   158.94f, 23.03f, -21.32f},   // 96790
            {WARLOCK,    162.43f, 24.90f, -21.25f},   // 96807
            {SISTER,     159.43f, 17.84f, -21.40f},   // 96845
            {SMUGGLER,   166.92f, 21.54f, -21.25f},   // 96848
        };
        return s;
    }

    // Everything on the lower level near Delrissa that is NOT in either flank —
    // the two mid-hall packs the run clears on its way past, the boss, the prop,
    // and the pair north of her. If a cylinder ever swallows one of these, the run
    // stops fighting something it is supposed to fight.
    std::vector<Spawn> const& Outsiders()
    {
        static std::vector<Spawn> const s = {
            {DELRISSA,   126.90f,  19.15f, -19.92f},   // 96966 — the boss herself
            {BROKEN_SENTINEL, 100.37f, 19.79f, -21.21f},  // 96950 — 6.7yd inside W
            {MAGE_GUARD, 107.50f, -22.85f, -21.47f},   // 96769 — west mid-hall pack
            {WARLOCK,    103.32f, -27.65f, -21.35f},   // 96806
            {PHYSICIAN,  105.24f, -33.20f, -21.30f},   // 96820
            {WITCH,      110.79f, -32.95f, -21.18f},   // 96840
            {SISTER,     113.07f, -27.38f, -21.49f},   // 96844
            {MAGE_GUARD, 146.15f, -25.44f, -21.19f},   // 96773 — east mid-hall pack
            {BLOOD_KNGT, 151.53f, -28.71f, -21.35f},   // 96784
            {MAGISTER,   159.78f, -19.11f, -21.39f},   // 96795
            {WARLOCK,    156.36f, -25.43f, -21.30f},   // 96811
            {MAGE_GUARD, 160.15f,  40.15f, -19.92f},   // 96772 — the pair north-east
            {MAGISTER,   164.91f,  40.57f, -19.92f},   // 96794
            {BLOOD_KNGT,  75.10f,  32.17f, -19.92f},   // 96782 — the pair north-west
            {PHYSICIAN,   85.91f,  39.38f, -19.92f},   // 96822
            {WARLOCK,     82.45f, -43.14f, -19.92f},   // 96804
            {PHYSICIAN,   80.35f, -37.33f, -19.92f},   // 96819
        };
        return s;
    }

    // Waypoint path 969460 — the hall sentinel 96946 patrols this line, and the
    // zone has to hold it wherever along it the party finds it.
    std::pair<float, float> const kPatrolA{126.83f, -80.82f};
    std::pair<float, float> const kPatrolB{127.08f, -48.44f};

    float Dist2d(float ax, float ay, float bx, float by)
    {
        float const dx = ax - bx;
        float const dy = ay - by;
        return std::sqrt(dx * dx + dy * dy);
    }

    DcQuarantineZone const& Zone(char const* nameFragment)
    {
        for (DcQuarantineZone const& z : DcSocialQuarantineRegistry::AllRows())
            if (std::string(z.name).find(nameFragment) != std::string::npos)
                return z;
        ADD_FAILURE() << "no quarantine zone named like '" << nameFragment << "'";
        static DcQuarantineZone const empty;
        return empty;
    }
}

TEST(DcSocialQuarantineTest, OnlyMagistersTerraceHasZones)
{
    EXPECT_TRUE(DcSocialQuarantineRegistry::HasRows(MGT));
    EXPECT_FALSE(DcSocialQuarantineRegistry::HasRows(0));
    EXPECT_FALSE(DcSocialQuarantineRegistry::HasRows(546));   // The Underbog
    EXPECT_FALSE(DcSocialQuarantineRegistry::HasRows(554));   // The Mechanar

    // Three rows, all gated on Delrissa. A zone on any OTHER boss of this map
    // would be in force during a leg it was never measured against.
    EXPECT_EQ(DcSocialQuarantineRegistry::AllZones(MGT).size(), 3u);
    EXPECT_EQ(DcSocialQuarantineRegistry::Zones(MGT, DELRISSA).size(), 3u);
    EXPECT_TRUE(DcSocialQuarantineRegistry::Zones(MGT, SELIN).empty());
    EXPECT_TRUE(DcSocialQuarantineRegistry::Zones(MGT, 24664).empty());  // Kael'thas
    EXPECT_TRUE(DcSocialQuarantineRegistry::Zones(MGT, 24744).empty());  // Vexallus
}

TEST(DcSocialQuarantineTest, EveryZoneNamesItsMembers)
{
    // The entry filter is NOT optional here, unlike the scripted-pull volume's.
    // It is the only thing keeping a cylinder off a prop or a scripted mob that
    // happens to stand inside it — the west zone's own Broken Sentinel is 6.7yd
    // from its centre. A row with an empty list would quarantine that prop, and
    // the next reader would find a hostile creature that no longer defends
    // itself and nothing in the log to explain it.
    for (DcQuarantineZone const& z : DcSocialQuarantineRegistry::AllRows())
    {
        EXPECT_NE(z.name, nullptr);
        EXPECT_NE(z.mapId, 0u) << z.name;
        EXPECT_NE(z.bossEntry, 0u) << z.name;
        EXPECT_GT(z.radius, 0.0f) << z.name;
        EXPECT_GT(z.zBand, 0.0f) << z.name;
        EXPECT_FALSE(z.entries.empty()) << z.name;
    }
}

TEST(DcSocialQuarantineTest, TheFlankCylindersHoldTheirOwnPackAndNothingElse)
{
    DcQuarantineZone const& west = Zone("west flank");
    DcQuarantineZone const& east = Zone("east flank");

    for (Spawn const& s : WestFlank())
    {
        EXPECT_TRUE(DcSocialQuarantineRegistry::InZone(west, s.x, s.y, s.z))
            << "west zone misses its own member " << s.entry;
        EXPECT_TRUE(DcSocialQuarantineRegistry::IsZoneEntry(west, s.entry))
            << "west zone's entry filter misses " << s.entry;
    }
    for (Spawn const& s : EastFlank())
    {
        EXPECT_TRUE(DcSocialQuarantineRegistry::InZone(east, s.x, s.y, s.z))
            << "east zone misses its own member " << s.entry;
        EXPECT_TRUE(DcSocialQuarantineRegistry::IsZoneEntry(east, s.entry))
            << "east zone's entry filter misses " << s.entry;
    }

    // Cross-containment: neither flank may hold the other's mobs...
    for (Spawn const& s : EastFlank())
        EXPECT_FALSE(DcSocialQuarantineRegistry::InZone(west, s.x, s.y, s.z));
    for (Spawn const& s : WestFlank())
        EXPECT_FALSE(DcSocialQuarantineRegistry::InZone(east, s.x, s.y, s.z));

    // ...nor anything else on the floor. The Broken Sentinel is the interesting
    // one — it IS inside the west cylinder and is kept out by the entry filter
    // alone, exactly as the rotunda's east pack keeps its own copy out.
    for (Spawn const& s : Outsiders())
    {
        bool const inWest = DcSocialQuarantineRegistry::InZone(west, s.x, s.y, s.z) &&
                            DcSocialQuarantineRegistry::IsZoneEntry(west, s.entry);
        bool const inEast = DcSocialQuarantineRegistry::InZone(east, s.x, s.y, s.z) &&
                            DcSocialQuarantineRegistry::IsZoneEntry(east, s.entry);
        EXPECT_FALSE(inWest) << "west zone swallows outsider " << s.entry
                             << " at (" << s.x << "," << s.y << ")";
        EXPECT_FALSE(inEast) << "east zone swallows outsider " << s.entry
                             << " at (" << s.x << "," << s.y << ")";
    }
}

TEST(DcSocialQuarantineTest, TheFlanksSitInsideTheGroundDelrissaMayBeFoughtOn)
{
    // The two measurements the row rests on, asserted so a future "just move the
    // camp back further" reads them first.
    //
    //  * boss_priestess_delrissa::CheckInRoom() evades the encounter outright at
    //    GetDistance(GetHomePosition()) >= 75yd, so 75 is the hard ceiling on how
    //    far back she may ever be dragged. Every yard of the fight happens inside
    //    that ball, and BOTH flanking packs are inside it — they are not
    //    somewhere the party can simply be camped away from, they are inside the
    //    only ground the encounter is allowed to occupy.
    //  * Each is nonetheless well outside her own aggro at spawn, so they are not
    //    part of the encounter by default. That is what makes them a QUARANTINE
    //    question (who may join) rather than an encounter-scripting one.
    float constexpr kDelrissaX = 126.90f, kDelrissaY = 19.15f;
    float constexpr kEvadeRadius = 75.0f;
    float constexpr kEliteReach = 21.0f;

    for (std::vector<Spawn> const* pack : {&WestFlank(), &EastFlank()})
        for (Spawn const& s : *pack)
        {
            float const d = Dist2d(s.x, s.y, kDelrissaX, kDelrissaY);
            EXPECT_LT(d, kEvadeRadius)
                << "flank mob " << s.entry << " is outside the boss's own evade "
                   "ball — it cannot reach a legal fight and needs no quarantine";
            EXPECT_GT(d, kEliteReach)
                << "flank mob " << s.entry << " is inside the boss's aggro at "
                   "spawn — the encounter always starts as a joint fight and the "
                   "quarantine is not the right tool for it";
        }
}

TEST(DcSocialQuarantineTest, TheSentinelZoneCoversItsWholePatrolPath)
{
    DcQuarantineZone const& patrol = Zone("hall sentinel");

    // Both waypoints and the midpoint, at the patrol's own floor. A zone that only
    // held one end would release the sentinel every time it walked to the other,
    // which is precisely the "not in range right now" fact with a 35-second shelf
    // life that the rotunda's own hall patrol taught this plan to distrust.
    for (auto const& p : {kPatrolA, kPatrolB})
        EXPECT_TRUE(DcSocialQuarantineRegistry::InZone(patrol, p.first, p.second, -21.5f))
            << "patrol endpoint (" << p.first << "," << p.second << ") is outside the zone";
    EXPECT_TRUE(DcSocialQuarantineRegistry::InZone(
        patrol, (kPatrolA.first + kPatrolB.first) * 0.5f,
        (kPatrolA.second + kPatrolB.second) * 0.5f, -21.5f));

    EXPECT_TRUE(DcSocialQuarantineRegistry::IsZoneEntry(patrol, SENTINEL));

    // The OTHER sentinel on this map (96944) shares the entry and patrols the
    // corridor back toward Vexallus 12yd higher. The z-band is what keeps it out —
    // assert that rather than trusting the 65yd of horizontal separation, since a
    // future re-centre could spend the horizontal margin without noticing.
    EXPECT_FALSE(DcSocialQuarantineRegistry::InZone(patrol, patrol.x, patrol.y, -9.5f))
        << "the corridor sentinel's z-level is inside the band";

    // And no rotunda mob is: the nearest rotunda spawn is the south pack, and the
    // zone must stop well short of it or a rotunda stage would arm against a pack
    // that cannot fight back.
    EXPECT_GT(Dist2d(patrol.x, patrol.y, 118.87f, -159.34f), patrol.radius + 40.0f)
        << "the sentinel zone reaches toward the rotunda's south pack";
}

TEST(DcSocialQuarantineTest, NoZoneOverlapsAScriptedPullPack)
{
    // The two sources must not both claim a pack. A scripted row's cylinder is
    // released when its stage arms — that is how the plan wakes exactly one pack
    // at a time — and a registry zone gated on the same boss would hold it anyway,
    // leaving the tank walking into a pack that will not fight back and a stage
    // that can never report itself cleared.
    for (DcQuarantineZone const& z : DcSocialQuarantineRegistry::AllRows())
        for (ScriptedPullStage const* s : ScriptedPullRegistry::Rows(z.mapId))
        {
            if (s->bossEntry != z.bossEntry)
                continue;
            bool sharesEntry = false;
            for (uint32 e : z.entries)
                if (ScriptedPullRegistry::IsPackEntry(*s, e))
                    sharesEntry = true;
            if (!sharesEntry)
                continue;
            EXPECT_GT(Dist2d(z.x, z.y, s->packX, s->packY), z.radius + s->packRadius)
                << "quarantine zone '" << z.name << "' overlaps scripted stage "
                << s->order << " ('" << s->name << "')";
        }
}
