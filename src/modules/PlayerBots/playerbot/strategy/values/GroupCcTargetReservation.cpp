#include "playerbot/playerbot.h"
#include "GroupCcTargetReservation.h"

#include "Group/Group.h"

#include <unordered_map>
#include <vector>

using namespace ai;

namespace
{
    constexpr uint32 kSelectDurationMs = 3000;
    constexpr uint32 kCastDurationMs = 5000;
    constexpr uint32 kSkipDurationMs = 8000;
    constexpr uint32 kLeaseMemoryDurationMs = kSkipDurationMs;
    constexpr uint32 kInactiveSweepIntervalMs = 5000;
    constexpr uint8 kMaxFailedStarts = 3;
    // Bound: same owner+target may fail to start CC at most 3 times per 8s
    // lease-memory window, then is skipped 8s. Selection is a hard 3s lease
    // (not refreshed; this owner cannot re-claim it at selection for 8s).
    // A delayed Execute may reacquire if the add is still free. A real start
    // opens 5s in-flight and does not consume a failed-start slot; unverified
    // in-flight expiry skips 8s. Counts reset when that 8s memory/skip elapses.

    struct CcClaim
    {
        ObjectGuid targetGuid;
        ObjectGuid ownerGuid;
        uint32 startMs = 0;
        uint32 durationMs = 0;
        uint8 failedStarts = 0;
        bool inFlight = false;
    };

    struct CcSkip
    {
        ObjectGuid targetGuid;
        ObjectGuid ownerGuid;
        uint32 startMs = 0;
        uint32 durationMs = 0;
    };

    // Owner+target memory for the 8s recovery window: blocks a new *selection*
    // claim after a hard lease expires, and carries failed-start counts into a
    // cast-time reacquire. This is not IsSkipped; other bots may claim, and the
    // delayed owner may reacquire only at Execute if the add is still free.
    struct CcLeaseMemory
    {
        ObjectGuid targetGuid;
        ObjectGuid ownerGuid;
        uint8 failedStarts = 0;
        uint32 startMs = 0;
        uint32 durationMs = 0;
    };

    // World-thread only (World::UpdatePlayerbotsTick / PlayerbotAI::UpdateAI).
    // These tables are not synchronized.
    std::unordered_map<uint32, std::vector<CcClaim>> claimsByGroup;
    std::unordered_map<uint32, std::vector<CcSkip>> skipsByGroup;
    std::unordered_map<uint32, std::vector<CcLeaseMemory>> memoryByGroup;

    // Per-access Prune only expires the caller's group. Inactive group IDs are
    // reaped every kInactiveSweepIntervalMs (wrap-safe) so disbanded groups
    // cannot retain keys. The interval is the amortization: maps are not
    // scanned on every bot tick. inInactiveSweep blocks nested Prune/sweep
    // while claim expiry may insert/erase skip and memory keys.
    uint32 lastInactiveSweepMs = 0;
    bool hasInactiveSweepMs = false;
    bool inInactiveSweep = false;

    struct CombatClaim
    {
        ObjectGuid targetGuid;
        ObjectGuid ownerGuid;
        uint32 startMs;
        uint32 durationMs;
        uint8 failures;
        std::string spell;
        uint32 spellId = 0;
    };
    std::unordered_map<uint32, std::vector<CombatClaim>> interruptClaims;
    std::unordered_map<uint32, std::vector<CombatClaim>> resurrectionClaims;
    std::unordered_map<uint32, std::vector<CombatClaim>> combatRetries;
    constexpr uint32 kCombatClaimMs = 1800;
    constexpr uint32 kResurrectionClaimMs = 15000;
    constexpr uint32 kCombatBackoffMs = 1800;
    constexpr uint8 kCombatMaxFailures = 2;

