/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_SCRIPTEDPULLREGISTRY_H
#define _PLAYERBOT_SCRIPTEDPULLREGISTRY_H

#include <cstdint>
#include <vector>

#include "Define.h"

class Player;
class Unit;
namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
// Static registry of SCRIPTED PULL STAGES: hand-authored "peel this pack out of
// that room, in this order, from this exact spot" plans.
//
// It is the third member of the positional-override family and sits between the
// other two. FightInPlaceRegistry FORBIDS a pull; BossPullbackRegistry MANDATES
// one for a boss and hand-authors its camp. This one mandates a SEQUENCE of trash
// pulls and hand-authors both the camp AND the spot the tank takes the tag from.
//
// Why the extra spot. The generic pull picks its own commit point — it glides
// toward the pack and stops just outside the pack's aggro bubble. That works in a
// corridor, where "just outside aggro" and "out of everyone else's aggro" are the
// same place. It does not work in a room whose packs share sight-lines with each
// other and with a boss: there the only safe place to stand is a specific spot
// behind a specific piece of wall, which has line of sight to exactly one pack and
// to nothing else. That spot is not derivable from the navmesh or from aggro
// radii — it is a fact about the room's walls that a human measured in-game.
//
// The canonical case is Selin Fireheart's room (Magisters' Terrace, 585). Selin
// only ever attacks targets inside his room (CanAIAttack is
// `who->GetPositionX() > 216.0f`), his two six-mob guard packs sit either side of
// him well inside that plane, and every route that walks the party in far enough
// to fight a pack also wakes him. Seven sessions of trying to fight the packs in
// place, in a corner, ended with the boss pulled every time. The design this
// registry serves inverts it: the party camps WELL BACK DOWN THE HALL, and the
// tank steps to one measured spot per pack — both of them outside the room,
// shooting a diagonal through the doorway at the pack on the far side — tags at
// range, and drags it all the way back out. The party never enters the room until
// both packs are dead. See the MgT rows in ScriptedPullRegistry.cpp for the
// geometry.
//
// WELL BACK is doing work in that sentence, and the distance that matters is not to
// the doorway — it is 40 YARDS TO THE NEAREST GUARD SPAWN. Two of the six mobs in
// each pack are Wretched Husks, which cast 44503 Fireball / 44504 Frostbolt (both
// 40yd) off smart_scripts rows carrying castFlags 64, SMARTCAST_COMBAT_MOVE. The
// core reads that flag as "no combat movement while the target is in range AND in
// line of sight". So inside 40yd a Husk's answer to being pulled is to step clear of
// whatever was blocking line of sight and then PLANT and shoot — for this room, to
// stop in the doorway and hold the fight open across it. Outside 40yd it has no such
// option and must run the corridor, which is what a drag-back is for.
//
// The camp began ~20yd from the doorway, well inside that, and four rounds of fixes
// went into policing the consequences. It is now mid-corridor at 55yd from the
// nearest guard spawn, which buys three things at once, none of them a gate that can
// fail:
//   * THE CASTERS HAVE TO COME. See above; this is the whole reason for the number.
//   * SELIN CANNOT FOLLOW. An accidental boss tag used to mean a boss fight at the
//     camp; from 45yd under his CanAIAttack plane, a dragged Selin crosses it long
//     before he arrives, stops being able to attack anything, and resets. The worst
//     mistake available in this room becomes free.
//   * THE LEASHES STOP BEING LOAD-BEARING. A 12yd tank excursion covered ~60% of the
//     distance to the doorway before; now it covers 26%.
// What it costs is drag length — ~42yd instead of ~15 — so anything clocked against
// the short version has to be sized from the row instead of flat (see
// ScriptedPullTravelBudgetMs below and its two use sites), and the arm gate can no
// longer be measured from the camp at all (see `armX`).
//
// The SECOND plan on that map — the five-stage rotunda between Vexallus and
// Delrissa — is the same machinery solving a differently-shaped room, and it is
// worth knowing which parts of the above were about Selin specifically. There is no
// wall to hide behind there: it is one open circle holding five formations whose
// nearest members are 12-23yd apart, against a ~21yd elite aggro reach. So the
// separation a stand spot provides is DISTANCE, not line of sight, and it only
// exists relative to the packs that are still ALIVE when the spot is used. That
// makes `order` load-bearing in a way it is not for Selin's mirrored pair: every
// stage is approached across ground the previous stages emptied, and a stand spot is
// only ever checked against the packs LATER in the order. Reorder those rows and the
// coordinates stay valid while the plan stops working — which is why changing the
// order means re-deriving the spots, not just renumbering (it did, when the order
// became south -> centre -> east -> north-east -> north-west).
//
// It is also the plan that mostly does not tag at range. See `bodyPull` below: four
// of those five rows are authored as body pulls, so their stand spots are waypoints
// rather than firing positions and the distance that has to clear the still-live packs
// is measured from the AGGRO EDGE the tank walks on to, ~8yd past the spot. The EAST
// row is the exception — its neighbour is engaged at T=0 no matter what the opener is,
// so there is no separation for a body pull to buy and the row keeps the 8yd instead.
//
// A stand spot is measured in-game and cannot be sanity-checked by eye: the first
// west row here was, to two decimals on all three axes, the SPAWN POSITION OF ONE
// OF THE MOBS, and the plan dutifully walked the tank into the middle of the pack
// for four test runs while every gate downstream was patched around the symptom.
// Cross-check a new row against `creature` before trusting it — and against the
// navmesh, which is how the rotunda's first east spot was caught sitting outside the
// room's south-east wall. The row-geometry gtests in t/TestScriptedPull.cpp exist for
// exactly this.
//
// Everything after the plan is the EXISTING advanced-pull machinery, unchanged:
// the Forming/Advancing/Returning/Engage FSM, the follower hold-at-camp, the
// drag-back action. A stage only changes WHICH pack the pull is aimed at, WHERE
// the party camps, and WHERE the tank stands to tag.
struct ScriptedPullStage
{
    uint32 mapId{0};
    // The stage only arms while this boss is the run's next objective, so the
    // plan can never fire on the way past the room after the boss is dead.
    uint32 bossEntry{0};
    // Pull sequence within the map, ascending. Stage N+1 only arms once stage N's
    // volume holds no live pack member — i.e. once that pack has been peeled out
    // and killed. Must be unique per map (it is the stage's identity: the in-flight
    // stage is remembered as DcPullContext::scriptedStage).
    //
    // That "only once N is empty" rule is also how a plan states a PREREQUISITE, and
    // the rotunda's first row is one: a Sunblade Sentinel patrols the hall the camp
    // stands in and, left alive, walks into the middle of a later stage's drag-back.
    // Expressed as stage order-2 whose volume is the patrol's own waypoint path, it
    // costs no new machinery — nothing downstream can arm while that volume holds a
    // live sentinel, and when the route trash already killed it on the way in the row
    // reads empty and is skipped.
    uint32 order{0};
    char const* name{nullptr};

