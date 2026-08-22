/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcSmartRest.h"

#include "DcSmartRestDecision.h"
#include "DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"

#include <optional>
#include <string>
#include <vector>

#include "Group.h"
#include "Log.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Timer.h"

namespace
{
    using DcSmartRestDecision::Member;
    using DcSmartRestDecision::Inputs;

    // Living, same-map group members — the exact population
    // DcPartyState::IsPartyReady gates on. `players` (optional) receives the
    // matching Player* per snapshot row so DescribeWait can name blockers.
    void BuildSnapshot(Player* leader, std::vector<Member>& out,
                       std::vector<Player*>* players = nullptr)
    {
        Group* group = leader->GetGroup();
        if (!group)
        {
            // Solo tank: a one-member party still smart-rests on itself.
            Member m;
            m.hpPct = leader->GetHealthPct();
            m.isManaUser = leader->getPowerType() == POWER_MANA &&
                           leader->GetMaxPower(POWER_MANA) > 0;
            if (m.isManaUser)
                m.manaPct = leader->GetPowerPct(POWER_MANA);
            m.isHealer = PlayerbotAI::IsHeal(leader);
            out.push_back(m);
            if (players)
                players->push_back(leader);
            return;
        }

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member)
                continue;
            if (member->GetMapId() != leader->GetMapId())
                continue;
            if (member->isDead())
                continue;  // Dead members hold the run via the rez recovery
                           // (DcRezRecovery::IsPending), never the rest latch.

            Member m;
            m.hpPct = member->GetHealthPct();
            m.isManaUser = member->getPowerType() == POWER_MANA &&
                           member->GetMaxPower(POWER_MANA) > 0;
            if (m.isManaUser)
                m.manaPct = member->GetPowerPct(POWER_MANA);
            m.isHealer = PlayerbotAI::IsHeal(member);
            out.push_back(m);
            if (players)
                players->push_back(member);
        }
    }

    Inputs BuildInputs(Player* leader, AiObjectContext* leaderCtx, DcRunState const& run, uint32 now)
    {
        Inputs in;
        in.latched = run.smartRestLatched;
        in.restElapsedMs = run.smartRestLatched ? now - run.smartRestSinceMs : 0;
        in.rearmed = run.smartRestRearmAtMs == 0 ||
                     static_cast<int32>(now - run.smartRestRearmAtMs) >= 0;
        in.hpTriggerPct = static_cast<float>(DcSettings::GetUInt(leader, "SmartRestHealthPct"));
        in.dpsManaTriggerPct = static_cast<float>(DcSettings::GetUInt(leader, "SmartRestDpsManaPct"));
        in.healerManaTriggerPct = static_cast<float>(DcSettings::GetUInt(leader, "SmartRestHealerManaPct"));
        in.maxRestMs = DC_SMART_REST_MAX_MS;

        // Boss-pull top-off: the next anchor is a real boss and the tank is
        // inside its engage range (same memoized distance+floor test the at-boss
        // trigger fires on), so the very next pull IS the boss. Raise the latch
        // entry to the mana release bar — never open a boss on a party that
        // merely clears the low trash triggers. Deliberately door/room-trash
        // blind: resting at a closed boss door, or between the careful pulls of
        // a room-aggro pre-clear, is exactly what a human group does at a boss.
        if (leaderCtx)
        {
            std::optional<DungeonBossInfo> const next =
                leaderCtx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
            in.bossPull = next.has_value() && next->kind == DungeonAnchorKind::Boss &&
                          DcTickMemoAccess::AtBossEngage(leader, leaderCtx, *next);
        }
        return in;
    }
}

namespace DcSmartRest
{
    bool Enabled(Player* bot)
    {
        return DcSettings::GetBool(bot, "SmartRest");
    }

