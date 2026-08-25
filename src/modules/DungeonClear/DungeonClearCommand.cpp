#include "Chat/Chat.h"
#include "playerbot/playerbot.h"
#include "Util/DungeonClearUtil.h"
#include "Settings/DcSettings.h"
#include "TestRun/DcTestDriver.h"
#include "DcValueKeys.h"
#include <string>

bool ChatHandler_HandleDungeonClearCommand(ChatHandler* self, char* args)
{
    if (!self)
        return true;

    Player* player = self->GetSession() ? self->GetSession()->GetPlayer() : nullptr;
    if (!player)
    {
        self->SendSysMessage("Need an in-game session.");
        return true;
    }

    std::string arg = args ? args : "";
    while (!arg.empty() && arg[0] == ' ')
        arg.erase(0, 1);

    std::string sub = arg;
    std::string rest;
    auto sp = arg.find(' ');
    if (sp != std::string::npos)
    {
        sub = arg.substr(0, sp);
        rest = arg.substr(sp + 1);
    }

    Player* tank = DcUtil::FindGroupTankBot(player);
    if (!tank && player->GetPlayerbotAI())
        tank = player;
    if (!tank || !tank->GetPlayerbotAI())
    {
        self->PSendSysMessage("DungeonClear: no bot tank available. Invite a tank bot or enable self-bot.");
        return true;
    }

    PlayerbotAI* tai = tank->GetPlayerbotAI();
    Event ev(sub.empty() ? "dc status" : ("dc " + sub), rest, player);

    auto run = [&](char const* action)
    {
        if (Action* a = tai->GetAiObjectContext()->GetAction(action))
            a->Execute(ev);
    };

    if (sub.empty() || sub == "status")
        run("dc status");
    else if (sub == "on")
        run("dc on");
    else if (sub == "off")
        run("dc off");
    else if (sub == "pause")
        run("dc pause");
    else if (sub == "skip")
        run("dc skip");
    else if (sub == "pull")
        run("dc pull");
    else if (sub == "bosses")
        run("dc bosses");
    else if (sub == "go")
    {
        Event goev("dc go", rest, player);
        if (Action* a = tai->GetAiObjectContext()->GetAction("dc go"))
            a->Execute(goev);
    }
    else if (sub == "config")
    {
        self->PSendSysMessage("DungeonClear.Enabled=%u EngageRange=%.1f PullMode=%u LootQualityMin=%u RestHealth=%.0f RestMana=%.0f",
            sDcSettings.moduleEnabled ? 1 : 0, sDcSettings.engageRange, sDcSettings.defaultPullMode,
            sDcSettings.lootQualityMin, sDcSettings.restHealth, sDcSettings.restMana);
    }
    else if (sub == "test")
    {
        if (!self->GetSession() || self->GetSession()->GetSecurity() < SEC_MODERATOR)
        {
            self->SendSysMessage("GM only.");
            return true;
        }
        return DcTestDriver::Handle(self, rest);
    }
    else
    {
        self->SendSysMessage("Usage: .dc on|off|pause|skip|pull|status|bosses|go <boss>|config|test");
    }
    return true;
}
