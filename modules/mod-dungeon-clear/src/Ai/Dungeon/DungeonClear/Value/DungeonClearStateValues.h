/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARSTATEVALUES_H
#define _PLAYERBOT_DUNGEONCLEARSTATEVALUES_H

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "G3D/Vector3.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/DcRunState.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;

// The leader-owned run-level state — enabled flag, pause cluster (paused flag +
// reason phrase + auto-paused door), the manual selected-boss override, and the
// two cross-bot leader-fight latches — consolidated into one owned struct
// (DcRunState, see DcRunState.h) so the whole run resets in lockstep through
// exactly one Reset() (and OnResume() for the pause cluster). This replaced five
// loose ManualSetValue globals ("dungeon clear enabled / paused / pause reason /
// paused door / selected boss") plus the two file-static latch maps
// (g_leaderCombatSince / g_partyEngagedLatch, each with its own mutex) whose
// resets were hand-replicated across DisableDungeonClear and the DcOn/Off/Skip/
// Go/Resume clusters — the "stale latch survives pause/skip/resume" bug class.
// Leader-owned; followers read `enabled`/`paused` cross-context via the leader's
// copy (DcLeaderSignal). Access through DcRun::Of (Util/DcRun.h). Reset on
// dc on / dc off / death / cleared; OnResume on pause resume + dc on.
class DungeonClearRunStateValue : public ManualSetValue<DcRunState&>
{
public:
    DungeonClearRunStateValue(PlayerbotAI* botAI)
        : ManualSetValue<DcRunState&>(botAI, data, DcKey::RunState)
    {
    }

    void Reset() override { data.Reset(); }

private:
    DcRunState data;
};

class DungeonClearSkippedValue : public ManualSetValue<std::unordered_set<uint32>&>
{
public:
    DungeonClearSkippedValue(PlayerbotAI* botAI)
        : ManualSetValue<std::unordered_set<uint32>&>(botAI, data, DcKey::Skipped)
    {
    }

private:
    std::unordered_set<uint32> data;
};

// Travel-objective anchors (BossRosterRegistry, DungeonAnchorKind::Objective)
// that have been completed this run. Unlike bosses, objectives have no kill-bit
// to read, so DcObjectiveArriveAction latches the anchor's entry here on
// arrival; NextDungeonBossValue filters these out so the clear advances and the
// objective never re-targets (even after the bot moves away). Reset on
// dc on / dc off / death / cleared alongside the skipped set so a fresh run
// starts clean.
class DungeonClearClearedAnchorsValue : public ManualSetValue<std::unordered_set<uint32>&>
{
public:
    DungeonClearClearedAnchorsValue(PlayerbotAI* botAI)
        : ManualSetValue<std::unordered_set<uint32>&>(botAI, data, DcKey::ClearedAnchors)
    {
    }

    void Reset() override { data.clear(); }

private:
    std::unordered_set<uint32> data;
};

// Boss entries that have been observed alive on the map at least once during
// the current run. This is what lets the status report tell a boss that "was
// alive and is now gone" (missing) apart from one we simply haven't reached
// yet — a boss in a far grid that has never loaded looks identical to a
// vanished one (neither is in the creature store), so without a memory of what
// we've actually seen alive, every unreached boss would falsely read as
// missing. Populated by DcBossesAction whenever a boss is found alive; cleared
// on `dc on` alongside the skipped set so a fresh run starts with a clean slate.
class DungeonClearSeenBossesValue : public ManualSetValue<std::unordered_set<uint32>&>
{
public:
    DungeonClearSeenBossesValue(PlayerbotAI* botAI)
        : ManualSetValue<std::unordered_set<uint32>&>(botAI, data, DcKey::SeenBosses)
    {
    }

    void Reset() override { data.clear(); }

private:
    std::unordered_set<uint32> data;
};