    bool UpdateLatch(Player* leader, AiObjectContext* leaderCtx)
    {
        if (!leader || !leaderCtx)
            return false;

        // Defensive leader guard: only the run's resolved tank owns the latch.
        // Any other caller (a follower's context reaching this gate through a
        // future refactor) must read the TANK's latch, never grow its own.
        Player* tank = leaderCtx->GetValue<Player*>(DcKey::PartyTank)->Get();
        if (tank && tank != leader)
            return IsLatched(tank);

        DcRunState& run = DcRun::Of(leaderCtx);
        uint32 const now = getMSTime();

        std::vector<Member> members;
        BuildSnapshot(leader, members);

        Inputs const in = BuildInputs(leader, leaderCtx, run, now);
        DcSmartRestDecision::Result const verdict =
            DcSmartRestDecision::Decide(in, members);

        if (verdict.latched && !run.smartRestLatched)
        {
            run.smartRestSinceMs = now;
            // DEBUG, not INFO: a latch is the normal, expected event of every
            // single rest cycle, so at INFO it was pure per-run noise in
            // Server.log. The rest is already visible where it matters — the
            // addon status line (DcStatusPublisher) names who we are waiting on.
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] smart rest: latched — party resting up ({} member(s) below {})",
                      leader->GetName(), verdict.blockers.size(),
                      in.bossPull ? "the boss-pull top-off bar" : "trigger");
        }
        else if (!verdict.latched && run.smartRestLatched)
        {
            if (verdict.timedOut)
            {
                // Someone can't reach its release bar (AFK human, bot with no
                // food) — push on rather than stall the run, and hold off
                // re-latching so the same member can't flap us straight back.
                run.smartRestRearmAtMs = now + DC_SMART_REST_REARM_MS;
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] smart rest: timeout release after {}ms — pushing on, re-latch in {}ms",
                         leader->GetName(), in.restElapsedMs, DC_SMART_REST_REARM_MS);
            }
            run.smartRestSinceMs = 0;
        }
        run.smartRestLatched = verdict.latched;
        return run.smartRestLatched;
    }

    bool IsLatched(Player* leaderTank)
    {
        if (!leaderTank)
            return false;
        PlayerbotAI* tankAI = GET_PLAYERBOT_AI(leaderTank);
        if (!tankAI)
            return false;
        return DcRun::Of(tankAI).smartRestLatched;
    }

    std::string DescribeWait(Player* leader)
    {
        if (!leader)
            return "";

        std::vector<Member> members;
        std::vector<Player*> players;
        BuildSnapshot(leader, members, &players);

        PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
        if (!leaderAI)
            return "";
        // BelowRelease/the bars don't read bossPull, so a null ctx would do —
        // pass the real one anyway to keep the Inputs identical to UpdateLatch's.
        Inputs const in = BuildInputs(leader, leaderAI->GetAiObjectContext(),
                                      DcRun::Of(leaderAI), getMSTime());

        // Keep the addon line short: name a few members, then collapse the
        // rest — same shape as DcPartyState::DescribePartyNotReady.
        constexpr size_t MAX_NAMED = 3;
        std::vector<std::string> parts;
        size_t extra = 0;

        for (size_t i = 0; i < members.size(); ++i)
        {
            Member const& m = members[i];
            if (!DcSmartRestDecision::BelowRelease(m, in))
                continue;

            std::string reason;
            if (m.hpPct < DcSmartRestDecision::HpReleaseBar(m, in))
                reason = "hp " + std::to_string(static_cast<int>(m.hpPct)) + "%";
            else
                reason = "mana " + std::to_string(static_cast<int>(m.manaPct)) + "%";

            if (parts.size() < MAX_NAMED)
                parts.push_back(std::string(players[i]->GetName()) + " (" + reason + ")");
            else
                ++extra;
        }

        if (parts.empty())
            return "";

        std::string out = "Smart Rest: waiting on ";
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i)
                out += ", ";
            out += parts[i];
        }
        if (extra)
            out += " +" + std::to_string(extra) + " more";
        return out;
    }
}
