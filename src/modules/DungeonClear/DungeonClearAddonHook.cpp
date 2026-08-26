#include "playerbot/playerbot.h"
#include "Util/DungeonClearUtil.h"
#include "Util/DcAddonComm.h"
#include "DcValueKeys.h"
#include <cctype>

// Called from HostHooks when LANG_ADDON / CHAT_MSG_ADDON arrives.
// Accepts both the companion protocol ("DC\tCMD\t<sub>[\t<param>]") and the
// legacy stub form ("DC\tON" / "DC:ON").
bool DungeonClear_HandleAddonMessage(Player* player, std::string const& msg)
{
    if (!player)
        return false;

    if (msg.rfind("DC\t", 0) != 0 && msg.rfind("DC:", 0) != 0)
        return false;

    std::string body = msg.size() > 3 ? msg.substr(3) : "";
    while (!body.empty() && (body[0] == '\t' || body[0] == ':' || body[0] == ' '))
        body.erase(0, 1);

    std::string subCmd;
    std::string param;

    if (body.rfind("CMD\t", 0) == 0)
    {
        std::string rest = body.substr(4);
        auto tab = rest.find('\t');
        if (tab == std::string::npos)
            subCmd = rest;
        else
        {
            subCmd = rest.substr(0, tab);
            param = rest.substr(tab + 1);
        }
    }
    else
    {
        auto tab = body.find('\t');
        if (tab == std::string::npos)
            subCmd = body;
        else
        {
            subCmd = body.substr(0, tab);
            param = body.substr(tab + 1);
        }
    }

    for (char& c : subCmd)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');

    if (subCmd.empty())
        return true;

    // Settings / spectate are not implemented on Tortoise yet — acknowledge so
    // the panel doesn't hang, and grey out spectate.
    if (subCmd == "sync")
    {
        DcAddonComm::SendToPlayer(player, "SYNCSTART");
        DcAddonComm::SendToPlayer(player, "SYNCEND");
        return true;
    }
    if (subCmd == "set" || subCmd == "reset")
    {
        DcAddonComm::SendError(player, "Per-run settings overrides are not available on this server yet.");
        return true;
    }
    if (subCmd == "spectate")
    {
        DcAddonComm::SendToPlayer(player, "SPECTATE\t0");
        DcAddonComm::SendError(player, "Spectator camera is not available on this server yet.");
        return true;
    }
    if (subCmd == "status")
        DcAddonComm::SendToPlayer(player, "SPECTATE\t0");

    Player* tank = DcUtil::FindGroupTankBot(player);
    if (!tank)
        tank = player->GetPlayerbotAI() ? player : nullptr;
    if (!tank || !tank->GetPlayerbotAI())
    {
        DcAddonComm::SendError(player, "No tank bot found in your group.");
        return true;
    }

    PlayerbotAI* tai = tank->GetPlayerbotAI();

    std::string eventParam = param;
    if (subCmd == "status" || subCmd == "bosses" || subCmd == "boss")
        eventParam = "addon";

    Event ev("addon", eventParam, player);

    auto run = [&](char const* actionName) {
        if (Action* act = tai->GetAiObjectContext()->GetAction(actionName))
            act->Execute(ev);
    };

    if (subCmd == "on")
        run("dc on");
    else if (subCmd == "off")
        run("dc off");
    else if (subCmd == "pause")
        run("dc pause");
    else if (subCmd == "skip")
        run("dc skip");
    else if (subCmd == "pull")
        run("dc pull");
    else if (subCmd == "status")
        run("dc status");
    else if (subCmd == "bosses" || subCmd == "boss")
        run("dc bosses");
    else if (subCmd == "go")
        run("dc go");

    return true;
}
