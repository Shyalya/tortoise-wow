/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- The Steamvault (map 545) — Main Chambers Door ------------------------
// The Main Chambers Door (GO 183049, at -95.56/-437.63/4.95) seals Warlord
// Kalithresh's chamber and is what the tank was parking on and stalling at: it
// is NOT opened by killing the two mini-bosses. Verified from the instance
// script (instance_steam_vault.cpp:41-96, go_main_chambers_access_panel):
//
//   * Each mini-boss has a "Main Chambers Access Panel" next to its own room:
//       Hydromancer Thespia   (17797, 88.40/-316.11)  -> panel 184125 ( 98.82/-316.34/-6.68)
//       Mekgineer Steamrigger (17796,-330.08/-121.51) -> panel 184126 (-332.35/-117.16/-6.71)
//   * Both panels spawn GO_FLAG_NOT_SELECTABLE (gameobject_template_addon
//     flags 48) and the flag is cleared only when that panel's boss dies
//     (SetBossState, instance_steam_vault.cpp:161-174).
//   * A player must then CLICK a panel. Its OnGossipHello checks whether BOTH
//     Thespia and Steamrigger are DONE (line 69) and, if so, the Coilfang Door
//     Controller (20926) rumbles and opens the door 4s later.
//
// So the door stays shut forever unless somebody clicks a panel — no bot ever
// did, and the door itself carries GO_FLAG_LOCKED + lock 1620, so the
// door-blocked force-open path can't budge it either. The party walked to a
// permanently shut gate, burned DoorBlockedTimeout, and auto-paused.
//
// Fix: an ANCHORED objective + event that clicks the panel. It is anchored
// (not conditional) because the framework's MoveTo is a short intra-room hop
// only — reaching a panel from wherever the last fight ended is a long haul
// that must be driven by the boss/objective navigation.
//
// WHICH panel: the derived roster runs the DBC encounter order — Thespia (0),
// Steamrigger (1), Kalithresh (2) — so the party is standing on Steamrigger's
// corpse when the door becomes openable, and HIS panel (184126) is ~5yd away.
// Anchoring there makes the detour a few steps instead of a lap of the ring.
// The objective borrows Kalithresh's encounterIndex (2); the roster's
// Objective-before-Boss tie-break sorts it ahead of him, i.e. immediately after
// Steamrigger.
//
// No WaitForGOState on the door: it is ~400yd from this panel, far outside a
// grid GO scan, so the step could never resolve it. The Wait covers the door
// controller's 4s delayed open, and the long walk south to Kalithresh covers
// the rest. Steps are MoveTo/UseGO/Wait — none is a rewind hazard, so the
// event does not need .Persistent().

namespace
{
    // Mekgineer Steamrigger's Main Chambers Access Panel.
    constexpr uint32 SV_ACCESS_PANEL_MEK = 184126;
    constexpr float SV_PANEL_MEK_X = -332.35f;
    constexpr float SV_PANEL_MEK_Y = -117.159f;
    constexpr float SV_PANEL_MEK_Z = -6.70766f;

    // Warlord Kalithresh's encounter slot — the objective shares it so it sorts
    // just before him.
    constexpr uint32 SV_KALITHRESH_ENCOUNTER = 2;
}

void RegisterSteamvaultEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(545, 1, "Open the Main Chambers Door")
                      .Anchored(/*orderIndex (doc)*/ 2)
                      .MoveTo(SV_PANEL_MEK_X, SV_PANEL_MEK_Y, SV_PANEL_MEK_Z, /*radius*/ 6.0f)
                      .UseGO(SV_ACCESS_PANEL_MEK, /*searchRadius*/ 14.0f)
                      // The door controller (20926) opens the gate 4s after the
                      // click; dwell past that so the event's success is visible
                      // in the log before the clear advances.
                      .Wait(6000)
                      .Build());
}

void RegisterSteamvaultRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = 545;
    p.add = {
        MakeObjective(OBJ(1), SV_KALITHRESH_ENCOUNTER, 545,
                      "Open the Main Chambers Door",
                      SV_PANEL_MEK_X, SV_PANEL_MEK_Y, SV_PANEL_MEK_Z,
                      /*arriveRadius*/ 8.0f, /*gateEntry*/ 0,
                      /*hook*/ 0, /*eventId*/ 1),
    };
    t.push_back(std::move(p));
}