    void PruneCombatMap(std::unordered_map<uint32, std::vector<CombatClaim>>& map)
    {
        uint32 now = WorldTimer::getMSTime();
        for (std::unordered_map<uint32, std::vector<CombatClaim>>::iterator group = map.begin(); group != map.end();)
        {
            for (std::vector<CombatClaim>::iterator it = group->second.begin(); it != group->second.end();)
                if (WorldTimer::getMSTimeDiff(it->startMs, now) >= it->durationMs)
                    it = group->second.erase(it);
                else
                    ++it;
            if (group->second.empty())
                group = map.erase(group);
            else
                ++group;
        }
    }

    bool IsTimedOut(uint32 startMs, uint32 durationMs)
    {
        return WorldTimer::getMSTimeDiff(startMs, WorldTimer::getMSTime()) >= durationMs;
    }

    uint32 GetGroupId(Player* bot)
    {
        if (!bot)
            return 0;

        Group* group = bot->GetGroup();
        return group ? group->GetId() : 0;
    }

    void AddSkip(uint32 groupId, ObjectGuid ownerGuid, ObjectGuid targetGuid)
    {
        std::vector<CcSkip>& skips = skipsByGroup[groupId];
        for (CcSkip& skip : skips)
        {
            if (skip.ownerGuid == ownerGuid && skip.targetGuid == targetGuid)
            {
                skip.startMs = WorldTimer::getMSTime();
                skip.durationMs = kSkipDurationMs;
                return;
            }
        }

        CcSkip skip;
        skip.targetGuid = targetGuid;
        skip.ownerGuid = ownerGuid;
        skip.startMs = WorldTimer::getMSTime();
        skip.durationMs = kSkipDurationMs;
        skips.push_back(skip);
    }

    CcLeaseMemory* FindMemory(uint32 groupId, ObjectGuid ownerGuid, ObjectGuid targetGuid)
    {
        std::unordered_map<uint32, std::vector<CcLeaseMemory>>::iterator groupIt = memoryByGroup.find(groupId);
        if (groupIt == memoryByGroup.end())
            return nullptr;

        for (CcLeaseMemory& memory : groupIt->second)
        {
            if (memory.ownerGuid == ownerGuid && memory.targetGuid == targetGuid)
                return &memory;
        }

        return nullptr;
    }

    void ClearMemory(uint32 groupId, ObjectGuid ownerGuid, ObjectGuid targetGuid)
    {
        std::unordered_map<uint32, std::vector<CcLeaseMemory>>::iterator groupIt = memoryByGroup.find(groupId);
        if (groupIt == memoryByGroup.end())
            return;

        std::vector<CcLeaseMemory>& memories = groupIt->second;
        for (std::vector<CcLeaseMemory>::iterator it = memories.begin(); it != memories.end(); ++it)
        {
            if (it->ownerGuid == ownerGuid && it->targetGuid == targetGuid)
            {
                memories.erase(it);
                break;
            }
        }

        if (memories.empty())
            memoryByGroup.erase(groupIt);
    }

    void RememberLease(uint32 groupId, ObjectGuid ownerGuid, ObjectGuid targetGuid, uint8 failedStarts)
    {
        if (CcLeaseMemory* existing = FindMemory(groupId, ownerGuid, targetGuid))
        {
            existing->failedStarts = failedStarts;
            existing->startMs = WorldTimer::getMSTime();
            existing->durationMs = kLeaseMemoryDurationMs;
            return;
        }

        CcLeaseMemory memory;
        memory.targetGuid = targetGuid;
        memory.ownerGuid = ownerGuid;
        memory.failedStarts = failedStarts;
        memory.startMs = WorldTimer::getMSTime();
        memory.durationMs = kLeaseMemoryDurationMs;
        memoryByGroup[groupId].push_back(memory);
    }

