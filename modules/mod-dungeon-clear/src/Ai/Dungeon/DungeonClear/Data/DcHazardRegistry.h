/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCHAZARDREGISTRY_H
#define _PLAYERBOT_DCHAZARDREGISTRY_H

#include <vector>

#include "Define.h"

// Static registry of persistent damage the combat AI cannot reason about on its
// own. THREE KINDS of emitter live here, because the game represents ground
// damage three different ways:
//
//   * DcHazardEmitter — a CREATURE that carries a permanent pulsing aura. Keyed
//     on (map, creature entry); resolved live off the creature's position.
//
//   * DcGroundHazard — a PERSISTENT AREA AURA (SPELL_EFFECT_PERSISTENT_AREA_AURA),
//     which is not a creature at all: the server spawns a DynamicObject at the
//     cast point and it ticks there until its duration expires. Keyed on (map,
//     spell id); resolved live off the DynamicObject's position. See below.
//
//   * DcTrapHazard — a GAMEOBJECT_TYPE_TRAP the script drops on the ground. Not
//     a unit and not a DynamicObject either: a real GameObject that re-arms on
//     its own cooldown and casts its damage spell at whoever is standing on it.
//     Keyed on (map, gameobject entry); resolved live off the GameObject's
//     position. The Shattered Halls Blaze (181915) is the case it exists for.
//
// All three feed the same three consumers through DcHazard, so a caller never
// has to know which kind it just avoided.
//
// --- the creature kind ---------------------------------------------------
//
// Two distinct threats, both represented by DcHazardEmitter:
//
//   (1) A PERSISTENT pulse the party should not LOITER in. The Arcatraz Sentinel
//   (map 552, entry 20869) channels 36716 -> 36717 "Energy Discharge" — 563-937
//   damage in a 15yd radius, every second, forever, including while dormant
//   (heroic 21586: 38828 -> 38829, 938-1562). A DORMANT Sentinel is rooted in
//   place (addon aura 31261), so its position is known at author time. You DO
//   fight the live one (it wakes AGGRESSIVE at 40% HP and chases), so this half is
//   only about not camping / resting / routing THROUGH the pulse — the in-combat
//   pulse while you kill it is healable. (The live one's <=10% "Explode", 36719,
//   is NOT handled here: 36719 STUNS the sentinel for its own 6s wind-up, so the
//   party bursts the helpless 10%-HP mob down before it detonates — pulling DPS
//   off it to "dodge" would only keep it alive long enough to actually explode.)
//
//   Maraudon's Creeping Sludge (map 349, entry 12222) is the same threat one tier
//   down: a permanent 5yd / ~100 dps "Poison Shock" sphere on a mob that moves at
//   2.0 yd/s. Slow enough that everyone except the melee swinging at it can simply
//   stand outside, which is exactly what a threat-1 keep-out buys.
//
//   (2) A persistent pulse the party must actively VACATE, from a creature it
//   canNOT fight. On death the live Sentinel summons the "Destroyed Sentinel"
//   (21761) at the corpse (SmartAI event 6 -> spell 37394). That summon is
//   NOT_SELECTABLE + hostile and carries the SAME permanent 36716 -> 36717 pulse,
//   15yd/1s, until it despawns. The party has just been meleeing the Sentinel, so
//   the summon spawns right on top of them; they cannot target it to kill it and
//   have no reason to stay — but nothing moves them off, so they stand in it and
//   die. THIS is the run-wiper ("Destroyed Sentinel's Energy Discharge"), and it
//   ticks AFTER the kill, often out of combat. `vacateRadius` marks such an
//   emitter and drives the active retreat.
//
// Consumed in three ways, on two threads:
//
//   STATIC pathing (worker thread). Threat (1): a dormant emitter is rooted, so
//   route avoidance is hand-authored DcNavPenaltyRegistry boxes at the spawn
//   coords, keeping DcRouteFilter / LongRangePathfinder::BuildCoreFromMesh on
//   their worker-safe contract (no Player*/Map*/VMAP — the route producer runs on
//   DcPathWorker and CANNOT resolve a live creature). Do not "improve" this by
//   handing the filter a Map*. This half only exists for emitters that are ROOTED:
//   a wandering one (Maraudon's Creeping Sludge) has no author-time position to
//   box, so for it — as for every ground pool — the live predicates below are the
//   whole route defence.
//
//   LIVE point validation (map thread). Threat (1): camp anchors, engage
//   standoffs and skirt legs are kept out of the pulse cylinder (`radius`) via
//   DcHazard::PointIsHot / SegmentIsHot, backed by the 500ms-cached
//   DungeonClearHazardsValue.
//
//   LIVE active retreat (map thread). Threat (2): DungeonClearHazardVacate-
//   {Trigger,Action} drive every party bot (all roles) out of `vacateRadius` of a
//   live emitter that carries it, in BOTH the combat and non-combat engines (the
//   summon's pulse ticks whether or not the bot is still flagged in combat).
//   Reads the same cached value.
//
// Mirrors RoomAggroRegistry / DcNavPenaltyRegistry: adding an emitter is a
// single table edit inside DungeonClear/, never a core change.
struct DcHazardEmitter
{
    uint32 mapId{0};
    uint32 creatureEntry{0};

