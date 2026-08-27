#include "playerbot/playerbot.h"
#include "Value/DungeonBossesValue.h"
#include "Data/DungeonEventRegistry.h"
#include "Data/DungeonWingRegistry.h"
#include "DcRunState.h"
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
        float x, float y, float z, float radius = 15.0f, uint32 eventId = 0,
        bool spawnOnApproach = false)
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
        b.spawnOnApproach = spawnOnApproach;
        return b;
    }
}

uint8 ai::DetermineDungeonRosterVariant(Player* bot)
{
    int32 wing = DungeonWingRegistry::FindNearestWing(bot);
    return wing < 0 ? 0xff : static_cast<uint8>(wing);
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

        case 48: // Blackfathom Deeps
            v.push_back(MakeBoss(4887, 10, mapId, "Ghamoo-ra",              -442.424f, 211.822f, -52.6367f));
            v.push_back(MakeBoss(4831, 20, mapId, "Lady Sarevess",          -305.567f, 409.450f, -57.0584f));
            v.push_back(MakeBoss(6243, 30, mapId, "Gelihast",               -413.593f, 39.8474f, -48.4051f));
            v.push_back(MakeBoss(12876, 40, mapId, "Baron Aquanis",         -782.210f, -63.260f, -42.4300f, 15.0f, 0, true));
            v.push_back(MakeBoss(4832, 50, mapId, "Twilight Lord Kelris",   -818.832f, -155.576f, -25.7923f));
            v.push_back(MakeBoss(4830, 60, mapId, "Old Serra'kis",          -746.744f, -169.427f, -50.6239f));
            v.push_back(MakeBoss(4829, 70, mapId, "Aku'mai",                -848.446f, -453.865f, -33.8922f));
            break;

        case 189: // Scarlet Monastery wings share this mapId; filtered below.
            v.push_back(MakeBoss(3983, 10, mapId, "Interrogator Vishas",   1786.55f, 1124.44f, 7.57f));
            v.push_back(MakeBoss(4543, 20, mapId, "Bloodmage Thalnos",     1820.34f, 1416.71f, -7.92f));
            v.push_back(MakeBoss(3974, 30, mapId, "Houndmaster Loksey",    119.94f, -260.87f, 18.63f));
            v.push_back(MakeBoss(6487, 40, mapId, "Arcanist Doan",         148.32f, -428.69f, 18.49f));
            v.push_back(MakeBoss(3975, 50, mapId, "Herod",                 1965.09f, -431.61f, 6.26f));
            v.push_back(MakeBoss(3976, 60, mapId, "Scarlet Commander Mograine", 1153.87f, 1398.39f, 32.61f));
            v.push_back(MakeBoss(4542, 70, mapId, "High Inquisitor Fairbanks", 1088.0f, 1386.0f, 32.0f));
            v.push_back(MakeBoss(3977, 80, mapId, "High Inquisitor Whitemane", 1202.13f, 1399.07f, 29.09f));
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
            v.push_back(MakeBoss(8567, 30, mapId, "Glutton",                 1180.0f, 1130.0f, 70.0f));
            v.push_back(MakeBoss(7354, 40, mapId, "Ragglesnout",             1150.0f, 1090.0f, 70.0f));
            v.push_back(MakeBoss(7356, 50, mapId, "Plaguemaw the Rotting",   1120.0f, 1050.0f, 70.0f));
            v.push_back(MakeBoss(7358, 60, mapId, "Amnennar the Coldbringer",1080.0f, 1000.0f, 70.0f));
            break;

        case 70: // Uldaman
            v.push_back(MakeBoss(6910, 10, mapId, "Revelosh",             -6110.0f, -251.0f, -190.0f));
            v.push_back(MakeBoss(7023, 20, mapId, "Obsidian Sentinel",    -6050.0f, -200.0f, -195.0f));
            v.push_back(MakeBoss(7228, 30, mapId, "Ironaya",              -6000.0f, -150.0f, -200.0f));
            v.push_back(MakeBoss(4854, 40, mapId, "Grimlok",              -5950.0f, -100.0f, -205.0f));
            v.push_back(MakeBoss(2748, 50, mapId, "Archaedas",            -5890.0f, -60.0f, -210.0f));
            break;

        case 90: // Gnomeregan
            v.push_back(MakeBoss(7361, 10, mapId, "Grubbis",                -285.0f, 66.0f, 296.0f));
            v.push_back(MakeBoss(7079, 20, mapId, "Viscous Fallout",        -230.0f, 100.0f, 296.0f));
            v.push_back(MakeBoss(6235, 30, mapId, "Electrocutioner 6000",   -180.0f, 140.0f, 296.0f));
            v.push_back(MakeBoss(6229, 40, mapId, "Crowd Pummeler 9-60",    -130.0f, 180.0f, 296.0f));
            v.push_back(MakeBoss(7800, 50, mapId, "Mekgineer Thermaplugg",   -80.0f, 220.0f, 296.0f));
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
            v.push_back(MakeBoss(9016, 10, mapId, "Bael'Gar",                    600.0f, -300.0f, -130.0f));
            v.push_back(MakeBoss(9319, 20, mapId, "Houndmaster Grebmar",         650.0f, -260.0f, -130.0f));
            v.push_back(MakeBoss(9018, 30, mapId, "High Interrogator Gerstahn",  700.0f, -220.0f, -130.0f));
            v.push_back(MakeBoss(9025, 40, mapId, "Lord Roccor",                 750.0f, -180.0f, -130.0f));
            v.push_back(MakeBoss(9056, 50, mapId, "Fineous Darkvire",            800.0f, -140.0f, -130.0f));
            v.push_back(MakeBoss(9156, 60, mapId, "Ambassador Flamelash",        850.0f, -100.0f, -130.0f));
            v.push_back(MakeBoss(9033, 70, mapId, "General Angerforge",          900.0f, -60.0f, -130.0f));
            v.push_back(MakeBoss(9019, 80, mapId, "Emperor Dagran Thaurissan",   950.0f, -20.0f, -130.0f));
            break;

        case 229: // Blackrock Spire (lower and upper wings share this map)
            // Lower Blackrock Spire.  Urok and Gizrul are summoned encounters;
            // their anchors are included so a spawned version is still picked
            // up by the normal boss search.
            v.push_back(MakeBoss(9196, 10, mapId, "Highlord Omokk",          -22.6518f, -299.750f, 31.7016f));
            v.push_back(MakeBoss(9736, 20, mapId, "Quartermaster Zigris",   -199.061f, -458.927f, 87.3902f));
            v.push_back(MakeBoss(9236, 30, mapId, "Shadow Hunter Vosh'gajin", -121.190f, -482.180f, 24.7100f));
            v.push_back(MakeBoss(9237, 40, mapId, "War Master Voone",        -16.9764f, -459.149f, -18.6442f));
            v.push_back(MakeBoss(9218, 50, mapId, "Mother Smolderweb",       -51.1337f, -326.459f, 43.0450f));
            v.push_back(MakeBoss(10584, 60, mapId, "Urok Doomhowl",          -49.4000f, -368.500f, 51.7000f, 15.0f, 0, true));
            v.push_back(MakeBoss(10220, 70, mapId, "Halycon",                -193.914f, -338.148f, 64.4879f));
            v.push_back(MakeBoss(10268, 80, mapId, "Gizrul the Slavener",    -167.580f, -382.410f, 64.4010f, 15.0f, 0, true));
            v.push_back(MakeBoss(9568, 90, mapId, "Overlord Wyrmthalak",     -22.6325f, -486.186f, 90.7531f));

            // Upper Blackrock Spire.
            v.push_back(MakeBoss(9816, 100, mapId, "Pyroguard Emberseer",    144.438f, -258.034f, 96.4066f));
            v.push_back(MakeBoss(10429, 110, mapId, "Warchief Rend Blackhand",159.276f, -443.619f, 122.059f));
            v.push_back(MakeBoss(10264, 105, mapId, "Solakar Flamewreath",   43.7685f, -259.820f, 91.6483f, 15.0f, 0, true));
            v.push_back(MakeBoss(10339, 120, mapId, "Gyth",                  211.762f, -397.580f, 111.180f));
            v.push_back(MakeBoss(10430, 130, mapId, "The Beast",              124.974f, -570.138f, 107.011f));
            v.push_back(MakeBoss(10363, 140, mapId, "General Drakkisath",     34.760f, -285.310f, 111.130f));
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
            v.push_back(MakeBoss(10997, 10, mapId, "Cannon Master Willey",     3670.0f, -3400.0f, 130.0f));
            v.push_back(MakeBoss(11143, 20, mapId, "Postmaster Malown",        3620.0f, -3350.0f, 130.0f));
            v.push_back(MakeBoss(10808, 30, mapId, "Timmy the Cruel",          3570.0f, -3300.0f, 130.0f));
            v.push_back(MakeBoss(10558, 40, mapId, "Hearthsinger Forresten",   3520.0f, -3250.0f, 130.0f));
            v.push_back(MakeBoss(10436, 50, mapId, "Baroness Anastari",        3470.0f, -3200.0f, 130.0f));
            v.push_back(MakeBoss(10437, 60, mapId, "Nerub'enkan",              3420.0f, -3150.0f, 130.0f));
            v.push_back(MakeBoss(10438, 70, mapId, "Maleki the Pallid",        3370.0f, -3100.0f, 130.0f));
            v.push_back(MakeBoss(10439, 80, mapId, "Ramstein the Gorger",      3320.0f, -3050.0f, 130.0f));
            v.push_back(MakeBoss(10440, 90, mapId, "Baron Rivendare",          3270.0f, -3000.0f, 130.0f));
            break;

        case 429: // Dire Maul wings share this mapId; filtered below.
            v.push_back(MakeBoss(11490, 10, mapId, "Zevrim Thornhoof",     -34.98f, -448.00f, -37.88f));
            v.push_back(MakeBoss(13280, 20, mapId, "Hydrospawn",             4.58f, -438.41f, -59.95f));
            v.push_back(MakeBoss(14327, 30, mapId, "Lethtendris",            86.19f, -197.89f, -4.06f));
            v.push_back(MakeBoss(11492, 50, mapId, "Alzzin the Wildshaper", 274.84f, -427.25f, -119.96f));
            v.push_back(MakeBoss(11489, 60, mapId, "Tendris Warpwood",       14.39f, 475.85f, -23.30f));
            v.push_back(MakeBoss(11488, 70, mapId, "Illyanna Ravenoak",      33.14f, 575.55f, -4.31f));
            v.push_back(MakeBoss(11487, 80, mapId, "Magister Kalendris",     30.0f, 645.0f, -4.0f));
            v.push_back(MakeBoss(11496, 90, mapId, "Immol'thar",            -38.08f, 812.44f, -29.45f));
            v.push_back(MakeBoss(11486, 100, mapId, "Prince Tortheldrin",     132.63f, 625.91f, -48.38f));
            v.push_back(MakeBoss(14326, 110, mapId, "Guard Mol'dar",          410.71f, -3.15f, -24.56f));
            v.push_back(MakeBoss(14322, 120, mapId, "Stomper Kreeg",         491.23f, 97.39f, -2.50f));
            v.push_back(MakeBoss(14321, 130, mapId, "Guard Fengus",          560.0f, 180.0f, -2.0f));
            v.push_back(MakeBoss(14323, 140, mapId, "Guard Slip'kik",        650.0f, 300.0f, 10.0f));
            v.push_back(MakeBoss(14325, 150, mapId, "Captain Kromcrush",     627.59f, 481.72f, 29.46f));
            v.push_back(MakeBoss(11501, 160, mapId, "King Gordok",           829.70f, 481.33f, 37.40f));
            break;

        case 349: // Maraudon
            v.push_back(MakeBoss(13282, 10, mapId, "Noxxion",               550.0f, 620.0f, -75.0f));
            v.push_back(MakeBoss(12258, 20, mapId, "Razorlash",             520.0f, 590.0f, -75.0f));
            v.push_back(MakeBoss(12236, 30, mapId, "Lord Vyletongue",       400.0f, 470.0f, -75.0f));
            v.push_back(MakeBoss(12225, 40, mapId, "Celebras the Cursed",   430.0f, 500.0f, -75.0f));
            v.push_back(MakeBoss(13601, 50, mapId, "Tinkerer Gizlock",      460.0f, 530.0f, -75.0f));
            v.push_back(MakeBoss(12203, 60, mapId, "Landslide",             490.0f, 560.0f, -75.0f));
            v.push_back(MakeBoss(13596, 70, mapId, "Rotgrip",                420.0f, 430.0f, -120.0f));
            v.push_back(MakeBoss(12201, 80, mapId, "Princess Theradras",    370.0f, 440.0f, -75.0f));
            break;

        case 389: // Ragefire Chasm
            v.push_back(MakeBoss(11517, 10, mapId, "Oggleflint",             -260.0f, 100.0f, -85.0f));
            v.push_back(MakeBoss(11518, 20, mapId, "Taragaman the Hungerer", -290.0f, 70.0f, -85.0f));
            v.push_back(MakeBoss(11519, 30, mapId, "Jergosh the Invoker",    -320.0f, 40.0f, -85.0f));
            v.push_back(MakeBoss(11520, 40, mapId, "Bazzalan",               -350.0f, 10.0f, -85.0f));
            break;

        case 249: // Onyxia's Lair
            v.push_back(MakeBoss(10184, 10, mapId, "Onyxia", 0.69628f, -211.972f, -86.075f, 25.0f));
            break;

        case 309: // Zul'Gurub
        {
            v.push_back(MakeBoss(14517, 10, mapId, "High Priestess Jeklik",  -12289.7f, -1382.18f, 144.643f));
            v.push_back(MakeBoss(14507, 20, mapId, "High Priest Venoxis",    -12029.8f, -1707.93f, 39.413f));
            v.push_back(MakeBoss(14510, 30, mapId, "High Priestess Mar'li", -12326.5f, -1577.11f, 133.588f));
            v.push_back(MakeBoss(11382, 40, mapId, "Bloodlord Mandokir",     -12167.8f, -1927.25f, 153.730f));
            v.push_back(MakeBoss(15114, 45, mapId, "Gahz'ranka",             -11688.95f, -1777.21f, 12.593f, 15.0f, 0, true));
            DungeonBossInfo thekal = MakeBoss(14599, 50, mapId, "High Priest Thekal", -11700.8f, -1998.94f, 62.4163f);
            thekal.alternateEntries.push_back(14509);
            v.push_back(thekal);
            v.push_back(MakeBoss(14515, 60, mapId, "High Priestess Arlokk",  -11569.2f, -1627.87f, 41.2767f, 15.0f, 0, true));
            v.push_back(MakeBoss(11380, 70, mapId, "Jin'do the Hexxer",       -11515.8f, -1275.48f, 79.6621f));
            v.push_back(MakeBoss(14834, 80, mapId, "Hakkar",                 -11787.4f, -1649.34f, 54.1017f));
            // Edge of Madness rotates exactly one of these creatures into the
            // shared summoning site. Keep it as one encounter with runtime
            // aliases so the other three variants are not falsely required.
            DungeonBossInfo edge = MakeBoss(15082, 90, mapId, "Edge of Madness",
                -11901.45f, -1906.337f, 65.370f, 15.0f, 0, true);
            edge.alternateEntries.push_back(15083); // Hazza'rah
            edge.alternateEntries.push_back(15084); // Renataki
            edge.alternateEntries.push_back(15085); // Wushoolay
            v.push_back(edge);
            break;
        }

        case 409: // Molten Core
            v.push_back(MakeBoss(12118, 10, mapId, "Lucifron",              1024.41f, -973.309f, -181.505f));
            v.push_back(MakeBoss(11982, 20, mapId, "Magmadar",              1143.14f, -1016.66f, -185.751f));
            v.push_back(MakeBoss(12259, 30, mapId, "Gehennas",               898.074f, -545.870f, -203.405f));
            v.push_back(MakeBoss(12057, 40, mapId, "Garr",                   691.018f, -498.387f, -214.266f));
            v.push_back(MakeBoss(12056, 50, mapId, "Baron Geddon",            614.043f, -806.284f, -207.001f));
            v.push_back(MakeBoss(12264, 60, mapId, "Shazzrah",                583.490f, -799.755f, -205.356f));
            v.push_back(MakeBoss(12098, 70, mapId, "Sulfuron Harbinger",      601.087f, -1179.11f, -195.882f));
            v.push_back(MakeBoss(11988, 80, mapId, "Golemagg the Incinerator",793.248f, -998.265f, -206.762f));
            v.push_back(MakeBoss(12018, 90, mapId, "Majordomo Executus",      847.103f, -816.153f, -229.775f, 15.0f, 0, true));
            v.push_back(MakeBoss(11502, 100, mapId, "Ragnaros",               842.237f, -833.683f, -231.916f, 15.0f, 0, true));
            break;

        case 469: // Blackwing Lair
            v.push_back(MakeBoss(12435, 10, mapId, "Razorgore the Untamed",  -7571.69f, -1088.25f, 413.465f));
            v.push_back(MakeBoss(13020, 20, mapId, "Vaelastrasz the Corrupt",-7482.26f, -1017.31f, 408.567f));
            v.push_back(MakeBoss(12017, 30, mapId, "Broodlord Lashlayer",    -7573.89f, -1035.25f, 449.248f));
            v.push_back(MakeBoss(11983, 40, mapId, "Firemaw",                -7506.40f, -1015.70f, 448.906f));
            v.push_back(MakeBoss(14601, 50, mapId, "Ebonroc",                -7358.19f, -994.32f, 477.350f));
            v.push_back(MakeBoss(11981, 60, mapId, "Flamegor",               -7407.94f, -1031.04f, 477.350f));
            v.push_back(MakeBoss(14020, 70, mapId, "Chromaggus",              -7515.34f, -1029.62f, 476.730f));
            v.push_back(MakeBoss(11583, 80, mapId, "Nefarian",                -7515.644f, -1222.698f, 534.7169f, 15.0f, 0, true));
            break;

        case 509: // Ruins of Ahn'Qiraj
            v.push_back(MakeBoss(15348, 10, mapId, "Kurinnaxx",              -8842.99f, 1624.00f, 19.6903f));
            v.push_back(MakeBoss(15341, 20, mapId, "General Rajaxx",          -9132.92f, 1599.21f, 26.8425f));
            v.push_back(MakeBoss(15340, 30, mapId, "Moam",                   -8851.26f, 2263.29f, 21.3093f));
            v.push_back(MakeBoss(15370, 40, mapId, "Buru the Gorger",         -9240.79f, 1230.42f, -63.7576f));
            v.push_back(MakeBoss(15369, 50, mapId, "Ayamiss the Hunter",      -9698.38f, 1535.55f, 21.444f));
            v.push_back(MakeBoss(15339, 60, mapId, "Ossirian the Unscarred",  -9493.60f, 2030.27f, 105.480f));
            break;

        case 531: // Temple of Ahn'Qiraj
            v.push_back(MakeBoss(15263, 10, mapId, "The Prophet Skeram",     -8343.20f, 2083.07f, 125.650f));
            v.push_back(MakeBoss(15516, 20, mapId, "Battleguard Sartura",     -8335.63f, 1649.10f, -28.860f));
            v.push_back(MakeBoss(15510, 30, mapId, "Fankriss the Unyielding", -8085.39f, 1196.72f, -91.970f));
            v.push_back(MakeBoss(15511, 40, mapId, "Lord Kri",                -8566.02f, 2174.84f, -4.04391f));
            v.push_back(MakeBoss(15543, 41, mapId, "Princess Yauj",           -8587.09f, 2175.10f, -4.23954f));
            v.push_back(MakeBoss(15544, 42, mapId, "Vem",                     -8534.08f, 2169.40f, -3.21880f));
            v.push_back(MakeBoss(15509, 50, mapId, "Princess Huhuran",        -8532.09f, 1696.53f, -90.260f));
            v.push_back(MakeBoss(15299, 60, mapId, "Viscidus",                -7993.96f, 926.309f, -52.699f));
            v.push_back(MakeBoss(15276, 70, mapId, "Emperor Vek'lor",         -8874.06f, 1204.74f, -104.170f));
            v.push_back(MakeBoss(15275, 71, mapId, "Emperor Vek'nilash",      -9019.14f, 1180.07f, -104.170f));
            v.push_back(MakeBoss(15517, 80, mapId, "Ouro",                    -9188.45f, 2091.56f, -64.170f, 15.0f, 0, true));
            v.push_back(MakeBoss(15727, 90, mapId, "C'Thun",                  -8577.27f, 1986.94f, 100.400f));
            break;

        case 533: // Naxxramas
            v.push_back(MakeBoss(15956, 10, mapId, "Anub'Rekhan",             3316.47f, -3476.23f, 287.260f));
            v.push_back(MakeBoss(15953, 20, mapId, "Grand Widow Faerlina",    3353.16f, -3620.63f, 261.180f));
            v.push_back(MakeBoss(15952, 30, mapId, "Maexxna",                 3503.04f, -3919.22f, 297.600f));
            v.push_back(MakeBoss(15954, 40, mapId, "Noth the Plaguebringer",  2675.49f, -3491.24f, 261.530f));
            v.push_back(MakeBoss(15936, 50, mapId, "Heigan the Unclean",      2793.86f, -3707.38f, 276.627f));
            v.push_back(MakeBoss(16011, 60, mapId, "Loatheb",                 2909.43f, -3999.46f, 274.280f));
            v.push_back(MakeBoss(16061, 70, mapId, "Instructor Razuvious",    2755.56f, -3098.04f, 267.860f));
            v.push_back(MakeBoss(16060, 80, mapId, "Gothik the Harvester",     2642.20f, -3388.39f, 285.600f));
            v.push_back(MakeBoss(16062, 90, mapId, "Highlord Mograine",        2519.95f, -2947.47f, 245.640f));
            v.push_back(MakeBoss(16065, 91, mapId, "Lady Blaumeux",            2516.24f, -2951.57f, 245.640f));
            v.push_back(MakeBoss(16064, 92, mapId, "Thane Korth'azz",          2513.52f, -2954.96f, 245.640f));
            v.push_back(MakeBoss(16063, 93, mapId, "Sir Zeliek",               2524.45f, -2944.71f, 245.640f));
            v.push_back(MakeBoss(16028, 100, mapId, "Patchwerk",               3308.46f, -3232.08f, 294.240f));
            v.push_back(MakeBoss(15931, 110, mapId, "Grobbulus",               3205.45f, -3341.86f, 320.177f));
            v.push_back(MakeBoss(15932, 120, mapId, "Gluth",                  3283.09f, -3156.96f, 297.788f));
            v.push_back(MakeBoss(15928, 130, mapId, "Thaddius",               3513.84f, -2926.55f, 302.914f));
            v.push_back(MakeBoss(15989, 140, mapId, "Sapphiron",               3521.30f, -5237.56f, 137.720f));
            v.push_back(MakeBoss(15990, 150, mapId, "Kel'Thuzad",              3746.41f, -5113.35f, 142.031f));
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

    std::stable_sort(bosses.begin(), bosses.end(), [](DungeonBossInfo const& a, DungeonBossInfo const& b)
    {
        uint32 const aKey = BossOrderKey(a);
        uint32 const bKey = BossOrderKey(b);
        if (aKey != bKey)
            return aKey < bKey;
        return a.kind == DungeonAnchorKind::Objective && b.kind == DungeonAnchorKind::Boss;
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

    DungeonWingLayout const* wingLayout = DungeonWingRegistry::Get(mapId);
    if (wingLayout && wingLayout->isolated)
    {
        DcRunState& state = AI_VALUE(DcRunState&, DcKey::RunState);
        if (!state.enabled || state.rosterMapId != mapId
            || state.rosterInstanceId != map->GetInstanceId() || state.rosterVariant == 0xff)
        {
            state.rosterMapId = mapId;
            state.rosterInstanceId = map->GetInstanceId();
            state.rosterVariant = DetermineDungeonRosterVariant(bot);
        }

        // Unknown position is a safe no-op.  The upstream implementation keeps
        // the full roster rather than turning a detection miss into an empty run.
        if (state.rosterVariant < wingLayout->wings.size())
        {
            result.erase(std::remove_if(result.begin(), result.end(), [mapId, &state](DungeonBossInfo const& boss)
            {
                return !DungeonWingRegistry::Contains(mapId, state.rosterVariant, boss.entry);
            }), result.end());
        }
    }

    // Merge only genuinely rare/raid-level nearby hostiles not already
    // covered by the hardcoded table.  Ordinary elite trash is not an
    // encounter and must never become a required boss just because it is
    // currently loaded near the party.
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
        if (info->Rank == CREATURE_ELITE_ELITE || info->Rank == CREATURE_ELITE_RARE)
            continue;

        uint32 entry = creature->GetEntry();
        bool known = std::any_of(result.begin(), result.end(), [entry](DungeonBossInfo const& b) { return b.entry == entry; });
        if (known)
            continue;

        result.push_back(MakeBoss(entry, nextIndex++, mapId, creature->GetName(),
            creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ()));
    }

    std::stable_sort(result.begin(), result.end(), [](DungeonBossInfo const& a, DungeonBossInfo const& b)
    {
        uint32 const aKey = BossOrderKey(a);
        uint32 const bKey = BossOrderKey(b);
        if (aKey != bKey)
            return aKey < bKey;
        return a.kind == DungeonAnchorKind::Objective && b.kind == DungeonAnchorKind::Boss;
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
