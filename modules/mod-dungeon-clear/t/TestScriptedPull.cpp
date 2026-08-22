/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/FightInPlaceRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/ScriptedPullRegistry.h"

// The scripted-pull plans on Magisters' Terrace (585): Selin Fireheart's room
// (orders 0-1) and the Delrissa rotunda (orders 2-6).
//
// Everything here is geometry the plans are only correct BECAUSE of — the pack
// cylinders sized to hold their own spawns and exclude their neighbours, the arm
// radius sized to sit between the last trash and the room, and the stage ordering.
// A row edit that breaks any of it produces a run that looks healthy in the log and
// pulls the boss, which is exactly the failure mode seven previous attempts at
// Selin's room died of. So it is pinned here rather than left to a live run to
// discover.
//
// The two plans want different things asserted, because the rooms are different
// shapes: Selin's pair is separated from the boss by a WALL, and its tests are about
// sight-lines through a doorway. The rotunda's five are separated from each other by
// DISTANCE alone, and its tests are about which packs are still alive at each stage.
// Use RowsFor(boss) rather than Rows(MGT) for anything that is really about one room.

namespace
{
    uint32 constexpr MGT = 585;
    uint32 constexpr SKULKER = 24688;   // Wretched Skulker
    uint32 constexpr BRUISER = 24689;   // Wretched Bruiser
    uint32 constexpr HUSK    = 24690;   // Wretched Husk
    uint32 constexpr CRYSTAL = 24722;   // Fel Crystal — hostile prop, NOT a pack member
    uint32 constexpr SELIN   = 24723;

    ScriptedPullStage const& East()
    {
        ScriptedPullStage const* s = ScriptedPullRegistry::Find(MGT, 0);
        EXPECT_NE(s, nullptr);
        return *s;
    }
    ScriptedPullStage const& West()
    {
        ScriptedPullStage const* s = ScriptedPullRegistry::Find(MGT, 1);
        EXPECT_NE(s, nullptr);
        return *s;
    }

    // --- the second plan on this map: the Delrissa rotunda ---------------------
    uint32 constexpr DELRISSA   = 24560;   // Priestess Delrissa — the rotunda's boss gate
    uint32 constexpr MAGE_GUARD = 24683;
    uint32 constexpr BLOOD_KNGT = 24684;
    uint32 constexpr MAGISTER   = 24685;
    uint32 constexpr WARLOCK    = 24686;
    uint32 constexpr PHYSICIAN  = 24687;
    uint32 constexpr WITCH      = 24696;
    uint32 constexpr SISTER     = 24697;
    uint32 constexpr SMUGGLER   = 24698;
    uint32 constexpr BROKEN_SENTINEL = 24808;  // hostile prop, NullCreatureAI, NOT a member
    uint32 constexpr SENTINEL   = 24777;   // Sunblade Sentinel — the hall patrol

    // The rows of ONE plan. Map 585 now carries two, and most of the geometry
    // asserted below is a statement about one room's walls and spawns — holding the
    // other plan's rows to it would pass or fail by coincidence.
    std::vector<ScriptedPullStage const*> RowsFor(uint32 bossEntry)
    {
        std::vector<ScriptedPullStage const*> out;
        for (ScriptedPullStage const* s : ScriptedPullRegistry::Rows(MGT))
            if (s->bossEntry == bossEntry)
                out.push_back(s);
        return out;
    }

    // The rotunda plan has SIX rows and only five of them are packs in the room. Order
    // 2 is the hall patrol — a prerequisite expressed as a stage, whose camp, cylinder
    // and arm anchor are all statements about the corridor rather than the rotunda —
    // so every assertion written against the room's spawns takes orders 3-7 only.
    ScriptedPullStage const& HallPatrol()
    {
        ScriptedPullStage const* s = ScriptedPullRegistry::Find(MGT, 2);
        EXPECT_NE(s, nullptr);
        return *s;
    }
    std::vector<ScriptedPullStage const*> RotundaPackRows()
    {
        std::vector<ScriptedPullStage const*> out;
        for (int32 o = 3; o <= 7; ++o)
            if (ScriptedPullStage const* s = ScriptedPullRegistry::Find(MGT, o))
                out.push_back(s);
        return out;
    }

    // The hall patrol's waypoint path (acore_world.waypoint_data, path 969450) and
    // its spawn — the ground the order-2 row has to own end to end.
    std::vector<std::pair<float, float>> const& PatrolPath()
    {
        static std::vector<std::pair<float, float>> const kPath{
            {105.54f, -214.95f}, {137.00f, -214.83f}, {137.13f, -214.67f}};
        return kPath;
    }

