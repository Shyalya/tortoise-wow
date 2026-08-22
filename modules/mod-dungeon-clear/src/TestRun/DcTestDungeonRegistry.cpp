/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcTestDungeonRegistry.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "PlayerbotAIConfig.h"

#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"

#include "TestRun/DcTestGearTiers.h"
#include "TestRun/DcTestRunRecord.h"

namespace DcTestDungeonRegistry
{
    std::vector<Row> const& All()
    {
        // Entrances = areatrigger_teleport targets (acore_world). Stratholme
        // uses the main (Crusaders' Square) gate, not the service entrance.
        // Dire Maul East uses the courtyard portal by Pusillin's start.
        // Every 3.3.5a 5-man dungeon, curated or not — uncurated maps ride the
        // auto-derived roster alone, and a failing run there is exactly the
        // signal the harness exists to produce. Sorted by recommended level.
        static std::vector<Row> const rows = {
            // --- Classic ---------------------------------------------------
            { "rfc",             "Ragefire Chasm",                389,     3.81f,   -14.82f,  -17.84f, 4.390f, 15, "" },
            { "wc",              "Wailing Caverns",                43,  -163.49f,   132.90f,  -73.66f, 5.830f, 18, "" },
            // Deadmines runs at 20, not the 18 the instance nominally opens at.
            // Its back half is a level-18 party's problem: Gilnid is a level-20
            // elite and his foundry holds 19 level-18 ELITES, so at 18 the party
            // fights same-level elites the whole way in with no level margin at
            // all. See the RoomAggroRegistry row for map 36.
            { "deadmines",       "The Deadmines",                  36,   -16.40f,  -383.07f,   61.78f, 1.860f, 20, "" },
            { "sfk",             "Shadowfang Keep",                33,  -229.13f,  2109.18f,   76.89f, 1.267f, 20, "" },
            { "stockade",        "The Stockade",                   34,    54.23f,     0.28f,  -18.34f, 6.260f, 24, "" },
            { "bfd",             "Blackfathom Deeps",              48,  -151.89f,   106.96f,  -39.87f, 4.530f, 24, "" },
            { "rfk",             "Razorfen Kraul",                 47,  1943.00f,  1544.63f,   82.00f, 1.380f, 30, "" },
            { "sm-gy",           "Scarlet Monastery: Graveyard",  189,  1688.99f,  1053.48f,   18.68f, 0.001f, 30, "Graveyard" },
            { "gnomer",          "Gnomeregan",                     90,  -332.22f,    -2.28f, -150.86f, 2.770f, 32, "" },
            { "sm-lib",          "Scarlet Monastery: Library",    189,   255.35f,  -209.09f,   18.68f, 6.267f, 35, "Library" },
            { "sm-arm",          "Scarlet Monastery: Armory",     189,  1610.83f,  -323.43f,   18.67f, 6.280f, 38, "Armory" },
            { "sm-cath",         "Scarlet Monastery: Cathedral",  189,   855.68f,  1321.50f,   18.67f, 0.002f, 40, "Cathedral" },
            { "rfd",             "Razorfen Downs",                129,  2592.55f,  1107.50f,   51.29f, 4.740f, 40, "" },
            { "uldaman",         "Uldaman",                        70,  -226.80f,    49.09f,  -46.03f, 1.390f, 44, "" },
            { "zf",              "Zul'Farrak",                    209,  1213.52f,   841.59f,    8.93f, 6.090f, 46, "" },
            { "maraudon",        "Maraudon",                      349,  1019.69f,  -458.31f,  -43.43f, 0.310f, 48, "" },
            { "st",              "The Temple of Atal'Hakkar",     109,  -319.24f,    99.90f, -131.85f, 3.190f, 52, "" },
            { "brd",             "Blackrock Depths",              230,   456.93f,    34.09f,  -68.09f, 4.712f, 54, "" },
            { "brs",             "Blackrock Spire",               229,    78.51f,  -225.04f,   49.84f, 5.100f, 58, "" },
            { "dm-east",         "Dire Maul: East",               429,    44.45f,  -154.82f,   -2.71f, 0.000f, 58, "East" },
            { "dm-west",         "Dire Maul: West",               429,   -62.97f,   159.87f,   -3.46f, 3.148f, 60, "West" },
            { "dm-north",        "Dire Maul: North",              429,   255.25f,   -16.06f,   -2.59f, 4.700f, 60, "North" },
            { "scholo",          "Scholomance",                   289,   196.37f,   127.05f,  134.91f, 6.090f, 60, "" },
            { "strat",           "Stratholme",                    329,  3593.15f, -3646.56f,  138.50f, 5.330f, 60, "" },
            // --- The Burning Crusade --------------------------------------
            { "ramparts",        "Hellfire Ramparts",             543, -1355.24f,  1641.12f,   68.25f, 0.669f, 62, "", 70 },
            { "blood-furnace",   "The Blood Furnace",             542,    -4.00f,    14.64f,  -44.80f, 4.887f, 63, "", 70 },
            { "slave-pens",      "The Slave Pens",                547,   120.10f,  -131.96f,   -0.80f, 1.476f, 64, "", 70 },
            { "underbog",        "The Underbog",                  546,     9.71f,   -16.20f,   -2.75f, 5.571f, 64, "", 70 },
            { "mana-tombs",      "Mana-Tombs",                    557,     0.02f,     0.95f,   -0.95f, 3.032f, 66, "", 70 },
            { "auchenai",        "Auchenai Crypts",               558,   -21.90f,     0.16f,   -0.12f, 0.035f, 67, "", 70 },
            { "sethekk",         "Sethekk Halls",                 556,    -4.68f,    -0.09f,    0.01f, 0.035f, 68, "", 70 },
            { "old-hillsbrad",   "Old Hillsbrad Foothills",       560,  2741.87f,  1315.25f,   14.04f, 2.960f, 68, "", 70 },
            { "shadow-labs",     "Shadow Labyrinth",              555,     0.49f,    -0.22f,   -1.13f, 3.159f, 70, "", 70 },
            { "steamvault",      "The Steamvault",                545,   -13.84f,     6.75f,   -4.26f, 0.000f, 70, "", 70 },
            { "shattered-halls", "The Shattered Halls",           540,   -40.87f,   -19.75f,  -13.81f, 1.111f, 70, "", 70 },
            { "black-morass",    "The Black Morass",              269, -1496.24f,  7034.70f,   32.56f, 1.757f, 70, "", 70 },
            { "botanica",        "The Botanica",                  553,    40.04f,   -28.61f,   -1.12f, 2.359f, 70, "", 70 },
            { "mechanar",        "The Mechanar",                  554,   -28.91f,     0.68f,   -1.81f, 0.035f, 70, "", 70 },
            { "arcatraz",        "The Arcatraz",                  552,    -1.23f,     0.01f,   -0.20f, 0.016f, 70, "", 70 },
            { "mgt",             "Magisters' Terrace",            585,     7.09f,    -0.45f,   -2.80f, 0.050f, 70, "", 70 },
            // --- Wrath of the Lich King -----------------------------------
            { "uk",              "Utgarde Keep",                  574,   153.79f,   -86.55f,   12.55f, 0.304f, 70, "", 80 },
            { "nexus",           "The Nexus",                     576,   145.87f,   -10.55f,  -16.64f, 1.528f, 71, "", 80 },
            { "an",              "Azjol-Nerub",                   601,   413.31f,   795.97f,  831.35f, 5.500f, 74, "", 80 },
            { "ok",              "Ahn'kahet: The Old Kingdom",    619,   333.35f, -1109.94f,   69.77f, 0.553f, 74, "", 80 },
            { "dtk",             "Drak'Tharon Keep",              600,  -517.34f,  -487.98f,   11.01f, 4.831f, 75, "", 80 },
            { "vh",              "The Violet Hold",               608,  1808.82f,   803.93f,   44.36f, 6.282f, 75, "", 80 },
            { "gundrak",         "Gundrak",                       604,  1891.84f,   832.17f,  176.67f, 2.109f, 77, "", 80 },
            { "hos",             "Halls of Stone",                599,  1153.24f,   806.16f,  195.94f, 4.715f, 78, "", 80 },
            { "hol",             "Halls of Lightning",            602,  1331.47f,   259.62f,   53.40f, 4.772f, 79, "", 80 },
            { "cos",             "The Culling of Stratholme",     595,  1431.10f,   556.92f,   36.69f, 5.160f, 79, "", 80 },
            { "oculus",          "The Oculus",                    578,  1055.93f,   986.85f,  361.07f, 5.745f, 79, "", 80 },
            { "up",              "Utgarde Pinnacle",              575,   584.12f,  -327.97f,  110.14f, 3.122f, 79, "", 80 },
            { "toc",             "Trial of the Champion",         650,   805.23f,   618.04f,  412.39f, 3.146f, 80, "", 80 },
            { "fos",             "The Forge of Souls",            632,  4922.86f,  2175.63f,  638.73f, 2.004f, 80, "", 80 },
            { "pos",             "Pit of Saron",                  658,   435.74f,   212.41f,  528.71f, 6.256f, 80, "", 80 },
            { "hor",             "Halls of Reflection",           668,  5239.01f,  1932.64f,  707.70f, 0.801f, 80, "", 80 },
        };
        return rows;
    }

