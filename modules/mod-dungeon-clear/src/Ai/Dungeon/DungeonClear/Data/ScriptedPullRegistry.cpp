/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "ScriptedPullRegistry.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "Player.h"
#include "Position.h"
#include "Unit.h"

#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Playerbots.h"

namespace
{
    // --- Magisters' Terrace (585) — Selin Fireheart's guard packs -------------
    //
    // Room geometry, all live-measured (acore_world.creature, tools/probe_navmesh.py,
    // and the in-game coordinates the design was authored from):
    //
    //   * Selin (24723) at (242.1, 0.3, 1.8). CanAIAttack is X > 216, and his aggro
    //     radius against a level-70 party is ~21yd.
    //   * The room proper opens at X=219; in front of it is a doorway slot at
    //     X 215-218 roughly Y -6..+6 (the Assembly Chamber Door, GO 188065, hangs
    //     at X=215.1), and in front of THAT an empty staging chamber spanning
    //     X 197-213. Everything at |Y| > ~6 with X < 219 is wall.
    //   * Back from the staging chamber a long hall runs down to X~135, where it
    //     opens into the Sunblade antechamber (the last trash before the room:
    //     four elites at X 129-135 / Y -14..-23 and four more at X 119-128 /
    //     Y +15..+21, all of them dead by the time any stage arms).
    //   * EAST pack (the -Y side; `creature_formations` leader 96831): six Wretched
    //     at X 222-230, Y -16..-23, around the fel crystal at (225.97, -20.08).
    //   * WEST pack (the +Y side; leader 96828): six Wretched at X 222-230,
    //     Y +17..+24, around the crystal at (226.31, 20.22).
    //   * CENTRE PAIR (leader 96825): a Skulker at (231.3, 2.8) and a Bruiser at
    //     (232.1, -2.0), ~10yd from Selin. Deliberately NOT a stage — they cannot be
    //     peeled off him, so they belong to the boss pull.
    //
    // Why these coordinates and not others:
    //
    //   CAMP (170.46, 0.57) sits mid-corridor, user-measured, 45.5yd short of
    //   Selin's CanAIAttack plane and 42yd from either stand spot. The number that
    //   makes it work is 55yd — the distance to the nearest guard spawn of either
    //   pack. The pack's two Wretched Husks cast 44503 Fireball / 44504 Frostbolt,
    //   both 40yd, and their smart_scripts rows carry castFlags 64
    //   (SMARTCAST_COMBAT_MOVE), which makes the core switch a mob's combat movement
    //   OFF the moment its target is in range AND in line of sight. So a camp inside
    //   40yd of the room is one a Husk answers by planting in the doorway and
    //   shooting, which is what held the fight open across the doorway at the
    //   original 20yd camp. Past 40 it has no such option and has to run the
    //   corridor, which is what a drag-back is for.
    //
    //   45.5yd under the boss plane is the second number: a Selin tagged by accident
    //   and dragged toward here crosses X=216 long before he arrives, stops being
    //   able to attack anything, and resets. The worst mistake available in this
    //   room is free.
    //
    //   Nearest spawn of any kind is 11.5yd (Blood Knight 96775 of the X 179-182
    //   pack), so the camp is clear floor — checked against `creature`, not by eye.
    //
    //   The camp's own predecessor (209.58, 18.97, -2.05) survives as the ARM
    //   ANCHOR, which is the job it was always doing as well as being the camp: the
    //   tank crosses a 25yd radius of it entering the staging chamber, which is
    //   after the last Sunblade pack (X <= 182.3, 27yd+ out) and before the route
    //   would carry it into the doorway.
    //
    //   The TWO STAND SPOTS are a mirrored pair either side of the doorway's centre
    //   line — same X, same Z, Y negated — and BOTH are outside the room. Each one
    //   shoots a diagonal through the doorway at the pack on the FAR side, and is
    //   walled off from the pack on its own side. That is the whole trick, and it
    //   is why neither pull ever has to set foot past X=216:
    //
    //   EAST STAND (212.22, +7.42) -> the -Y pack. The line to it crosses X=216 at
    //   Y ~ 0, dead centre of the doorway, at 25-27yd: far outside the pack's ~19yd
    //   aggro, hence the ranged tag. The same spot's line to the +Y pack crosses
    //   X=216 at Y ~ +11, which is wall, so the pack 16yd away on its own side
    //   cannot see it.
    //
    //   WEST STAND (212.24, -7.41) -> the +Y pack, the exact mirror. Nearest member
    //   27.8yd (inside a 30yd shield, which is why the tag has to be aimed at the
    //   NEAREST member — see NearestPackMember); Selin 30.8yd; the centre pair
    //   20.2yd but on a line that leaves the doorway at Y ~ -6.3, i.e. wall. The
    //   -Y pack is only 14.6yd from here, which is fine precisely because the stage
    //   ordering guarantees it is dead before this stage ever arms.
    //
    // The pack cylinders are r=12 around each pack's crystal. That holds all six
    // spawns (they span ~8yd) and excludes every neighbour: the centre pair is
    // 22.9yd from the east centre and 18.5yd from the west, and Selin is 25.5 /
    // 25.8yd out. The entry filter is exact — 24688 Skulker / 24689 Bruiser /
    // 24690 Husk exist nowhere else on map 585 — and it is what keeps the fel
    // crystals (24722, faction 190, sitting at the dead centre of both cylinders)
    // from reading as live pack members and stalling the stage forever.
    //
    // ARM RADIUS 25 from the ARM ANCHOR (209.58, 18.97), never from the camp. The
    // camp is 43yd back from the anchor, and a 25yd radius drawn at the camp instead
    // would cover the X 179-182 Sunblade pack 11.5yd away — the stage would arm while
    // that pack is still the run's live problem, take the pull pipeline off it, and
    // pin the party in the middle of it.
    uint32 constexpr MGT_MAP            = 585;
    uint32 constexpr MGT_SELIN          = 24723;
    uint32 constexpr MGT_WRETCHED_SKULK = 24688;
    uint32 constexpr MGT_WRETCHED_BRUIS = 24689;
    uint32 constexpr MGT_WRETCHED_HUSK  = 24690;

