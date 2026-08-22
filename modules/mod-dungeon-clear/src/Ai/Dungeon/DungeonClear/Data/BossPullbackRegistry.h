/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_BOSSPULLBACKREGISTRY_H
#define _PLAYERBOT_BOSSPULLBACKREGISTRY_H

#include "Define.h"

// Static registry of PULL-BACK bosses: bosses that must never be fought where
// they stand, because the ground they stand on kills the party.
//
// THE TABLE IS CURRENTLY EMPTY — read BossPullbackRegistry.cpp before adding to
// it. Its one and only row (Ghaz'an, 18105, The Underbog 546) was retired in
// S1593 once the actual cause was fixed upstream: he was only ever in the water
// because a headless party never fired areatrigger 4302, the sole caller of his
// ACTION_MOVE_TO_PLATFORM. He climbs onto meshed, connected ground now and is
// fought like any other boss. The machinery stays because the facility is
// general and its invariants are pinned by tests.
//
// That history is the main thing to take from this file: a pull-back row is a
// POSITIONAL WORKAROUND, and the first question for a new one is always whether
// the boss is standing in a bad place because of an upstream defect that can be
// fixed instead. This row spent a long time treating a symptom.
//
// The fix is positional and hand-authored, in the same spirit as
// FightInPlaceRegistry (which forbids a pull) — this is its mirror image: it
// MANDATES one. A row says "the party fights this boss HERE, not where he lives".
// The tank walks the party to `camp`, goes out ALONE to tag the boss, drags him
// back, and the whole party fights on the anchor.
//
// Everything after that is the EXISTING advanced-pull machinery, unchanged: the
// Forming/Advancing/Returning/Engage FSM, the follower hold-at-camp, and the
// drag-back action all run exactly as they do for a trash pull. A row only
// changes WHICH target the pull is aimed at and WHERE the camp is.
//
// Why a registry and not geometry: "is this spot lethal" is not derivable. The
// navmesh happily reports the water sheet as walkable (it IS — you can swim it),
// the mob is reachable, and there is no aura or hazard emitter to detect. Only a
// human who has watched the encounter knows the party has to stand somewhere
// else. Mirrors RoomAggroRegistry / BossRosterRegistry: adding a fix is a single
// table edit inside DungeonClear/, never a core change or an mmap regen.
struct BossPullback
{
    uint32 mapId{0};
    uint32 bossEntry{0};
    // Party fight anchor: hand-authored, on safe ground, verified against the
    // navmesh. This is ALSO the boss's roster anchor (see the matching
    // BossRosterPatch), so boss navigation walks the party here instead of
    // routing them at the boss's live position.
    float  campX{0.0f}, campY{0.0f}, campZ{0.0f};

    // FORCE-AGGRO opt-in. 0 (the default) means "tag this boss the normal way" —
    // the tag leg walks inside his aggro bubble and lets him notice the tank, the
    // same pull every other boss in every other dungeon gets. A positive value is
    // the range, in yards, within which the tank instead forces him into combat
    // outright (DcForcePullbackAggro).
    //
    // DEFAULT OFF ON PURPOSE, and it should stay the exception. Forcing bypasses
    // the boss's own aggro logic, which is normal, tuned behaviour we want almost
    // everywhere: it can start an encounter from outside the range the script
    // expects, skip a script's own aggro hooks, and it is indiscriminate about
    // where the boss is standing when it lands. Setting this is a statement that a
    // SPECIFIC boss cannot be tagged normally at all — not a shortcut for one that
    // is merely awkward.
    //
    // Ghaz'an was the case it was built for, on the belief that his platform was
    // off-mesh and he never finished his lap — so no reachable spot existed
    // inside his aggro bubble. Both were consequences of him being stuck in the
    // water, and both went away with the areatrigger fix (S1593). No row uses
    // this today, and "the tag leg keeps timing out" is a reason to find out WHY
    // before it is a reason to set this.
    float  forceAggroRange{0.0f};

    // SUMMON-IF-STUCK opt-in, and like forceAggroRange it defaults OFF and should
    // stay rare — this one more so, because it relocates a boss outright.
    //
    // It fires in exactly one situation: the tank is home at the anchor, the boss
    // is engaged and coming, and he is STILL IN THE WATER BELOW the anchor. Then he
    // is teleported to the anchor rather than waited on. It is not a shortcut for a
    // slow boss — the water-and-below test is what keeps it to the failure it is
    // for, and a boss that has climbed out onto dry ground is left alone to walk
    // the rest of the way himself.
    //
    // Ghaz'an was the case for this too: engaged from the water he had to chase
    // the party up a climb his aggro path could not follow, and he would hang at
    // the water's edge indefinitely. He is not engaged from the water any more —
    // he walks up under his own script, before the fight — so nothing needs it.
    // Kept because "the boss physically cannot reach the party" is a real class
    // of failure, but a boss that is merely SLOW to arrive is not it.
    bool   summonWhenStuckBelow{false};
};

class BossPullbackRegistry
{
public:
    // The pull-back row for (mapId, bossEntry), or nullptr when the boss is
    // fought normally. Pure (no game state) so it is unit-testable on its own.
    // Linear scan; the table is tiny.
    static BossPullback const* Find(uint32 mapId, uint32 bossEntry);

    // True iff `mapId` has any pull-back boss. Cheap early-out for the per-tick
    // callers (the engage gate and the camp guard) so a map with no rows pays one
    // bool and nothing else.
    static bool HasRows(uint32 mapId);
};

#endif  // _PLAYERBOT_BOSSPULLBACKREGISTRY_H