    void PruneExpiredClaims(uint32 groupId, std::vector<CcClaim>& claims)
    {
        for (std::vector<CcClaim>::iterator it = claims.begin(); it != claims.end();)
        {
            if (IsTimedOut(it->startMs, it->durationMs))
            {
                if (it->inFlight)
                {
                    // Outcome never verified: 8s skip before this owner retries.
                    AddSkip(groupId, it->ownerGuid, it->targetGuid);
                    ClearMemory(groupId, it->ownerGuid, it->targetGuid);
                }
                else
                {
                    // Selection timeout frees the add. Remember failed starts
                    // and block only this owner's selection re-claim for 8s.
                    RememberLease(groupId, it->ownerGuid, it->targetGuid, it->failedStarts);
                }
                it = claims.erase(it);
            }
            else
                ++it;
        }
    }

    void PruneExpiredSkips(std::vector<CcSkip>& skips)
    {
        for (std::vector<CcSkip>::iterator it = skips.begin(); it != skips.end();)
        {
            if (IsTimedOut(it->startMs, it->durationMs))
                it = skips.erase(it);
            else
                ++it;
        }
    }

    void PruneExpiredMemory(std::vector<CcLeaseMemory>& memories)
    {
        for (std::vector<CcLeaseMemory>::iterator it = memories.begin(); it != memories.end();)
        {
            if (IsTimedOut(it->startMs, it->durationMs))
                it = memories.erase(it);
            else
                ++it;
        }
    }

    // Interval-gated. Must not call Prune: nested prune can erase the key
    // currently being iterated. AddSkip/RememberLease/ClearMemory may insert
    // or erase skipsByGroup and memoryByGroup; those maps are walked only
    // after claims finish, so those iterators stay valid. Claims run first
    // so in-flight -> skip and selection -> lease memory happen before
    // skip/memory expiry, matching per-group Prune. Empty keys are erased
    // with the loop iterator; helpers never leave an empty vector behind.
    void MaybeSweepInactiveGroups()
    {
        if (inInactiveSweep)
            return;

        uint32 now = WorldTimer::getMSTime();
        if (!hasInactiveSweepMs)
        {
            hasInactiveSweepMs = true;
            lastInactiveSweepMs = now;
            return;
        }

        if (WorldTimer::getMSTimeDiff(lastInactiveSweepMs, now) < kInactiveSweepIntervalMs)
            return;

        lastInactiveSweepMs = now;
        inInactiveSweep = true;

        for (std::unordered_map<uint32, std::vector<CcClaim>>::iterator it = claimsByGroup.begin();
             it != claimsByGroup.end();)
        {
            PruneExpiredClaims(it->first, it->second);
            if (it->second.empty())
                it = claimsByGroup.erase(it);
            else
                ++it;
        }

        for (std::unordered_map<uint32, std::vector<CcSkip>>::iterator it = skipsByGroup.begin();
             it != skipsByGroup.end();)
        {
            PruneExpiredSkips(it->second);
            if (it->second.empty())
                it = skipsByGroup.erase(it);
            else
                ++it;
        }

        for (std::unordered_map<uint32, std::vector<CcLeaseMemory>>::iterator it = memoryByGroup.begin();
             it != memoryByGroup.end();)
        {
            PruneExpiredMemory(it->second);
            if (it->second.empty())
                it = memoryByGroup.erase(it);
            else
                ++it;
        }

        PruneCombatMap(interruptClaims);
        PruneCombatMap(resurrectionClaims);
        PruneCombatMap(combatRetries);

        inInactiveSweep = false;
    }

