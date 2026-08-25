#include "playerbot/playerbot.h"
#include "Util/DungeonClearUtil.h"
#include "DcValueKeys.h"
#include <sstream>

// Called from HostHooks / chat addon path when prefix matches.
bool DungeonClear_HandleAddonMessage(Player* player, std::string const& msg)
{
    // Expected: "DC\tON" / "DC\tOFF" / "DC\tPAUSE" / "DC\tSKIP" / "DC\tPULL" / "DC\tSTATUS"
    if (msg.rfind("DC\t", 0) != 0 && msg.rfind("DC:", 0) != 0)
        return false;
    if (!player)
        return false;

    std::string cmd = msg.size() > 3 ? msg.substr(3) : "";
    while (!cmd.empty() && (cmd[0] == '\t' || cmd[0] == ':' || cmd[0] == ' '))
        cmd.erase(0, 1);
    // uppercase fold
    for (char& c : cmd)
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';

    Player* tank = DcUtil::FindGroupTankBot(player);
    if (!tank) tank = player->GetPlayerbotAI() ? player : nullptr;
    if (!tank || !tank->GetPlayerbotAI())
        return true;

    PlayerbotAI* tai = tank->GetPlayerbotAI();
    Event ev("addon", "", player);
    auto run = [&](char const* a) {
        if (Action* act = tai->GetAiObjectContext()->GetAction(a))
            act->Execute(ev);
    };

    if (cmd == "ON") run("dc on");
    else if (cmd == "OFF") run("dc off");
    else if (cmd == "PAUSE") run("dc pause");
    else if (cmd == "SKIP") run("dc skip");
    else if (cmd == "PULL") run("dc pull");
    else if (cmd == "STATUS" || cmd == "BOSS") run(cmd == "BOSS" ? "dc bosses" : "dc status");
    return true;
}
