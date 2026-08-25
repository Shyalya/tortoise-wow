#include "Action/DungeonClearChatActions.h"
#include "playerbot/playerbot.h"
#include "Util/DungeonClearUtil.h"
#include "Settings/DcSettings.h"
#include "DcValueKeys.h"
#include "DcRunState.h"
#include "Data/DungeonBossInfo.h"
#include "Maps/Map.h"
#include <sstream>
#include <optional>
#include <unordered_set>
#include <algorithm>
#include <cctype>

using namespace ai;

namespace
{
    PlayerbotAI* TankAI(Player* bot)
    {
        Player* tank = DcUtil::FindGroupTankBot(bot);
        if (!tank)
            return nullptr;
        return tank->GetPlayerbotAI();
    }
}

bool DcOnAction::Execute(Event& event)
{
    Player* owner = event.getOwner();
    if (!DcUtil::IsRealCommander(owner, bot))
        return false;
    if (!bot->GetMap() || !bot->GetMap()->IsDungeon())
    {
        ai->TellError(owner, "Must be inside a dungeon to start dungeon clear.");
        return false;
    }
    Player* tank = DcUtil::FindGroupTankBot(bot);
    if (!tank || !tank->GetPlayerbotAI())
    {
        ai->TellError(owner, "Need a bot tank in the group.");
        return false;
    }
    PlayerbotAI* tai = tank->GetPlayerbotAI();
    DcRunState& st = tai->GetAiObjectContext()->GetValue<DcRunState&>(DcKey::RunState)->Get();
    st.Reset();
    st.enabled = true;
    uint8& pull = tai->GetAiObjectContext()->GetValue<uint8&>(DcKey::PullMode)->Get();
    pull = sDcSettings.defaultPullMode;
    DcUtil::TellGroup(tai, tank, "Dungeon clear ON — tank driving.");
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
    DcUtil::DisableDungeonClear(tank->GetPlayerbotAI(), tank, "commanded off");
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
    if (st.paused)
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
    ctx->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get().insert(next->entry);
    DcRunState& st = ctx->GetValue<DcRunState&>(DcKey::RunState)->Get();
    st.selectedBossEntry = 0;
    DcUtil::TellGroup(tank->GetPlayerbotAI(), tank, std::string("Skipped: ") + next->name);
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
    mode = (mode + 1) % 3;
    char const* names[] = {"Dynamic", "Leeroy", "Advanced"};
    DcUtil::TellGroup(tank->GetPlayerbotAI(), tank, std::string("Pull mode: ") + names[mode]);
    return true;
}

bool DcStatusAction::Execute(Event& event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;
    Player* tank = DcUtil::FindEnabledTank(bot);
    if (!tank) tank = DcUtil::FindGroupTankBot(bot);
    std::ostringstream out;
    if (!tank || !tank->GetPlayerbotAI())
    {
        out << "DC: no tank bot.";
        ai->TellPlayerNoFacing(owner, out.str());
        return true;
    }
    auto* ctx = tank->GetPlayerbotAI()->GetAiObjectContext();
    DcRunState const& st = ctx->GetValue<DcRunState&>(DcKey::RunState)->Get();
    uint8 mode = ctx->GetValue<uint8&>(DcKey::PullMode)->Get();
    char const* names[] = {"Dynamic", "Leeroy", "Advanced"};
    out << "DC: " << (st.enabled ? (st.paused ? "PAUSED" : "ON") : "OFF")
        << " pull=" << names[mode % 3];
    auto next = ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (next)
        out << " next=" << next->name;
    if (st.paused && !st.pauseReason.empty())
        out << " (" << st.pauseReason << ")";
    ai->TellPlayerNoFacing(owner, out.str());
    return true;
}

bool DcBossesAction::Execute(Event& event)
{
    if (!event.getOwner())
        return false;
    auto bosses = AI_VALUE(std::vector<DungeonBossInfo>, DcKey::DungeonBosses);
    auto const& skipped = AI_VALUE(std::unordered_set<uint32>&, DcKey::Skipped);
    std::ostringstream out;
    out << "Bosses (" << bosses.size() << "): ";
    for (size_t i = 0; i < bosses.size(); ++i)
    {
        if (i) out << ", ";
        out << bosses[i].name;
        if (skipped.count(bosses[i].entry))
            out << "[skip]";
        if (bosses[i].kind == DungeonAnchorKind::Objective)
            out << "[obj]";
    }
    ai->TellPlayerNoFacing(event.getOwner(), out.str());
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
    auto* ctx = tank->GetPlayerbotAI()->GetAiObjectContext();
    auto bosses = ctx->GetValue<std::vector<DungeonBossInfo>>(DcKey::DungeonBosses)->Get();
    for (DungeonBossInfo const& b : bosses)
    {
        std::string lower = b.name;
        std::string p = param;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        std::transform(p.begin(), p.end(), p.begin(), ::tolower);
        if (lower.find(p) != std::string::npos)
        {
            ctx->GetValue<DcRunState&>(DcKey::RunState)->Get().selectedBossEntry = b.entry;
            DcUtil::TellGroup(tank->GetPlayerbotAI(), tank, std::string("Routing to ") + b.name);
            return true;
        }
    }
    ai->TellError(owner, "Boss not found.");
    return false;
}