    // Party camp: where the followers hold, passive, and where the tank drags the
    // pack back to. Hand-authored OUT of the pack's line of sight.
    //
    // PER ROW, not per plan. Selin's two rows share one camp because his two packs
    // are a mirrored pair either side of one doorway, so one piece of wall serves
    // both. The rotunda's do not: one camp far enough back to be safe from the SOUTH
    // pack is 97-102yd from the northern ones, and the drag-back is then most of the
    // maneuver's wall clock and all of its risk. Those rows move the camp forward one
    // room once the packs that made the back camp necessary are dead — which is only
    // sound because the ordering already guarantees they are (see `order`), and
    // because the forward camp still clears every LIVE pack by more than the 40yd a
    // castFlags-64 caster answers by planting instead of coming.
    float campX{0.0f}, campY{0.0f}, campZ{0.0f};

    // Tank stand spot: the one place with line of sight to THIS pack and to
    // nothing else. The tag leg walks here first and pulls from here.
    float standX{0.0f}, standY{0.0f}, standZ{0.0f};

    // The pack, as a cylinder: everything in `entries` within `packRadius` (2D)
    // and `packZBand` (vertical half-band) of (packX,packY,packZ). Sized to hold
    // the pack's spawns and to EXCLUDE its neighbours — a stage that swallowed the
    // boss or the next pack would select the wrong target and never complete.
    float packX{0.0f}, packY{0.0f}, packZ{0.0f};
    float packRadius{0.0f};
    float packZBand{0.0f};

    // Arm gate: the stage is dormant until the tank is within `armRadius` (2D) of
    // the arm anchor. Without it the plan would arm from the instance entrance —
    // the boss is "next" from the first tick — and hijack the pull pipeline while
    // the trash between here and there is still up. Size the radius so it covers
    // the staging ground in front of the room and stops short of the last pack
    // before it.
    //
    // (0,0,0) => measure from the CAMP, which is the right anchor whenever the camp
    // is a few yards behind the pack, i.e. for every row where "the tank has walked
    // up to the camp" and "the tank has walked up to the room" are the same event.
    //
    // They are NOT the same event once the camp is backed off far enough to be out of
    // the pack's reach, and conflating them is a live hazard, not a tidiness point.
    // Selin's camp is mid-corridor, 43yd back from the anchor and 11.5yd from the
    // X 179-182 Sunblade pack; an arm radius of 25 measured from THERE arms the stage
    // while that pack is still alive. The plan would hijack the pull pipeline off it,
    // walk the tank 40yd past it to the stand spot, and pin the followers, passive, in
    // the middle of it. So a row whose camp is out of reach of its own work names its
    // arm anchor separately, and Selin's stays where it has always been: the staging
    // chamber in front of the doorway.
    //
    // A PATROL is the other thing that forces a row off its camp, and the rotunda rows
    // are the case: their camp is clear floor 57yd from the nearest pack, but a
    // Sunblade Sentinel's waypoint path runs the hall it stands in and passes 5.6yd
    // from it. Route trash — dead before the party settles, in the healthy case — but
    // an arm radius drawn at the camp covers the patrol's whole approach, so the stage
    // could arm off a tank standing next to a live one. Their anchor sits forward at
    // the mouth of the room's neck, which clears the patrol line and the camp both.
    float armX{0.0f}, armY{0.0f}, armZ{0.0f};
    float armRadius{0.0f};

    // True once a row names an arm anchor distinct from its camp.
    bool HasArmAnchor() const
    {
        return armX != 0.0f || armY != 0.0f || armZ != 0.0f;
    }

    // Only these creature entries count as pack members. Position alone is not
    // enough: rooms contain props that read as hostile (Selin's fel crystals are
    // faction 190 and sit at the centre of both guard packs), and a stage that
    // counted one would never report its pack cleared.
    std::vector<uint32> entries;

