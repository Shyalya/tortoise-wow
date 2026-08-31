/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DUNGEON_CLEAR_TUNING_H
#define _DUNGEON_CLEAR_TUNING_H

#include <cmath>
#include <cstdint>

using uint32 = std::uint32_t;  // matches the core's Define.h typedef; this
                               // header stays core-include-free (tests build it
                               // standalone), so alias locally instead.

// Single source of truth for the DungeonClear tuning constants that are SHARED
// across translation units. Historically several of these were defined twice
// (once in Trigger/DungeonClearTriggers.cpp, once in
// Action/DungeonClearActions.cpp) with comments warning they "must stay in
// sync" — kept aligned only by hand. Centralizing them here makes drift
// impossible: one definition, every site includes it.
//
// Phase 1 of the constant-centralization refactor only houses the previously
// DUPLICATED constants. File-local (single-TU) constants stay where they are;
// Bucket-A consolidation and Bucket-B promotion to DcSettings are later phases.
//
// NOTE: namespace-scope constexpr has internal linkage, so each including TU
// gets its own internal copy seeded from this one definition — no ODR issue and
// no linker conflict. The DC_ names are kept at global scope (as they were
// effectively file-global via the old anonymous-namespace defs) so call sites
// stay unqualified and unchanged.

// Portable pi. NOT M_PI: MSVC's <math.h> only defines the M_* macros when
// _USE_MATH_DEFINES is set BEFORE the first inclusion of math.h, and any TU that
// reaches <cmath> before the core's Define.h (which sets it on Windows) never
// gets them — that is the "M_PI: undeclared identifier" build break on MSVC, and
// its cascade inside Position.h ("fmod: no overloaded function takes 1
// arguments", from the argument that failed to compile). The module's CMake also
// forces _USE_MATH_DEFINES on the compiler command line so the CORE headers stay
// buildable; this constant makes OUR sources not care either way. Keeps this
// header constant-expression and core-include-free.
constexpr float DC_PI = 3.14159265358979323846f;

// Asymmetric ranges so a trash pack sitting just outside the boss room gets
// engaged before the at-boss trigger fires. 22yd is just outside most level-80
// elite aggro radii (~18-20yd), giving room to position before melee. The
// trigger uses this to decide "at the boss"; the action uses it for the same
// hand-off, so they must agree.
constexpr float DC_ENGAGE_RANGE = 22.0f;

// How many consecutive engage-trash ticks may hand a far, long-route pack to Advance
// WITHOUT the gap closing before this action takes the tick back and walks in itself.
// The hand-off is only sound while Advance is travelling toward the pack; for one
// beside or behind the route to the next boss it is permanent, and the pack DC already
// voted to fight is abandoned mid-tag (tr-20260804-153254-2). Sized in ticks, not
// seconds, because it is a per-decision budget: ~20 think ticks is a few seconds of
// "we are not getting any nearer" — long enough that a genuine ramp descent (which
// improves the straight-line gap several times a second) never reaches it.
constexpr uint32 DC_LONGROUTE_DEFER_LIMIT = 20;

// Extra standoff added OUTSIDE a room-aggro boss's skirt sphere when computing
// its (uncapped) boss-engage range — see DcEngageGeometry::BossEngageRange. The
// engage hand-off for a room-aggro boss must trip while the tank is still clear
// of the sphere, with a few yards of slack so a per-tick approach glide that
// overshoots the hand-off distance still stops before crossing the sphere edge
// (and waking the boss mid-clear). Small: the sphere already carries the aggro
// margin + path padding beyond the boss's real wake distance.
constexpr float DC_ROOM_AGGRO_STANDOFF_BUFFER = 4.0f;
// Ordering invariant: the tank's hold/clear standoff is the avoid sphere PLUS
// this buffer, so it must be strictly outside the sphere. Encoded at compile time
// so a future edit that zeroes/negates the buffer (re-introducing the "engage
// hand-off lands inside the sphere" regression) fails the build instead of
// silently waking the boss mid-clear.
static_assert(DC_ROOM_AGGRO_STANDOFF_BUFFER > 0.0f,
              "room-aggro standoff must sit OUTSIDE the avoid sphere");

