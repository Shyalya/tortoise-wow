// Scripted door/lever/escort sequences for the "extended" Classic instance
// tier. Same pattern as ClassicStarterEvents.cpp: one BossRosterPatch
// objective anchor per event, slotted at an encounterIndex between the
// bosses it gates.
#include "Data/DungeonEventRegistry.h"

namespace ai
{
    void RegisterClassicExtendedEvents(std::vector<DungeonEvent>& out, std::vector<BossRosterPatch>& patches)
    {
        // ---- Uldaman (70): Ironaya's Seal / ancient door ----
        out.push_back(
            EventBuilder(70001, 70, "Uldaman Ancient Door")
                .Anchored(45)
                .MoveTo(-6100.0f, -300.0f, -195.0f, 6.0f)
                .UseGO(124367, 6.0f)
                .Timeout(30000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 70;
            patch.add.push_back(DcRoster::MakeObjective(124367, 45, 70, "Ancient Door",
                -6100.0f, -300.0f, -195.0f, 10.0f, 124367, 70001));
            patches.push_back(std::move(patch));
        }

        // ---- Sunken Temple (109): Atal'ai serpent statues / altar ----
        out.push_back(
            EventBuilder(109001, 109, "ST Atal'ai Statues")
                .Anchored(45)
                .MoveTo(-360.0f, -60.0f, -68.0f, 8.0f)
                .Wait(3000)
                .Timeout(60000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 109;
            patch.add.push_back(DcRoster::MakeObjective(0, 45, 109, "Atal'ai Statues",
                -360.0f, -60.0f, -68.0f, 10.0f, 0, 109001));
            patches.push_back(std::move(patch));
        }

        // ---- Razorfen Downs (129): Gong of Tuten'kash ----
        out.push_back(
            EventBuilder(129001, 129, "RFD Gong")
                .Anchored(5)
                .UseGO(148917, 8.0f)
                .Wait(5000)
                .Timeout(45000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 129;
            patch.add.push_back(DcRoster::MakeObjective(148917, 5, 129, "Gong of Tuten'kash",
                1240.0f, 1235.0f, 70.0f, 10.0f, 148917, 129001));
            patches.push_back(std::move(patch));
        }

        // ---- Blackrock Depths (230): Shadowforge gate ----
        out.push_back(
            EventBuilder(230001, 230, "BRD Shadowforge Gate")
                .Anchored(15)
                .MoveTo(650.0f, -260.0f, -130.0f, 6.0f)
                .UseGO(161460, 6.0f)
                .Timeout(30000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 230;
            patch.add.push_back(DcRoster::MakeObjective(161460, 15, 230, "Shadowforge Gate",
                650.0f, -260.0f, -130.0f, 10.0f, 161460, 230001));
            patches.push_back(std::move(patch));
        }

        // ---- Scholomance (289): Viewing Room door ----
        out.push_back(
            EventBuilder(289001, 289, "Scholo Viewing Room")
                .Anchored(25)
                .UseGO(175570, 6.0f)
                .Timeout(30000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 289;
            patch.add.push_back(DcRoster::MakeObjective(175570, 25, 289, "Viewing Room Door",
                150.0f, -60.0f, 40.0f, 10.0f, 175570, 289001));
            patches.push_back(std::move(patch));
        }

        // ---- Stratholme (329): Service Entrance gate ----
        out.push_back(
            EventBuilder(329001, 329, "Strath Service Gate")
                .Anchored(25)
                .UseGO(175351, 8.0f)
                .Timeout(30000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 329;
            patch.add.push_back(DcRoster::MakeObjective(175351, 25, 329, "Service Gate",
                3620.0f, -3350.0f, 130.0f, 10.0f, 175351, 329001));
            patches.push_back(std::move(patch));
        }

        // ---- Dire Maul (429): Conservatory door ----
        out.push_back(
            EventBuilder(429001, 429, "DM Conservatory")
                .Anchored(35)
                .UseGO(176907, 6.0f)
                .Timeout(30000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 429;
            patch.add.push_back(DcRoster::MakeObjective(176907, 35, 429, "Conservatory Door",
                -160.0f, -140.0f, 15.0f, 10.0f, 176907, 429001));
            patches.push_back(std::move(patch));
        }

        // ---- Maraudon (349): purple/orange path gate ----
        out.push_back(
            EventBuilder(349001, 349, "Maraudon Path Gate")
                .Anchored(45)
                .MoveTo(430.0f, 500.0f, -75.0f, 6.0f)
                .Timeout(30000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 349;
            patch.add.push_back(DcRoster::MakeObjective(0, 45, 349, "Path Gate",
                430.0f, 500.0f, -75.0f, 10.0f, 0, 349001));
            patches.push_back(std::move(patch));
        }
    }
}