    // TAKE THE TAG BY WALKING INTO THE PACK — never with a ranged opener.
    //
    // The default is the opposite, and for a good reason: a ranged tag lets the tank
    // stand on the authored spot and stay there, which is the whole design of Selin's
    // rows. A body pull is what happens when no opener resolves, and the code has
    // always treated that as the degraded case.
    //
    // The rotunda inverts it, off live observation: pull most of those formations with
    // a ranged opener and the tagged pack brings NEIGHBOURS that the same pull taken by
    // body contact does not. That is not a wider CONFIG_CREATURE_FAMILY_ASSISTANCE_
    // RADIUS — that radius is a flat config drawn around the PULLED mob and is the
    // same either way — so this row does not try to out-measure it with geometry. It
    // forbids the opener instead, which is the one lever that reproducibly changes
    // which mobs come.
    //
    // The MECHANISM is worth knowing before setting this on a new row, because it
    // decides which rows it can help. CallAssistance fires twice over: once at T=0 from
    // the mob's SPAWN (engaging the neighbour 2000ms later), and then again every
    // CreatureFamilyAssistancePeriod — 3s — from wherever the mob is standing by then,
    // for the whole fight. Only the second kind is opener-dependent, because only it
    // samples a position the pull style can change. So this flag buys separation for a
    // pair that is a NEAR MISS at spawn and would have been caught by a later re-call,
    // and buys nothing at all for a pair that is already inside at T=0. The rotunda's
    // east/north-east pair is the second kind, which is why that row is ranged: see the
    // cross-pack margin table in ScriptedPullRegistry.cpp.
    //
    // What it costs is the stand spot's original job. On a ranged row the spot is a
    // FIRING POSITION and the tank never leaves it (DC_SCRIPTED_PULL_CREEP is 0). On a
    // body-pull row it is a WAYPOINT: the tank walks to it, and then walks on to the
    // aggro edge of its own pack — about 8 more yards, in a direction the spot chose.
    // So the number a body-pull row is authored against is not "is the nearest member
    // inside 30yd" but "is the point 18-19yd from the nearest member still clear of
    // every pack this plan has not pulled yet". See the rotunda dossier in
    // ScriptedPullRegistry.cpp for those five measurements, and
    // RotundaBodyTagPointsClearEveryStillLivePack in t/TestScriptedPull.cpp for the
    // assertion that keeps them true.
    //
    // Two things downstream key on this, both in DcPullActions' Advancing branch, and
    // both already existed as the no-opener fallback path:
    //   * the stand-spot CLAMP on the walk-in is dropped, so the tank may cross the
    //     8yd from the spot to the aggro edge;
    //   * the generic bystander detour is restored, because there is no longer an
    //     authored firing lane for it to bend the tank off.
    // The third is new and is only needed once a body pull is the PLAN rather than a
    // fallback: the walk-to-the-stand-spot leg has to stop re-issuing itself once the
    // tank has arrived (DcPullContext::scriptedAtStandMs), or the two legs fight —
    // forward to the aggro edge, back to the spot, forward again.
    bool bodyPull{false};

    // WHICH member of the pack to tag: the one FURTHEST FROM THIS ANCHOR.
    //
    // The default ranks candidates by distance from the STAND SPOT and takes the
    // nearest, which is the right answer for a ranged row (the nearest member is the
    // one the opener can actually reach) and a defensible one for a body pull (the
    // shortest walk in). It is the wrong answer when the pack has a NEIGHBOUR that is
    // going to come anyway, because then the only quantity worth optimising is how far
    // that neighbour has to run before it arrives.
    //
    // The rotunda's east pack is the case, and the reason is stock and measurable.
    // Sunblade Mage Guard 96774 sits at (146.79, -125.14) with combat reach 1.8 and NE
    // pack Ethereum Smuggler 96849 at (144.71, -113.34) with reach 1.25: 12.00yd apart
    // against a limit of 13.05 — AnyAssistCreatureInRangeCheck hands
    // CreatureFamilyAssistanceRadius (10) to IsWithinDistInMap with both radius flags
    // set, so the two reaches are ADDED to the allowance, not subtracted from the gap.
    // Inside by 1.05yd, at T=0, before anything moves. Creature::AtEngage runs
    // CreatureGroup::MemberEngagingTarget for EVERY member of a groupAI-3 formation,
    // and each of those goes through Unit::Attack, which calls CallAssistance() from
    // the member's own SPAWN position. So tagging ANY east mob engages 96774 where it
    // stands and pulls the north-east formation in behind it. No stand spot and no
    // choice of target can prevent that — the call is issued before anything has moved.
    //
    // What the choice DOES decide is where the fight starts. Tag the east member
    // nearest the north-east pack and the two formations converge on the tank at once;
    // tag the one furthest from it and the east pack is already at the camp, being
    // fought, by the time the north-east pack finishes its run. The anchor names the
    // neighbour, not a spot on the floor: point it at the pack the plan cannot avoid
    // waking, and the row takes the tag from the far side of its own.
    //
    // Everything else about selection is unchanged — the candidate must still be a live
    // member of THIS stage's cylinder, still reachable, still not the abort target.
    // (0,0,0) => rank nearest-to-the-stand-spot, as before.
    float avoidX{0.0f}, avoidY{0.0f}, avoidZ{0.0f};

    // True once a row names a neighbour to tag away from.
    bool HasAvoidAnchor() const
    {
        return avoidX != 0.0f || avoidY != 0.0f || avoidZ != 0.0f;
    }
};