    // --- Magisters' Terrace (585) — the rotunda between Vexallus and Delrissa ----
    //
    // The second plan on this map, and the first one whose packs are separated by
    // DISTANCE rather than by walls. Selin's two stages hide behind a doorway; these
    // five stand in the open.
    //
    // THE ROOM. A rotunda ~65yd across centred on (126.9, -129.6, -19.7), floor at
    // Z -20.2..-21.3 and single-level (the navmesh column holds one surface in that
    // band anywhere inside it). It is the run's third leg: Selin -> Vexallus ->
    // HERE -> Delrissa (126.9, 19.2), who is the boss this plan is gated on. The
    // party arrives from Vexallus down the east corridor, and leaves north.
    //
    // Five formations, 23 elites, and they are CLOSE — 12 to 23yd between the
    // nearest members of neighbouring packs, against a ~20-21yd elite aggro reach on
    // a level-70 party. There is no spot in this room that is "outside aggro"
    // full stop; every stand spot below is only outside the aggro of the packs that
    // are still ALIVE when it is used, which is what makes the ordering load-bearing
    // rather than cosmetic.
    //
    //   `creature_formations` leader, centroid, and the entries in each:
    //     S   96778  (117.24, -155.37)  5 mobs  — Blood Knight, Warlock, Magister,
    //                                             Mage Guard, Sister of Torment
    //     E   96824  (150.85, -132.46)  4 mobs  — Physician, Magister, Warlock, Mage Guard
    //     C   96777  (126.54, -128.90)  4 mobs  — Physician, Mage Guard, Blood Knight, Warlock
    //     NE  96780  (140.73, -109.45)  5 mobs  — Warlock, Blood Knight, Ethereum
    //                                             Smuggler, Sister of Torment, Magister
    //     NW  96767  (112.17, -110.32)  5 mobs  — Mage Guard, Sister of Torment,
    //                                             Coilskar Witch, Physician, Warlock
    //
    // THE BACK CAMP (141.70, -211.71, -21.13) is user-measured, and it is not in the
    // room at all: south of the rotunda the floor runs neck, small room, neck, hall —
    // the rotunda's mouth is a neck ~12yd wide at Y -170..-173, then a chamber
    // spanning X 114-138 / Y -174..-194, then a second neck at Y -195..-198, then the
    // hall the camp stands in. The number that makes the back camp work is 57.1yd —
    // the distance to the NEAREST spawn of any of the five packs (Sister of Torment
    // 96843 of the south pack).
    //
    // 57yd is the same argument Selin's camp makes at 55, against the same problem
    // and, this time, three castFlags-64 rows rather than two: Sunblade Magister's
    // Frostbolt (44606/46035), Sunblade Warlock's Immolate (44518/46042) and Coilskar
    // Witch's Forked Lightning (20299/46150) all carry SMARTCAST_COMBAT_MOVE, which
    // makes the core switch a mob's combat movement OFF while its target is in range
    // AND in line of sight. A camp inside a caster's reach is one it answers by
    // planting and shooting instead of coming; past that reach it has to run, which
    // is what a drag-back is for. (The exact reach is not derivable from this
    // deployment's DB — spellrange_dbc is empty — but 40yd is the long-range cap for
    // this class of mob spell in 3.3.5a, and 57.1 clears it by 17.)
    //
    // WHAT THE BACK CAMP COSTS is drag length, and it only buys anything for the two
    // packs that are actually near it. One camp served all five to begin with, and the
    // hauls ran 31.7yd (south) to 76.7yd (north-west) — nearly double Selin's 42.
    // Nothing about that is unsafe; two facts keep a long haul honest, and neither is
    // a gate that can fail:
    //   * INSTANCED CREATURES DO NOT LEASH. Creature::CanCreatureAttack returns true
    //     unconditionally for a non-player-owned creature once GetMap()->IsDungeon(),
    //     before it ever reaches the CONFIG_CREATURE_LEASH_RADIUS home-distance test.
    //     A 77yd drag cannot reset the pack; only a boundary or a genuine no-path can,
    //     and this trash has neither.
    //   * EVERY WAIT ON THIS GROUND IS ALREADY DISTANCE-SIZED — the tag/return leg
    //     watchdog (DcPullLegTimeoutMs) and the forming dwell (ScriptedPullTravelBudgetMs).
    //     The spread gate is waived outright across Forming/Advancing/Returning
    //     (DcLeaderSignal::IsPullPhaseHolding), so a tank 77yd from its camp-held party
    //     is not a straggler to anything.
    // What it costs is everything a long haul spends rather than breaks: a minute of
    // wall clock per stage, a tank out of the healer's range for most of it, and the
    // whole width of the room for a chase excursion to go wrong in.
    //
    // THE FORWARD CAMP (137.11, -179.23, -21.43) is user-measured, and it is not the
    // middle of the small room — it is TUCKED AGAINST THE ROOM'S EAST WALL, about 3yd
    // off it, and the wall is the entire point.
    //
    // WHY THE WALL. The rotunda's mouth is a neck ~11yd wide: at its narrowest (Y -170
    // and Y -173) the floor spans X 121.5-132.25, and every sight-line from this room
    // into the rotunda has to thread it. Standing in the middle of the chamber does not
    // thread it — it aims straight up the middle. From (126.00, -184.00), the spot this
    // row used first, the neck is OPEN to twelve of the live pack members: two of the
    // east pack, and all five of both northern ones. From the wall the same twelve are
    // all blocked, because the line leaves the chamber at X 134.5-139.8 and hits wall.
    // Tightest of them is north-west Physician 96818 at X 134.47, clearing the neck's
    // 132.25 lip by 2.2yd; the rest have more.
    //
    // That matters for the mechanic this whole plan is built around. A castFlags-64
    // caster (SMARTCAST_COMBAT_MOVE) plants and shoots only while its target is in
    // range AND IN LINE OF SIGHT; take either away and it has to run. The back camp
    // takes the RANGE away, at the cost of a 58-85yd haul. The forward camp takes the
    // LINE OF SIGHT away instead, which is why it can afford to be close.
    //
    // It has to, because the range margin here is thin: 43.97yd to east Physician 96824
    // against a 40yd reach is four yards, where the back camp had seventeen. If the
    // wall were not doing the work this spot would be the wrong one — so the LOS
    // property is asserted rather than assumed (RotundaForwardCampHasNoSightLineIntoTheRoom).
    //
    // WHAT IT BUYS. Hauls of 26.6yd (east), 47.3 (north-east) and 52.2 (north-west)
    // measured from each row's body-tag point, against 58.4 / 79.7 / 84.7 from the back
    // camp. It also sits 14.4yd from the arm anchor, so the plan re-arms the moment a
    // drag ends with no dead walk between stages, and 35.6yd from the nearest point of
    // the hall patrol's waypoint path.
    //
    // IT IS ONLY SOUND BECAUSE OF THE ORDER, and the number that says so is 27.0yd —
    // the forward camp's distance to south Sister of Torment 96843, which is also the
    // one pack it still has an open sight-line to. That is inside every reach that
    // matters, so a party standing there with the south pack alive is a party in a
    // fight it did not choose. Every row that uses the forward camp is one that by
    // construction cannot arm until the SOUTH row is empty
    // (ScriptedPullRegistry::SelectOrder), so the mobs it is inside the reach of are
    // dead before anyone stands there. Move the forward camp onto the south row and
    // that stops being true.
    //
    // THE CENTRE ROW MOVED FORWARD (was the back camp until tp-20260807-203840-1).
    // South is the only row the forward camp's 27.0yd argument excludes, and centre
    // runs after it — so centre was paying the back camp's haul for a constraint that
    // did not apply to it. What that cost is the single largest number in the plan and
    // it is a number about the TANK, not about aggro: on a body-pull row the tank
    // walks to the stand spot ALONE, tags with its body, and runs the whole way home
    // alone with the pack on it while the party holds at the camp. From the hall that
    // is a 62.9yd solo run; from the forward camp it is 34.2. Live, across twenty
    // heroic runs, "the puller died mid-pull" on THIS row seven times — more than
    // every other row in the dungeon put together — and each of those killed the run,
    // because a party parked at a camp nobody is dragging to meets the pack standing
    // still.
    //
    // The forward camp clears the centre pack by 47.5yd (nearest member Mage Guard
    // 96766 at (123.05,-133.24)), so the same two properties every other row's camp
    // has still hold: outside a 21yd elite reach, and outside the 40yd a castFlags-64
    // caster answers by planting instead of coming. TheForwardCampOnlyServesRowsWhose
    // BlockersAreDead is what asserts that, for this row along with the other three.
    //
    // WHAT IT COSTS is elbow room. The chamber's east wall curves in south of Y -179,
    // so the camp has clear floor for about a yard in every direction and two to three
    // yards everywhere except east-south-east. Follower slots that fan into the wall
    // get clamped by the navmesh; that is the price of the sight-line and it was
    // measured in game rather than derived.
    //
    // FOUR OF THE FIVE STAGES ARE BODY PULLS (ScriptedPullStage::bodyPull). Live:
    // pulling those formations with a ranged opener brings NEIGHBOURING packs that the
    // same pull taken by body contact leaves asleep. See `bodyPull`'s comment for why
    // such a row forbids the opener rather than trying to buy the difference back with
    // geometry. The consequence is that a stand spot is a WAYPOINT, not a firing
    // position: the tank walks to it and then walks ~8yd further, to the aggro edge of
    // its own pack, and it is THAT point — not the spot — that has to clear every pack
    // the plan has not pulled yet.
    //
    // The EAST row is the exception and the next block is why.
    //
    // WHAT THE OPENER ACTUALLY CHANGES, since the difference is real and the mechanism
    // is not the one the first version of this comment guessed. CallAssistance is not
    // one event at tag time. Unit::Attack sets m_assistanceTimer to
    // CreatureFamilyAssistancePeriod, and Creature::Update then re-runs
    // SetNoCallAssistance(false) + CallAssistance() EVERY 3 SECONDS for the whole
    // fight, from wherever the mob is standing at that moment. So there are two ways a
    // neighbour is taken:
    //   * THE T=0 CALL, issued from the mob's SPAWN and engaging the neighbour exactly
    //     CreatureFamilyAssistanceDelay (2000ms) later. Nothing about the opener
    //     changes it — it is fired before anything has moved.
    //   * THE PERIODIC RE-CALLS, which re-run the whole radius search from the mob's
    //     CURRENT position. These are entirely opener-dependent, because the opener is
    //     what decides where the fight happens: a body pull has the pack in contact and
    //     dragged off its spawn within a tick, while a ranged tag leaves it fighting
    //     near where it stood, re-rolling the search until one connects.
    // The radius never changes — it is a flat config either way. What changes is how
    // many times it is SAMPLED from inside a neighbour's bubble. That is why the margin
    // is the thing to look at rather than the flag: a pair with tenths of a yard of
    // slack is decided by one tick of movement and a body pull clears it; a pair that
    // is inside at T=0 is not a pull-style question at all.
    //
    // THE ORDER is hand-authored: S -> C -> E -> NE -> NW. It is not the camp-distance
    // ordering it started as (S 57.1, E 74.4, C 80.3, NE 97.6, NW 102.2 — which would
    // put E before C), and the swap is deliberate. What the order actually has to
    // guarantee is that no stage's approach or tag point sits in a live pack's aggro,
    // and either ordering can be made to; C-before-E is the order the room was
    // observed to want.
    //
    // It is NOT free, and what it costs is the centre stand spot — which is why the
    // rows below are not simply the old rows renumbered. Approached from due south,
    // the only ground that reaches the centre pack runs straight between it and the
    // east pack, which under this order is still up: the old (132.50, -158.00) spot
    // leaves its body-tag point 21.2yd from east Magister 96796, against that mob's
    // 20yd reach. A yard of margin is no margin. So stage C now comes up the WEST side
    // instead, across the ground the south stage just cleared, and its tag point
    // stands 34.6yd off the nearest live mob. Reorder these rows again and the spots
    // have to be re-derived, not renumbered.
    //
    // THE STAND SPOTS are navmesh-verified floor (mmaps tile 585, the room's own Z
    // band), each placed ~26yd from its pack's nearest member: far enough that walking
    // UP to the spot is not itself the pull (a level-71 elite reaches 21yd), close
    // enough that the unauthored last leg — spot to aggro edge — is only about eight
    // yards long.
    //
    //   stage  stand spot                target        body-tag point     nearest    drag
    //                                                  (18yd out)         live pack
    //   S      (125.50, -184.50, -21.27) 96843 26.0yd  (123.46, -176.75)  C  43.5yd  31.7yd
    //   C      (111.50, -156.50, -20.92) 96766 26.0yd  (115.04, -149.36)  NW 35.3yd  34.2yd
    //   E      (139.00, -160.00, -21.14) 96824 25.8yd  (143.16, -153.37)  NE 40.1yd  19.3yd
    //   NE     (141.50, -140.00, -21.20) 96808 26.0yd  (140.86, -132.05)  NW 32.2yd  39.5yd
    //   NW     (122.50, -137.50, -20.20) 96767 26.0yd  (120.26, -129.82)   —      —  44.2yd
    //
    // (The drag column is stand spot to that row's OWN camp: the back camp for S, the
    // forward camp for C, E, NE and NW. C's 34.2 was 62.9 while it hauled to the hall
    // — see "THE CENTRE ROW MOVED FORWARD" above for why that number was the one that
    // killed runs.)
    //
    // The "nearest live pack" column is the number the plan lives or dies on, and the
    // margin it carries is that distance minus the mob's reach: +23.5, +14.3, +19.1,
    // +12.2 yards. 18yd is where the walk-in aims — GetAggroRange (detection_range 20
    // for every Sunblade entry, 20 - (playerLevel - mobLevel)) less the 2yd the tag
    // leg backs off by so the arrival itself trips MoveInLineOfSight.
    //
    // A pack that does not notice the arrival is walked into a little further, and on
    // a scripted stage that creep is bounded to 4yd (DC_PULL_SCRIPTED_CREEP_LIMIT), so
    // the worst stop is 14yd out and the worst margin +9.7. It is bounded BECAUSE of
    // this table: unbounded, the creep runs to body contact, and the table's whole
    // point is that these rows have 12-24yd of margin at the aggro edge and none at
    // all at the spawn. Live, before the bound existed (tp-20260803-232932-1): every
    // rotunda stage walked to 4.3yd, took its neighbours with it, and all ten runs
    // stalled or wiped in this room.
    //
    // Only the NEAREST member is aimed at — NearestPackMember ranks from the stand
    // spot and the formation brings the rest — so the far side of a pack sitting 35yd
    // out is not a defect. Every spot is between the pack and the camp, so the
    // drag-back is a straight run home rather than a loop around what is being dragged.
    //
    // The centre spot is 3.0yd from south Mage Guard 96768's spawn and sits inside the
    // south pack's footprint. That is not the "measured with a mob targeted" mistake
    // the header warns about — it is the west approach, and the south pack is stage 2
    // and dead before stage 3 can arm. There is no other floor on that side.
    //
    // THE PACK CYLINDERS are sized per pack (the spawns are not equally tight: the
    // east pack spans 8.4yd from its centroid, the south pack 4.6). Each radius holds
    // all of its own spawns and clears the nearest member of every other pack:
    // S r=7 (own 4.6, others 22.9), E r=11 (8.4 / 19.9), C r=9 (6.3 / 19.6),
    // NE r=8 (5.6 / 16.8), NW r=8 (5.0 / 18.2). Checked against every one of the 72
    // spawns of these eight entries on map 585 — each cylinder holds exactly its own
    // formation and nothing else, including the unaffiliated Sunblade Magister 96792
    // at (126.87, -90.39) north of the room.
    //
    // THE ENTRY FILTER is the union of all five packs' entries, because these packs
    // SHARE entries — unlike Selin's room, where the filter alone identified the
    // guards. Here the cylinder does all of the disambiguating and the entry list only
    // keeps non-members out. It earns its place anyway, for exactly Selin's reason:
    // Broken Sentinel 96948 (24808) is a hostile-faction prop on NullCreatureAI
    // sitting 0.7yd from the EAST pack's centroid — dead centre of that cylinder,
    // the same trick the fel crystals play in Selin's. Counting it would mean the
    // east stage never reported its pack cleared and the plan stopped there forever.
    // The Sunblade Warlocks' summoned imps (44517, cast out of combat) are left out
    // for the mirror-image reason: an imp that outlives its pack must not hold the
    // next stage down, and it dies at the camp with everything else regardless.
    //
    // THE ARM ANCHOR (127.50, -190.00) is the mouth of the neck, 25.9yd forward of
    // the camp, with a 20yd radius — so the row names one rather than measuring from
    // its camp, and for a reason Selin's row does not have. Sunblade Sentinel 96945
    // PATROLS the hall the camp stands in, waypoint path 969450 running the south
    // wall from (105.54, -214.95) east to (137.00, -214.83) — which passes 5.6yd from
    // the camp. Route trash, killed on the way in, but a 20yd radius drawn at the
    // camp would cover its whole east half, so a stage could arm off a tank standing
    // next to a live patrol and pin the party there, passive. Measured from the neck
    // instead, the anchor clears that patrol line by 4.9yd and the camp by 5.9, while
    // still sitting 31.8yd short of the nearest pack spawn: the tank arms about six
    // yards after it leaves the camp heading for the room, and re-arms the same way
    // between stages. Nothing else lives in the 47yd of corridor between the camp and
    // the room, so there is no trash for an armed plan to hijack the pipeline off.
    //
    // THE HALL PATROL IS A STAGE, and it is the plan's first one. The arm anchor above
    // keeps a live Sunblade Sentinel from ARMING a rotunda stage, which was only ever
    // half the problem: a sentinel standing at the west end of its path when the party
    // walks in is never engaged at all, and then walks back east — 5.6yd from the back
    // camp — some tens of seconds later, in the middle of whatever stage is running by
    // then. Live (tr-20260804-175340-3): "Fighting Sunblade Sentinel" arrives one
    // second after "Pulling the pack back to camp", and the run wiped in that room.
    //
    // A patrol is not something a gate can wait out safely, because "not in range
    // right now" is a fact with a 35-second shelf life: the path is 31.5yd end to end
    // with a 5s dwell at each waypoint. So the plan does not wait it out — it names
    // the patrol as stage order-2 and kills it first, and every later stage inherits
    // the prerequisite for free, because SelectOrder will not look past a stage whose
    // volume still holds a live member.
    //
    // The row is the ordinary machinery pointed at a moving target:
    //   * PACK CYLINDER (121.34, -214.89, -21.40) r=24, z-band 6. Centred on the
    //     midpoint of waypoint path 969450 — (105.54, -214.95) to (137.00, -214.83) —
    //     which the 24yd radius covers end to end with 8yd of slack for a sentinel
    //     between waypoints, and which also holds the back camp (20.7yd) so a dragged
    //     sentinel does not leave its own volume mid-fight. Nothing else is in the
    //     hall: the nearest other spawn is a squirrel 74yd north.
    //   * ENTRY FILTER {24777}, and the z-band is what keeps it honest. The OTHER
    //     Sunblade Sentinel on this map, 96944, patrols the corridor back toward
    //     Vexallus on path 969440 — same entry, 12yd higher at Z -9.5 and 65yd east,
    //     so either filter alone would do and both together cost nothing.
    //   * STAND SPOT = THE CAMP. A stand spot is a measured vantage on a pack that
    //     does not move; against one that does, the only honest answer is "start from
    //     the camp and walk at it", which is what a body-pull row with the camp as its
    //     stand spot does. In the common case the sentinel is walking toward the camp
    //     anyway and the walk-in is a few yards.
    //   * ARM ANCHOR (130.00, -198.00) r=30, NOT the rotunda's. This row has to arm
    //     while the tank is still coming down into the hall, so it covers the hall,
    //     both necks and the small room — everything between the corridor mouth and
    //     the rotunda's own neck. It stops 40.7yd short of the corridor descent at
    //     (167.26, -214.30), so it cannot hijack the pipeline off the trash back
    //     there, and 40.2yd short of the south pack's nearest spawn, so it never
    //     covers a rotunda pack.
    // When the route trash already killed the sentinel on the way in — the healthy
    // case, and the common one — the volume reads empty and the row is skipped.
    //
    // AND THE EAST ROW TAGS AWAY FROM THE NORTH-EAST PACK (ScriptedPullStage::avoid*).
    // Those two formations come together and cannot be made not to: east Mage Guard
    // 96774 (146.79, -125.14) and north-east Ethereum Smuggler 96849 (144.71, -113.34)
    // are 12.00yd apart, against a limit of 13.05 — AnyAssistCreatureInRangeCheck
    // passes CreatureFamilyAssistanceRadius (10) to IsWithinDistInMap with both radius
    // flags set, so it ADDS the two combat reaches (1.8 and 1.25) rather than
    // subtracting them. Inside by 1.05yd. Faction template 16 carries
    // RESPOND_TO_CALL_FOR_HELP (0x0001), neither entry has DONT_CALL_ASSISTANCE or
    // IGNORE_ALL_ASSISTANCE_CALLS, and both spawns are MovementType 0 — so it is not
    // marginal and not intermittent. Creature::AtEngage engages every member of a
    // groupAI-3 formation at its own spawn spot, each calling CallAssistance() from
    // there before anything has moved: tag any east mob and 96774 calls 96849; tag any
    // north-east mob and 96849 calls 96774. Symmetric, so reordering does not help.
    //
    // This is a T=0 bond, and that is what makes it different in kind from the other
    // near-misses in this room. The full cross-pack matrix, nearest member to nearest
    // member against each pair's own limit:
    //   E  <-> NE   12.00 / 13.05   INSIDE by 1.05   (96774 / 96849)
    //   C  <-> NE   13.34 / 13.60   inside by 0.26   (96777 / 96808)
    //   C  <-> NW   14.87 / 13.60   clear by 1.27
    //   C  <-> E    15.40 / 13.60   clear by 1.80
    //   everything else clears by 5yd or more.
    // The centre/north-east pair is inside on paper and does NOT fire in practice —
    // 0.26yd is about one server tick of a mob's movement, and the centre row's body
    // pull has that pack off its spawn before anything samples it. Do not "fix" it.
    // The east pair has four times the slack and fires before any of that matters.
    //
    // THE EAST ROW IS THE ONE RANGED ROW, and it follows from the above. A body pull
    // buys separation by keeping the opener from waking a neighbour; here the
    // neighbour is already awake at T=0, so there is nothing to buy and the walk-in is
    // spending 8yd of the tank's distance for it. The row takes the tag from the stand
    // spot instead — 25.8yd to Physician 96824, inside the 8-30yd opener band that both
    // the class table and the ranged-weapon fallback resolve to — and the stand spot is
    // 45.9yd from the nearest north-east mob and 53.1 from the nearest north-west one,
    // so nothing else is woken by standing there. What it changes is the shape of a
    // fight that was always going to be two packs: the tank never enters the room, and
    // both formations run the full haul to the forward camp.
    //
    // The other lever is the avoid anchor, which decides WHERE that joint fight starts:
    // pointed at the north-east centroid (140.73, -109.45) it makes the east row tag
    // Physician 96824, 31.1yd from that pack — the furthest of the four, and 2.7yd
    // clear of the runner-up, so the choice is stable rather than a coin flip between
    // two near-equal candidates. Against the forward camp that is a 43.97yd haul for
    // the east pack and ~66yd for the north-east one, so east arrives and is being
    // fought while north-east is still running. The north-east row that follows will
    // usually find its own cylinder empty and be skipped, which is correct and not a
    // defect: SelectOrder is built to walk past a stage whose pack is already dead.
    //
    // The one thing the ranged tag costs is transient. Every Sunblade caster here
    // carries castFlags 64 (SMARTCAST_COMBAT_MOVE), so while the tank stands on the
    // spot at 25.8yd with line of sight, the east pack's casters plant and shoot
    // instead of running. The drag-back is what collects them: the forward camp has no
    // sight-line into the room at all (RotundaForwardCampHasNoSightLineIntoTheRoom), so
    // the moment the tank is back through the neck they have to come. That is the same
    // mechanic both Selin rows are built on, not a new risk.
    uint32 constexpr MGT_SB_SENTINEL    = 24777;
    uint32 constexpr MGT_DELRISSA       = 24560;
    uint32 constexpr MGT_SB_MAGE_GUARD  = 24683;
    uint32 constexpr MGT_SB_BLOOD_KNGT  = 24684;
    uint32 constexpr MGT_SB_MAGISTER    = 24685;
    uint32 constexpr MGT_SB_WARLOCK     = 24686;
    uint32 constexpr MGT_SB_PHYSICIAN   = 24687;
    uint32 constexpr MGT_COILSKAR_WITCH = 24696;
    uint32 constexpr MGT_SISTER_TORMENT = 24697;
    uint32 constexpr MGT_ETHEREUM_SMUG  = 24698;