    float Dist2d(float ax, float ay, float bx, float by)
    {
        float const dx = ax - bx;
        float const dy = ay - by;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Every rotunda spawn, by pack, in the plan's own order (acore_world.creature,
    // map 585, grouped by creature_formations leader). The rows are only correct
    // relative to these, so they are the fixture rather than a bounding box.
    //
    // INDEX IS PACK-ROW ORDER (order 3 + index), not a stable name for a pack: the
    // plan pulls south -> centre -> east -> north-east -> north-west, so index 1 is
    // the CENTRE formation and index 2 the EAST one. Re-ordering the rows means
    // re-ordering this. The hall patrol (order 2) is not in here — it is not in the
    // room.
    std::vector<std::vector<std::pair<float, float>>> const& RotundaPacks()
    {
        static std::vector<std::vector<std::pair<float, float>>> const kPacks{
            // 0 south (leader 96778)
            {{118.87f, -159.34f}, {121.07f, -155.71f}, {114.28f, -157.69f},
             {118.43f, -151.50f}, {113.57f, -152.61f}},
            // 1 centre (leader 96777)
            {{130.97f, -132.13f}, {123.05f, -133.24f}, {121.44f, -126.02f},
             {130.70f, -124.20f}},
            // 2 east (leader 96824)
            {{152.72f, -138.12f}, {146.19f, -134.35f}, {157.72f, -132.21f},
             {146.79f, -125.14f}},
            // 3 north-east (leader 96780)
            {{139.40f, -114.11f}, {144.71f, -113.34f}, {136.38f, -108.14f},
             {144.08f, -107.02f}, {139.10f, -104.64f}},
            // 4 north-west (leader 96767)
            {{110.49f, -114.37f}, {115.20f, -112.54f}, {108.24f, -111.12f},
             {116.12f, -107.24f}, {110.82f, -106.34f}},
        };
        return kPacks;
    }

    // Where the tank actually stands when a BODY-PULL row takes its tag: the walk-in
    // closes to GetAggroRange minus the 2yd it backs off by, aimed at the nearest pack
    // member from the stand spot. Every Sunblade entry in this room carries
    // detection_range 20 and the aggro radius is `detection - (playerLevel - mobLevel)`,
    // so a level-70 party sees 20yd from the level-70 mobs and 21 from the level-71
    // ones — 18yd of stop distance either way, once the 2yd is taken off the smaller.
    //
    // This point, NOT the stand spot, is what a body-pull row has to clear the
    // still-live packs from: the spot is only a waypoint on the way to it.
    float constexpr kRotundaTagStop = 18.0f;

    // ...and where it stands in the WORST case, having spent the whole creep. A pack
    // that does not notice the tank arriving inside its radius is walked into at
    // DC_PULL_TAG_CREEP_YARDS_PER_SEC until it does, and on a scripted stage that is
    // bounded to DC_PULL_SCRIPTED_CREEP_LIMIT (4yd) — see DcPullTagStopTest. The rows
    // have to clear the live packs from here too, or the bound is the wrong number.
    float constexpr kRotundaTagStopCrept = kRotundaTagStop - 4.0f;

    // Which member of `pack` the row will actually tag — NearestPackMember's ranking,
    // reproduced: nearest to the stand spot, unless the row names a neighbour to tag
    // away from, in which case furthest from THAT.
    std::pair<float, float> TagTarget(ScriptedPullStage const& s,
                                      std::vector<std::pair<float, float>> const& pack)
    {
        std::pair<float, float> pick = pack.front();
        float best = 1e9f;
        for (auto const& m : pack)
        {
            float const d = s.HasAvoidAnchor()
                ? -Dist2d(m.first, m.second, s.avoidX, s.avoidY)
                :  Dist2d(m.first, m.second, s.standX, s.standY);
            if (d < best)
            {
                best = d;
                pick = m;
            }
        }
        return pick;
    }

    std::pair<float, float> BodyTagPoint(ScriptedPullStage const& s,
                                         std::vector<std::pair<float, float>> const& pack,
                                         float stopAt = kRotundaTagStop)
    {
        std::pair<float, float> const nearest = TagTarget(s, pack);
        float const best = Dist2d(nearest.first, nearest.second, s.standX, s.standY);
        if (best <= stopAt)
            return {s.standX, s.standY};   // already inside — the spot IS the tag point
        float const f = (best - stopAt) / best;
        return {s.standX + (nearest.first - s.standX) * f,
                s.standY + (nearest.second - s.standY) * f};
    }
}

TEST(DcScriptedPullTest, MagistersTerraceCarriesTwoIndependentPlans)
{
    EXPECT_TRUE(ScriptedPullRegistry::HasRows(MGT));
    EXPECT_FALSE(ScriptedPullRegistry::HasRows(0));
    EXPECT_FALSE(ScriptedPullRegistry::HasRows(560));   // Old Hillsbrad

    std::vector<ScriptedPullStage const*> const rows = ScriptedPullRegistry::Rows(MGT);
    ASSERT_EQ(rows.size(), 8u);

    // Rows() is ascending by order, and `order` is UNIQUE ACROSS THE MAP because it
    // is the stage's identity: Find(mapId, order) carries no boss filter, so two
    // plans numbering from zero would resolve a latched stage of one to a row of the
    // other. This is the assertion that keeps a third plan from restarting at 0.
    for (size_t i = 0; i < rows.size(); ++i)
        EXPECT_EQ(rows[i]->order, static_cast<uint32>(i)) << "at index " << i;

    // Two plans, gated on different bosses — which is what keeps them from ever
    // being live at the same time (DueStage drops every row whose bossEntry is not
    // the run's next objective).
    std::vector<ScriptedPullStage const*> const selin = RowsFor(SELIN);
    std::vector<ScriptedPullStage const*> const rotunda = RowsFor(DELRISSA);
    ASSERT_EQ(selin.size(), 2u);
    ASSERT_EQ(rotunda.size(), 6u);   // the hall patrol + five packs

    // ONE camp for Selin's plan — his two packs are a mirrored pair either side of
    // one doorway, so one piece of wall serves both and the party never relocates.
    for (ScriptedPullStage const* s : selin)
    {
        EXPECT_FLOAT_EQ(s->campX, selin[0]->campX) << "stage " << s->order;
        EXPECT_FLOAT_EQ(s->campY, selin[0]->campY) << "stage " << s->order;
        EXPECT_FLOAT_EQ(s->campZ, selin[0]->campZ) << "stage " << s->order;
    }

    // EXACTLY TWO for the rotunda's, and which row gets which is the whole point of
    // the split: the back camp for the rows that run while the south pack is alive,
    // the forward camp for the four that only run once it is dead. A third camp
    // appearing here means someone authored one without the argument that justifies
    // it (see TheForwardCampOnlyServesRowsWhoseBlockersAreDead).
    std::vector<std::pair<float, float>> camps;
    for (ScriptedPullStage const* s : rotunda)
    {
        std::pair<float, float> const c{s->campX, s->campY};
        if (std::find(camps.begin(), camps.end(), c) == camps.end())
            camps.push_back(c);
    }
    EXPECT_EQ(camps.size(), 2u);
    // The rows are ascending by order, so the camp may move forward once and never
    // back — a plan that oscillated between camps would walk the party through the
    // neck twice per pull.
    size_t moved = 0;
    for (size_t i = 1; i < rotunda.size(); ++i)
        if (rotunda[i]->campX != rotunda[i - 1]->campX ||
            rotunda[i]->campY != rotunda[i - 1]->campY)
            ++moved;
    EXPECT_EQ(moved, 1u) << "the rotunda camp moves more than once across the plan";

    // And no rotunda camp is anywhere near Selin's, so neither plan's arm gate can
    // reach the other's ground.
    for (ScriptedPullStage const* s : rotunda)
        EXPECT_GT(Dist2d(selin[0]->campX, selin[0]->campY, s->campX, s->campY), 100.0f)
            << "stage " << s->order;
}

TEST(DcScriptedPullTest, FindRejectsUnknownMapsAndOrders)
{
    EXPECT_EQ(ScriptedPullRegistry::Find(MGT, -1), nullptr);   // the "no stage" sentinel
    EXPECT_EQ(ScriptedPullRegistry::Find(MGT, 8), nullptr);    // one past the last row
    EXPECT_EQ(ScriptedPullRegistry::Find(0, 0), nullptr);
}

TEST(DcScriptedPullTest, PackCylindersHoldTheirOwnSpawns)
{
    // East pack: six Wretched at X 222-230, Y -16..-23. Every corner of that box
    // must read as inside its stage.
    EXPECT_TRUE(ScriptedPullRegistry::InPack(East(), 222.0f, -16.0f, -2.9f));
    EXPECT_TRUE(ScriptedPullRegistry::InPack(East(), 230.0f, -16.0f, -2.9f));
    EXPECT_TRUE(ScriptedPullRegistry::InPack(East(), 222.0f, -23.0f, -2.9f));
    EXPECT_TRUE(ScriptedPullRegistry::InPack(East(), 230.0f, -23.0f, -2.9f));

    // West pack: X 222-230, Y +17..+24.
    EXPECT_TRUE(ScriptedPullRegistry::InPack(West(), 222.0f, 17.0f, -2.9f));
    EXPECT_TRUE(ScriptedPullRegistry::InPack(West(), 230.0f, 17.0f, -2.9f));
    EXPECT_TRUE(ScriptedPullRegistry::InPack(West(), 222.0f, 24.0f, -2.9f));
    EXPECT_TRUE(ScriptedPullRegistry::InPack(West(), 230.0f, 24.0f, -2.9f));
}

TEST(DcScriptedPullTest, PackCylindersExcludeTheirNeighbours)
{
    // The centre pair (Skulker 231.3,2.8 and Bruiser 232.1,-2.0) belongs to the
    // BOSS pull, not to either stage — they sit ~10yd from Selin and cannot be
    // peeled off him. A cylinder that swallowed one would aim the stage at a mob
    // whose pull wakes the boss.
    EXPECT_FALSE(ScriptedPullRegistry::InPack(East(), 231.3f, 2.8f, -2.9f));
    EXPECT_FALSE(ScriptedPullRegistry::InPack(East(), 232.1f, -2.0f, -2.9f));
    EXPECT_FALSE(ScriptedPullRegistry::InPack(West(), 231.3f, 2.8f, -2.9f));
    EXPECT_FALSE(ScriptedPullRegistry::InPack(West(), 232.1f, -2.0f, -2.9f));

    // Selin himself (242.1, 0.3).
    EXPECT_FALSE(ScriptedPullRegistry::InPack(East(), 242.1f, 0.3f, -2.9f));
    EXPECT_FALSE(ScriptedPullRegistry::InPack(West(), 242.1f, 0.3f, -2.9f));

    // And the two stages never overlap each other.
    EXPECT_FALSE(ScriptedPullRegistry::InPack(East(), West().packX, West().packY, -2.9f));
    EXPECT_FALSE(ScriptedPullRegistry::InPack(West(), East().packX, East().packY, -2.9f));
}

TEST(DcScriptedPullTest, PackCylinderIsFloorBanded)
{
    EXPECT_TRUE(ScriptedPullRegistry::InPack(East(), East().packX, East().packY, -2.9f));
    // Far above/below the room's single floor: out, whatever the 2D distance says.
    EXPECT_FALSE(ScriptedPullRegistry::InPack(East(), East().packX, East().packY, 40.0f));
    EXPECT_FALSE(ScriptedPullRegistry::InPack(East(), East().packX, East().packY, -60.0f));
}

TEST(DcScriptedPullTest, OnlyTheGuardEntriesArePackMembers)
{
    for (ScriptedPullStage const* s : RowsFor(SELIN))
    {
        EXPECT_TRUE(ScriptedPullRegistry::IsPackEntry(*s, SKULKER));
        EXPECT_TRUE(ScriptedPullRegistry::IsPackEntry(*s, BRUISER));
        EXPECT_TRUE(ScriptedPullRegistry::IsPackEntry(*s, HUSK));
        // The fel crystal sits at the dead centre of BOTH cylinders and is hostile
        // (faction 190). Counting it would mean the stage never reports its pack
        // cleared and the plan never advances to the next one.
        EXPECT_FALSE(ScriptedPullRegistry::IsPackEntry(*s, CRYSTAL));
        EXPECT_FALSE(ScriptedPullRegistry::IsPackEntry(*s, SELIN));
    }
}

TEST(DcScriptedPullTest, ArmRadiusSitsBetweenTheAntechamberAndTheDoor)
{
    // Armed once the tank is in the staging chamber (X 197-213) in front of the room.
    EXPECT_TRUE(ScriptedPullRegistry::InArmRange(East(), 197.0f, 5.0f, -2.9f));
    EXPECT_TRUE(ScriptedPullRegistry::InArmRange(East(), 209.0f, 5.6f, -2.9f));
    // Still armed at the doorway, so a stage that has to re-arm mid-room can.
    EXPECT_TRUE(ScriptedPullRegistry::InArmRange(East(), 216.0f, 0.0f, -2.9f));
    // NOT armed back at the last Sunblade pack before the room (all X <= 182.3): the
    // plan must not hijack the pull pipeline while that trash is the run's problem.
    EXPECT_FALSE(ScriptedPullRegistry::InArmRange(East(), 182.3f, 0.0f, -2.9f));
    EXPECT_FALSE(ScriptedPullRegistry::InArmRange(East(), 182.3f, 18.97f, -2.9f));
    // Or from the far side of the instance (Priestess Delrissa, X=126.9).
    EXPECT_FALSE(ScriptedPullRegistry::InArmRange(East(), 126.9f, 19.2f, -2.9f));
}

TEST(DcScriptedPullTest, TheArmGateIsAnchoredForwardOfTheCamp)
{
    // The arm gate answers "has the tank walked up to the ROOM", and for these rows
    // that is not "has it walked up to the CAMP" — the camp is mid-corridor, 43yd
    // back from the anchor and 11.5yd from the X 179-182 Sunblade pack.
    //
    // If the gate were measured from the camp, the stage would arm while that pack of
    // four elites is still alive: the pull pipeline would be taken off them, the tank
    // walked 40yd past them to a stand spot, and the followers pinned passive in the
    // middle of them. So Selin's rows name an arm anchor, and it is far enough forward
    // of their camp that the arm circle cannot reach back to it.
    //
    // EVERY row on this map names an anchor; only Selin's needs it to be out of reach
    // of its own camp. The rotunda's answer to "there is live trash beside the camp"
    // is the hall-patrol STAGE rather than an unreachable anchor, and once that is the
    // mechanism the anchor is free to cover a camp — which is what lets a drag ending
    // at the forward camp re-arm the next stage without a dead walk. See
    // TheRotundaArmGateClearsTheBackCampAndItsPatrol for the rotunda's version.
    for (ScriptedPullStage const* s : ScriptedPullRegistry::Rows(MGT))
        ASSERT_TRUE(s->HasArmAnchor()) << "stage " << s->order;

    for (ScriptedPullStage const* s : RowsFor(SELIN))
    {
        float const dx = s->armX - s->campX;
        float const dy = s->armY - s->campY;
        EXPECT_GT(std::sqrt(dx * dx + dy * dy), s->armRadius)
            << "stage " << s->order
            << ": the arm radius reaches back to the camp, so the stage can arm "
               "before the trash standing at the camp is dead";
    }

    // Stated the other way round, in coordinates: standing ON the camp does not arm
    // the stage, and neither does standing on the pack that camps beside it
    // (X 179-182, |Y| <= 8 — see the row comments).
    EXPECT_FALSE(ScriptedPullRegistry::InArmRange(East(), East().campX, East().campY,
                                                 East().campZ));
    EXPECT_FALSE(ScriptedPullRegistry::InArmRange(East(), 179.02f, -7.98f, -2.63f));
    EXPECT_FALSE(ScriptedPullRegistry::InArmRange(East(), 182.34f, 4.85f, -2.66f));
}

TEST(DcScriptedPullTest, CampIsOutsideTheBossAggroGateAndStandsAreAuthored)
{
    // The camp must sit BELOW Selin's CanAIAttack plane (X > 216) — that plane is
    // why the party can hold there at all while the fight happens on top of them.
    EXPECT_LT(East().campX, 216.0f);
    EXPECT_LT(West().campX, 216.0f);
    // And outside the fight-in-place room, so the drag-back lands on ground the
    // rest of the pull pipeline already treats as safe.
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(MGT, East().campX, East().campY));

    // BOTH stand spots are outside the room too. Each shoots a diagonal through
    // the doorway at the pack on the FAR side, so no part of the plan needs the
    // tank past Selin's plane until both packs are dead.
    EXPECT_LT(East().standX, 216.0f);
    EXPECT_LT(West().standX, 216.0f);
    // And they are a mirrored pair about the doorway's centre line.
    EXPECT_NEAR(East().standX, West().standX, 0.5f);
    EXPECT_NEAR(East().standY, -West().standY, 0.5f);
}

TEST(DcScriptedPullTest, TheCampIsFarEnoughBackToBeUnreachable)
{
    // The camp is not merely "outside the room": it is far enough back that the two
    // things that kept going wrong at a 20yd camp cannot physically happen. Both are
    // distances, so both are assertable.
    //
    // Two of the six mobs in each pack are Wretched Husks, which cast 44503 Fireball
    // and 44504 Frostbolt — both 40yd (Spell.dbc -> SpellRange.dbc) — off
    // smart_scripts rows carrying castFlags 64, SMARTCAST_COMBAT_MOVE. The core reads
    // that flag as "no combat movement while the target is in range AND in line of
    // sight", so inside 40yd a Husk's answer to being pulled is to step clear of the
    // wall and PLANT, which for this room means stopping in the doorway and holding
    // the fight open across it. Outside 40yd it has no such option and must run.
    //
    // Measured to the pack's OWN SPAWNS, not to the stand spot: the stand spot is a
    // proxy that happens to correlate, and this is the distance the mechanic uses.
    float constexpr kHuskSpellRange = 40.0f;
    // Selin's own leash. He only attacks targets at X > 216, so a boss accidentally
    // tagged and dragged toward this camp stops being able to attack anything and
    // resets. Worth having real margin: it turns the worst mistake in this room from
    // a wipe into a no-op.
    float constexpr kSelinPlaneX = 216.0f;

    // Real spawn positions (acore_world.creature, map 585) of both guard packs.
    std::vector<std::pair<float, float>> const guards{
        {224.41f, -16.27f}, {222.32f, -18.01f}, {222.65f, -20.81f},
        {228.56f, -16.65f}, {227.31f, -22.97f}, {230.53f, -20.94f},
        {225.52f,  16.98f}, {222.50f,  20.46f}, {228.52f,  17.92f},
        {230.19f,  19.71f}, {224.13f,  23.29f}, {228.04f,  23.75f}};

    for (ScriptedPullStage const* s : RowsFor(SELIN))
    {
        float nearest = 1e9f;
        for (auto const& g : guards)
        {
            float const dx = g.first - s->campX;
            float const dy = g.second - s->campY;
            nearest = std::min(nearest, std::sqrt(dx * dx + dy * dy));
        }
        EXPECT_GT(nearest, kHuskSpellRange)
            << "stage " << s->order << ": a Husk standing on its spawn is within "
            << nearest << "yd of the camp, so it can shoot the party from the room "
               "instead of being dragged out of it";

        EXPECT_GT(kSelinPlaneX - s->campX, kHuskSpellRange)
            << "stage " << s->order << ": the camp is not clear of Selin's plane by "
               "enough for a mis-tagged boss to reset on the way";

        // And the camp is BEHIND the last Sunblade pack before the room (X 179-182),
        // so the route has already cleared that pack by the time the party is asked
        // to hold there. A camp forward of it would pin the party into live trash.
        EXPECT_LT(s->campX, 179.0f) << "stage " << s->order;
    }
}

