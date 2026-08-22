/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

// --- The Nexus (map 576) — free Keristrasza before she can be fought -------
//
// The Nexus is a hub and three spokes. The party zones in at (145.9,-10.6,-16.1)
// on the west side, crosses the central chamber, and clears Grand Magus Telestra
// (494.7,89.1) to the north-east, Anomalus (637.7,-289.1) to the east, and
// Ormorok the Tree-Shaper (265,-225.4) through the south-west garden. Every spoke
// returns through the hub, and DungeonEncounter.dbc already lists the three in
// that order (normal bits 0/1/2, heroic 1/2/3 behind the Frozen Commander's bit
// 0), so the derived roster's BOSSES are already in travel order.
//
// THE GATE. Keristrasza (26723) stands in the middle of the hub at
// (301.5,-5.5,-15.5) inside a frozen prison and CANNOT BE ATTACKED until three
// GameObjects are clicked — boss_keristrasza.cpp:
//
//     void RemovePrison(bool remove)
//     {
//         if (remove) { me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE); ... }
//         else        { me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE); ...    }
//     }
//     bool CanRemovePrison()
//     {
//         for (uint8 i = DATA_TELESTRA_ORB; i <= DATA_ORMOROK_ORB; ++i)
//             if (instance->GetBossState(i) != DONE)
//                 return false;
//         return true;
//     }
//
// The three GOs are the Containment Spheres, one per orb boss, all standing on
// the hub floor around her:
//
//     188526  Telestra's Containment Sphere   (281.9, -25.5, -16.9)
//     188527  Anomalus' Containment Sphere    (322.2,  14.7, -16.8)
//     188528  Ormorok's Containment Sphere    (281.8,  15.2, -16.6)
//
// Each is a GOOBER whose smart_scripts row fires on SMART_EVENT_GOSSIP_HELLO —
// which `GameObject::Use()` reaches through `AI()->GossipHello(player, false)` —
// and does three things: SMART_ACTION_SET_INST_DATA (setting the matching
// DATA_*_ORB boss state to DONE), despawn the Breath Caster (27048) holding that
// beam on the prison, and SMART_ACTION_SET_DATA on Keristrasza, whose
// `SetData` re-tests CanRemovePrison and drops UNIT_FLAG_NON_ATTACKABLE on the
// third click. Nothing else in the instance ever frees her, so without this the
// run reaches the hub, finds an unattackable boss, and stalls.
//
// THE FIX: three ordered travel objectives, one per sphere, each running a
// one-step UseGameObject. Boss navigation walks the tank to each sphere in turn
// and the step clicks it — the Sunken Temple Atal'ai statue idiom exactly, with
// the same 8yd arrive radius and 12yd GO search.
//
// WHY THREE OBJECTIVES AND NOT ONE THREE-STEP EVENT: the spheres are 40-57yd
// apart. An event step's own movement is a plain `HopTo` MovePoint, documented as
// a SHORT intra-room hop; a far haul belongs to a BOSS/OBJECTIVE anchor so the
// boss navigation and the dynamic-pull machinery drive it. Three anchors also put
// three legible rows in the `dc bosses` panel instead of one opaque one.
//
// ORDER. The spheres are only clickable once their own boss is dead —
// gameobject_template_addon flags all three GO_FLAG_NOT_SELECTABLE|NODESPAWN (48)
// and instance_nexus clears NOT_SELECTABLE from each in SetBossState — and
// `GameObject::Use()` early-returns on NOT_SELECTABLE, which the executor
// detects and holds on rather than latching a click that never happened. So all
// three objectives sort AFTER all three orb bosses and before Keristrasza. Their
// relative order is the shortest walk from the south corridor the party returns
// through from Ormorok: Telestra's sphere (the southern one), then Ormorok's,
// then Anomalus' across the hub — 81yd rather than the 98yd of any other order.
//
// REQUIRED, not Optional: unlike the Sunken Temple statues (a bonus wing) these
// gate the LAST BOSS. A sphere that will not click — a boss the human `dc skip`ped
// leaves its sphere NOT_SELECTABLE forever — must surface as a stall the human can
// act on, because the clear genuinely cannot finish from there.
//
// NOT persistent: one step each, so a Drive gap restarting at step 0 re-evaluates
// the same GO. Idempotent both ways — a clicked sphere is left GO_STATE_ACTIVE by
// the goober path (autoCloseTime 86400000), which the executor reads as already
// done, and even a duplicate click only re-sets an instance boss state that is
// already DONE.
//
// THE FROZEN COMMANDER (heroic only) is not an event but it IS a roster
// correction, in the second patch below. Map 576 is one of
// `InstanceScript::IsTwoFactionInstance()`'s maps, so `instance_nexus`
// UpdateEntry's the single spawn (guid 4764, entry 26796 Commander Stoutbeard,
// spawnMask 2, at (424.5,186,-34.9)) to 26798 Commander Kolurg for an ALLIANCE
// party. That breaks the derived roster in two independent ways:
//
//   1. NO KILL-BIT. `instance_encounters` has PRIMARY KEY (`entry`) — one credit
//      row per DungeonEncounter.dbc row — and the only row for encounter 519
//      ("Frozen Commander", heroic, encounterIndex 0) credits 26796. KillRewarder
//      passes the VICTIM'S CURRENT ENTRY to Map::UpdateEncounterState, so an
//      Alliance party killing 26798 matches nothing and DBC bit 0 never flips.
//      Unfixable in data (the schema forbids a second credit row), so completion
//      is read from the instance script's own DATA_COMMANDER_EVENT slot instead —
//      BossAI::JustDied sets it for either entry. See doneBossStateIndex.
//   2. NO LIVENESS. BossSpawnIndex stores 26796; every entry-keyed lookup
//      (BuildLiveness, GetLiveBoss, IsCreaturePresentOnMap) then misses on an
//      Alliance run. Advance crosses DC_BOSS_GRID_LOADED_RANGE, finds nothing,
//      and hard-stalls "not spawned" ~130yd short of aggro range — and because
//      the commander sorts FIRST, the whole run wedged at 0/5. Fixed on the other
//      side, in DcFactionEntrySwapRegistry (this roster table is static; the team
//      is a per-run fact, so DungeonBossesValue applies the rewrite).
//
// Order key 1 keeps it ahead of the 2..8 scale below, where its untouched bit 0
// already put it.
//
// The Crystalline Frayers of Ormorok's garden are NOT an event: they are a
// targeting question, and live in DcNeverTargetRegistry.

