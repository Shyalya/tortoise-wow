#include "TestRun/DcTestDriver.h"
#include "Chat/Chat.h"
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "Util/DungeonClearUtil.h"
#include "DcValueKeys.h"
#include "Maps/MapManager.h"
#include <cstdlib>
#include <sstream>

namespace
{
    struct Entrance { uint32 mapId; float x, y, z; char const* name; };
    Entrance const kEntrances[] = {
        {36, -16.0f, -385.0f, 62.0f, "Deadmines"},
        {33, -228.0f, 2111.0f, 76.9f, "Shadowfang Keep"},
        {43, -150.0f, 130.0f, -74.0f, "Wailing Caverns"},
        {189, 1688.0f, 1050.0f, 18.0f, "Scarlet Monastery"},
        {209, 1210.0f, 840.0f, 9.0f, "Zul'Farrak"},
        {34, 54.0f, 0.0f, -25.0f, "Stockades"},
        {389, 2.0f, -10.0f, -50.0f, "Ragefire Chasm"},
        {70, -230.0f, 280.0f, -45.0f, "Uldaman"},
        {109, -600.0f, 100.0f, -90.0f, "Sunken Temple"},
        {129, 2480.0f, 920.0f, 30.0f, "Razorfen Downs"},
        {230, 400.0f, -150.0f, -70.0f, "Blackrock Depths"},
        {289, 100.0f, 100.0f, 100.0f, "Scholomance"},
        {329, 3500.0f, -3400.0f, 138.0f, "Stratholme"},
        {429, -50.0f, -700.0f, -2.0f, "Dire Maul"},
        {349, 700.0f, -50.0f, -60.0f, "Maraudon"},
    };

    Entrance const* FindEntrance(std::string const& name)
    {
        for (auto const& e : kEntrances)
        {
            std::string n = e.name;
            std::string q = name;
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            std::transform(q.begin(), q.end(), q.begin(), ::tolower);
            if (n.find(q) != std::string::npos)
                return &e;
        }
        return &kEntrances[0];
    }
}

bool DcTestDriver::Handle(ChatHandler* handler, std::string const& args)
{
    Player* gm = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!gm)
        return true;

    std::string dungeon = args.empty() ? "deadmines" : args;
    uint32 seed = static_cast<uint32>(time(nullptr));
    auto sp = dungeon.find(' ');
    if (sp != std::string::npos)
    {
        // "deadmines 12345" seed form
        try { seed = static_cast<uint32>(std::stoul(dungeon.substr(sp + 1))); } catch (...) {}
        dungeon = dungeon.substr(0, sp);
    }

    Entrance const* ent = FindEntrance(dungeon);
    handler->PSendSysMessage("DC test: dungeon=%s seed=%u — teleporting you to entrance. Invite tank/healer/dps bots and `.dc on`.",
        ent->name, seed);

    gm->TeleportTo(ent->mapId, ent->x, ent->y, ent->z, 0.0f);

    // Optional: if GM has bots in group, enable DC on the tank.
    if (Player* tank = DcUtil::FindGroupTankBot(gm))
    {
        if (PlayerbotAI* tai = tank->GetPlayerbotAI())
        {
            Event ev("dc on", "", gm);
            if (Action* a = tai->GetAiObjectContext()->GetAction("dc on"))
                a->Execute(ev);
            handler->PSendSysMessage("DC test: auto-enabled on tank %s", tank->GetName());
        }
    }
    else
        handler->SendSysMessage("DC test: no tank bot in group yet — invite bots then `.dc on`.");

    return true;
}