TEST(DcScriptedPullTest, TravelBudgetsCoverTheAuthoredDrag)
{
    // The forming dwell is measured across the camp-to-stand gap and sized from the
    // gap, not flat. The flat 8s it used to carry is ~5s of a 42yd walk at the rate
    // this budget assumes — so it expired before the party could park on a camp that
    // far back, and the tank tagged with the followers still strung out along the
    // corridor. Assert the budget the authored geometry actually needs.
    float const dx = East().campX - East().standX;
    float const dy = East().campY - East().standY;
    float const drag = std::sqrt(dx * dx + dy * dy);

    // Something crossing that gap at the pessimistic rate must fit inside the budget
    // with the base still to spare.
    EXPECT_GT(ScriptedPullTravelBudgetMs(drag),
              DC_SCRIPTED_PULL_TRAVEL_BASE_MS +
                  static_cast<uint32>(drag / 8.0f * 1000.0f))
        << "the forming dwell would expire while the party is still walking to camp";
    // And it is still BOUNDED — a follower that cannot path may not hold the run open.
    EXPECT_LT(ScriptedPullTravelBudgetMs(drag), 60000u);
    // Degenerate input is the base, never a division blow-up.
    EXPECT_EQ(ScriptedPullTravelBudgetMs(0.0f), DC_SCRIPTED_PULL_TRAVEL_BASE_MS);
    EXPECT_EQ(ScriptedPullTravelBudgetMs(-5.0f), DC_SCRIPTED_PULL_TRAVEL_BASE_MS);
}

TEST(DcScriptedPullTest, StandSpotsAreNotMobSpawnPoints)
{
    // A stand spot is measured in-game, and the first west row was — to two
    // decimals on all three axes — the spawn position of a Wretched Bruiser
    // (228.52, 17.92, -2.95). The plan walked the tank into the middle of the pack
    // for four test runs while the symptoms were patched downstream, because
    // nothing anywhere asserted that the spot the tank is sent to is EMPTY FLOOR.
    //
    // The live spawn boxes (acore_world.creature, map 585): the -Y pack occupies
    // X 222-231 / Y -16..-23 and the +Y pack X 222-231 / Y +17..+24. A stand spot
    // inside either box is a mob's feet, not a vantage point.
    //
    // Worth knowing when a coordinate is handed over for a row: `.gps` reports the
    // SELECTED unit's position, not the caller's, so a measurement taken with a mob
    // targeted is that mob's feet. One camp coordinate arrived that way and matched
    // Sunblade Magister 96787 to two decimals on all three axes. Cross-check a new
    // row against `creature` — an exact spawn match is the tell.
    auto inSpawnBox = [](float x, float y)
    {
        return x >= 221.0f && x <= 232.0f &&
               ((y >= 16.0f && y <= 25.0f) || (y >= -24.0f && y <= -15.0f));
    };

    for (ScriptedPullStage const* s : ScriptedPullRegistry::Rows(MGT))
    {
        EXPECT_FALSE(inSpawnBox(s->standX, s->standY))
            << "stage " << s->order << " stand spot (" << s->standX << ", "
            << s->standY << ") sits inside a guard pack's spawn box";
        EXPECT_FALSE(inSpawnBox(s->campX, s->campY))
            << "stage " << s->order << " camp sits inside a guard pack's spawn box";
        // A stand spot must also never be inside its OWN target volume — that is
        // the same error stated in the row's own terms.
        //
        // The hall patrol is the one row this cannot be said of, and not by accident:
        // its volume is a PATH rather than a formation's footprint, so any spot close
        // enough to walk at the sentinel from is inside it, and the volume is sized to
        // hold the camp on purpose so a dragged sentinel does not leave its own stage
        // mid-fight. See TheHallPatrolIsTheRotundasFirstStage.
        if (s->order != 2)
            EXPECT_FALSE(ScriptedPullRegistry::InPack(*s, s->standX, s->standY, s->standZ))
                << "stage " << s->order;
    }
}

TEST(DcScriptedPullTest, EveryPackMemberIsInPullSpellRangeOfItsStandSpot)
{
    // The tag is taken FROM the stand spot, so the distance that matters is spot ->
    // pack member, and the plan is only viable while the NEAREST member is inside a
    // tank pull spell's reach. Avenger's Shield / Shield of the Templar reach 30yd,
    // and that has to be enough on its own: the clamp buys NOTHING extra now
    // (DC_SCRIPTED_PULL_CREEP is 0 — see TheTagIsTakenFromTheSpotAndNotAYardCloser).
    //
    // tr-20260802-215715-3 is why this is pinned: the scan handed the pull a member
    // 31.6yd from the east stand spot, the shield could not reach, and the generic
    // walk-in carried the tank off the spot and into the room. Ranking the pick
    // from the stand spot is the fix; this asserts the row geometry it relies on.
    float constexpr kPullSpellRange = 30.0f;

    auto nearestSpawnDist = [](ScriptedPullStage const& s,
                               std::vector<std::pair<float, float>> const& spawns)
    {
        float best = 1e9f;
        for (auto const& p : spawns)
        {
            float const dx = p.first - s.standX;
            float const dy = p.second - s.standY;
            best = std::min(best, std::sqrt(dx * dx + dy * dy));
        }
        return best;
    };

    // The REAL spawn positions (acore_world.creature, map 585), not a bounding box:
    // the west margin is thin (27.8yd of 30) and a box corner would flatter it.
    EXPECT_LT(nearestSpawnDist(East(), {{224.41f, -16.27f}, {222.32f, -18.01f},
                                        {222.65f, -20.81f}, {228.56f, -16.65f},
                                        {227.31f, -22.97f}, {230.53f, -20.94f}}),
              kPullSpellRange);
    EXPECT_LT(nearestSpawnDist(West(), {{225.52f, 16.98f}, {222.50f, 20.46f},
                                        {228.52f, 17.92f}, {230.19f, 19.71f},
                                        {224.13f, 23.29f}, {228.04f, 23.75f}}),
              kPullSpellRange);
}

TEST(DcScriptedPullTest, StandSpotsStayOutsideSelinsAggro)
{
    // Selin (24723) spawns at (242.07, 0.3) and reaches ~21yd against a level-70
    // party. Both stand spots have to clear that with real margin, because the tank
    // parks on one for the whole tag and the pack's run-in.
    float constexpr kSelinX = 242.07f, kSelinY = 0.3f, kSelinReach = 21.0f;
    for (ScriptedPullStage const* s : ScriptedPullRegistry::Rows(MGT))
    {
        float const dx = s->standX - kSelinX;
        float const dy = s->standY - kSelinY;
        EXPECT_GT(std::sqrt(dx * dx + dy * dy), kSelinReach)
            << "stage " << s->order << " stand spot is inside Selin's aggro";
    }
}

TEST(DcScriptedPullTest, CampLeashCannotReachTheRoom)
{
    // The leash bounds how far the tank may stray from camp during the camp fight
    // before it is walked back. It is only worth having if the furthest point it
    // tolerates is still short of the room — otherwise a chase excursion reaches
    // the doorway and the unpulled pack sees the tank anyway.
    //
    // The room's doorway sits at roughly (216, 0). With no arrival hold left, this
    // leash is the ONLY thing standing between a chase and the doorway for the whole
    // camp fight, so the bar it has to clear is the one that matters.
    float const dx = 216.0f - East().campX;
    float const dy = 0.0f - East().campY;
    float const campToDoor = std::sqrt(dx * dx + dy * dy);

    EXPECT_LT(DC_SCRIPTED_PULL_LEASH, campToDoor);
    // And the leash must sit outside the distance the recall releases at, or the latch
    // would trip and clear on the same tick and produce the in-out shuffle.
    EXPECT_GT(DC_SCRIPTED_PULL_LEASH, DC_SCRIPTED_PULL_RECALL_HOME);
}

TEST(DcScriptedPullTest, BothLeashesFitAGroundEffectStepOut)
{
    // THE LEASHES ARE SIZED BY THE STEP-OUT, and this is the property that makes them
    // usable: a leash tighter than a legal escape is not a loose bound on a fight, it
    // is a CONTRADICTION. Two rungs then drive the bot to two places forever — the
    // step-out out of the effect, the leash back onto the camp — and neither can
    // yield, which is the ping-pong the player watched.
    //
    // The two step-outs a camp fight can meet, and how far from the camp they leave a
    // bot that was in melee (~5yd out) when the effect landed:
    //   * generic avoid-aoe: one hop of min(radius + 1, AiPlayerbot.FleeDistance),
    //     and FleeDistance is 5 by default and on this server.  5 + 5 = 10yd.
    //   * MgT's Magic Dampening Field escape: only accepts a spot clearing every field
    //     by DAMPENING_CLEAR (9yd), off rings of 7/10/13/16 — so from 5yd out it lands
    //     on the 10yd ring.  5 + 10 = 15yd, less what the ring shares with the camp.
    float constexpr kFleeDistance   = 5.0f;   // AiPlayerbot.FleeDistance
    float constexpr kMeleeStandoff  = 5.0f;   // melee reach + the slot fan
    float constexpr kDampeningClear = 9.0f;   // MgTShared DAMPENING_CLEAR

    // Neither leash may be inside the strict lower bound: a dampening escape is never
    // ACCEPTED closer than DAMPENING_CLEAR to the field centre, so a leash under that
    // can never coexist with a field on the camp, whatever else is true.
    EXPECT_GT(DC_SCRIPTED_PULL_FOLLOWER_LEASH, kDampeningClear)
        << "an escape this leash forbids is the only escape the bot is allowed to take";
    EXPECT_GT(DC_SCRIPTED_PULL_LEASH, kDampeningClear);

    // And both must clear the generic hop taken from melee range, which is the one
    // that happens on every map rather than only in Magisters' Terrace.
    EXPECT_GT(DC_SCRIPTED_PULL_FOLLOWER_LEASH, kMeleeStandoff + kFleeDistance);
    EXPECT_GT(DC_SCRIPTED_PULL_LEASH, kMeleeStandoff + kFleeDistance);

    // The recall's release band has the same job on the way back: letting go on the
    // camp anchor puts the tank back inside an effect centred on it, so it has to let
    // go outside the widest field a step-out is sized against.
    EXPECT_GE(DC_SCRIPTED_PULL_RECALL_HOME, kDampeningClear);
}

TEST(DcScriptedPullTest, TheTagIsTakenFromTheSpotAndNotAYardCloser)
{
    // The stand spots clear the CENTRE PAIR by about a yard, not by three, so any
    // creep toward the pack spends the whole margin. From the east stand spot
    // (212.22, 7.42) the two mobs flanking Selin sit at:
    //   Skulker 96825 (231.70,  2.63)  20.06yd
    //   Bruiser 96830 (231.62, -1.86)  21.50yd
    // and a level-69 elite reaches ~19yd against a level-70.
    //
    // tr-20260803-133734-1 is why this is pinned at zero: with 2.5yd of creep the tank
    // stood at ~(213.34, 5.19) — 18.54yd from that Skulker, inside aggro — and
    // body-pulled both centre mobs on the first pull. Creep also swings the sight-line
    // to them from Y~6.5 at the doorway (wall) to Y~4.8 (the opening), so it shortens
    // the range and grants line of sight at the same time.
    EXPECT_FLOAT_EQ(DC_SCRIPTED_PULL_CREEP, 0.0f);

    // The margin the spots actually have, asserted so a re-measure cannot quietly
    // erase it. Both stand spots must clear the centre pair's ~19yd reach.
    float constexpr kEliteReach = 19.0f;
    std::vector<std::pair<float, float>> const centrePair{{231.70f, 2.63f},
                                                          {231.62f, -1.86f}};
    for (ScriptedPullStage const* s : ScriptedPullRegistry::Rows(MGT))
    {
        for (auto const& c : centrePair)
        {
            float const dx = c.first - s->standX;
            float const dy = c.second - s->standY;
            EXPECT_GT(std::sqrt(dx * dx + dy * dy), kEliteReach)
                << "stage " << s->order << " stand spot is inside the centre pair's "
                   "aggro — the mobs flanking Selin, which no stage owns";
        }
    }
}

