#include "Action/DungeonClearChatActions.h"
#include "playerbot/playerbot.h"
#include "Util/DungeonClearUtil.h"
#include "Util/DcAddonComm.h"
#include "Settings/DcSettings.h"
#include "DcValueKeys.h"
#include "DcRunState.h"
#include "Data/DungeonBossInfo.h"
#include "Data/DungeonWingRegistry.h"
#include "Maps/Map.h"
#include <sstream>
#include <optional>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <cstdlib>

using namespace ai;

namespace
{
    bool IsSilent(Event& event)
    {
        std::string const p = event.getParam();
        return p == "addon" || p == "silent";
    }
}

bool DcOnAction::Execute(Event& event)
{
    Player* owner = event.getOwner();
    if (!DcUtil::IsRealCommander(owner, bot))
        return false;
    if (!bot->GetMap() || (!bot->GetMap()->IsDungeon() && !bot->GetMap()->IsRaid()))
    {
        if (owner)
            DcAddonComm::SendError(owner, "Must be inside a dungeon or raid to start dungeon clear.");
        ai->TellError(owner, "Must be inside a dungeon or raid to start dungeon clear.");
        return false;
    }
    Player* tank = DcUtil::FindGroupTankBot(bot);
    if (!tank || !tank->GetPlayerbotAI())
    {
        if (owner)
            DcAddonComm::SendError(owner, "Need a bot tank in the group.");
        ai->TellError(owner, "Need a bot tank in the group.");
        return false;
    }
    PlayerbotAI* tai = tank->GetPlayerbotAI();
    DcUtil::ResetDungeonClearRun(tai, tank);
    DcRunState& st = tai->GetAiObjectContext()->GetValue<DcRunState&>(DcKey::RunState)->Get();
    st.enabled = true;
    // Pull mode is preserved across runs (addon / `.dc pull` can set it before On).
    DcUtil::TellGroup(tai, tank, "Dungeon clear ON — tank driving.");
    DcAddonComm::MarkActiveTank(tank->GetObjectGuid());
    DcAddonComm::PushStatus(tai, tank);
    DcAddonComm::PushBossList(tai, tank, true);
    return true;
}

bool DcOffAction::Execute(Event& event)
{
    Player* owner = event.getOwner();
    if (!DcUtil::IsRealCommander(owner, bot))
        return false;
    Player* tank = DcUtil::FindEnabledTank(bot);
    if (!tank) tank = DcUtil::FindGroupTankBot(bot);
    if (!tank || !tank->GetPlayerbotAI())
        return false;
    ObjectGuid tankGuid = tank->GetObjectGuid();
    DcUtil::DisableDungeonClear(tank->GetPlayerbotAI(), tank, "commanded off");
    DcAddonComm::UnmarkActiveTank(tankGuid);
    DcAddonComm::PushStatus(tank->GetPlayerbotAI(), tank);
    return true;
}

bool DcPauseAction::Execute(Event& event)
{
    Player* owner = event.getOwner();
    if (!DcUtil::IsRealCommander(owner, bot))
        return false;
    Player* tank = DcUtil::FindEnabledTank(bot);
    if (!tank || !tank->GetPlayerbotAI())
        return false;
    DcRunState& st = tank->GetPlayerbotAI()->GetAiObjectContext()->GetValue<DcRunState&>(DcKey::RunState)->Get();
    if (!st.enabled)
        return false;

    std::string param = event.getParam();
    for (char& c : param)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');

    bool wantResume = (param == "resume");
    bool wantPause = (param == "pause");
    if (wantResume || (st.paused && !wantPause))
    {
        st.OnResume();
        DcUtil::TellGroup(tank->GetPlayerbotAI(), tank, "Dungeon clear resumed.");
    }
    else
    {
        st.paused = true;
        st.pauseReason = "manual pause";
        DcUtil::TellGroup(tank->GetPlayerbotAI(), tank, "Dungeon clear paused.");
    }
    DcAddonComm::PushStatus(tank->GetPlayerbotAI(), tank);
    return true;
}

