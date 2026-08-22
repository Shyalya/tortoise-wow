/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "AiObjectContext.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectData.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "Timer.h"

#include <atomic>
#include <optional>
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

// --- Uldaman (map 70) — the Ironaya seal, CONDITIONAL --------------------
// Ironaya (creature 7228, DungeonEncounter bit 2) sits behind the Seal of
// Khaz'Mul (GO 124372, lockId 0), which is opened only by interacting with the
// Keystone (GO 124371). Verified mechanic (DB + DBC + core, 2026-06-17):
//
//   * The keystone's Lock.dbc lock 359 = ITEM slot, item 7733 "Staff of
//     Prehistoria" — the player normally clicks the keystone holding the staff.
//   * The keystone runs SmartAI on SMART_EVENT_GOSSIP_HELLO (smart_scripts,
//     source_type 1, entry 124371): it opens the seal (124372 -> GO_STATE_ACTIVE),
//     sets DATA_IRONAYA_DOORS=DONE, and STRIPS Ironaya's unit_flags 33554434
//     (UNIT_FLAG_NON_ATTACKABLE | bit25) so she becomes attackable and walks out.
//   * GameObject::Use(bot) fires that GOSSIP_HELLO chain server-side with NO lock
//     check (the gossip-hello path runs before the type switch), so a plain
//     UseGO(124371) triggers everything — the staff is not functionally required.
//   * The seal (124372) is already excluded from BotCanOpenDoorLikePlayer's
//     force-open (lock-free, not allowlisted in DcEventDoorRegistry), so only
//     this event opens it.
//
// Anchored ordering can't slot this (it must run between DBC bits 1 and 2, with
// no integer order between them), so it is CONDITIONAL like Shadowfang Keep's
// courtyard door: relevance-31 DcRunEventAction preempts the boss pull and the
// door-blocked stall. The conditional driver drives the ClearRadius engage
// pipeline then Drive()s the keystone/wait steps. Required — Ironaya is a
// mandatory spine boss, so a non-firing seal should stall for the human rather
// than silently skip into a dead-end at the shut door.

namespace
{
    constexpr uint32 ULD_KEYSTONE  = 124371;
    constexpr uint32 ULD_SEAL_DOOR = 124372;
    constexpr uint32 ULD_IRONAYA   = 7228;
    // Proximity gate to the antechamber; the GO/creature are co-loaded so this
    // doubles as a grid-safety check.
    constexpr float  ULD_SCAN      = 120.0f;

    // The Ironaya antechamber's clear centre — between the south entry ramp
    // (y ~198) and the seal (y ~295). Radius covers all Stonevault trash
    // (x -267..-210, y 198..286) and stays SHORT of the sealed Ironaya
    // (-235.7, 309.6, 61yd from centre) so the sweep never targets her.
    constexpr float  ULD_ROOM_X      = -235.0f;
    constexpr float  ULD_ROOM_Y      = 248.0f;
    constexpr float  ULD_ROOM_Z      = -48.0f;
    constexpr float  ULD_ROOM_RADIUS = 55.0f;
    constexpr float  ULD_ROOM_ZBAND  = 20.0f;
    constexpr uint32 ULD_ROOM_TIMEOUT = 120000;  // give-up valve on the pre-clear

    // --- Altar of the Keepers + Altar of Archaedas (both summoning rituals) --
    // Both altars are GAMEOBJECT_TYPE_SUMMONING_RITUAL whose completion casts a
    // SEND_EVENT spell (gameobject_template Data1) on the ritual owner. The
    // single-participant ritual would otherwise have the bot channel an anim
    // spell for ~5s; instead each event CASTS that same SEND_EVENT spell on self
    // (triggered), reproducing the ritual's own completion cast verbatim
    // (GameObject::Use -> Update: spellCaster->CastSpell(spellCaster, spellId,
    // true)). Both SEND_EVENT scripts are idempotent and only read the caster's
    // proximity to the target / the instance state, so the direct cast is safe.
    constexpr uint32 ULD_STONE_KEEPER       = 4857;    // 4 ring the Keepers altar
    constexpr uint32 ULD_TEMPLE_DOOR        = 124367;  // Hall of the Keepers exit
    constexpr uint32 SPELL_AWAKEN_KEEPERS   = 11568;   // Altar of The Keepers (GO 130511)
    constexpr uint32 SPELL_AWAKEN_ARCHAEDAS = 10340;   // Altar of Archaedas  (GO 133234)

