// Scripted door/lever/escort sequences for the five original Classic
// "starter" instances. Each event is anchored to an objective slotted into
// the boss roster via a BossRosterPatch so DungeonBossesValue/NextBoss route
// the party to it in the right order, and DungeonClearRunEventAction walks
// the step list once the party arrives.
#include "Data/DungeonEventRegistry.h"

namespace ai
{
    void RegisterClassicStarterEvents(std::vector<DungeonEvent>& out, std::vector<BossRosterPatch>& patches)
    {
        // ---- Deadmines (36): Defias Cannon blasts open the Iron Clad Door ----
        out.push_back(
            EventBuilder(36001, 36, "Iron Clad Door (Defias Cannon)")
                .Anchored(5)
                .MoveTo(-108.0f, -594.0f, 36.0f, 6.0f)
                .UseGO(16398, 6.0f)          // Defias Cannon
                .WaitForGOState(16397, 0)    // Iron Clad Door
                .Custom("cannon volley")
                .Timeout(45000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 36;
            patch.add.push_back(DcRoster::MakeObjective(16397, 5, 36, "Iron Clad Door",
                -108.0f, -594.0f, 36.0f, 10.0f, 16397, 36001));
            patches.push_back(std::move(patch));
        }

        // ---- Shadowfang Keep (33): courtyard gate lever ----
        out.push_back(
            EventBuilder(33001, 33, "SFK Courtyard Gate")
                .Anchored(5)
                .MoveTo(-241.0f, 2125.0f, 81.0f, 5.0f)
                .UseGO(18900, 6.0f)
                .Timeout(30000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 33;
            patch.add.push_back(DcRoster::MakeObjective(18900, 5, 33, "Courtyard Gate",
                -241.0f, 2125.0f, 81.0f, 10.0f, 18900, 33001));
            patches.push_back(std::move(patch));
        }

        // ---- Wailing Caverns (43): Disciple of Naralex escort gate ----
        out.push_back(
            EventBuilder(43001, 43, "WC Disciple of Naralex")
                .Anchored(65)
                .TalkTo(3678, 8.0f)          // Disciple of Naralex
                .Wait(5000)
                .Timeout(120000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 43;
            patch.add.push_back(DcRoster::MakeObjective(3678, 65, 43, "Disciple of Naralex",
                -190.0f, 870.0f, -62.0f, 10.0f, 0, 43001));
            patches.push_back(std::move(patch));
        }

        // ---- Scarlet Monastery (189): Cathedral door ----
        out.push_back(
            EventBuilder(189001, 189, "SM Cathedral Door")
                .Anchored(55)
                .MoveTo(1940.0f, 442.0f, 33.0f, 6.0f)
                .UseGO(104591, 6.0f)
                .Timeout(30000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 189;
            patch.add.push_back(DcRoster::MakeObjective(104591, 55, 189, "Cathedral Door",
                1940.0f, 442.0f, 33.0f, 10.0f, 104591, 189001));
            patches.push_back(std::move(patch));
        }

        // ---- Zul'Farrak (209): Gahz'rilla gong ----
        out.push_back(
            EventBuilder(209001, 209, "ZF Gahz'rilla Gong")
                .Anchored(55)
                .MoveTo(1900.0f, 1100.0f, 9.0f, 8.0f)
                .UseGO(141832, 6.0f)         // Gong of Zul'Farrak
                .Wait(8000)
                .Timeout(60000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 209;
            patch.add.push_back(DcRoster::MakeObjective(141832, 55, 209, "Gong of Zul'Farrak",
                1900.0f, 1100.0f, 9.0f, 10.0f, 141832, 209001));
            patches.push_back(std::move(patch));
        }
    }
}
