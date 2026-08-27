/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcFollowerLifecycle.h"
#include "DcStrategyGate.h"

#include "DungeonClearUtil.h"   // DC_PULL_* log macros

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
#include "Config.h"
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
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearLiveBossValue.h"

namespace
{
    // GUIDs of players that currently carry a DC follow-tank MoveFollow
    // generator. Mutated from the follow-tank action (bot AI update) and read by
    // the reaper (world update / OnPlayerbotUpdate); both run on the world
    // thread today, but the set is tiny and the lock is uncontended, so guard it
    // anyway to stay correct if bot updates ever move off-thread.
    std::set<ObjectGuid> g_dcFollowingPlayers;
    std::mutex g_dcFollowingMutex;

    // GUIDs of followers DC has put into the mod-playerbots "passive" combat
    // strategy for an advanced pull. Tracked so ReapStrandedPassives is the
    // single authoritative teardown — it removes passive the moment the leader
    // leaves a holding pull phase, even for a follower that got dragged into
    // combat (its own engines can't self-heal a passive-lock mid-fight). Only
    // passive DC itself applied lives here; a player's manual passive is never
    // recorded, so it's never clobbered.
    std::set<ObjectGuid> g_dcPassivePlayers;
    std::mutex g_dcPassiveMutex;

    // Followers are released from passive on a short delay AFTER the leader
    // commits the pull (flips to Engage), so the tank gets a head start on
    // threat before DPS pile on. Maps a follower GUID to the getMSTime()
    // deadline at which it drops passive. Only the graceful Engage transition is
    // delayed; a hard exit (run ended / paused / leader gone) and the
    // camp-safety valve release immediately. Guarded by g_dcPassiveMutex.
    std::map<ObjectGuid, uint32> g_dcPlayerReleaseAt;

    // Pets are released from passive on a further delay AFTER their owner: a pet
    // freed the instant its owner drops passive charges in and bodies the pack
    // before the tank has settled aggro, botching the pull. Maps a follower GUID
    // to the getMSTime() deadline at which its pet flips back to REACT_DEFENSIVE.
    // Guarded by g_dcPassiveMutex (same lifecycle as g_dcPassivePlayers).
    std::map<ObjectGuid, uint32> g_dcPetReleaseAt;

    // Healers held at camp are pinned with the "stay" strategy (not "+passive" —
    // they must keep casting heals) so playerbots' reach-to-heal MOVEMENT
    // (ReachTargetAction early-outs on HasStrategy("stay")) is suppressed and the
    // healer tops the tank off IN PLACE instead of running forward to chase heal
    // range. Maps a healer GUID to the bitmask of states DC added the strategy in
    // (bit0 = BOT_STATE_COMBAT, bit1 = BOT_STATE_NON_COMBAT) so release strips
    // exactly what we applied and leaves a player's own manual "stay" alone.
    // Guarded by g_dcPassiveMutex (same lifecycle as g_dcPassivePlayers).
    std::map<ObjectGuid, uint8> g_dcHealerStayStates;

    // "stay" and "follow" are siblings in mod-playerbots' MovementStrategyContext
    // (constructed with supportsSiblings = true), and Engine::addStrategy removes
    // every sibling of the strategy it adds. So `+stay` SILENTLY DELETES the bot's
    // "follow" strategy from that engine, and the inverse `-stay` does NOT restore
    // it (removeStrategy never re-adds siblings). Left unrepaired, a healer we pin
    // loses "follow" for good and just stands there once the run ends / `dc off`
    // — the "healer stuck in stay" bug. (DPS are unaffected: they get "+passive",
    // which lives in the combat-strategy context and never touches "follow".)
    // Maps a healer GUID to the bitmask of states where our `+stay` actually
    // evicted a present "follow" (bit0 = COMBAT, bit1 = NON_COMBAT) so release
    // restores exactly what we clobbered — and leaves alone a state that had no
    // "follow" to begin with. Guarded by g_dcPassiveMutex.
    std::map<ObjectGuid, uint8> g_dcHealerFollowStates;
}

