/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "GameObject.h"
#include "InstanceScript.h"
#include "Player.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"

// --- Razorfen Downs (map 129) — the GONG, CONDITIONAL + REPEATABLE --------
// The first thing a party does in RFD: ring the gong (GO 148917) by the
// entrance to summon Tuten'kash. The encounter is staged — each ring summons a
// wave (Tomb Fiends, then Tomb Reavers) and after the THIRD ring Tuten'kash
// himself spawns. The gong's SmartAI disables it (GO_FLAG_NOT_SELECTABLE) the
// instant it is rung and re-enables it only once the wave's deaths tick its
// kill-counter — so the gong is self-pacing: a bot just rings it whenever it is
// selectable again, and the server decides when the next wave (or the boss)
// appears.
//
// Navigation is done by Tuten'kash's BOSS anchor, which sits ON the gong
// (BossRosterRegistry): the tank travels to the gong exactly as it would to any
// boss — the long-range pathfinder, the dynamic-pull camps and the combat
// handling all drive it, which a bespoke event move cannot (an event-driven
// long haul fights the pull camp and never resets it — the live failure that
// sank the earlier LongMoveTo). The event therefore needs NO movement of its
// own; it only rings.
//
// The gong is rung in place. Condition 4 gates the ring on the tank being AT
// the gong (within range), the gong being selectable, and Tuten'kash absent —
// so it fires only once boss-nav has delivered the tank, preempting the
// boss-not-present stall (relevance 31 > 20). The anchor and the ring target are
// the SAME point (the gong), so there is no tug-of-war between boss-nav and the
// event.
//
// REPEATABLE so it re-fires for each of the three rings instead of latching
// after the first; the gong's own selectable flag (condition 4) paces the rings
// and Tuten'kash being summoned ends the loop (the 3rd ring leaves the gong
// NOT_SELECTABLE, so condition 4 reads false). Rendered first in the panel
// (PanelBeforeBoss 7355) since it precedes boss #0.
//
// --- "Approach Tuten'kash" (condition 11) — close the last gap to him ------
// The ring phase parks the tank AT the gong. But Tuten'kash does NOT spawn at
// the gong: creature_summon_groups summons him ~83yd off (2487.94, 804.22) and
// his SmartAI ("On Reset - Move Point") then walks him to a fixed combat spot
// (2515.71, 854.81, 47.68) — which is still ~37yd from the gong, OUTSIDE his
// aggro radius. So once he settles the party stands at the gong, he stands at
// his spot, and nobody aggros: the run deadlocks ("holding for at-boss",
// live=0, forever).
//
// The boss flow can't recover this on its own. He is a TempSummon (no spawnId),
// so GetLiveBoss / FindLiveCreatureOnMap — which scan the map's spawn-id store
// (DB spawns only) — never see him; live-boss tracking reads him as absent and
// the tank holds on the static gong anchor. We don't need to TRACK him, though;
// we just need to walk to where he goes. A second conditional event fires the
// instant he is summoned (a grid FindNearestCreature DOES see a summon, unlike
// the spawn-store scan) and MoveTos the party onto his combat spot, putting them
// in aggro range. REPEATABLE so the completed step holds the tank planted there
// (DcRunEventAction returns true on a repeatable completion) until aggro lands
// and the combat engine takes over — and so it self-heals if he ever evades and
// walks back to reset. The condition reads false once his encounter bit is set
// (killed), ending the loop. Folded under Tuten'kash in the panel (he gates it).

namespace
{
    bool RfdGong(Player* bot, AiObjectContext* context);
    bool RfdApproach(Player* bot, AiObjectContext* context);
}

void RegisterRazorfenDownsEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(129, 1, "Ring the Gong")
                      .Conditional(&RfdGong)
                      .Repeatable()
                      .PanelBeforeBoss(/*Tuten'kash*/ 7355)
                      // searchRadius must cover condition 4's ring range (30yd)
                      // so a tank parked at the far edge still finds the gong and
                      // HopTos the last yards to Use() it.
                      .UseGO(/*gong*/ 148917, /*searchRadius*/ 35.0f)
                      .Build());

    out.push_back(EventBuilder(129, 2, "Approach Tuten'kash")
                      .Conditional(&RfdApproach)
                      .Repeatable()
                      .PanelBeforeBoss(/*Tuten'kash*/ 7355)
                      // His SmartAI "On Reset - Move Point" target — the spot he
                      // walks to and idles at. A tight arrival radius walks the
                      // tank essentially onto it so aggro is guaranteed even if his
                      // radius is small or he is still en route from the summon
                      // spot when the tank arrives.
                      .MoveTo(/*combat spot*/ 2515.71f, 854.81f, 47.68f, /*radius*/ 6.0f)
                      .Build());
}