TEST(DcScriptedPullTest, TheRoomItselfIsTheKeepOutForFollowers)
{
    // The follower keep-out is a PLACE, not a radius: the fight-in-place row is the
    // room, and no party member may stand in it during a scripted stage. A radius
    // alone was not enough — a follower drifting inside its leash reads as "parked"
    // and yields the tick to whatever is carrying it, so it can cross the doorway
    // while still nominally in bounds.
    //
    // The camp and both stand spots must therefore be OUTSIDE the box, or the
    // keep-out would fire the moment anyone reached the spot they are told to
    // stand on.
    for (ScriptedPullStage const* s : ScriptedPullRegistry::Rows(MGT))
    {
        EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(MGT, s->campX, s->campY));
        EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(MGT, s->standX, s->standY));
    }
    // The doorway and the room floor beyond it ARE in the box — that is the point.
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(MGT, 216.0f, 0.0f));
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(MGT, 226.0f, 20.0f));
}

TEST(DcScriptedPullTest, TheFollowerLeashIsWiderThanTheGateThatWaitsOnIt)
{
    // The leash is a FIGHT radius and must not be the thing a passive follower parks
    // against. A radius is not a place: a bot stops the instant it crosses the
    // boundary, so parking against a leash settles it in a SHELL at leash distance on
    // whichever side it arrived from — a full leash off the authored point, in
    // different cover, and the leash has only grown since. Live
    // (tr-20260803-121459-1, when it was 8yd): every passive tick logged
    // "parked" at 6.0-7.9yd and the party stood at (139.79, -7.66) while the row said
    // (134.14, -14.36), which reads as "the camp is nowhere near the coordinates".
    //
    // Two properties keep that from coming back, and the second is why the first is
    // not enough on its own:
    //   1. Passive followers pin to the slot, not the leash (DcFollowerActions).
    //   2. The leash still has to be WIDER than the gate that waits on the party, or
    //      a follower parked legitimately mid-fight would read as "not set" forever.
    EXPECT_GT(DC_SCRIPTED_PULL_FOLLOWER_LEASH, 2.0f)
        << "a fight radius this tight would fight the follower's own rotation";
    // And the slot pin a passive follower uses must be far tighter than the leash, or
    // switching between them achieves nothing.
    EXPECT_LT(2.0f, DC_SCRIPTED_PULL_FOLLOWER_LEASH * 0.5f);
}

TEST(DcScriptedPullTest, FollowerLeashIsTighterThanTheTanks)
{
    // The tank plants ON the camp and the pack piles onto it there, so a follower has
    // less legitimate ground to cover than the tank does — the tank's number carries a
    // 5yd rejoin standoff and the knockbacks on top. The ORDERING is what is pinned
    // here; the sizes themselves come from the step-out
    // (BothLeashesFitAGroundEffectStepOut).
    EXPECT_LT(DC_SCRIPTED_PULL_FOLLOWER_LEASH, DC_SCRIPTED_PULL_LEASH);

    // But still wide enough to close on a mob standing on the tank, from the far
    // side of the camp slot fan: slot offset + melee reach + a little footwork.
    EXPECT_GT(DC_SCRIPTED_PULL_FOLLOWER_LEASH, 2.0f + 5.0f);
}

TEST(DcScriptedPullTest, TheSeedGateOpensExactlyWhereTheFollowerMayAlreadyStand)
{
    // The non-combat assist's scripted-stage exception. A follower the pack never
    // touches has no rung left that can make it attack (the DC multiplier zeroes the
    // stock proactive pickers, the combat-side assist is stood down, and an instance
    // kill order is combat-engine only), so the seed has to be allowed back in — but
    // ONLY for a mob it would not have to leave the camp to reach.
    //
    // That makes the follower leash the whole specification: the gate must open at
    // exactly the radius the follower is already permitted to occupy. A yard wider and
    // the seed points at ground the leash will pull the bot back off, which is the
    // in-out shuffle; a yard tighter and a mob standing legitimately on the far edge of
    // the party's own footprint reads as "not arrived" and the stall survives.
    EXPECT_TRUE(ScriptedCampFightHasReachedCamp(0.0f)) << "on the camp is arrived";
    EXPECT_TRUE(ScriptedCampFightHasReachedCamp(DC_SCRIPTED_PULL_FOLLOWER_LEASH));
    EXPECT_FALSE(ScriptedCampFightHasReachedCamp(DC_SCRIPTED_PULL_FOLLOWER_LEASH + 0.1f));

    // The mobs that stranded the party in tr-20260808-191202-9 were still crossing the
    // rotunda when the tank reached camp — 41 to 50 yards from their own spawns and
    // nowhere near it. Those ticks must stay closed, or the seed becomes the walk.
    EXPECT_FALSE(ScriptedCampFightHasReachedCamp(19.7f)) << "the aggro-confirm distance";
    EXPECT_FALSE(ScriptedCampFightHasReachedCamp(29.7f)) << "the east row's ranged tag";

    // And the gate must sit inside the standoff the whole plan is built on: every
    // rotunda row keeps 40yd of caster range between its camp and the packs still
    // standing, so an "arrived" mob can never be one of theirs.
    EXPECT_LT(DC_SCRIPTED_PULL_FOLLOWER_LEASH, 40.0f);
}

TEST(DcScriptedPullTest, LosingGroundIsARatchetNotATickDelta)
{
    // Three legs re-issue one unchanged destination every tick — the tank's drag-back,
    // the follower hold-at-camp, and the tank's camp-leash recall — and DcMoveTo
    // dedupes on destination, so the moment another generator takes the bot the leg
    // goes silent and the standing-still backstops stay blind (the bot is moving,
    // outward). Distance is the only evidence, and this is the read.
    //
    // tr-20260803-125341-1 is why the third leg exists: the recall had no ratchet, so
    // after the leash tripped at 13.2yd it issued nothing for twenty-one seconds while
    // MoveChase drove the tank to X~216 and into Selin's room.

    // No leg in flight -> never fires, whatever the distance.
    EXPECT_FALSE(ScriptedPullLostGround(0.0f, 500.0f));

    // Closing, or holding station, is not losing ground.
    EXPECT_FALSE(ScriptedPullLostGround(20.0f, 20.0f));
    EXPECT_FALSE(ScriptedPullLostGround(20.0f, 4.0f));

    // A RATCHET, not a tick-to-tick delta: path noise and the arc around a doorway
    // both give ground momentarily, so the tolerance has to absorb them.
    EXPECT_FALSE(ScriptedPullLostGround(20.0f, 20.0f + DC_SCRIPTED_PULL_LOSE_GROUND));
    EXPECT_TRUE(ScriptedPullLostGround(20.0f, 20.0f + DC_SCRIPTED_PULL_LOSE_GROUND + 0.1f));

    // The live shape: latched at 13.2yd, then carried outward by a chase.
    EXPECT_TRUE(ScriptedPullLostGround(13.2f, 17.0f));
    EXPECT_TRUE(ScriptedPullLostGround(13.2f, 46.0f));

    // And the tolerance must stay well inside the leash it defends, or the recall
    // would be re-issued only after the tank had already left the leash behind.
    EXPECT_LT(DC_SCRIPTED_PULL_LOSE_GROUND, DC_SCRIPTED_PULL_LEASH * 0.5f);
}

TEST(DcScriptedPullTest, ABodyPullFromTheStandSpotWakesTheCentrePair)
{
    // A tank with NO opener (a level-70 warrior whose ranged slot is empty or holds
    // the wrong ammo — see ResolveRangedWeaponPull) body-pulls instead of holding the
    // stand spot, because holding it means waiting out the whole leg budget for a tag
    // that can never fire and then walking at the boss anyway.
    //
    // This pins what that costs, so the trade-off is a recorded decision rather than
    // something rediscovered from a log. The walk-in runs from the stand spot to the
    // pack's nearest member, and on BOTH stages that line passes well inside the
    // ~19yd reach of the centre pair — the two mobs flanking Selin that no stage
    // owns. A body pull takes them too. Nothing tunable fixes it; the geometry is the
    // geometry, which is exactly why the ranged opener is worth keeping working.
    float constexpr kEliteReach = 19.0f;
    std::vector<std::pair<float, float>> const centrePair{{231.70f, 2.63f},
                                                          {231.62f, -1.86f}};
    // Nearest real spawn to each stand spot (creature rows on map 585).
    std::vector<std::pair<float, float>> const nearestMember{{224.41f, -16.27f},
                                                             {225.52f, 16.98f}};

    auto distToSegment = [](std::pair<float, float> const& p,
                            std::pair<float, float> const& a,
                            std::pair<float, float> const& b)
    {
        float const dx = b.first - a.first;
        float const dy = b.second - a.second;
        float const len = dx * dx + dy * dy;
        float t = len > 0.0f
            ? ((p.first - a.first) * dx + (p.second - a.second) * dy) / len
            : 0.0f;
        t = std::max(0.0f, std::min(1.0f, t));
        float const cx = a.first + t * dx;
        float const cy = a.second + t * dy;
        return std::sqrt((p.first - cx) * (p.first - cx) +
                         (p.second - cy) * (p.second - cy));
    };

    std::vector<ScriptedPullStage const*> const rows = RowsFor(SELIN);
    ASSERT_EQ(rows.size(), nearestMember.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        std::pair<float, float> const stand{rows[i]->standX, rows[i]->standY};
        for (auto const& c : centrePair)
        {
            EXPECT_LT(distToSegment(c, stand, nearestMember[i]), kEliteReach)
                << "stage " << rows[i]->order << ": the body-pull line now clears the "
                   "centre pair — if this is a real re-measure the fallback got safer, "
                   "but check it before relaxing anything that depends on it";
        }
    }

    // And the reason the trade is still worth taking: the drag-back delivers whatever
    // was woken to the row's CAMP, which is far outside the room rather than in the
    // doorway. A body pull is a worse pull than the authored one; it is a much better
    // one than standing still and then walking in with no camp at all.
    for (ScriptedPullStage const* s : rows)
    {
        float const dx = s->campX - s->standX;
        float const dy = s->campY - s->standY;
        EXPECT_GT(std::sqrt(dx * dx + dy * dy), 40.0f)
            << "stage " << s->order << ": camp is close enough to the stand spot that "
               "a body pull would fight next to the room it was dragged out of";
    }
}

TEST(DcScriptedPullTest, TheCampFightIsBoundedByProgressNotByAWallClock)
{
    // Engage was the one leg of a scripted pull with no watchdog at all, and it
    // retired on a single attacker-list read. tr-20260803-144046-4 is what that
    // costs when the read is wrong: "back on the camp (4.5yd) -> fighting" and then
    // four minutes and thirteen seconds of total silence — Engage for 254s, three
    // members combat-flagged, every victim empty and every health bar at 100%. A
    // latched stage stands the advance rung down and camp-holds the party, so no
    // other rung could break it.

    // Not sampled yet can never be stale, however long the run has been going.
    EXPECT_FALSE(ScriptedPullEngageStalled(0, 10u * 60u * 1000u));

    // The clock is re-armed by PROGRESS, so what it measures is time since the
    // pack's health last moved — not time in the phase. A fight that keeps landing
    // damage restamps `since` and can never trip, whatever its total length.
    uint32 constexpr kStart = 100000;
    EXPECT_FALSE(ScriptedPullEngageStalled(kStart, kStart));
    EXPECT_FALSE(ScriptedPullEngageStalled(kStart,
                                           kStart + DC_SCRIPTED_PULL_ENGAGE_STALL_MS));
    EXPECT_TRUE(ScriptedPullEngageStalled(
        kStart, kStart + DC_SCRIPTED_PULL_ENGAGE_STALL_MS + 1));

    // getMSTime() can read backwards across the sample and the compare (the same
    // millisecond-boundary race that made the Returning leg fire at random until it
    // was clamped). Never trip on an underflow.
    EXPECT_FALSE(ScriptedPullEngageStalled(kStart, kStart - 1));

    // Sized between the two things it has to tell apart: long enough that the
    // slowest healthy stage on record (tr-20260803-144046-8, 137s arm to done)
    // cannot trip it on a lull, short enough that the 254s freeze is caught with
    // most of it left.
    EXPECT_GT(DC_SCRIPTED_PULL_ENGAGE_STALL_MS, 30u * 1000u);
    EXPECT_LT(DC_SCRIPTED_PULL_ENGAGE_STALL_MS, 137u * 1000u);
}

