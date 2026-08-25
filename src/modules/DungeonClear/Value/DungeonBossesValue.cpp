#include "playerbot/playerbot.h"
#include "Value/DungeonBossesValue.h"
#include "Data/DungeonEventRegistry.h"
#include "Maps/GridSearchers.h"
#include "Maps/Map.h"
#include "Objects/Player.h"
#include "Objects/Creature.h"
#include <algorithm>
#include <sstream>

using namespace ai;

namespace
{
    DungeonBossInfo MakeBoss(uint32 entry, uint32 encounterIndex, uint32 mapId, char const* name,
        float x, float y, float z, float radius = 15.0f, uint32 eventId = 0)
    {
        DungeonBossInfo b;
        b.entry = entry;
        b.encounterIndex = encounterIndex;
        b.name = name;
        b.mapId = mapId;
        b.x = x; b.y = y; b.z = z;
        b.kind = DungeonAnchorKind::Boss;
        b.arriveRadius = radius;
        b.eventId = eventId;
        return b;
    }
}

std::vector<DungeonBossInfo> ai::GetHardcodedBossTable(uint32 mapId)
{
    std::vector<DungeonBossInfo> v;

    // Boss encounterIndex values are spaced by 10 so BossRosterPatch
    // objectives (registered in Data/Events/*.cpp) can slot in between them.
    switch (mapId)
    {
        case 36: // Deadmines
            v.push_back(MakeBoss(644,  10, mapId, "Rhahk'Zor",             -158.0f, -654.0f, 20.0f));
            v.push_back(MakeBoss(642,  20, mapId, "Sneed",                 -184.0f, -600.0f, 21.0f));
            v.push_back(MakeBoss(1763, 30, mapId, "Gilnid",                -224.0f, -580.0f, 6.0f));
            v.push_back(MakeBoss(646,  40, mapId, "Mr. Smite",             -191.0f, -540.0f, 15.0f));
            v.push_back(MakeBoss(647,  50, mapId, "Captain Greenskin",     -152.0f, -487.0f, 20.0f));
            v.push_back(MakeBoss(639,  60, mapId, "Edwin VanCleef",        -101.0f, -523.0f, 40.0f));
            break;

        case 33: // Shadowfang Keep
            v.push_back(MakeBoss(3914, 10, mapId, "Rethilgore",            -122.0f, 2213.0f, 92.0f));
            v.push_back(MakeBoss(3887, 20, mapId, "Baron Silverlaine",     -155.0f, 2222.0f, 92.0f));
            v.push_back(MakeBoss(3886, 30, mapId, "Odo the Blindwatcher",  -190.0f, 2202.0f, 111.0f));
            v.push_back(MakeBoss(3927, 40, mapId, "Fenrus the Devourer",   -210.0f, 2168.0f, 108.0f));
            v.push_back(MakeBoss(4278, 50, mapId, "Wolf Master Nandos",    -218.0f, 2145.0f, 111.0f));
            v.push_back(MakeBoss(4275, 60, mapId, "Archmage Arugal",       -170.0f, 2110.0f, 128.0f));
            break;

        case 43: // Wailing Caverns
            v.push_back(MakeBoss(3671, 10, mapId, "Lord Cobrahn",          -101.0f, 720.0f, -50.0f));
            v.push_back(MakeBoss(3670, 20, mapId, "Lady Anacondra",        -138.0f, 745.0f, -55.0f));
            v.push_back(MakeBoss(5720, 30, mapId, "Lord Pythas",           -160.0f, 800.0f, -58.0f));
            v.push_back(MakeBoss(3673, 40, mapId, "Lord Serpentis",        -190.0f, 850.0f, -60.0f));
            v.push_back(MakeBoss(3669, 50, mapId, "Skum",                  -230.0f, 900.0f, -63.0f));
            v.push_back(MakeBoss(5721, 60, mapId, "Verdan the Everliving", -250.0f, 930.0f, -65.0f));
            v.push_back(MakeBoss(3654, 70, mapId, "Mutanus the Devourer",  -270.0f, 970.0f, -70.0f));
            break;

        case 189: // Scarlet Monastery (Graveyard/Library/Armory/Cathedral share this mapId)
            v.push_back(MakeBoss(3983, 10, mapId, "Interrogator Vishas",   1815.0f, 60.0f, 30.0f));
            v.push_back(MakeBoss(4543, 20, mapId, "Bloodmage Thalnos",     1790.0f, 40.0f, 30.0f));
            v.push_back(MakeBoss(3974, 30, mapId, "Houndmaster Loksey",    1900.0f, 220.0f, 30.0f));
            v.push_back(MakeBoss(6487, 40, mapId, "Arcanist Doan",         1950.0f, 260.0f, 30.0f));
            v.push_back(MakeBoss(3975, 50, mapId, "Herod",                 2020.0f, -120.0f, 30.0f));
            v.push_back(MakeBoss(3976, 60, mapId, "Scarlet Commander Mograine", 2100.0f, 320.0f, 35.0f));
            v.push_back(MakeBoss(3977, 70, mapId, "High Inquisitor Whitemane", 2140.0f, 360.0f, 35.0f));
            break;

        case 209: // Zul'Farrak
            v.push_back(MakeBoss(7271, 10, mapId, "Antu'sul",              1214.0f, 949.0f, -49.0f));
            v.push_back(MakeBoss(8127, 20, mapId, "Theka the Martyr",      1180.0f, 900.0f, -49.0f));
            v.push_back(MakeBoss(7796, 30, mapId, "Witch Doctor Zum'rah",  1140.0f, 860.0f, -49.0f));
            v.push_back(MakeBoss(7267, 40, mapId, "Nekrum Gutchewer",      1100.0f, 820.0f, -49.0f));
            v.push_back(MakeBoss(8128, 50, mapId, "Sandarr Dunereaver",    1060.0f, 780.0f, -49.0f));
            v.push_back(MakeBoss(7272, 60, mapId, "Chief Ukorz Sandscalp", 1020.0f, 740.0f, -49.0f));
            break;

        case 34: // The Stockade
            v.push_back(MakeBoss(1716, 10, mapId, "Dextren Ward",         -25.0f, 90.0f, -60.0f));
            v.push_back(MakeBoss(1663, 20, mapId, "Kam Deepfury",          10.0f, 60.0f, -60.0f));
            v.push_back(MakeBoss(1666, 30, mapId, "Bazil Thredd",           0.0f, 110.0f, -60.0f));
            v.push_back(MakeBoss(1717, 40, mapId, "Hamhock",                40.0f, 40.0f, -60.0f));
            v.push_back(MakeBoss(1696, 50, mapId, "Targorr the Dread",      60.0f, 20.0f, -60.0f));
            break;

        case 47: // Razorfen Kraul
            v.push_back(MakeBoss(4424, 10, mapId, "Aggem Thorncurse",     -108.0f, -285.0f, -50.0f));
            v.push_back(MakeBoss(4428, 20, mapId, "Roogug",               -160.0f, -320.0f, -50.0f));
            v.push_back(MakeBoss(4422, 30, mapId, "Overlord Ramtusk",     -220.0f, -360.0f, -50.0f));
            v.push_back(MakeBoss(4421, 40, mapId, "Death Speaker Jargba", -260.0f, -400.0f, -50.0f));
            v.push_back(MakeBoss(4420, 50, mapId, "Charlga Razorflank",   -300.0f, -440.0f, -50.0f));
            break;

        case 129: // Razorfen Downs
            v.push_back(MakeBoss(7355, 10, mapId, "Tuten'kash",              1240.0f, 1235.0f, 70.0f));
            v.push_back(MakeBoss(7357, 20, mapId, "Mordresh Fire Eye",       1200.0f, 1180.0f, 70.0f));
            v.push_back(MakeBoss(7796, 30, mapId, "Glutton",                 1180.0f, 1130.0f, 70.0f));
            v.push_back(MakeBoss(7358, 40, mapId, "Ragglesnout",             1150.0f, 1090.0f, 70.0f));
            v.push_back(MakeBoss(7358, 50, mapId, "Plaguemaw the Rotting",   1120.0f, 1050.0f, 70.0f));
            v.push_back(MakeBoss(7356, 60, mapId, "Amnennar the Coldbringer",1080.0f, 1000.0f, 70.0f));
            break;

        case 70: // Uldaman
            v.push_back(MakeBoss(6910, 10, mapId, "Revelosh",             -6110.0f, -251.0f, -190.0f));
            v.push_back(MakeBoss(4854, 20, mapId, "Obsidian Sentinel",    -6050.0f, -200.0f, -195.0f));
            v.push_back(MakeBoss(2748, 30, mapId, "Ironaya",              -6000.0f, -150.0f, -200.0f));
            v.push_back(MakeBoss(7228, 40, mapId, "Grimlok",              -5950.0f, -100.0f, -205.0f));
            v.push_back(MakeBoss(4854, 50, mapId, "Archaedas",            -5890.0f, -60.0f, -210.0f));
            break;

        case 90: // Gnomeregan
            v.push_back(MakeBoss(6235, 10, mapId, "Grubbis",                -285.0f, 66.0f, 296.0f));
            v.push_back(MakeBoss(6229, 20, mapId, "Viscous Fallout",        -230.0f, 100.0f, 296.0f));
            v.push_back(MakeBoss(6228, 30, mapId, "Electrocutioner 6000",   -180.0f, 140.0f, 296.0f));
            v.push_back(MakeBoss(7361, 40, mapId, "Crowd Pummeler 9-60",    -130.0f, 180.0f, 296.0f));
            v.push_back(MakeBoss(6235, 50, mapId, "Mekgineer Thermaplugg",   -80.0f, 220.0f, 296.0f));
            break;

        case 109: // Sunken Temple (Atal'Hakkar)
            v.push_back(MakeBoss(5710, 10, mapId, "Jammal'an the Prophet",   -453.0f, 79.0f, -57.0f));
            v.push_back(MakeBoss(8586, 20, mapId, "Ogom the Wretched",       -420.0f, 40.0f, -60.0f));
            v.push_back(MakeBoss(5711, 30, mapId, "Morphaz",                 -400.0f, 0.0f, -63.0f));
            v.push_back(MakeBoss(5719, 40, mapId, "Hukku",                   -380.0f, -40.0f, -66.0f));
            v.push_back(MakeBoss(8580, 50, mapId, "Shirrak the Dead Watcher",-360.0f, -80.0f, -69.0f));
            v.push_back(MakeBoss(5717, 60, mapId, "Atal'alarion",            -340.0f, -120.0f, -72.0f));
            break;

        case 230: // Blackrock Depths
            v.push_back(MakeBoss(9019, 10, mapId, "Bael'Gar",                    600.0f, -300.0f, -130.0f));
            v.push_back(MakeBoss(8983, 20, mapId, "Houndmaster Grebmar",         650.0f, -260.0f, -130.0f));
            v.push_back(MakeBoss(9018, 30, mapId, "High Interrogator Gerstahn",  700.0f, -220.0f, -130.0f));
            v.push_back(MakeBoss(8983, 40, mapId, "Lord Roccor",                 750.0f, -180.0f, -130.0f));
            v.push_back(MakeBoss(9024, 50, mapId, "Fineous Darkvire",            800.0f, -140.0f, -130.0f));
            v.push_back(MakeBoss(9025, 60, mapId, "Ambassador Flamelash",        850.0f, -100.0f, -130.0f));
            v.push_back(MakeBoss(9033, 70, mapId, "General Angerforge",          900.0f, -60.0f, -130.0f));
            v.push_back(MakeBoss(8929, 80, mapId, "Emperor Dagran Thaurissan",   950.0f, -20.0f, -130.0f));
            break;

        case 289: // Scholomance
            v.push_back(MakeBoss(10437, 10, mapId, "Kirtonos the Herald",       180.0f, -20.0f, 40.0f));
            v.push_back(MakeBoss(1853,  20, mapId, "Jandice Barov",             150.0f, -60.0f, 40.0f));
            v.push_back(MakeBoss(10508, 30, mapId, "Rattlegore",                120.0f, -100.0f, 40.0f));
            v.push_back(MakeBoss(1856,  40, mapId, "Ras Frostwhisper",           90.0f, -140.0f, 40.0f));
            v.push_back(MakeBoss(1860,  50, mapId, "Lorekeeper Polkelt",         60.0f, -180.0f, 40.0f));
            v.push_back(MakeBoss(10503, 60, mapId, "The Ravenian",               30.0f, -220.0f, 40.0f));
            v.push_back(MakeBoss(1854,  70, mapId, "Instructor Malicia",          0.0f, -260.0f, 40.0f));
            v.push_back(MakeBoss(1858,  80, mapId, "Doctor Theolen Krastinov",  -30.0f, -300.0f, 40.0f));
            v.push_back(MakeBoss(10901, 90, mapId, "Lord Alexei Barov",         -60.0f, -340.0f, 40.0f));
            v.push_back(MakeBoss(1852, 100, mapId, "Darkmaster Gandling",       -90.0f, -380.0f, 40.0f));
            break;

        case 329: // Stratholme
            v.push_back(MakeBoss(10516, 10, mapId, "Cannon Master Willey",     3670.0f, -3400.0f, 130.0f));
            v.push_back(MakeBoss(10516, 20, mapId, "Postmaster Malown",        3620.0f, -3350.0f, 130.0f));
            v.push_back(MakeBoss(10812, 30, mapId, "Timmy the Cruel",          3570.0f, -3300.0f, 130.0f));
            v.push_back(MakeBoss(11032, 40, mapId, "Hearthsinger Forresten",   3520.0f, -3250.0f, 130.0f));
            v.push_back(MakeBoss(10440, 50, mapId, "Baroness Anastari",        3470.0f, -3200.0f, 130.0f));
            v.push_back(MakeBoss(10435, 60, mapId, "Nerub'enkan",              3420.0f, -3150.0f, 130.0f));
            v.push_back(MakeBoss(10436, 70, mapId, "Maleki the Pallid",        3370.0f, -3100.0f, 130.0f));
            v.push_back(MakeBoss(10438, 80, mapId, "Ramstein the Gorger",      3320.0f, -3050.0f, 130.0f));
            v.push_back(MakeBoss(10439, 90, mapId, "Baron Rivendare",          3270.0f, -3000.0f, 130.0f));
            break;

        case 429: // Dire Maul (East/West/North share this mapId)
            v.push_back(MakeBoss(11490, 10, mapId, "Zevrim Thornhoof",     -40.0f, -20.0f, 15.0f));
            v.push_back(MakeBoss(11492, 20, mapId, "Hydrospawn",           -80.0f, -60.0f, 15.0f));
            v.push_back(MakeBoss(11491, 30, mapId, "Pusillin",            -120.0f, -100.0f, 15.0f));
            v.push_back(MakeBoss(14326, 40, mapId, "Tendris Warpwood",    -160.0f, -140.0f, 15.0f));
            v.push_back(MakeBoss(11501, 50, mapId, "Illyanna Ravenoak",   -200.0f, -180.0f, 15.0f));
            v.push_back(MakeBoss(11488, 60, mapId, "Alzzin the Wildshaper",-240.0f, -220.0f, 15.0f));
            v.push_back(MakeBoss(11517, 70, mapId, "Guard Mol'dar",       -280.0f, -260.0f, 15.0f));
            v.push_back(MakeBoss(11518, 80, mapId, "Stomper Kreeg",       -320.0f, -300.0f, 15.0f));
            v.push_back(MakeBoss(11519, 90, mapId, "Captain Kromcrush",   -360.0f, -340.0f, 15.0f));
            v.push_back(MakeBoss(11520, 100, mapId, "King Gordok",        -400.0f, -380.0f, 15.0f));
            break;

        case 349: // Maraudon
            v.push_back(MakeBoss(12225, 10, mapId, "Noxxion",               550.0f, 620.0f, -75.0f));
            v.push_back(MakeBoss(12236, 20, mapId, "Razorlash",             520.0f, 590.0f, -75.0f));
            v.push_back(MakeBoss(13282, 30, mapId, "Landslide",             490.0f, 560.0f, -75.0f));
            v.push_back(MakeBoss(12118, 40, mapId, "Tinkerer Gizlock",      460.0f, 530.0f, -75.0f));
            v.push_back(MakeBoss(12203, 50, mapId, "Celebras the Cursed",   430.0f, 500.0f, -75.0f));
            v.push_back(MakeBoss(12201, 60, mapId, "Lord Vyletongue",       400.0f, 470.0f, -75.0f));
            v.push_back(MakeBoss(12902, 70, mapId, "Princess Theradras",    370.0f, 440.0f, -75.0f));
            break;

        case 389: // Ragefire Chasm
            v.push_back(MakeBoss(11517, 10, mapId, "Oggleflint",             -260.0f, 100.0f, -85.0f));
            v.push_back(MakeBoss(11518, 20, mapId, "Taragaman the Hungerer", -290.0f, 70.0f, -85.0f));
            v.push_back(MakeBoss(11519, 30, mapId, "Jergosh the Invoker",    -320.0f, 40.0f, -85.0f));
            v.push_back(MakeBoss(11520, 40, mapId, "Bazzalan",               -350.0f, 10.0f, -85.0f));
            break;

        default:
            break;
    }

    return v;
}