    // The rotunda's two camps and its arm anchor. The BACK camp is the hall below the
    // south neck and serves the hall patrol and the south pack; the FORWARD camp is
    // against the small room's east wall and serves the four rows that run once the
    // south pack is dead. See the dossier above for both numbers, why the forward one
    // is only sound once south is empty, and why it is at the WALL rather than in the
    // middle of the room.
    float constexpr MGT_ROT_CAMP_X = 141.70f, MGT_ROT_CAMP_Y = -211.71f, MGT_ROT_CAMP_Z = -21.13f;
    float constexpr MGT_ROT_FWD_X  = 137.11f, MGT_ROT_FWD_Y  = -179.23f, MGT_ROT_FWD_Z  = -21.43f;
    float constexpr MGT_ROT_ARM_X  = 127.50f, MGT_ROT_ARM_Y  = -190.00f, MGT_ROT_ARM_Z  = -21.27f;
    float constexpr MGT_ROT_ARM_R  = 20.0f;
    // Single-level room; the band only has to tolerate the ~1yd of floor relief
    // between the spawns and the cylinder's authored centre.
    float constexpr MGT_ROT_ZBAND  = 10.0f;

    std::vector<ScriptedPullStage> const& Table()
    {
        static std::vector<ScriptedPullStage> const kStages = []
        {
            std::vector<ScriptedPullStage> t;

            ScriptedPullStage east;
            east.mapId      = MGT_MAP;
            east.bossEntry  = MGT_SELIN;
            east.order      = 0;
            east.name       = "Magisters' Terrace — Selin's east guard pack";
            east.campX      = 170.46f;  east.campY  =   0.57f; east.campZ  = -2.72f;
            east.standX     = 212.22f;  east.standY =  7.42f;  east.standZ = -2.82f;
            east.packX      = 225.97f;  east.packY  = -20.08f; east.packZ  = -2.90f;
            east.packRadius = 12.0f;
            east.packZBand  = 12.0f;
            east.armX       = 209.58f;  east.armY   = 18.97f;  east.armZ   = -2.05f;
            east.armRadius  = 25.0f;
            east.entries    = {MGT_WRETCHED_SKULK, MGT_WRETCHED_BRUIS, MGT_WRETCHED_HUSK};
            t.push_back(east);

            ScriptedPullStage west;
            west.mapId      = MGT_MAP;
            west.bossEntry  = MGT_SELIN;
            west.order      = 1;
            west.name       = "Magisters' Terrace — Selin's west guard pack";
            west.campX      = 170.46f;  west.campY  =   0.57f; west.campZ  = -2.72f;
            west.standX     = 212.24f;  west.standY = -7.41f;  west.standZ = -2.82f;
            west.packX      = 226.31f;  west.packY  = 20.22f;  west.packZ  = -2.90f;
            west.packRadius = 12.0f;
            west.packZBand  = 12.0f;
            west.armX       = 209.58f;  west.armY   = 18.97f;  west.armZ   = -2.05f;
            west.armRadius  = 25.0f;
            west.entries    = {MGT_WRETCHED_SKULK, MGT_WRETCHED_BRUIS, MGT_WRETCHED_HUSK};
            t.push_back(west);

            // --- the Delrissa rotunda: the hall patrol, then five packs ----------
            //
            // `order` continues from Selin's 0/1 rather than restarting, because
            // order is the stage's identity WITHIN THE MAP: Find(mapId, order) has no
            // boss filter, so a second plan reusing 0 and 1 would resolve a latched
            // rotunda stage to a Selin row. DueStage's boss gate is what keeps the two
            // plans from ever being live at once; the numbering is what keeps them
            // from being confused for one another afterwards.

            // Order 2, and the reason it is FIRST rather than a gate bolted onto the
            // others: see "THE HALL PATROL IS A STAGE" above. Everything after it
            // inherits "the sentinel is dead" from SelectOrder, which will not look
            // past a stage whose volume still holds a live member.
            ScriptedPullStage patrol;
            patrol.mapId      = MGT_MAP;
            patrol.bossEntry  = MGT_DELRISSA;
            patrol.order      = 2;
            patrol.name       = "Magisters' Terrace — Delrissa rotunda, the hall patrol";
            patrol.campX      = MGT_ROT_CAMP_X; patrol.campY = MGT_ROT_CAMP_Y;
            patrol.campZ      = MGT_ROT_CAMP_Z;
            // The camp IS the stand spot: a vantage point is a fact about where a pack
            // stands, and this one walks.
            patrol.standX     = MGT_ROT_CAMP_X; patrol.standY = MGT_ROT_CAMP_Y;
            patrol.standZ     = MGT_ROT_CAMP_Z;
            // Midpoint of waypoint path 969450, wide enough to hold the whole path and
            // the camp it is dragged to; the z-band keeps the corridor sentinel 96944
            // (same entry, 12yd higher) out.
            patrol.packX      = 121.34f; patrol.packY = -214.89f; patrol.packZ = -21.40f;
            patrol.packRadius = 24.0f;
            patrol.packZBand  = 6.0f;
            // Its own anchor, not the rotunda's: this row must arm while the tank is
            // still walking down into the hall.
            patrol.armX       = 130.00f; patrol.armY = -198.00f; patrol.armZ = -21.30f;
            patrol.armRadius  = 30.0f;
            patrol.entries    = {MGT_SB_SENTINEL};
            patrol.bodyPull   = true;
            t.push_back(patrol);

            std::vector<uint32> const rotunda = {
                MGT_SB_MAGE_GUARD, MGT_SB_BLOOD_KNGT, MGT_SB_MAGISTER, MGT_SB_WARLOCK,
                MGT_SB_PHYSICIAN, MGT_COILSKAR_WITCH, MGT_SISTER_TORMENT,
                MGT_ETHEREUM_SMUG};

            struct RotundaRow
            {
                uint32 order;
                char const* name;
                float standX, standY, standZ;
                float packX, packY, packZ, packRadius;
                float campX, campY, campZ;
                // (0,0,0) => no neighbour to tag away from; rank nearest-to-the-spot.
                float avoidX, avoidY, avoidZ;
                // Body pull (walk to the aggro edge) or ranged tag from the spot.
                // Four of the five are body pulls; the EAST row is not — see the
                // dossier's "THE EAST ROW IS THE ONE RANGED ROW" note.
                bool bodyPull;
            };
            // South, centre, east, north-east, north-west. The order is authored,
            // not derived from camp distance — see the dossier above, and note that
            // the centre row's stand spot only works BECAUSE it comes before the
            // east row (it approaches across the south pack's emptied ground).
            //
            // The camp column is the one that moves: south and centre haul back to the
            // hall, and the three rows that only run once those two are dead haul back
            // to the small room instead.
            //
            // The bodyPull column moves once, on the EAST row, and for a reason that is
            // about that row's neighbour rather than about the room: see "THE EAST ROW
            // IS THE ONE RANGED ROW" in the dossier above.
            RotundaRow const rows[] = {
                {3, "Magisters' Terrace — Delrissa rotunda, south pack",
                 125.50f, -184.50f, -21.27f, 117.24f, -155.37f, -21.15f,  7.0f,
                 MGT_ROT_CAMP_X, MGT_ROT_CAMP_Y, MGT_ROT_CAMP_Z, 0.0f, 0.0f, 0.0f, true},
                // The centre row hauls to the FORWARD camp, not the hall. Its own
                // blocker — the south pack, the one thing the forward camp is
                // inside the reach of — is dead before this row can arm, which is
                // the same argument the east row makes and the only one the
                // forward camp ever needed. See "THE CENTRE ROW MOVED FORWARD" in
                // the dossier for what the back camp was costing.
                {4, "Magisters' Terrace — Delrissa rotunda, centre pack",
                 111.50f, -156.50f, -20.92f, 126.54f, -128.90f, -20.44f,  9.0f,
                 MGT_ROT_FWD_X, MGT_ROT_FWD_Y, MGT_ROT_FWD_Z, 0.0f, 0.0f, 0.0f, true},
                // The east row tags away from the north-east pack's centroid: those two
                // formations are inside each other's assistance radius and come as one,
                // so the only thing left to choose is how far the second one has to run.
                // It is also the one row that tags at RANGE, because a body pull cannot
                // buy the separation back and the walk-in costs 8yd of it.
                {5, "Magisters' Terrace — Delrissa rotunda, east pack",
                 139.00f, -160.00f, -21.14f, 150.85f, -132.46f, -20.91f, 11.0f,
                 MGT_ROT_FWD_X, MGT_ROT_FWD_Y, MGT_ROT_FWD_Z, 140.73f, -109.45f, -20.79f,
                 false},
                {6, "Magisters' Terrace — Delrissa rotunda, north-east pack",
                 141.50f, -140.00f, -21.20f, 140.73f, -109.45f, -20.79f,  8.0f,
                 MGT_ROT_FWD_X, MGT_ROT_FWD_Y, MGT_ROT_FWD_Z, 0.0f, 0.0f, 0.0f, true},
                {7, "Magisters' Terrace — Delrissa rotunda, north-west pack",
                 122.50f, -137.50f, -20.20f, 112.17f, -110.32f, -20.69f,  8.0f,
                 MGT_ROT_FWD_X, MGT_ROT_FWD_Y, MGT_ROT_FWD_Z, 0.0f, 0.0f, 0.0f, true},
            };
            for (RotundaRow const& r : rows)
            {
                ScriptedPullStage s;
                s.mapId      = MGT_MAP;
                s.bossEntry  = MGT_DELRISSA;
                s.order      = r.order;
                s.name       = r.name;
                s.campX      = r.campX;        s.campY  = r.campY;       s.campZ  = r.campZ;
                s.standX     = r.standX;       s.standY = r.standY;      s.standZ = r.standZ;
                s.packX      = r.packX;        s.packY  = r.packY;       s.packZ  = r.packZ;
                s.packRadius = r.packRadius;
                s.packZBand  = MGT_ROT_ZBAND;
                s.armX       = MGT_ROT_ARM_X;  s.armY   = MGT_ROT_ARM_Y; s.armZ   = MGT_ROT_ARM_Z;
                s.armRadius  = MGT_ROT_ARM_R;
                s.avoidX     = r.avoidX;       s.avoidY = r.avoidY;      s.avoidZ = r.avoidZ;
                s.entries    = rotunda;
                // Per row, not plan-wide. A ranged opener on FOUR of these five brings
                // neighbours the body pull does not; the east row's neighbour comes
                // either way, so that row spends the opener instead of the 8yd.
                s.bodyPull   = r.bodyPull;
                t.push_back(s);
            }

            std::stable_sort(t.begin(), t.end(),
                             [](ScriptedPullStage const& a, ScriptedPullStage const& b)
                             {
                                 return a.mapId != b.mapId ? a.mapId < b.mapId
                                                           : a.order < b.order;
                             });
            return t;
        }();
        return kStages;
    }
}

