/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCACTIONSHARED_H
#define _PLAYERBOT_DCACTIONSHARED_H

// Cross-family helper layer for the DungeonClear action files. When the single
// 4,600-line DungeonClearActions.cpp was split along its four family seams
// (advance / engage / pull / follower), the anonymous-namespace helpers and
// tuning constants that more than one family used could no longer be file-local.
// They live here in namespace DcActionShared, declared once and defined in
// DcActionShared.cpp; each family .cpp does `using namespace DcActionShared;`
// so the call sites read exactly as they did before the split.

#include <optional>
#include <string>

#include "Define.h"

class PlayerbotAI;
class Player;
class Unit;
namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
class DcApproachState;
struct DungeonBossInfo;

namespace DcActionShared
{
    // --- Shared tuning constants (used by >=2 families) --------------------

    // COMBAT-priority movement can't be interrupted by the bot's own combat
    // reflexes (see DungeonClearAdvanceAction). Only use it for the final
    // approach within this distance of attack range; past that, approach at
    // NORMAL so the tank stops to fight packs it aggros en route instead of
    // plowing through them to a locked target.
    inline constexpr float DC_COMBAT_APPROACH_RANGE = 10.0f;

    // Long-path cache TTL. Most bosses hold a fixed position, so a longer TTL
    // is safe; rebuild costs are bounded (~8 PathGenerator calls × sub-ms each).
    // Keeping the TTL short keeps stale paths from outlasting edge cases like
    // portal traversal or stuck-teleport recovery. A boss that relocates within
    // the TTL is handled out-of-band by DC_LONG_PATH_RETARGET_DIST.
    inline constexpr uint32 DC_LONG_PATH_TTL_MS = 15 * 1000;

    // How far the effective boss target may drift from the position the cached
    // long-path was built toward before EnsureLongPath forces an early rebuild,
    // ahead of the TTL. Advance feeds the boss's LIVE position when it is
    // loaded, so a pool-spawn / wandering boss (e.g. the Wailing Caverns
    // Disciples, which spawn at one of several pooled anchors and rarely sit on
    // the one BossSpawnIndex picked) gets a route to where it actually is — and
    // the moment its live position streams in far from the static anchor the
    // first build used, the path retargets instead of walking the wrong way for
    // the full TTL ("goes the wrong direction, then says the way is blocked").
    // Above minor patrol jitter so a pacing boss doesn't thrash rebuilds; the
    // direct-pursuit shortcut already handles the close-range (<=80yd, LOS)
    // tracking, so this only governs the long-range approach.
    inline constexpr float DC_LONG_PATH_RETARGET_DIST = 15.0f;

    // Self-healing watchdog for an in-flight async path job. A real
    // BuildCoreFromMesh is a single Detour query — sub-millisecond, well under
    // one tick — so if a submitted job has produced NO mailbox result after
    // this long, the result was lost (e.g. swept by the reaper while the bot
    // was afk with dc off, since the reset paths don't all clear the pending
    // jobId) or the worker thread is wedged/dead. Either way, abandon the
    // pending job and rebuild synchronously so the bot can never wedge forever
    // on "plotting route". Must stay well below DC_ASYNC_PATH_RESULT_TTL_MS (the
    // 30s mailbox sweep) so a genuinely-completed result is collected via
    // TryTake before this fires, and far above any real build time.
    inline constexpr uint32 DC_ASYNC_PATH_PENDING_TIMEOUT_MS = 5 * 1000;

    // How far the bot may have moved from the position an async path job was
    // SUBMITTED from before the finished result is stale-discarded at drain
    // instead of installed. A pending window is sub-second, so honest movement
    // stays a few yards; only a relocation the polyline can't know about — a
    // TeleportParty landing, an event repositioning the leader — exceeds this.
    // Installing such a route re-enters it with a straight opening spline back
    // toward the pre-relocation start and the tank sprints the wrong way (the
    // post-Brazen wrong-direction run in Old Hillsbrad). Discarding falls
    // through to an immediate resubmit from the live position.
    inline constexpr float DC_ASYNC_PATH_START_DRIFT_MAX = 40.0f;

