/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcStatusPublisher.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include "DungeonClearUtil.h"   // DcTargeting::GetInstanceScript (until DcTargeting moves)

#include "DungeonClearMath.h"
#include "DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "AttackersValue.h"
#include "CellImpl.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "ItemTemplate.h"
#include "LootMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "InstanceScript.h"
#include "LootObjectStack.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "Chat.h"
#include "ServerFacade.h"
#include "Timer.h"
#include "World.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcCombatFlag.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRezRecovery.h"
#include "Ai/Dungeon/DungeonClear/Util/DcSmartRest.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearLiveBossValue.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

namespace
{
    // Per-tank push bookkeeping. lastStatus is the exact STATUS payload last
    // sent (the change-detector compares against it verbatim); lastBossSig is a
    // cheap fingerprint of the boss list so the expensive boss re-scan only runs
    // when a boss actually dies / gets skipped / the target changes. `primed`
    // guards the very first tick so a freshly-marked tank always emits once.
    struct DcPushState
    {
        std::string lastStatus;
        uint64 lastBossSig = 0;
        bool primed = false;
    };

    std::map<ObjectGuid, DcPushState> g_dcActiveTanks;
    std::mutex g_dcActiveTanksMutex;

    // Server-side observer for changed STATUS frames (the `.dc test` harness).
    // Registered once at module startup, before any run exists — not guarded.
    DcStatusPublisher::StatusObserver g_dcStatusObserver;

    // Throttle accumulator for the world-tick detector (ms).
    uint32 g_dcPushAccumMs = 0;
    constexpr uint32 DC_PUSH_INTERVAL_MS = 400;

    // Cheap fingerprint of the boss list's user-visible state. Driven entirely
    // off values that are O(1) to read — the completed-encounter bitmask (flips
    // when a boss dies), the skipped-set size, and the committed target — so it
    // can be checked every detector pass without the per-boss creature scans
    // that BuildBossList / DcBossesAction perform. alive->missing transitions
    // are intentionally NOT captured here (they need the scan and are cosmetic);
    // the addon still refreshes those via its zone-change / window-show request.
    uint64 BuildBossSignature(PlayerbotAI* botAI)
    {
        AiObjectContext* context = botAI->GetAiObjectContext();
        Player* bot = botAI->GetBot();
        if (!bot)
            return 0;

        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        uint32 const mask = inst ? inst->GetCompletedEncounterMask() : 0u;
        uint32 const skipped =
            static_cast<uint32>(AI_VALUE(std::unordered_set<uint32>&, DcKey::Skipped).size());
        // Objectives and conditional events report "dead" off the cleared-anchor
        // latch, not the encounter mask — without folding it in, completing one
        // (e.g. an Uldaman altar event) leaves {mask,skipped,...} unchanged so the
        // detector never re-pushes the list and the panel row stays stuck "alive".
        // The set only grows within a run, so its size is a sufficient change key.
        uint32 const cleared =
            static_cast<uint32>(AI_VALUE(std::unordered_set<uint32>&, DcKey::ClearedAnchors).size());
        uint32 const selected = DcRun::Of(context).selectedBossEntry;
        uint32 const count =
            static_cast<uint32>(AI_VALUE(std::vector<DungeonBossInfo>, DcKey::DungeonBosses).size());

        // Fold in the map + instance id so the detector always fires when the
        // tank crosses into a different instance (clear one dungeon, walk into
        // the next; or step from a dungeon into the raid it gates). Without it a
        // transition where {mask,skipped,selected,count} happen to coincide
        // would leave the addon showing the prior dungeon's bosses.
        uint32 const mapId = bot->GetMapId();
        uint32 const instanceId = bot->GetInstanceId();

        // FNV-1a over the words. Collisions are harmless: the only cost of
        // a missed change is a slightly stale boss row until the next real
        // transition or an explicit request.
        uint64 h = 1469598103934665603ull;
        for (uint32 w : {mask, skipped, cleared, selected, count, mapId, instanceId})
        {
            h ^= w;
            h *= 1099511628211ull;
        }
        return h;
    }

    // True when the run is stalled on a flagged blocking door that still reads
    // closed RIGHT NOW. Resolves the GO and checks its live state instead of
    // trusting the 500ms-cached blocking-door value, so the panel never paints
    // "Blocked by Door" at a gate that has already swung open.
    bool StallDoorStillClosed(PlayerbotAI* botAI, AiObjectContext* context,
                              std::string const& stall)
    {
        if (stall.empty())
            return false;
        ObjectGuid const doorGuid =
            context->GetValue<ObjectGuid>(DcKey::BlockingDoor)->Get();
        if (doorGuid.IsEmpty())
            return false;
        GameObject* door = botAI->GetGameObject(doorGuid);
        return door && DcEngageGeometry::IsDoorClosed(door);
    }

