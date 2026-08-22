/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"

#include "Creature.h"
#include "Player.h"

// --- The Underbog (map 546) — send Ghaz'an up to his platform --------------
//
// Ghaz'an (18105) spawns in the lake and swims DB waypoint path 1383920, whose
// every node sits between z 32 and z 47 — underwater. The only thing that ever
// moves him out is `at_underbog_ghazan` (areatrigger 4302, at 234.98/-379.28/
// 72.52 on the upper walkway): its ACTION_MOVE_TO_PLATFORM starts path 1383921,
// which climbs the ramp and ends on his platform at (256.28, -458.73, 81.37),
// where boss_ghazan stamps home and does MoveRandom(12).
//
// DcTestAreaTriggers relays that packet now, so on any route that crosses the
// volume he climbs exactly as he does for a real party. This event is the
// backstop for a route that does NOT: 4302's radius is 10yd, map 546 has no
// hand-authored WaypointHints, and the nearest trash to the trigger centre is
// 22yd away — so the party genuinely can path around it. Missing it would leave
// the boss anchor pointing at an empty platform.
//
// Same shape as the ZulFarrak Zum'rah and Shattered Halls Nethekurse fixes: a
// Conditional + Repeatable event whose predicate reads the exact signature (he
// is alive and still below the ramp) and whose Custom hook fires the SAME
// DoAction the areatrigger script fires. DoAction carries its own
// `_movedToPlatform` guard, so a relay that already worked makes this a no-op.
//
// The predicate is MAP-WIDE (DcTargeting::GetLiveBoss) rather than a proximity
// scan, and that is the load-bearing choice. Now that he has no pull-back row,
// DcAdvanceAction routes at his LIVE position — so if he only started climbing
// once the party was near, they would spend the ~30s climb chasing a boss that
// swims AWAY from them first (path 1383921 opens by heading out to
// (276.9, -457.9, 37.1), deeper water) before it turns up the ramp. Firing map-
// wide means the event trips while the party is still on Hungarfen and he is
// already parked on his platform by the time he becomes the route's target.
//
// Repeatable because a despawn/respawn reconstructs his AI with
// `_movedToPlatform` false and re-arms the deadlock.

// --- The Underbog (map 546) — two-hop drop past Ghaz'an, ANCHORED ----------
//
// Like The Slave Pens, the route onward from the second boss crosses a BREAK in
// the navmesh: after Ghaz'an (second boss, bit 1) the party must descend toward
// Swamplord Musel'ek (bit 2) down a tiered slope that sits on disconnected mesh
// islands boss-nav cannot route across. Here the drop is TWO hops, not one — a
// single teleport would land the party too far ahead, so the descent is split
// with a pause in between to keep the bots from outrunning real players.
//
// A roster OBJECTIVE anchor (BossRosterRegistry map 546) sits at the upper ledge
// (274.72, -462.60, 81.37), ordered between Ghaz'an and Swamplord (encounterIndex
// 2, Swamplord's bit — the Objective-before-Boss tie-break in Apply() sorts it
// ahead of him); boss-nav drives the tank to it. This event then:
//   1. TELEPORTS the whole party down to the mid landing (333.63, -471.46, 52.10),
//   2. WAITS 10s so the party doesn't get too far ahead of the players,
//   3. TELEPORTS the party down to the lower landing (355.71, -471.68, 24.32),
// after which boss-nav resumes toward Swamplord from solid, connected mesh.
//
// PERSISTENT (unlike the single-hop Slave Pens teleport). The first teleport
// relocates the leader ~60yd from the anchor, so a non-persistent event's
// at-objective trigger would go false the next tick (tank no longer near the
// anchor) and the Wait + second teleport would never run — worse, the >1s-gap
// restart would rewind the chain to step 0. A Persistent event keeps its progress
// and its at-objective trigger stays sticky once started (IsPersistentAnchored-
// EventActive), so the tank can sit at the mid landing through the pause and the
// second hop fires. Each TeleportParty is itself idempotent (Done immediately if
// the leader is already on its landing), so a tick-gap restart never re-teleports.

namespace
{
    constexpr uint32 UB_GHAZAN = 18105;
    // "Still down in the lake": his patrol tops out at z 46.8 and the ramp's
    // first dry node is z 74.7, so anywhere in that gap is unambiguous.
    constexpr float UB_GHAZAN_WATER_Z = 60.0f;
    constexpr uint32 UB_HOOK_SEND_GHAZAN_TO_PLATFORM = 10;  // ObjectiveHookRegistry id

    // True only while the deadlock signature holds: he is alive and still below
    // the ramp. Once he is climbing (or up) this reads false forever, so the
    // Repeatable can never spin.
    //
    // GetLiveBoss, NOT FindNearestCreature: it is MAP-WIDE, so this can fire
    // while the party is still on Hungarfen and give him the whole ~30s climb
    // before they ever look for him — a radius scan would only notice once they
    // were already close, which is exactly when it is too late to matter. It is
    // also O(1) through the cached live-boss GUID once he IS the current target;
    // before that it costs one entry-filtered store scan per tick, the same
    // order as the Nethekurse and Zum'rah predicates on their maps, and only on
    // map 546.
    bool UbGhazanStillInTheLake(Player* bot, AiObjectContext* context)
    {
        Creature* ghazan = DcTargeting::GetLiveBoss(bot, context, UB_GHAZAN);
        if (!ghazan || !ghazan->IsAlive())
            return false;

        return ghazan->GetPositionZ() < UB_GHAZAN_WATER_Z;
    }
}

void RegisterUnderbogEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(546, 2, "Send Ghaz'an up to his platform")
                      .Conditional(&UbGhazanStillInTheLake)
                      .Repeatable()
                      .PanelBeforeBoss(UB_GHAZAN)
                      .Custom(UB_HOOK_SEND_GHAZAN_TO_PLATFORM)
                      .Build());

    out.push_back(EventBuilder(546, 1, "Drop down past Ghaz'an")
                      .Anchored(/*orderIndex, doc-only*/ 2)
                      .Persistent()
                      // Hop 1: upper ledge -> mid landing.
                      .TeleportParty(/*ledge*/ 274.72f, -462.60f, 81.37f,
                                     /*mid landing*/ 333.63f, -471.46f, 52.10f)
                      // Pause so the party doesn't outrun the human players.
                      .Wait(10000)
                      // Hop 2: mid landing -> lower landing (the checkpoint here is
                      // the mid landing the prior hop left the party on).
                      .TeleportParty(/*mid landing*/ 333.63f, -471.46f, 52.10f,
                                     /*lower landing*/ 355.71f, -471.68f, 24.32f)
                      .Build());
}

// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterUnderbogRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- The Underbog (map 546) ----------------------------------
    // The auto-roster derives all four bosses (Hungarfen 17770 / bit 0,
    // Ghaz'an 18105 / bit 1, Swamplord Musel'ek 17826 / bit 2, The Black
    // Stalker 17882 / bit 3) from their static spawns.
    //
    // GHAZ'AN still needs his ANCHOR relocated, but for a much smaller reason
    // than before. The auto-roster derives an anchor from his STATIC SPAWN
    // (193.68, -425.00, 43.54), which is open water — every navmesh column in
    // that basin is a water surface sheet at z ~= 50.8 over a floor at z ~= 3.6,
    // so an anchor there sends boss navigation SWIMMING. But he does not FIGHT
    // there any more: the areatrigger relay (DcTestAreaTriggers) plus the
    // "Send Ghaz'an up to his platform" event above put him where the encounter
    // intends, on his platform at the end of waypoint path 1383921.
    //
    // So the anchor is simply HIS PLATFORM — the same thing a derived anchor
    // would be for any other boss, just taken from where he ends up rather than
    // where he spawns. Probed against the live mmaps: (256.28, -458.73) has
    // walkable mesh at z 81.45, his whole 12yd MoveRandom circle is on that same
    // deck, and the deck is the one boss-nav already drives the tank to for the
    // drop-down objective 18yd away at (274.72, -462.60, 81.37).
    //
    // He is an ORDINARY BOSS now — normal walk-in, normal tag, no pull-back row,
    // no force-aggro, no summon. See BossPullbackRegistry.cpp for what was
    // removed and why.
    //
    // remove + re-add (rather than `reorder`) because only remove+add can carry
    // hand-authored coordinates; completionFrom = his own entry keeps his real
    // DungeonEncounter kill-bit (1), resolved off the base list before the
    // removal takes effect (BossRosterRegistry::ApplyOne step 1).
    //
    // The path from Ghaz'an down to Swamplord additionally crosses a
    // navmesh BREAK — a tiered slope whose lower tiers sit on disconnected
    // mesh islands boss-nav can't route to.
    //
    // Add a travel OBJECTIVE at the upper ledge, ordered between Ghaz'an
    // and Swamplord. It borrows encounterIndex 2 (Swamplord's bit): the
    // Objective-before-Boss tie-break in Apply() sorts it AHEAD of
    // Swamplord at the shared key, so after Ghaz'an (bit 1) the tank
    // visits this objective and only then Swamplord. Sharing the bit is
    // safe — an objective is filtered by the cleared-anchor latch, never
    // the completion mask. Its eventId 1 (UnderbogEvents.cpp) does a TWO-
    // hop teleport (with a 5s pause between hops) down the break the
    // instant the tank reaches the ledge.
    {
        BossRosterPatch p;
        p.mapId = 546;
        p.remove = { 18105 };
        p.add = {
            // Ghaz'an, anchored on the platform his own script walks him to
            // (waypoint path 1383921, final node; mesh z probed at 81.45).
            // Keeps his own kill-bit via completionFrom.
            MakeBoss(18105, 546, "Ghaz'an",
                     256.28f, -458.73f, 81.45f, /*completionFrom*/ 18105),
            MakeObjective(OBJ(1), /*encounterIndex*/ 2, 546,
                          "Drop down past Ghaz'an",
                          274.72f, -462.60f, 81.37f,
                          /*arriveRadius*/ 6.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 1),
        };
        t.push_back(std::move(p));
    }
}
