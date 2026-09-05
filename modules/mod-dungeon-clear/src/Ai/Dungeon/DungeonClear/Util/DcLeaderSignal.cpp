/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcLeaderSignal.h"

#include "DungeonClearUtil.h"   // DC_PULL_* log macros
#include "DungeonClearMath.h"
#include "DungeonClearTuning.h"
#include "DcZoneLine.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"
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
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "Chat.h"
#include "ServerFacade.h"
#include "StringFormat.h"
#include "Timer.h"
#include "World.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/SealedEncounterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcEngageGeometry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearLiveBossValue.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/DcRunState.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

namespace
{
    // Scout-trail reachability probe. Now a thin alias for the shared
    // DcEngageGeometry::IsNavReachable (was a byte-identical file-local twin here
    // and in DcPullPlanner). Kept as a local name so the call sites read unchanged.
    inline bool IsNavReachable(Player* bot, Position const& p)
    {
        return DcEngageGeometry::IsNavReachable(bot, p);
    }

    // A trail hold point must clear the instance zone line, for the same reason a
    // pull camp must (DcZoneLine): the tank walked IN through the entrance, so
    // the oldest breadcrumbs of a run sit on the exit trigger, and a follower
    // told to hold `lag` yards back at the start of a dungeon is told to stand on
    // the way out. A headless bot shrugs that off — it sends no areatrigger
    // packet — but a self-bot's client reports it and the player is teleported
    // out of the instance.
    inline bool TrailOverZoneLine(Player* bot, Position const& p)
    {
        return DcZoneLine::WouldCrossTheLine(bot, p.GetPositionX(), p.GetPositionY(),
                                             p.GetPositionZ());
    }

    // Leader-election memo. FindLeaderTank is on the hot path: nearly every DC
    // trigger gate of every bot consults it each AI tick (IsEnabled, the
    // cross-bot camp/party-tank reads, the multiplier), and each call walks the
    // whole group with a GET_PLAYERBOT_AI lookup per member — O(triggers x
    // members) per tick, noticeable in a 40-bot raid. The election result is
    // stable on tick timescales, so cache it per (group, map instance) for a few
    // hundred ms. Correctness is preserved by validating the cached leader on
    // every read (alive, same Map, still a tank bot, still in this group) and
    // falling through to a full re-scan the instant validation fails — so a
    // leader death/leave is picked up immediately; only the *election of a
    // different eligible tank* (lower GUID joining, no current-leader change)
    // can lag by up to the TTL, which no consumer is sensitive to. Negative
    // results ("group has no tank bot") are cached too — groups without a tank
    // query just as often.
    //
    // Only the GUID is memoised, never a Player*, and validation re-resolves it
    // through ObjectAccessor — this memo is what makes it safe for the party-tank
    // value above it to be uncached (#20). Mutex-guarded like the other
    // file-scope registries; the critical section is the map lookup only, with
    // validation deliberately outside it (MapUpdater threads run this
    // concurrently for every DC bot in every instance).
    constexpr uint32 LEADER_CACHE_TTL_MS = 250;
    // Lazy janitor bound: past this many entries, stale rows (disbanded groups,
    // emptied maps) are swept on the next store.
    constexpr size_t LEADER_CACHE_SWEEP_SIZE = 256;

    struct LeaderCacheEntry
    {
        ObjectGuid leader;   // Empty = cached "no tank bot in this group"
        uint32 stampMs = 0;
    };

    // Keyed on the MAP INSTANCE, not the map id: two members of one group can sit
    // in two different copies of the same dungeon (each bound to its own instance
    // id), which share a map id but are separate Map objects on separate
    // MapUpdater threads. Keying on the id alone made those two copies share one
    // cache row. Packed as (mapId << 32 | instanceId) so continents — where every
    // instance id is 0 — still separate by map.
    inline uint64 LeaderCacheMapKey(Player* reference)
    {
        return (static_cast<uint64>(reference->GetMapId()) << 32) |
               static_cast<uint64>(reference->GetInstanceId());
    }

    std::map<std::pair<uint64, uint64>, LeaderCacheEntry> g_leaderCache;
    std::mutex g_leaderCacheMutex;

    // The cached GUID is only trusted while the player it names would still win
    // (or at least remain) the election from `reference`'s point of view.
    //
    // Resolves the GUID through ObjectAccessor rather than trusting a stored
    // pointer, and compares the MAP OBJECT rather than the map id — a groupmate in
    // another copy of the same dungeon passes a GetMapId() test while living on a
    // different Map, updated by a different MapUpdater thread. Electing it as
    // leader hands every follower a cross-thread pointer to write through (#20).
    //
    // Called OUTSIDE g_leaderCacheMutex: it touches no cache state, and now that
    // the party-tank value is uncached this runs on every read, so it must not
    // hold a global lock across two hash-map lookups.
    Player* ValidateCachedLeader(Player* reference, Group* group, ObjectGuid guid)
    {
        if (guid.IsEmpty())
            return nullptr;
        Player* leader = ObjectAccessor::FindPlayer(guid);
        if (!leader || !leader->IsAlive())
            return nullptr;
        if (leader->FindMap() != reference->FindMap())
            return nullptr;
        if (leader->GetGroup() != group)
            return nullptr;
        if (!PlayerbotAI::IsTank(leader) || !GET_PLAYERBOT_AI(leader))
            return nullptr;
        return leader;
    }

    // Return (and maintain) the leader's combat-start stamp: 0 while the leader is
    // out of combat; the getMSTime() its current combat was first observed once it
    // is in combat (stable across the fight). This lets the threat-lead window
    // (DungeonClearMath::ShouldReleaseFollower) measure from a FRESH combat start on
    // the Leeroy / walk-in / general-assist path, which — unlike the advanced-pull
    // camp — has no pull-phase transition to mark fight start. MUST be called every
    // tick a leader is resolved — including ticks where no assist is wanted — so an
    // out-of-combat window clears the stamp instead of leaving a stale one that
    // would zero the next fight's lead.
    //
    // The stamp lives on the leader's own DcRunState (was the g_leaderCombatSince
    // file-static map + mutex + lazy sweep). No map/mutex is needed: it is a facet
    // of the leader's run, keyed by the leader implicitly, and every group member
    // ticks on the same map thread as the leader — the same single-threaded cross-
    // bot access DcPullContext already relies on. The stamp resets to 0 both here
    // (out-of-combat read) and on run teardown (DcRunState::Reset).
    uint32 LeaderCombatSince(Player* leader)
    {
        if (!leader)
            return 0;
        PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
        if (!leaderAI)
            return 0;
        DcRunState& run = DcRun::Of(leaderAI);

        if (!leader->IsInCombat())
        {
            run.leaderCombatSinceMs = 0;
            return 0;
        }
        if (run.leaderCombatSinceMs != 0)
            return run.leaderCombatSinceMs;

        // First in-combat tick: stamp it. Never store 0 (it reads as "out of
        // combat"); a leader whose combat began at getMSTime()==0 latches to 1.
        uint32 const now = getMSTime();
        run.leaderCombatSinceMs = now != 0 ? now : 1u;
        return run.leaderCombatSinceMs;
    }

