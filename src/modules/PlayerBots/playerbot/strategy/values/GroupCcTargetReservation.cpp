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
    constexpr uint8 kMaxAttempts = 3;

    struct CcClaim
    {
        ObjectGuid targetGuid;
        ObjectGuid ownerGuid;
        uint32 startMs = 0;
        uint32 durationMs = 0;
        uint8 attempts = 0;
        bool inFlight = false;
    };

    struct CcSkip
    {
        ObjectGuid targetGuid;
        ObjectGuid ownerGuid;
        uint32 startMs = 0;
        uint32 durationMs = 0;
    };

    std::unordered_map<uint32, std::vector<CcClaim>> claimsByGroup;
    std::unordered_map<uint32, std::vector<CcSkip>> skipsByGroup;

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

    void Prune(uint32 groupId)
    {
        std::vector<CcClaim>& claims = claimsByGroup[groupId];
        for (std::vector<CcClaim>::iterator it = claims.begin(); it != claims.end();)
        {
            if (IsTimedOut(it->startMs, it->durationMs))
            {
                // Selection timeout frees the add so another bot (or this one)
                // can reacquire. Skip only in-flight expiry: the owner already
                // started a real cast that never verified.
                if (it->inFlight)
                    AddSkip(groupId, it->ownerGuid, it->targetGuid);
                it = claims.erase(it);
            }
            else
                ++it;
        }

        if (claims.empty())
            claimsByGroup.erase(groupId);

        std::vector<CcSkip>& skips = skipsByGroup[groupId];
        for (std::vector<CcSkip>::iterator it = skips.begin(); it != skips.end();)
        {
            if (IsTimedOut(it->startMs, it->durationMs))
                it = skips.erase(it);
            else
                ++it;
        }

        if (skips.empty())
            skipsByGroup.erase(groupId);
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

bool GroupCcTargetReservation::IsClaimedByOther(Player* bot, ObjectGuid targetGuid)
{
    uint32 groupId = GetGroupId(bot);
    if (!groupId || targetGuid.IsEmpty())
        return false;

    Prune(groupId);
    CcClaim* claim = FindClaim(groupId, targetGuid);
    return claim && claim->ownerGuid != bot->GetObjectGuid();
}

bool GroupCcTargetReservation::IsSkipped(Player* bot, ObjectGuid targetGuid)
{
    uint32 groupId = GetGroupId(bot);
    if (!groupId || targetGuid.IsEmpty())
        return false;

    Prune(groupId);
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
    uint32 groupId = GetGroupId(bot);
    if (!groupId || targetGuid.IsEmpty())
        return false;

    Prune(groupId);
    CcClaim* claim = FindClaim(groupId, targetGuid);
    return claim && claim->ownerGuid == bot->GetObjectGuid();
}

bool GroupCcTargetReservation::IsInFlight(Player* bot, ObjectGuid targetGuid)
{
    uint32 groupId = GetGroupId(bot);
    if (!groupId || targetGuid.IsEmpty())
        return false;

    Prune(groupId);
    CcClaim* claim = FindClaim(groupId, targetGuid);
    return claim && claim->ownerGuid == bot->GetObjectGuid() && claim->inFlight;
}

ObjectGuid GroupCcTargetReservation::GetOwnedTarget(Player* bot)
{
    uint32 groupId = GetGroupId(bot);
    if (!groupId)
        return ObjectGuid();

    Prune(groupId);
    CcClaim* claim = FindOwnedClaim(groupId, bot->GetObjectGuid());
    return claim ? claim->targetGuid : ObjectGuid();
}

void GroupCcTargetReservation::Claim(Player* bot, ObjectGuid targetGuid)
{
    uint32 groupId = GetGroupId(bot);
    if (!groupId || !bot || targetGuid.IsEmpty())
        return;

    Prune(groupId);
    if (IsClaimedByOther(bot, targetGuid))
        return;

    ObjectGuid botGuid = bot->GetObjectGuid();
    if (CcClaim* existing = FindClaim(groupId, targetGuid))
    {
        if (existing->ownerGuid == botGuid)
        {
            // Keep the selection heartbeat alive while the bot still intends
            // this add. Do not shorten an in-flight cast window.
            if (!existing->inFlight)
            {
                existing->startMs = WorldTimer::getMSTime();
                existing->durationMs = kSelectDurationMs;
            }
            return;
        }

        return;
    }

    if (IsSkipped(bot, targetGuid))
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

    CcClaim claim;
    claim.targetGuid = targetGuid;
    claim.ownerGuid = botGuid;
    claim.startMs = WorldTimer::getMSTime();
    claim.durationMs = kSelectDurationMs;
    claim.attempts = 0;
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
    if (!GetGroupId(bot))
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
        Claim(bot, targetGuid);

    return IsOwnedBy(bot, targetGuid);
}

void GroupCcTargetReservation::RecordCast(Player* bot, Unit* target, bool castStarted)
{
    if (!bot || !target)
        return;

    uint32 groupId = GetGroupId(bot);
    if (!groupId)
        return;

    Prune(groupId);
    CcClaim* claim = FindClaim(groupId, target->GetObjectGuid());
    if (!claim || claim->ownerGuid != bot->GetObjectGuid())
        return;

    // A failed recast must not drop a live in-flight claim (including after the
    // 3rd successful start, which already added a skip).
    if (!castStarted && claim->inFlight)
        return;

    claim->attempts++;
    if (castStarted)
    {
        claim->startMs = WorldTimer::getMSTime();
        claim->durationMs = kCastDurationMs;
        claim->inFlight = true;
    }

    if (claim->attempts >= kMaxAttempts)
    {
        ObjectGuid targetGuid = claim->targetGuid;
        ObjectGuid botGuid = bot->GetObjectGuid();
        AddSkip(groupId, botGuid, targetGuid);
        if (!castStarted)
            Release(bot, targetGuid);
    }
}

void GroupCcTargetReservation::Release(Player* bot, ObjectGuid targetGuid)
{
    uint32 groupId = GetGroupId(bot);
    if (!groupId || !bot || targetGuid.IsEmpty())
        return;

    std::unordered_map<uint32, std::vector<CcClaim>>::iterator groupIt = claimsByGroup.find(groupId);
    if (groupIt == claimsByGroup.end())
        return;

    ObjectGuid botGuid = bot->GetObjectGuid();
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
}