    // Commit-timeout for the loot yield, shared by the tank's advance yield and
    // the follower's follow-tank yield. After a pull the tank holds until the
    // WHOLE party has finished looting (see the advance loot-yield block), and
    // each follower steps off follow to walk in to its own corpses — both for
    // at most this long. Past it the tank force-advances toward the next boss
    // and followers resume following, instead of the party parking forever on a
    // corpse it can't finish (group-loot rolls pending, bags full, un-pickable).
    // 15s comfortably covers several members each walking in from lootDistance
    // (15yd / ~7yd/s ≈ 2s) and grabbing multiple items, while still bounding a
    // wedge. This is the "reasonable timeout, then move on" window.
    inline constexpr uint32 DC_LOOT_YIELD_TIMEOUT_MS = 15 * 1000;

    // How long a loot the bot abandoned (its yield above timed out on it) stays
    // on the per-corpse give-up list. While listed, DungeonClearUtil::Strip-
    // SkippedLoot keeps it out of both the loot flags and stock's nearest-target
    // pick, so the bot walks away (follow/advance) instead of re-committing to
    // it the instant it drifts back within lootDistance — the corpse<->tank /
    // chest<->path ping-pong. Long enough that the party has moved on by the
    // time it lapses; short enough to retry once in case a pending group roll
    // resolved in the meantime.
    inline constexpr uint32 DC_LOOT_GIVEUP_TTL_MS = 60 * 1000;

    // Camp cutoff: how long the bot may stand within interaction range of ONE
    // plain corpse (can-loot true) before we treat its loot as un-finishable and
    // skip it (see DcLootPolicy::MaybeGiveUpCampedLoot). Unlike the yield
    // timeout above — which budgets for walking in from lootDistance — this
    // clock starts only once the bot has arrived, where a real pickup resolves
    // in a tick or two. Bots park on corpses whose loot they can never take
    // (group-roll items pending a real player's roll, items reserved for others,
    // bags full); without this they burned the full 15s yield timeout on each
    // such corpse, and the tank — which holds its advance while any follower is
    // looting — stalled the whole party for it. 3s clears a normal auto-loot
    // comfortably while cutting the dead time on a stuck corpse 5x.
    inline constexpr uint32 DC_LOOT_CAMP_TIMEOUT_MS = 3 * 1000;

    // Optional pre-attack ranged pull. Fire one class-appropriate cast at
    // the engage target before the existing auto-attack engagement runs.
    // If the cast lands, the tank gets the threat lead that auto-attack
    // alone never provides. If anything fails (cooldown, silenced, target
    // out of range, spell not known) we just fall through to auto-attack
    // — the existing reliable path is never replaced. Set to false to
    // restore previous behavior exactly.
    inline constexpr bool  DC_TRY_PULL_SPELL = true;
    // Tight settle tolerance to a follower's individual (fuzzed) camp slot. Much
    // smaller than the shared hold radius so the bot actually travels the last
    // 1-2yd onto its distinct slot — otherwise it would park the instant it
    // crossed the 4yd shared radius and the per-bot variance would never show.
    // The slot is navmesh-validated and players don't physically block one
    // another, so settling this close is always reachable.
    inline constexpr float DC_PULL_SLOT_RADIUS = 1.0f;

    // --- Shared helpers (used by >=2 families) -----------------------------


    // The class opener resolved to a concrete spell the BOT has actually LEARNED.
    struct ResolvedPullSpell
    {
        uint32 spellId;
        float  minRange;
        float  maxRange;
    };

    // Resolve the per-class pull opener to a spell id the TANK has trained, or
    // nullopt. Used by the engage walk-in (EngageDirect) and the advanced pull.
    std::optional<ResolvedPullSpell> ResolvePullSpell(PlayerbotAI* botAI, Player* bot);

    // The one success reason: DisableDungeonClear called with exactly this
    // string means the run ended because every boss died. Shared so the
    // all-cleared action and the `.dc test` harness classifier can never
    // drift apart on the wording.
    inline constexpr char kReasonAllCleared[] = "All bosses cleared!";

    // Tear the run down: clears every "dungeon clear *" value and announces the
    // reason. Mode goes disabled. Every run-end path funnels through here, so
    // this is also where the `.dc test` harness observes the run ending (see
    // DcTestRunManager::OnRunDisabled — a no-op outside a monitored test run).
    void DisableDungeonClear(PlayerbotAI* botAI, std::string const& reason);

    // Mode stays enabled; set a stall reason the fallback trigger / `dc status`
    // read, announced once per reason change.
    void StallDungeonClear(PlayerbotAI* botAI, std::string const& reason);