TEST(DcScriptedPullTest, TheCampWalksAtAStandoffLongBeforeTheStageIsRetired)
{
    // Two answers to the same evidence at different confidence, so the order they
    // fire in is the property: eight seconds of a pack taking nothing while something
    // sits outside the leash means TRY MOVING UP; forty-five means the stage is over.
    // If these ever crossed, the stage would retire before the camp had walked once
    // and the standoff rung would be dead code.
    EXPECT_LT(DC_SCRIPTED_PULL_STANDOFF_MS, DC_SCRIPTED_PULL_ENGAGE_STALL_MS);

    // Enough of a gap to fit several steps in before the retirement, since a 35yd
    // stand-off needs more than one.
    EXPECT_GE(DC_SCRIPTED_PULL_ENGAGE_STALL_MS / DC_SCRIPTED_PULL_STANDOFF_MS, 3u);

    // Same latch contract as the retirement's, including the underflow corner.
    uint32 constexpr kStart = 100000;
    EXPECT_FALSE(ScriptedPullStandoffStalled(0, 10u * 60u * 1000u));
    EXPECT_FALSE(ScriptedPullStandoffStalled(kStart, kStart));
    EXPECT_FALSE(ScriptedPullStandoffStalled(kStart,
                                             kStart + DC_SCRIPTED_PULL_STANDOFF_MS));
    EXPECT_TRUE(ScriptedPullStandoffStalled(
        kStart, kStart + DC_SCRIPTED_PULL_STANDOFF_MS + 1));
    EXPECT_FALSE(ScriptedPullStandoffStalled(kStart, kStart - 1));

    // A step has to be worth taking against the leash it exists to escape — anything
    // at or under it would leave the camp inside the same standoff it just walked at.
    EXPECT_GT(DC_SCRIPTED_PULL_CAMP_STEP, DC_SCRIPTED_PULL_LEASH - DC_SCRIPTED_PULL_RECALL_HOME);
    // ...and small enough that one bad sample cannot move the party a whole room. The
    // rotunda's longest camp-to-stand segment is ~34yd (the centre row from the
    // forward camp), so a step must be a fraction of a segment, not a segment.
    EXPECT_LT(DC_SCRIPTED_PULL_CAMP_STEP, 20.0f);
}

TEST(DcScriptedPullTest, TheFollowerLeashStretchesExactlyFarEnoughToShoot)
{
    // A SmartAI range-mode caster holds station at spellMaxRange - NOMINAL_MELEE_RANGE
    // from its victim and neither closes nor backs off: Sunblade Magister (Frostbolt,
    // 40yd) at 35, Warlock (Immolate, 30) and Coilskar Witch (Forked Lightning, 30) at
    // 25. Sitting on a camp the tank is planted on, that is 40-45yd of camp-to-mob
    // against a 28.5yd spellDistance — so the plain leash and the shot are mutually
    // exclusive and the follower ping-pongs between them.
    float constexpr kSpell = 28.5f;   // sPlayerbotAIConfig.spellDistance
    float constexpr kMelee = 4.0f;    // reach sum + 1

    // Anything the follower can already hit from inside the leash changes nothing.
    EXPECT_FLOAT_EQ(ScriptedFollowerReachLeash(10.0f, kSpell),
                    DC_SCRIPTED_PULL_FOLLOWER_LEASH);
    EXPECT_FLOAT_EQ(ScriptedFollowerReachLeash(30.0f, kSpell),
                    DC_SCRIPTED_PULL_FOLLOWER_LEASH);

    // The measured cases both fit, and both stretch by yards rather than tens of them.
    // The live hunter cycle: target 40.3yd off the camp.
    float const hunter = ScriptedFollowerReachLeash(40.3f, kSpell);
    EXPECT_GT(hunter, DC_SCRIPTED_PULL_FOLLOWER_LEASH);
    EXPECT_LT(hunter, DC_SCRIPTED_PULL_FOLLOWER_REACH_CAP);
    // A Magister standing off 35yd from a tank 10yd out from the camp.
    float const magister = ScriptedFollowerReachLeash(45.0f, kSpell);
    EXPECT_GT(magister, hunter);
    EXPECT_LT(magister, DC_SCRIPTED_PULL_FOLLOWER_REACH_CAP);

    // What the stretch has to BUY is the shot, and with margin. Spending it along the
    // camp->mob line — which is where the follower's own reach action walks it — the
    // residual gap must be inside range, and must stop short of the range edge so one
    // step of the mob's movement cannot put it back out. Same reason the tank's recall
    // releases in a band rather than on a point.
    EXPECT_LT(40.3f - hunter, kSpell);
    EXPECT_LE(40.3f - hunter, kSpell * 0.9f + 0.01f);
    EXPECT_LT(45.0f - magister, kSpell);

    // Monotone in the gap, and hard-capped however far the mob runs.
    EXPECT_GE(ScriptedFollowerReachLeash(200.0f, kSpell),
              ScriptedFollowerReachLeash(60.0f, kSpell));
    EXPECT_FLOAT_EQ(ScriptedFollowerReachLeash(200.0f, kSpell),
                    DC_SCRIPTED_PULL_FOLLOWER_REACH_CAP);
    EXPECT_FLOAT_EQ(ScriptedFollowerReachLeash(200.0f, kMelee),
                    DC_SCRIPTED_PULL_FOLLOWER_REACH_CAP);

    // Degenerate inputs never widen anything.
    EXPECT_FLOAT_EQ(ScriptedFollowerReachLeash(0.0f, kSpell),
                    DC_SCRIPTED_PULL_FOLLOWER_LEASH);
    EXPECT_FLOAT_EQ(ScriptedFollowerReachLeash(50.0f, 0.0f),
                    DC_SCRIPTED_PULL_FOLLOWER_LEASH);

    // THE CAP IS THE SAFETY ARGUMENT and it is measured off the tightest camp any row
    // uses: the rotunda's forward camp clears the nearest LIVE pack member by 47.5yd
    // (centre Mage Guard 96766) against a ~21.5yd level-71 elite reach. A follower may
    // therefore stand 26yd off that camp without waking anything, and the cap must
    // leave real margin under it rather than sitting on it.
    float constexpr kForwardCampToNearestLivePack = 47.5f;
    float constexpr kEliteReach = 21.5f;
    EXPECT_LT(DC_SCRIPTED_PULL_FOLLOWER_REACH_CAP,
              kForwardCampToNearestLivePack - kEliteReach - 3.0f)
        << "a stretched follower must still be outside every live pack's aggro";
    // And it may never exceed the ground the TANK is allowed to fight on plus its own
    // standing leash — past that the party is ahead of its own tank.
    EXPECT_LT(DC_SCRIPTED_PULL_FOLLOWER_REACH_CAP,
              DC_SCRIPTED_PULL_LEASH + DC_SCRIPTED_PULL_FOLLOWER_LEASH);
}

TEST(DcScriptedPullTest, SelectOrderRunsTheLowestLiveStage)
{
    std::vector<uint32> const orders{0u, 1u};

    // Both packs up -> east first. That ordering is the plan: the east pack is the
    // one the tank has a safe sight-line to while the party is still walking up.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder(orders, {true, true}, -1), 0);
    // East cleared -> west.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder(orders, {false, true}, -1), 1);
    // Both cleared -> nothing due; the run walks in and takes the boss.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder(orders, {false, false}, -1), -1);
    // A stage that fizzled and left survivors behind re-arms ahead of the next one.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder(orders, {true, false}, -1), 0);
}

TEST(DcScriptedPullTest, SelectOrderKeepsACommittedStageWhileItsPackIsDraggedOut)
{
    std::vector<uint32> const orders{0u, 1u};

    // THE case this pin exists for: the tank has tagged the east pack and is
    // hauling it back to camp, so the east VOLUME reads empty mid-drag. Without the
    // pin the plan would hand the tank the west pack while it is still running home
    // with the east one.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder(orders, {false, true}, 0), 0);
    // The pin holds even when nothing anywhere reads live.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder(orders, {false, false}, 0), 0);
    // A pin naming a stage this map does not have is ignored, not trusted.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder(orders, {false, true}, 99), 1);
}

TEST(DcScriptedPullTest, SelectOrderHandlesEmptyInput)
{
    // No stage passed the boss/arm gates this tick.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder({}, {}, -1), -1);
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder({}, {}, 0), -1);
    // Only the west stage is in arm range: its own order is what comes back, not
    // its index in the (filtered) list.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder({1u}, {true}, -1), 1);
}

// ---------------------------------------------------------------------------
// The Delrissa rotunda (orders 2-7): the hall patrol, then five packs. A different
// shape of room to Selin's, so a different set of things have to be true: no wall
// separates the five packs, and what keeps each stand spot safe is DISTANCE to the
// packs that are still alive when it is used. That makes the ORDER part of the
// geometry, and every assertion below is written against the live spawn positions
// rather than a bounding box.
// ---------------------------------------------------------------------------

TEST(DcScriptedPullTest, TheHallPatrolIsTheRotundasFirstStage)
{
    // Sunblade Sentinel 96945 walks path 969450 along the hall the back camp stands
    // in — (105.54, -214.95) to (137.00, -214.83), passing 5.6yd from the camp — and
    // the run only kills it on the way in when it happens to be at the near end. Left
    // alive it walks back east some tens of seconds later, into the middle of whatever
    // stage is running by then (tr-20260804-175340-3: "Fighting Sunblade Sentinel" one
    // second after "Pulling the pack back to camp", and the run wiped in that room).
    //
    // A gate cannot wait that out safely — "not in range right now" has a 35-second
    // shelf life on a 31.5yd path with a 5s dwell at each end — so the plan names the
    // patrol as its FIRST stage instead, and every later row inherits "the sentinel is
    // dead" from SelectOrder, which will not look past a live stage.
    ScriptedPullStage const& p = HallPatrol();
    EXPECT_EQ(p.order, 2u);
    EXPECT_EQ(p.bossEntry, DELRISSA);
    EXPECT_TRUE(p.bodyPull);

    // Only the sentinel counts, and the rotunda's packs are not in this row at all.
    ASSERT_EQ(p.entries.size(), 1u);
    EXPECT_TRUE(ScriptedPullRegistry::IsPackEntry(p, SENTINEL));
    for (uint32 e : {MAGE_GUARD, BLOOD_KNGT, MAGISTER, WARLOCK, PHYSICIAN,
                     WITCH, SISTER, SMUGGLER})
        EXPECT_FALSE(ScriptedPullRegistry::IsPackEntry(p, e));
    // ...and no pack row counts the sentinel, or a stage would wait on a mob that is
    // not in its room.
    for (ScriptedPullStage const* s : RotundaPackRows())
        EXPECT_FALSE(ScriptedPullRegistry::IsPackEntry(*s, SENTINEL))
            << "stage " << s->order;

    // THE VOLUME IS THE PATH, end to end, with the camp inside it — a sentinel dragged
    // home must not read as "gone" the moment it arrives.
    for (auto const& w : PatrolPath())
        EXPECT_TRUE(ScriptedPullRegistry::InPack(p, w.first, w.second, -21.40f))
            << "waypoint (" << w.first << ", " << w.second << ") is outside the row's "
               "own volume, so a sentinel standing there does not hold the plan";
    EXPECT_TRUE(ScriptedPullRegistry::InPack(p, p.campX, p.campY, p.campZ));

    // The OTHER Sunblade Sentinel on this map, 96944, patrols the corridor back toward
    // Vexallus on path 969440 — same entry, 12yd higher and 65yd east. Either the
    // radius or the z-band would keep it out; assert both, because a later re-measure
    // that widens one should still fail rather than silently rely on the other.
    EXPECT_FALSE(ScriptedPullRegistry::InPack(p, 186.47f, -214.10f, -9.51f));
    EXPECT_FALSE(ScriptedPullRegistry::InPack(p, 203.97f, -214.18f, -9.47f));
    EXPECT_FALSE(ScriptedPullRegistry::InPack(p, 167.26f, -214.30f, -9.47f))
        << "the corridor sentinel's near waypoint is inside the hall row's volume";
    // Directly above the hall floor is still not the hall: the z-band, on its own.
    EXPECT_FALSE(ScriptedPullRegistry::InPack(p, 121.34f, -214.89f, -9.50f));

    // THE STAND SPOT IS THE CAMP. A vantage point is a fact about where a pack
    // stands; this one walks, so the only honest answer is "start from the camp and
    // walk at it".
    EXPECT_FLOAT_EQ(p.standX, p.campX);
    EXPECT_FLOAT_EQ(p.standY, p.campY);
    EXPECT_FLOAT_EQ(p.standZ, p.campZ);

    // AND IT ARMS WHERE THE OTHERS DO NOT. This row has to be live while the tank is
    // still walking down into the hall, so its circle covers the hall, both necks and
    // the small room...
    EXPECT_TRUE(ScriptedPullRegistry::InArmRange(p, p.campX, p.campY, p.campZ));
    EXPECT_TRUE(ScriptedPullRegistry::InArmRange(p, 126.00f, -184.00f, -21.27f));
    EXPECT_TRUE(ScriptedPullRegistry::InArmRange(p, 127.50f, -190.00f, -21.27f));
    for (auto const& w : PatrolPath())
        EXPECT_TRUE(ScriptedPullRegistry::InArmRange(p, w.first, w.second, -21.40f))
            << "the tank cannot arm the patrol row from beside the patrol";
    // ...and stops short of the corridor descent, so it cannot hijack the pull
    // pipeline off the trash back there.
    EXPECT_FALSE(ScriptedPullRegistry::InArmRange(p, 167.26f, -214.30f, -9.47f));
    // ...and short of the rotunda itself, so it never covers a pack the room owns.
    for (auto const& pack : RotundaPacks())
        for (auto const& m : pack)
            EXPECT_FALSE(ScriptedPullRegistry::InArmRange(p, m.first, m.second, -20.9f))
                << "a rotunda spawn stands inside the hall row's arm circle";
}