    // Altar of The Keepers (GO 130511) at the Hall-of-the-Keepers centre. The
    // roster delivers the tank here (objective OBJ(1), ordered after Grimlok /
    // before Archaedas — see BossRosterRegistry). The hall holds LIVE trash
    // (Stone Stewards 4860, Earthen Stonebreakers 7396 / Stonecarvers 7397 — they
    // cast combat abilities, no stoned aura) that the first ClearRadius step
    // clears before the altar is fired; only the 4 Stone Keepers (4857) are stoned
    // statues (immune -> ignored by the clear, woken+killed after the altar).
    // Radius spans the keeper ring plus trash up to the temple door (~36yd); zBand
    // keeps the lower Archaedas level (z ~-52) out.
    constexpr float  ULD_KEEPER_X       = 104.85f;
    constexpr float  ULD_KEEPER_Y       = 272.45f;
    constexpr float  ULD_KEEPER_Z       = -26.53f;
    constexpr float  ULD_KEEPER_RADIUS  = 40.0f;
    constexpr float  ULD_KEEPER_ZBAND   = 15.0f;
    constexpr uint32 ULD_KEEPER_TIMEOUT = 120000;

    // Altar of Archaedas (GO 133234), in the Temple of the Stars below the hall.
    constexpr float  ULD_ARCH_ALTAR_X = 96.48f;
    constexpr float  ULD_ARCH_ALTAR_Y = 269.05f;
    constexpr float  ULD_ARCH_ALTAR_Z = -52.15f;

    // Activation: due while the party is near the still-shut seal and Ironaya is
    // present (always spawned, just sealed). Latches DONE on completion, and also
    // reads false the instant the seal opens, so it fires exactly once.
    bool UldamanIronayaSeal(Player* bot, AiObjectContext* context)
    {
        // Ordering guard. The seal antechamber's ClearRadius must not fire until
        // Ironaya is the NEXT boss: the still-shut seal sits within ULD_SCAN
        // (120yd) of the earlier Revelosh / Lost-Dwarves approaches, so the
        // proximity scan alone flickers true and the antechamber seek drags the
        // tank at trash it can't reach cleanly (the witnessed early-Ironaya fire).
        // The DB encounter order puts Ironaya at slot 3 (after Revelosh and the
        // Lost Dwarves), so gate on her being current. (This event stays
        // CONDITIONAL — unlike the keeper/Archaedas altars it has LIVE Stonevault
        // trash whose ClearRadius seek walks the tank in; see the altar note below
        // for why those two are anchored objectives instead.)
        std::optional<DungeonBossInfo> const next =
            context->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
        bool const ironayaIsNext = next.has_value() && next->entry == ULD_IRONAYA;

        GameObject* seal = bot->FindNearestGameObject(ULD_SEAL_DOOR, ULD_SCAN);
        Creature* ironaya = bot->FindNearestCreature(ULD_IRONAYA, ULD_SCAN, /*alive*/ true);

        // Throttled diagnostic: one line / 5s so a live run shows WHY the event
        // is/isn't due. Lands in DungeonClear.log. atomic because bot AI ticks run
        // on the MapUpdate.Threads pool — the throttle stamp is read/written from
        // multiple map threads (the check-then-set race is benign).
        static std::atomic<uint32> lastLog{0};
        uint32 const now = getMSTime();
        if (getMSTimeDiff(lastLog, now) >= 5000)
        {
            lastLog = now;
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] Uldaman Ironaya seal cond: seal={} state={} ironaya={} nextBoss={} ({})",
                      bot->GetName(), seal ? "found" : "MISSING",
                      seal ? static_cast<int>(seal->GetGoState()) : -1,
                      ironaya ? "present" : "no",
                      next.has_value() ? next->entry : 0,
                      ironayaIsNext ? "her turn" : "not yet");
        }

        if (!ironayaIsNext)
            return false;                            // earlier bosses still up
        if (!seal)
            return false;                            // not near the chamber yet
        if (seal->GetGoState() != GO_STATE_READY)
            return false;                            // already open -> done
        return ironaya != nullptr;
    }

    // --- Why the two altars below are ANCHORED, not CONDITIONAL --------------
    // The Hall of the Keepers and the Temple of the Stars are FAR from where a
    // conditional gate could fire: the keeper altar (104.8, 272.5, -26.5) sits
    // ~36yd past the temple door, and Archaedas's altar (96.5, 269, -52.1) is a
    // full floor below it. A conditional event has NO long-range navigation — it
    // only HopTo's (raw MovePoint, 74-node capped) and preempts the boss-nav from
    // up to ULD_SCAN (120yd) away. The keeper event's first step CLEARS the hall
    // (live Stewards / Earthen), but a conditional gate fires that ClearRadius
    // from 120yd out, where the trash fails the bounded reachability probe — so it
    // reports "clear" at once and the event runs the altar with all the trash
    // still up (the live wipe), or the bare follow-up MoveTo can't cross the gap
    // and never clicks the altar. Archaedas sitting directly below the keeper
    // altar makes it worse: the boss-nav pulls the tank DOWN to him while the gate
    // yanks it UP to the keepers.
    //
    // So both altars are travel OBJECTIVES (BossRosterRegistry, map 70) ordered
    // after Grimlok and before Archaedas, each wired to an ANCHORED event here:
    // the boss-nav LongRangePathfinder delivers the tank INTO the hall first,
    // where the trash IS reachable, so the ClearRadius step engages it properly
    // and holds until the room is clear before the altar fires. Exactly the
    // Stratholme Slaughterhouse / Dire Maul Conservatory-Door pattern. No
    // conditional predicate is needed for either.
}

