/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "Creature.h"
#include "GameObject.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "SharedDefines.h"

// --- Blackfathom Deeps (map 48) - the Fires of Aku'mai --------------------
// Aku'mai sits behind the Portal of Aku'Mai (GO 21117) and the party opens it
// by lighting four braziers. The mechanic is read out of the core's OWN
// instance script (src/scripts/dungeons/blackfathom_deeps/
// instance_blackfathom_deeps.cpp: go_fire_of_akumaiAI::OnUse plus
// instance_blackfathom_deeps::SetData/Update/IsWaveEventFinished), not from a
// wiki, because three of its details decide the step list outright:
//
//   1. OnUse REFUSES while Kelris lives - `if (GetData(TYPE_KELRIS) != DONE)
//      return false;`. Lighting anything before Twilight Lord Kelris (4832) is
//      a silent no-op, so this event is anchored behind him (key 7 of 8).
//   2. Each brazier is single-use: OnUse sets GO_STATE_ACTIVE and stamps
//      GO_FLAG_NO_INTERACT on itself. A second Use() can never land - which is
//      what keeps the script's `ASSERT(m_uiShrinesLit < 4)` out of reach, so a
//      re-driven UseGO step cannot take the world down with it.
//   3. The door opens only on IsWaveEventFinished(): all four lit AND every
//      summoned wave mob dead. Each light arms a wave 3s later (DoSpawnMobs),
//      summoned about 50yd east around x=-769 with SetInCombatWithZone() and
//      then WALKING IN to Kelris' spawn point - that is, straight onto the
//      party standing at the braziers. Hence light, kill, light, kill: a
//      ClearRadius after every brazier, entry-filtered to the four wave mobs
//      so the sweep cannot wander off into unrelated trash.
//
// And one hard deadline. The portal is a DOOR with autoCloseTime 85, and
// DoUseDoorOrButton is called exactly once, from the wave-finished branch:
// GameObject::UseDoorOrButton stamps `m_cooldownTime = time(nullptr) + 85`, so
// it shuts again 85 seconds after it opens and no path in the script reopens
// it. The event therefore does not hand back at the braziers - it ends by
// walking the party INTO the doorway, so a post-fight loot-and-drink cannot
// eat the window and strand the run in front of a closed portal.
namespace
{
    constexpr uint32 BFD_MAP = 48;

    // Clear-order key. 1..6 are the bosses up to Kelris and 8 is Aku'mai; see
    // DC_BOSS_ORDER_1121 for map 48, which leaves 7 free for this.
    constexpr uint32 BFD_ORDER_SHRINES = 7;

    constexpr uint32 BFD_GO_PORTAL_DOOR = 21117;

    // The four braziers, walked as a loop around their square. They stand
    // 10-12yd apart, so the whole circuit is short - what costs time is the
    // wave between each pair, not the walking.
    struct BfdShrine
    {
        uint32 goEntry;
        float x, y, z;
    };

    constexpr BfdShrine BFD_SHRINES[] = {
        { 21118, -813.5f, -158.5f, -24.5f },
        { 21119, -813.6f, -170.5f, -24.5f },
        { 21120, -824.0f, -170.4f, -24.5f },
        { 21121, -823.9f, -158.5f, -24.5f },
    };

    // Centre of that square - the sweep anchor for every wave.
    constexpr float BFD_SQUARE_X = -818.75f;
    constexpr float BFD_SQUARE_Y = -164.50f;
    constexpr float BFD_SQUARE_Z = -24.50f;

    // Reaches the summon line at x=-769 (about 51yd out) with margin, and the
    // vertical band covers the brazier floor at -24.5 down to the summon
    // points at -25.9. The entry filter is what keeps it honest at this size.
    constexpr float BFD_SWEEP_RADIUS = 70.0f;
    constexpr float BFD_SWEEP_ZBAND = 25.0f;
    constexpr uint32 BFD_WAVE_TIMEOUT = 120000;

    // Aku'mai Servant / Aku'mai Snapjaw / Murkshallow Snapclaw / Murkshallow
    // Softshell - the four entries DoSpawnMobs draws its waves from.
    constexpr uint32 BFD_WAVE_SERVANT = 4978;
    constexpr uint32 BFD_WAVE_SNAPJAW = 4825;
    constexpr uint32 BFD_WAVE_SNAPCLAW = 4815;
    constexpr uint32 BFD_WAVE_SOFTSHELL = 4977;