namespace
{
    constexpr uint32 NEX_MAP = 576;

    // The three Containment Spheres (gameobject_template 188526-188528).
    constexpr uint32 NEX_SPHERE_TELESTRA = 188526;
    constexpr uint32 NEX_SPHERE_ANOMALUS = 188527;
    constexpr uint32 NEX_SPHERE_ORMOROK  = 188528;

    // Sphere positions. Z is the navmesh floor under each GO (probed against the
    // real mmtiles: -16.50 / -16.47 / -16.35), not the GO's own Z — the models sit
    // a few tenths below the walkable surface and the anchor must be somewhere the
    // tank can actually stand. All three are on the open hub floor with mesh
    // directly underneath, so no offset is needed.
    constexpr float NEX_TELESTRA_X = 281.9f;
    constexpr float NEX_TELESTRA_Y = -25.5f;
    constexpr float NEX_TELESTRA_Z = -16.5f;

    constexpr float NEX_ORMOROK_X = 281.8f;
    constexpr float NEX_ORMOROK_Y = 15.2f;
    constexpr float NEX_ORMOROK_Z = -16.4f;

    constexpr float NEX_ANOMALUS_X = 322.2f;
    constexpr float NEX_ANOMALUS_Y = 14.7f;
    constexpr float NEX_ANOMALUS_Z = -16.5f;

    // Comfortably covers each sphere objective's 8yd arrive radius, and the three
    // spheres are 40-57yd apart so a 12yd search can never find the wrong one.
    // (They carry distinct entries anyway, so the bot-centred FindNearestGameObject
    // the step uses is unambiguous either way.)
    constexpr float NEX_SPHERE_SEARCH = 12.0f;
    constexpr float NEX_SPHERE_ARRIVE = 8.0f;