bool ScriptedPullRegistry::HasRows(uint32 mapId)
{
    for (ScriptedPullStage const& s : Table())
        if (s.mapId == mapId)
            return true;
    return false;
}

std::vector<ScriptedPullStage const*> ScriptedPullRegistry::Rows(uint32 mapId)
{
    std::vector<ScriptedPullStage const*> out;
    for (ScriptedPullStage const& s : Table())
        if (s.mapId == mapId)
            out.push_back(&s);
    return out;  // Table() is kept sorted by (mapId, order), so this is ascending.
}

ScriptedPullStage const* ScriptedPullRegistry::Find(uint32 mapId, int32 order)
{
    if (order < 0)
        return nullptr;
    for (ScriptedPullStage const& s : Table())
        if (s.mapId == mapId && s.order == static_cast<uint32>(order))
            return &s;
    return nullptr;
}

bool ScriptedPullRegistry::InPack(ScriptedPullStage const& s, float x, float y, float z)
{
    if (std::fabs(z - s.packZ) > s.packZBand)
        return false;
    float const dx = x - s.packX;
    float const dy = y - s.packY;
    return (dx * dx + dy * dy) <= (s.packRadius * s.packRadius);
}

bool ScriptedPullRegistry::IsPackEntry(ScriptedPullStage const& s, uint32 entry)
{
    return std::find(s.entries.begin(), s.entries.end(), entry) != s.entries.end();
}