    // True when the TANK's fight is live: the reference `bot` — always the elected
    // leader/tank on the assist + scout-lag paths — is in combat, OR a BOT groupmate
    // close enough to share that fight is in combat. Used to arm the leader-fight
    // assist and release the dynamic scout-lag trail. It exists for the leader-flag
    // flicker: when the tank takes a pack before a verdict commits (dynamic Idle +
    // surprise aggro), its IsInCombat() read flickers as it tags/leashes, or a
    // groupmate registers combat a tick first, and keying solely on the leader's own
    // flag left the party parked at lag range watching the tank die.
    //
    // TWO exclusions, both learned from the "dps run to me, not the tank" failure —
    // the signal must speak for THE TANK'S fight, not "anyone, anywhere":
    //   * The HUMAN party leader is never counted. A human flagged in combat OUT OF
    //     LOS of the tank's pull (their own straggler, a phantom flag, a mob across
    //     the room) would otherwise arm the party assist for a fight the tank is not
    //     in; the party then stampedes a tank with no resolvable target and stacks on
    //     the human. Only bots — and the tank's own flag — speak for the tank.
    //   * A bot beyond PartyMaxSpread of the tank is not counted: a lagging bot that a
    //     far/gated spawn phantom-flagged (see the phantom-combat class) must not arm
    //     the assist. The tank's OWN combat is distance 0, so a real pull always
    //     registers regardless of where the stragglers trail.
    // The caller latches the result for PartyCombatLatch seconds, so a one-tick tank
    // flicker is still bridged by a nearby bot that already took a hit.
    bool AnyGroupMemberInCombat(Player* bot)
    {
        if (!bot)
            return false;
        Group* group = bot->GetGroup();
        if (!group)
            return bot->IsInCombat();  // solo: only our own combat counts

        float const spread = DcSettings::GetFloat(bot, "PartyMaxSpread");
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive())
                continue;
            if (member->GetMapId() != bot->GetMapId())
                continue;
            if (!member->IsInCombat())
                continue;
            // The tank's own flag is the primary anchor (distance 0 — always in).
            if (member == bot)
                return true;
            // Otherwise only a BOT close enough to share the tank's fight counts —
            // never the human master, never a far/phantom-flagged straggler.
            if (!GET_PLAYERBOT_AI(member))
                continue;
            if (bot->GetExactDist2d(member) <= spread)
                return true;
        }
        return false;
    }

    // Hysteresis latch over AnyGroupMemberInCombat. See the PartyCombatLatch
    // registry note: a bare in-combat read is a TOCTOU gate — combat begins/drops
    // on ticks we don't control, and a single stale/false reading must not flip the
    // party out of "help" mode. Once ANY member is seen in combat we stamp the
    // leader's run state and report engaged for graceMs after the last positive
    // observation, so a lone false reading (or a one-tick combat gap from a
    // leashing/repositioning pack) is absorbed instead of snapping the party back to
    // the far scout-lag ring. The latch is fed every tick the gate is consulted:
    // every follower evaluates the scout-lag / assist triggers each tick, so as long
    // as one is alive the leader's combat is observed promptly.
    //
    // The stamp lives on the leader's own DcRunState (was the g_partyEngagedLatch
    // file-static map + mutex + lazy sweep) — same single-threaded cross-bot access
    // as LeaderCombatSince above; it resets to 0 on run teardown (DcRunState::Reset).
    bool IsPartyEngagedLatched(Player* leader, uint32 graceMs)
    {
        if (!leader)
            return false;
        // graceMs == 0 -> latch disabled, fall back to the bare instantaneous read.
        bool const live = AnyGroupMemberInCombat(leader);
        if (graceMs == 0)
            return live;

        PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
        if (!leaderAI)
            return live;  // no run state to latch into; instantaneous read
        DcRunState& run = DcRun::Of(leaderAI);
        uint32 const now = getMSTime();
        if (live)
        {
            run.partyEngagedLatchMs = now != 0 ? now : 1u;
            return true;
        }
        if (run.partyEngagedLatchMs == 0)
            return false;
        if (getMSTimeDiff(run.partyEngagedLatchMs, now) < graceMs)
            return true;
        run.partyEngagedLatchMs = 0;
        return false;
    }
}

Player* DcLeaderSignal::FindLeaderTank(Player* reference)
{
    if (!reference)
        return nullptr;

    Group* group = reference->GetGroup();
    if (!group)
    {
        // Solo: a tank bot leads itself; anyone else has no leader. Cheap —
        // no group walk, so no caching either.
        return (PlayerbotAI::IsTank(reference) && GET_PLAYERBOT_AI(reference))
                   ? reference : nullptr;
    }

    // The election is per (group, map instance): members on another Map are
    // excluded from the candidate set, so two parties of one raid split across
    // maps — or across two copies of the same dungeon — legitimately resolve
    // different leaders.
    std::pair<uint64, uint64> const key(group->GetObjectGuid().GetRawValue(),
                                        LeaderCacheMapKey(reference));
    uint32 const now = getMSTime();

    // Read the memo under the lock, validate outside it. The validation is two
    // hash-map lookups and does not touch the cache, and every party-tank read
    // now lands here (the value is uncached — see DungeonClearPartyTankValue),
    // so keeping it inside would hold one global mutex across all of them.
    bool fresh = false;
    ObjectGuid memoLeader;
    {
        std::lock_guard<std::mutex> lock(g_leaderCacheMutex);
        auto it = g_leaderCache.find(key);
        if (it != g_leaderCache.end() &&
            getMSTimeDiff(it->second.stampMs, now) < LEADER_CACHE_TTL_MS)
        {
            fresh = true;
            memoLeader = it->second.leader;
        }
    }
    if (fresh)
    {
        // Negative hit: this group/map had no eligible tank bot moments ago.
        // Trust it for the TTL; the next restamp flips it the moment one appears.
        if (memoLeader.IsEmpty())
            return nullptr;
        // Positive hit: trust it only while it still validates. A failed
        // validation (died / left map / left group / AI gone) falls through to a
        // fresh scan below — never a stale leader.
        if (Player* cached = ValidateCachedLeader(reference, group, memoLeader))
            return cached;
    }

    // FLAG-BOUND SHORT-CIRCUIT, before any tank election. Exactly one member
    // carries the run flag (`dungeon clear enabled`), and the flag and the
    // election must never diverge: live (runs 40/42/43), a mid-run
    // ResetStrategies (boss-XP level-up -> gear hook) re-loaded a lower-GUID
    // ex-tank's stored TANK strategy set, IsTank flipped true for it, and the
    // election moved to a bot WITHOUT the flag - deadlock with every readable
    // condition green: the elected had no flag (IsEnabled false), the flagged
    // one was no longer elected (IsDungeonClearLeader false), and the idle
    // trigger went dark on both while route/enabled/pull all diagnosed
    // healthy. A flagged, living member on this map IS the leader; the
    // election below only decides when nobody is flagged (pre-run, or the
    // flag owner died or left).
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive())
            continue;
        if (member->FindMap() != reference->FindMap())
            continue;
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI || !DcRun::Of(memberAI).enabled)
            continue;
        {
            std::lock_guard<std::mutex> lock(g_leaderCacheMutex);
            g_leaderCache[key] = LeaderCacheEntry{member->GetObjectGuid(), now};
        }
        return member;
    }

    // Candidate set: alive tank BOTS on the reference's map. GetFirstMember walks
    // every member of the group — including all raid sub-groups — so each member
    // sees the same candidate set and elects the same leader.
    //
    // Election rule differs by group kind:
    //   * Party (5-man): lowest-GUID tank bot — a deterministic, state-free pick.
    //   * Raid: the bot flagged Main Tank (MEMBER_FLAG_MAINTANK) wins outright; if
    //     none is flagged (or the flagged member isn't an eligible tank bot), the
    //     eligible tank bot with the highest equipped gear score wins, GUID-tiebroken.
    // Raids carry several tanks across sub-groups and the player expects the one
    // they designated MT to drive; absent an MT, the best-geared tank is the most
    // survivable choice to hold threat for the whole raid.
    bool const isRaid = group->isRaidGroup();

    // The MT designation lives on the member SLOT flags, not on the Player, so it
    // is read from the group's slot list (a real-player MT is resolved to a Player
    // below and rejected like any non-bot tank, falling through to gear score).
    ObjectGuid mainTankGuid;
    if (isRaid)
    {
        // Tortoise port: 1.12 groups carry no member role flags - main tank
        // assignment arrived with raid targets later. The branch below never
        // finds one, and the caller's fallback (leader, then first warrior)
        // carries the raid case the way it already carries five-mans.
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            if (false)
            {
                mainTankGuid = slot.guid;
                break;
            }
        }
    }

    Player* leader = nullptr;
    uint32 bestGearScore = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive())
            continue;
        // Same Map OBJECT, not merely the same map id — see ValidateCachedLeader.
        if (member->FindMap() != reference->FindMap())
            continue;
        if (!PlayerbotAI::IsTank(member))
            continue;
        // Only bot tanks can drive — a real-player tank has no PlayerbotAI to
        // run the clear, so it can never be the leader.
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI)
            continue;

        if (!isRaid)
        {
            // Party: lowest GUID.
            if (!leader || member->GetObjectGuid() < leader->GetObjectGuid())
                leader = member;
            continue;
        }

        // Raid: a flagged, eligible Main Tank bot wins immediately — no other
        // candidate can outrank an explicit player designation.
        if (!mainTankGuid.IsEmpty() && member->GetObjectGuid() == mainTankGuid)
        {
            leader = member;
            break;
        }

        // Otherwise rank by equipped gear score (GUID as the stable tiebreak so
        // every member still converges on the same leader).
        uint32 const gearScore = memberAI->GetEquipGearScore(member);
        if (!leader || gearScore > bestGearScore ||
            (gearScore == bestGearScore && member->GetObjectGuid() < leader->GetObjectGuid()))
        {
            leader = member;
            bestGearScore = gearScore;
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_leaderCacheMutex);
        if (g_leaderCache.size() > LEADER_CACHE_SWEEP_SIZE)
        {
            for (auto it = g_leaderCache.begin(); it != g_leaderCache.end();)
            {
                if (getMSTimeDiff(it->second.stampMs, now) >= LEADER_CACHE_TTL_MS)
                    it = g_leaderCache.erase(it);
                else
                    ++it;
            }
        }
        g_leaderCache[key] = LeaderCacheEntry{
            leader ? leader->GetObjectGuid() : ObjectGuid::Empty, now};
    }
    return leader;
}
bool DcLeaderSignal::IsDungeonClearLeader(Player* bot)
{
    return bot && FindLeaderTank(bot) == bot;
}
Player* DcLeaderSignal::FindRunOwner(Player* bot)
{
    auto owns = [](Player* p) -> bool
    {
        if (!p)
            return false;
        PlayerbotAI* ai = GET_PLAYERBOT_AI(p);
        return ai && DcRun::Of(ai).enabled;
    };

    // The living leader is the owner in every healthy case, and resolving it first
    // keeps this on the leader cache's fast path rather than walking the group.
    if (Player* leader = FindLeaderTank(bot))
        if (owns(leader))
            return leader;

    if (!bot)
        return nullptr;

    Group* group = bot->GetGroup();
    if (!group)
        return owns(bot) ? bot : nullptr;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member->GetMapId() != bot->GetMapId())
            continue;
        if (owns(member))
            return member;
    }
    return nullptr;
}