    // Walk-in plus one click. The 30s EventStepTimeout default is enough for the
    // last few yards, but the step also HOLDS (deliberately) on a sphere that is
    // still NOT_SELECTABLE, and 60s makes that read as a stall rather than a race
    // with a boss corpse whose SetBossState has only just landed.
    constexpr uint32 NEX_SPHERE_TIMEOUT = 60000;

    // Clear-order keys. The orb bosses keep their DBC kill-bits and are only
    // REORDERED onto this scale so the three sphere objectives have somewhere to
    // sit between Ormorok and Keristrasza (normal packs them into bits 2 and 3,
    // heroic into 3 and 4 — there is no integer in between on either difficulty).
    // Key 1 belongs to the heroic-only Frozen Commander (second patch below).
    constexpr int32 NEX_ORDER_TELESTRA        = 2;
    constexpr int32 NEX_ORDER_ANOMALUS        = 3;
    constexpr int32 NEX_ORDER_ORMOROK         = 4;
    constexpr int32 NEX_ORDER_SPHERE_TELESTRA = 5;
    constexpr int32 NEX_ORDER_SPHERE_ORMOROK  = 6;
    constexpr int32 NEX_ORDER_SPHERE_ANOMALUS = 7;
    constexpr int32 NEX_ORDER_KERISTRASZA     = 8;

    constexpr uint32 NEX_TELESTRA    = 26731;
    constexpr uint32 NEX_ANOMALUS    = 26763;
    constexpr uint32 NEX_ORMOROK     = 26794;
    constexpr uint32 NEX_KERISTRASZA = 26723;

    // Heroic-only bonus boss. 26796 is the entry the SPAWN carries (and therefore
    // the one BossSpawnIndex derives); an Alliance party sees it as 26798.
    constexpr uint32 NEX_COMMANDER = 26796;

    // The spawn's own coords, kept verbatim — the re-add drops the derived row so
    // they have to be restated here. Floor is -34.9 (the lower ring), not the
    // hub's -16; BOSS_SNAP_RADIUS pulls it onto the mesh either way.
    constexpr float NEX_COMMANDER_X = 424.55f;
    constexpr float NEX_COMMANDER_Y = 185.96f;
    constexpr float NEX_COMMANDER_Z = -34.94f;

    constexpr int32 NEX_ORDER_COMMANDER = 1;

    // DATA_COMMANDER_EVENT from nexus.h — the instance script's OWN boss-state
    // index space, authored explicitly here rather than borrowed from any DBC
    // encounterIndex (they do not align: bit 4 of the heroic mask is Keristrasza).
    constexpr int32 NEX_DATA_COMMANDER_EVENT = 4;
}

void RegisterNexusEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(NEX_MAP, 1, "Telestra's Containment Sphere")
                      .Anchored(/*orderIndex (doc)*/ NEX_ORDER_SPHERE_TELESTRA)
                      .UseGO(NEX_SPHERE_TELESTRA, NEX_SPHERE_SEARCH)
                          .Timeout(NEX_SPHERE_TIMEOUT)
                      .Build());

    out.push_back(EventBuilder(NEX_MAP, 2, "Ormorok's Containment Sphere")
                      .Anchored(/*orderIndex (doc)*/ NEX_ORDER_SPHERE_ORMOROK)
                      .UseGO(NEX_SPHERE_ORMOROK, NEX_SPHERE_SEARCH)
                          .Timeout(NEX_SPHERE_TIMEOUT)
                      .Build());

    out.push_back(EventBuilder(NEX_MAP, 3, "Anomalus' Containment Sphere")
                      .Anchored(/*orderIndex (doc)*/ NEX_ORDER_SPHERE_ANOMALUS)
                      .UseGO(NEX_SPHERE_ANOMALUS, NEX_SPHERE_SEARCH)
                          .Timeout(NEX_SPHERE_TIMEOUT)
                      .Build());
}