TEST(DcScriptedPullTest, TheHallPatrolGatesEveryRotundaPack)
{
    // Stated as the ordering property the gate actually is: the patrol's order is
    // below every pack row's, so SelectOrder cannot reach a pack while the hall row's
    // volume still holds a live sentinel — and when the route trash already killed it,
    // the row reads empty and is skipped rather than blocking.
    std::vector<uint32> orders{HallPatrol().order};
    for (ScriptedPullStage const* s : RotundaPackRows())
    {
        EXPECT_GT(s->order, HallPatrol().order) << "stage " << s->order;
        orders.push_back(s->order);
    }
    ASSERT_EQ(orders.size(), 6u);

    // Sentinel alive, every pack alive -> the sentinel, whatever else is up.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder(
                  orders, {true, true, true, true, true, true}, -1), 2);
    // Sentinel alive and the packs somehow already dead -> still the sentinel.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder(
                  orders, {true, false, false, false, false, false}, -1), 2);
    // Sentinel dead (the healthy case) -> straight to the south pack.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder(
                  orders, {false, true, true, true, true, true}, -1), 3);
    // Everything dead -> nothing due; the run walks in and takes the boss.
    EXPECT_EQ(ScriptedPullRegistry::SelectOrder(
                  orders, {false, false, false, false, false, false}, -1), -1);
}

TEST(DcScriptedPullTest, RotundaStagesRunInTheAuthoredOrder)
{
    std::vector<ScriptedPullStage const*> const rows = RotundaPackRows();
    ASSERT_EQ(rows.size(), RotundaPacks().size());

    // The order is HAND-AUTHORED — south, centre, east, north-east, north-west — and
    // it is not the camp-distance ordering the plan started with (that one runs the
    // east pack third, at 74.4yd, before the centre pack at 80.3). Nothing derives it,
    // so pin it against the spawns: the row at index i must be the formation this
    // fixture lists at index i, which is the identity the whole rest of this file's
    // "packs later in the order are still alive" reasoning rests on.
    for (size_t i = 0; i < rows.size(); ++i)
    {
        float nearest = 1e9f;
        size_t nearestPack = 0;
        for (size_t j = 0; j < RotundaPacks().size(); ++j)
            for (auto const& m : RotundaPacks()[j])
            {
                float const dd = Dist2d(m.first, m.second, rows[i]->packX, rows[i]->packY);
                if (dd < nearest)
                {
                    nearest = dd;
                    nearestPack = j;
                }
            }
        EXPECT_EQ(nearestPack, i)
            << "stage " << rows[i]->order << " points at formation " << nearestPack
            << " — the rows and this fixture disagree about the pull order, and every "
               "stand spot below is only safe relative to the packs later in it";
    }

    // The camp-distance ordering is NOT a property of these rows, and with two camps
    // in play it is not even a well-formed question: stages 3 and 4 haul to the hall
    // and stages 5-7 to the small room, so "which pack is further from camp" compares
    // two different camps. What replaces it is the clearance assertions below — the
    // invariant the ordering was only ever a proxy for.

    // And every pack is beyond any of this room's casters. Sunblade Magister's
    // Frostbolt, Sunblade Warlock's Immolate and Coilskar Witch's Forked Lightning
    // all carry castFlags 64 (SMARTCAST_COMBAT_MOVE): inside their reach the answer
    // to being pulled is to plant and shoot rather than to come. 40yd is the
    // long-range cap for this class of mob spell in 3.3.5a.
    float constexpr kCasterRange = 40.0f;
    for (size_t i = 0; i < rows.size(); ++i)
        for (auto const& m : RotundaPacks()[i])
            EXPECT_GT(Dist2d(m.first, m.second, rows[i]->campX, rows[i]->campY),
                      kCasterRange)
                << "stage " << rows[i]->order << ": a caster on its spawn can reach "
                   "the camp, so it will hold the fight open instead of being dragged";
}

TEST(DcScriptedPullTest, EveryRotundaRowIsABodyPullExceptTheEastOne)
{
    // The rotunda's rows are body pulls: live, a ranged opener on those formations
    // brings neighbours that walking in does not. Selin's pair are the opposite — a
    // ranged tag from a spot behind a wall is the ONLY thing that works in that room,
    // and a body pull there wakes the centre pair (see
    // ABodyPullFromTheStandSpotWakesTheCentrePair).
    //
    // So the flag is per plan, not per map, and asserting both halves is what stops a
    // future edit from applying one room's answer to the other.
    //
    // THE EAST ROW (order 5) IS THE ONE EXCEPTION, and it is an exception on purpose
    // rather than an oversight. A body pull buys separation by keeping the opener from
    // waking a neighbour; east's neighbour — north-east, via Mage Guard 96774 calling
    // Ethereum Smuggler 96849 at 12.00yd against a 13.05yd limit — is engaged at T=0
    // from the mob's own spawn, before any pull style can matter. With nothing to buy,
    // the row stops paying the 8yd walk-in for it and tags from the spot instead. See
    // TheEastRotundaRowTagsAtRangeFromTheStandSpot for the geometry that makes that
    // legal, and the dossier in ScriptedPullRegistry.cpp for the margin table.
    for (ScriptedPullStage const* s : RowsFor(DELRISSA))
    {
        if (s->order == 5)
            EXPECT_FALSE(s->bodyPull)
                << "the east row tags at range — its neighbour comes either way";
        else
            EXPECT_TRUE(s->bodyPull) << "rotunda stage " << s->order;
    }
    for (ScriptedPullStage const* s : RowsFor(SELIN))
        EXPECT_FALSE(s->bodyPull) << "Selin stage " << s->order;

    // Exactly one rotunda row is ranged. If a second one ever goes ranged it must be
    // for its own measured reason, not by copying this one.
    size_t ranged = 0;
    for (ScriptedPullStage const* s : RowsFor(DELRISSA))
        if (!s->bodyPull)
            ++ranged;
    EXPECT_EQ(ranged, 1u);
}

TEST(DcScriptedPullTest, RotundaStandSpotsClearEveryStillLivePack)
{
    // Half of the plan: the tank has to reach the stand spot without pulling anything.
    // "Anything" means its OWN pack (the spot is a waypoint, and arriving on it must
    // not be the pull) and every pack later in the order, which is still alive.
    //
    // 21yd is what a level-71 elite reaches against a level-70 party (detection_range
    // 20, plus one for the level difference). The other half — where the tank stands
    // when it actually takes the tag — is RotundaBodyTagPointsClearEveryStillLivePack.
    float constexpr kEliteReach = 21.0f;

    std::vector<ScriptedPullStage const*> const rows = RotundaPackRows();
    ASSERT_EQ(rows.size(), RotundaPacks().size());

    for (size_t i = 0; i < rows.size(); ++i)
    {
        ScriptedPullStage const& s = *rows[i];

        // Close enough that the last unauthored leg — spot to aggro edge — is short.
        // Only the NEAREST member matters: NearestPackMember ranks from the stand spot
        // and the formation brings the rest, which is why the far side of a pack
        // sitting 37yd out is not a defect.
        float own = 1e9f;
        for (auto const& m : RotundaPacks()[i])
            own = std::min(own, Dist2d(m.first, m.second, s.standX, s.standY));
        EXPECT_LT(own, 30.0f)
            << "stage " << s.order << ": the walk from the stand spot to the pack's "
               "aggro edge is longer than the authored approach that precedes it";
        // ...and outside its own pack's aggro, or the walk out IS the pull, and it
        // happens before the party has finished setting at the camp.
        EXPECT_GT(own, kEliteReach) << "stage " << s.order;

        // And every pack still standing is out of reach of that spot.
        for (size_t j = i + 1; j < rows.size(); ++j)
            for (auto const& m : RotundaPacks()[j])
                EXPECT_GT(Dist2d(m.first, m.second, s.standX, s.standY), kEliteReach)
                    << "stage " << s.order << " stand spot is inside the aggro of "
                       "pack " << j << ", which this plan has not pulled yet";
    }
}

TEST(DcScriptedPullTest, RotundaBodyTagPointsClearEveryStillLivePack)
{
    // THE PLAN, on a body-pull row. The tank does not tag from the stand spot — it
    // walks on to its own pack's aggro edge, ~8yd further in — so the spot clearing
    // the live packs proves nothing on its own. This is the number that matters.
    //
    // It is also the assertion that caught the reorder. With the old camp-distance
    // ordering the east pack died before the centre one and the centre row could
    // approach from due south at (132.50, -158.00). Pulling centre FIRST leaves that
    // spot's tag point 21.0yd from east Magister 96796 against a 20yd reach — one yard,
    // which is none — so the centre row now comes up the west side instead, across the
    // ground the south stage just cleared, and stands 34.6yd off.
    //
    // The margins the five rows carry, tag point to the nearest still-live mob:
    //   S  43.5yd   C  35.3yd   E  40.1yd   NE 32.2yd   NW  (nothing left)
    // against 20-21yd of reach. 4yd is the bar — well under every one of them, so it
    // fails on a genuine regression rather than on a re-measure of a yard.
    //
    // Asserted at BOTH stop distances: where the walk-in aims (18yd from the target)
    // and where it can end up having spent the whole creep (14yd), which drops the
    // worst margin from +12.2 to +9.7. That second number is the one that makes
    // DC_PULL_SCRIPTED_CREEP_LIMIT a defensible 4 rather than an arbitrary one, and
    // the run that made it matter is in the header of DcPullTagStopTest.
    float constexpr kEliteReach = 21.0f;
    float constexpr kMargin     = 4.0f;

    std::vector<ScriptedPullStage const*> const rows = RotundaPackRows();
    ASSERT_EQ(rows.size(), RotundaPacks().size());

    for (float stopAt : {kRotundaTagStop, kRotundaTagStopCrept})
        for (size_t i = 0; i < rows.size(); ++i)
        {
            ScriptedPullStage const& s = *rows[i];

            // A RANGED row has no walk-in and therefore no body-tag point: the tag is
            // taken from the stand spot, which RotundaStandSpotsClearEveryStillLivePack
            // already holds to the same clearances (and to a STRICTER bar, since the
            // spot is the far end of the 8yd this test models). Nothing to check here.
            if (!s.bodyPull)
                continue;

            std::pair<float, float> const tag =
                BodyTagPoint(s, RotundaPacks()[i], stopAt);

            // The walk-in stops INSIDE its own pack's aggro — that is the pull.
            float own = 1e9f;
            for (auto const& m : RotundaPacks()[i])
                own = std::min(own, Dist2d(m.first, m.second, tag.first, tag.second));
            EXPECT_LE(own, stopAt + 0.01f)
                << "stage " << s.order << ": the walk-in stops short of its own pack";

            for (size_t j = i + 1; j < rows.size(); ++j)
                for (auto const& m : RotundaPacks()[j])
                    EXPECT_GT(Dist2d(m.first, m.second, tag.first, tag.second),
                              kEliteReach + kMargin)
                        << "stage " << s.order << " (stopping " << stopAt
                        << "yd out): the body-tag point is within "
                        << (kEliteReach + kMargin) << "yd of pack " << j
                        << ", which this plan has not pulled yet — the stand spot "
                           "clears it and the point the tank walks to does not";
        }
}