bool ScriptedPullRegistry::InArmRange(ScriptedPullStage const& s, float x, float y, float z)
{
    // The row's own anchor when it names one, else the camp (see ScriptedPullStage
    // ::armX for why a far-back camp must not be its own arm gate).
    float const ax = s.HasArmAnchor() ? s.armX : s.campX;
    float const ay = s.HasArmAnchor() ? s.armY : s.campY;
    float const az = s.HasArmAnchor() ? s.armZ : s.campZ;
    if (std::fabs(z - az) > DC_SCRIPTED_PULL_ARM_ZBAND)
        return false;
    float const dx = x - ax;
    float const dy = y - ay;
    return (dx * dx + dy * dy) <= (s.armRadius * s.armRadius);
}

int32 ScriptedPullRegistry::SelectOrder(std::vector<uint32> const& orders,
                                        std::vector<bool> const& live, int32 pinned)
{
    // A committed stage always wins: mid-drag its own volume reads empty (the pack
    // is being hauled out of it), and re-deriving from `live` there would hand the
    // tank the NEXT pack while it is still running home with this one.
    if (pinned >= 0)
        for (uint32 o : orders)
            if (o == static_cast<uint32>(pinned))
                return pinned;

    size_t const n = std::min(orders.size(), live.size());
    for (size_t i = 0; i < n; ++i)
        if (live[i])
            return static_cast<int32>(orders[i]);
    return -1;
}

