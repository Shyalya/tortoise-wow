#include "TestRun/DcTestDriver.h"
#include "Chat/Chat.h"
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "Util/DungeonClearUtil.h"
#include "DcValueKeys.h"
#include "Maps/MapManager.h"
#include "Group/Group.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{
    struct Entrance { uint32 mapId; float x, y, z; char const* name; };
    Entrance const kEntrances[] = {
        {36, -16.0f, -385.0f, 62.0f, "Deadmines"},
        {33, -228.0f, 2111.0f, 76.9f, "Shadowfang Keep"},
        {43, -150.0f, 130.0f, -74.0f, "Wailing Caverns"},
        {48, -150.234f, 106.594f, -39.779f, "Blackfathom Deeps"},
        {90, -329.098f, -3.20722f, -152.851f, "Gnomeregan"},
        {189, 1688.0f, 1050.0f, 18.0f, "Scarlet Monastery"},
        {209, 1210.0f, 840.0f, 9.0f, "Zul'Farrak"},
        {34, 54.0f, 0.0f, -25.0f, "Stockades"},
        {47, 1942.27f, 1544.23f, 83.3055f, "Razorfen Kraul"},
        {389, 2.0f, -10.0f, -50.0f, "Ragefire Chasm"},
        {70, -230.0f, 280.0f, -45.0f, "Uldaman"},
        {109, -600.0f, 100.0f, -90.0f, "Sunken Temple"},
        {129, 2480.0f, 920.0f, 30.0f, "Razorfen Downs"},
        {230, 400.0f, -150.0f, -70.0f, "Blackrock Depths"},
        {229, -22.6518f, -299.750f, 31.7016f, "Blackrock Spire (Lower)"},
        {229, 144.438f, -258.034f, 96.4066f, "Blackrock Spire (Upper)"},
        {229, 78.3534f, -226.841f, 49.7662f, "Blackrock Spire"},
        {289, 100.0f, 100.0f, 100.0f, "Scholomance"},
        {329, 3500.0f, -3400.0f, 138.0f, "Stratholme"},
        {429, -50.0f, -700.0f, -2.0f, "Dire Maul"},
        {349, 700.0f, -50.0f, -60.0f, "Maraudon"},
        {249, 30.8916f, -54.079f, -5.02784f, "Onyxia's Lair"},
        {309, -11916.6f, -1243.52f, 92.5338f, "Zul'Gurub"},
        {409, 1091.89f, -466.985f, -105.084f, "Molten Core"},
        {469, -7672.32f, -1107.05f, 396.651f, "Blackwing Lair"},
        {509, -8436.53f, 1519.17f, 31.907f, "Ruins of Ahn'Qiraj"},
        {531, -8221.35f, 2014.34f, 129.071f, "Temple of Ahn'Qiraj"},
        {533, 3005.87f, -3435.01f, 293.882f, "Naxxramas"},
    };

    std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    std::string Trim(std::string value)
    {
        size_t const first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        size_t const last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    Entrance const* FindEntrance(std::string const& name)
    {
        std::string const query = Lower(Trim(name));
        if (query.empty())
            return nullptr;

        Entrance const* best = nullptr;
        size_t bestLength = 0;
        for (auto const& e : kEntrances)
        {
            std::string const candidate = Lower(e.name);
            if (candidate == query)
                return &e;
            if (candidate.find(query) != std::string::npos && query.size() > bestLength)
            {
                best = &e;
                bestLength = query.size();
            }
        }
        return best;
    }
}

bool DcTestDriver::Handle(ChatHandler* handler, std::string const& args)
{
    Player* gm = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!gm)
        return true;

    std::string dungeon = Trim(args.empty() ? "deadmines" : args);

    Entrance const* ent = FindEntrance(dungeon);
    if (!ent)
    {
        handler->PSendSysMessage("DC test: unknown dungeon '%s'.", dungeon.c_str());
        return true;
    }
    handler->PSendSysMessage("DC test: dungeon=%s — teleporting the GM and grouped bots to the entrance.", ent->name);

    gm->TeleportTo(ent->mapId, ent->x, ent->y, ent->z, 0.0f);

    // This command is GM-only and is intended as a self-contained smoke test:
    // bring grouped playerbots to the same entrance before enabling the run.
    if (Player* tank = DcUtil::FindGroupTankBot(gm))
    {
        if (Group* group = gm->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->getSource();
                if (member && member != gm && member->GetPlayerbotAI())
                    member->TeleportTo(ent->mapId, ent->x, ent->y, ent->z, 0.0f);
            }
        }

        if (PlayerbotAI* tai = tank->GetPlayerbotAI())
        {
            Event ev("dc on", "", gm);
            if (Action* a = tai->GetAiObjectContext()->GetAction("dc on"))
            {
                if (a->Execute(ev))
                    handler->PSendSysMessage("DC test: enabled on tank %s.", tank->GetName());
                else
                    handler->PSendSysMessage("DC test: could not enable on tank %s; use `.dc status` for details.", tank->GetName());
            }
        }
    }
    else
        handler->SendSysMessage("DC test: no tank bot in group — invite a tank bot and run `.dc on`.");

    return true;
}