TEST(DcScriptedPullTest, RotundaStandSpotsAreEmptyFloorBetweenPackAndCamp)
{
    std::vector<ScriptedPullStage const*> const rows = RotundaPackRows();
    ASSERT_EQ(rows.size(), RotundaPacks().size());

    for (size_t i = 0; i < rows.size(); ++i)
    {
        ScriptedPullStage const& s = *rows[i];

        // Not a spawn point. `.gps` reports the SELECTED unit's position, so a
        // coordinate measured with a mob targeted is that mob's feet — which is
        // exactly how Selin's first west row went in.
        //
        // The bar is 2yd rather than something roomier on purpose. The CENTRE row's
        // spot stands 3.0yd off south Mage Guard 96768 and inside the south pack's
        // footprint, which is not the mistake this test is for: it is the west
        // approach, the south pack is stage 2 and dead before stage 3 can arm, and
        // there is no other floor on that side. A wider bar would fail an intentional
        // row and teach the next reader to widen it again.
        for (auto const& pack : RotundaPacks())
            for (auto const& m : pack)
                EXPECT_GT(Dist2d(m.first, m.second, s.standX, s.standY), 2.0f)
                    << "stage " << s.order << " stand spot is a mob's spawn";

        // Between the pack and the camp, not past it. The drag-back is a straight run
        // home; a spot on the far side of its own pack would make it a loop around
        // the thing being dragged.
        float const campToPack  = Dist2d(s.campX, s.campY, s.packX, s.packY);
        float const campToStand = Dist2d(s.campX, s.campY, s.standX, s.standY);
        EXPECT_LT(campToStand, campToPack)
            << "stage " << s.order << ": the stand spot is further from camp than the "
               "pack it is pulling";
    }
}

TEST(DcScriptedPullTest, RotundaCylindersHoldTheirOwnPackAndNothingElse)
{
    std::vector<ScriptedPullStage const*> const rows = RotundaPackRows();
    ASSERT_EQ(rows.size(), RotundaPacks().size());

    for (size_t i = 0; i < rows.size(); ++i)
    {
        ScriptedPullStage const& s = *rows[i];
        for (size_t j = 0; j < rows.size(); ++j)
            for (auto const& m : RotundaPacks()[j])
            {
                bool const in = ScriptedPullRegistry::InPack(*rows[i], m.first, m.second,
                                                             s.packZ);
                EXPECT_EQ(in, i == j)
                    << "stage " << s.order << " cylinder " << (in ? "holds" : "misses")
                    << " a member of pack " << j
                    << " — a stage that swallowed a neighbour would target the wrong "
                       "pack, and one that missed its own would never retire";
            }
    }
}

TEST(DcScriptedPullTest, TheBrokenSentinelPropIsNotARotundaPackMember)
{
    // Broken Sentinel 96948 (24808) is a hostile-faction prop on NullCreatureAI
    // sitting 0.7yd from the EAST pack's cylinder centre — the same trick Selin's fel
    // crystals play. Counting it would mean the east stage never reported its pack
    // cleared and the plan stopped there forever, with the party camp-held.
    //
    // The east pack is stage ORDER 5 — third of the rotunda's five packs, behind south
    // and centre, and behind the hall patrol that opens the plan.
    ScriptedPullStage const* const east = ScriptedPullRegistry::Find(MGT, 5);
    ASSERT_NE(east, nullptr);
    EXPECT_TRUE(ScriptedPullRegistry::InPack(*east, 151.23f, -131.86f, -21.04f))
        << "the prop is inside the cylinder — so the ENTRY filter is the only thing "
           "keeping it out, which is what the next assertion is for";

    for (ScriptedPullStage const* s : RotundaPackRows())
    {
        EXPECT_FALSE(ScriptedPullRegistry::IsPackEntry(*s, BROKEN_SENTINEL))
            << "stage " << s->order;
        EXPECT_FALSE(ScriptedPullRegistry::IsPackEntry(*s, DELRISSA)) << "stage " << s->order;
        // The rotunda packs SHARE entries, unlike Selin's — the cylinder does the
        // disambiguating and the entry list only keeps non-members out. So every row
        // carries the union, and a row missing one would silently under-count its pack.
        for (uint32 e : {MAGE_GUARD, BLOOD_KNGT, MAGISTER, WARLOCK, PHYSICIAN,
                         WITCH, SISTER, SMUGGLER})
            EXPECT_TRUE(ScriptedPullRegistry::IsPackEntry(*s, e))
                << "stage " << s->order << " is missing entry " << e;
    }
}

TEST(DcScriptedPullTest, TheRotundaArmGateClearsTheBackCampAndItsPatrol)
{
    // The rotunda's back camp is clear floor 57yd from the nearest pack, so unlike
    // Selin's it is not a live pack that forces the anchor forward — it is a PATROL.
    // Sunblade Sentinel 96945 walks path 969450 along the hall that camp stands in,
    // passing 5.6yd from it. An arm radius drawn at the back camp would cover the
    // patrol's whole east half, so a pack stage could arm off a tank standing next to
    // a live sentinel and pin the followers there, passive.
    //
    // The hall-patrol row is now the primary answer to that sentinel and this anchor
    // is the second one, which is worth keeping rather than collapsing: the ordering
    // gate stops the pack stages from being SELECTED, and the anchor stops them from
    // being candidates in the first place. Neither depends on the other being right.
    std::vector<ScriptedPullStage const*> const rows = RotundaPackRows();
    ASSERT_FALSE(rows.empty());

    for (ScriptedPullStage const* s : rows)
    {
        ASSERT_TRUE(s->HasArmAnchor()) << "stage " << s->order;
        // Standing anywhere on the patrol's path does not arm a pack stage...
        EXPECT_FALSE(ScriptedPullRegistry::InArmRange(*s, 137.00f, -214.83f, -21.34f))
            << "stage " << s->order << ": armed at the patrol's east end";
        EXPECT_FALSE(ScriptedPullRegistry::InArmRange(*s, 105.54f, -214.95f, -21.44f))
            << "stage " << s->order << ": armed at the patrol's west end";
        // ...including where it passes CLOSEST to the anchor, which is the point the
        // 20yd radius actually has to clear (24.9yd) — the endpoints flatter it.
        EXPECT_FALSE(ScriptedPullRegistry::InArmRange(*s, 127.50f, -214.90f, -21.40f))
            << "stage " << s->order << ": armed at the patrol's nearest approach";
        // ...and neither does the BACK camp, which sits on that same hall.
        EXPECT_FALSE(ScriptedPullRegistry::InArmRange(*s, 141.70f, -211.71f, -21.13f))
            << "stage " << s->order << ": armed from the back camp";
        // But stepping off it toward the room does, so the plan arms without the tank
        // having to walk into the room first.
        EXPECT_TRUE(ScriptedPullRegistry::InArmRange(*s, 130.0f, -195.0f, -21.27f))
            << "stage " << s->order << ": the neck does not arm the stage, so the plan "
               "can never start";
        // And the FORWARD camp is inside the circle, deliberately — that is what lets
        // a drag ending there re-arm the next stage with no dead walk between pulls.
        // It is only safe because the forward camp is not on the patrol's ground: it
        // stands 35.6yd off the nearest point of the path.
        EXPECT_TRUE(ScriptedPullRegistry::InArmRange(*s, 137.11f, -179.23f, -21.43f))
            << "stage " << s->order << ": a drag ending at the forward camp leaves the "
               "arm circle, so the next stage cannot arm without walking somewhere";
    }

    // The arm circle reaches no pack spawn at all: there is nothing inside it for an
    // armed plan to hijack the pull pipeline off.
    for (size_t i = 0; i < rows.size(); ++i)
        for (auto const& m : RotundaPacks()[i])
            EXPECT_FALSE(ScriptedPullRegistry::InArmRange(*rows[0], m.first, m.second,
                                                          -20.9f))
                << "a pack " << i << " member stands inside the arm circle";
}

TEST(DcScriptedPullTest, TheForwardCampOnlyServesRowsWhoseBlockersAreDead)
{
    // One camp for five packs meant hauls of 32 to 77yd. Nothing about that is unsafe
    // — instanced creatures do not distance-leash (Creature::CanCreatureAttack returns
    // true unconditionally once GetMap()->IsDungeon(), before the
    // CONFIG_CREATURE_LEASH_RADIUS home check) — but it is a minute of wall clock per
    // stage with the tank out of the healer's range for most of it. So the last three
    // rows haul to the small room instead.
    //
    // THE NUMBER THAT MAKES IT CONDITIONAL is 25.7yd: that is how close the forward
    // camp stands to south Sister of Torment 96843. A party told to hold there with
    // the south pack alive is a party in a fight it did not choose. So assert the
    // ordering that makes it moot rather than the distance that would forbid it.
    std::vector<ScriptedPullStage const*> const rows = RotundaPackRows();
    ASSERT_EQ(rows.size(), RotundaPacks().size());

    float constexpr kEliteReach = 21.0f;
    float constexpr kCasterRange = 40.0f;

    for (size_t i = 0; i < rows.size(); ++i)
    {
        ScriptedPullStage const& s = *rows[i];
        // Every pack still alive when this row runs — its own included, because the
        // party holds at the camp while the tank walks out to tag — must be clear of
        // the camp by more than an elite's reach AND by more than a castFlags-64
        // caster's 40yd, or the pull is answered by planting rather than by coming.
        for (size_t j = i; j < rows.size(); ++j)
            for (auto const& m : RotundaPacks()[j])
            {
                float const d = Dist2d(m.first, m.second, s.campX, s.campY);
                EXPECT_GT(d, kEliteReach)
                    << "stage " << s.order << " camp is inside pack " << j << "'s aggro";
                EXPECT_GT(d, kCasterRange)
                    << "stage " << s.order << ": a caster of pack " << j << " can reach "
                       "the camp, so it holds the fight open instead of being dragged";
            }
        // The tank's camp leash — what keeps a chase excursion from walking back into
        // the room during the camp fight — has to fit in that gap too, and the bar is
        // NOT "the excursion stops short of the pack". A tank standing at the far edge
        // of its leash is a tank that has spent the leash walking TOWARD something, so
        // what has to survive is the pack's reach measured from where the leash lets it
        // stand: dist - LEASH > kEliteReach, not dist > LEASH.
        //
        // The weaker form is what this assertion used to be. It would not have caught
        // the 12 -> 18 widening on its own — the caster-range bar above already forces
        // 40yd of clearance, which absorbs an 18yd excursion — but it states the wrong
        // property, and the leash is exactly the constant someone will reach for next.
        // In this geometry the strong form binds at a leash of about 19; on any future
        // camp placed inside caster range it binds immediately.
        for (size_t j = i; j < rows.size(); ++j)
            for (auto const& m : RotundaPacks()[j])
                EXPECT_GT(Dist2d(m.first, m.second, s.campX, s.campY) -
                              DC_SCRIPTED_PULL_LEASH,
                          kEliteReach)
                    << "a full leash excursion from stage " << s.order
                    << "'s camp puts the tank inside pack " << j << "'s aggro";
    }

    // And the moved camp does what it was moved for: the northern hauls come down
    // from 70+yd to under 60. Asserted as a ceiling rather than an exact figure, so a
    // re-measure of a yard does not fail it but a camp put back in the hall does.
    for (ScriptedPullStage const* s : rows)
        EXPECT_LT(Dist2d(s->campX, s->campY, s->standX, s->standY), 65.0f)
            << "stage " << s->order << ": the haul is back to its pre-forward-camp "
               "length";

    // The budgets that ride on that ground are still sized FROM it, not flat — that is
    // what made the old 77yd haul safe and it is what keeps this one safe.
    float longest = 0.0f;
    for (ScriptedPullStage const* s : rows)
        longest = std::max(longest, Dist2d(s->campX, s->campY, s->standX, s->standY));
    EXPECT_GT(longest, 40.0f) << "the geometry moved — re-check the budgets below";
    EXPECT_GT(ScriptedPullTravelBudgetMs(longest),
              DC_SCRIPTED_PULL_TRAVEL_BASE_MS +
                  static_cast<uint32>(longest / 8.0f * 1000.0f));
    // And it stays bounded — a follower that cannot path may not hold the run open.
    EXPECT_LT(ScriptedPullTravelBudgetMs(longest), 60000u);
}