    void Prune(uint32 groupId)
    {
        MaybeSweepInactiveGroups();
        if (inInactiveSweep)
            return;

        std::unordered_map<uint32, std::vector<CcClaim>>::iterator claimsGroup = claimsByGroup.find(groupId);
        if (claimsGroup != claimsByGroup.end())
        {
            PruneExpiredClaims(groupId, claimsGroup->second);
            if (claimsGroup->second.empty())
                claimsByGroup.erase(claimsGroup);
        }

        std::unordered_map<uint32, std::vector<CcSkip>>::iterator skipsGroup = skipsByGroup.find(groupId);
        if (skipsGroup != skipsByGroup.end())
        {
            PruneExpiredSkips(skipsGroup->second);
            if (skipsGroup->second.empty())
                skipsByGroup.erase(skipsGroup);
        }

        std::unordered_map<uint32, std::vector<CcLeaseMemory>>::iterator memoryGroup = memoryByGroup.find(groupId);
        if (memoryGroup != memoryByGroup.end())
        {
            PruneExpiredMemory(memoryGroup->second);
            if (memoryGroup->second.empty())
                memoryByGroup.erase(memoryGroup);
        }
    }

    // Grouped callers prune their own group. Ungrouped callers still amortize
    // the inactive sweep so disbanded IDs are reaped without a live group.
    uint32 AccessGroupState(Player* bot)
    {
        uint32 groupId = GetGroupId(bot);
        if (groupId)
            Prune(groupId);
        else
            MaybeSweepInactiveGroups();
        return groupId;
    }

    CcClaim* FindClaim(uint32 groupId, ObjectGuid targetGuid)
    {
        std::unordered_map<uint32, std::vector<CcClaim>>::iterator groupIt = claimsByGroup.find(groupId);
        if (groupIt == claimsByGroup.end())
            return nullptr;

        for (CcClaim& claim : groupIt->second)
        {
            if (claim.targetGuid == targetGuid)
                return &claim;
        }

        return nullptr;
    }

    CcClaim* FindOwnedClaim(uint32 groupId, ObjectGuid ownerGuid)
    {
        std::unordered_map<uint32, std::vector<CcClaim>>::iterator groupIt = claimsByGroup.find(groupId);
        if (groupIt == claimsByGroup.end())
            return nullptr;

        for (CcClaim& claim : groupIt->second)
        {
            if (claim.ownerGuid == ownerGuid)
                return &claim;
        }

        return nullptr;
    }
}

namespace
{
    bool ClaimCombat(std::unordered_map<uint32, std::vector<CombatClaim>>& claimsByGroup,
                     PlayerbotAI* ai, Unit* target, bool allowDead, std::string const& spell, uint32 duration, uint32 hostileSpellId)
    {
        // A resurrection claim is naturally completed when the corpse is no
        // longer dead.  Drop the owner lease before selecting another corpse.
        if (allowDead && target && target->IsAlive())
        {
            Player* bot = ai ? ai->GetBot() : nullptr;
            uint32 groupId = GetGroupId(bot);
            if (groupId)
            {
                std::vector<CombatClaim>& claims = claimsByGroup[groupId];
                for (std::vector<CombatClaim>::iterator it = claims.begin(); it != claims.end();)
                    if (it->targetGuid == target->GetObjectGuid())
                        it = claims.erase(it);
                    else
                        ++it;
            }
            return false;
        }
        if (!ai || !target || (!allowDead && !target->IsAlive()))
            return false;
        Player* bot = ai->GetBot();
        uint32 groupId = GetGroupId(bot);
        if (!bot || !groupId)
            return true; // solo/manual behavior is unchanged
        MaybeSweepInactiveGroups();
        PruneCombatMap(combatRetries);
        uint32 now = WorldTimer::getMSTime();
        std::vector<CombatClaim>& claims = claimsByGroup[groupId];
        for (std::vector<CombatClaim>::iterator it = claims.begin(); it != claims.end();)
        {
            if (WorldTimer::getMSTimeDiff(it->startMs, now) >= it->durationMs)
                it = claims.erase(it);
            else
                ++it;
        }
        for (std::vector<CombatClaim>::iterator it = claims.begin(); it != claims.end();)
            if (!spell.empty() && it->targetGuid == target->GetObjectGuid() && it->spellId != hostileSpellId)
                it = claims.erase(it);
            else
                ++it;
        for (CombatClaim const& claim : claims)
            if (claim.targetGuid == target->GetObjectGuid() && claim.ownerGuid != bot->GetObjectGuid())
                return false;
        std::vector<CombatClaim>& retries = combatRetries[groupId];
        for (CombatClaim const& retry : retries)
            if (retry.targetGuid == target->GetObjectGuid() && retry.ownerGuid == bot->GetObjectGuid() &&
                retry.spell == spell)
                return false;
        for (CombatClaim const& claim : claims)
            if (claim.targetGuid == target->GetObjectGuid() && claim.ownerGuid == bot->GetObjectGuid())
                return claim.spell == spell;
        CombatClaim claim;
        claim.targetGuid = target->GetObjectGuid();
        claim.ownerGuid = bot->GetObjectGuid();
        claim.startMs = now;
        claim.durationMs = duration;
        claim.failures = 0;
        claim.spell = spell;
        claim.spellId = hostileSpellId;
        claims.push_back(claim);
        return true;
    }

