/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"

#include "GameObject.h"
#include "InstanceScript.h"
#include "Player.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"

// --- Blood Furnace (map 542) — Broggok's cell-door event ------------------
// Broggok sits at the SOUTH end of his hall (455.34, -1.83, 9.63), walled off
// behind a rear gate (GO 181819, y=34.15) that stays shut until the event is
// finished — so boss-nav alone can never reach him. The trigger is the "Cell
// Door Lever" (GO 181982) at the party's staging point (456.56, 54.35, 9.62),
// midway up the hall:
//
//   1. Pulling the lever (go_broggok_lever::OnGossipHello) flips Broggok's boss
//      state NOT_STARTED -> IN_PROGRESS and opens the first of four flanking
//      prison cells. Each cell's Nascent Fel Orcs (17398) SetInCombatWithZone,
//      so they come to the party; clearing one cell opens the next.
//   2. After the FOURTH cell is cleared the instance script opens the rear gate
//      and fires ACTION_ACTIVATE_BROGGOK — Broggok drops REACT_PASSIVE /
//      UNIT_FLAG_NON_ATTACKABLE and the party can finally engage him.
//
// Two CONDITIONAL + REPEATABLE events cover this; boss-nav (Broggok auto-derives
// as a boss anchor from his static spawn) drives the long approach, exactly like
// Razorfen Downs' gong:
//
//   * "Pull the Cell Door Lever" (condition 17) fires once boss-nav has parked
//     the tank near the lever and the lever is still shut — it Use()s the lever
//     to start the waves. Its gate (boss state NOT_STARTED) reads false the
//     instant the lever is pulled, so it never double-fires.
//   * "Hold the cell door" (condition 18) keeps the tank planted AT the lever
//     through the wave phase (boss state IN_PROGRESS while the rear gate is
//     still shut). The orcs come to the held tank and the combat engine kills
//     them; the event engine only drives in the brief out-of-combat gaps
//     between cells, which is exactly when the tank would otherwise wander down
//     to bang on the closed rear gate. Once the 4th cell opens the gate, this
//     reads false and boss-nav walks the tank through to the now-attackable
//     Broggok. Both events are folded under Broggok in the panel.

namespace
{
    constexpr uint32 BF_BROGGOK = 17380;         // boss creature (auto-anchored)
    constexpr uint32 BF_LEVER = 181982;          // "Cell Door Lever" (the trigger)
    constexpr uint32 BF_REAR_GATE = 181819;      // opens after the 4th wave
    // DATA_BROGGOK in instance_blood_furnace — the SCRIPT boss-state index (not a
    // DBC encounter bit). Used only to read the NOT_STARTED / IN_PROGRESS phase
    // the completed-encounter mask can't express; killed is read as DONE here too
    // (the instance is always loaded during a run, so GetBossState is authoritative).
    constexpr uint32 BF_DATA_BROGGOK = 1;
    // How close the tank must be to the lever for the pull to fire — comfortably
    // larger than the boss engage range so the event takes over the instant
    // boss-nav parks the tank by the lever, before it descends to the rear gate.
    constexpr float BF_LEVER_RANGE = 30.0f;
    // Grid scan radius for the rear gate from the lever staging spot (~20yd away).
    constexpr float BF_GATE_SCAN = 60.0f;

    // --- condition 17: pull the lever -------------------------------------
    // DUE while the lever has not yet been pulled (boss state NOT_STARTED) and
    // the tank is at the lever. ACTION_PREPARE_BROGGOK flips the state to
    // IN_PROGRESS the instant the lever is used, so this reads false for the rest
    // of the run — one pull, never a re-pull.
    bool BfPullLever(Player* bot, AiObjectContext* /*context*/)
    {
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        if (!inst)
            return false;
        if (inst->GetBossState(BF_DATA_BROGGOK) != NOT_STARTED)
            return false;

        // The lever must exist, be shut (GO_STATE_READY) and selectable.
        GameObject* lever = bot->FindNearestGameObject(BF_LEVER, 200.0f);
        if (!lever)
            return false;
        if (lever->HasGameObjectFlag(GO_FLAG_NOT_SELECTABLE) ||
            lever->GetGoState() != GO_STATE_READY)
            return false;

        // Only once boss-nav has parked the tank at the lever — otherwise the
        // event would preempt the boss approach partway and try to drive it.
        return bot->IsWithinDist(lever, BF_LEVER_RANGE);
    }

    // --- condition 18: hold through the waves ------------------------------
    // DUE during the wave phase: the lever has been pulled (IN_PROGRESS) and the
    // rear gate is still shut. The gate (GO 181819) is the authoritative wave
    // signal — the instance script opens it the instant the 4th cell is cleared,
    // at the same moment it makes Broggok attackable. While it is shut, hold the
    // tank at the lever; once it opens, stand down so boss-nav walks the tank
    // through to engage Broggok. Reads false before the lever is pulled (the pull
    // event owns that phase) and once Broggok is killed (state DONE).
    bool BfHoldCellDoor(Player* bot, AiObjectContext* /*context*/)
    {
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        if (!inst)
            return false;
        if (inst->GetBossState(BF_DATA_BROGGOK) != IN_PROGRESS)
            return false;

        // Hold only while the rear gate is still closed. Out of range / gone =>
        // let boss-nav take over (the gate is ~20yd from the held tank, so the
        // scan never legitimately misses it during the wave phase).
        GameObject* gate = bot->FindNearestGameObject(BF_REAR_GATE, BF_GATE_SCAN);
        if (!gate)
            return false;
        return gate->GetGoState() == GO_STATE_READY;
    }
}

namespace
{
    bool BfPullLever(Player* bot, AiObjectContext* context);
    bool BfHoldCellDoor(Player* bot, AiObjectContext* context);
}

void RegisterBloodFurnaceEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(542, 1, "Pull the Cell Door Lever")
                      .Conditional(&BfPullLever)
                      .Repeatable()
                      .PanelBeforeBoss(/*Broggok*/ BF_BROGGOK)
                      // searchRadius covers condition 17's lever range (30yd) so a
                      // tank parked at the far edge still finds the lever and
                      // HopTos the last yards to Use() it.
                      .UseGO(BF_LEVER, /*searchRadius*/ 35.0f)
                      .Build());

    out.push_back(EventBuilder(542, 2, "Hold the cell door")
                      .Conditional(&BfHoldCellDoor)
                      .Repeatable()
                      .PanelBeforeBoss(/*Broggok*/ BF_BROGGOK)
                      // Plant the tank on the lever staging spot so the cell orcs
                      // come to it and it never drifts down onto the shut rear
                      // gate between waves. A tight arrival radius keeps it put.
                      .MoveTo(/*lever spot*/ 456.56f, 54.35f, 9.62f, /*radius*/ 6.0f)
                      .Build());
}