// Conditional events (DungeonEventRegistry) that have been observed DUE (their
// gating condition read true) at least once this run. Some conditional events
// are latched by their own gating condition rather than a ConditionalLatchKey —
// e.g. a Stratholme ziggurat's instance data flips 1 ("acolytes up") -> 2
// ("chamber cleared") the instant the Ash'ari Crystal topples, which is during
// combat, before the dormant in-combat executor can run its completion tick and
// latch the event normally. Such an event would otherwise read pending forever
// on the panel. SweepCompletedConditionalEvents remembers here every event seen
// due, so when one later reads not-due it can tell genuine completion ("was due,
// now isn't") apart from not-yet-started ("never been due"). Reset on
// dc on / instance change alongside the cleared/skipped sets.
class DungeonClearSeenDueEventsValue : public ManualSetValue<std::unordered_set<uint32>&>
{
public:
    DungeonClearSeenDueEventsValue(PlayerbotAI* botAI)
        : ManualSetValue<std::unordered_set<uint32>&>(botAI, data, DcKey::SeenDueEvents)
    {
    }

    void Reset() override { data.clear(); }

private:
    std::unordered_set<uint32> data;
};

class DungeonClearStallReasonValue : public ManualSetValue<std::string&>
{
public:
    DungeonClearStallReasonValue(PlayerbotAI* botAI)
        : ManualSetValue<std::string&>(botAI, data, DcKey::StallReason)
    {
    }

private:
    std::string data;
};

class DungeonClearLastSaidReasonValue : public ManualSetValue<std::string&>
{
public:
    DungeonClearLastSaidReasonValue(PlayerbotAI* botAI)
        : ManualSetValue<std::string&>(botAI, data, DcKey::LastSaidReason)
    {
    }

private:
    std::string data;
};

// Short token describing what the advance action did on its most recent tick:
// "moving" (gliding the planned route), "pursuing" (closing directly on a live
// boss / final approach), or "recovering" (wedged off the route and repathing).
// Empty when the tank isn't actively navigating. DcStatusAction reads it to
// report a fine-grained activity to the companion addon — the navigation
// sub-states the coarse poll-time conditions (combat/loot/rest/stall) can't see
// on their own. Stamped every advance tick so the ~2s status poll always reads
// a fresh value; cleared on dc on / disable so a stale phase can't leak across
// runs. Distinct from the user-facing addon state: a "route not built yet"
// reading is derived at poll time, not stored here.
class DungeonClearPhaseValue : public ManualSetValue<std::string&>
{
public:
    DungeonClearPhaseValue(PlayerbotAI* botAI)
        : ManualSetValue<std::string&>(botAI, data, DcKey::Phase)
    {
    }

private:
    std::string data;
};

class DungeonClearFallbackTargetValue : public ManualSetValue<ObjectGuid>
{
public:
    DungeonClearFallbackTargetValue(PlayerbotAI* botAI)
        : ManualSetValue<ObjectGuid>(botAI, ObjectGuid::Empty, DcKey::FallbackTarget)
    {
    }
};

// Sticky target for the blocking-trash engage action. Without this the
// per-tick re-scan in DungeonClearEngageTrashAction will flip between two
// roughly-equidistant corridor mobs as the bot moves, leaving it bouncing
// in place issuing MoveTo to each in turn and never reaching either. The
// stored GUID is kept across ticks as long as the unit is still alive,
// hostile, and on the bot's map; only invalidated when the unit dies,
// despawns, or the strategy itself is reset (dc on/off, party death, etc.).
class DungeonClearEngageTrashTargetValue : public ManualSetValue<ObjectGuid>
{
public:
    DungeonClearEngageTrashTargetValue(PlayerbotAI* botAI)
        : ManualSetValue<ObjectGuid>(botAI, ObjectGuid::Empty, DcKey::EngageTrashTarget)
    {
    }
};