    // Keep-out radius (yd), 2D, for the persistent pulse (threat 1 — camp/route
    // placement). Sized as the aura's own radius plus a margin wide enough that a
    // bot which drifts a few yards mid-fight still does not clip it — but kept
    // BELOW caster range, so ranged DPS can still hold a firing line past one.
    float  radius{0.0f};

    // Vertical half-extent (yd). An emitter on another floor is not a hazard;
    // without this a Sentinel at z22 would sterilise the z48 catwalk above it.
    float  zBand{12.0f};

    // Active-vacate pulse (threat 2), yd. When > 0, this is a creature the party
    // must MOVE OUT of — either because it cannot be fought at all (the Destroyed
    // Sentinel is NOT_SELECTABLE) or because meleeing it is never worth the aura
    // (Maraudon's Creeping Sludge, which at 2.0 yd/s can be left standing). This
    // is the raw pulse radius; the two bands below place the retreat around it.
    // 0 => this emitter is fought or merely avoided in placement, never fled.
    float  vacateRadius{0.0f};

    // Hysteresis, yd, past `vacateRadius`. See DcHazard for the invariant these
    // two share; the short version is that `holdBand` says how far out the bot
    // still reads IN DANGER, and `retreatSlack` how far out the retreat AIMS.
    //
    // retreatSlack MUST exceed holdBand, or the bot arrives still in danger and
    // re-flees forever. How MUCH it exceeds it is the arrival margin.
    //
    // The defaults (2 / 6) are the Destroyed Sentinel's: flee just past the rim
    // and go inert on arrival, so the party carries ONWARD past the corpse rather
    // than being pinned at the rim by a summon that cannot be killed.
    //
    // A row raises `holdBand` when the party must not merely leave but STAY OUT
    // while the emitter lives — a live, slow mob the melee are meant to abandon
    // rather than trade with. A wide hold band beats the bot's own MoveChase:
    // every time the chase pulls it back inside, the retreat pushes it out again,
    // so it orbits outside the aura instead of oscillating through it.
    float  holdBand{2.0f};
    float  retreatSlack{6.0f};
};

// A persistent-area-aura ground pool: Scholomance's "Cloud of Disease", left
// behind where a Diseased Ghoul dies. There is NO creature to key on — the
// server creates a DynamicObject at the cast point carrying the aura, and it
// pulses there for the spell's duration with nothing to target and nothing to
// kill.
//
// Every such pool is a threat-2 emitter by nature: it cannot be fought, so the
// only correct behaviour is to leave. `vacateRadius` is therefore expected to be
// set on every row here, unlike the creature table where most rows are merely
// avoided during placement.
//
// The route half (DcNavPenaltyRegistry boxes) has no counterpart for these: a
// pool's position is not known until a mob dies on it, so there is nothing to
// hand-author. Route avoidance for a ground pool is the live retreat plus the
// live point/segment validation, and that is all it can be.
struct DcGroundHazard
{
    uint32 mapId{0};