// Flip any pets whose post-owner-release grace window has elapsed back to
// REACT_DEFENSIVE. Kept separate from the player sweep below because pet
// releases outlive g_dcPassivePlayers: the owner is dropped from that set the
// instant it's freed, but its pet stays scheduled here for a few more seconds.
static void ReapPetReleases()
{
    std::vector<ObjectGuid> due;
    {
        std::lock_guard<std::mutex> lock(g_dcPassiveMutex);
        if (g_dcPetReleaseAt.empty())
            return;
        uint32 const now = getMSTime();
        for (auto it = g_dcPetReleaseAt.begin(); it != g_dcPetReleaseAt.end();)
        {
            // getMSTime() wraps ~every 49 days; the signed-diff test stays
            // correct across the wrap where (now >= deadline) would not.
            if (int32(now - it->second) >= 0)
            {
                due.push_back(it->first);
                it = g_dcPetReleaseAt.erase(it);
            }
            else
                ++it;
        }
    }

    for (ObjectGuid guid : due)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player || !player->IsInWorld())
            continue;
        if (Pet* pet = player->GetPet())
            pet->SetReactState(REACT_DEFENSIVE);
    }
}

void DcFollowerLifecycle::MarkFollowing(ObjectGuid player)
{
    std::lock_guard<std::mutex> lock(g_dcFollowingMutex);
    g_dcFollowingPlayers.insert(player);
}
void DcFollowerLifecycle::UnmarkFollowing(ObjectGuid player)
{
    std::lock_guard<std::mutex> lock(g_dcFollowingMutex);
    g_dcFollowingPlayers.erase(player);
}
void DcFollowerLifecycle::ReapOrphanedFollows()
{
    std::lock_guard<std::mutex> lock(g_dcFollowingMutex);
    if (g_dcFollowingPlayers.empty())
        return;

    for (auto it = g_dcFollowingPlayers.begin(); it != g_dcFollowingPlayers.end();)
    {
        Player* player = ObjectAccessor::FindPlayer(*it);

        // Player left the world (logged out, bot despawned): nothing to clear,
        // and the GUID would otherwise linger forever. Drop it.
        if (!player || !player->IsInWorld())
        {
            it = g_dcFollowingPlayers.erase(it);
            continue;
        }

        // AI still ticking -> its own follow-tank teardown owns this generator;
        // leave the mark in place and move on.
        if (GET_PLAYERBOT_AI(player))
        {
            ++it;
            continue;
        }

        // AI gone but the player is still in world (a self-bot toggled out of bot
        // mode). Cancel the leftover continuous follow so movement control reverts
        // to the human; a real player has no AI to self-heal it otherwise.
        if (player->GetMotionMaster() &&
            player->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
        {
            if (player->isMoving())
                player->StopMoving();
            player->GetMotionMaster()->Clear();
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] reaped orphaned follow generator (self-bot left bot "
                     "mode) -> movement control returned to player",
                     player->GetName());
        }

        it = g_dcFollowingPlayers.erase(it);
    }
}
void DcFollowerLifecycle::EnsureTankBearForm(Player* bot)
{
    if (!bot || bot->getClass() != CLASS_DRUID)
        return;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;
    // Already shifted into a bear form — nothing to do.
    if (botAI->HasAnyAuraOf(bot, "bear form", "dire bear form", nullptr))
        return;
    // Prefer dire bear form (more armor + the bear/dire-bear HP bonus); the
    // dire-bear action's alternatives fall back to plain bear form, but invoke
    // it explicitly too so an untrained dire bear still shifts.
    if (!botAI->DoSpecificAction("dire bear form", Event(), true))
        botAI->DoSpecificAction("bear form", Event(), true);
}
void DcFollowerLifecycle::ApplyFollowerPassive(Player* follower)
{
    if (!follower)
        return;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(follower);
    if (!botAI)
        return;

    {
        std::lock_guard<std::mutex> lock(g_dcPassiveMutex);
        // Already DC-managed — nothing to do (idempotent across hold ticks).
        if (g_dcPassivePlayers.count(follower->GetObjectGuid()))
            return;
    }

    // Fresh hold -> fresh camp-safety grace latch. The latch lives in the
    // follower's own pull-context copy and is never Reset() there (the FSM reset
    // runs on the leader), so a stale timestamp from a previous hold would
    // defeat PullSafetyGrace and fire the valve on the first qualifying tick.
    botAI->GetAiObjectContext()
        ->GetValue<DcPullContext&>(DcKey::PullContext)->Get().campSafetySince = 0;

    // Healers are deliberately NOT made fully passive: they must keep casting
    // heals on the tank through the drag-back. But they ARE pinned with the
    // "stay" strategy, which disables EVERY stock mover that voluntarily checks
    // HasStrategy("stay") — reach-to-heal (ReachTargetAction), the melee/behind
    // formation positioning (CombatFormationMoveAction), etc. The bug this fixes:
    // a held healer ran PAST the tank into the pack. When the camp-hold yields the
    // tick for the healer to cast, some stock mover would walk it to a heal/follow
    // point that sits BEHIND the tank — and "behind the tank" is toward the pack
    // while the tank faces camp on the drag-back. With "stay" no mover can run on
    // that yield, so the cast-heal fires in place; the camp-hold action itself
    // drives a CLAMPED approach (DcPullPlanner::ComputeHealApproach) when the tank
    // is out of heal range, so the healer can still close the gap but never crosses
    // to the threat side of the tank. We join g_dcPassivePlayers so
    // ReapStrandedPassives releases the healer (clearing the stay pin) on the SAME
    // schedule as the held DPS — it only advances once they do. Any aggro a heal
    // still pulls is the tank's to taunt off; the pet stays defensive (untouched).
    if (PlayerbotAI::IsHeal(follower))
    {
        uint8 added = 0;
        // Record per state whether our `+stay` is about to evict a present
        // "follow" sibling, so release can put exactly that back (see
        // g_dcHealerFollowStates). The check must run BEFORE ChangeStrategy("+stay")
        // — afterwards the sibling is already gone.
        uint8 followEvicted = 0;
        if (!botAI->HasStrategy("stay", BOT_STATE_COMBAT))
        {
            if (botAI->HasStrategy("follow", BOT_STATE_COMBAT))
                followEvicted |= 0x1;
            DcStrategyGate::RequestStrategy(botAI->GetBot()->GetObjectGuid(), "+stay", static_cast<uint8>(BOT_STATE_COMBAT));
            added |= 0x1;
        }
        if (!botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT))
        {
            if (botAI->HasStrategy("follow", BOT_STATE_NON_COMBAT))
                followEvicted |= 0x2;
            DcStrategyGate::RequestStrategy(botAI->GetBot()->GetObjectGuid(), "+stay", static_cast<uint8>(BOT_STATE_NON_COMBAT));
            added |= 0x2;
        }
        {
            std::lock_guard<std::mutex> lock(g_dcPassiveMutex);
            g_dcPassivePlayers.insert(follower->GetObjectGuid());
            g_dcHealerStayStates[follower->GetObjectGuid()] = added;
            // Only record an eviction we actually caused this pin; if the bot was
            // already pinned (added==0 for that state) we did not touch "follow",
            // so the prior pin's record (if any) must survive untouched.
            if (followEvicted)
                g_dcHealerFollowStates[follower->GetObjectGuid()] |= followEvicted;
            // Re-holding within the post-release grace window: cancel any pending
            // pet release so a defensive pet isn't flipped unexpectedly.
            g_dcPetReleaseAt.erase(follower->GetObjectGuid());
        }
        DC_PULL_DEBUG("[DC:{}] advanced-pull: healer pinned at camp (stay, heals only)",
                      follower->GetName());
        return;
    }

    // A passive the player set themselves is left entirely alone: don't add a
    // duplicate, and don't record it (so we never strip it on release).
    if (botAI->HasStrategy("passive", BOT_STATE_COMBAT))
        return;

    DcStrategyGate::RequestStrategy(botAI->GetBot()->GetObjectGuid(), "+passive", static_cast<uint8>(BOT_STATE_COMBAT));
    if (Pet* pet = follower->GetPet())
        pet->SetReactState(REACT_PASSIVE);

    {
        std::lock_guard<std::mutex> lock(g_dcPassiveMutex);
        g_dcPassivePlayers.insert(follower->GetObjectGuid());
        // Re-holding within the post-release grace window: cancel the pending
        // pet release so the pet we just set passive isn't flipped back.
        g_dcPetReleaseAt.erase(follower->GetObjectGuid());
    }

    DC_PULL_DEBUG("[DC:{}] advanced-pull: held passive at camp", follower->GetName());
}
void DcFollowerLifecycle::RemoveFollowerPassive(Player* follower)
{
    if (!follower)
        return;

    uint8 healerStay = 0;
    uint8 followRestore = 0;
    bool isHealerStay = false;
    {
        std::lock_guard<std::mutex> lock(g_dcPassiveMutex);
        // Only strip passive we applied; a manual passive was never recorded.
        if (!g_dcPassivePlayers.erase(follower->GetObjectGuid()))
            return;
        // The release is happening now — drop any armed graceful-release timer.
        g_dcPlayerReleaseAt.erase(follower->GetObjectGuid());
        // A held healer carries the "stay" pin instead of "+passive": remember
        // which states WE added so we strip exactly those (and leave a player's
        // own manual stay alone).
        auto it = g_dcHealerStayStates.find(follower->GetObjectGuid());
        if (it != g_dcHealerStayStates.end())
        {
            isHealerStay = true;
            healerStay = it->second;
            g_dcHealerStayStates.erase(it);
        }
        // ...and which states had their "follow" sibling evicted by our `+stay`,
        // so we can put it back. Always cleared here so a stale record can't leak.
        auto fit = g_dcHealerFollowStates.find(follower->GetObjectGuid());
        if (fit != g_dcHealerFollowStates.end())
        {
            followRestore = fit->second;
            g_dcHealerFollowStates.erase(fit);
        }
    }

    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(follower))
    {
        // Healer release: drop the movement pin so it can rejoin the party, then
        // fall through (a held healer was never made passive, so the passive
        // strip below is a harmless no-op and the pet was never touched).
        if (isHealerStay)
        {
            if ((healerStay & 0x1) && botAI->HasStrategy("stay", BOT_STATE_COMBAT))
                DcStrategyGate::RequestStrategy(botAI->GetBot()->GetObjectGuid(), "-stay", static_cast<uint8>(BOT_STATE_COMBAT));
            if ((healerStay & 0x2) && botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT))
                DcStrategyGate::RequestStrategy(botAI->GetBot()->GetObjectGuid(), "-stay", static_cast<uint8>(BOT_STATE_NON_COMBAT));
            // Restore the "follow" strategy our `+stay` evicted (sibling removal in
            // Engine::addStrategy). Without this the healer keeps no movement
            // strategy in its non-combat engine and just stands there once the run
            // ends / `dc off` — the "healer stuck in stay" bug. Guard on HasStrategy
            // so we never double-add if something re-installed it meanwhile.
            if ((followRestore & 0x1) && !botAI->HasStrategy("follow", BOT_STATE_COMBAT))
                DcStrategyGate::RequestStrategy(botAI->GetBot()->GetObjectGuid(), "+follow", static_cast<uint8>(BOT_STATE_COMBAT));
            if ((followRestore & 0x2) && !botAI->HasStrategy("follow", BOT_STATE_NON_COMBAT))
                DcStrategyGate::RequestStrategy(botAI->GetBot()->GetObjectGuid(), "+follow", static_cast<uint8>(BOT_STATE_NON_COMBAT));
            DC_PULL_DEBUG("[DC:{}] advanced-pull: healer released (stay cleared, "
                          "follow restored: {})", follower->GetName(), followRestore);
            return;
        }

        if (botAI->HasStrategy("passive", BOT_STATE_COMBAT))
            DcStrategyGate::RequestStrategy(botAI->GetBot()->GetObjectGuid(), "-passive", static_cast<uint8>(BOT_STATE_COMBAT));

        // Hold the pet passive a little longer than its owner: releasing them in
        // lockstep lets the pet bolt in and pull aggro off the tank before he's
        // settled. Schedule the flip back to REACT_DEFENSIVE; ReapPetReleases
        // (ticked from ReapStrandedPassives) applies it once the delay elapses.
        // A non-positive delay reverts to the old immediate release.
        float const delaySec = DcSettings::GetFloat(follower, "PullPetReleaseDelay");
        if (delaySec > 0.0f && follower->GetPet())
        {
            std::lock_guard<std::mutex> lock(g_dcPassiveMutex);
            g_dcPetReleaseAt[follower->GetObjectGuid()] =
                getMSTime() + uint32(delaySec * 1000.0f);
        }
        else if (Pet* pet = follower->GetPet())
            pet->SetReactState(REACT_DEFENSIVE);

        DC_PULL_DEBUG("[DC:{}] advanced-pull: released from passive", follower->GetName());
    }
}
void DcFollowerLifecycle::ReapStrandedPassives()
{
    // Apply any due pet releases first: these persist after the owner has been
    // dropped from g_dcPassivePlayers, so they must run even when the player
    // sweep below early-returns on an empty set.
    ReapPetReleases();

    std::vector<ObjectGuid> marked;
    {
        std::lock_guard<std::mutex> lock(g_dcPassiveMutex);
        if (g_dcPassivePlayers.empty())
            return;
        marked.assign(g_dcPassivePlayers.begin(), g_dcPassivePlayers.end());
    }

    for (ObjectGuid guid : marked)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);

        // Player gone, or its AI was deleted (self-bot left bot mode): we can no
        // longer drive a ChangeStrategy, so just drop the mark. The strategy was
        // never persisted to the DB (direct ChangeStrategy doesn't Save), so it
        // evaporates with the engine anyway.
        if (!player || !player->IsInWorld() || !GET_PLAYERBOT_AI(player))
        {
            std::lock_guard<std::mutex> lock(g_dcPassiveMutex);
            g_dcPassivePlayers.erase(guid);
            g_dcPlayerReleaseAt.erase(guid);
            g_dcHealerStayStates.erase(guid);
            // Engine gone -> its strategies (stay AND the "follow" we evicted) are
            // rebuilt fresh by AiFactory on the next bot, so there is nothing to
            // restore; just drop the record so it can't leak.
            g_dcHealerFollowStates.erase(guid);
            continue;
        }

        uint32 phase = 0;
        Position camp;
        bool const inPull = DcLeaderSignal::GetLeaderPullInfo(player, phase, camp);

        // Release this follower the moment the leader leaves a holding phase.
        // GetLeaderPullInfo returns true for Engage too (only Idle returns
        // false), so a plain !inPull test would keep the party passive through
        // the entire camp fight: the tank flips to Engage on arrival but only
        // drops to Idle out of combat — which never happens while it's tanking.
        // Gate on IsPullPhaseHolding instead so Engage releases the party (per
        // the DcPullPhase contract: "Idle and Engage release them"), along with
        // pull off / dc off / paused / leader gone. The single authoritative
        // teardown — fires regardless of the follower's own engine state.
        // A standing camp-safety release (the valve fired but the drag was KEPT —
        // see DcPullContext::SafetyRelease) strips the party's passive at once
        // even though the leader is still in a holding phase: the whole point of
        // the release is that the held followers fight back NOW.
        bool const released = DcLeaderSignal::IsLeaderPullHoldReleased(player);
        if (!inPull || !DcLeaderSignal::IsPullPhaseHolding(phase) || released)
        {
            // The graceful pull commit (leader reached camp and flipped to
            // Engage, so inPull is still true) can hold the party passive a
            // little longer to give the tank a threat head start before DPS
            // pile on — the player-side analogue of PullPetReleaseDelay, and the
            // clock the pet delay then stacks on. A hard exit (run ended,
            // paused, dc off, leader gone -> !inPull) always releases at once,
            // as does a zero delay — and so does a camp-safety release, whether
            // the drag was kept (released above) or the pull was aborted into
            // Engage (partyReleased survives that transition for exactly this).
            float const delaySec =
                (inPull && !released) ? DcSettings::GetFloat(player, "PullPlayerReleaseDelay") : 0.0f;
            if (delaySec <= 0.0f)
            {
                RemoveFollowerPassive(player);
                continue;
            }

            uint32 const now = getMSTime();
            bool releaseNow = false;
            {
                std::lock_guard<std::mutex> lock(g_dcPassiveMutex);
                auto it = g_dcPlayerReleaseAt.find(guid);
                if (it == g_dcPlayerReleaseAt.end())
                    // First Engage tick for this follower — arm the timer.
                    g_dcPlayerReleaseAt[guid] = now + uint32(delaySec * 1000.0f);
                else if (int32(now - it->second) >= 0)
                    releaseNow = true;
            }
            if (releaseNow)
                RemoveFollowerPassive(player);
            continue;
        }

        // Still in a holding phase. If a graceful-release timer was armed (a
        // brief Engage flicker, or the leader re-entered a hold), disarm it so
        // the party isn't dropped mid-hold.
        {
            std::lock_guard<std::mutex> lock(g_dcPassiveMutex);
            g_dcPlayerReleaseAt.erase(guid);
        }

        // Camp-safety valve: a held, passive follower taking real damage (a
        // patrol clipped camp, or the pull went sideways) can't defend itself —
        // release the party so it can fight back. This is an emergency, so it
        // bypasses the graceful release delay entirely. Two refinements over the
        // original raw HP% check (both mirror the CC-assist gate's shape):
        //   1. Attribution: damage from the pack being DRAGGED is an ordinary
        //      drag taking splash, not a failed pull — the valve must not throw
        //      the drag away for it. Only a non-pull attacker qualifies.
        //   2. Grace (PullSafetyGrace): the qualifying state must persist, so a
        //      single stray hit that the follower shrugs off doesn't dump the
        //      whole maneuver on a flicker.
        // The release itself KEEPS an in-flight drag (ReleaseLeaderPullHold):
        // aborting a Returning drag fights the pack wherever the party happens
        // to be standing — the exact over-pull the drag exists to prevent.
        // Through DcSettings (cached conf read + per-run override), not raw
        // sConfigMgr — a per-tick GetOption here re-walks the config map and
        // re-logs a missing-property warning every tick a pull is held.
        float const safetyHp = DcSettings::GetFloat(player, "PullSafetyHpPct");
        if (player->IsInCombat() && safetyHp > 0.0f &&
            player->GetHealthPct() < safetyHp)
        {
            // Attacker attribution: does EVERYTHING currently hitting this
            // follower belong to the pack being dragged? Same destination-pack
            // test as EnRoutePackAvoidPoint: the tagged target itself, its
            // formation, or proximity to it for the (common) unformationed pack.
            bool attackerIsPullTarget = false;
            ObjectGuid const pullGuid = DcLeaderSignal::GetLeaderPullTarget(player);
            Unit* const pullTarget =
                pullGuid ? ObjectAccessor::GetUnit(*player, pullGuid) : nullptr;
            if (pullTarget && !player->getAttackers().empty())
            {
                CreatureGroup const* targetGroup = pullTarget->ToCreature()
                    ? pullTarget->ToCreature()->GetFormation() : nullptr;
                uint32 const targetFormation = targetGroup ? targetGroup->GetId() : 0u;
                constexpr float kPackRadius = 12.0f;  // = EnRoutePackAvoidPoint's
                attackerIsPullTarget = true;
                for (Unit* a : player->getAttackers())
                {
                    if (!a || a == pullTarget)
                        continue;
                    Creature const* c = a->ToCreature();
                    CreatureGroup const* grp = c ? c->GetFormation() : nullptr;
                    if (targetFormation && grp && grp->GetId() == targetFormation)
                        continue;
                    if (pullTarget->GetExactDist2d(a) <= kPackRadius)
                        continue;
                    attackerIsPullTarget = false;
                    break;
                }
            }

            // Latch lives in the FOLLOWER's own pull-context copy (the leader's
            // copy carries the FSM; a follower's is otherwise unused) — armed
            // fresh per hold by ApplyFollowerPassive.
            uint32& since = GET_PLAYERBOT_AI(player)->GetAiObjectContext()
                ->GetValue<DcPullContext&>(DcKey::PullContext)->Get().campSafetySince;
            uint32 const graceMs =
                uint32(DcSettings::GetFloat(player, "PullSafetyGrace") * 1000.0f);
            uint32 sinceOut = since;
            bool const trip = DungeonClearMath::ShouldTripCampSafety(
                /*inCombat*/ true, player->GetHealthPct(), safetyHp,
                attackerIsPullTarget, since, getMSTime(), graceMs, sinceOut);
            since = sinceOut;
            if (trip)
            {
                DC_PULL_INFO("[DC:{}] advanced-pull SAFETY: passive follower at "
                             "{:.0f}% in combat with a non-pull attacker -> "
                             "releasing party",
                             player->GetName(), player->GetHealthPct());
                DcLeaderSignal::ReleaseLeaderPullHold(player);
                RemoveFollowerPassive(player);
            }
        }
        else
        {
            // Not qualifying at all — keep the grace latch clear so the next
            // qualifying spell starts its own grace from scratch.
            if (PlayerbotAI* ai = GET_PLAYERBOT_AI(player))
                ai->GetAiObjectContext()
                    ->GetValue<DcPullContext&>(DcKey::PullContext)->Get().campSafetySince = 0;
        }
    }
}
