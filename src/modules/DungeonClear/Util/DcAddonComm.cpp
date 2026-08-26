#include "Util/DcAddonComm.h"
#include "playerbot/playerbot.h"
#include "Util/DungeonClearUtil.h"
#include "DcValueKeys.h"
#include "DcRunState.h"
#include "Data/DungeonBossInfo.h"
#include "Data/DungeonWingRegistry.h"
#include "Maps/GridSearchers.h"
#include "Maps/Map.h"
#include "Group/Group.h"
#include "ObjectAccessor.h"
#include "Objects/Player.h"
#include "Objects/Creature.h"
#include "Chat/Chat.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace
{
    struct PushState
    {
        std::string lastStatus;
        bool primed = false;
    };

    std::map<ObjectGuid, PushState> g_activeTanks;
    std::mutex g_activeMutex;
    uint32 g_pushAccumMs = 0;
    constexpr uint32 kPushIntervalMs = 500;

    bool IsRealPlayer(Player* p)
    {
        return p && p->IsInWorld() && p->GetSession() && !p->GetPlayerbotAI();
    }

    bool BossAliveNear(Player* bot, DungeonBossInfo const& info)
    {
        if (!bot || !info.entry)
            return false;
        float radius = std::max(40.0f, info.arriveRadius * 4.0f);
        std::list<Creature*> found;
        for (uint32 entry : info.alternateEntries)
            GetCreatureListWithEntryInGrid(found, bot, entry, radius);
        GetCreatureListWithEntryInGrid(found, bot, info.entry, radius);
        for (Creature* c : found)
            if (c && c->IsAlive())
                return true;
        return false;
    }

    std::string BossStatus(Player* bot,
                           DungeonBossInfo const& info,
                           std::unordered_set<uint32> const& skipped,
                           std::unordered_set<uint32> const& cleared)
    {
        uint32 const stateKey = DungeonBossStateKey(info);
        uint32 const skipKey = stateKey;
        uint32 const anchorKey = stateKey;
        if (cleared.count(anchorKey))
            return "dead";
        if (skipped.count(skipKey))
            return "skipped";
        if (info.kind == DungeonAnchorKind::Objective)
            return "alive";
        if (Map* map = bot->GetMap())
        {
            if (map->IsLoaded(info.x, info.y) && !BossAliveNear(bot, info))
            {
                // Grid loaded and no living boss — treat as dead when close enough
                // that we would have seen it; otherwise still "alive" (not yet reached).
                if (bot->GetDistance(info.x, info.y, info.z) <= info.arriveRadius * 4.0f)
                    return info.spawnOnApproach ? "waiting" : "dead";
            }
        }
        return "alive";
    }

    std::string Truncate(std::string s, size_t maxLen)
    {
        if (s.size() <= maxLen)
            return s;
        return s.substr(0, maxLen);
    }
}

namespace DcAddonComm
{
    uint8 TortoisePullToAddon(uint8 tortoiseMode)
    {
        // Tortoise: Dynamic=0, Leeroy=1, Advanced=2
        // Addon:    Leeroy/Off=0, Advanced/On=1, Dynamic=2
        switch (tortoiseMode % 3)
        {
            case 1: return 0;
            case 2: return 1;
            default: return 2;
        }
    }

    uint8 AddonPullToTortoise(uint8 addonMode)
    {
        switch (addonMode % 3)
        {
            case 0: return 1; // Leeroy
            case 1: return 2; // Advanced
            default: return 0; // Dynamic
        }
    }

    uint8 AddonPullKeywordToTortoise(std::string const& param, uint8 currentTortoise)
    {
        std::string p = param;
        for (char& c : p)
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if (p == "off" || p == "leeroy")
            return 1;
        if (p == "on" || p == "advanced")
            return 2;
        if (p == "dynamic" || p == "dyn")
            return 0;
        return (currentTortoise + 1) % 3;
    }

