/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <optional>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBossOrdering.h"

// Golden table for the boss-ordering kernel behind two shipped regressions
// (mid-list kill backtracking to boss #1; same-index siblings stranded).
// PickTarget is pure — encounter-ordered candidates + committed identity in,
// chosen candidate out — so every behavior is pinned here without game state.

namespace
{
    // Candidate factory. `order` < 0 leaves orderOverride at -1 (order by
    // encounterIndex); >= 0 sets an explicit clear-order override.
    DungeonBossInfo B(uint32 entry, uint32 encounterIndex, int32 order = -1)
    {
        DungeonBossInfo b;
        b.entry = entry;
        b.encounterIndex = encounterIndex;
        b.orderOverride = order;
        return b;
    }

    // Convenience: PickTarget with no prior commit.
    std::optional<DungeonBossInfo> Fresh(std::vector<DungeonBossInfo> const& cands)
    {
        return DcBossOrdering::PickTarget(cands, /*stickyEntry*/ 0, /*idx*/ 0,
                                          /*haveStickyIndex*/ false);
    }

    // Convenience: PickTarget holding a prior commit at (entry, orderKey).
    std::optional<DungeonBossInfo> Held(std::vector<DungeonBossInfo> const& cands,
                                        uint32 stickyEntry, uint32 stickyOrderKey)
    {
        return DcBossOrdering::PickTarget(cands, stickyEntry, stickyOrderKey,
                                          /*haveStickyIndex*/ true);
    }
}

// --- empty / fresh --------------------------------------------------------

TEST(DcBossOrderingTest, EmptyCandidatesReturnsNullopt)
{
    EXPECT_FALSE(DcBossOrdering::PickTarget({}, 0, 0, false).has_value());
    EXPECT_FALSE(DcBossOrdering::PickTarget({}, 111, 5, true).has_value());
}

TEST(DcBossOrderingTest, FreshSelectionTakesLowestIndex)
{
    // cands arrive in encounter order; the fresh pick is always the front.
    std::vector<DungeonBossInfo> cands{B(10, 0), B(20, 1), B(30, 2)};
    auto pick = Fresh(cands);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->entry, 10u);
}

// --- commit-and-hold ------------------------------------------------------

TEST(DcBossOrderingTest, CommitHeldWhileStickyStillPresent)
{
    // Sticky boss 20 is still a candidate: return it unchanged, no re-rank to
    // the lowest-index survivor.
    std::vector<DungeonBossInfo> cands{B(10, 0), B(20, 1), B(30, 2)};
    auto pick = Held(cands, /*stickyEntry*/ 20, /*orderKey*/ 1);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->entry, 20u);
}

TEST(DcBossOrderingTest, CommitHeldEvenWhenLowerIndexSurvivorExists)
{
    // A lower-index boss (10) is still uncleared, but we committed to 20 — hold
    // it. (Party started mid-list; do not backtrack.)
    std::vector<DungeonBossInfo> cands{B(10, 0), B(20, 1), B(30, 2)};
    auto pick = Held(cands, 20, 1);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->entry, 20u);
}

// --- advance-forward (the mid-list backtrack regression) ------------------

TEST(DcBossOrderingTest, AdvanceForwardAfterStickyKilledPicksNextHigher)
{
    // Committed to 20 (index 1); it was killed and dropped from cands. Advance
    // to the next index AFTER it (30/idx2), never back to 10/idx0.
    std::vector<DungeonBossInfo> cands{B(10, 0), B(30, 2)};
    auto pick = Held(cands, /*stickyEntry*/ 20, /*orderKey*/ 1);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->entry, 30u) << "must advance forward, not backtrack to boss #1";
}

TEST(DcBossOrderingTest, WrapAroundWhenNothingAheadMopsUpLowerIndex)
{
    // Committed to the last boss (30/idx2), killed. Nothing ahead of index 2, so
    // wrap to mop up a lower-index boss left behind (10/idx0).
    std::vector<DungeonBossInfo> cands{B(10, 0)};
    auto pick = Held(cands, /*stickyEntry*/ 30, /*orderKey*/ 2);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->entry, 10u);
}

// --- same-index siblings (Sunken Temple forcefields) ----------------------

TEST(DcBossOrderingTest, SameIndexSiblingFinishedBeforeAdvancing)
{
    // Two siblings share index 0 (a gate expressed as two anchors). Committed to
    // 10; it dropped. Finish the remaining index-0 sibling (11) before advancing
    // to the higher-index boss (20/idx1).
    std::vector<DungeonBossInfo> cands{B(11, 0), B(20, 1)};
    auto pick = Held(cands, /*stickyEntry*/ 10, /*orderKey*/ 0);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->entry, 11u) << "same-index sibling must be cleared before advancing";
}

TEST(DcBossOrderingTest, AdvancesPastIndexOnceSiblingsExhausted)
{
    // Same as above but the index-0 sibling is already gone: advance to idx1.
    std::vector<DungeonBossInfo> cands{B(20, 1), B(30, 2)};
    auto pick = Held(cands, /*stickyEntry*/ 10, /*orderKey*/ 0);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->entry, 20u);
}

// --- orderOverride keys ordering (Stratholme Barthilas-before-ziggurats) ---

TEST(DcBossOrderingTest, AdvanceUsesOrderKeyNotRawEncounterIndex)
{
    // Barthilas carries DBC bit 10 but an orderOverride of 8 so it clears BEFORE
    // the ziggurats. We just left orderKey 7. `cands` is in ORDER-KEY order, and
    // the advance must key on BossOrderKey (8 -> Barthilas), not the raw
    // encounterIndex (which would rank the ziggurat's 9 below Barthilas's 10).
    std::vector<DungeonBossInfo> cands{
        B(/*entry*/ 100, /*encIdx*/ 10, /*order*/ 8),   // Barthilas: order key 8
        B(/*entry*/ 200, /*encIdx*/ 9,  /*order*/ 9)};  // ziggurat:  order key 9
    auto pick = Held(cands, /*stickyEntry*/ 50, /*orderKey*/ 7);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->entry, 100u) << "advance must key on BossOrderKey, not encounterIndex";
}