// GUID of the tank this (non-tank) bot is currently follow-following during
// dungeon clear. Set by DungeonClearFollowTankAction each time it issues the
// continuous MoveFollow; reset to empty once the follow has been torn down.
// Needed because MoveFollow is a persistent MotionMaster generator: when the
// DC tank goes away (dc off / death / all-cleared), the follow-tank trigger
// stops being selected and nothing would otherwise cancel the leftover
// "chase the tank" order. A self-bot can't self-heal that the way a normal
// bot does (its ordinary follow targets itself and no-ops without clearing),
// so it stays glued to the tank. This GUID lets the trigger fire one final
// teardown tick to Clear() the generator. Empty = not currently following a
// DC tank, nothing to tear down.
class DungeonClearFollowedTankValue : public ManualSetValue<ObjectGuid>
{
public:
    DungeonClearFollowedTankValue(PlayerbotAI* botAI)
        : ManualSetValue<ObjectGuid>(botAI, ObjectGuid::Empty, DcKey::FollowedTank)
    {
    }
};

// Boss entry NextDungeonBossValue committed to on its previous computation
// (0 = none). Read back as a commit-and-hold anchor: the value keeps returning
// the same boss until it leaves the candidate set (killed, skipped, or
// despawned), with no per-tick distance re-ranking or reachability re-probe.
// Without it the nearest-boss pick flip-flops between two roughly-equidistant
// bosses (branching routes, two wings) as the bot moves and the bounded
// reachability probe flickers, wedging the bot between competing long-path
// routes — the same jitter DungeonClearEngageTrashTargetValue solves for trash.
// Self-correcting — a stale entry from a prior run is simply ignored once it's
// no longer among the live/unspawned candidates.
class DungeonClearStickyBossValue : public ManualSetValue<uint32>
{
public:
    DungeonClearStickyBossValue(PlayerbotAI* botAI)
        : ManualSetValue<uint32>(botAI, 0u, DcKey::StickyBoss)
    {
    }
};

// The manual selected-boss override (was DungeonClearSelectedBossValue, "dungeon
// clear selected boss") is now DcRunState::selectedBossEntry — see DcRunState.h.

// The dynamic instance id the run-scoped completion state currently belongs to.
// The cleared-anchors / skipped / commit sets live in the bot's context, not the
// instance, so they survive a re-enter; NextDungeonBossValue compares this against
// the live instance id and wipes that state when the bot crosses into a new
// instance, so re-running the same dungeon (without a fresh `dc on`) doesn't
// inherit the prior run's latched objectives. 0 = not yet stamped.
class DungeonClearRunInstanceValue : public ManualSetValue<uint32>
{
public:
    DungeonClearRunInstanceValue(PlayerbotAI* botAI)
        : ManualSetValue<uint32>(botAI, 0u, DcKey::RunInstance)
    {
    }
};


// The long-path cache bookkeeping that used to live in five loose values —
// the boss entry + world position the cache was built for, the TTL deadline,
// and the async-job id/timestamp — now lives in DcApproachState (the "dungeon
// clear approach state" value), so it resets in lockstep with the rest of the
// approach FSM. The cached path itself (the ChunkedPathfinder::Result) still
// has its own value, DungeonClearLongPathValue.

// Index of the segment in the cached LongPath the bot is currently
// walking toward. Reset to 0 whenever the path target changes.
// Stored separately from the path so the segment list itself stays
// immutable while caching.
class DungeonClearCurrentHopValue : public ManualSetValue<uint32>
{
public:
    DungeonClearCurrentHopValue(PlayerbotAI* botAI)
        : ManualSetValue<uint32>(botAI, 0u, DcKey::CurrentHop)
    {
    }
};

// The per-approach stuck/recovery counters and give-up latches that used to
// live in five loose values — the consecutive-rebuild count, the loot-yield
// commit anchor, the path-ends-short escalation counter, and the direct-pursuit
// give-up latch — now live as fields on DcApproachState (the "dungeon clear
// approach state" value), so they reset in lockstep with the rest of the
// approach FSM. See DcApproachState for the per-field rationale.

