// Scripted door/lever/escort sequences for the "extended" Classic instance
// tier. Same pattern as ClassicStarterEvents.cpp: one BossRosterPatch
// objective anchor per event, slotted at an encounterIndex between the
// bosses it gates.
#include "Data/DungeonEventRegistry.h"
#include "Data/DungeonWingRegistry.h"
#include "Objects/GameObject.h"
#include "Objects/Player.h"
#include <utility>

namespace
{
    bool IsClosedGameObject(Player* bot, uint32 entry, float range)
    {
        GameObject* go = bot ? bot->FindNearestGameObject(entry, range) : nullptr;
        return go && go->GetGoState() == GO_STATE_READY;
    }

    bool DireMaulCourtyardDoor(Player* bot, ai::AiObjectContext*)
    {
        return IsClosedGameObject(bot, 177219, 40.0f);
    }

    bool DireMaulInnerDoor(Player* bot, ai::AiObjectContext*)
    {
        return IsClosedGameObject(bot, 177217, 40.0f);
    }

    bool DireMaulCrescentDoorLower(Player* bot, ai::AiObjectContext*)
    {
        return IsClosedGameObject(bot, 177221, 40.0f);
    }

    bool DireMaulCrescentDoorUpper(Player* bot, ai::AiObjectContext*)
    {
        return IsClosedGameObject(bot, 179550, 40.0f);
    }
}

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
                .Anchored(40)
                // Ironbark opens the door after the gossip; the tank must stay
                // at his spawn while the NPC walks to the east wing door.
                .TalkTo(14241, 8.0f, 0)
                .WaitForGOState(176907, GO_STATE_ACTIVE_ALTERNATIVE, 90000, 250.0f)
                .Persistent()
                .Optional()
                .Timeout(120000)
                .Build());
        {
            BossRosterPatch patch;
            patch.mapId = 429;
            patch.add.push_back(DcRoster::MakeObjective(DungeonWingRegistry::ObjectiveEntry(1), 40, 429,
                "Ironbark the Redeemed (Conservatory Door)",
                -56.59f, -269.12f, -57.87f, 12.0f, 0, 429001, 40));
            patches.push_back(std::move(patch));
        }

        // West wing: the five crystal generators must be activated before
        // Immol'thar becomes attackable.  Each is a real travel objective so
        // boss navigation, rather than a short event MoveTo, handles the
        // distance between pylons.
        struct Pylon
        {
            uint32 sequence;
            uint32 gameObject;
            float x, y, z;
            int32 order;
        };
        Pylon const pylons[] = {
            {2, 177259,  12.94f, 277.93f,  -8.93f, 55},
            {3, 177257, -92.35f, 442.67f,  28.55f, 56},
            {4, 177258, 121.22f, 429.09f,  28.45f, 57},
            {5, 179504,  78.14f, 737.40f, -24.62f, 85},
            {6, 179505, -155.43f, 734.17f, -24.62f, 86}
        };
        for (Pylon const& pylon : pylons)
        {
            uint32 const eventId = 429100 + pylon.sequence;
            out.push_back(
                EventBuilder(eventId, 429, "Activate Dire Maul crystal generator")
                    .Anchored(static_cast<uint32>(pylon.order))
                    .ClearRadius(pylon.x, pylon.y, pylon.z, 40.0f, 15.0f, 120000)
                    .MoveTo(pylon.x, pylon.y, pylon.z, 6.0f)
                    .UseGO(pylon.gameObject, 12.0f)
                    .Wait(6000)
                    .Persistent()
                    .Optional()
                    .Timeout(180000)
                    .Build());

            BossRosterPatch patch;
            patch.mapId = 429;
            patch.add.push_back(DcRoster::MakeObjective(
                DungeonWingRegistry::ObjectiveEntry(pylon.sequence),
                static_cast<uint32>(pylon.order), 429,
                "Dire Maul crystal generator", pylon.x, pylon.y, pylon.z,
                45.0f, 0, eventId, pylon.order));
            patches.push_back(std::move(patch));
        }

        // The West entrance room is too large for one clear radius.  Tile it
        // with short, ordered sweeps so the party clears the treant packs
        // without waking the higher-level Eldreth/Highborne area.
        struct Sweep
        {
            uint32 sequence;
            float x, y, z, radius, zBand;
            int32 order;
        };
        Sweep const sweeps[] = {
            {8,  -97.0f, 202.0f, -4.0f, 45.0f, 20.0f, 45},
            {9,  -15.0f, 192.0f, -3.5f, 45.0f, 20.0f, 45},
            {10, 128.0f, 200.0f, -4.0f, 45.0f, 20.0f, 45},
            {11, -44.0f, 280.0f, -7.5f, 48.0f, 20.0f, 46},
            {12,  60.0f, 285.0f, -8.0f, 45.0f, 20.0f, 46},
            {13, -93.0f, 357.0f, -4.0f, 42.0f, 20.0f, 47},
            {14, 126.0f, 357.0f, -4.0f, 42.0f, 20.0f, 47}
        };
        for (Sweep const& sweep : sweeps)
        {
            uint32 const eventId = 429200 + sweep.sequence;
            out.push_back(
                EventBuilder(eventId, 429, "Clear Dire Maul West entrance")
                    .Anchored(static_cast<uint32>(sweep.order))
                    .ClearRadius(sweep.x, sweep.y, sweep.z, sweep.radius, sweep.zBand, 180000)
                    .Persistent()
                    .Optional()
                    .Timeout(240000)
                    .Build());

            BossRosterPatch patch;
            patch.mapId = 429;
            patch.add.push_back(DcRoster::MakeObjective(
                DungeonWingRegistry::ObjectiveEntry(sweep.sequence),
                static_cast<uint32>(sweep.order), 429,
                "Clear Dire Maul West entrance", sweep.x, sweep.y, sweep.z,
                30.0f, 0, eventId, sweep.order));
            patches.push_back(std::move(patch));
        }

        // ---- Dire Maul (429): on-path doors -------------------------------
        // These doors cannot be represented as travel objectives: the closed
        // door truncates navigation before the objective.  Conditional events
        // preempt the normal door stall when the GO is actually in reach.
        {
            out.push_back(
                EventBuilder(429002, 429, "Open Gordok Courtyard Door")
                    .Conditional(&DireMaulCourtyardDoor)
                    .UseGO(177219, 25.0f)
                    .WaitForGOState(177219, GO_STATE_ACTIVE, 30000, 25.0f)
                    .Optional()
                    .Timeout(30000)
                    .Build());
            out.push_back(
                EventBuilder(429003, 429, "Open Gordok Inner Door")
                    .Conditional(&DireMaulInnerDoor)
                    .UseGO(177217, 25.0f)
                    .WaitForGOState(177217, GO_STATE_ACTIVE, 30000, 25.0f)
                    .Optional()
                    .Timeout(30000)
                    .Build());
            out.push_back(
                EventBuilder(429009, 429, "Open Crescent Door (Lower)")
                    .Conditional(&DireMaulCrescentDoorLower)
                    .UseGO(177221, 25.0f)
                    .WaitForGOState(177221, GO_STATE_ACTIVE, 30000, 25.0f)
                    .Optional()
                    .Timeout(30000)
                    .Build());
            out.push_back(
                EventBuilder(429010, 429, "Open Crescent Door (Upper)")
                    .Conditional(&DireMaulCrescentDoorUpper)
                    .UseGO(179550, 25.0f)
                    .WaitForGOState(179550, GO_STATE_ACTIVE, 30000, 25.0f)
                    .Optional()
                    .Timeout(30000)
                    .Build());
        }

        // Maraudon is connected internally.  It has wing labels for status,
        // but no artificial path-gate event; remove the unreachable Rotgrip
        // anchor instead of pretending a movement-only event completed it.
        {
            BossRosterPatch patch;
            patch.mapId = 349;
            patch.removeEntries.push_back(13596);
            patches.push_back(std::move(patch));
        }
    }
}