// Cone scan for "blocking trash" — the geometric fallback the trigger uses and
// the action falls back to when the corridor path scan can't run (boss off-mesh,
// etc.). 35yd catches packs one tick-cycle earlier than the engage range. Both
// TUs feed it to DcTargeting::FindBlockingTrash, so it is one constant
// despite the old per-context names (DC_ENGAGE_CONE_* / DC_TRASH_CONE_*).
constexpr float DC_TRASH_CONE_RANGE = 35.0f;
constexpr float DC_TRASH_CONE_HALF_ANGLE = DC_PI / 3.0f;  // 60°

// When true, evaluate "blocking trash" via the bot's actual mmap path polyline
// instead of the geometric cone. Catches packs around corners and avoids "pack
// on the other side of a wall" false positives. Falls back to the cone scan when
// path computation fails. Both TUs must agree or one path scans while the other
// does not.
constexpr bool  DC_USE_CORRIDOR_SCAN = true;
constexpr float DC_CORRIDOR_LOOKAHEAD = 35.0f;

// Mid-glide hazard probe cadence (Advance). While a continuous escort spline is
// in flight the hop ladder short-circuits, so a patrol can enter the committed
// window unobserved; the probe re-tests the remaining window against the
// bystander avoid-spheres at most this often. At ~7yd/s glide speed this is a
// ~3.5yd resolution — far finer than the 35yd capped window — for one grid
// visit per half-second per tank. Gated on AdvanceWindowYards > 0 and
// PullEnRouteAvoid, so normal difficulty pays nothing.
constexpr uint32 DC_GLIDE_HAZARD_PROBE_MS = 500;

// En-route truncation shaping (Advance; DcEngageGeometry::TruncateWindowAtSphere).
//
// DC_AVOID_MIN_GLIDE is the floor on what a truncated window is allowed to be.
// Below it the "stop at the hazard threshold" is indistinguishable from standing
// still, and Advance would spend the tick either issuing a 2yd spline or — when
// the truncation leaves the lone seed point — dropping into the per-point MoveTo
// walk, whose per-hop LastMovement delay is the ~2yd/s crawl the spline window
// exists to eliminate. A tank that close to a pack has nothing left to avoid, so
// the truncation is declined and the glide runs; the pack is owned from there by
// the blocking-trash detector and the pull pipeline. Sized just above one
// polyline point spacing (~4yd) so a truncation always buys at least one real
// leg of travel.
constexpr float DC_AVOID_MIN_GLIDE = 6.0f;
// How far OUTSIDE the sphere the truncated window's last point sits. Parking
// exactly on the boundary reads as "inside" to the next tick's test (the
// crossing solve rejects a start point already within r), which would decline
// every following truncation for that sphere; a yard of clearance keeps the stop
// re-derivable. Small on purpose — the sphere is already padded by
// PullEnRouteMargin, so this is anti-jitter, not safety margin.
constexpr float DC_AVOID_EDGE_BACKOFF = 1.0f;

// Longest single sidestep the BYSTANDER orbit (DcEngageGeometry::OrbitRing) will
// issue. The room-aggro boss skirt steps a fixed angle, which is a ~12yd chord at
// a 20yd stand-off and a ~35yd chord at 60 — fine for a boss orbit the tank is
// committed to running all the way around, wrong for a trash pack it is merely
// walking past, because the pull's early-out routinely cancels that leg one tick
// after issuing it. A leg this size is cheap enough to abandon and still long
// enough to change the approach line.
constexpr float DC_ORBIT_MAX_LEG_YARDS = 12.0f;