    // The spell whose PERSISTENT_AREA_AURA effect creates the pool. This is what
    // DynamicObject::GetSpellId() returns, so it is the *cast* spell, not the
    // per-tick spell it triggers.
    uint32 spellId{0};

    // Keep-out radius (yd), 2D, for camp/standoff/skirt placement. Sized as the
    // aura's own radius plus a margin for drift. Kept modest — a ground pool sits
    // exactly where the party was just fighting, so an over-wide keep-out
    // sterilises the room the party still has to clear.
    float  radius{0.0f};

    // Vertical half-extent (yd). Scholomance stacks rooms; a pool on the floor
    // below is not a hazard.
    float  zBand{6.0f};

    // Active-vacate pulse (yd) — the RAW aura radius, so the retreat's aim point
    // (pulse + retreatSlack) lands outside this row's own keep-out cylinder and
    // PointIsHot does not reject it. Same rule the Destroyed Sentinel row
    // follows; see the note in the .cpp.
    float  vacateRadius{0.0f};

    // Same two bands, same invariant (retreatSlack > holdBand) as
    // DcHazardEmitter above. Pools keep the defaults: a pool is a fixed patch of
    // ground, so stepping just past its rim is a complete answer and there is
    // nothing to stay out of once the bot is clear.
    float  holdBand{2.0f};
    float  retreatSlack{6.0f};
};

// A GAMEOBJECT_TYPE_TRAP dropped on the floor by a script: the Shattered Halls
// "Blaze" (181915), left where a Shattered Hand Archer's flame arrow lands.
//
// This is the THIRD representation of the same idea and it needs its own table
// for the same reason the ground pools did: a GameObject is neither a Unit nor a
// DynamicObject, so both existing resolvers return nullptr on its guid and the
// `continue` that follows reads exactly like "no hazard here".
//
// A trap differs from a pool in how it fires: instead of a periodic aura it runs
// GameObject::Update's GO_READY branch, searching for a player inside
// `trap.diameter / 2` and, on finding one, casting `trap.spellId` (whose OWN
// radius is what actually splashes) before re-arming after `trap.cooldown`. So
// there are two radii to reconcile, and the one that matters for `vacateRadius`
// is the CAST spell's — a bot standing outside the trigger circle still eats the
// splash when a melee standing on top of it sets the trap off.
//
// Every trap row is a threat-2 emitter by nature: there is nothing to fight and
// nothing to kill, so `vacateRadius` is expected on every row here.
//
// No DcNavPenaltyRegistry counterpart, same as the pools: the trap's position is
// not known until an arrow lands, so there is nothing to hand-author for the
// worker-thread router. The live predicates are the whole route defence.
struct DcTrapHazard
{
    uint32 mapId{0};

    // The GameObject entry of the trap. This is what GameObject::GetEntry()
    // returns, so it is the object the script spawns — not the spell that
    // spawned it and not the spell the trap casts.
    uint32 goEntry{0};

    // Keep-out radius (yd), 2D, for camp/standoff/skirt placement. Kept modest:
    // traps land exactly where the party is standing and several can be alive at
    // once, so an over-wide keep-out sterilises the corridor the party still has
    // to fight down.
    float  radius{0.0f};

    // Vertical half-extent (yd).
    float  zBand{6.0f};

    // Active-vacate radius (yd) — the trap's CAST-spell radius, not its trigger
    // diameter, and not the padded `radius` above. Same rule as the pool rows:
    // the retreat aims vacateRadius + retreatSlack, and that aim point must fall
    // outside this row's own PointIsHot cylinder or every candidate is rejected.
    float  vacateRadius{0.0f};