// Vertical tolerance, in yards, for the arm-range test (a bot on the room's floor
// vs an anchor coordinate measured a fraction of a yard off it). Deliberately
// loose: the arm gate is about "have we walked up to the room yet", not precision.
inline constexpr float DC_SCRIPTED_PULL_ARM_ZBAND = 20.0f;

// How far past the stand spot the tag walk-in may travel, in yards.
//
// ZERO. The tag is taken FROM the spot, full stop. This began as 2.5yd of slack for
// "a mob a hair outside the pull spell's range", which sounded harmless and is not:
// the stand spots have a yard of aggro margin, not three, and 2.5yd of creep spends
// all of it.
//
// Measured, from the EAST stand spot (212.22, 7.42) — the numbers the original row
// comment worked through for the west spot and never did for this one:
//   centre-pair Skulker 96825 (231.70,  2.63)   20.06yd
//   centre-pair Bruiser 96830 (231.62, -1.86)   21.50yd
// A level-69 elite reaches ~19yd against a level-70, so the spot itself clears them
// by about a yard. Creep 2.5yd toward the east pack and the tank stands at
// (213.34, 5.19): 18.54yd from that Skulker — inside aggro — and the sight-line to it
// now leaves the doorway at Y~4.8 instead of Y~6.5, i.e. through the opening instead
// of into the wall. The creep shortens the range AND opens line of sight, and
// tr-20260803-133734-1 is what that looks like: the tank body-pulled both centre mobs
// on the first pull, which is a fight the plan never accounted for right next to the
// boss.
//
// Nothing is lost by removing it. Both packs' nearest member is inside a 30yd opener
// from its own spot (east ~25yd, west ~27.8yd), so the slack was never needed here,
// and a genuinely unreachable tag still falls out to the leg watchdog and the normal
// walk-in exactly as before — bounded, and loud in the log.
inline constexpr float DC_SCRIPTED_PULL_CREEP = 0.0f;

// --- holding the fight at the camp ---------------------------------------
// A drag-back ends the moment the TANK is home, on a scripted stage exactly as on an
// ordinary trash pull: arriving IS the end of the maneuver, and the flip to Engage
// releases tank and party together.
//
// It briefly did not. Because the tag is taken at range from a stand spot, the pack
// starts its run ~42yd out and the tank is home several seconds ahead of it, and a
// GATHER radius held the tank on the camp until every live attacker had run in — on
// the theory that stock combat, handed a victim that far away, would chase it back
// into the room. But the drag is what empties the room: by the time the tank stands
// on the camp the whole pack is loose in the corridor behind it, so the chase that
// gate feared is a chase down open hallway, not into the room. Meanwhile the hold
// cost a real thing — the tank standing still, back to an inbound six-mob pack,
// building no threat while the released DPS opened on the runners.
//
// So ONE gate scoped to a scripted stage remains:
//
//   LEASH   for the whole camp fight, how far the tank may stray from camp before it
//           is walked back. Generous on purpose: it exists to catch a chase
//           excursion, not to fight the tank's own footwork. It used to have to stay
//           inside the ~20yd from camp to the doorway; with the camp 45yd back that
//           ceiling is slack. This — not an arrival hold — is what keeps the tank off
//           the doorway, and it applies for the entire fight rather than just its
//           first seconds.
//
// IT IS SIZED BY THE STEP-OUT, NOT BY THE FOOTWORK. It was 12yd — "planted, with
// footwork" — and that number is wrong the moment a ground effect lands ON the camp,
// because then the two rungs driving the bot are asking for incompatible places and
// neither ever wins:
//   * the generic avoid-aoe hops min(radius + 1, AiPlayerbot.FleeDistance) = 5yd from
//     wherever the bot is standing, and CheckLastFlee then forbids reversing that hop
//     for 5s — so a bot hauled back does not settle, it re-hops SIDEWAYS;
//   * MgT's Magic Dampening Field step-out is stricter still: it only accepts a spot
//     that clears every field by DAMPENING_CLEAR (9yd), off rings of 7/10/13/16yd
//     around the bot.
// And the bot doing the stepping is not standing on the camp anchor when the field
// lands — it is in melee on something that reached the camp, i.e. already ~5yd out.
// That puts the generic hop at ~10yd from camp and a dampening escape at ~14yd, both
// outside a 12yd leash. So EVERY legal step-out tripped the recall, the recall walked
// the bot back into the effect, and the effect pushed it out again. That is the
// ping-pong the player watched: not a chase, two correct rungs with contradictory
// destinations, neither of which can yield.
//
// 14 is that worst case with a yard of slack, and it is deliberately NOT the 18 this
// number was first set to. The leash is not just a ceiling on an excursion — with the
// release band below it also sets where the tank FIGHTS, and every yard of it is spent
// permanently, on every camp fight, whether or not anything was ever dropped on the
// camp. At 18/10 the tank's steady state was 10-20yd forward of the authored camp:
// live, one Selin camp fight logged seven leash trips (18.0, 18.1, 18.6, 19.0, 19.3,
// 20.8, 22.7yd) in under a minute, and eighteen across five minutes of play. That is
// the reported "tank runs forward" — not the walk-in, the camp fight.
//
// The ping-pong this constant was widened for is fixed by the two rungs that landed
// with it and not by the width: the recall RELEASES at DC_SCRIPTED_PULL_RECALL_HOME
// (outside any field centred on the camp) instead of marching the tank back onto the
// anchor, and a recall is dropped outright while the bot stands in a ground effect. 12
// already cleared both step-out bounds the tests pin; 14 keeps a 4yd band above the
// release so the latch cannot arm and clear on the same tick.
//
// It stays far short of what the plans need it to be short of: Selin's
// camp-to-doorway gap, and the 40yd of caster range every rotunda row keeps between
// its camp and every still-live pack. Both are pinned in t/TestScriptedPull.cpp.
inline constexpr float  DC_SCRIPTED_PULL_LEASH   = 14.0f;