// Half-width of the path "blocking trash" band. Widened from 8 to 18 so it
// roughly matches level-80 elite aggro radius: a pack sitting a few yards off the
// route line still aggros as the tank passes, so it must count as blocking trash.
// With the old 8yd band such a pack was never a candidate, so the selector picked
// an on-line mob farther ahead and the tank ran straight through the side pack to
// reach it. Trigger and action share the FindBlockingTrashOnPath band, so this
// must be one value.
constexpr float DC_CORRIDOR_WIDTH = 18.0f;

// Advanced-pull commit range: a pull is only STARTED once its target pack is
// within this distance, so the camp (stamped at the tank's spot) stays close
// enough that the run-in reaches the pack and the drag-back is short. The
// corridor scan sees packs out to ~35yd, so without this the camp lands in the
// run-in's overshoot dead band and every pull after the first whiffs. The pull
// trigger and the pull action must agree on when a pull starts.
constexpr float DC_PULL_START_RANGE = 26.0f;

// How long the route-driving rungs (idle/advance and at-boss) stand down for a
// pull maneuver that has left phase Idle.
//
// The stand-down itself closes a real hole: relevance keeps advance (15) below
// pull (35) during a healthy maneuver, but BOTH pull rungs go quiet in the window
// between a ranged tag landing and the pack arriving — the non-combat pull trigger
// returns !IsInCombat() for any non-Idle phase, and the drag-back trigger lives on
// the combat engine the bot has not been flipped onto yet. Advance is then the top
// live rung and routes the tank at the next BOSS, mid-pull. (Magisters' Terrace,
// tr-20260802-215715-3: a 69.8yd escort spline to Selin issued on the tag tick,
// which the drag-back then fought for five seconds.)
//
// The bound is what keeps the cure from being worse than the disease. Every leg
// carries its own watchdog and the Engage phase is cleaned up out of combat, so a
// healthy maneuver never comes near this; it exists only so a wedged phase cannot
// silence the run's driver forever.
constexpr uint32 DC_PULL_ADVANCE_STANDDOWN_MAX_MS = 30000;

// How long a camp write by the pull machinery (prospective publish, commit,
// dynamic seed, unplanned-aggro fresh camp) counts as "fresh". While fresh, the
// pull action owns the camp and Advance's scout camp-trailing stands down; once
// it goes stale (the pull trigger is loot/ready-gated, or no pull is running)
// Advance trails the camp behind the moving tank every tick. Replaces the old
// "is a pack in pull-scan range" ownership test, whose conditions were weaker
// than the ones the pull action actually needs to run — any tick the two
// disagreed, NOBODY moved the camp, and the spread gate (camp-anchored in pull
// mode) kept passing while the tank glided away from the party.
constexpr uint32 DC_CAMP_PUBLISH_FRESH_MS = 1000;

// Bystander-detour borrow budget: how long the pull may keep driving the
// approach around another pack's aggro sphere WITHOUT the tank getting any
// closer to the pack it is walking at, before it hands the walk back to Advance.
// The clock restamps on every yard of real progress (DungeonClearMath::
// ShouldKeepAvoidDetour), so this only ever measures a stalled orbit — 8s of
// pure sideways travel is already far more than any legitimate arc needs, and
// the bound is what keeps a non-converging orbit from freezing the run while
// Advance's own wedge ladder is parked.
constexpr uint32 DC_PULL_AVOID_STALL_MS = 8000;
// A tick has to beat the detour's best distance-to-pack by this much to count as
// progress and restamp the clock. Absorbs the sub-yard jitter of a glide so an
// orbit that is merely drifting can't hold the borrow open forever.
constexpr float DC_PULL_AVOID_PROGRESS_YD = 1.0f;