    void SendToPlayer(Player* player, std::string const& payload)
    {
        if (!player || !player->GetSession())
            return;
        // Turtle/vanilla: SendAddonMessage(prefix, message) → client sees
        // CHAT_MSG_ADDON with arg1=prefix, arg2=message.
        player->SendAddonMessage("DC", Truncate(payload, 250));
    }

    void SendToGroup(PlayerbotAI* ai, Player* bot, std::string const& payload)
    {
        if (!bot)
            return;
        std::string const clipped = Truncate(payload, 250);

        if (Group* g = bot->GetGroup())
        {
            for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
            {
                Player* m = ref->getSource();
                if (IsRealPlayer(m))
                    m->SendAddonMessage("DC", clipped, bot);
            }
            return;
        }

        // Solo / self-bot: deliver to the commanding real player if any.
        if (ai)
        {
            if (Player* master = ai->GetMaster())
            {
                if (IsRealPlayer(master))
                    master->SendAddonMessage("DC", clipped, bot);
                else if (master->GetSession())
                    master->SendAddonMessage("DC", clipped, bot);
            }
        }
        if (IsRealPlayer(bot))
            bot->SendAddonMessage("DC", clipped);
    }

    void SendError(Player* player, std::string const& msg)
    {
        SendToPlayer(player, "ERROR\t" + msg);
    }

    std::string BuildStatusPayload(PlayerbotAI* ai, Player* bot)
    {
        if (!ai || !bot || !ai->GetAiObjectContext())
            return "STATUS\t0\t0\tNone\t\t0\toff\t\t2\t0";

        AiObjectContext* ctx = ai->GetAiObjectContext();
        DcRunState const& st = ctx->GetValue<DcRunState&>(DcKey::RunState)->Get();
        uint8 pull = ctx->GetValue<uint8&>(DcKey::PullMode)->Get();
        auto next = ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
        auto const& skipped = ctx->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get();
        std::string stall = ctx->GetValue<std::string&>(DcKey::StallReason)->Get();

        std::string stateStr = "off";
        std::string detail;
        if (st.enabled && st.paused)
        {
            stateStr = "paused";
            detail = st.pauseReason.empty() ? "holding position" : st.pauseReason;
        }
        else if (st.enabled && !stall.empty())
        {
            stateStr = "stalled";
            detail = stall;
        }
        else if (st.enabled && bot->IsInCombat())
        {
            bool bossFight = false;
            if (next && next->kind == DungeonAnchorKind::Boss && next->entry)
            {
                if (Unit* t = bot->GetVictim())
                    if (DungeonBossMatchesEntry(*next, t->GetEntry()))
                        bossFight = true;
            }
            stateStr = bossFight ? "fighting_boss" : "fighting_trash";
            detail = bossFight && next ? ("Fighting " + next->name + ".") : "In combat.";
        }
        else if (st.enabled && next)
        {
            if (!bot->IsStopped())
            {
                stateStr = "moving";
                detail = "En route to " + next->name + ".";
            }
            else
            {
                stateStr = "idle";
                detail = "Holding near " + next->name + ".";
            }
        }
        else if (st.enabled)
        {
            stateStr = "idle";
            detail = "Idle.";
        }

        std::ostringstream out;
        out << "STATUS\t"
            << (st.enabled ? "1" : "0") << "\t"
            << (next ? next->entry : 0) << "\t"
            << (next ? next->name : "None") << "\t"
            << stall << "\t"
            << skipped.size() << "\t"
            << stateStr << "\t"
            << detail << "\t"
            << uint32(TortoisePullToAddon(pull)) << "\t"
            << "0"; // live Dynamic verdict — Tortoise has no per-pack governor yet
        return out.str();
    }