// --- roster patch ---------------------------------------------------------
void RegisterNexusRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    BossRosterPatch p;
    p.mapId = NEX_MAP;

    // Three sphere objectives, in walk order, all between Ormorok and
    // Keristrasza. An objective's `encounterIndex` is an ordering hint only (it
    // has no kill-bit and NextDungeonBossValue never tests the completion mask
    // for one), so it stays 0 and the clear orders by orderOverride.
    p.add = {
        MakeObjective(OBJ(1), /*encounterIndex*/ 0, NEX_MAP, "Telestra's Containment Sphere",
                      NEX_TELESTRA_X, NEX_TELESTRA_Y, NEX_TELESTRA_Z,
                      NEX_SPHERE_ARRIVE, /*gateEntry*/ 0, /*hook*/ 0,
                      /*eventId*/ 1, /*orderOverride*/ NEX_ORDER_SPHERE_TELESTRA),
        MakeObjective(OBJ(2), /*encounterIndex*/ 0, NEX_MAP, "Ormorok's Containment Sphere",
                      NEX_ORMOROK_X, NEX_ORMOROK_Y, NEX_ORMOROK_Z,
                      NEX_SPHERE_ARRIVE, /*gateEntry*/ 0, /*hook*/ 0,
                      /*eventId*/ 2, /*orderOverride*/ NEX_ORDER_SPHERE_ORMOROK),
        MakeObjective(OBJ(3), /*encounterIndex*/ 0, NEX_MAP, "Anomalus' Containment Sphere",
                      NEX_ANOMALUS_X, NEX_ANOMALUS_Y, NEX_ANOMALUS_Z,
                      NEX_SPHERE_ARRIVE, /*gateEntry*/ 0, /*hook*/ 0,
                      /*eventId*/ 3, /*orderOverride*/ NEX_ORDER_SPHERE_ANOMALUS),
    };

    // Slot the four real bosses onto the same scale so the spheres sort between
    // Ormorok and Keristrasza. Their DBC kill-bits are untouched — orderOverride
    // only moves the clear sequence — and their relative order is unchanged (it
    // already matched the travel path on both difficulties). The heroic-only
    // Frozen Commander is absent from THIS patch on purpose: it is not in the
    // derived normal roster, where a reorder row for it would be a silent no-op.
    // It gets its own HeroicOnly patch below.
    p.reorder = {
        { NEX_TELESTRA,    NEX_ORDER_TELESTRA    },
        { NEX_ANOMALUS,    NEX_ORDER_ANOMALUS    },
        { NEX_ORMOROK,     NEX_ORDER_ORMOROK     },
        { NEX_KERISTRASZA, NEX_ORDER_KERISTRASZA },
    };

    t.push_back(std::move(p));

    // --- heroic only: the Frozen Commander -------------------------------
    //
    // Remove the derived row and re-add it with a completion source that works
    // for BOTH factions' entries, plus the explicit order key its bit 0 gave it
    // implicitly. A bare `reorder` row cannot express this: completion is the
    // broken half, and only remove+re-add can set doneBossStateIndex.
    //
    // MakeBoss parks encounterIndex at 64 when doneBossStateIndex is set, so the
    // heroic mask's real bit 0 is never consulted for this anchor — on a Horde
    // run it WOULD flip correctly, but reading one source on one faction and
    // another on the other is how a divergence hides. Both read the boss-state
    // slot; both behave identically.
    //
    // The entry stays 26796 here. DungeonBossesValue rewrites it to 26798 for an
    // Alliance party, after this patch and before wing-filtering/snapping.
    BossRosterPatch heroic;
    heroic.mapId = NEX_MAP;
    heroic.gate = DcDifficultyGate::HeroicOnly;
    heroic.remove = { NEX_COMMANDER };
    heroic.add = {
        MakeBoss(NEX_COMMANDER, NEX_MAP, "Frozen Commander",
                 NEX_COMMANDER_X, NEX_COMMANDER_Y, NEX_COMMANDER_Z,
                 /*completionFrom*/ 0, /*orderOverride*/ NEX_ORDER_COMMANDER,
                 /*doneBossStateIndex*/ NEX_DATA_COMMANDER_EVENT),
    };

    t.push_back(std::move(heroic));
}