// Per-corpse loot give-up list: maps a loot GUID whose pickup the bot
// abandoned to the ms timestamp at which the skip expires — or to
// DungeonClearUtil::LOOT_SKIP_STICKY (0) for a permanent skip. Each DC tick the
// still-live entries are stripped from the stock "available loot" stack and
// cleared from "loot target" (see DcLootPolicy::StripSkippedLoot), so
// neither the loot flags nor stock's nearest-target pick can re-commit to it.
// This is what breaks the corpse<->tank / chest<->path ping-pong that arose
// when un-finishable loot kept re-arming the loot yield every time the bot
// drifted back within LootDistance. The ttl depends on WHY the corpse was
// skipped. Permanent reasons (empty / below the quality floor / roll-locked or
// won-by-another — the winner's item auto-delivers, never re-looted — round-
// robin or allowed-looter excluding us, or bags full with no mid-dungeon
// vendor) are recorded sticky by the content-inspecting MaybeSkipUnworthyLoot
// and never re-arm on a backtrack. A real expiry is used only for the residual
// cases that inspection can't pre-classify — a camp / 15s-yield timeout on loot
// that looked takeable but didn't complete (a momentary second looter, own loot
// not yet reached) — so it is retried once rather than abandoned outright. All
// entries — sticky included — are cleared on dc on/off / party death via
// DisableDungeonClear.
class DungeonClearLootSkipValue : public ManualSetValue<std::map<ObjectGuid, uint32>&>
{
public:
    DungeonClearLootSkipValue(PlayerbotAI* botAI)
        : ManualSetValue<std::map<ObjectGuid, uint32>&>(botAI, data, DcKey::LootSkip)
    {
    }

    void Reset() override { data.clear(); }

private:
    std::map<ObjectGuid, uint32> data;
};

// The loot GUID the bot is currently "camped" on — within interaction range
// (can-loot true) of one specific corpse — together with the ms timestamp at
// which it arrived. DcLootPolicy::MaybeGiveUpCampedLoot uses the pair to
// time how long the bot has stood on the SAME corpse: a normal loot
// transaction clears in a tick or two, so camping a plain (non-gathering)
// corpse much longer means its loot is un-finishable (group-roll items pending
// a real player's roll, items reserved for others, bags full). Past the camp
// cutoff the corpse is blacklisted at once instead of burning the full
// loot-yield timeout on it — the "stuck on certain corpses" stall. Empty / 0
// whenever the bot isn't standing on a corpse (resets every tick can-loot is
// false), so a new corpse starts a fresh clock.
class DungeonClearLootCampGuidValue : public ManualSetValue<ObjectGuid>
{
public:
    DungeonClearLootCampGuidValue(PlayerbotAI* botAI)
        : ManualSetValue<ObjectGuid>(botAI, ObjectGuid::Empty, DcKey::LootCampGuid)
    {
    }
};

class DungeonClearLootCampStartValue : public ManualSetValue<uint32>
{
public:
    DungeonClearLootCampStartValue(PlayerbotAI* botAI)
        : ManualSetValue<uint32>(botAI, 0u, DcKey::LootCampStart)
    {
    }
};

// --- Advanced pulls -------------------------------------------------------
// Per-run toggle for "advanced pull" (LOS pull-to-camp) mode. When on, instead
// of the tank walking up to a trash pack and fighting in place, it marks a camp
// where the party holds (DPS/healers passive), runs in just far enough to grab
// aggro, then runs back to camp before the rest of the group engages. Leader-
// owned like `dungeon clear enabled`; reset to false alongside it (dc on / dc
// off / death / cleared) so a fresh run never starts mid-feature. Toggled by
// DcPullAction (chat keyword / `.dc pull` / addon button).
class DungeonClearPullModeValue : public ManualSetValue<bool>
{
public:
    DungeonClearPullModeValue(PlayerbotAI* botAI)
        : ManualSetValue<bool>(botAI, false, DcKey::PullMode)
    {
    }
};

// User-facing tri-state advanced-pull *preference* (the addon's Off/On/Dynamic
// control), distinct from the behavioral `dungeon clear pull mode` bool above:
//   0 Off     — no advanced pull; the bool is false.
//   1 On      — always pull-to-camp; the bool is true.
//   2 Dynamic — let the AI decide per pack whether to Leeroy or pull-to-camp
//               (DcPullPlanner governor drives the bool live); the default and
//               recommended mode.
// DcPullAction is the single writer and keeps the bool in lock-step (true only
// for On). It is settable BEFORE a run: a preference set while DC is disabled
// survives the off window and is applied by `dc on` when the run starts (so the
// run begins already in the requested mode). Reset to 0 by `dc off`; leader-
// owned and surfaced in the STATUS payload for the addon's segmented control.
class DungeonClearPullSettingValue : public ManualSetValue<uint32>
{
public:
    DungeonClearPullSettingValue(PlayerbotAI* botAI)
        : ManualSetValue<uint32>(botAI, 2u, DcKey::PullSetting)
    {
    }
};