Player* DcLeaderSignal::FindTerminalDriver(Player* bot)
{
    Player* owner = FindRunOwner(bot);
    if (!owner)
        return nullptr;

    PlayerbotAI* ownerAI = GET_PLAYERBOT_AI(owner);
    if (!ownerAI)
        return nullptr;
    DcRunState const& run = DcRun::Of(ownerAI);
    if (!run.enabled || run.paused)
        return nullptr;  // a pause holds the terminal rungs exactly as it holds the rest

    // Healthy case first: an elected leader exists, so it drives, and this is
    // byte-for-byte the old behaviour.
    if (Player* leader = FindLeaderTank(owner))
        return leader;

    Group* group = owner->GetGroup();
    if (!group)
        return owner;  // solo tank: it is the only candidate either way

    // No leader — the tank is dead. Elect deterministically so every member agrees
    // and exactly one fires. Living members outrank corpses (a living member can
    // actually act on the verdict); the all-dead pass is what lets a full wipe
    // still reach the party-died bailout.
    Player* bestAlive = nullptr;
    Player* bestAny = nullptr;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member->GetMapId() != owner->GetMapId())
            continue;
        if (!GET_PLAYERBOT_AI(member))
            continue;  // a real player has no AI to run the rung
        if (!bestAny || member->GetObjectGuid() < bestAny->GetObjectGuid())
            bestAny = member;
        if (member->IsAlive() && (!bestAlive || member->GetObjectGuid() < bestAlive->GetObjectGuid()))
            bestAlive = member;
    }
    return bestAlive ? bestAlive : bestAny;
}

bool DcLeaderSignal::IsTerminalDriver(Player* bot)
{
    return bot && FindTerminalDriver(bot) == bot;
}

bool DcLeaderSignal::IsInPausedDungeonClearRun(Player* bot)
{
    if (!bot)
        return false;

    // Resolve the run owner from any member (the leader resolves to itself).
    Player* leader = FindLeaderTank(bot);
    if (!leader)
        return false;

    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return false;

    AiObjectContext* leaderCtx = leaderAI->GetAiObjectContext();
    DcRunState const& run = DcRun::Of(leaderCtx);
    return run.enabled && run.paused;
}
bool DcLeaderSignal::IsLeaderDroppingInHole(Player* bot)
{
    if (!bot)
        return false;

    Player* leader = FindLeaderTank(bot);
    if (!leader || leader == bot)
        return false;  // the leader runs the drop; only followers hold for it

    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return false;

    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    if (!DcRun::Of(ctx).enabled)
        return false;

    // The leader's active anchored-event step must be a DropInHole.
    std::optional<DungeonBossInfo> const next =
        ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next.has_value() || next->kind != DungeonAnchorKind::Objective || !next->eventId)
        return false;

    DungeonEvent const* ev = DungeonEventRegistry::Find(next->mapId, next->eventId);
    if (!ev)
        return false;

    DungeonEventProgress const& prog =
        ctx->GetValue<DungeonEventProgress&>(DcKey::EventProgress)->Get();
    if (prog.eventId != ev->id || prog.stepIndex >= ev->steps.size())
        return false;

    // Hold for the WHOLE time the DropInHole step is active — including the landing
    // tick, on which the RunStep gate teleports the party down and advances the
    // step in one go. Gating on "not yet landed" instead would open a one-tick race:
    // a follower ticking after the leader lands but before the same-tick teleport
    // would read "not dropping" and try to follow the just-landed leader, which is
    // still across the un-pathable gap — clipping for that tick. The step stops
    // being active (stepIndex advances) in the same tick the teleport fires, so the
    // hold releases exactly when the followers are already down on the landing.
    return ev->steps[prog.stepIndex].kind == EventStepKind::DropInHole;
}