    // Clear the stall + last-said reason.
    void ClearStall(AiObjectContext* ctx);

    // Record the navigation micro-activity token for the addon status poll.
    void SetPhase(AiObjectContext* ctx, std::string const& phase);

    // Party is gathered + rested + no outstanding loot: safe to start the next
    // pull / engage the next boss.
    bool IsBetweenPullsReady(Player* bot, AiObjectContext* context);

    // Build (or refresh) the long-range route to `target` and cache it on the
    // approach state; the advance ladder and the engage fallbacks both call it.
    void EnsureLongPath(Player* bot, AiObjectContext* ctx, DcApproachState& appr,
                        DungeonBossInfo const& target);

    // Turn the bot to face `unit` if it is not already roughly facing it.
    void DcFaceIfNeeded(Player* bot, Unit* unit);

    // Is the bot standing in a damaging ground effect right now?
    //
    // Every rung that RECALLS a bot to a fixed point has to ask this first, because a
    // recall and a step-out are two correct rungs that can want incompatible places:
    // the step-out has to leave the effect, the recall has to return to the camp, and
    // when the effect is ON the camp neither can yield. The recall is the one that
    // gives, on the simple grounds that the step-out is answering damage.
    //
    // Reads the stock "area debuff" value, which returns the negative dynamic-object
    // aura the bot CURRENTLY HAS — so it is false the moment the bot is clear, and
    // the recall re-arms by itself one tick later.
    bool DcInGroundEffect(AiObjectContext* ctx);

    // A PULL MANEUVER OWNS THE TANK — the ACTION-side half of the stand-down the
    // route triggers (DungeonClearIdleTrigger / DungeonClearAtBossTrigger) already
    // perform. Call it at the top of any rung whose destination is the ROUTE (the
    // next boss), before it issues movement; it returns true when that rung must
    // stand down, and logs why.
    //
    // It has to be re-asked here because a trigger cannot un-queue an action. The
    // engine's queue is a member and survives ticks: a basket is pushed when its
    // trigger fires and is consumed only when its action is finally popped —
    // which, while a pull maneuver runs, it is not. The pull rung (relevance 35)
    // executes and BREAKS the action loop every tick, so a route basket pushed on
    // the tick the pull committed just sits there, outliving the trigger that
    // pushed it.
    //
    // It gets its turn on the first tick BOTH pull rungs go dark, and there is
    // exactly one such tick per pull: the one right after a RANGED tag. Combat has
    // started, so DungeonClearPullTrigger (non-combat) returns !IsInCombat() ==
    // false; the bot has not been flipped onto the combat engine yet (engine
    // transitions are action-driven, not derived from the combat flag), so
    // DungeonClearPullManeuverTrigger cannot run either. The stale route basket is
    // then the top live rung, and what it does with the tick is drive the tank at
    // the boss — forward into the room the pull is dragging out of.
    //
    // Live (tr-20260803-194800-14, Magisters' Terrace, Selin's east guard pack):
    //   19:52:08 advanced-pull: ranged tag spell 16857 at 28.8yd
    //   19:52:08 advance window: leg 0 violates bystander sphere ... (r=28.1)
    //            -> too close to honour, gliding 10 -> 10 pts
    //   19:52:08 spline issued: 10 pts, 37.3yd, delay=5000ms,
    //            from=(210.9,9.1,-2.8) to=(242.7,19.7,-0.8)
    //   19:52:09 advanced-pull: aggro confirmed at 44.7yd from camp -> dragging
    //   19:52:15 scripted-pull: drag lost ground to camp (26.2yd vs best 22.6)
    //            — something else is driving the tank
    // The window it glided through was KNOWN to cross a bystander's aggro sphere
    // and was honoured anyway ("too close to honour") — which is what wakes the
    // packs the plan exists to leave alone, and is the reported "runs forward into
    // the room aggroing everything". Exactly ONE advance execution, on exactly the
    // tick both pull rungs were dark: the signature of a queued basket, not of a
    // trigger passing. S1448 closed the trigger and the same spline came back,
    // because the trigger was never the way in.
    //
    // Mirrors BOTH of the triggers' gates, not just the scripted one: the tag ->
    // drag handoff window is identical for an ordinary advanced pull and for a
    // pull-back, and so is the damage a boss-bound glide does inside it.
    bool PullOwnsTheTank(Player* bot, AiObjectContext* ctx, char const* rung);
}  // namespace DcActionShared

#endif