// All transient state for one advanced/dynamic pull run, consolidated into a
// single owned struct (DcPullContext, see DcPullContext.h) so the whole pull FSM
// — phase, phase-entry timestamp, camp, breadcrumb trail, the abort/tag latches,
// and the Dynamic verdict (decision + per-pack latch + re-check throttle) — resets
// in lockstep through exactly one Reset(). This replaced seven loose ManualSetValue
// globals ("dungeon clear pull phase/since/camp position/breadcrumbs/abort target",
// "...pull decision[ target][ since]", and "...last pull target") whose resets were
// scattered across 8 call sites and provably incomplete (last-pull-target and
// breadcrumbs survived pause/skip/resume), the root of the "stale latch" bug class.
// Leader-owned; followers read `phase`/`camp` cross-context via the leader's copy
// (DcLeaderSignal::GetLeaderPullInfo / GetLeaderCampHold). Reset alongside the run
// state (dc on/off / death / cleared) and on every pull interrupt (pause / skip /
// resume / go) through DungeonClearChatActions::ResetPullTransient.
class DungeonClearPullContextValue : public ManualSetValue<DcPullContext&>
{
public:
    DungeonClearPullContextValue(PlayerbotAI* botAI)
        : ManualSetValue<DcPullContext&>(botAI, data, DcKey::PullContext)
    {
    }

    void Reset() override { data.Reset(); }

private:
    DcPullContext data;
};

// --- Submerged swim legs (Tier A) ---------------------------------------
// One active "swim leg": a 3D polyline through a water volume the navmesh can't
// route through (the floor under liquid is discarded at mmap-build time, leaving
// only a surface sheet, so a submerged tunnel — e.g. Blackfathom Deeps -> Lady
// Sarevess — is a disconnected mesh island). When the normal route planner
// dead-ends AND water lies between the bot and the target, SwimPathfinder builds
// this polyline (greedy 3D navigator, VMAP-collision based) and Advance drives
// it with a raw 3D escort spline, bypassing the navmesh Z-clamp entirely.
//
// `points` carry SUBMERGED Z verbatim; `cursor` is how far along they've been
// walked; `target` is the boss GUID the leg was built toward (a target change
// invalidates it); `buildStart` + the watchdog fields catch a stale leg and a
// non-progressing swim. Auto-invalidated on target change / arrival and reset
// alongside the rest of the run state (dc on/off, skip, death, cleared).
struct DungeonClearSwimState
{
    bool active{false};
    std::vector<G3D::Vector3> points;
    uint32 cursor{0};
    uint32 targetEntry{0};        // boss entry the leg was built toward (0 = none)
    G3D::Vector3 buildStart;
    // Closing-distance wedge detector for the leg (nav review F11): tracks the
    // nearest approach to the current swim point + the wall-clock of the last
    // gain, so a leg making no headway underwater is abandoned. Was the inline
    // lastProgressMs/lastDistToPoint pair.
    DcProgressWatchdog progressWatch;

    void Reset()
    {
        active = false;
        points.clear();
        cursor = 0;
        targetEntry = 0;
        buildStart = G3D::Vector3();
        progressWatch.Reset();
    }
};

class DungeonClearSwimStateValue : public ManualSetValue<DungeonClearSwimState&>
{
public:
    DungeonClearSwimStateValue(PlayerbotAI* botAI)
        : ManualSetValue<DungeonClearSwimState&>(botAI, data, DcKey::SwimState)
    {
    }

    void Reset() override { data.Reset(); }

private:
    DungeonClearSwimState data;
};

