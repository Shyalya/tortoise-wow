#include "Util/DcAddonComm.h"
#include "playerbot/playerbot.h"
#include "Util/DungeonClearUtil.h"
#include "DcValueKeys.h"
#include "DcRunState.h"
#include "Settings/DcSettings.h"
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
#include <utility>
#include <vector>

namespace
{
    struct PushState
    {
        std::string lastStatus;
        std::string lastBoss;
        bool primed = false;
        bool bossPrimed = false;
        bool bossPushInFlight = false;
    };

    // Build the fingerprint and the rows from one view of the AI context.  A
    // periodic tick should not scan every boss once to decide whether to push
    // and then scan them all again while constructing the push itself.  Keeping
    // the statuses alongside the fingerprint also makes the decision and the
    // packet internally consistent when a creature dies between ticks.
    struct BossListSnapshot
    {
        std::vector<DungeonBossInfo> bosses;
        std::vector<std::string> statuses;
        std::string fingerprint;
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
                           std::unordered_set<uint32> const& cleared,
                           DcRunState const& runState)
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
                {
                    if (info.spawnOnApproach && !runState.observedBosses.count(stateKey))
                        return "waiting";
                    return "dead";
                }
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

    BossListSnapshot BuildBossListSnapshot(PlayerbotAI* ai, Player* bot)
    {
        BossListSnapshot snapshot;
        if (!ai || !bot || !ai->GetAiObjectContext())
            return snapshot;

        AiObjectContext* ctx = ai->GetAiObjectContext();
        // Resolve NextDungeonBoss first.  Its calculation can update the
        // cleared/selected state that is reflected by both the rows and the
        // fingerprint below.
        auto next = ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
        snapshot.bosses = ctx->GetValue<std::vector<DungeonBossInfo>>(DcKey::DungeonBosses)->Get();
        auto const& skipped = ctx->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get();
        auto const& cleared = ctx->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get();
        DcRunState const& state = ctx->GetValue<DcRunState&>(DcKey::RunState)->Get();

        std::ostringstream out;
        out << state.selectedBossEntry << ':' << state.selectedBossStateKey << '|'
            << (next ? DungeonBossStateKey(*next) : 0) << '|';
        snapshot.statuses.reserve(snapshot.bosses.size());
        for (DungeonBossInfo const& info : snapshot.bosses)
        {
            uint32 const key = DungeonBossStateKey(info);
            std::string status = BossStatus(bot, info, skipped, cleared, state);
            out << key << ':' << status << ';';
            snapshot.statuses.push_back(std::move(status));
        }
        snapshot.fingerprint = out.str();
        return snapshot;
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

    void SendBossListSnapshot(PlayerbotAI* ai, Player* bot, bool silent,
                              BossListSnapshot const& snapshot)
    {
        SendToGroup(ai, bot, "BOSS_START");
        if (snapshot.bosses.empty())
        {
            if (!silent)
                SendToGroup(ai, bot, "CHAT\tNo bosses found for this map.");
            SendToGroup(ai, bot, "BOSS_END");
            return;
        }

        for (size_t i = 0; i < snapshot.bosses.size(); ++i)
        {
            DungeonBossInfo const& info = snapshot.bosses[i];
            std::string name = info.name;
            if (info.kind == DungeonAnchorKind::Objective)
                name = "Objective: " + info.name;
            std::string const& status = snapshot.statuses[i];
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

    bool ClaimBossPush(ObjectGuid tank, std::string const& fingerprint,
                       bool explicitRequest)
    {
        std::lock_guard<std::mutex> lock(g_activeMutex);
        auto& pushState = g_activeTanks[tank];
        // A command/request arriving while the periodic path is already
        // sending this exact snapshot needs no second packet.  Explicit
        // requests otherwise always send, even when the fingerprint is
        // unchanged, because the client may have just opened the panel.
        if (explicitRequest && pushState.bossPushInFlight
            && pushState.bossPrimed && pushState.lastBoss == fingerprint)
            return false;

        pushState.lastBoss = fingerprint;
        pushState.bossPrimed = true;
        pushState.bossPushInFlight = true;
        return true;
    }

    void FinishBossPush(ObjectGuid tank, std::string const& fingerprint)
    {
        std::lock_guard<std::mutex> lock(g_activeMutex);
        auto it = g_activeTanks.find(tank);
        if (it != g_activeTanks.end() && it->second.bossPrimed
            && it->second.lastBoss == fingerprint)
            it->second.bossPushInFlight = false;
    }

    void PushSettings(Player* recipient, PlayerbotAI* /*ai*/, Player* bot)
    {
        if (!recipient)
            return;

        DcRunState* state = DcUtil::LeaderRunState(bot);
        SendToPlayer(recipient, "SYNCSTART");

        auto send = [&](char const* key, uint32 value, uint32 minValue,
                        uint32 maxValue, uint32 type, bool overridden)
        {
            std::ostringstream line;
            line << "SETTINGS\t" << key << "\t" << value << "\t"
                 << minValue << "\t" << maxValue << "\t" << type << "\t"
                 << (overridden ? 1 : 0);
            SendToPlayer(recipient, line.str());
        };
        auto sendFloat = [&](char const* key, float value, float minValue,
                             float maxValue, uint32 type, bool overridden)
        {
            std::ostringstream line;
            line << "SETTINGS\t" << key << "\t" << value << "\t"
                 << minValue << "\t" << maxValue << "\t" << type << "\t"
                 << (overridden ? 1 : 0);
            SendToPlayer(recipient, line.str());
        };

        // Only advertise settings with real server-side behavior.  This keeps
        // the addon from presenting controls that merely acknowledge a command.
        bool const preventRelease = state && state->hasPreventBotReleaseOverride
            ? state->preventBotReleaseOverride : DcUtil::EffectivePreventBotRelease(bot);
        uint32 const lootQuality = state && state->hasLootQualityOverride
            ? state->lootQualityOverride : DcUtil::EffectiveLootQualityMin(bot);
        bool const ignoreChests = state && state->hasIgnoreChestsOverride
            ? state->ignoreChestsOverride : DcUtil::EffectiveIgnoreChests(bot);
        float const restHealth = state && state->hasRestHealthOverride
            ? state->restHealthOverride : DcUtil::EffectiveRestHealth(bot);
        float const restMana = state && state->hasRestManaOverride
            ? state->restManaOverride : DcUtil::EffectiveRestMana(bot);
        float const partyMaxSpread = state && state->hasPartyMaxSpreadOverride
            ? state->partyMaxSpreadOverride : DcUtil::EffectivePartyMaxSpread(bot);
        uint32 const pullMax = state && state->hasPullMaxOverride
            ? state->pullMaxOverride : DcUtil::EffectivePullMax(bot);

        send("PreventBotRelease", preventRelease ? 1u : 0u, 0, 1, 0,
            state && state->hasPreventBotReleaseOverride);
        send("LootMinQuality", lootQuality, 0, 6, 1,
            state && state->hasLootQualityOverride);
        send("IgnoreChests", ignoreChests ? 1u : 0u, 0, 1, 0,
            state && state->hasIgnoreChestsOverride);
        send("RestHealthPct", static_cast<uint32>(restHealth + 0.5f), 0, 100, 1,
            state && state->hasRestHealthOverride);
        send("RestManaPct", static_cast<uint32>(restMana + 0.5f), 0, 100, 1,
            state && state->hasRestManaOverride);
        sendFloat("PartyMaxSpread", partyMaxSpread, 10.0f, 60.0f, 3,
            state && state->hasPartyMaxSpreadOverride);
        send("PullDynamicMaxLeeroyMobs", pullMax, 1, 20, 1,
            state && state->hasPullMaxOverride);

        SendToPlayer(recipient, "SYNCEND");
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
        else if (st.enabled && DcUtil::PartyNeedsRest(bot))
        {
            stateStr = "resting";
            detail = "Waiting for the party to recover.";
        }
        else if (st.enabled && DcUtil::PartyNeedsRegroup(bot))
        {
            stateStr = "regrouping";
            detail = "Waiting for the party to catch up.";
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
        Unit* sizingTarget = DcUtil::FindHostileNear(bot, sDcSettings.trashEngageRange + 8.0f);
        uint32 const nearbyHostiles = sizingTarget
            ? DcUtil::CountHostileNear(bot, sizingTarget, 15.0f) : 0;
        uint32 const liveDynamicVerdict = pull == static_cast<uint8>(DcPullMode::Dynamic)
            ? (nearbyHostiles <= DcUtil::EffectivePullMax(bot) ? 1u : 3u)
            : 0u;

        out << "STATUS\t"
            << (st.enabled ? "1" : "0") << "\t"
            << (next ? next->entry : 0) << "\t"
            << (next ? next->name : "None") << "\t"
            << stall << "\t"
            << skipped.size() << "\t"
            << stateStr << "\t"
            << detail << "\t"
            << uint32(TortoisePullToAddon(pull)) << "\t"
            << liveDynamicVerdict;
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

        BossListSnapshot const snapshot = BuildBossListSnapshot(ai, bot);
        // Explicit requests and command-driven changes prime the same cache
        // used by the periodic pusher, preventing a duplicate burst on the
        // next world tick.  Prime before the network calls so a re-entrant
        // command (or a concurrent tick) cannot overwrite a newer reset with
        // this already-built snapshot.
        if (!ClaimBossPush(bot->GetObjectGuid(), snapshot.fingerprint, true))
            return;
        SendBossListSnapshot(ai, bot, silent, snapshot);
        FinishBossPush(bot->GetObjectGuid(), snapshot.fingerprint);
    }

    void MarkActiveTank(ObjectGuid tank)
    {
        std::lock_guard<std::mutex> lock(g_activeMutex);
        g_activeTanks[tank] = PushState{}; // force status + boss emit on next tick
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
            BossListSnapshot const bossSnapshot = BuildBossListSnapshot(ai, bot);
            bool send = false;
            bool sendBoss = false;
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
                if (!it->second.bossPrimed || it->second.lastBoss != bossSnapshot.fingerprint)
                {
                    // Claim the fingerprint while no lock is held during the
                    // actual network send.  This avoids both packet spam and
                    // re-entrant lock acquisition from SendAddonMessage.
                    it->second.lastBoss = bossSnapshot.fingerprint;
                    it->second.bossPrimed = true;
                    it->second.bossPushInFlight = true;
                    sendBoss = true;
                }
            }
            if (send)
                SendToGroup(ai, bot, payload);
            if (sendBoss)
            {
                SendBossListSnapshot(ai, bot, true, bossSnapshot);
                FinishBossPush(guid, bossSnapshot.fingerprint);
            }
        }
    }
}