// Where a tripped recall LETS GO. It used to be the generic 5yd camp-arrive ball, and
// that gap IS the ping-pong's amplitude: trip at the leash and release at 5 is a
// 13yd forced march back onto the exact point the bot was just pushed off — which,
// for an effect centred on the camp, means back into the middle of it.
//
// A recall exists to end an excursion, not to re-plant the tank on a coordinate, so
// it lets go once the tank is comfortably home. Wide enough to clear a field sitting
// on the camp (a dampening escape is only ever accepted at 9yd out), and still well
// inside the trip distance, so the latch cannot arm and clear on the same tick — the
// in-out shuffle the old 5yd ball was chosen to avoid.
inline constexpr float  DC_SCRIPTED_PULL_RECALL_HOME = 10.0f;

// --- clocks on a scripted stage's ground --------------------------------------
// A scripted stage's distances are authored, not emergent, and a row is free to put
// its camp a long way from its stand spot. Every wait measured across that gap has
// to be sized from the gap itself; the flat numbers below are only the floor.
//
// The floor, in ms: what something crossing a camp-to-stand gap of a few yards
// needs. This is the whole budget these waits used to have, back when that
// described every row.
inline constexpr uint32 DC_SCRIPTED_PULL_TRAVEL_BASE_MS = 8000;

// Yards per second credited to whatever is crossing that ground. Wretched trash and
// a bot both run at ~7-8 yd/s; 5 is deliberately pessimistic, because these budgets
// are WATCHDOGS — they must not expire on a healthy crossing, and paying for that
// with a slower assumed speed costs nothing, since each wait ends the moment the
// thing actually arrives rather than at its clock.
inline constexpr float  DC_SCRIPTED_PULL_TRAVEL_YD_PER_SEC = 5.0f;

// How long to allow for something to cross `yards` of a scripted stage's ground.
//
// THE FORMING DWELL uses it: the tank waiting for the party to park at camp before it
// tags. A flat 8s broke that once Selin's camp moved 42yd from its stand spots —
// roughly 6-9s of travel, which 8s only just fails to cover and then fails EVERY
// pull, so the dwell expired and the tank tagged with the followers still strung out
// along the hall behind it. (The gather hold was the other caller, until arrival
// stopped being something the tank waits past — see DC_SCRIPTED_PULL_LEASH above.)
//
// Still BOUNDED, for the original reason: a follower that cannot path must never be
// able to freeze the run.
inline constexpr uint32 ScriptedPullTravelBudgetMs(float yards)
{
    float const travelMs =
        (yards > 0.0f ? yards : 0.0f) / DC_SCRIPTED_PULL_TRAVEL_YD_PER_SEC * 1000.0f;
    return DC_SCRIPTED_PULL_TRAVEL_BASE_MS + static_cast<uint32>(travelMs);
}

// The FOLLOWERS' leash. Tighter than the tank's, for the reason it always was: the
// tank plants ON the camp and the pack piles onto it there, so a follower has less
// legitimate ground to cover than the tank does.
//
// But sized the same way as the tank's — by the STEP-OUT, not by melee reach. It was
// 8yd on the argument that a melee follower needs its fuzzed slot offset plus melee
// reach and no more, and that argument holds right up until something is dropped on
// the camp. A dampening-field escape is only ever ACCEPTED at 9yd or more from the
// field centre, so an 8yd leash and a field on the camp were unsatisfiable at the
// same time by construction: the follower stepped out, the leash pulled it straight
// back in, and it spent the fight walking instead of casting. That is what "the party
// never settles to dps or heal" looks like from the outside.
//
// 12 is a melee follower already ~5yd out on its mob plus the generic hop, and clear
// of the 9yd a dampening escape is accepted at. Same correction as the tank's above:
// this was first set to 15, which bought nothing the bound below does not already buy
// and spent it on a party that fights three yards further forward all fight.
inline constexpr float  DC_SCRIPTED_PULL_FOLLOWER_LEASH = 12.0f;

// How far past that leash a follower may stand to BRING ITS OWN TARGET INTO RANGE.
//
// The leash above is sized by the step-out, and the step-out is the only reason a
// held follower legitimately leaves the camp — right up until the thing it is
// supposed to be shooting refuses to come to the camp at all. A SmartAI range-mode
// caster stands off 25-35yd from the tank (see DC_SCRIPTED_PULL_CAMP_STEP), so it can
// easily sit 40-45yd from a camp the tank is planted on, and a 28.5yd spellDistance
// then makes the leash and the shot mutually exclusive: the follower walks out, gets
// in range, is recalled, and is out of range again the moment it arrives.
//
// Live (tp-20260808-211456-1), one hunter, one cycle, verbatim:
//     assist camp: LOS, out of range (40.3yd) -> yield to stock reach
//     assist camp: engaged C24685#22 in range+LOS (31.5yd) -> yield
//     hold-at-camp: walking to camp (8.8yd, ...)
// and round again. 5261 "LOS, out of range" lines and 2819 "no-LOS, closing" lines
// across that plan's twenty runs — the reported "ranged DPS run around like they
// can't get enough range".
//
// So the leash stretches by exactly the shortfall and no further (see
// ScriptedFollowerReachLeash). What bounds the stretch is the camps themselves: the
// tightest one any row uses is the rotunda's forward camp, which clears the nearest
// LIVE pack by 47.5yd against a ~21.5yd elite reach, leaving 26yd of ground a
// follower can stand on without waking anything. 22 keeps four of those in hand, and
// it is far more than the cases need — the hunter above wanted 14.6yd, and a Sunblade
// Magister parked 45yd off the camp wants 18.
inline constexpr float  DC_SCRIPTED_PULL_FOLLOWER_REACH_CAP = 22.0f;