// How long the leader must be FLAGGED in combat with no actual engagement —
// nobody in the party has a victim and nothing is attacking anyone — before the
// driving ladder resumes anyway (DungeonClearMath::MayDriveWhileFlagged).
//
// This exists because a hostile area aura can set the combat flag with no fight
// behind it (the Arcatraz Eredar Soul-Eaters' Entropic Aura, 45yd against a ~20yd
// aggro radius), and the old flag-only gate then froze the run permanently.
//
// The value is the guard against the opposite error, and is the whole safety
// margin of the change: a genuine fight has one-tick holes when a target dies
// before anything re-acquires, and resuming the drive in one of those would walk
// the tank out of a live fight. 5s is several react delays — far longer than any
// retarget hole — while costing a real freeze only five seconds. Compare
// DungeonClearBreakStuckCombatTrigger's 15s, sized for the same hazard (a scripted
// in-combat lull) but guarding a much stronger action: that one force-clears
// combat, this one only resumes walking, so it can afford to be quicker. If a
// fight is ever abandoned mid-pack, this is the number to raise.
constexpr uint32 DC_FLAGGED_NO_ENGAGE_GRACE_MS = 5000;

// --- the NoRezzer floor ----------------------------------------------------
// How long the silence after the last rezzer died has to last before "no one can
// resurrect" is allowed to END THE RUN, and how long a bare combat flag may hold
// that decision off on its own. Sized off the thing that broke: Magisters'
// Terrace's Kael'thas is immune, passive and summonless at 1 HP for 11 seconds
// before he kills himself, and for all 11 the party reads unengaged while still
// carrying his flag. 12s clears that with margin.
//
// The ceiling is what keeps the flag hold from becoming a hang: a flag with
// nothing behind it is the phantom-combat case, which has its own hatch
// (DungeonClearBreakStuckCombatTrigger, 15s) and its own force-clear. Past 60s
// neither has resolved it and holding a run open on it buys nothing — fall back
// to the verdict the branch always gave.
constexpr uint32 DC_NO_REZZER_QUIET_GRACE_MS = 12000;
constexpr uint32 DC_NO_REZZER_HOLD_MAX_MS    = 60000;

// How close a live combat holder has to be to count as "still fighting us" for
// the rez release. A hostile AREA AURA holds the flag from 45yd with nothing on
// the party — the freeze DcCombatFlag exists for — so any radius used to decide
// "the fight is not over" has to sit clear underneath that reach while still
// covering a camp fight the party is strung out across.
constexpr float DC_FIGHT_HOLDER_RADIUS = 40.0f;

// How far from a member an attacker (or that member's own victim) may be and
// still count as a FIGHT for DcCombatFlag::IsEngaged.
//
// `getAttackers()` holds anything that ever called Attack() on the member and
// keeps holding it until that creature dies or evades — at ANY range. Cross a
// navmesh break (a TeleportParty, a one-way drop) and the attackers left behind
// stay in the set forever, hundreds of yards away and unable to arrive. The bare
// `victim || attackers` predicate then reads "this party is fighting" for the
// rest of the run, which is a hard deadlock: the stranded member's follow-tank
// rung stands down (MayDrive), the tank's spread gate waits on it as out of
// range, and DcStrandedRecovery — the failsafe built for exactly this — both
// re-arms its clock every tick and early-outs. All three at once, from one
// member. Live on Azjol-Nerub, tr-20260818-214742-2: a healer parked 94yd from
// the tank for 12m37s, held by four Skittering Swarmers 445yd away and 366yd
// overhead, stranded recovery firing zero times while it happened.
//
// DISTANCE ONLY, deliberately. This predicate is asked every tick by every
// follower rung, party-wide; DcEngageGeometry::IsReachable costs a pathfind per
// reference, which is fine in ScanCombatHolders (behind cheaper reads, on the
// phantom-combat hatch) and not fine here.
//
// ScanCombatHolders now bounds its own walk by this same radius, and needs to:
// its reachability test delegates to the CHUNKED pathfinder, which accepts any
// path with forward progress (that is how a boss route longer than
// PathGenerator's ~296yd cap gets walked at all) and so calls a holder 350yd
// overhead "reachable". One radius, asked the same way on both sides, or the
// hatch built to clear exactly this stays inert while the flag test clears.
//
// Generous on purpose — it is a sanity bound, not a fight radius. Nothing that
// is genuinely swinging at, casting on, or being chased by a member sits past
// 100yd of it (mob spell reach tops out around 40yd, and DC_FIGHT_HOLDER_RADIUS
// is the 40yd question); anything that does has been left behind by geometry.
// Set it wide so a real fight is never mistaken for a stranding, since that
// error walks the tank away mid-pull, whereas the error in the other direction
// costs one stranded-recovery teleport.
constexpr float DC_ENGAGEMENT_RADIUS = 100.0f;