    void RecordCombat(std::unordered_map<uint32, std::vector<CombatClaim>>& claimsByGroup,
                      Player* bot, Unit* target, std::string const& spell, bool castStarted)
    {
        if (!bot || !target || !bot->GetGroup())
            return;
        uint32 groupId = bot->GetGroup()->GetId();
        std::vector<CombatClaim>& claims = claimsByGroup[groupId];
        for (std::vector<CombatClaim>::iterator it = claims.begin(); it != claims.end(); ++it)
        {
            bool sameInterruptCast = true;
            if (!spell.empty() && !castStarted && it->spellId)
            {
                Spell* current = target->GetCurrentSpell(CURRENT_GENERIC_SPELL);
                if (!current)
                    current = target->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
                sameInterruptCast = current && current->m_spellInfo && current->m_spellInfo->Id == it->spellId;
            }
            if (it->targetGuid != target->GetObjectGuid() || it->ownerGuid != bot->GetObjectGuid() ||
                (!spell.empty() && (it->spell != spell || !sameInterruptCast)))
                continue;
            if (castStarted)
            {
                // Keep the owner lease briefly after a successful reactive
                // cast so another bot cannot duplicate it in the same tick.
                it->startMs = WorldTimer::getMSTime();
                it->durationMs = spell.empty() ? kResurrectionClaimMs : kCombatClaimMs;
            }
            else
            {
                CombatClaim retry = *it;
                ++retry.failures;
                retry.startMs = WorldTimer::getMSTime();
                retry.durationMs = kCombatBackoffMs * (retry.failures >= kCombatMaxFailures ? 2 : 1);
                combatRetries[groupId].push_back(retry);
                claims.erase(it);
            }
            return;
        }
    }
}

bool GroupCcTargetReservation::ClaimInterrupt(PlayerbotAI* ai, Unit* target, std::string const& spell)
{
    // Re-check at claim time: triggers can be evaluated from stale cached
    // state, while movement or another cast may have made this interrupt
    // unusable.  CanCastSpell covers range, resource and cooldown readiness.
    if (!ai || spell.empty() || !ai->CanCastSpell(spell, target, true, nullptr, false, true))
        return false;
    Spell* hostile = target->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!hostile)
        hostile = target->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (!hostile || !hostile->m_spellInfo || hostile->GetCastedTime() < 400)
        return false;
    return ClaimCombat(interruptClaims, ai, target, false, spell, kCombatClaimMs, hostile->m_spellInfo->Id);
}

void GroupCcTargetReservation::RecordInterrupt(Player* bot, Unit* target, std::string const& spell, bool castStarted)
{
    RecordCombat(interruptClaims, bot, target, spell, castStarted);
}

bool GroupCcTargetReservation::ClaimResurrection(PlayerbotAI* ai, Unit* target)
{
    return ClaimCombat(resurrectionClaims, ai, target, true, std::string(), kResurrectionClaimMs, 0);
}