bool DcSkipAction::Execute(Event& event)
{
    Player* owner = event.getOwner();
    if (!DcUtil::IsRealCommander(owner, bot))
        return false;
    Player* tank = DcUtil::FindEnabledTank(bot);
    if (!tank || !tank->GetPlayerbotAI())
        return false;
    auto* ctx = tank->GetPlayerbotAI()->GetAiObjectContext();
    auto next = ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next)
        return false;
    uint32 const skipKey = DungeonBossStateKey(*next);
    ctx->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get().insert(skipKey);
    DcRunState& st = ctx->GetValue<DcRunState&>(DcKey::RunState)->Get();
    st.selectedBossEntry = 0;
    st.selectedBossStateKey = 0;
    // A manual skip must invalidate an in-flight event as well.  Otherwise a
    // conditional event can remain "active" and be rediscovered after its
    // objective was skipped.
    st.eventId = 0;
    st.eventInstanceId = 0;
    st.eventLastDriveAt = 0;
    st.eventMaxStep = 0;
    st.eventProgressAt = 0;
    ctx->GetValue<uint32&>(DcKey::EventProgress)->Get() = 0;
    ctx->GetValue<uint32&>(DcKey::EventStartedAt)->Get() = 0;
    ctx->GetValue<uint32&>(DcKey::EventStepStartedAt)->Get() = 0;
    DcUtil::TellGroup(tank->GetPlayerbotAI(), tank, std::string("Skipped: ") + next->name);
    DcAddonComm::PushStatus(tank->GetPlayerbotAI(), tank);
    DcAddonComm::PushBossList(tank->GetPlayerbotAI(), tank, true);
    return true;
}

bool DcPullModeAction::Execute(Event& event)
{
    Player* owner = event.getOwner();
    if (!DcUtil::IsRealCommander(owner, bot))
        return false;
    Player* tank = DcUtil::FindGroupTankBot(bot);
    if (!tank || !tank->GetPlayerbotAI())
        return false;
    uint8& mode = tank->GetPlayerbotAI()->GetAiObjectContext()->GetValue<uint8&>(DcKey::PullMode)->Get();
    mode = DcAddonComm::AddonPullKeywordToTortoise(event.getParam(), mode);
    char const* names[] = {"Dynamic", "Leeroy", "Advanced"};
    DcUtil::TellGroup(tank->GetPlayerbotAI(), tank, std::string("Pull mode: ") + names[mode % 3]);
    DcAddonComm::PushStatus(tank->GetPlayerbotAI(), tank);
    return true;
}

bool DcStatusAction::Execute(Event& event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;
    Player* tank = DcUtil::FindEnabledTank(bot);
    if (!tank) tank = DcUtil::FindGroupTankBot(bot);
    if (!tank || !tank->GetPlayerbotAI())
    {
        if (!IsSilent(event))
            ai->TellPlayerNoFacing(owner, "DC: no tank bot.");
        DcAddonComm::SendError(owner, "No tank bot found in your group.");
        return true;
    }

    PlayerbotAI* tai = tank->GetPlayerbotAI();
    DcAddonComm::PushStatus(tai, tank);

    if (!IsSilent(event))
    {
        auto* ctx = tai->GetAiObjectContext();
        DcRunState const& st = ctx->GetValue<DcRunState&>(DcKey::RunState)->Get();
        uint8 mode = ctx->GetValue<uint8&>(DcKey::PullMode)->Get();
        char const* names[] = {"Dynamic", "Leeroy", "Advanced"};
        std::ostringstream out;
        out << "DC: " << (st.enabled ? (st.paused ? "PAUSED" : "ON") : "OFF")
            << " pull=" << names[mode % 3];
        auto next = ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
        if (next)
            out << " next=" << next->name;
        if (st.paused && !st.pauseReason.empty())
            out << " (" << st.pauseReason << ")";
        ai->TellPlayerNoFacing(owner, out.str());
    }
    return true;
}