// The max party-spread default lives in DcSettingsRegistry ("PartyMaxSpread");
// the trigger, the advance gate, and the status publisher all read it through
// DcSettings so per-run addon overrides apply. The HP/mana recovery thresholds
// live in DungeonClearUtil::RestMin{Hp,Mp}Pct().

// --- Shared geometry bands -------------------------------------------------
// These three were file-local to DungeonClearUtil.cpp but are now shared across
// the split util units (DcEngageGeometry door/level tests, DcTargeting corridor
// scans, DcPullPlanner classification), so they live here as the single home
// (Bucket A of the constant-centralization plan).

// 2D half-width band around the walked route within which a closed door counts
// as sitting "on the corridor". Matches DOOR_CORRIDOR_WIDTH in
// DungeonClearBlockingDoorValue — a door's GO origin (its hinge/jamb) can be
// several yards off the line the bot actually walks through, so a tighter band
// misses wide gates.
constexpr float DC_DOOR_BAND = 8.0f;

// Vertical tolerance for matching a door to a leg of the route (or to a target).
// A door's GO origin Z sits at its OWN floor; the route point we test it against
// is on the walking surface. Beyond this gap the door is on a different level — a
// stacked ship deck, a ramp above/below — and must not count as blocking. The
// door tests are otherwise 2D, so without this a door directly over or under a
// point that is merely near in plan-view falsely blocks the corridor (parks the
// tank short, vetoes near-side packs). Wide enough to absorb a doorway
// threshold/lip and minor navmesh-vs-GO Z drift, tight enough to keep adjacent
// floors apart.
constexpr float DC_DOOR_Z_BAND = 6.0f;

// Below this vertical offset a candidate is treated as on the bot's own level:
// slopes, stairs and ramps stay under it within a corridor lookahead. WotLK
// inter-floor gaps are larger, so a genuine other-level mob always exceeds it.
// Shared by IsLevelReachable (trash), IsAtBossEngage (boss-arrival floor guard),
// and the dynamic-pull classification.
constexpr float DC_Z_LEVEL_TOLERANCE = 5.0f;

// Vertical half-band for matching a trash candidate to a leg of the corridor in
// the blocking-trash scans. The corridor band test is otherwise 2D, so a mob at
// the bottom of a ledge — directly under (or over) an early leg of the route in
// plan view — would match that leg and get picked NOW, even though along the
// actual route it is far beyond the lookahead (the tank reaches it only after
// winding down the ramp). With this gate the mob can only match the legs on its
// OWN floor, which the along-path lookahead then correctly defers until the
// tank actually descends. Looser than DC_Z_LEVEL_TOLERANCE to absorb
// navmesh-vs-terrain Z drift along the polyline and mobs perched on bumps.
constexpr float DC_CORRIDOR_Z_BAND = 8.0f;