// The follower's leash for one tick: the standing one, stretched just far enough to
// put `campToTarget` inside `attackRange`, and never past the cap.
//
// The 0.9 is what stops the stretch from landing the follower exactly on its own
// range edge, where one step of the mob's own movement puts it out again — the same
// reason the recall releases in a band rather than on a point.
inline constexpr float ScriptedFollowerReachLeash(float campToTarget, float attackRange)
{
    if (campToTarget <= 0.0f || attackRange <= 0.0f)
        return DC_SCRIPTED_PULL_FOLLOWER_LEASH;
    float const needed = campToTarget - attackRange * 0.9f;
    if (needed <= DC_SCRIPTED_PULL_FOLLOWER_LEASH)
        return DC_SCRIPTED_PULL_FOLLOWER_LEASH;
    return needed < DC_SCRIPTED_PULL_FOLLOWER_REACH_CAP
               ? needed : DC_SCRIPTED_PULL_FOLLOWER_REACH_CAP;
}

// Has the camp fight actually REACHED the camp? Measured mob-to-camp, and answered
// against the follower leash on purpose: inside it, a follower handed this mob is
// being handed something already standing on ground it is allowed to occupy, so the
// seed can never turn into a walk. Outside it, the same seed would point a follower
// at a mob it must leave the camp to reach, which is the one thing a scripted stage
// forbids.
//
// This is the gate on the NON-COMBAT assist's scripted stand-down. That stand-down
// used to be unconditional, and it closed the seed along with the walk: a follower
// the pack has not personally touched has no stock proactive picker (the DC
// multiplier zeroes "attack anything"/"pull start"/"reach pull"), no DC assist, and
// no instance kill order (MgT's focus triggers all require IsInCombat) — so it stands
// at the camp watching until a mob picks it. Measured across the 20 runs of
// tp-20260808-191156-1, from "party released" to a follower's own first combat entry:
// ordinary camp fights (assist live) 97 samples, median 1s, NONE over 15s; scripted
// rows (assist stood down) 402 samples, 25 over 15s, worst 86s. tr-20260808-191202-9's
// east row ran 62s against a 22s median for that row, with the hunter entering combat
// 59s after release — three seconds before the fight ended.
inline constexpr bool ScriptedCampFightHasReachedCamp(float mobDistToCamp)
{
    return mobDistToCamp <= DC_SCRIPTED_PULL_FOLLOWER_LEASH;
}

// --- the losing-ground ratchet -------------------------------------------------
// How much ground a leg that should only ever CLOSE may lose against its own
// best-so-far before it is re-issued from scratch. Giving ground is proof something
// else owns the bot's movement (a route spline the pull did not cancel, a chase, a
// knockback). Slack enough to ignore path noise and the arc around a doorway, tight
// enough that an outward excursion is caught within a second rather than after four.
inline constexpr float  DC_SCRIPTED_PULL_LOSE_GROUND = 2.5f;

// Has such a leg lost ground? `bestSoFar` is 0 when no leg is in flight.
//
// THREE legs need this and each one had to be found the hard way, so it is one
// predicate now rather than a fourth copy of the comparison:
//   * the tank's DRAG-BACK          (DcPullContext::scriptedReturnBest)
//   * the FOLLOWER hold-at-camp     (DcPullContext::campHoldBest)
//   * the tank's CAMP LEASH recall  (DcPullContext::scriptedRecallBest)
//
// They share a cause, not just a shape. DcMoveTo DEDUPES on destination, and all
// three re-issue one unchanged point every tick — so the moment another generator
// takes the bot, the leg reports "already going there", issues nothing, and goes
// SILENT. It is not refused, so nothing is logged; and the standing-still backstops
// cannot see it either, because the bot is moving perfectly well, just outward. That
// leaves distance as the only available evidence, which is what this reads.
inline constexpr bool ScriptedPullLostGround(float bestSoFar, float now)
{
    return bestSoFar > 0.0f && now > bestSoFar + DC_SCRIPTED_PULL_LOSE_GROUND;
}