void GroupCcTargetReservation::RecordResurrection(Player* bot, Unit* target, bool castStarted)
{
    RecordCombat(resurrectionClaims, bot, target, std::string(), castStarted);
}

bool GroupCcTargetReservation::IsClaimedByOther(Player* bot, ObjectGuid targetGuid)
{
    uint32 groupId = AccessGroupState(bot);
    if (!groupId || targetGuid.IsEmpty())
        return false;

    CcClaim* claim = FindClaim(groupId, targetGuid);
    return claim && claim->ownerGuid != bot->GetObjectGuid();
}

bool GroupCcTargetReservation::IsSkipped(Player* bot, ObjectGuid targetGuid)
{
    uint32 groupId = AccessGroupState(bot);
    if (!groupId || targetGuid.IsEmpty())
        return false;

    std::unordered_map<uint32, std::vector<CcSkip>>::iterator groupIt = skipsByGroup.find(groupId);
    if (groupIt == skipsByGroup.end())
        return false;

    ObjectGuid botGuid = bot->GetObjectGuid();
    for (CcSkip const& skip : groupIt->second)
    {
        if (skip.ownerGuid == botGuid && skip.targetGuid == targetGuid)
            return true;
    }

    return false;
}

bool GroupCcTargetReservation::IsOwnedBy(Player* bot, ObjectGuid targetGuid)
{
    uint32 groupId = AccessGroupState(bot);
    if (!groupId || targetGuid.IsEmpty())
        return false;

    CcClaim* claim = FindClaim(groupId, targetGuid);
    return claim && claim->ownerGuid == bot->GetObjectGuid();
}

bool GroupCcTargetReservation::IsInFlight(Player* bot, ObjectGuid targetGuid)
{
    uint32 groupId = AccessGroupState(bot);
    if (!groupId || targetGuid.IsEmpty())
        return false;

    CcClaim* claim = FindClaim(groupId, targetGuid);
    return claim && claim->ownerGuid == bot->GetObjectGuid() && claim->inFlight;
}

ObjectGuid GroupCcTargetReservation::GetOwnedTarget(Player* bot)
{
    uint32 groupId = AccessGroupState(bot);
    if (!groupId)
        return ObjectGuid();

    CcClaim* claim = FindOwnedClaim(groupId, bot->GetObjectGuid());
    return claim ? claim->targetGuid : ObjectGuid();
}

void GroupCcTargetReservation::Claim(Player* bot, ObjectGuid targetGuid, bool reacquireAfterExpiry)
{
    uint32 groupId = AccessGroupState(bot);
    if (!groupId || !bot || targetGuid.IsEmpty())
        return;

    if (IsClaimedByOther(bot, targetGuid))
        return;

    ObjectGuid botGuid = bot->GetObjectGuid();
    if (FindClaim(groupId, targetGuid))
    {
        // Hard 3s selection lease: do not refresh, even for the owner.
        // In-flight windows are extended only by RecordCast on a real start.
        return;
    }

    if (IsSkipped(bot, targetGuid))
        return;

    if (!reacquireAfterExpiry && FindMemory(groupId, botGuid, targetGuid))
        return;

    if (CcClaim* owned = FindOwnedClaim(groupId, botGuid))
    {
        if (owned->targetGuid == targetGuid)
            return;

        // In-flight protection outranks a later selection of a different add.
        if (owned->inFlight)
            return;

        Release(bot, owned->targetGuid);
        Prune(groupId);
    }

    uint8 failedStarts = 0;
    if (CcLeaseMemory* memory = FindMemory(groupId, botGuid, targetGuid))
        failedStarts = memory->failedStarts;

    if (failedStarts >= kMaxFailedStarts)
    {
        AddSkip(groupId, botGuid, targetGuid);
        ClearMemory(groupId, botGuid, targetGuid);
        return;
    }

    CcClaim claim;
    claim.targetGuid = targetGuid;
    claim.ownerGuid = botGuid;
    claim.startMs = WorldTimer::getMSTime();
    claim.durationMs = kSelectDurationMs;
    claim.failedStarts = failedStarts;
    claim.inFlight = false;
    claimsByGroup[groupId].push_back(claim);
}