    void PushStatus(PlayerbotAI* ai, Player* bot)
    {
        if (!ai || !bot)
            return;
        std::string payload = BuildStatusPayload(ai, bot);
        SendToGroup(ai, bot, payload);
        {
            std::lock_guard<std::mutex> lock(g_activeMutex);
            auto& ps = g_activeTanks[bot->GetObjectGuid()];
            ps.lastStatus = payload;
            ps.primed = true;
        }
    }

    void PushBossList(PlayerbotAI* ai, Player* bot, bool silent)
    {
        if (!ai || !bot || !ai->GetAiObjectContext())
            return;

        AiObjectContext* ctx = ai->GetAiObjectContext();
        auto bosses = ctx->GetValue<std::vector<DungeonBossInfo>>(DcKey::DungeonBosses)->Get();
        auto const& skipped = ctx->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get();
        auto const& cleared = ctx->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get();

        SendToGroup(ai, bot, "BOSS_START");
        if (bosses.empty())
        {
            if (!silent)
                SendToGroup(ai, bot, "CHAT\tNo bosses found for this map.");
            SendToGroup(ai, bot, "BOSS_END");
            return;
        }

        for (DungeonBossInfo const& info : bosses)
        {
            std::string name = info.name;
            if (info.kind == DungeonAnchorKind::Objective)
                name = "Objective: " + info.name;
            std::string status = BossStatus(bot, info, skipped, cleared);
            std::string wing = DungeonWingRegistry::WingName(bot->GetMapId(), info.entry);

            std::ostringstream line;
            line << "BOSS\t" << info.entry << "\t" << BossOrderKey(info) << "\t"
                 << name << "\t" << status << "\t"
                 << int(info.x) << "\t" << int(info.y) << "\t" << int(info.z) << "\t"
                 << wing << "\t\t" << info.encounterIndex;
            SendToGroup(ai, bot, line.str());
        }
        SendToGroup(ai, bot, "BOSS_END");
    }

    void MarkActiveTank(ObjectGuid tank)
    {
        std::lock_guard<std::mutex> lock(g_activeMutex);
        g_activeTanks[tank]; // primed=false → force emit on next tick
    }

    void UnmarkActiveTank(ObjectGuid tank)
    {
        std::lock_guard<std::mutex> lock(g_activeMutex);
        g_activeTanks.erase(tank);
    }

    void TickStatusPushes(uint32 diffMs)
    {
        g_pushAccumMs += diffMs;
        if (g_pushAccumMs < kPushIntervalMs)
            return;
        g_pushAccumMs = 0;

        std::vector<ObjectGuid> tanks;
        {
            std::lock_guard<std::mutex> lock(g_activeMutex);
            if (g_activeTanks.empty())
                return;
            tanks.reserve(g_activeTanks.size());
            for (auto const& kv : g_activeTanks)
                tanks.push_back(kv.first);
        }

        for (ObjectGuid guid : tanks)
        {
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (!bot || !bot->IsInWorld() || !bot->GetPlayerbotAI())
            {
                std::lock_guard<std::mutex> lock(g_activeMutex);
                g_activeTanks.erase(guid);
                continue;
            }
            PlayerbotAI* ai = bot->GetPlayerbotAI();
            if (!ai->GetAiObjectContext())
                continue;
            DcRunState const& st = ai->GetAiObjectContext()->GetValue<DcRunState&>(DcKey::RunState)->Get();
            if (!st.enabled)
            {
                std::lock_guard<std::mutex> lock(g_activeMutex);
                g_activeTanks.erase(guid);
                continue;
            }

            std::string payload = BuildStatusPayload(ai, bot);
            bool send = false;
            {
                std::lock_guard<std::mutex> lock(g_activeMutex);
                auto it = g_activeTanks.find(guid);
                if (it == g_activeTanks.end())
                    continue;
                if (!it->second.primed || it->second.lastStatus != payload)
                {
                    it->second.lastStatus = payload;
                    it->second.primed = true;
                    send = true;
                }
            }
            if (send)
                SendToGroup(ai, bot, payload);
        }
    }
}