Unit* ScriptedPullRegistry::NearestPackMember(Player* bot, AiObjectContext* ctx,
                                              ScriptedPullStage const& s)
{
    if (!bot || !ctx)
        return nullptr;
    // Shares the events framework's volume scan (entry-filtered, z-banded, and
    // reachability-probed), so "is this pack still up" means the same thing here
    // as it does for a ClearRadius step.
    //
    // Ranked from the STAND SPOT, not from the bot. The tag is taken from the
    // stand spot, so the only distance that matters is the one the pull spell has
    // to cover once the tank gets there — and at commit time the bot is still out
    // in the corridor, where "nearest to me" can be a mob on the far side of the
    // pack. Live (tr-20260802-215715-3): the bot committed from 38.9yd out, the
    // scan handed it a Bruiser that was 31.6yd from the stand spot, Avenger's
    // Shield reaches 30, so the tag fell through to the generic close-to-aggro-edge
    // walk-in and carried the tank off the spot and into the room. The nearest
    // member to that same spot is ~25yd — comfortably in range.
    //
    // NEVER the pack member a pull already gave up on. An abort hands ONE mob to the
    // normal walk-in engage (DcPullContext::abortTarget), and every ordinary target
    // path honours that — the corridor scan filters it, the sticky latch releases on
    // it, the pull trigger defers on it. This path did not, and being the path that
    // runs AHEAD of all of them that made the abort unescapable rather than merely
    // ignored: the plan kept handing back the one GUID the trigger was refusing, so
    // the maneuver never ran again and nothing could clear either flag.
    //
    // Live (tr-20260803-144046-2): "target ... is the abort target -> defer to normal
    // engage", 1145 times in four minutes, on the same Bruiser, while the party stood
    // parked at the camp.
    //
    // Excluding it here answers both questions the caller asks at once. As a TARGET
    // the stage falls through to the next member of its own pack and the plan carries
    // on; as the DueStage liveness probe it means a stage whose only survivor is the
    // abort target reads as empty and simply does not arm, which is what lets the run
    // walk in and fight the thing instead of re-arming a plan around it.
    //
    // UNLESS THE ROW NAMES A NEIGHBOUR (ScriptedPullStage::avoidX), in which case the
    // ranking inverts: the furthest member from that anchor wins instead. A row only
    // does that when the neighbour is coming regardless — see the east rotunda row —
    // and the tag then decides how much of a head start this pack gets rather than how
    // short the walk is. Everything else about selection is untouched, including the
    // abort-target exclusion below, which still applies in both modes.
    DcPullContext const& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    Position const stand(s.standX, s.standY, s.standZ);
    Position const avoid(s.avoidX, s.avoidY, s.avoidZ);
    return DcTargeting::NearestHostileNearPoint(bot, ctx, s.packX, s.packY, s.packZ,
                                                s.packRadius, s.packZBand, &s.entries,
                                                &stand, pull.abortTarget,
                                                s.HasAvoidAnchor() ? &avoid : nullptr);
}