// --- The gong activation condition (condition 4, repeatable) --------------
// The gong (GO 148917) summons Tuten'kash in three rings, a wave of trash
// between each. The gong's SmartAI flips it NOT_SELECTABLE the instant it is
// rung and re-enables it only once that wave's deaths tick its kill-counter, so
// it paces itself. This condition is the gate for the REPEATABLE "Ring the Gong"
// event: DUE while the party should ring again.
//
// Travel to the gong is NOT the event's job — Tuten'kash's boss anchor sits on
// the gong, so the boss navigation (pathfinder + dynamic-pull camps + combat)
// delivers the tank there exactly as to any boss. This gate therefore also
// requires the tank to already be AT the gong: that way the event fires only
// after boss-nav has arrived (preempting the boss-not-present stall to ring),
// and never hijacks the long approach with an event-driven move.
namespace
{
    constexpr uint32 RFD_GONG = 148917;
    constexpr uint32 RFD_TUTENKASH = 7355;
    // Tuten'kash is DungeonEncounter bit 0 (the map's first encounter), so his
    // kill flips bit 0 of the completed-encounter mask — the authoritative "the
    // gong event is finished" signal even after his corpse despawns.
    constexpr uint32 RFD_TUTENKASH_BIT = 0;
    // How close the tank must be to the gong for the ring to fire. Comfortably
    // larger than the boss engage range so the event takes over the instant
    // boss-nav has parked the tank at the gong, before the stall escalates.
    constexpr float RFD_GONG_RING_RANGE = 30.0f;
    // How far the "Approach Tuten'kash" condition scans for the summoned boss.
    // He spawns ~83yd from the gong (where the tank rings), so this must reach
    // that far to detect him at his summon spot; the grid around the party is
    // loaded out to there (it rang the gong that summoned him).
    constexpr float RFD_TUTENKASH_SCAN = 100.0f;

    bool RfdGong(Player* bot, AiObjectContext* /*context*/)
    {
        // Done for good once Tuten'kash is up (the 3rd ring summoned him — let
        // the boss flow take him) or already killed (his encounter bit is set).
        if (DcTargeting::FindLiveCreatureOnMap(bot, RFD_TUTENKASH))
            return false;
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        if (inst && (inst->GetCompletedEncounterMask() & (1u << RFD_TUTENKASH_BIT)))
            return false;

        // The gong must exist, be selectable (the previous wave is dead and its
        // SmartAI re-armed it) and shut. A NOT_SELECTABLE / already-activated
        // gong means a wave is still up — let combat finish before the next ring.
        GameObject* gong = bot->FindNearestGameObject(RFD_GONG, 200.0f);
        if (!gong)
            return false;
        if (gong->HasGameObjectFlag(GO_FLAG_NOT_SELECTABLE) ||
            gong->GetGoState() != GO_STATE_READY)
            return false;

        // Only ring once boss-nav has parked the tank at the gong — otherwise the
        // event would preempt the boss travel partway and try to drive it itself.
        if (!bot->IsWithinDist(gong, RFD_GONG_RING_RANGE))
            return false;

        return true;
    }

    // --- the approach gate (condition 11, repeatable) ---------------------
    // DUE while Tuten'kash has been summoned (the 3rd ring is rung) but is not
    // yet killed: drive the party from the gong onto his combat spot so they
    // enter aggro range. Reads false before he is up (the gong event still owns
    // the ring phase) and once his encounter bit is set (dead) — the two gates
    // (gong selectable vs boss present) are mutually exclusive, so the gong and
    // approach events never both fire.
    bool RfdApproach(Player* bot, AiObjectContext* /*context*/)
    {
        // Done for good once he is killed — his kill flips encounter bit 0, the
        // signal that survives even after his corpse despawns.
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        if (inst && (inst->GetCompletedEncounterMask() & (1u << RFD_TUTENKASH_BIT)))
            return false;

        // Fire only once the 3rd ring has SUMMONED him. He is a
        // creature_summon_groups TempSummon (no spawnId), invisible to the
        // map-wide spawn-store scan (FindLiveCreatureOnMap) — but a grid
        // FindNearestCreature DOES see a summon, which is all we need to know
        // he is up. Until then the gong event owns the ring phase; stand down.
        return bot->FindNearestCreature(RFD_TUTENKASH, RFD_TUTENKASH_SCAN,
                                        /*alive*/ true) != nullptr;
    }
}

// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterRazorfenDownsRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- Razorfen Downs (map 129) --------------------------------
    // Tuten'kash is the first encounter (DungeonEncounter bit 0) but he has
    // NO static creature spawn — he is summoned only after the party rings
    // the entrance gong three times (see the "Ring the Gong" conditional
    // event + condition 4). With no spawn, BossSpawnIndex never emits him,
    // so without this he would never head the list, be travelled to, or be
    // tracked. Add him with his real encounterIndex 0 (default) so his kill
    // flips bit 0 the moment he spawns.
    //
    // The anchor sits ON THE GONG (148917 @ 2552,857), NOT his summon spot.
    // This is deliberate: it makes the boss navigation drive the tank to
    // the gong exactly as to any boss — long-range pathfinder, dynamic-pull
    // camps, combat — which is the robust travel an event step cannot do.
    // The gong event then rings in place. While he is absent the tank
    // arrives at the gong and the event (relevance 31) rings, preempting the
    // boss-not-present stall (relevance 20); once the third ring summons him
    // at his real spot (~80yd off) live-boss tracking retargets the engage
    // to his actual position.
    {
        BossRosterPatch p;
        p.mapId = 129;
        p.add = {
            MakeBoss(7355, 129, "Tuten'kash",
                     /*on the gong*/ 2552.44f, 856.984f, 51.495f, /*completionFrom*/ 0),
        };
        t.push_back(std::move(p));
    }
}