void ai::ApplyRosterPatches(std::vector<DungeonBossInfo>& bosses, uint32 mapId)
{
    std::vector<BossRosterPatch> patches = DungeonEventRegistry::Instance().GetRosterPatchesForMap(mapId);
    for (BossRosterPatch const& patch : patches)
    {
        if (!patch.removeEntries.empty())
        {
            bosses.erase(std::remove_if(bosses.begin(), bosses.end(), [&patch](DungeonBossInfo const& b)
            {
                return std::find(patch.removeEntries.begin(), patch.removeEntries.end(), b.entry) != patch.removeEntries.end();
            }), bosses.end());
        }

        for (DungeonBossInfo const& add : patch.add)
            bosses.push_back(add);
    }

    std::sort(bosses.begin(), bosses.end(), [](DungeonBossInfo const& a, DungeonBossInfo const& b)
    {
        return BossOrderKey(a) < BossOrderKey(b);
    });
}

std::vector<DungeonBossInfo> DungeonBossesValue::Calculate()
{
    std::vector<DungeonBossInfo> result;

    Map* map = bot->GetMap();
    if (!map || (!map->IsDungeon() && !map->IsRaid()))
        return result;

    uint32 mapId = bot->GetMapId();
    result = GetHardcodedBossTable(mapId);
    ApplyRosterPatches(result, mapId);

    // Merge in nearby elite/rare-elite/worldboss hostiles not already
    // covered by the hardcoded table - keeps the engine useful on maps we
    // don't have (or don't fully have) scripted, and tolerates renamed or
    // patched mobs.
    std::list<Creature*> hostiles;
    GetHostileCreaturesListInRange(hostiles, bot, 100.0f);

    uint32 nextIndex = result.empty() ? 0 : (BossOrderKey(result.back()) + 1);
    for (Creature* creature : hostiles)
    {
        if (!creature || !creature->IsAlive())
            continue;

        CreatureInfo const* info = creature->GetCreatureInfo();
        if (!info || info->Rank == CREATURE_ELITE_NORMAL)
            continue;

        uint32 entry = creature->GetEntry();
        bool known = std::any_of(result.begin(), result.end(), [entry](DungeonBossInfo const& b) { return b.entry == entry; });
        if (known)
            continue;

        result.push_back(MakeBoss(entry, nextIndex++, mapId, creature->GetName(),
            creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ()));
    }

    std::sort(result.begin(), result.end(), [](DungeonBossInfo const& a, DungeonBossInfo const& b)
    {
        return BossOrderKey(a) < BossOrderKey(b);
    });

    return result;
}

std::string DungeonBossesValue::Format()
{
    std::vector<DungeonBossInfo> bosses = Get();
    std::ostringstream out;
    out << "{";
    for (DungeonBossInfo const& b : bosses)
        out << b.name << "(" << b.entry << ")" << ",";
    out << "}";
    return out.str();
}