    // The doorway itself. Probed on the navmesh (tools/meshprobe.cpp): the
    // portal sits on mesh at z=-25.8, and from Kelris the path through to
    // Aku'mai is complete - 65 polygons, no gap. Standing in the doorway is
    // therefore a real position to hold, not a guess.
    constexpr float BFD_PORTAL_X = -818.4f;
    constexpr float BFD_PORTAL_Y = -200.6f;
    constexpr float BFD_PORTAL_Z = -25.8f;
}

void RegisterBlackfathomDeepsEvents(std::vector<DungeonEvent>& out)
{
    EventBuilder b(BFD_MAP, 1, "Light the Fires of Aku'mai");

    // Persistent: this spans four separate fights with quiet gaps between
    // them, and a tick-gap restart must not send the party back to a brazier
    // it already lit (that Use() would be refused anyway, but the walk would
    // still be spent).
    b.Anchored(/*orderIndex*/ BFD_ORDER_SHRINES).Persistent();

    for (BfdShrine const& s : BFD_SHRINES)
    {
        b.MoveTo(s.x, s.y, s.z, /*radius*/ 5.0f)
            .UseGO(s.goEntry, /*searchRadius*/ 12.0f)
            .ClearRadius(BFD_SQUARE_X, BFD_SQUARE_Y, BFD_SQUARE_Z,
                         BFD_SWEEP_RADIUS, BFD_SWEEP_ZBAND)
                .OnlyEntries({ BFD_WAVE_SERVANT, BFD_WAVE_SNAPJAW,
                               BFD_WAVE_SNAPCLAW, BFD_WAVE_SOFTSHELL })
                .Timeout(BFD_WAVE_TIMEOUT);
    }

    // The fourth wave dying is what fires DoUseDoorOrButton, so the door opens
    // within a second of the sweep finishing; 30s is slack, not a wait.
    // No WaitForGOState on the portal here. A step that times out is not a
    // patient wait - the executor turns it into StepResult::Failed and the
    // whole event dies. The door is reported to close visually while still
    // being passable, and in that case the gate would cost the run for a
    // state flag while the way is open. It buys little either: the instance
    // script opens the door for certain once Kelris and all four fires are
    // done, so by the time this step ran it is open anyway.
    // NO closing walk to the portal. It used to be
    //     b.MoveTo(BFD_PORTAL_X, BFD_PORTAL_Y, BFD_PORTAL_Z, 10.0f);
    // which is 36yd from the brazier square - and MoveTo is a SHORT intra-room
    // hop on a plain MovePoint, with no pathfinding at all (see EventStepKind).
    // The first party ever to light all four fires wedged on exactly this step
    // (STALLED at step 12/13 kind 0) with everything before it done. The haul to
    // Aku'mai belongs to the boss navigation, which the ladder starts the moment
    // this event completes.

    // Not Optional. Skipping it would only walk the party to a portal that
    // stays shut, so a failure here has to read as a stall, not as progress.
    out.push_back(b.Build());
}

// --- roster patch ---------------------------------------------------------
void RegisterBlackfathomDeepsRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = BFD_MAP;

    // One objective, sitting between Kelris (6) and Aku'mai (8), anchored at
    // the centre of the brazier square so boss-nav travels the tank in there
    // before the first Use(). encounterIndex carries the ordering because on
    // map 48 the bosses are keyed through DC_BOSS_ORDER_1121; an objective has
    // no kill-bit of its own, so this is ordering only.
    p.add = {
        MakeObjective(OBJ(1), /*encounterIndex*/ BFD_ORDER_SHRINES, BFD_MAP,
                      "Light the four Fires of Aku'mai",
                      BFD_SQUARE_X, BFD_SQUARE_Y, BFD_SQUARE_Z,
                      /*arriveRadius*/ 10.0f, /*gateEntry*/ 0, /*hook*/ 0,
                      /*eventId*/ 1),
    };

    t.push_back(std::move(p));
}