Creature* DcLeaderSignal::GetLeaderEscortee(Player* bot)
{
    if (!bot)
        return nullptr;

    Player* leader = FindLeaderTank(bot);
    if (!leader)
        return nullptr;

    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return nullptr;

    // Only while the leader's clear is actively running and unpaused — mirrors the
    // party-tank / follow semantics so a paused run stops treating the escortee as
    // a heal target too.
    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    if (!DcRun::Of(ctx).enabled || DcRun::Of(ctx).paused)
        return nullptr;

    // The leader's active anchored-event step must be an EscortCreature.
    std::optional<DungeonBossInfo> const next =
        ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next.has_value() || next->kind != DungeonAnchorKind::Objective || !next->eventId)
        return nullptr;

    DungeonEvent const* ev = DungeonEventRegistry::Find(next->mapId, next->eventId);
    if (!ev)
        return nullptr;

    DungeonEventProgress const& prog =
        ctx->GetValue<DungeonEventProgress&>(DcKey::EventProgress)->Get();
    if (prog.eventId != ev->id || prog.stepIndex >= ev->steps.size())
        return nullptr;

    EventStep const& step = ev->steps[prog.stepIndex];
    if (step.kind != EventStepKind::EscortCreature || !step.creatureEntry)
        return nullptr;

    // Find the escortee near the REQUESTING bot (the would-be healer): the caller
    // still range/LOS-gates it, but scanning from `bot` keeps the reposition mover
    // working off a creature it can actually path to. Generous radius so a healer
    // trailing the mounted Tarren Mill ride still resolves him.
    float const searchR = step.radius > 0.0f ? step.radius : 100.0f;
    return bot->FindNearestCreature(step.creatureEntry, searchR, /*alive*/ true);
}

bool DcLeaderSignal::IsPullPhaseHolding(uint32 phase)
{
    return phase == static_cast<uint32>(DcPullPhase::Forming) ||
           phase == static_cast<uint32>(DcPullPhase::Advancing) ||
           phase == static_cast<uint32>(DcPullPhase::Returning);
}
bool DcLeaderSignal::GetLeaderPullInfo(Player* bot, uint32& phaseOut, Position& campOut)
{
    if (!bot)
        return false;

    Player* leader = FindLeaderTank(bot);
    if (!leader)
        return false;

    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return false;

    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    // Pull behavior only matters while the run is live and pull mode is on. A
    // paused/off leader holds the whole party via the normal gates — EXCEPT
    // while a maneuver phase is actually holding the party at camp: `dc pause`
    // mid-drag deliberately lets the drag finish (abandoning the tank with a
    // live pack on it is worse; see DungeonClearPullManeuverTrigger), so the
    // holding signal must survive the pause or the followers would be released
    // from camp — and ReapStrandedPassives would strip their passive — while
    // the tank is still dragging the pack home. Once the phase resolves to
    // Engage/Idle the paused gate takes over and the run holds as usual.
    DcPullContext const& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    // `bossPullback` substitutes for the pull-mode bool: a BossPullbackRegistry
    // drag runs at any pull setting (see DungeonClearPullTrigger), and the party
    // MUST hold at the anchor through it — that hold is what keeps everyone but
    // the tank out of the water while the tank fetches the boss.
    if (!DcRun::Of(ctx).enabled ||
        (!ctx->GetValue<bool>(DcKey::PullMode)->Get() && !pull.bossPullback))
        return false;

    if (pull.phase == DcPullPhase::Idle)
        return false;
    if (!IsPullPhaseHolding(static_cast<uint32>(pull.phase)) &&
        DcRun::Of(ctx).paused)
        return false;

    phaseOut = static_cast<uint32>(pull.phase);
    campOut = pull.camp;
    return true;
}
bool DcLeaderSignal::GetLeaderCampHold(Player* bot, Position& campOut, bool& passiveOut)
{
    if (!bot)
        return false;

    Player* leader = FindLeaderTank(bot);
    if (!leader || leader == bot)
        return false;  // the leader drives; it never holds at its own camp

    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return false;

    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    DcPullContext const& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    // `bossPullback` substitutes for the pull-mode bool — see GetLeaderPullInfo.
    if (!DcRun::Of(ctx).enabled ||
        (!ctx->GetValue<bool>(DcKey::PullMode)->Get() && !pull.bossPullback))
        return false;

    // Honor `paused` only while NO maneuver phase is holding: a pause mid-drag
    // lets the drag finish (see DungeonClearPullManeuverTrigger), and the party
    // must stay pinned at camp until it resolves to Engage — releasing them
    // here would walk them into the inbound pack. Same rule as
    // GetLeaderPullInfo above; `enabled` stays first so `dc off` still releases
    // everyone instantly.
    if (!IsPullPhaseHolding(static_cast<uint32>(pull.phase)) &&
        DcRun::Of(ctx).paused)
        return false;
    Position const camp = pull.camp;
    // No camp marked yet (pull mode just toggled on, or a reset cleared it): there
    // is nothing to hold at, so let the caller fall back (briefly) to follow.
    if (!pull.HasCamp())
        return false;

    // SEALED ENCOUNTER, final approach: stop holding at the camp and FOLLOW THE TANK
    // IN. Only between maneuvers — a live maneuver still owns the party.
    //
    // In pull mode the camp survives between pulls by design, so after the last trash
    // stage retires the followers are still pinned at it. For an ordinary boss that is
    // harmless: they get released and run in when the fight starts. For a boss whose
    // room LOCKS on engage (SealedEncounterRegistry) it is the whole bug — Selin's camp
    // is 71.6yd behind his door, so the party is pinned outside a door that shuts the
    // moment the tank engages, and spends the fight at it.
    //
    // Standing the hold down here is also what makes the approach clump satisfiable
    // (DcPartyState::GetSpreadGate) and the muster reachable
    // (DungeonClearAtBossTrigger) rather than a pair of gates asking the party to be
    // somewhere it has been ordered not to go.
    if (!IsPullPhaseHolding(static_cast<uint32>(pull.phase)) && pull.scriptedStage < 0)
    {
        if (std::optional<DungeonBossInfo> const next =
                ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get())
        {
            if (next->kind == DungeonAnchorKind::Boss)
            {
                if (SealedEncounterRow const* const sealed =
                        SealedEncounterRegistry::Find(leader->GetMapId(), next->entry))
                {
                    if (SealedEncounterRegistry::InApproachRange(
                            *sealed, leader->GetPositionX(), leader->GetPositionY(),
                            leader->GetPositionZ(), next->x, next->y, next->z))
                        return false;
                }
            }
        }
    }

    campOut = camp;
    // A standing camp-safety release keeps the maneuver (the tank is still
    // dragging) but frees the party: still camped, no longer passive — the same
    // posture as holding at camp between pulls.
    passiveOut = IsPullPhaseHolding(static_cast<uint32>(pull.phase)) &&
                 !pull.partyReleased;
    return true;
}
bool DcLeaderSignal::IsLeaderCampFightActive(Player* bot)
{
    if (!bot || bot->isDead())
        return false;

    Player* leader = FindLeaderTank(bot);
    if (!leader || leader == bot)
        return false;  // the leader runs the fight; it never assists itself

    uint32 phase = 0;
    Position camp;
    if (!GetLeaderPullInfo(bot, phase, camp))
        return false;

    // Only the camp fight (Engage) — during the holding phases the party is
    // pinned passive at camp by hold-at-camp/stay-at-camp, and a leader-in-combat
    // while merely scouting (Idle) is handled by the drag-back maneuver, which
    // flips the phase out of Idle before any party member would assist.
    return phase == static_cast<uint32>(DcPullPhase::Engage) && leader->IsInCombat();
}
namespace
{
    // The leader's scripted-pull context, or nullptr when `bot` has no leader, is
    // the leader, or the run isn't live. Shared by the two scripted-pull signals.
    DcPullContext const* LeaderScriptedPull(Player* bot)
    {
        if (!bot || bot->isDead())
            return nullptr;

        Player* leader = DcLeaderSignal::FindLeaderTank(bot);
        if (!leader || leader == bot)
            return nullptr;  // the leader has its own leash inside the maneuver

        PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
        if (!leaderAI)
            return nullptr;

        AiObjectContext* ctx = leaderAI->GetAiObjectContext();
        if (!DcRun::Of(ctx).enabled)
            return nullptr;

        DcPullContext const& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
        if (pull.scriptedStage < 0 || !pull.HasCamp())
            return nullptr;
        return &pull;
    }
}