// Detour gates for IsLevelReachable's cross-level path probe. A candidate on
// another vertical level only counts as reachable when the NAVIGATIONAL path to
// it is commensurate with its straight-line distance: a mob 15yd below the
// ledge whose real approach is a 70yd ramp detour merely LOOKS close and must
// not be proactively engaged/pulled (it is handled when the route brings the
// tank to its floor). The slack term keeps legitimate short around-the-corner
// detours alive at close range, where a pure ratio test is too strict.
constexpr float DC_TRASH_DETOUR_RATIO = 2.0f;
constexpr float DC_TRASH_DETOUR_SLACK = 20.0f;

// The same guard, far tighter, for the hazard-vacate retreat
// (DungeonClearHazardVacateAction). A retreat point is only ever ~14yd from the
// emitter, and its whole job is to open a few yards NOW — so a candidate whose
// real walk is a long way round is not a retreat at all, it is an evacuation of
// the room. "A path exists" was the only test, and tr-20260815-154816-5 is what
// that allowed: a point a few yards away through a wall, reachable the long way,
// committed to and then walked for 47 SECONDS, carrying the tank ~60yd across
// the cavern with twelve Creeping Sludges in tow. The party wiped strung out
// over 100yd.
//
// Ratio alone is too strict at these distances (a bot 3yd from its aim point
// rounding a corner trivially exceeds any ratio), hence the slack term — sized
// to a doorway or a pillar, not to a wall.
constexpr float DC_VACATE_DETOUR_RATIO = 1.5f;
constexpr float DC_VACATE_DETOUR_SLACK = 8.0f;

// How long the retreat may ride one committed destination before re-looking.
// Bounded by the detour gate above, a valid retreat walk is a few seconds at
// most; anything longer means the world moved out from under the commitment
// (the emitter drifted, the bot wedged) and re-deciding beats riding it out.
constexpr uint32 DC_VACATE_COMMIT_MAX_MS = 4000;

// The bound both detour gates apply: a route may be `ratio` times the straight
// line, or `slack` yards longer than it, whichever is more generous. Shared so
// the two call sites cannot drift, and so the numbers above can be pinned by a
// test without needing a live bot to path with.
constexpr float DcDetourBound(float straight, float ratio, float slack)
{
    float const byRatio = straight * ratio;
    float const bySlack = straight + slack;
    return byRatio > bySlack ? byRatio : bySlack;
}

// Smart Rest failsafes (DcSmartRest::UpdateLatch). A latched rest normally
// releases when every bot reaches full hp/mana — but a member that CANNOT get
// there (an AFK human who never drinks, a bot with no food when the food cheat
// is off) must not stall the run forever, so a latch is force-released after
// DC_SMART_REST_MAX_MS. The rearm cooldown then blocks an immediate re-latch on
// that same member, or the party would flap latch/timeout in a tight cycle.
// Worst case: 3-minute rest / 30-second push cycles — strictly better than the
// legacy gate, which stalls indefinitely on the same member.
constexpr uint32 DC_SMART_REST_MAX_MS   = 180000;
constexpr uint32 DC_SMART_REST_REARM_MS = 30000;

// Position-based stuck detection, shared by the Advance drive and the
// door-blocked walk-in (both glide the same escort spline). If the bot is
// supposed to be moving but its world position barely shifts for
// DC_STUCK_TICK_LIMIT consecutive ticks, treat it as wedged and recover
// (halt the spline + re-anchor the cursor) rather than blindly relaunching.
//
// DC_STUCK_DISPLACEMENT is PER TICK and must stay well below the distance a
// HEALTHY escort glide covers in one tick, or normal movement reads as stuck.
// With run speed ~7 yd/s and the ~0.2s tick cadence a gliding bot moves
// ~1.4-1.5 yd/tick — so an old 1.5yd value flagged every healthy tick. A
// genuinely wedged bot moves ~0 yd/tick, so 0.5yd cleanly separates the two:
// 3x above the wedge floor yet ~1yd below the slowest healthy glide.
constexpr float DC_STUCK_DISPLACEMENT = 0.5f;
constexpr uint32 DC_STUCK_TICK_LIMIT = 5;

