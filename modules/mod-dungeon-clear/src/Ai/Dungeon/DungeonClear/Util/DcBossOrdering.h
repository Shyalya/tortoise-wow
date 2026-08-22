/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCBOSSORDERING_H
#define _PLAYERBOT_DCBOSSORDERING_H

#include <optional>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"

// Pure boss-ordering kernel, hoisted out of NextDungeonBossValue's anonymous
// namespace so its four behaviors (commit-and-hold, same-index-sibling
// completion, advance-forward, wrap-around) are unit-testable. It takes only the
// encounter-ordered candidate list plus the committed boss's identity/order key
// — no game types — so a golden table can pin the logic behind at least two
// shipped regressions (mid-list kill backtracking to boss #1; not-yet-spawned
// next boss skipped for a far spawned one — memory `dc-boss-ordering-fixes`).
// The game-state resolution (candidate filtering by kill-mask / skip / corpse,
// sticky-order lookup) stays in NextDungeonBossValue::Calculate.
namespace DcBossOrdering
{
    // From the encounter-ordered candidate list, pick the boss to head toward.
    // `stickyEntry` is the boss chosen on the previous computation;
    // `stickyEncounterIndex` is its order key (BossOrderKey) when `haveStickyIndex`
    // (the sticky boss is still known in the "dungeon bosses" list, even if it's
    // now dead and absent from `cands`).
    //
    // COMMIT-AND-HOLD. Once we've committed to a boss we return it unchanged for
    // as long as it remains in the candidate list — no per-tick re-ranking by
    // distance, no per-tick reachability re-probe. Both signals are noisy near a
    // decision boundary: straight-line distance swings as the bot rounds corners,
    // and the bounded 2-stride IsReachable probe flickers when the bot is wedged.
    // Re-deciding on them every recompute makes the target (and the long-path
    // rebuilt from it) flip-flop between two bosses, which wedges the bot between
    // competing routes. The commit releases only when the boss leaves `cands`
    // entirely — killed (mask bit set), skipped, or turned to a corpse — all
    // handled by the caller's candidate filtering, so a gone boss simply isn't
    // here to match.
    //
    // ADVANCE-FORWARD. When the commit releases (the boss we were heading to is
    // gone), we head to the next boss *after* it in encounter order, not to the
    // lowest-index survivor. Snapping to the lowest index is correct only when the
    // party clears strictly from boss #1; a party that started mid-list (e.g. via
    // a manual `dungeon clear boss #3` selection, or by skipping early bosses)
    // would otherwise walk back to boss #1 on every kill. We take the candidates
    // with encounter index strictly greater than the boss we just left and pick
    // the lowest-index one; only if nothing remains ahead do we wrap to the full
    // set to mop up any lower-index bosses left behind.
    //
    // SAME-INDEX SIBLINGS. Before advancing past the index we just left, finish
    // any remaining anchors that SHARE it — a single gate can be expressed as
    // several same-index objectives (Sunken Temple's six forcefield defenders all
    // sit at index 0, each on its own balcony needing its own boss-nav anchor).
    // The old strict-greater advance picked the next HIGHER index and stranded the
    // siblings. `cands` is in clear order, so the first equal-index match is the
    // next sibling to clear.
    //
    // STRICTLY ORDINAL — NO REACHABILITY RE-RANK. We pick by encounter index alone
    // and never skip a lower-index boss because a bounded path probe reads it
    // "unreachable" (that probe tests the boss's STATIC spawn anchor, arbitrary
    // for pool-spawn bosses like the Wailing Caverns Disciples). Advance walks
    // toward the chosen boss using its LIVE coords, and a boss that genuinely
    // can't be reached is handled by Advance's stall -> manual `dc skip` path.
    inline std::optional<DungeonBossInfo> PickTarget(std::vector<DungeonBossInfo> const& cands,
                                                     uint32 stickyEntry,
                                                     uint32 stickyEncounterIndex,
                                                     bool haveStickyIndex)
    {
        if (cands.empty())
            return std::nullopt;

        // Hold the commit if the boss is still in the candidate list.
        if (stickyEntry)
            for (DungeonBossInfo const& info : cands)
                if (info.entry == stickyEntry)
                    return info;

        // `cands` arrives in encounter-index order, so the first match is always
        // the lowest-index one.

        if (haveStickyIndex)
        {
            // Finish any remaining anchors that SHARE the index we just left
            // before advancing past it.
            for (DungeonBossInfo const& info : cands)
                if (BossOrderKey(info) == stickyEncounterIndex)
                    return info;

            // Commit released and the shared slot is done: head to the
            // lowest-index boss strictly after the one we just left.
            for (DungeonBossInfo const& info : cands)
                if (BossOrderKey(info) > stickyEncounterIndex)
                    return info;
        }

        // Fresh selection (no prior commit) or wrap-around (nothing ahead):
        // lowest-index survivor.
        return cands.front();
    }
}

#endif