bool DcBossesAction::Execute(Event& event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    Player* tank = DcUtil::FindEnabledTank(bot);
    if (!tank) tank = DcUtil::FindGroupTankBot(bot);
    if (!tank) tank = bot;
    PlayerbotAI* tai = tank->GetPlayerbotAI() ? tank->GetPlayerbotAI() : ai;

    bool silent = IsSilent(event);
    DcAddonComm::PushBossList(tai, tank, silent);

    if (!silent)
    {
        auto bosses = tai->GetAiObjectContext()->GetValue<std::vector<DungeonBossInfo>>(DcKey::DungeonBosses)->Get();
        auto const& skipped = tai->GetAiObjectContext()->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get();
        std::ostringstream out;
        out << "Bosses (" << bosses.size() << "): ";
        for (size_t i = 0; i < bosses.size(); ++i)
        {
            if (i) out << ", ";
            out << bosses[i].name;
            if (std::string wing = DungeonWingRegistry::WingName(tank->GetMapId(), bosses[i].entry); !wing.empty())
                out << "[" << wing << "]";
            uint32 const skipKey = DungeonBossStateKey(bosses[i]);
            if (skipped.count(skipKey))
                out << "[skip]";
            if (bosses[i].kind == DungeonAnchorKind::Objective)
                out << "[obj]";
        }
        ai->TellPlayerNoFacing(owner, out.str());
    }
    return true;
}

bool DcGoAction::Execute(Event& event)
{
    Player* owner = event.getOwner();
    if (!DcUtil::IsRealCommander(owner, bot))
        return false;
    Player* tank = DcUtil::FindEnabledTank(bot);
    if (!tank) tank = DcUtil::FindGroupTankBot(bot);
    if (!tank || !tank->GetPlayerbotAI())
        return false;
    std::string param = event.getParam();
    if (param.empty())
        return false;

    // Strip a trailing "addon" marker if a chat path ever appended one.
    if (size_t pos = param.find('\t'); pos != std::string::npos)
        param = param.substr(0, pos);

    auto* ctx = tank->GetPlayerbotAI()->GetAiObjectContext();
    auto bosses = ctx->GetValue<std::vector<DungeonBossInfo>>(DcKey::DungeonBosses)->Get();

    // Addon Go buttons send "entry:encounterIndex" so duplicate creature
    // entries (for example two different vanilla encounters using the same
    // template) remain individually selectable.
    std::string entryParam = param;
    std::string indexParam;
    if (size_t colon = param.find(':'); colon != std::string::npos)
    {
        entryParam = param.substr(0, colon);
        indexParam = param.substr(colon + 1);
    }

    char* end = nullptr;
    unsigned long entryNum = std::strtoul(entryParam.c_str(), &end, 10);
    bool numeric = end && end != entryParam.c_str() && *end == '\0';
    uint32 encounterIndex = 0;
    bool hasEncounterIndex = false;
    if (numeric && !indexParam.empty())
    {
        char* indexEnd = nullptr;
        unsigned long parsedIndex = std::strtoul(indexParam.c_str(), &indexEnd, 10);
        hasEncounterIndex = indexEnd && indexEnd != indexParam.c_str() && *indexEnd == '\0';
        if (hasEncounterIndex)
            encounterIndex = static_cast<uint32>(parsedIndex);
    }
    if (numeric && !indexParam.empty() && !hasEncounterIndex)
        numeric = false;

    for (DungeonBossInfo const& b : bosses)
    {
        bool match = false;
        if (numeric)
            match = b.entry == uint32(entryNum)
                && (!hasEncounterIndex || b.encounterIndex == encounterIndex);
        else
        {
            std::string lower = b.name;
            std::string p = param;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            std::transform(p.begin(), p.end(), p.begin(), ::tolower);
            match = lower.find(p) != std::string::npos;
        }
        if (!match)
            continue;

        DcRunState& st = ctx->GetValue<DcRunState&>(DcKey::RunState)->Get();
        if (!st.enabled)
        {
            st.enabled = true;
            DcAddonComm::MarkActiveTank(tank->GetObjectGuid());
        }
        st.selectedBossEntry = b.entry;
        st.selectedBossStateKey = DungeonBossStateKey(b);
        DcUtil::TellGroup(tank->GetPlayerbotAI(), tank, std::string("Routing to ") + b.name);
        DcAddonComm::PushStatus(tank->GetPlayerbotAI(), tank);
        return true;
    }
    if (owner)
        DcAddonComm::SendError(owner, "Boss not found.");
    ai->TellError(owner, "Boss not found.");
    return false;
}
