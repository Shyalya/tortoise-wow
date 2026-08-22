/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "NextDungeonBossValue.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Creature.h"
#include "Player.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBossOrdering.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearStateValues.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

namespace
{
    // A boss "exists alive" on the current map if any spawned creature with the
    // matching entry is alive. Returns:
    //   present=true,  alive=true   — at least one live instance
    //   present=true,  alive=false  — instance(s) exist but all are dead
    //   present=false, alive=false  — no spawned instance found at all
    struct BossLiveState
    {
        bool present;
        bool alive;
    };

    // One pass over the creature store builds the liveness of every boss
    // entry of interest, instead of re-scanning the whole store once per boss
    // (the old per-boss CheckBossLive was O(bosses x store)). `wanted` is the
    // set of boss entries we care about; entries never seen stay absent and
    // read as {present=false, alive=false} via the caller's default.
    std::unordered_map<uint32, BossLiveState> BuildLiveness(Map* map,
                                                            std::unordered_set<uint32> const& wanted)
    {
        std::unordered_map<uint32, BossLiveState> liveness;
        if (!map || wanted.empty())
            return liveness;

        for (auto const& kv : map->GetCreatureBySpawnIdStore())
        {
            Creature* c = kv.second;
            if (!c)
                continue;
            uint32 const entry = c->GetEntry();
            if (!wanted.count(entry))
                continue;
            BossLiveState& state = liveness[entry];  // default {false,false}
            state.present = true;
            if (c->IsAlive())
                state.alive = true;
        }
        return liveness;
    }

    BossLiveState LookupLive(std::unordered_map<uint32, BossLiveState> const& liveness, uint32 entry)
    {
        auto it = liveness.find(entry);
        return it != liveness.end() ? it->second : BossLiveState{false, false};
    }
}