ScriptedPullStage const* ScriptedPullRegistry::DueStage(Player* bot, AiObjectContext* ctx)
{
    if (!bot || !ctx)
        return nullptr;

    uint32 const mapId = bot->GetMapId();
    if (!HasRows(mapId))
        return nullptr;

    std::vector<ScriptedPullStage const*> const rows = Rows(mapId);
    if (rows.empty())
        return nullptr;

    // Boss gate: the plan exists to get the party to ONE boss, so it is dormant
    // unless that boss is the run's current objective. This is also what retires
    // it — once Selin is dead the next objective is a different boss and no stage
    // can arm on the way back past his room.
    std::optional<DungeonBossInfo> const next =
        ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next.has_value())
        return nullptr;

    DcPullContext const& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    int32 const pinned = pull.scriptedStage;

    // An in-flight stage short-circuits everything below it: the boss/arm gates and
    // the live scan all describe whether a pull may START, and this one already has.
    if (pinned >= 0)
        if (ScriptedPullStage const* held = Find(mapId, pinned))
            return held;

    std::vector<uint32> orders;
    std::vector<bool> live;
    orders.reserve(rows.size());
    live.reserve(rows.size());
    for (ScriptedPullStage const* s : rows)
    {
        if (s->bossEntry != next->entry)
            continue;
        if (!InArmRange(*s, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
            continue;
        orders.push_back(s->order);
        live.push_back(NearestPackMember(bot, ctx, *s) != nullptr);
    }

    int32 const pick = SelectOrder(orders, live, /*pinned*/ -1);
    return pick < 0 ? nullptr : Find(mapId, pick);
}

bool ScriptedPullRegistry::IsStageTarget(ScriptedPullStage const& s, Unit const* u)
{
    if (!u)
        return false;
    return IsPackEntry(s, u->GetEntry()) &&
           InPack(s, u->GetPositionX(), u->GetPositionY(), u->GetPositionZ());
}