bool GroupCcTargetReservation::PrepareFallbackCast(PlayerbotAI* ai, Unit* target)
{
    if (!ai || !target)
        return false;

    Player* bot = ai->GetBot();
    if (!bot)
        return false;

    // Ungrouped bots never participate in the fallback claim table.
    // AccessGroupState still amortizes inactive-group expiry.
    if (!AccessGroupState(bot))
        return true;

    Unit* rtiCcTarget = ai->GetAiObjectContext()->GetValue<Unit*>("rti cc target")->Get();
    if (rtiCcTarget == target)
        return true;

    ObjectGuid targetGuid = target->GetObjectGuid();
    if (IsClaimedByOther(bot, targetGuid))
        return false;

    if (IsSkipped(bot, targetGuid) && !IsOwnedBy(bot, targetGuid))
        return false;

    if (!IsOwnedBy(bot, targetGuid))
        Claim(bot, targetGuid, true);

    return IsOwnedBy(bot, targetGuid);
}

void GroupCcTargetReservation::RecordCast(Player* bot, Unit* target, bool castStarted)
{
    if (!bot || !target)
        return;

    uint32 groupId = AccessGroupState(bot);
    if (!groupId)
        return;

    CcClaim* claim = FindClaim(groupId, target->GetObjectGuid());
    if (!claim || claim->ownerGuid != bot->GetObjectGuid())
        return;

    // A failed recast must not drop a live in-flight claim.
    if (!castStarted && claim->inFlight)
        return;

    if (castStarted)
    {
        claim->startMs = WorldTimer::getMSTime();
        claim->durationMs = kCastDurationMs;
        claim->inFlight = true;
        // Successful start: wait for the aura (or 5s in-flight expiry). Do not
        // spend a failed-start slot and do not skip later retries until this
        // window ends without verification.
        ClearMemory(groupId, claim->ownerGuid, claim->targetGuid);
        return;
    }

    claim->failedStarts++;
    RememberLease(groupId, claim->ownerGuid, claim->targetGuid, claim->failedStarts);
    if (claim->failedStarts >= kMaxFailedStarts)
    {
        ObjectGuid targetGuid = claim->targetGuid;
        ObjectGuid botGuid = bot->GetObjectGuid();
        AddSkip(groupId, botGuid, targetGuid);
        ClearMemory(groupId, botGuid, targetGuid);
        Release(bot, targetGuid);
    }
}

void GroupCcTargetReservation::Release(Player* bot, ObjectGuid targetGuid)
{
    uint32 groupId = GetGroupId(bot);
    if (!groupId || !bot || targetGuid.IsEmpty())
    {
        MaybeSweepInactiveGroups();
        return;
    }

    // Explicit drop must not run claim-expiry transitions on this target.
    // Sweep after the erase so an interval hit cannot RememberLease/AddSkip
    // a timed-out claim and then have ClearMemory wipe that result.
    ObjectGuid botGuid = bot->GetObjectGuid();
    ClearMemory(groupId, botGuid, targetGuid);

    std::unordered_map<uint32, std::vector<CcClaim>>::iterator groupIt = claimsByGroup.find(groupId);
    if (groupIt == claimsByGroup.end())
    {
        MaybeSweepInactiveGroups();
        return;
    }

    std::vector<CcClaim>& claims = groupIt->second;
    for (std::vector<CcClaim>::iterator it = claims.begin(); it != claims.end(); ++it)
    {
        if (it->targetGuid == targetGuid && it->ownerGuid == botGuid)
        {
            claims.erase(it);
            break;
        }
    }

    if (claims.empty())
        claimsByGroup.erase(groupIt);

    MaybeSweepInactiveGroups();
}