    // True when the cached "next dungeon boss" still names a Boss whose encounter
    // is already complete — i.e. the value has gone stale across a kill. It is a
    // CalculatedValue with a ~2s recompute TTL, so for up to that long after a
    // boss dies it keeps returning the just-killed boss (the commit only advances
    // on the next recompute). Publishing a status frame in that window flickers
    // the panel back to the dead boss — and paints it "Blocked" if a travel stall
    // happens to be set (e.g. a slow path build to the next anchor). The detector
    // skips the status emit until the value resettles a tick or two later; the
    // boss-list emit is unaffected, so the kill still shows on the roster at once.
    bool NextBossAlreadyDead(PlayerbotAI* botAI)
    {
        Player* bot = botAI->GetBot();
        if (!bot)
            return false;
        std::optional<DungeonBossInfo> const next =
            botAI->GetAiObjectContext()
                ->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)
                ->Get();
        if (!next.has_value() || next->kind != DungeonAnchorKind::Boss ||
            next->encounterIndex >= 32)
            return false;
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        uint32 const mask = inst ? inst->GetCompletedEncounterMask() : 0u;
        return (mask & (1u << next->encounterIndex)) != 0u;
    }
}

void DcStatusPublisher::SendAddonMessage(PlayerbotAI* botAI, std::string const& msg)
{
    Player* bot = botAI->GetBot();
    if (!bot || !bot->GetGroup())
        return;

    std::string const payload = "DC\t" + msg;

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, payload.c_str(),
                                 LANG_ADDON, CHAT_TAG_NONE,
                                 bot->GetObjectGuid(), bot->GetName());

    for (Player* receiver : botAI->GetRealPlayersInGroup())
        ServerFacade::instance().SendPacket(receiver, data);
}
std::string DcStatusPublisher::BuildStatusPayload(PlayerbotAI* botAI)
{
    AiObjectContext* context = botAI->GetAiObjectContext();
    Player* bot = botAI->GetBot();

    bool const enabled = DcRun::Of(context).enabled;
    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    auto const& skipped = AI_VALUE(std::unordered_set<uint32>&, DcKey::Skipped);
    std::string const& stall = AI_VALUE(std::string&, DcKey::StallReason);

    // Calculate dynamic state for addon UI. Authoritative conditions (combat,
    // stall, loot, rest) take precedence over the advance action's self-reported
    // navigation phase, since they reflect ground truth the phase token can't
    // see. `detail` is a short human sentence the addon shows under the state
    // line — who we're waiting on, what we're heading to, etc.
    std::string stateStr = "off";
    std::string detail;
    bool const paused = DcRun::Of(context).paused;
    // `pullMode` is the behavioral gate (a pull maneuver is actually armed) and
    // drives the state/detail wording below; `pullSetting` is the user's tri-state
    // preference (Off/On/Dynamic) reported to the addon's control. They differ for
    // Dynamic, where the governor flips the bool per pack — `pullDecision` (0 none /
    // 1 Leeroy / 2 Advanced / 3 waiting-for-patrol) is the live verdict the addon
    // shows under "Dynamic".
    bool const pullMode = AI_VALUE(bool, DcKey::PullMode);
    uint32 const pullSetting = AI_VALUE(uint32, DcKey::PullSetting);
    DcPullContext const& pull = AI_VALUE(DcPullContext&, DcKey::PullContext);
    uint32 const pullDecision = static_cast<uint32>(pull.decision);
    uint32 const pullPhase = static_cast<uint32>(pull.phase);
    std::string const bossName = next.has_value() ? next->name : "the boss";

    if (enabled && paused)
    {
        // Paused takes precedence over every running sub-state — the addon
        // paints this state yellow. `enabled` stays 1 so the addon can see the
        // eventual resume. `detail` carries WHY we're paused (set at the pause
        // site: a manual hold vs. a door the tank can't open) so the panel can
        // report the cause; the addon supplies the "boss progress saved"
        // reassurance line. Fall back to a generic hold if no reason was stamped.
        stateStr = "paused";
        std::string const& pauseReason = DcRun::Of(context).pauseReason;
        detail = pauseReason.empty() ? "holding position" : pauseReason;
    }
    else if (enabled && bot && pullMode && DcLeaderSignal::IsPullPhaseHolding(pullPhase))
    {
        // Mid advanced-pull. Takes precedence over the combat sub-state — during
        // the return leg the tank is in combat but we want to report the pull.
        stateStr = "pulling";
        // A line-of-sight pull (ranged pack) is called out so the player knows the
        // tank is deliberately dragging the casters out of sight, not just kiting.
        char const* losTag = pull.losPull ? " (out of line of sight)" : "";
        if (pullPhase == static_cast<uint32>(DcPullPhase::Returning))
            detail = pull.losPull
                         ? "Pulling the ranged pack back to camp, out of line of sight."
                         : "Pulling the pack back to camp.";
        else
            detail = std::string("Pulling — party holding at camp") + losTag + ".";
    }
    else if (enabled && bot)
    {
        // The floors the between-pulls gate is actually holding for (0/0 under
        // Smart Rest or a phantom flag) — read once, used by the rest arm below.
        DcPartyState::RestGate const rest = DcPartyState::GetRestGate(bot, context);

        // "In combat" alone is not a fight. Under a phantom flag (a 45yd hostile
        // area aura, nothing aggroed) this arm reported "fighting_trash —
        // Clearing trash from the path" for the twelve minutes a frozen party
        // stood in the Arcatraz Eredar room's aura doing nothing at all
        // (tr-20260801-194932-20), hiding the real state — which is the rest /
        // spread wait further down. Require an actual engagement to claim a fight.
        if (bot->IsInCombat() && !DcCombatFlag::IsPhantomFlag(bot, context))
        {
            Unit* currentTarget = context->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Get();
            if (currentTarget && next.has_value() && currentTarget->GetEntry() == next->entry)
            {
                stateStr = "fighting_boss";
                detail = "Engaging " + bossName + ".";
            }
            else
            {
                stateStr = "fighting_trash";
                detail = (currentTarget && currentTarget->GetName()[0] != '\0')
                             ? ("Fighting " + std::string(currentTarget->GetName()) + ".")
                             : "Clearing trash from the path.";
            }
        }
        // "Blocked by a door" must be LIVE truth, not cache residue: the
        // blocking-door value refreshes on a 500ms interval and the stall
        // string only clears when Advance next runs (which can sit behind the
        // loot/rest gates for many seconds while the party regroups at the
        // doorway). Right after a gate opens, both are stale — and the addon
        // paints "stalled" as Blocked too, so the panel kept shouting
        // "Blocked" at an open gate. Re-check the flagged door's actual state
        // here, and let looting/resting (the truthful post-door states) win
        // over a leftover stall string.
        else if (StallDoorStillClosed(botAI, context, stall))
        {
            stateStr = "door_blocked";
            // The stall reason already rides in its own field; leave detail empty.
        }
        // Post-combat rez recovery: a corpse is being handled (or waiting on
        // the player). Reported ahead of loot/rest — the run is parked for the
        // recovery, whatever else is also pending. Read-only (DescribeWait
        // never touches the recovery clock).
        else if (DcRezRecovery::IsPending(bot))
        {
            stateStr = "resting";
            std::string const who = DcRezRecovery::DescribeWait(bot);
            detail = who.empty() ? "Recovering a fallen party member." : who;
        }
        else if (AI_VALUE(bool, DcKey::Stock::HasAvailableLoot) || AI_VALUE(bool, DcKey::Stock::CanLoot) ||
                 DcPartyState::IsAnyPartyMemberLooting(bot))
        {
            stateStr = "looting";
            std::string const who = DcPartyState::DescribePartyLooting(bot);
            detail = who.empty() ? "Collecting loot." : (who + ".");
        }
        // Smart Rest: the latch IS the recovery wait — read it (never update;
        // the between-pulls gate owns updates) and name who is still short of
        // full. The spread-only wait falls through to the next arm.
        else if (DcSmartRest::Enabled(bot) && DcSmartRest::IsLatched(bot))
        {
            stateStr = "resting";
            std::string const who = DcSmartRest::DescribeWait(bot);
            detail = who.empty() ? "Resting the party." : (who + ".");
        }
        // Status display must use the SAME spread the advance gate enforces, or
        // the tank parks at the limit while still reporting "En route" instead
        // of "Waiting on". GetSpreadGate is the one shared body (per-run
        // PartyMaxSpread override, mid-maneuver waiver, camp anchor in pull
        // mode), so the panel can never report a different wait than the gate.
        // Under Smart Rest the hp/mana thresholds are 0 (the latch above owns
        // recovery), leaving this arm the spread-only "out of range" waits —
        // exactly mirroring the between-pulls gate.
        else if (DcPartyState::SpreadGate const gate = DcPartyState::GetSpreadGate(bot, context);
                 !DcPartyState::IsPartyReady(bot, rest.minHp, rest.minMp,
                     gate.maxSpread, gate.anchor, gate.maxTankGap))
        {
            stateStr = "resting";
            std::string const who = DcPartyState::DescribePartyNotReady(bot, rest.minHp, rest.minMp,
                                                                            gate.maxSpread, gate.anchor,
                                                                            gate.maxTankGap);
            detail = who.empty() ? "Waiting for the party to recover." : (who + ".");
        }
        else if (!stall.empty())
        {
            // Genuine non-door stall (unreachable boss, dead-end escalation…)
            // — or a door stall whose door has not yet been re-verified open.
            stateStr = "stalled";
        }
        // Room-wide-aggro boss: the tank is deliberately clearing the room before
        // pulling the boss. Reported after loot/rest (which are truthful sub-states
        // between room pulls) but before the generic moving/idle, so the panel
        // explains why the tank is working trash next to a boss it isn't pulling.
        else if (DcTargeting::IsRoomClearActive(bot, context))
        {
            stateStr = "clearing_room";
            detail = "Clearing the room before pulling " + bossName + ".";
        }
        else
        {
            // No blocking condition — report what the advance action is up to,
            // using its per-tick phase token plus the route cache state.
            std::string const& phase = AI_VALUE(std::string&, DcKey::Phase);
            uint32 const pathTarget =
                AI_VALUE(DcApproachState&, DcKey::ApproachState).longPathTargetEntry;
            bool const routeReady = next.has_value() && pathTarget == next->entry;

            if (phase == "recovering")
            {
                stateStr = "recovering";
                detail = "Stuck; replanning the route to " + bossName + ".";
            }
            else if (phase == "pursuing")
            {
                stateStr = "pursuing";
                detail = "Closing in on " + bossName + ".";
            }
            else if (next.has_value() && !routeReady)
            {
                // A boss is selected but no route to it is cached yet: the tank
                // is between picking the target and its first path build.
                stateStr = "pathing";
                detail = "Plotting a route to " + bossName + ".";
            }
            else if (phase == "moving" || bot->isMoving())
            {
                stateStr = "moving";
                detail = next.has_value() ? ("En route to " + bossName + ".") : "Advancing.";
            }
            else
            {
                stateStr = "idle";
                detail = next.has_value() ? ("Holding near " + bossName + ".") : "Idle.";
            }
        }
    }

    std::ostringstream addonMsg;
    addonMsg << "STATUS\t"
             << (enabled ? "1" : "0") << "\t"
             << (next.has_value() ? std::to_string(next->entry) : "0") << "\t"
             << (next.has_value() ? next->name : "None") << "\t"
             << (stall.empty() ? "" : stall) << "\t"
             << skipped.size() << "\t"
             << stateStr << "\t"
             << detail << "\t"
             // Trailing field (index 8): advanced-pull preference as a tri-state
             // (0 Off / 1 On / 2 Dynamic) for the addon's segmented control + tiny
             // cycle. Appended so older addons ignoring it stay compatible (same
             // pattern as the BOSS wing field).
             << pullSetting << "\t"
             // Trailing field (index 9): live Dynamic verdict for the pack the tank
             // is sizing up (0 none / 1 Leeroy / 2 Advanced / 3 waiting-for-patrol).
             // Only meaningful when pullSetting == 2; the addon shows it as the
             // Dynamic sub-label.
             // Appended last so older addons ignore it.
             << pullDecision;

    return addonMsg.str();
}
void DcStatusPublisher::PushStatus(PlayerbotAI* botAI)
{
    if (!botAI)
        return;

    std::string payload = BuildStatusPayload(botAI);
    SendAddonMessage(botAI, payload);

    // Keep the change-detector's STATUS snapshot in lock-step with an explicit
    // push so it doesn't re-emit the identical payload on the very next pass.
    // Deliberately leave lastBossSig untouched: a fresh `dc on` resets it to the
    // 0 sentinel via MarkActiveTank, so the first detector pass emits the boss
    // list exactly once; later skip/go changes flip the signature and re-push.
    if (Player* bot = botAI->GetBot())
    {
        std::lock_guard<std::mutex> lock(g_dcActiveTanksMutex);
        auto it = g_dcActiveTanks.find(bot->GetObjectGuid());
        if (it != g_dcActiveTanks.end())
        {
            it->second.lastStatus = std::move(payload);
            it->second.primed = true;
        }
    }
}
void DcStatusPublisher::MarkActiveTank(ObjectGuid tank)
{
    std::lock_guard<std::mutex> lock(g_dcActiveTanksMutex);
    // Default-constructed state (primed=false) forces an emit on the first
    // detector pass even if nothing has changed yet.
    g_dcActiveTanks[tank];
}
void DcStatusPublisher::UnmarkActiveTank(ObjectGuid tank)
{
    std::lock_guard<std::mutex> lock(g_dcActiveTanksMutex);
    g_dcActiveTanks.erase(tank);
}
void DcStatusPublisher::SetStatusObserver(StatusObserver observer)
{
    g_dcStatusObserver = std::move(observer);
}
void DcStatusPublisher::TickStatusPushes(uint32 diff)
{
    // Throttle: detect at most every DC_PUSH_INTERVAL_MS. Status transitions are
    // human-perceptible at this granularity, and it keeps the per-tick cost off
    // the world loop.
    g_dcPushAccumMs += diff;
    if (g_dcPushAccumMs < DC_PUSH_INTERVAL_MS)
        return;
    g_dcPushAccumMs = 0;

    // Snapshot the GUIDs under lock, then do the heavier work (value reads,
    // packet builds) without holding it — SendAddonMessage / DoSpecificAction
    // must not run under our mutex.
    std::vector<ObjectGuid> tanks;
    {
        std::lock_guard<std::mutex> lock(g_dcActiveTanksMutex);
        if (g_dcActiveTanks.empty())
            return;
        tanks.reserve(g_dcActiveTanks.size());
        for (auto const& kv : g_dcActiveTanks)
            tanks.push_back(kv.first);
    }

    for (ObjectGuid guid : tanks)
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || !bot->IsInWorld())
        {
            // Tank vanished without a clean dc off (logout, instance reset).
            // Drop it; the addon will resync on its next explicit request.
            std::lock_guard<std::mutex> lock(g_dcActiveTanksMutex);
            g_dcActiveTanks.erase(guid);
            continue;
        }

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            continue;

        // Latch conditional events whose gating condition has gone false this run
        // (e.g. a Stratholme ziggurat acolyte clear, whose instance data flips to
        // "cleared" mid-combat before the executor can latch it). This grows the
        // cleared set, which BuildBossSignature counts — so a completed event both
        // re-pushes the boss list and flips its folded note to (done) below.
        DungeonEventExecutor::SweepCompletedConditionalEvents(
            bot, botAI->GetAiObjectContext(), bot->GetMapId());

        std::string const payload = BuildStatusPayload(botAI);
        uint64 const bossSig = BuildBossSignature(botAI);
        // Don't publish a transient stale frame whose target boss is already dead
        // (the "next dungeon boss" value lags a kill by its recompute TTL). Holding
        // the emit avoids flickering the panel back to the just-killed boss; the
        // correct frame goes out once the value resettles. The boss-list emit below
        // is intentionally NOT gated, so the kill still updates the roster at once.
        bool const staleDeadBoss = NextBossAlreadyDead(botAI);

        bool emitStatus = false;
        bool emitBosses = false;
        {
            std::lock_guard<std::mutex> lock(g_dcActiveTanksMutex);
            auto it = g_dcActiveTanks.find(guid);
            if (it == g_dcActiveTanks.end())
                continue;  // unmarked between snapshot and now

            DcPushState& st = it->second;
            if (!staleDeadBoss && (!st.primed || st.lastStatus != payload))
            {
                st.lastStatus = payload;
                emitStatus = true;
            }
            if (!st.primed || st.lastBossSig != bossSig)
            {
                st.lastBossSig = bossSig;
                emitBosses = true;
            }
            st.primed = true;
        }

        if (emitStatus)
        {
            SendAddonMessage(botAI, payload);
            if (g_dcStatusObserver)
                g_dcStatusObserver(guid, payload);
        }
        if (emitBosses)
            // Reuse the existing boss-list action (silent) so the BOSS_START /
            // BOSS* / BOSS_END framing and per-boss status logic live in one
            // place. "addon" param suppresses the chat echo.
            botAI->DoSpecificAction("dc bosses", Event("dc push", "addon"), true);
    }
}