std::optional<DungeonBossInfo> NextDungeonBossValue::Calculate()
{
    if (!bot || !bot->IsInWorld())
        return std::nullopt;

    Map* map = bot->GetMap();
    if (!map || !map->IsDungeon())
        return std::nullopt;

    // Wipe stale run-completion latches when this bot is first seen in a new
    // instance (objective/event "Done" lives in the bot's context, not the
    // instance, so it outlives a re-enter — unlike boss kills, which ride the
    // self-resetting encounter mask). Shared with the boss-list builder so the
    // panel clears even while DC is off; see DcTargeting::ResetCompletionLatches…
    DcTargeting::ResetCompletionLatchesForNewInstance(bot, context);

    std::vector<DungeonBossInfo> const& bosses =
        AI_VALUE(std::vector<DungeonBossInfo>, DcKey::DungeonBosses);

    std::unordered_set<uint32> const& skipped =
        AI_VALUE(std::unordered_set<uint32>&, DcKey::Skipped);

    // Travel objectives (BossRosterRegistry) have no kill-bit; once reached they
    // are latched here by DcObjectiveArriveAction so they drop out of the
    // candidate list exactly like a killed boss.
    std::unordered_set<uint32> const& cleared =
        AI_VALUE(std::unordered_set<uint32>&, DcKey::ClearedAnchors);

    // The completed-encounter mask is the authoritative kill signal. It is
    // set by Map::UpdateEncounterState from the same KillRewarder path that
    // grants loot and quest credit (KillRewarder.cpp -> UpdateEncounterState),
    // flipping bit (1 << DungeonEncounter.dbc encounterIndex). BossSpawnIndex
    // stores that exact DBC encounterIndex, so the bit lines up directly.
    //
    // Do NOT use InstanceScript::GetBossState(encounterIndex) here: that
    // indexes the script's internal bosses[] vector by its own DATA_*
    // constants — a different index space that only aligns with the DBC
    // encounterIndex by coincidence. When it doesn't align, a misaligned slot
    // reading DONE silently skips a live boss, and a slot that never reads
    // DONE leaves a freshly-killed boss looking alive (the symptom: party
    // kills a boss but never advances to the next one). The mask is reliable
    // for every dungeon whose encounters are ENCOUNTER_CREDIT_KILL_CREATURE,
    // which is precisely the set BossSpawnIndex indexes.
    InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
    uint32 const completedMask = inst ? inst->GetCompletedEncounterMask() : 0u;

    // Resolve every boss's liveness in a single store pass (shared by the
    // selected-override check and the partition loop below).
    std::unordered_set<uint32> wantedEntries;
    wantedEntries.reserve(bosses.size());
    for (DungeonBossInfo const& info : bosses)
        wantedEntries.insert(info.entry);
    std::unordered_map<uint32, BossLiveState> const liveness = BuildLiveness(map, wantedEntries);

    // Check if there is a manually selected boss target override
    uint32 const selectedEntry = DcRun::Of(context).selectedBossEntry;
    if (selectedEntry != 0)
    {
        for (DungeonBossInfo const& info : bosses)
        {
            if (info.entry == selectedEntry)
            {
                bool invalid = false;
                if (cleared.count(info.entry))
                    invalid = true;
                else if (info.kind == DungeonAnchorKind::Boss &&
                         info.encounterIndex < 32 && (completedMask & (1u << info.encounterIndex)))
                    invalid = true;
                else if (info.kind == DungeonAnchorKind::Boss && info.doneBossStateIndex >= 0 &&
                         inst && inst->GetBossState(static_cast<uint32>(info.doneBossStateIndex)) == DONE)
                    invalid = true;

                if (!invalid)
                {
                    BossLiveState const state = LookupLive(liveness, info.entry);
                    if (state.present && !state.alive)
                        invalid = true;
                }

                if (invalid)
                {
                    // Already dead/completed: clear override and drop back to automatic
                    DcRun::Of(context).selectedBossEntry = 0u;
                    break;
                }

                // Force return this boss and update sticky
                context->GetValue<uint32>(DcKey::StickyBoss)->Set(info.entry);
                return info;
            }
        }
    }

    // Build the candidate list in DBC encounter order (the order `bosses`
    // already arrives in). A boss is a candidate unless it's authoritatively
    // finished or deliberately passed over:
    //   - completion bit set — the group killed it,
    //   - user-skipped,
    //   - present but dead — a corpse whose completion bit hasn't flipped yet;
    //     transient, so we don't re-engage it.
    // A boss whose grid simply hasn't streamed in yet (not present at all) is
    // STILL a candidate, in its natural encounter-order slot.
    //
    // This list used to be split into alive-vs-not-spawned tiers, with the
    // not-spawned tier consulted only when nothing was spawned. That made the
    // bot skip past the next in-order boss (whose grid wasn't loaded) to a far
    // boss that merely happened to be spawned already — e.g. in Stratholme,
    // killing Forresten (idx 1) jumped straight to Barthilas (idx 10) because
    // the live-side bosses between them hadn't streamed in. Advance already
    // handles a not-yet-spawned target correctly: it walks toward the boss's
    // static spawn coords to stream the grid in, and only stalls (prompting a
    // manual `dc skip`) once close enough that the grid is certainly loaded and
    // the boss is genuinely absent (a true event-gate). So in-order selection
    // across spawned and not-yet-spawned bosses alike is safe, and keeps the
    // clear sequential. The empty-result == all-cleared contract that
    // AllClearedTrigger relies on still holds: the list empties only once every
    // boss is completed, skipped, or a fresh corpse.
    std::vector<DungeonBossInfo> cands;
    for (DungeonBossInfo const& info : bosses)
    {
        if (skipped.count(info.entry))
            continue;

        // Travel objective already reached this run (latched on arrival). Its
        // encounterIndex is just an ordering hint, not a real DBC bit, so it
        // must be filtered here rather than via the completion mask below.
        if (cleared.count(info.entry))
            continue;

        // Authoritative: this encounter is already complete in this instance.
        // Objectives never carry a real kill-bit, so the mask is consulted for
        // Boss anchors only (an objective's ordering index could otherwise
        // collide with an unrelated boss's set bit and vanish prematurely).
        if (info.kind == DungeonAnchorKind::Boss &&
            info.encounterIndex < 32 && (completedMask & (1u << info.encounterIndex)))
            continue;

        // Non-DBC door-gating boss (e.g. the Mechanar Gatewatchers): no
        // GetCompletedEncounterMask bit ever flips for it, so its completion is
        // read from the instance script's own boss-state slot instead. Persistent
        // for the instance's life, so a re-enable after the kill won't re-target
        // a boss whose corpse has long despawned. See doneBossStateIndex.
        if (info.kind == DungeonAnchorKind::Boss && info.doneBossStateIndex >= 0 &&
            inst && inst->GetBossState(static_cast<uint32>(info.doneBossStateIndex)) == DONE)
            continue;

        BossLiveState const state = LookupLive(liveness, info.entry);
        if (state.present && !state.alive)
            continue;  // corpse — transient, will flip to completed

        cands.push_back(info);
    }

    uint32 const stickyEntry = AI_VALUE(uint32, DcKey::StickyBoss);

    // Resolve the committed boss's ORDER KEY from the full boss list (it stays
    // here even after the boss dies and drops out of the candidate list), so
    // PickTarget can advance to the next boss after it rather than snapping back
    // to the lowest survivor when the commit releases. Uses BossOrderKey (not the
    // raw encounterIndex) so a reordered boss advances along its path slot.
    uint32 stickyEncounterIndex = 0;
    bool haveStickyIndex = false;
    if (stickyEntry)
        for (DungeonBossInfo const& info : bosses)
            if (info.entry == stickyEntry)
            {
                stickyEncounterIndex = BossOrderKey(info);
                haveStickyIndex = true;
                break;
            }

    std::optional<DungeonBossInfo> pick =
        DcBossOrdering::PickTarget(cands, stickyEntry, stickyEncounterIndex, haveStickyIndex);

    // A stall reason set by Advance always describes the boss it was heading to
    // ("Can't reach <boss>: not spawned", "Stuck near <boss>", …). The instant
    // the committed target advances to a DIFFERENT boss — the previous one was
    // killed or skipped — that reason is obsolete: leaving it set makes the panel
    // shout "Can't reach Malor" while the roster already shows Galford as next.
    // Advance clears its own stall on a successful step, but it returns early
    // (without clearing) whenever it is parked behind a loot/rest yield — exactly
    // the post-kill window — so the stale reason can linger for the whole yield.
    // Clear it here, at the single chokepoint every target change flows through,
    // independent of whatever Advance is doing. Advance re-sets a fresh stall for
    // the new target if it, too, can't proceed.
    if (pick && stickyEntry && pick->entry != stickyEntry)
        context->GetValue<std::string&>(DcKey::StallReason)->Get().clear();

    // Record the commit so the next computation holds it. Storing 0 on an empty
    // result releases the commit cleanly when the dungeon is cleared or every
    // boss is skipped.
    context->GetValue<uint32>(DcKey::StickyBoss)->Set(pick ? pick->entry : 0u);
    return pick;
}