bool DcLeaderSignal::IsLeaderScriptedPullActive(Player* bot)
{
    return LeaderScriptedPull(bot) != nullptr;
}

bool DcLeaderSignal::IsLeaderScriptedCampFight(Player* bot)
{
    DcPullContext const* const pull = LeaderScriptedPull(bot);
    return pull && pull->phase == DcPullPhase::Engage;
}
bool DcLeaderSignal::IsLeaderFightAssistWanted(Player* bot)
{
    if (!bot || bot->isDead())
        return false;

    Player* leader = FindLeaderTank(bot);
    if (!leader || leader == bot)
        return false;

    // Maintain the leader's combat-start stamp on EVERY tick a leader is resolved
    // — before any wanted/not-wanted decision below — so the threat-lead window
    // always measures from a fresh combat start and an out-of-combat window can
    // never leave a stale stamp. (On the advanced-pull camp path the real lead is
    // enforced by the passive-strategy release delay in ReapStrandedPassives; this
    // stamp's lead matters for the Leeroy / walk-in / general-assist paths, which
    // have no passive hold and no pull-phase transition to mark fight start.)
    uint32 const combatSince = LeaderCombatSince(leader);

    bool wanted = false;
    if (IsLeaderCampFightActive(bot))
    {
        // Advanced-pull camp fight: the existing Engage-phase gate.
        wanted = true;
    }
    else
    {
        // Defer to the pull machinery ONLY while a holding phase actually pins the
        // party passive at camp (Forming/Advancing/Returning — the drag must not
        // be broken by followers charging out after the tank). A camp that is
        // merely MARKED (GetLeaderCampHold returns true for ANY phase while pull
        // mode is on) must NOT mute the assist: at phase Idle/Engage with the
        // leader in combat — a walk-in on an aborted pack, the tank chasing a
        // caster/straggler out of the camp fight, scout aggro the maneuver hasn't
        // picked up yet — an unconditional defer left the followers parked at camp
        // watching the tank solo, and an out-of-LOS follower never entered combat
        // at all. The brief Idle+combat window before the maneuver's first combat
        // tick flips the phase to Returning is harmless: the moment the phase
        // turns holding this defers again and stay-at-camp re-pins the party.
        Position camp;
        bool passive = false;
        if (GetLeaderCampHold(bot, camp, passive) && passive)
            return false;

        PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
        if (!leaderAI)
            return false;

        AiObjectContext* ctx = leaderAI->GetAiObjectContext();
        if (!DcRun::Of(ctx).enabled ||
            DcRun::Of(ctx).paused)
            return false;

        // General case — no camp in effect (pull mode off, a Leeroy verdict in
        // dynamic mode, boss walk-ins): the leader tank fighting ANYTHING on an
        // active run means the whole party assists. This covers tank aggro around
        // a corner or beyond a follower's natural engage range, where group combat
        // never propagates and the (multiplier-suppressed) stock pickers would
        // leave the party idling behind.
        //
        // Assist off ANY party member's combat, not just the elected leader's own
        // flag: when the tank takes a pack before a verdict commits (dynamic Idle +
        // surprise aggro — "in combat, no pull state"), the leader's IsInCombat()
        // read flickers as it tags/leashes, and on a tight spiral that left the
        // suppressed party parked at lag range watching the tank die. If anyone in
        // the group is fighting, drive the out-of-combat members in.
        bool const leaderInCombat = leader->IsInCombat();
        // Latched: a bare AnyGroupMemberInCombat read is a TOCTOU gate (combat
        // starts/drops on ticks we don't control). The hysteresis keeps the assist
        // ARMED for PartyCombatLatch seconds after the last positive combat read so
        // a one-tick gap (leash / reposition / stale cross-bot read) can't drop the
        // party back to passive mid-fight. See IsPartyEngagedLatched.
        uint32 const latchMs =
            uint32(DcSettings::GetFloat(leader, "PartyCombatLatch") * 1000.0f);
        bool const groupInCombat = IsPartyEngagedLatched(leader, latchMs);
        // Diagnostic for the spiral death: fires in the divergence this gate fixes —
        // a party fight reads live while the elected leader's own flag reads out of
        // combat. With the old leader-only gate this returned false here and the
        // party stayed passive; this line proves the new path engages.
        //
        // IT PRINTS ITS OWN INPUTS, AND IT HAS TO. The two terms are sampled on
        // DIFFERENT CLOCKS — `group` is latched for PartyCombatLatch seconds past
        // the last positive read, `leader` is instantaneous — so this fires for the
        // whole latch tail after EVERY fight ends, when the leader has legitimately
        // dropped combat. Read as a state ("the tank walked off mid-fight") it is
        // pure artifact: across tp-20260808-162331-1 it covered 2173 seconds in 621
        // episodes, 93% of them <= 4s, piled on the 3.0s latch value. That reading
        // cost a designed, tested and merged feature that then had to be reverted.
        // The stale-ms and the latch window below are what stop the next reader
        // making the same mistake.
        if (groupInCombat && !leaderInCombat)
            DC_PULL_DEBUG("[DC:{}] assist: groupmate in combat while leader reads "
                          "out-of-combat -> assisting (was the no-pull-state stall) "
                          "[leader flag=0 instant | group=1 {} | latch {:.1f}s]",
                          bot->GetName(),
                          AnyGroupMemberInCombat(leader)
                              ? "live"
                              : Acore::StringFormat(
                                    "LATCH TAIL, {}ms since live",
                                    getMSTimeDiff(DcRun::Of(GET_PLAYERBOT_AI(leader))
                                                      .partyEngagedLatchMs,
                                                  getMSTime())),
                          latchMs / 1000.0f);
        wanted = groupInCombat;
    }

    if (!wanted)
        return false;

    // Threat lead. A real group gives the tank a beat to gather the pack and build
    // AoE threat before DPS pile in; piling in instantly is both a bot-tell and a
    // direct contributor to follower/healer over-aggro. Hold this follower for the
    // lead window after the leader's combat began. The lead DURATION reuses
    // PullPlayerReleaseDelay — the same knob that delays DPS release on the
    // advanced-pull camp path — so both follower-release paths share one "threat
    // lead" value. Healers bypass (a withheld heal is a wipe), as does a tank below
    // PullThreatLeadPanicHp (it is losing the fight — pile in). The `alreadyInCombat`
    // bypass is the key one for the "dps run to me" fix: a follower already flagged
    // in combat with the pack out of its line of sight is released ONTO the tank's
    // fight at once, instead of being stranded through the lead where stock follow-
    // master would drift it to the human. Regroup is NOT gated here: it is
    // positioning, not damage, and a healer running for LOS during the lead is fine.
    uint32 const leadMs =
        uint32(DcSettings::GetFloat(leader, "PullPlayerReleaseDelay") * 1000.0f);
    float const panicHp = DcSettings::GetFloat(leader, "PullThreatLeadPanicHp");
    return DungeonClearMath::ShouldReleaseFollower(
        PlayerbotAI::IsHeal(bot), bot->IsInCombat(), combatSince, getMSTime(), leadMs,
        leader->GetHealthPct(), panicHp);
}
bool DcLeaderSignal::IsLeaderShouldAssistFight(Player* bot)
{
    // The leader-side mirror of IsLeaderFightAssistWanted. The followers' gate
    // bails for the leader (leader == bot), so when a follower aggros a pack the
    // tank never saw — around a sharp corner, or after the tank called the pull
    // done and walked off toward the next objective — the tank has no behavior to
    // rejoin: it just freezes on the Advance rest gate while the party fights. This
    // drives it back onto the fight to take threat.
    if (!bot || bot->isDead() || bot->IsInCombat())
        return false;

    // Leader only. A follower's assist is owned by IsLeaderFightAssistWanted.
    Player* leader = FindLeaderTank(bot);
    if (!leader || leader != bot)
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;
    AiObjectContext* ctx = botAI->GetAiObjectContext();
    if (!DcRun::Of(ctx).enabled ||
        DcRun::Of(ctx).paused)
        return false;

    // A pull maneuver that is actively holding the party at camp / dragging owns
    // the tank's positioning — don't divert it mid-drag. (At phase Idle/Engage the
    // tank is free to rejoin a stray fight, exactly as the followers' gate allows.)
    DcPullContext const& pull =
        ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    if (IsPullPhaseHolding(static_cast<uint32>(pull.phase)))
        return false;

    // The tank already sees something it can engage — let its own engage-trash /
    // engage-boss scan (higher relevance) take it; this path is only for the fight
    // the tank CAN'T see. "attackers" is the stock LOS-filtered list, so it is
    // empty exactly while the pack is out of sight and non-empty the moment the
    // tank rounds the corner, at which point this stands down.
    if (!ctx->GetValue<GuidVector>(DcKey::Stock::Attackers)->Get().empty())
        return false;

    // A groupmate is fighting but the tank is not. Latched (PartyCombatLatch) on
    // the SAME hysteresis the followers' assist and the scout-lag gate use, so a
    // one-tick combat gap can't bounce the tank between assisting and advancing.
    uint32 const latchMs =
        uint32(DcSettings::GetFloat(leader, "PartyCombatLatch") * 1000.0f);
    return IsPartyEngagedLatched(leader, latchMs);
}
bool DcLeaderSignal::IsLeaderDynamicScouting(Player* bot)
{
    if (!bot)
        return false;

    Player* leader = FindLeaderTank(bot);
    if (!leader || leader == bot)
        return false;  // the leader drives; the lag applies to followers only

    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return false;

    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    if (!DcRun::Of(ctx).enabled ||
        DcRun::Of(ctx).paused)
        return false;

    // Dynamic mode only (pull setting == 2). Off/On have no scouting-then-decide
    // window — the party either always follows close (Off) or always holds at camp
    // (On) — so the lag would only ever delay them for no benefit there.
    if (ctx->GetValue<uint32>(DcKey::PullSetting)->Get() != 2u)
        return false;

    // While a PERSISTENT anchored event drives (ZulFarrak's temple), the pull
    // system is forced Off (see DungeonClearPullModeCurrentValue): drop the
    // scout-lag too so followers stay tight on the tank and are in position when
    // each wave hits, instead of lagging up-ramp to rest and arriving late. Same
    // single predicate as the pull-mode override, evaluated on the leader's context.
    if (DungeonEventExecutor::IsPersistentAnchoredEventActive(ctx))
        return false;

    // Still scouting: the verdict for the upcoming pack hasn't been committed. Once
    // the tank tags the pack it enters combat, and an Advanced verdict marks a camp
    // (handing the party to hold-at-camp) — either way the phase leaves Idle and the
    // party stops lagging and engages. Release off ANY party member's combat, not
    // just the leader's own flag: a surprise pull before a verdict commits ("in
    // combat, no pull state") flickers the leader's IsInCombat() as it tags/leashes,
    // and keying the trail on the leader alone left the party lagging at a
    // LOS-blocked range on the spiral while the tank fought and died. If anyone in
    // the party is fighting, drop the trail so the assist/regroup can collapse it.
    //
    // Latched (PartyCombatLatch): the bare in-combat read here and the assist's
    // gate are the SAME TOCTOU race seen from opposite sides — combat starts on a
    // tick we don't control, and a single false reading would resume the far lag
    // mid-fight, fighting the assist that wants to collapse the party. Holding the
    // engaged verdict for the grace window keeps both decisions consistent: the
    // party will not run BACK out to the scout-lag ring until the leader has been
    // continuously out of combat for the whole window. See IsPartyEngagedLatched.
    uint32 const latchMs =
        uint32(DcSettings::GetFloat(leader, "PartyCombatLatch") * 1000.0f);
    if (IsPartyEngagedLatched(leader, latchMs))
        return false;
    DcPullContext const& pull =
        ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    if (pull.phase != DcPullPhase::Idle)
        return false;

    // Leeroy roll-in: with a standing Leeroy verdict, release the lag as the tank
    // closes to commit range + PullDynamicRollInLead on the verdicted pack — the
    // tank is committing to the charge, so the party closes the remaining gap
    // DURING the final approach and arrives roughly with first contact, instead
    // of standing flat-footed at the lag ring until combat registers and only
    // then starting a 15-20yd run. Advanced verdicts are untouched: the camp
    // machinery owns the party there (and the Advanced flip after a release is
    // covered too — hold-at-camp outranks follow-tank, so the party walks back).
    if (!pull.decisionTarget.IsEmpty())
    {
        Unit* target = ObjectAccessor::GetUnit(*leader, pull.decisionTarget);
        bool const targetAlive = target && target->IsAlive();
        float const tankToTarget = targetAlive ? leader->GetExactDist2d(target) : 0.0f;
        float const commitRange = targetAlive
            ? DcEngageGeometry::PullCommitRange(leader, target, DC_PULL_START_RANGE)
            : 0.0f;
        float const lead = DcSettings::GetFloat(leader, "PullDynamicRollInLead");
        if (DungeonClearMath::ShouldRollInForLeeroy(static_cast<uint32>(pull.decision), targetAlive,
                                                    tankToTarget, commitRange, lead))
        {
            DC_PULL_TRACE("[DC:{}] scout-lag: leeroy roll-in — tank {:.1f}yd from "
                          "pack (commit {:.1f} + lead {:.1f}) -> lag released",
                          bot->GetName(), tankToTarget, commitRange, lead);
            return false;
        }
    }
    return true;
}
bool DcLeaderSignal::GetLeaderScoutTrailPoint(Player* bot, float lag, Position& out)
{
    if (!bot)
        return false;

    Player* leader = FindLeaderTank(bot);
    if (!leader || leader == bot)
        return false;  // the leader drives; only followers trail it

    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return false;

    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    // The trail lives in the LEADER's context — only the tank runs Advance and so
    // only the tank records breadcrumbs.
    std::vector<Position> const& crumbs =
        ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get().breadcrumbs;
    if (crumbs.empty())
        return false;

    Position const tankPos(leader->GetPositionX(), leader->GetPositionY(),
                           leader->GetPositionZ());

    // Walk BACK along the trail (newest -> oldest) accumulating real walked distance,
    // exactly like ComputeTrailCamp, and return a point EXACTLY `lag` yards behind
    // the tank. A 3D segment > kJumpGuard is a drag/teleport seam — stop, nothing
    // beyond it is contiguously "behind" the tank.
    //
    // INTERPOLATE, do not snap to the next crumb: crumbs are recorded only every
    // ~4yd (RecordBreadcrumb kSpacing), so returning the first crumb at >= lag
    // overshot the lag by up to one crumb spacing. With the follower's own
    // kTrailArrival (4yd) hold slack on top, a lag=9 request parked followers ~16yd
    // from the tank — outside a 15yd PartyMaxSpread — and the tank's between-pulls
    // gate then waited forever (the observed scout-lag/trail-arrival deadlock at
    // tank3D~15, lag 9). Interpolating along the crossing segment lands the target
    // at the true `lag` distance so rest = lag + kTrailArrival stays inside the gate.
    //
    // Reachability (a full PathGenerator build per probe) is deliberately tested
    // LAZILY: only the point at/past the lag distance is probed, and the pre-lag
    // crumbs only as a farthest-first fallback when nothing at the lag was reachable
    // (trail shorter than the lag, or every far point across a seam).
    std::vector<std::pair<float, Position>> preLag;  // (along, crumb), nearest-first
    bool found = false;
    DungeonClearMath::WalkTrailBack(
        crumbs, tankPos, DungeonClearMath::TrailJumpGuard,
        [&](DungeonClearMath::TrailStep const& s) -> bool
        {
            if (s.along < lag)
            {
                preLag.emplace_back(s.along, s.crumb);
                return true;
            }
            // This segment reaches the lag mark. If it CROSSES it (alongPrev < lag),
            // interpolate the exact-lag point on the segment instead of snapping to
            // the crumb (crumbs are ~4yd apart, so snapping overshoots the lag by up
            // to one spacing — the scout-lag/trail-arrival deadlock); past the first
            // crossing alongPrev >= lag and PointAt returns the crumb itself.
            Position const target = (s.alongPrev < lag) ? s.PointAt(lag) : s.crumb;
            // Only ever trail to a point the follower can reach over a complete
            // generated path; one across a navmesh seam would straight-line under
            // the map. The interpolated point sits on a contiguous walked segment,
            // so it is reachable whenever the bracketing crumb is; fall back to the
            // crumb if the snap missed, else keep walking back.
            if (IsNavReachable(bot, target) && !TrailOverZoneLine(bot, target))
            {
                out = target;
                found = true;
                return false;
            }
            if (IsNavReachable(bot, s.crumb) && !TrailOverZoneLine(bot, s.crumb))
            {
                out = s.crumb;
                found = true;
                return false;
            }
            return true;
        });
    if (found)
        return true;

    // Far-behind follower. The walk above starts at the TANK and probes crumbs
    // the follower may not be able to reach at all before it ever looks near
    // the follower - measured Maraudon 2026-09-05: 237 "trail join failed" in
    // 90 min, 106 of them with a crumb inside 25yd of the bot. So when the
    // follower is well behind, look for the crumb nearest to IT; if that is
    // walkable, join the trail there (aiming a few crumbs toward the tank so
    // the join is progress) and let the glide take over from that point.
    if (bot->GetExactDist(leader) > 60.0f)
    {
        std::size_t nearestIdx = crumbs.size();
        float nearest = 40.0f;   // join radius
        for (std::size_t i = 0; i < crumbs.size(); ++i)
        {
            float const d = bot->GetExactDist(&crumbs[i]);
            if (d < nearest)
            {
                nearest = d;
                nearestIdx = i;
            }
        }
        if (nearestIdx < crumbs.size())
        {
            std::size_t const ahead = std::min(nearestIdx + 4, crumbs.size() - 1);
            std::size_t const candidates[2] = { ahead, nearestIdx };
            for (std::size_t idx : candidates)
            {
                Position const& c = crumbs[idx];
                if (bot->GetExactDist(&c) < 3.0f)
                    continue;   // already standing on it
                if (IsNavReachable(bot, c) && !TrailOverZoneLine(bot, c))
                {
                    static std::mutex s_joinLogLock;
                    static std::unordered_map<ObjectGuid, uint32> s_joinLogAt;
                    uint32 const now = getMSTime();
                    bool speak = false;
                    {
                        std::lock_guard<std::mutex> lock(s_joinLogLock);
                        uint32& last = s_joinLogAt[bot->GetObjectGuid()];
                        if (!last || getMSTimeDiff(last, now) > 10000)
                        {
                            last = now;
                            speak = true;
                        }
                    }
                    if (speak)
                        LOG_INFO("playerbots.dungeonclear",
                                 "[DC:{}] trail join via nearest crumb #{} of {} ({}yd away, "
                                 "{}yd behind the tank)",
                                 bot->GetName(), int(idx), int(crumbs.size()),
                                 int(bot->GetExactDist(&c)), int(bot->GetExactDist(leader)));
                    out = c;
                    return true;
                }
            }
        }
    }

    // Trail shorter than the full lag (or nothing reachable past it): trail the
    // farthest reachable pre-lag crumb (the follower simply stacks a little
    // closer until more trail accrues).
    for (auto it = preLag.rbegin(); it != preLag.rend(); ++it)
    {
        if (IsNavReachable(bot, it->second) && !TrailOverZoneLine(bot, it->second))
        {
            out = it->second;
            return true;
        }
    }

    // MEASUREMENT (Dragonmaw 2026-08-31). Three explanations for stranded
    // followers have now been measured and dropped, and every one of them was a
    // guess about WHERE the follower is relative to the trail. This is that
    // number, taken at the only place that knows it.
    //
    //   nearest small  -> the trail runs right past the bot and the reachability
    //                     probe is what refuses it.
    //   nearest large  -> the bot is not on the tank's path at all, and no trail
    //                     logic can reach it.
    //
    // Rate-limited per bot: this failure path runs ~13x a second across ten
    // parties, and journal volume has starved this server before. No extra
    // PathGenerator build here either - the nearest-crumb distance is arithmetic.
    if (bot->GetExactDist(leader) > 60.0f)
    {
        static std::mutex s_nearLogLock;
        static std::unordered_map<ObjectGuid, uint32> s_nearLogAt;
        uint32 const now = getMSTime();
        bool speak = false;
        {
            std::lock_guard<std::mutex> lock(s_nearLogLock);
            uint32& last = s_nearLogAt[bot->GetObjectGuid()];
            if (!last || getMSTimeDiff(last, now) > 10000)
            {
                last = now;
                speak = true;
            }
        }
        if (speak)
        {
            float nearest = 1e9f;
            std::size_t nearestIdx = 0;
            for (std::size_t i = 0; i < crumbs.size(); ++i)
            {
                float const d = bot->GetExactDist(&crumbs[i]);
                if (d < nearest) { nearest = d; nearestIdx = i; }
            }
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] trail join failed: {}yd behind the tank, nearest crumb "
                     "{}yd away (#{} of {})",
                     bot->GetName(), int(bot->GetExactDist(leader)), int(nearest),
                     int(nearestIdx), int(crumbs.size()));
        }
    }
    return false;
}
bool DcLeaderSignal::GetLeaderScoutTrail(Player* bot, float lag, std::vector<Position>& out)
{
    out.clear();
    if (!bot)
        return false;

    Player* leader = FindLeaderTank(bot);
    if (!leader || leader == bot)
        return false;  // the leader drives; only followers trail it

    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return false;

    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    std::vector<Position> const& crumbs =
        ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get().breadcrumbs;
    if (crumbs.size() < 2)
        return false;

    Position const tankPos(leader->GetPositionX(), leader->GetPositionY(),
                           leader->GetPositionZ());
    Position const botPos(bot->GetPositionX(), bot->GetPositionY(),
                          bot->GetPositionZ());

    // Walk BACK along the trail (newest -> oldest) accumulating real walked
    // distance, exactly like GetLeaderScoutTrailPoint. Two indices fall out of
    // the walk:
    //   * lagIdx  — the FIRST crumb at least `lag` yards behind the tank: the
    //               forward (toward-tank) end of the follower's glide window.
    //   * nearIdx — the crumb NEAREST the follower among the crumbs at/behind the
    //               lag point: the near end the glide starts from.
    // The window is then crumbs[nearIdx .. lagIdx] (increasing index == forward),
    // so the follower rides the centered trail from where it stands up to `lag`
    // behind the tank, never closing past the lag. A 3D segment > kJumpGuard is a
    // drag/teleport seam — stop, nothing beyond it is contiguously behind the tank.
    int lagIdx = -1;
    int nearIdx = -1;
    float nearDist = std::numeric_limits<float>::max();
    DungeonClearMath::WalkTrailBack(
        crumbs, tankPos, DungeonClearMath::TrailJumpGuard,
        [&](DungeonClearMath::TrailStep const& s) -> bool
        {
            if (s.along < lag)
                return true;  // still ahead of the lag point — not part of the window
            if (lagIdx < 0)
                lagIdx = static_cast<int>(s.index);
            float const d = botPos.GetExactDist(&s.crumb);
            if (d < nearDist)
            {
                nearDist = d;
                nearIdx = static_cast<int>(s.index);
            }
            return true;
        });

    // No crumb as far back as `lag` (trail shorter than the lag), or the follower
    // is at/ahead of the lag crumb (already caught up): nothing to glide. Let the
    // caller fall back to the single-point step / Follow fan.
    if (lagIdx < 0 || nearIdx < 0 || nearIdx >= lagIdx)
        return false;

    // Follower too far off the trail for a safe straight entry leg to the nearest
    // crumb — fall back to the point step, whose PathGenerator build routes the
    // off-trail approach properly instead of straight-lining it.
    if (nearDist > DungeonClearMath::TrailEntryReach)
        return false;

    // Build the forward window: live follower position, then the contiguous
    // crumbs from the near end up to the lag crumb. Skip the near crumb when the
    // follower is basically standing on it (avoids a degenerate first leg).
    out.push_back(botPos);
    int startK = nearIdx;
    if (botPos.GetExactDist(&crumbs[nearIdx]) < 2.0f)
        ++startK;
    for (int k = startK; k <= lagIdx; ++k)
        out.push_back(crumbs[k]);

    if (out.size() < 2)
        return false;

    // Probe ONLY the entry leg (follower -> first crumb): the rest of the window
    // is the tank's own contiguous walked ground. One PathGenerator build, the
    // same cost the point variant pays. A straight entry across a navmesh seam
    // would glide the follower under the map, so reject it here.
    if (!IsNavReachable(bot, out[1]))
    {
        out.clear();
        return false;
    }

    // Cap the window for client safety (a long spline packet is a factor in the
    // 3.3.5 client's DC-burst heap sensitivity). The window is naturally short —
    // it spans only the follower's overshoot past the lag — but bound it anyway.
    // Trimming keeps the near portion; the glide simply continues next window.
    constexpr std::size_t kFollowerWindowCap = 24;  // ~ cap * crumb spacing (4yd)
    if (out.size() > kFollowerWindowCap)
        out.resize(kFollowerWindowCap);

    return true;
}
bool DcLeaderSignal::GetLeaderRoomAggroSphere(Player* bot, Position& centerOut,
                                             float& radiusOut)
{
    if (!bot)
        return false;

    Player* leader = FindLeaderTank(bot);
    if (!leader || leader == bot)
        return false;  // the leader skirts in EngageDirect; this is for followers

    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return false;

    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    if (!DcRun::Of(ctx).enabled ||
        DcRun::Of(ctx).paused)
        return false;

    // Same gate the tank's own skirt uses (RoomAggroSkirtPoint), read off the
    // leader's context so the whole party shares one verdict: a flagged boss with
    // room trash left and the tank at the boss room.
    if (!DcTargeting::IsRoomClearActive(leader, ctx))
        return false;

    std::optional<DungeonBossInfo> next =
        ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next.has_value())
        return false;

    Creature* boss = DcTargeting::GetLiveBoss(leader, ctx, next->entry);
    if (!boss)
        return false;

    // Size the avoid-sphere with THIS follower's own aggro range and reach (a
    // low-level follower's notice distance against the boss can differ from the
    // tank's). The single-source helper is the same formula the tank's skirt and
    // the room-trash exclusion read, so the party and tank agree on where the
    // sphere ends.
    radiusOut = DcEngageGeometry::RoomAggroSphereRadius(bot, boss);
    centerOut = Position(boss->GetPositionX(), boss->GetPositionY(),
                         boss->GetPositionZ(), 0.0f);
    return true;
}
void DcLeaderSignal::AbortLeaderPull(Player* bot)
{
    if (!bot)
        return;
    Player* leader = FindLeaderTank(bot);
    if (!leader)
        return;
    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return;
    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    DcPullContext& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    if (IsPullPhaseHolding(static_cast<uint32>(pull.phase)))
    {
        // Through EnterEngage (not a bare phase write) so the per-pull tag latch
        // is cleared on this Engage entry too — same rule as DcSetPullPhase.
        pull.EnterEngage(getMSTime());
        DC_PULL_INFO("[DC:{}] advanced-pull: leader pull aborted (forced to Engage) "
                     "-> party released", leader->GetName());
    }
}
void DcLeaderSignal::ReleaseLeaderPullHold(Player* bot)
{
    if (!bot)
        return;
    Player* leader = FindLeaderTank(bot);
    if (!leader)
        return;
    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return;
    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    DcPullContext& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    DcPullPhase const before = pull.phase;
    bool const aborted = pull.SafetyRelease(getMSTime());
    if (aborted)
        DC_PULL_INFO("[DC:{}] advanced-pull: safety release during Advancing -> "
                     "pull aborted (forced to Engage), party released",
                     leader->GetName());
    else if (pull.partyReleased && IsPullPhaseHolding(static_cast<uint32>(before)))
        DC_PULL_INFO("[DC:{}] advanced-pull: safety release -> party freed to "
                     "fight, tank keeps the drag (phase {})",
                     leader->GetName(), static_cast<uint32>(before));
}
bool DcLeaderSignal::IsLeaderPullHoldReleased(Player* bot)
{
    if (!bot)
        return false;
    Player* leader = FindLeaderTank(bot);
    if (!leader)
        return false;
    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return false;
    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    return ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get().partyReleased;
}
ObjectGuid DcLeaderSignal::GetLeaderPullTarget(Player* bot)
{
    if (!bot)
        return ObjectGuid::Empty;
    Player* leader = FindLeaderTank(bot);
    if (!leader)
        return ObjectGuid::Empty;
    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return ObjectGuid::Empty;
    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    return ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get().pullTarget;
}
void DcLeaderSignal::SetLeaderDazeImmunity(Player* leader, bool apply)
{
    if (!leader)
        return;

    // Block all spells carrying the Daze mechanic (spell 1604 is the only one
    // creatures apply). spellId 0 = a blanket mechanic block. Pair add/remove
    // exactly: remove first so a re-apply never stacks duplicate entries.
    leader->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_DAZE, false);
    if (apply)
    {
        leader->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_DAZE, true);
        // Immunity only blocks FUTURE applications; clear any Daze already on
        // the tank from before pull mode came on (or before the drag started).
        leader->RemoveAurasDueToSpell(1604);
    }
}

bool DcLeaderSignal::GetLeaderGatherPoint(Player* bot, DcGatherPoint& out)
{
    if (!bot)
        return false;
    Player* leader = FindLeaderTank(bot);
    if (!leader || leader == bot)
        return false;
    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return false;
    AiObjectContext* ctx = leaderAI->GetAiObjectContext();
    if (!DcRun::Of(ctx).enabled || DcRun::Of(ctx).paused)
        return false;
    DcGatherPoint const& gp = ctx->GetValue<DcGatherPoint&>(DcKey::GatherPoint)->Get();
    if (!gp.Live(getMSTime()) || gp.mapId != bot->GetMapId())
        return false;
    if (bot->GetDistance(gp.x, gp.y, gp.z) > gp.reach)
        return false;
    out = gp;
    return true;
}