    // Same two bands, same invariant (retreatSlack > holdBand) as the other two
    // tables. A trap is a fixed patch of ground, so a thin hold band is right:
    // step just past the rim and carry on fighting.
    float  holdBand{2.0f};
    float  retreatSlack{6.0f};
};

class DcHazardRegistry
{
public:
    // True iff `mapId` has at least one CREATURE emitter.
    static bool HasEmitters(uint32 mapId);

    // True iff `mapId` has at least one GROUND (persistent-area-aura) row.
    static bool HasGroundHazards(uint32 mapId);

    // True iff `mapId` has at least one TRAP (GameObject) row.
    static bool HasTrapHazards(uint32 mapId);

    // True iff `mapId` has a row of ANY kind. This is the cheap early-out the
    // live predicates and the vacate trigger gate on, so a map that registers
    // only ground pools (Scholomance) or only traps (Shattered Halls) is not
    // skipped by a creature-only check.
    static bool HasAnyHazard(uint32 mapId);

    // The emitter row for (mapId, creatureEntry), or nullptr when that creature
    // is not a registered hazard. Linear scan — the table is small.
    static DcHazardEmitter const* Find(uint32 mapId, uint32 creatureEntry);

    // The ground row for (mapId, spellId), or nullptr when that spell does not
    // leave a registered pool. Linear scan — the table is small.
    static DcGroundHazard const* FindGround(uint32 mapId, uint32 spellId);

    // The trap row for (mapId, goEntry), or nullptr when that gameobject is not
    // a registered trap. Linear scan — the table is small.
    static DcTrapHazard const* FindTrap(uint32 mapId, uint32 goEntry);

    // Every registered trap GameObject entry on `mapId`, in table order. Empty
    // when the map registers none. The live value sweeps by entry rather than
    // sweeping every gameobject and filtering, because a dungeon floor carries
    // hundreds of doors, chairs and torches and only a handful of them burn.
    static std::vector<uint32> TrapEntries(uint32 mapId);

    // Pure geometry: true when (px,py,pz) lies inside a keep-out cylinder of
    // `radius`/`zBand` centred on (ex,ey,ez). No game state — unit-testable.
    // A radius of 0 makes the cylinder inert.
    static bool PointInCylinder(float radius, float zBand,
                                float ex, float ey, float ez,
                                float px, float py, float pz);

    // Pure geometry: true when the 2D segment (ax,ay)->(bx,by) clips the
    // keep-out circle of `radius` centred on (ex,ey), with the leg touching the
    // `zBand` around `ez`. Mirrors DcEngageGeometry::NeedsRoomAggroSkirt.
    static bool SegmentClipsCylinder(float radius, float zBand,
                                     float ex, float ey, float ez,
                                     float ax, float ay, float az,
                                     float bx, float by, float bz);

    // Row-typed wrappers over the two primitives above, one pair per emitter
    // kind. Callers hold a row, not loose scalars, so these are what they use.
    static bool PointInside(DcHazardEmitter const& e,
                            float ex, float ey, float ez,
                            float px, float py, float pz);

    static bool SegmentClips(DcHazardEmitter const& e,
                             float ex, float ey, float ez,
                             float ax, float ay, float az,
                             float bx, float by, float bz);

    static bool PointInside(DcGroundHazard const& g,
                            float ex, float ey, float ez,
                            float px, float py, float pz);

    static bool SegmentClips(DcGroundHazard const& g,
                             float ex, float ey, float ez,
                             float ax, float ay, float az,
                             float bx, float by, float bz);

    static bool PointInside(DcTrapHazard const& t,
                            float ex, float ey, float ez,
                            float px, float py, float pz);

    static bool SegmentClips(DcTrapHazard const& t,
                             float ex, float ey, float ez,
                             float ax, float ay, float az,
                             float bx, float by, float bz);
};

#endif