// Consecutive not-ready ticks the between-pulls gate tolerates before it
// actually halts the advance. The gate is a bare threshold test (spread / HP /
// mana / rest latch) with no hysteresis, so a party strung out right AT
// PartyMaxSpread trips it for a single tick as the tank glides. Halting costs a
// StopBot(Hold), which CANCELS the escort spline — so a one-tick blip became a
// full stop/re-issue and a visible hitch (live: isolated "advance yielding:
// party not ready / resting" lines seconds apart, each one a micro-stutter mid-
// run). Riding out a brief trip costs at most this many ticks of extra travel
// while a genuine wait — the party really is behind, or resting — trips every
// tick and still halts almost immediately.
//
// "At most this many ticks of extra travel" is only true because TryBetweenPullsRest
// rides out the debounce ONLY for a glide already in flight. Letting it fall through
// with nothing running would let the ladder launch a fresh 35-38yd escort window on a
// debounce tick — five seconds of committed travel bought with a three-tick grace, and
// unbounded travel if the ready/not-ready flicker repeats. See the note there.
constexpr uint32 DC_PARTY_YIELD_DEBOUNCE_TICKS = 3;

// Upper bound on the between-pulls hold. The debounce above rides out a blip;
// this bounds the WAIT, which had none - partyNotReadyTicks counted up forever
// and the tank held until the run froze at 420s. Live 2026-08-29 in Blackfathom
// Deeps: 219 holds in 90 minutes, 58 on "out of range", no run recovered from
// one. Releasing reopens the travel window the ratchet above deliberately
// closes, so keep this long enough that it stays a last resort.
constexpr uint32 DC_PARTY_YIELD_MAX_MS = 60000;

// How long without an off-path rebuild before the rung-4 budget counts as a
// fresh episode. Shorter than this and we are still in the same wedge: the
// loop alternates failed and successful resnaps, so counting CONSECUTIVE
// failures never accumulates (1039 logged "rebuild #1" in twenty minutes).
constexpr uint32 DC_OFFPATH_EPISODE_GAP_MS = 20000;

// Consecutive Resnap recoveries allowed before the stuck ladder stops trusting
// the cached route and forces a rebuild. Resnap only proves the bot's position
// can be snapped ONTO the polyline — never that it can walk ALONG it — so a bot
// wedged against geometry beside a still-valid route re-snaps successfully
// forever. Counting only rebuilds pinned the ladder on its first rung (live:
// nine "resnapped onto existing route (rebuildAttempts=0)" lines over ~24s on
// the Durnholde terraces, escalating to nothing). Two lets a genuine transient
// drift resolve cheaply while a real geometric wedge reaches the rebuild in a
// few seconds.
constexpr uint32 DC_MAX_RESNAP_ATTEMPTS = 2;

// Consecutive navmesh-nudges the stuck ladder may spend before it gives up and
// stalls for real. The nudge is the ladder's TOP rung, so without a budget it is
// not an escalation at all — it is an infinite escape valve that resets
// rebuildAttempts and hides the wedge from the player forever. Two is enough for
// the case the nudge is actually for (a bot a few yards off a walkable poly);
// past that the geometry, not the position, is the problem.
constexpr uint32 DC_MAX_NUDGE_ATTEMPTS = 2;

// A nudge is a SIDESTEP, so its generated path must be commensurate with its
// straight-line offset. On a ramp a blind 5yd axis probe routinely resolves to a
// path that leaves the incline, doubles back along the corridor and returns —
// tens of yards of travel sold as a nudge. That is the "long walk" half of the
// ramp ping-pong (33yd, four times, in tr-20260818-073620-14). Reject any probe
// whose path exceeds this multiple of the offset; a real sidestep is ~1x.
constexpr float DC_NUDGE_MAX_DETOUR_RATIO = 2.5f;

#endif  // _DUNGEON_CLEAR_TUNING_H