// Cursor into the cached long-path's flattened polyline plus the
// off-path tick counter. Reset whenever the path is rebuilt (boss
// change, TTL expiry, forced rebuild). The follower keeps this in sync
// each Advance tick; nothing else writes to it.
class DungeonClearFollowerStateValue : public ManualSetValue<DungeonFollowerState&>
{
public:
    DungeonClearFollowerStateValue(PlayerbotAI* botAI)
        : ManualSetValue<DungeonFollowerState&>(botAI, data, DcKey::FollowerState)
    {
    }

    void Reset() override { data = DungeonFollowerState{}; }

private:
    DungeonFollowerState data;
};

// Progress through the active travel-objective EVENT (DungeonEventRegistry),
// driven by DcObjectiveArriveAction via DungeonEventExecutor: which event, the
// current step, the step-entry timestamp (timeout base) and an attempt counter.
// Leader-owned like the rest of the run state. Self-healing — Drive() resets it
// when a different event starts (eventId mismatch) OR the bot is in a different
// instance than the progress was recorded for (a re-entered fresh run). The
// instance check is what catches a PERSISTENT event re-run in a new instance,
// which the eventId/gap heals miss; it is tied to the instance, NOT to dc on/off,
// so toggling dungeon-clear mid-run keeps real progress. Reset() clears it too.
class DungeonEventProgressValue : public ManualSetValue<DungeonEventProgress&>
{
public:
    DungeonEventProgressValue(PlayerbotAI* botAI)
        : ManualSetValue<DungeonEventProgress&>(botAI, data, DcKey::EventProgress)
    {
    }

    void Reset() override { data.Reset(); }

private:
    DungeonEventProgress data;
};

// Progress through the active CONDITIONAL event (DungeonEventRegistry, off the
// boss list), driven by DcRunEventAction. Kept SEPARATE from the anchored-event
// progress above because the two id namespaces are per-map independent: an
// anchored objective and a conditional event on the same map can share an id,
// and Drive() self-heals on eventId mismatch (and on an instance change for a
// re-entered run) — sharing one value would make it thrash whenever the run
// alternated between them. Leader-owned, self-healing, reset with the run state.
class DungeonConditionalEventProgressValue : public ManualSetValue<DungeonEventProgress&>
{
public:
    DungeonConditionalEventProgressValue(PlayerbotAI* botAI)
        : ManualSetValue<DungeonEventProgress&>(botAI, data, DcKey::ConditionalEventProgress)
    {
    }

    void Reset() override { data.Reset(); }

private:
    DungeonEventProgress data;
};

// All transient per-approach state for the boss-approach FSM, owned as a single
// value so it resets in lockstep through exactly one Reset() — see DcApproachState
// for the full rationale (it replaced nine loose counter/latch globals whose
// scattered resets were the "stale latch survives pause/skip/resume" bug class).
// Reset alongside the run state (dc on/off, death, cleared) and on every pull
// interrupt, the same lifetime as DungeonClearPullContextValue.
class DungeonClearApproachStateValue : public ManualSetValue<DcApproachState&>
{
public:
    DungeonClearApproachStateValue(PlayerbotAI* botAI)
        : ManualSetValue<DcApproachState&>(botAI, data, DcKey::ApproachState)
    {
    }

    void Reset() override { data.Reset(); }

private:
    DcApproachState data;
};

// Within-tick predicate memo (DcTickMemo). Pure performance scratch: it dedups
// IsAtBossEngage / IsBetweenPullsReady reads that several triggers + the advance
// action make in one tick. Self-expiring on a 50ms window that cannot cross a
// tick, so it never carries an answer between ticks; Reset() clears it alongside
// the rest of the run state for tidiness, but correctness does not depend on it.
class DungeonClearTickMemoValue : public ManualSetValue<DcTickMemo&>
{
public:
    DungeonClearTickMemoValue(PlayerbotAI* botAI)
        : ManualSetValue<DcTickMemo&>(botAI, data, DcKey::TickMemo)
    {
    }

    void Reset() override { data = DcTickMemo{}; }

private:
    DcTickMemo data;
};

#endif
