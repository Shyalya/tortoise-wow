/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCHAZARD_H
#define _PLAYERBOT_DCHAZARD_H

#include "Define.h"

class Player;

// The LIVE half of DcHazardRegistry: "is this spot / this leg standing in a
// persistent damage aura?" See DcHazardRegistry.h for the mechanism and why it
// is split from the route half.
//
// Covers ALL THREE emitter kinds transparently — the creature-carried pulses
// (DcHazardEmitter, via DungeonClearHazardsValue), the persistent-area-aura
// ground pools (DcGroundHazard, via DungeonClearGroundHazardsValue) and the
// gameobject traps (DcTrapHazard, via DungeonClearTrapHazardsValue). A caller
// asks "is this spot hot" and never has to know which kind answered.
//
// MAP THREAD ONLY. These resolve live creature / dynamic-object / gameobject
// positions through those three 500ms-cached values and must never be called from the
// DcPathWorker thread — the route producer avoids the creature emitters via the
// DcNavPenaltyRegistry boxes instead, which need no game state at all. Ground
// pools and traps have no route-half counterpart at all: their positions are not
// known until a mob dies / an arrow lands, so the live predicates are the whole
// defence.
//
// Every entry point takes a cheap DcHazardRegistry map early-out first, so a map
// with no rows of either kind pays one bool per call and nothing else.
namespace DcHazard
{
    // The two hysteresis bands now live PER ROW (DcHazardEmitter/DcGroundHazard
    // `holdBand` and `retreatSlack`), because they encode two different intents and
    // one pair of globals could only ever serve one of them:
    //
    //   holdBand    — how far past the pulse the bot still reads IN DANGER.
    //   retreatSlack — how far past the pulse the retreat AIMS.
    //
    // retreatSlack > holdBand always, so the retreat overshoots the danger band and
    // the bot is genuinely clear when it arrives rather than re-firing on landing.
    //
    // A THIN holdBand (the Destroyed Sentinel's 2) means "leave, then carry on":
    // the trigger goes inert on arrival, normal driving resumes, and the party
    // advances past the corpse. Nothing pulls a cleared bot back toward an
    // unattackable summon, so there is nothing to hold for.
    //
    // A WIDE holdBand (Maraudon's Creeping Sludge, 6) means "stay out while it
    // lives": the emitter is a live mob the bot's own MoveChase keeps steering it
    // back into, and only a band wider than melee reach can outlast that. The bot
    // orbits the aura instead of oscillating through it.
    //
    // These remain as the DEFAULTS a row inherits when it does not say otherwise.
    inline constexpr float VacateRetreatSlack = 6.0f;
    inline constexpr float VacateStayBand     = 2.0f;

    // True when (x,y,z) lies inside any live emitter's keep-out cylinder.
    // The test to use when validating a point the party will STAND on: a camp
    // anchor, a per-bot camp slot, a standoff position.
    bool PointIsHot(Player* bot, float x, float y, float z);

    // True when the straight segment (ax,ay,az)->(bx,by,bz) clips any live
    // emitter's keep-out cylinder. The test to use when validating something the
    // party will WALK ALONG, since a leg can pass straight through an emitter
    // while both of its endpoints sit clear.
    //
    // Callers walking a PathGenerator polyline must feed CONSECUTIVE PAIRS to
    // this, never the vertices to PointIsHot: PathGenerator returns string-pulled
    // CORNER points, not a densified line, so an open-room leg is often just
    // {start, end} and a per-vertex scan degenerates to testing the destination
    // alone — reintroducing the exact hole this function exists to close.
    bool SegmentIsHot(Player* bot, float ax, float ay, float az,
                      float bx, float by, float bz);

    // Convenience wrapper: SegmentIsHot from `bot`'s current position to (x,y,z).
    bool LegIsHot(Player* bot, float x, float y, float z);

    // The nearest live active-vacate hazard whose radius the bot is standing
    // inside (same-floor) — a creature emitter with vacateRadius > 0 (the
    // Destroyed Sentinel's persistent pulse), any ground pool (Scholomance's
    // Cloud of Disease), or any gameobject trap (the Shattered Halls Blaze).
    // Returned as its live position + the raw pulse radius, so
    // the retreat action can compute a point to clear to. `ok == false` when the
    // bot is clear of everything.
    //
    // Presence-based, not aura-based: a vacate emitter is dangerous the whole time
    // it exists (the summon pulses permanently), so there is no wind-up signal to
    // wait for — being inside its radius is the signal.
    //
    // MAP THREAD ONLY, same as the predicates above. This is what the retreat
    // (DungeonClearHazardVacate{Trigger,Action}) fires on.
    struct VacateEmitter
    {
        bool  ok{false};
        float x{0.0f}, y{0.0f}, z{0.0f};
        float pulseRadius{0.0f};
        float retreatSlack{VacateRetreatSlack};  // this row's, not the global
    };
    VacateEmitter NearestVacate(Player* bot);

    // True when (x,y,z) lies inside ANY live vacate emitter's DANGER band — the
    // exact condition NearestVacate fires on, asked about a point the bot is not
    // standing on yet.
    //
    // This is what a retreat must validate its destination against, and it is NOT
    // the same question as PointIsHot. PointIsHot answers "is this a bad place to
    // PLANT something" using each row's placement `radius`; this answers "will the
    // retreat trigger re-fire the moment I arrive here" using `vacateRadius +
    // holdBand`. For a row whose danger band is wider than its placement radius
    // the two disagree, and a retreat that only consulted PointIsHot would happily
    // pick a landing spot that is still in danger — then flee again, and again.
    // With a field of overlapping emitters (a Maraudon sludge pack) that is a
    // per-tick thrash that never leaves the pack: tr-20260815-134844-5 logged 83
    // retreat re-issues in ~25s across two bots, both of which died mid-flee.
    //
    // MAP THREAD ONLY, same as everything else here.
    bool PointIsInVacateBand(Player* bot, float x, float y, float z);
}

#endif