// --- the camp fight's only clock -----------------------------------------
// Every OTHER leg of a scripted pull carries a watchdog; Engage carried none. It
// retires on one predicate — "is any member of this pack still on the party's
// attacker lists" — and if that predicate never goes false the stage never retires,
// which does not merely delay the run but wedges it: a latched stage pins the pull
// target, force-enables the pipeline, holds the party at the camp and stands the
// advance rung down. Nothing downstream can break that; the phase is the only place
// it can be broken.
//
// Live (tr-20260803-144046-4): the tank reached the camp, logged "back on the camp
// (4.5yd) -> fighting", and then said NOTHING for four minutes and thirteen seconds
// — Engage for 254s with three members combat-flagged, every victim empty and every
// health bar at 100%. The pack was on somebody's attacker list and nothing was
// happening. The run was still in that state when it was killed by hand.
//
// PROGRESS, NOT WALL CLOCK. A camp fight's length is set by the pack and the party's
// damage, not by any distance the row authored, so there is no honest budget to size
// it against — and a flat ceiling generous enough for a slow six-mob heroic pack
// (tr-20260803-144046-8 spent 137s on one stage) is too generous to catch a freeze
// quickly. Watching the pack's summed health instead separates the two outright: a
// fight that is happening moves it within seconds, so a legitimate fight of any
// length re-arms the clock over and over and can never trip. Only a fight that has
// actually stopped runs the window down.
//
// 45s of a heroic pack's health not moving by even a percent is not a slow fight, it
// is a stopped one — long enough to ride out a chain-CC or a healer drinking through
// a bad pull, far short of the four minutes this cost.
inline constexpr uint32 DC_SCRIPTED_PULL_ENGAGE_STALL_MS = 45000;

// Has the camp fight stopped happening? `since` is the ms the health signature last
// changed; 0 means "not sampled yet", which can never be stale.
inline constexpr bool ScriptedPullEngageStalled(uint32 since, uint32 nowMs)
{
    return since != 0 && nowMs > since &&
           (nowMs - since) > DC_SCRIPTED_PULL_ENGAGE_STALL_MS;
}

// --- the camp that walks forward -------------------------------------------------
//
// A CAMP IS ONLY A CAMP IF THE FIGHT CAN HAPPEN AT IT, and against one class of mob it
// cannot. AzerothCore's SmartAI has a RANGE MODE: SmartAI::InitializeAI takes the
// first SMARTCAST_COMBAT_MOVE cast a creature owns (there is no SMARTCAST_MAIN_SPELL
// anywhere on this trash) and calls SetMainSpell, which sets
//   _attackDistance = spellMaxRange - NOMINAL_MELEE_RANGE
// and then chases with MoveChase(victim, _attackDistance). ChaseRange(float) has
// MinRange 0, so such a mob never backs away — but it never closes past that distance
// either. It stands there and shoots. For the rotunda that is:
//
//   Sunblade Magister  Frostbolt 44606      40yd -> stands off at 35yd
//   Sunblade Warlock   Immolate 44518       30yd -> stands off at 25yd
//   Coilskar Witch     Forked Lightning 20299 30yd -> stands off at 25yd
//
// Every one of those is outside DC_SCRIPTED_PULL_LEASH (14) and well outside
// DC_SCRIPTED_PULL_FOLLOWER_LEASH (12). So a camp fight whose last live member is one
// of them is unwinnable BY CONSTRUCTION, and it fails in a very particular way: the
// tank's rotation chases the only thing it can see, trips the leash at 14, is recalled
// to the 10yd release band, yields the tick, and chases again — while the followers,
// pinned at 12, log "LOS, out of range" or "no-LOS" at it and never fire.
//
// Live (tr-20260808-211502-8, the north-east row): 42 leash trips between 21:29:38 and
// 21:32:05, every one of them 14.1-14.8yd out and back to 9.5-10.0, with the pack's
// health frozen at 88% the whole time — one Sunblade Magister, alive and untouched,
// standing where nobody was allowed to go. 218 trips across that plan's twenty runs.
//
// THE ANSWER IS TO MOVE THE CAMP, NOT TO LENGTHEN THE LEASH. Widening the leash was
// tried and reverted (see DC_SCRIPTED_PULL_LEASH: at 18/10 the tank's steady state
// moved 10-20yd forward of the authored camp on EVERY camp fight, whether or not
// anything was ever standing off). And it would not even work here — 35yd of stand-off
// is not a leash length any camp can afford. What a human tank does instead is walk up
// to the caster and hold there, and the party moves up with them.
//
// SO THE CAMP WALKS, along the camp->stand-spot segment and no further. The stand spot
// is the one forward point on this row that the plan has already reasoned about: it is
// authored ~26yd from the pack and cleared against every pack the plan has not pulled
// yet (the margins table in each row's comment), and the tank has already walked to it
// once this pull to take the tag. Anything past it is ground nobody measured.
//
// Step size. One leash-band per trip: far enough that a step is worth taking (a 35yd
// stand-off needs two or three of them), small enough that the party never jumps a
// whole room on one bad sample. The clamp to the segment is what bounds the total.
inline constexpr float  DC_SCRIPTED_PULL_CAMP_STEP = 12.0f;

// How long the fight has to have stopped before the camp walks. Deliberately far
// shorter than DC_SCRIPTED_PULL_ENGAGE_STALL_MS, because these are answers to the same
// evidence at different confidence: eight seconds of a heroic pack taking no damage
// while something sits outside the leash is enough to try moving up, and 45s of it
// after we have already walked the camp to the stand spot is the admission that the
// stage is over. The stall watchdog therefore stays exactly as it was — this rung runs
// in front of it and re-arms the progress clock whenever it fires, so a camp that IS
// walking cannot also be retiring.
inline constexpr uint32 DC_SCRIPTED_PULL_STANDOFF_MS = 8000;

// Has the camp fight stalled long enough to walk the camp at whatever is standing off?
// Same `since` contract as ScriptedPullEngageStalled.
inline constexpr bool ScriptedPullStandoffStalled(uint32 since, uint32 nowMs)
{
    return since != 0 && nowMs > since &&
           (nowMs - since) > DC_SCRIPTED_PULL_STANDOFF_MS;
}