TEST(DcScriptedPullTest, RotundaForwardCampHasNoSightLineIntoTheRoom)
{
    // The forward camp is against the small room's EAST WALL, not in the middle of it,
    // and this is the property that buys. A castFlags-64 caster (SMARTCAST_COMBAT_MOVE
    // — Magister Frostbolt, Warlock Immolate, Witch Forked Lightning) plants and shoots
    // only while its target is in range AND in line of sight; take either away and it
    // has to run, which is what the drag-back needs. The back camp takes the RANGE away
    // at the cost of a 58-85yd haul. The forward camp takes the LINE OF SIGHT away
    // instead, which is what lets it sit 27-48yd from the room.
    //
    // It HAS to: the range margin here is four yards (43.97 to east Physician 96824
    // against a 40yd reach) where the back camp had seventeen. So the sight-line is
    // load-bearing and gets asserted rather than assumed.
    //
    // Every line from this chamber into the rotunda has to thread the north neck, whose
    // floor at its narrowest (probed off the map-585 mmtiles at Y -170 and Y -173) spans
    // X 121.5-132.25. A line that leaves the chamber outside that span hits wall.
    float constexpr kNeckY = -173.0f;
    float constexpr kNeckX0 = 121.5f, kNeckX1 = 132.25f;

    // Where the camp-to-mob line crosses the neck plane, or nullopt if it never does
    // (the mob is south of it, i.e. in this chamber already).
    auto neckCrossing = [](ScriptedPullStage const& s, std::pair<float, float> const& m)
        -> std::pair<bool, float>
    {
        float const dy = m.second - s.campY;
        if (dy <= 0.0f)
            return {false, 0.0f};
        float const t = (kNeckY - s.campY) / dy;
        if (t <= 0.0f || t >= 1.0f)
            return {false, 0.0f};
        return {true, s.campX + (m.first - s.campX) * t};
    };

    std::vector<ScriptedPullStage const*> const rows = RotundaPackRows();
    ASSERT_EQ(rows.size(), RotundaPacks().size());

    bool sawForwardCamp = false;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        ScriptedPullStage const& s = *rows[i];
        // Only the rows that camp IN the chamber make this claim; the back camp is a
        // different room with a different argument (57yd of range).
        if (s.campY < -195.0f)
            continue;
        sawForwardCamp = true;

        // Every pack still alive when this row runs — including its own, because the
        // party holds at the camp while the tank walks out to tag it.
        for (size_t j = i; j < rows.size(); ++j)
            for (auto const& m : RotundaPacks()[j])
            {
                auto const [crosses, x] = neckCrossing(s, m);
                if (!crosses)
                    continue;
                EXPECT_TRUE(x < kNeckX0 || x > kNeckX1)
                    << "stage " << s.order << ": the camp has an open sight-line "
                       "through the neck (crossing X " << x << ") to a member of pack "
                    << j << " — a castFlags-64 caster there will plant and shoot "
                       "instead of being dragged, and the camp is inside 40yd";
            }
    }
    ASSERT_TRUE(sawForwardCamp) << "no row camps in the small room any more";

    // Stated as the thing that would silently undo it: the middle of the chamber, which
    // is where this camp started, DOES have an open line — so a re-measure that drifts
    // back toward the centre is a regression and not a rounding difference.
    ScriptedPullStage centred = *rows.back();
    centred.campX = 126.00f;
    centred.campY = -184.00f;
    bool anyOpen = false;
    for (auto const& pack : RotundaPacks())
        for (auto const& m : pack)
        {
            auto const [crosses, x] = neckCrossing(centred, m);
            if (crosses && x >= kNeckX0 && x <= kNeckX1)
                anyOpen = true;
        }
    EXPECT_TRUE(anyOpen)
        << "the neck no longer blocks anything from the middle of the room, so this "
           "test is not measuring what it thinks it is";
}

TEST(DcScriptedPullTest, TheEastRowTagsAwayFromTheNorthEastPack)
{
    // East and north-east come together and cannot be made not to. East Mage Guard
    // 96774 (146.79, -125.14, reach 1.8) and north-east Ethereum Smuggler 96849
    // (144.71, -113.34, reach 1.25) are 12.00yd apart, 8.95 once
    // AnyAssistCreatureInRangeCheck subtracts both radii, against
    // CreatureFamilyAssistanceRadius = 10 — and Creature::AtEngage engages every member
    // of a groupAI-3 formation at its own SPAWN, each calling CallAssistance() from
    // there before anything has moved. Tag any east mob and 96774 calls 96849; tag any
    // north-east mob and 96849 calls 96774. Symmetric, so reordering does not help.
    //
    // What is left to choose is where the joint fight starts, and that is what the
    // avoid anchor decides: the east row tags the member FURTHEST from the north-east
    // pack, so the east formation is at the camp and being fought before the
    // north-east one finishes its run.
    ScriptedPullStage const* const east = ScriptedPullRegistry::Find(MGT, 5);
    ScriptedPullStage const* const ne   = ScriptedPullRegistry::Find(MGT, 6);
    ASSERT_NE(east, nullptr);
    ASSERT_NE(ne, nullptr);

    // The anchor is the north-east pack's own cylinder centre, not a hand-picked spot
    // near it — so moving that row moves this one's aim with it.
    ASSERT_TRUE(east->HasAvoidAnchor());
    EXPECT_FLOAT_EQ(east->avoidX, ne->packX);
    EXPECT_FLOAT_EQ(east->avoidY, ne->packY);
    EXPECT_FLOAT_EQ(east->avoidZ, ne->packZ);

    // Exactly one row carries one. A row that named a neighbour it did NOT have to
    // wake would be trading reach for nothing.
    size_t anchored = 0;
    for (ScriptedPullStage const* s : ScriptedPullRegistry::Rows(MGT))
        if (s->HasAvoidAnchor())
            ++anchored;
    EXPECT_EQ(anchored, 1u);

    // It picks Physician 96824 (152.72, -138.12) — 31.1yd from the north-east
    // centroid, the furthest of the four, and 2.7yd clear of the runner-up, so the
    // choice is stable rather than a coin flip between two near-equal candidates.
    std::vector<std::pair<float, float>> const& pack = RotundaPacks()[2];
    std::pair<float, float> const pick = TagTarget(*east, pack);
    EXPECT_NEAR(pick.first, 152.72f, 0.01f);
    EXPECT_NEAR(pick.second, -138.12f, 0.01f);

    float best = 0.0f, second = 0.0f;
    for (auto const& m : pack)
    {
        float const d = Dist2d(m.first, m.second, east->avoidX, east->avoidY);
        if (d > best) { second = best; best = d; }
        else if (d > second) { second = d; }
    }
    EXPECT_GT(best - second, 2.0f)
        << "two east members are near-equally far from the north-east pack, so the "
           "tag is a coin flip and a re-measure can silently swap it";

    // And the pick is genuinely the far side: the member the DEFAULT ranking would
    // have taken is no closer to the north-east pack than this one.
    float nearestToStand = 1e9f;
    std::pair<float, float> defaultPick = pack.front();
    for (auto const& m : pack)
    {
        float const d = Dist2d(m.first, m.second, east->standX, east->standY);
        if (d < nearestToStand) { nearestToStand = d; defaultPick = m; }
    }
    EXPECT_GE(Dist2d(pick.first, pick.second, east->avoidX, east->avoidY),
              Dist2d(defaultPick.first, defaultPick.second,
                     east->avoidX, east->avoidY));

    // The anchor must not have bought the head start by pushing the tag point into
    // something else — the clearance assertions still have to hold, and they do
    // (RotundaBodyTagPointsClearEveryStillLivePack runs on the same TagTarget).
    EXPECT_LT(Dist2d(pick.first, pick.second, east->standX, east->standY), 30.0f)
        << "the furthest-from-the-neighbour member is out of walk-in range of the "
           "stand spot";
}

TEST(DcScriptedPullTest, TheEastRotundaRowTagsAtRangeFromTheStandSpot)
{
    // The east row is the rotunda's one ranged row, and this is the geometry that has
    // to hold for that to be legal. The WHY is in the row above and in the dossier:
    // north-east is engaged at T=0 by Mage Guard 96774's CallAssistance from its own
    // spawn (12.00yd to Ethereum Smuggler 96849, against a 13.05yd limit once both
    // combat reaches are added to CreatureFamilyAssistanceRadius), so no pull style can
    // separate the two packs and the body pull's 8yd walk-in buys nothing. What it
    // costs is real: the tank ends 8yd deeper into a room holding two more live packs.
    //
    // So the row keeps the distance and takes the tag from the spot. Three things have
    // to be true for that, and none of them is true by construction.
    ScriptedPullStage const* const east = ScriptedPullRegistry::Find(MGT, 5);
    ASSERT_NE(east, nullptr);
    ASSERT_FALSE(east->bodyPull);

    std::vector<std::pair<float, float>> const& pack = RotundaPacks()[2];
    std::pair<float, float> const pick = TagTarget(*east, pack);

    // ONE: the opener has to reach the member the row actually tags — not merely the
    // nearest one. Both the class table and the ranged-weapon fallback
    // (ResolveRangedWeaponPull) resolve to an 8-30yd band, and the avoid anchor picks
    // the member FURTHEST from the neighbour, which is the one most likely to fall out
    // the far end of it. It is Physician 96824 at 25.8yd, so the band holds with 4yd to
    // spare. A re-measure that pushes the spot back for more distance spends that 4.
    float const toTag = Dist2d(pick.first, pick.second, east->standX, east->standY);
    EXPECT_GT(toTag, 8.0f)
        << "the tag target is inside the opener's minimum range, so the cast fails and "
           "the row falls through to the body pull it is trying not to take";
    EXPECT_LT(toTag, 30.0f)
        << "the tag target is out of the opener's 30yd reach, so this row cannot tag "
           "at all and will burn its whole leg budget standing on the spot";
    EXPECT_NEAR(toTag, 25.83f, 0.1f);

    // TWO: standing there must not wake anything by proximity. A ranged row holds the
    // spot for the whole tag rather than passing through it, so the 21yd elite reach
    // has to clear the two packs that are still alive at stage 5 by a real margin —
    // north-east at 45.9yd and north-west at 53.1yd, not by a yard.
    float constexpr kEliteReach = 21.0f;
    for (size_t j = 3; j <= 4; ++j)
    {
        float nearest = 1e9f;
        for (auto const& m : RotundaPacks()[j])
            nearest = std::min(nearest,
                               Dist2d(m.first, m.second, east->standX, east->standY));
        EXPECT_GT(nearest, kEliteReach + 15.0f)
            << "pack " << j << " is close enough to the east stand spot that holding "
               "it for a ranged tag is a second pull";
    }

    // THREE: the drag has to be worth taking. The two packs come together, so the only
    // thing left to buy is arrival ORDER, and that is bought by haul length from the
    // forward camp: east ~44yd, north-east ~66yd. The east formation is at the camp and
    // being fought while the north-east one is still running. If those ever converge,
    // the avoid anchor has stopped doing its job and the row is just a double pull.
    float const eastHaul = Dist2d(pick.first, pick.second, east->campX, east->campY);
    float neHaul = 1e9f;
    for (auto const& m : RotundaPacks()[3])
        neHaul = std::min(neHaul, Dist2d(m.first, m.second, east->campX, east->campY));
    EXPECT_GT(neHaul - eastHaul, 15.0f)
        << "the north-east pack reaches the camp within 15yd of the east pack's haul, "
           "so the two formations arrive together and the head start is gone";
}
