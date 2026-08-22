/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTANKFORM_H
#define _PLAYERBOT_DCTANKFORM_H

class PlayerbotAI;

// Pre-pull shapeshift gate for a DRUID tank. The mirror image of DcFormGate,
// which drops a form so an item-use cast can go out; this one puts the tank's
// combat form UP before it opens a pull.
//
// Why it is needed: stock only ever shifts a feral tank on the COMBAT engine.
// The "bear form" trigger lives in BearDruidStrategy, whose type is
// STRATEGY_TYPE_TANK|STRATEGY_TYPE_MELEE — combat-engine only — so nothing in
// the non-combat engine has an opinion about the druid's form. Every
// DungeonClear pull, by construction, is opened OUT of combat: the tank walks
// to the aggro edge / casts its opener while still on the non-combat engine,
// and only crosses to the combat engine once aggro lands. So the druid tags in
// caster form, eats the pack's opener at cloth armour and caster health, and
// shifts a beat later when BearDruidStrategy finally gets a tick — the "pulls
// in human form, takes a few hits, then shifts to bear" the runs show.
//
// Stock's own DruidPullStrategy already names "dire bear form" as its pull
// PRE-action for exactly this reason, but DungeonClear does not drive stock's
// pull strategy — it owns the whole maneuver (DungeonClearPullAction) and the
// walk-in engage (EngageDirect) — so that pre-action never runs here.
//
// Two things it must not break, and why it doesn't:
//
//  * DRINKING. A bear cannot drink — stock's DrinkAction::isPossible rejects
//    every feral form outright — so shifting early could in principle starve a
//    Smart Rest top-off. Both call sites sit DOWNSTREAM of the rest gate: the
//    pull trigger is ready-gated, and the boss engage's own comment records
//    that the trigger "already proved ... the between-pulls readiness (incl.
//    the Smart Rest top-off)". The engage call is additionally range-gated to
//    DC_PULL_START_RANGE, so a tank still crossing open ground is left in
//    caster form. (Post-combat the tank is in bear form anyway — stock put it
//    there — so the bear-can't-drink case is pre-existing, and Smart Rest's
//    maxRestMs failsafe already bounds it.)
//
//  * DcFormGate. The event/objective item-use steps DROP the form to get their
//    cast out; this gate would put it back. They can't fight over a tick: the
//    engine runs one action per tick, and the event rungs (EventDue) outrank
//    every engage rung, so the shift simply doesn't happen while an event
//    drives. Worst case is one wasted shift, never a stall — DcFormGate's
//    drop is synchronous and its step is idempotent.
namespace DcTankForm
{
    // True when this bot fights as a bear: a druid running the "bear" combat
    // strategy that has actually trained a bear form. Deliberately keyed on the
    // strategy rather than PlayerbotAI::IsTank(bySpec=true) — the spec test for
    // a druid asks whether it is ALREADY in bear form, which is circular here.
    bool IsBearTank(PlayerbotAI* botAI);

    // IMPURE: if the bot is a bear tank and is not already in Bear/Dire Bear
    // form, shift it. Returns true when a shift was cast this tick.
    //
    // FIRE-AND-FORGET by design — callers ignore the result and carry on with
    // the same tick's pull work. The shift is instant, off the movement path
    // (it neither stops a glide nor is interrupted by one), so the form comes
    // up underneath the approach rather than in place of it. Nothing in the
    // pull is allowed to WAIT on it: a form that cannot go up (silenced, out of
    // mana, mid-GCD, polymorphed) must never be able to hold a pull open, which
    // is why this returns a bool nobody branches on and why the caller has no
    // retry state to livelock on. The next tick simply tries again, and the
    // pull proceeds in caster form if it never lands.
    bool EnsureBearForm(PlayerbotAI* botAI);
}

#endif