// --- The muster ------------------------------------------------------------------
//
// A stage is a PLANNED fight against a hand-counted pack, so the party should walk
// into it the way it walks into a boss — topped up — and not merely "no longer
// resting". The ordinary between-pulls floors are min(90, AlmostFullHealth) HP and
// min(75, HighMana) mana, i.e. 85/65 on stock config, and they are sized for the
// emergent case where the next pull is whatever the corridor scan found. For an
// authored five-elite heroic pack that contains its own healer they are too low, and
// the gap shows up as the tank dying in the fight's opening seconds.
//
// Live (tr-20260805-191834-3): "Waiting on Shannon (low mana), Erinerice (low HP)" at
// 12:25, gate released 12:30, stage armed, tank dead at 12:52, party wiped by 13:14.
// The floors were satisfied. They were satisfied at 65% healer mana.
//
// THESE SIT ABOVE WHAT STOCK BOTS RESTORE TO, which is deliberate and is why the wait
// is bounded (see DC_SCRIPTED_PULL_MUSTER_MS). DungeonClearNeedsEat/DrinkTrigger is
// raised to the same numbers while a stage is pending so the floors are reachable
// rather than merely demanded; a bot with nothing to eat still has natural out-of-
// combat regen, and the timeout covers the rest.
inline constexpr float  DC_SCRIPTED_PULL_MUSTER_HP = 90.0f;
inline constexpr float  DC_SCRIPTED_PULL_MUSTER_MP = 80.0f;

// How long the muster may hold the plan. Bounded for the same reason every wait in
// this pipeline is: a stage that can never arm is a run that never finishes, and that
// is a strictly worse failure than a pull taken at 70% mana. Sized to a drink from
// HighMana (65) to the floor above with a wide margin — a full drink cycle is ~10s —
// so in practice the timeout is a backstop and not the usual exit.
inline constexpr uint32 DC_SCRIPTED_PULL_MUSTER_MS = 40000;

// The substance floor: once a muster has ARMED — the party was genuinely below the
// floors — it holds at least this long even if the percentages close sooner. The
// release test is an instantaneous percentage over a 5-point band, so one AoE heal
// satisfied it in 1-5s and stages armed against parties that never sat down
// (tp-20260806-212646-1: 115/184 musters ended <=5s the plan before). Sized to one
// real drink cycle: sit latency plus 2-3 drink ticks. A party already at the floors
// when the stage comes due never arms a muster and pays nothing.
inline constexpr uint32 DC_SCRIPTED_PULL_MUSTER_MIN_MS = 8000;

class ScriptedPullRegistry
{
public:
    // --- table access -----------------------------------------------------
    // Cheapest possible gate: most maps have no scripted pull at all and pay one
    // integer compare per call.
    static bool HasRows(uint32 mapId);
    // Every row on `mapId`, ascending by `order`.
    static std::vector<ScriptedPullStage const*> Rows(uint32 mapId);
    // The row with this `order` on this map, or nullptr. `order` is int32 so the
    // "no stage" sentinel (-1, as stored in DcPullContext::scriptedStage) can be
    // passed straight through.
    static ScriptedPullStage const* Find(uint32 mapId, int32 order);

    // --- pure predicates (no world state; unit-tested directly) ------------
    // Is (x,y,z) inside the stage's pack cylinder?
    static bool InPack(ScriptedPullStage const& s, float x, float y, float z);
    // Is `entry` one of the stage's pack members?
    static bool IsPackEntry(ScriptedPullStage const& s, uint32 entry);
    // Is a bot at (x,y,z) close enough to the arm anchor for the stage to arm?
    static bool InArmRange(ScriptedPullStage const& s, float x, float y, float z);
    // Which stage should run? `orders`/`live` are parallel and ascending: `live[i]`
    // is true while stage `orders[i]`'s volume still holds a live pack member.
    // `pinned` is the order of a stage a maneuver is already committed to (-1 for
    // none) and always wins — a stage in flight has DRAGGED its pack out of its own
    // volume, so re-deriving from `live` mid-drag would hand the tank the next pack
    // while it is still hauling this one. Returns the order to run, or -1.
    static int32 SelectOrder(std::vector<uint32> const& orders,
                             std::vector<bool> const& live, int32 pinned);

    // Does `u` belong to `s`'s pack — right entry, standing inside the stage's own
    // volume? The predicate the pull pipeline's positional vetoes except on.
    static bool IsStageTarget(ScriptedPullStage const& s, Unit const* u);

    // --- live resolution --------------------------------------------------
    // The stage the pull pipeline should be running right now, or nullptr. Applies
    // the boss gate, the arm gate, the in-flight pin and SelectOrder.
    //
    // UNCACHED and not cheap — it runs one entry-filtered volume scan per candidate
    // stage. Everything on the per-tick path goes through
    // DcTickMemoAccess::ScriptedStage instead, which memoises this within the tick.
    static ScriptedPullStage const* DueStage(Player* bot, AiObjectContext* ctx);
    // Nearest live, reachable pack member of `s` — the pull target for the stage.
    static Unit* NearestPackMember(Player* bot, AiObjectContext* ctx,
                                   ScriptedPullStage const& s);
};

#endif  // _PLAYERBOT_SCRIPTEDPULLREGISTRY_H