    Row const* Find(std::string const& tokenOrMapId)
    {
        if (tokenOrMapId.empty())
            return nullptr;

        for (Row const& row : All())
            if (tokenOrMapId == row.token)
                return &row;

        // Numeric fallback — only unambiguous for maps with a single row.
        char* end = nullptr;
        unsigned long const asMap = std::strtoul(tokenOrMapId.c_str(), &end, 10);
        if (!end || *end != '\0' || asMap == 0)
            return nullptr;

        Row const* hit = nullptr;
        for (Row const& row : All())
            if (row.mapId == asMap)
            {
                if (hit)
                    return nullptr;  // wing-split map: demand a wing token
                hit = &row;
            }
        return hit;
    }

    void WriteSidecar()
    {
        char const* path = "dc_test_dungeons.json";
        if (char const* env = std::getenv("DC_TEST_DUNGEONS_FILE"))
            if (env[0])
                path = env;

        using DcTestRunRecord::EscapeJson;

        // The ilvl ladder the dashboard's start form offers, per row and per
        // difficulty (heroic runs at a different level, so a TBC row's two
        // ladders differ). Emitted here rather than computed in the dashboard so
        // the two can't drift: this file IS the catalogue.
        auto appendLadder = [](std::ostringstream& out, std::uint32_t mapId, std::uint32_t level)
        {
            out << '[';
            bool firstChoice = true;
            for (DcTestGearTiers::Choice const& choice : DcTestGearTiers::Ladder(mapId, level))
            {
                if (!firstChoice)
                    out << ',';
                firstChoice = false;
                out << "{\"ilvl\":" << choice.ilvl << ",\"label\":\"" << EscapeJson(choice.label)
                    << "\"}";
            }
            out << ']';
        };

        std::ostringstream s;
        s << "{\"limits\":{\"maxConcurrent\":"
          << DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.MaxConcurrent")
          << ",\"maxPlans\":"
          << DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.MaxPlans")
          << ",\"planMaxTotal\":"
          << DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.Plan.MaxTotal")
          << "}";

        // What a run gets when it asks for nothing. Read once at startup like
        // the rest of this file, so it is a label for the form's "server
        // default" entry, not an authority — the worldserver resolves the real
        // values when the run starts.
        s << ",\"gearDefaults\":{\"ilvl\":"
          << (sPlayerbotAIConfig.autoGearScoreLimit > 0 ? sPlayerbotAIConfig.autoGearScoreLimit : 0)
          << ",\"quality\":"
          << (sPlayerbotAIConfig.autoGearQualityLimit > 0 ? sPlayerbotAIConfig.autoGearQualityLimit
                                                          : 3)
          << "},\"qualities\":[";
        for (std::uint32_t q = 1; q <= 5; ++q)
            s << (q > 1 ? "," : "") << "{\"v\":" << q << ",\"label\":\""
              << DcTestGearTiers::QualityName(q) << "\"}";
        s << "],\"dungeons\":[";
        bool first = true;
        for (Row const& row : All())
        {
            if (!first)
                s << ',';
            first = false;
            s << "{\"token\":\"" << EscapeJson(row.token) << '"'
              << ",\"name\":\"" << EscapeJson(row.name) << '"'
              << ",\"mapId\":" << row.mapId
              << ",\"level\":" << row.recommendedLevel
              << ",\"heroicLevel\":" << row.heroicLevel
              << ",\"wing\":\"" << EscapeJson(row.wing) << '"'
              << ",\"gear\":";
            appendLadder(s, row.mapId, row.recommendedLevel);
            if (row.heroicLevel)
            {
                s << ",\"gearHeroic\":";
                appendLadder(s, row.mapId, row.heroicLevel);
            }
            s << '}';
        }
        s << "]}";

        std::ofstream f(path, std::ios::out | std::ios::trunc);
        if (!f.is_open())
            return;
        f << s.str() << '\n';
    }
}