namespace
{
    bool UldamanIronayaSeal(Player* bot, AiObjectContext* context);
}

void RegisterUldamanEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(70, 1, "Unseal Ironaya (Seal of Khaz'Mul)")
                      .Conditional(&UldamanIronayaSeal)
                      // Render in the panel just before Ironaya (cosmetic; does not
                      // affect engine ordering — the seal is opened on her gate).
                      .PanelBeforeBoss(ULD_IRONAYA)
                      // 1) clear the antechamber (sealed Ironaya is out of range).
                      .ClearRadius(ULD_ROOM_X, ULD_ROOM_Y, ULD_ROOM_Z,
                                   ULD_ROOM_RADIUS, ULD_ROOM_ZBAND)
                      .Timeout(ULD_ROOM_TIMEOUT)
                      // 2) step onto the keystone (a short hop south of the room).
                      .MoveTo(-234.69f, 239.62f, -50.91f, /*radius*/ 5.0f)
                      // 3) use the keystone -> SmartAI opens the seal + unseals
                      //    Ironaya. UseGO is idempotent (skips an already-used GO),
                      //    so a combat tick-gap re-running the step list is safe.
                      .UseGO(ULD_KEYSTONE, /*searchRadius*/ 10.0f)
                      // 4) wait for the seal to open. The keystone's SmartAI opens
                      //    it on a 27s CREATE_TIMED_EVENT (the door "rumbles open"
                      //    delay), NOT synchronously on the click — so the timeout
                      //    must clear 27s with margin (live-verified: a 10s timeout
                      //    spuriously "stalled" until Ironaya unsealed at +27s).
                      .WaitForGOState(ULD_SEAL_DOOR, /*GO_STATE_ACTIVE*/ 0,
                                      /*timeout*/ 45000)
                      .Build());

    // --- Altar of the Keepers (Stone Keepers) — ANCHORED -------------------
    // Anchored at OBJ(1) (BossRosterRegistry map 70), ordered after Grimlok and
    // before Archaedas: boss-nav delivers the tank into the hall, this clears the
    // live trash, then fires the altar to awaken the keepers and kills all four;
    // their deaths open the temple door (124367) onward to the Archaedas descent.
    //
    // Persistent so the multi-keeper fight (each death chain-wakes the next and
    // re-enters combat, leaving a >1s drive gap) can't rewind to step 0 and
    // re-cast the altar / re-walk the approach.
    out.push_back(EventBuilder(70, 2, "Awaken the Stone Keepers (Altar of the Keepers)")
                      .Anchored(/*orderIndex, doc-only*/ 7)
                      // 1) clear the hall's LIVE trash (Stewards / Earthen) first.
                      //    The boss-nav delivered the tank into the hall, so the
                      //    trash is reachable and the seek engages it; the 4 stoned
                      //    Stone Keepers are immune and ignored here. Held until the
                      //    room is clear so the altar never fires with trash up.
                      .ClearRadius(ULD_KEEPER_X, ULD_KEEPER_Y, ULD_KEEPER_Z,
                                   ULD_KEEPER_RADIUS, ULD_KEEPER_ZBAND)
                      .Timeout(ULD_KEEPER_TIMEOUT)
                      // 2) centre on the altar (close the last few yards).
                      .MoveTo(ULD_KEEPER_X, ULD_KEEPER_Y, ULD_KEEPER_Z, /*radius*/ 6.0f)
                      // 3) fire the altar's ritual SEND_EVENT (11568): wakes the
                      //    nearest keeper, which "enters combat with the zone" and
                      //    pulls the party (see the altar comment above for why a
                      //    direct CastSpell is used instead of UseGO on the ritual).
                      .CastSpell(SPELL_AWAKEN_KEEPERS)
                      // 4) kill all four. Each keeper's death chain-wakes the next
                      //    (SmartAI SetData) and re-pulls the zone, so a plain-gate
                      //    KillCreature (party auto-aggros; no .engage onto the
                      //    still-stoned/immune statues) holds the party here until
                      //    none remain alive.
                      .KillCreature(ULD_STONE_KEEPER, /*count*/ 4, /*searchRadius*/ 50.0f)
                      // 5) confirm the temple door has rumbled open before the
                      //    clear advances (search wide — it sits ~36yd off centre).
                      .WaitForGOState(ULD_TEMPLE_DOOR, /*GO_STATE_ACTIVE*/ 0,
                                      /*timeout*/ 60000, /*searchRadius*/ 70.0f)
                      .Persistent()
                      .Build());

    // --- Altar of Archaedas (final-boss summon) — ANCHORED -----------------
    // Anchored at OBJ(2) (ordered after the keeper altar, before Archaedas the
    // boss). Archaedas is a roster encounter but spawns stoned/non-attackable;
    // boss-nav delivers the tank onto the altar, this wakes him, the objective
    // latches, and the normal boss pull then fights and kills him.
    out.push_back(EventBuilder(70, 3, "Summon Archaedas (Altar of Archaedas)")
                      .Anchored(/*orderIndex, doc-only*/ 8)
                      // 1) step onto the Altar of Archaedas in his chamber.
                      .MoveTo(ULD_ARCH_ALTAR_X, ULD_ARCH_ALTAR_Y, ULD_ARCH_ALTAR_Z,
                              /*radius*/ 6.0f)
                      // 2) fire the altar's ritual SEND_EVENT (10340): sets
                      //    DATA_ARCHAEDAS=IN_PROGRESS and wakes the stoned boss.
                      .CastSpell(SPELL_AWAKEN_ARCHAEDAS)
                      .Build());
}


// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterUldamanRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- Uldaman (map 70) — Altar of the Keepers + Altar of Archaedas ---
    // The path to Archaedas runs through two GAMEOBJECT summoning rituals:
    // the Altar of the Keepers (wake + kill the 4 Stone Keepers to open the
    // temple door) and the Altar of Archaedas (wake the stoned final boss).
    // Both altars are FAR from any conditional gate and surrounded only by
    // dormant, immune statues — so a conditional event (HopTo-only, no
    // long-range nav) fires the instant the tank nears the room and then
    // can't reach the altar (the live "never clicks the altar" bug). Make
    // them travel OBJECTIVES so the boss-nav LongRangePathfinder delivers
    // the tank ONTO each altar first; the anchored events (UldamanEvents
    // 70/2 and 70/3) then fire the rituals. Mirrors Dire Maul's Ironbark
    // and Stratholme's Slaughterhouse.
    //
    // Encounter order is Grimlok (bit 6) -> Archaedas (bit 7) with no
    // integer slot between them, so bump Archaedas's clear ORDER to 10
    // (his real DBC kill-bit 7 is untouched — completion keys on
    // encounterIndex, never on the order override) and slot the keeper
    // altar at 8, the Archaedas altar at 9. The keeper altar sits a full
    // floor ABOVE Archaedas (z -26.5 vs -51.7) at nearly the same x/y, so
    // visiting it as an explicit waypoint also stops the boss-nav from
    // dragging the tank straight down to the stoned boss below.
    {
        BossRosterPatch p;
        p.mapId = 70;
        p.reorder = { { 2748, 10 } };  // Archaedas — order only, bit 7 kept
        p.add = {
            // arriveRadius 30 so the tank "arrives" on reaching the hall
            // and the event's ClearRadius (r40) drives the trash clear from
            // the entrance inward, rather than shoving to the altar first.
            MakeObjective(OBJ(1), /*encounterIndex*/ 7, 70,
                          "Altar of the Keepers",
                          104.85f, 272.45f, -26.53f, /*arriveRadius*/ 30.0f,
                          /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 2,
                          /*orderOverride*/ 8),
            MakeObjective(OBJ(2), /*encounterIndex*/ 7, 70,
                          "Altar of Archaedas",
                          96.48f, 269.05f, -52.15f, /*arriveRadius*/ 10.0f,
                          /*gateEntry*/ 0, /*hook*/ 0, /*eventId*/ 3,
                          /*orderOverride*/ 9),
        };
        t.push_back(std::move(p));
    }
}
