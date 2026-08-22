/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_ENGAGE_GEOMETRY_H
#define _DC_ENGAGE_GEOMETRY_H

#include <optional>
#include <vector>

#include "MoveSplineInitArgs.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h"

class Player;
class Unit;
class Creature;
class GameObject;
class WorldObject;
namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
struct DungeonBossInfo;

class DcEngageGeometry
{
public:
    // --- Dynamic aggro range ------------------------------------------------
    // The distance at which `u` would aggro `bot`. For a Creature this is the
    // core's own GetAggroRange (detection range adjusted by level difference,
    // clamped 5-45yd by the core), so the value already reflects giant elites
    // (large detection) and neutral/low-level ambient mobs (small detection)
    // instead of a single hardcoded band. Clamped to [floorYd, capYd] so a
    // pathological value can never blow out the caller's scan/engage geometry.
    // Non-creatures (and a disabled DungeonClear.DynamicAggroRange config)
    // return `fallback`. Cheap — no allocation, no pathfinding.
    static float AggroRangeOf(Player* bot, Unit* u, float fallback,
                              float floorYd, float capYd);

    // THE single source for the aggro-reach formula — the distance at which `u`
    // notices `bot`, expressed as a radius around `u`:
    //   GetAggroRange(bot) + u's reach + bot's reach + AggroRangeMargin
    //   + partyBuffer
    // `partyBuffer` is the extra slack an AVOIDANCE wants and a DETECTION does
    // not: avoidance is steering the whole party past the pack (followers cut
    // corners), detection is only asking "will this thing pull". Callers:
    //   detection  (AggroRangeOf)          -> partyBuffer = 0
    //   avoidance  (BystanderSpheres)      -> partyBuffer = PullEnRouteMargin
    //   boss skirt (RoomAggroSphereRadius) -> partyBuffer = RoomAggroPathPadding
    //   hand-offs  (BossEngageRange, PullCommitRange) -> partyBuffer = 0
    // Extracting it is what ends the drift where the blocking-trash band and the
    // avoid sphere disagreed about the same fact by ~3.5yd (the code comment in
    // BystanderSpheres promised they "can never disagree"; they did). Returns 0
    // for null/non-creature inputs. No clamping — callers clamp to their own
    // floors/caps.
    static float AggroReach(Player* bot, Unit* u, float partyBuffer);

    // The pure formula behind AggroReach, separated so the invariant "detection
    // and avoidance agree up to partyBuffer" is gtestable without a live map.
    static float AggroReachYards(float aggroRange, float mobReach, float botReach,
                                 float aggroMargin, float partyBuffer);

    // The distance at which the tank should consider itself "at the boss" and
    // hand off from the smooth long-path/direct-pursuit glide to the decisive
    // engage pull. Derived from the LIVE boss's actual aggro range plus the
    // tank's reach and a margin, so a small-aggro boss yields a short range
    // (the smooth glide carries the tank most of the way in before the pull,
    // killing the old stutter-creep) while a large-aggro elite yields a longer
    // one (the tank commits to the pull right as it would aggro anyway). Falls
    // back to `staticRange` when the boss isn't loaded yet or the dynamic-aggro
    // config is off. The trigger ladder and the advance action MUST both read
    // this so they agree on "are we at the boss".
    static float BossEngageRange(Player* bot, AiObjectContext* ctx,
                                 DungeonBossInfo const& boss, float staticRange);

    // The distance at which an advanced pull is COMMITTED: the tank stops gliding,
    // holds, and waits for the party to set at the camp BEFORE it steps in to tag.
    // Derived from `target`'s REAL aggro radius (the exact core value from
    // Creature::GetAggroRange + both reaches + AggroRangeMargin, identical to the
    // boss handoff) so the tank Forms just OUTSIDE aggro and never face-pulls the
    // pack mid-glide — the whole point of the careful pull. Clamped to the
    // PullCommitRange floor/cap (the cap stays inside the ~35yd detection band).
    // Falls back to `staticRange` for a non-creature target or when the
    // DynamicAggroRange config is off. Cheap — no allocation, no pathfinding.
    static float PullCommitRange(Player* bot, Unit* target, float staticRange);

    // --- Room-aggro clear: skirt the boss's aggro sphere -------------------
    // SINGLE SOURCE OF TRUTH for the room-aggro "avoid sphere": the radius around
    // a flagged boss the tank/party must stay OUTSIDE while pre-clearing its room,
    // so clearing never wakes the boss. EVERY room-aggro radius derives from this
    // one value — the room-trash exclusion (DungeonClearRoomTrashValue), the skirt
    // avoid-ring (AggroSafeApproachPoint's safeRadius), the follower skirt
    // (DcLeaderSignal), and the tank's boss-engage standoff (BossEngageRange).
    // Sized off `bot`'s OWN notice distance against the boss (a low-level follower
    // differs from the tank) + both combat reaches + the aggro margin + path
    // padding: a melee unit standing within a combat reach of the nearest still-
    // clearable pack (just outside this sphere) is still kept clear of the boss's
    // real aggro. Centralising it is what ends the recurring "two radii drifted
    // apart -> dead band / aggro leak" class of bug — callers MUST use this rather
    // than re-deriving the formula. Returns 0 when either input is missing.
    static float RoomAggroSphereRadius(Player* bot, Creature* boss);

    // When clearing a room before a boss whose engage drags the WHOLE room into
    // combat (RoomAggroRegistry), the tank must reach each trash pack WITHOUT its
    // approach path crossing the boss's aggro sphere — otherwise it wakes the boss
    // mid-clear and the room piles on (the SM Cathedral / Mograine failure mode:
    // Mograine sits in the room centre, so the straight line to a pack on the far
    // side cuts right through his aggro range). The room-trash VALUE already
    // excludes mobs INSIDE the sphere, but it does nothing about the PATH to a
    // pack outside it.
    //
    // Given the boss centre (bx,by,bz) and a `safeRadius` (its aggro range + the
    // reaches + margin — the same sizing as the value's exclusion sphere), returns
    // a navmesh-snapped DETOUR waypoint to move to FIRST when the straight 2D line
    // from the bot to `target` passes within `safeRadius` of the boss: a point on
    // the safe ring, stepped from the bot's current bearing toward the target's
    // bearing (both measured from the boss), so calling it each tick walks the tank
    // AROUND the sphere on the short arc instead of through it. Returns nullopt
    // once the direct approach is clear (engage the target straight) or when no
    // walkable detour can be snapped (fall back to the direct approach rather than
    // freeze the clear).
    //
    // `orbitDir` (optional, in/out) latches the rotation direction once the skirt
    // commits to rounding the LONG way around a wall-blocked short arc, so the
    // tank rounds the whole long way (going "backward" past the boss) instead of
    // flipping back toward the target every tick and bouncing between two ring
    // points forever. Pass nullptr for a one-shot, latch-free skirt (followers).
    // Reset to 0 by this function the instant the straight approach clears.
    //
    // `profile` picks the ring the orbit rides — see OrbitProfile.
    enum class OrbitProfile : uint8
    {
        // A room-aggro BOSS, the case this function was written for. The orbit
        // rides a FIXED wide ring (safeRadius + RoomAggroPartyMargin), so a tank
        // standing inside it deliberately backs out to the ring before arcing.
        // Correct there: the boss must not be woken at all, the whole room-clear
        // happens from outside that ring, and the backing-out is a one-time cost
        // paid at the start of a long orbit.
        RoomAggroBoss,
        // A BYSTANDER trash pack the tank is merely walking past. Rides the
        // tank's OWN current stand-off instead of a fixed ring, so the step is
        // purely tangential — never radially in, never radially out.
        //
        // The fixed ring is wrong here and it showed: safeRadius already carries
        // PullEnRouteMargin, so the boss ring added a SECOND party buffer
        // (RoomAggroPartyMargin, 10) on top, putting the waypoint ~40yd from a
        // pack with ~15yd of real aggro. A 34-degree step at that radius is a
        // ~23yd leg, most of it radial — the tank visibly ran BACKWARD away from
        // the pack it was approaching. And backing out is itself what clears the
        // bot->target line, so the very next tick the early-out fired, the detour
        // was dropped, and the tank snapped forward again: an excursion that
        // bought nothing and cost two reversals. Live heroic logged 73 of these,
        // only 3 of which ran long enough to be given up on — the rest were
        // one-tick round trips.
        Bystander,
    };
    static std::optional<Position> AggroSafeApproachPoint(
        Player* bot, float bx, float by, float bz, float safeRadius, Unit* target,
        int8* orbitDir = nullptr,
        OrbitProfile profile = OrbitProfile::RoomAggroBoss);

    // Pure: the ring radius and angular step one orbit tick should use, split out
    // of AggroSafeApproachPoint so the "never radially outward, never radially
    // inward" contract of the Bystander profile is gtestable without a live map.
    //
    // Bystander also bounds the LEG rather than the ANGLE: a fixed 34-degree step
    // is a 12yd leg at 20yd of stand-off and a 35yd leg at 60yd, and the long one
    // is a committed MoveTo the next tick may throw away. Capping the chord keeps
    // one sidestep cheap enough to abandon.
    struct OrbitStep
    {
        float radius = 0.0f;   // distance from the sphere centre to place the waypoint
        float step = 0.0f;     // radians of arc for this tick
    };
    static OrbitStep OrbitRing(OrbitProfile profile, float safeRadius,
                               float botRadius, float partyMargin,
                               float maxLegYards);

    // The chooser behind AggroSafeApproachPoint, factored out as pure geometry so
    // it can be unit-tested without a live map: true when the straight 2D line
    // from (botX,botY) to (targetX,targetY) passes within `avoidRadius` of the
    // boss centre (bossX,bossY) — i.e. a detour is needed. A bot already inside
    // the padded sphere reads true (its endpoint is within the radius, so the
    // recovery case still demands an exit waypoint). A degenerate radius
    // (avoidRadius <= 0) reads false (nothing to skirt).
    static bool NeedsRoomAggroSkirt(float botX, float botY,
                                    float targetX, float targetY,
                                    float bossX, float bossY, float avoidRadius);

    // --- En-route pack avoidance ------------------------------------------
    // One bystander pack's aggro sphere, as the avoidance sees it.
    struct AvoidSphere
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float r = 0.0f;          // aggro reach + reaches + margins
        ObjectGuid guid;         // the mob the sphere is centred on (latch key)
    };

    // Pure: index of the sphere the straight 2D line bot->target violates FIRST,
    // i.e. the violated sphere whose centre is nearest the bot; -1 when the line
    // is clear of all of them.
    //
    // Nearest-first, rather than "worst" or "all at once", is what lets the
    // proven single-sphere orbit do the work: the tank rounds the obstacle
    // actually in front of it, and once past, the next violator becomes the
    // nearest and the orbit re-aims at that. Trying to solve all spheres in one
    // step is where a multi-sphere solver would have to invent geometry that
    // AggroSafeApproachPoint has already had four bug-fix passes on.
    static int FirstViolatedSphere(float botX, float botY,
                                   float targetX, float targetY,
                                   std::vector<AvoidSphere> const& spheres);

    // Build the bystander avoid-sphere set around a leg of travel from the bot
    // to `legEnd`. Extracted verbatim from EnRoutePackAvoidPoint's body so the
    // pull leg and the Advance glide can never disagree about what "inside
    // aggro" means: every sphere is sized
    //   GetAggroRange(bot) + mob reach + bot reach + AggroRangeMargin
    //   + PullEnRouteMargin
    // and dead / non-hostile / critter / totem / already-in-combat / other-floor
    // mobs never become spheres. `exclude` is the destination pack — by
    // identity, formation id, or 12yd proximity — which must NEVER become an
    // avoid sphere (it is what the caller is walking to); pass nullptr for
    // Advance, which has no pack it is deliberately walking to. NOT gated on
    // PullEnRouteAvoid — callers gate. One grid search per call.
    static std::vector<AvoidSphere> BystanderSpheres(Player* bot,
                                                     Position const& legEnd,
                                                     Unit* exclude);

    // Pure: first sphere any leg of `polyline` violates, or -1 when the whole
    // polyline is clear. Writes the violating leg's START point index to
    // `legOut` — polyline[legOut] is the last point still outside the sphere
    // (when legOut > 0; legOut == 0 means the hazard is violated from the very
    // first leg, i.e. effectively already at/inside it), so a caller that
    // truncates its window to [0..legOut] glides up to the hazard's threshold
    // and stops there.
    //
    // NOTE the deliberate contract difference from FirstViolatedSphere, which
    // picks the violated sphere whose CENTRE IS NEAREST THE BOT (see its comment:
    // it feeds a single-sphere orbit that re-aims as each obstacle is passed).
    // Over a polyline "nearest the bot" is the wrong question — a route can
    // double back, so the sphere to stop at is the one violated EARLIEST ALONG
    // THE ROUTE, which may be further away in straight-line terms. Do not
    // "unify" these two orderings; they answer different questions. Within one
    // leg, ties break to the sphere centre nearest the leg start, which makes a
    // 2-point polyline behave identically to FirstViolatedSphere.
    static int FirstViolatedSphereOnPolyline(
        std::vector<G3D::Vector3> const& polyline,
        std::vector<AvoidSphere> const& spheres, size_t& legOut);

    // Pure: the point along the segment a->b where it FIRST crosses INTO the
    // circle (cx, cy, r), pulled `backoff` yards back along the segment so the
    // walker parks a hair outside rather than exactly on the boundary (a stop
    // dead on the edge reads as "inside" to the next tick and flip-flops). Z is
    // interpolated. nullopt when `a` is already inside the circle (there is no
    // threshold ahead of the walker to stop at), when the segment never reaches
    // the circle, or when the backoff eats the whole approach.
    static std::optional<G3D::Vector3> SegmentCircleEntry(
        G3D::Vector3 const& a, G3D::Vector3 const& b,
        float cx, float cy, float r, float backoff);

    // Truncate a spline window (window[0] IS the walker's live position) at the
    // first bystander sphere on it, so the glide runs up to that pack's aggro
    // THRESHOLD and stops there out of combat. Returns true and rewrites
    // `window` when the truncation is honourable; returns false leaving
    // `window` UNTOUCHED when it is not.
    //
    // That second half is the whole point of this function. The first cut of
    // this avoidance truncated to the last polyline VERTEX before the hazard
    // (window.resize(legOut + 1)), which collapses the window to the lone seed
    // point whenever the tank is at or inside a sphere — and a <2-point window
    // is not a stop, it drops Advance into the legacy per-point MoveTo walk.
    // Each hop then costs its own LastMovement delay, so the tank CRAWLED
    // through the hazard at ~2yd/s instead of gliding past it at ~7. With
    // heroic elites padded to ~30yd of reach the tank is nearly always inside
    // someone's sphere, so that was most of the approach: live Sethekk heroic,
    // 181 of 302 truncations collapsed to a single point — the "takes a few
    // steps, stops, takes a few steps" report, and the same stutter-creep the
    // spline window exists to kill (see BossEngageRange's header note).
    //
    // So: stop at the threshold, not at a vertex, and when the surviving glide
    // would be shorter than `minGlideYards` — the tank is already at or inside
    // the sphere, and there is no walk left to keep out of it — decline the
    // truncation and let the caller glide through at full speed. Preference,
    // never refusal, and never a crawl: the pack is still owned downstream by
    // the blocking-trash detector (same formula, minus the party buffer) and by
    // the pull pipeline's own orbit.
    //
    // `legOut` / `sphereOut` report the violation whether or not it was
    // honoured, for logging and for the mid-glide probe's ignore latch;
    // sphereOut is -1 when the window was clear.
    static bool TruncateWindowAtSphere(std::vector<G3D::Vector3>& window,
                                       std::vector<AvoidSphere> const& spheres,
                                       float minGlideYards, float backoff,
                                       size_t& legOut, int& sphereOut);

    // A detour waypoint that keeps the walk to `target` out of every OTHER
    // pack's aggro range, or nullopt when the straight approach is already clear
    // (or avoidance is off / nothing can be snapped — always degrade to walking
    // straight, never to standing still).
    //
    // Why this exists: the pull classifier estimates who joins a fight that
    // STAYS PUT at the target. That is the right question for sizing the pull and
    // the wrong one for getting there — a pack 40yd off the path aggros nothing
    // by standing still, and everything when the tank jogs past it. Live Sethekk
    // heroic: a pull predicted at 3 mobs was fought by 11, with three uninvolved
    // packs sitting 36-64yd from the target. The verdict was right; the walk was
    // not.
    //
    // Cost is bounded and this is deliberately NOT free: one grid search plus a
    // handful of line tests per call. Gated by PullEnRouteAvoid (heroic-only by
    // default) and intended for the leader tank's approach, not every bot.
    static std::optional<Position> EnRoutePackAvoidPoint(Player* bot,
                                                         AiObjectContext* ctx,
                                                         Unit* target);

    // True when `target` is itself STANDING INSIDE another pack's aggro sphere,
    // i.e. there is no approach to it — however the route bends — that does not
    // wake that pack. This is the half EnRoutePackAvoidPoint structurally cannot
    // cover: it steers the LEG around spheres in the way, but a sphere containing
    // the DESTINATION has no way around it, and the orbit's exit test ("a straight
    // shot at the target clears the sphere") can never come true, so the tank
    // either orbits until the borrow watchdog gives up or walks straight in.
    //
    // Same sizing as BystanderSpheres (AggroReach + PullEnRouteMargin) and the
    // same exclusions (the target's own packmates by formation or 12yd proximity,
    // dead / non-hostile / critter / totem / already-in-combat / other-floor), so
    // "inside aggro" means one thing across the module. One grid search around the
    // TARGET (not the leg), so it is cheaper than a full BystanderSpheres build.
    // Armed by PullEnRouteAvoid (heroic) or EnRouteSweepApplies (normal); false
    // when neither owns the difficulty.
    static bool TargetInsideBystanderPack(Player* bot, Unit* target);

    // Is the en-route pack SWEEP in force for this bot? A non-heroic dungeon on
    // the RouteSweepRegistry allow-list — see that header for why the scope is a
    // per-map table rather than a difficulty or a config key, and
    // DcTargeting::FindEnRouteAggroPack for what the sweep does.
    static bool EnRouteSweepApplies(Player* bot);

    // Chase-leash gate: what the walk toward a live trash target should do this
    // tick. Resolves the game state — the per-approach anchor (DcApproachState::
    // chaseTarget/chaseAnchor/chaseOrigin), the drift, the hot-destination test —
    // and hands the decision to the unit-tested DungeonClearMath::DecideChase.
    //
    // Re-anchors (and returns Follow) whenever the target changes, so the anchor
    // is stamped on the first tick a walk to that mob is actually attempted: the
    // moment we committed. Persists the hold latch back into the approach state,
    // and re-anchors again on GiveUp so neither caller can be left holding a latch
    // that reports GiveUp forever (see the note at the re-anchor).
    //
    // Exempt by construction: a target already IN COMBAT is a fight, not a pull —
    // a mob kiting a follower must still be run down — and a leash of 0
    // (PullChaseLeash) disables the gate entirely.
    static DungeonClearMath::ChaseVerdict ChaseLeash(Player* bot,
                                                     AiObjectContext* ctx,
                                                     Unit* target);

    // Stamp the chase anchor explicitly, for a caller that knows the exact moment
    // the plan was made and does not walk on that same tick.
    //
    // The advanced pull is that caller: it COMMITS at the Idle->Forming edge (the
    // camp is measured and the aggro estimate taken there) and then stands still
    // for the whole Forming dwell while the party sets. Left to ChaseLeash's own
    // lazy anchoring the anchor would be stamped on the first Advancing tick —
    // seconds later, at wherever the pack had wandered to by then — and the leash
    // would measure drift from a position the plan knows nothing about. Anchor at
    // the commit and the leg is leashed to the ground the pull was actually
    // planned against.
    static void AnchorChase(Player* bot, AiObjectContext* ctx, Unit* target);

    // True when the tank is close enough AND on a navigable level with the boss
    // to hand off from route-following to the decisive engage pull. The 3D
    // distance alone (BossEngageRange) treats a boss one floor up/down as
    // "arrived" while the tank is still running underneath it to reach the ramp
    // — which parks the tank under the boss forever ("boss is close", never
    // pulls). This adds the trash scan's level-reachability probe so the handoff
    // is deferred until the route lifts the tank onto the boss's own floor. The
    // at-boss trigger, the blocking-trash trigger, and the advance action MUST
    // all gate on this so they agree on "are we at the boss".
    static bool IsAtBossEngage(Player* bot, AiObjectContext* ctx,
                               DungeonBossInfo const& boss, float staticRange);

    // True when the tank is inside the room-clear ENVELOPE of a room-aggro boss —
    // the whole region the deliberate room-clear roams: max(room radius, skirt
    // orbit ring) + a buffer, on the boss's own floor. WIDER than IsAtBossEngage
    // (the engage standoff), which sat INSIDE the orbit ring and so dropped the
    // bot out of the room-clear window the moment it ran back onto the ring to
    // round the sphere (handing it to the boss-bound Advance — the live Jammal'an
    // failure). This is the DRIVER's window (DcTargeting::IsRoomClearActive + the
    // no-progress valve); the governor enforces a separate, narrower keep-out ring.
    // Returns false for a non-room-aggro boss (no envelope).
    static bool WithinRoomClearWindow(Player* bot, AiObjectContext* ctx,
                                      DungeonBossInfo const& boss);

    // True when the tank has actually REACHED the room-clear envelope BY PATH —
    // the same window as WithinRoomClearWindow, but measured along the navmesh
    // route to the live boss instead of straight-line. WithinRoomClearWindow (and
    // its WithinBossRangeOnFloor core) takes a same-floor straight-line shortcut,
    // so a room one WALL over reads as "in the window" even though it is a long
    // detour away by foot (Scholomance: the chamber east of Marduk & Vectus is
    // ~55yd straight-line but a 170yd+ walk around). Gating room-clear ACTIVATION
    // on that lie let the conditional room-clear event — which outranks
    // boss-travel — hijack the approach from across the map and fixate on a single
    // barely-reachable straggler. This forces the navmesh probe so room-clear only
    // engages once the tank is genuinely in the room; until then boss-travel
    // (Advance) routes it in. Returns false for a non-room-aggro boss.
    static bool TankReachedRoomByPath(Player* bot, AiObjectContext* ctx,
                                      DungeonBossInfo const& boss);

    // True when `u` is a creature that fights at RANGE — a caster or a physical
    // ranged attacker — and will therefore stand and shoot/cast from afar instead
    // of running to melee. Used by the advanced-pull camp placer to decide whether
    // the camp must break line of sight to the pack (so the rangers are forced to
    // close to the camp rather than plinking the party from across the room).
    // Detection is a cheap OR of three signals, no pathfinding:
    //   1. unit_class is a caster class (creatures map casters to MAGE; PRIEST/
    //      SHAMAN/WARLOCK are checked too in case a realm's data uses them).
    //   2. a ranged WEAPON is equipped in the virtual ranged slot — bow / gun /
    //      crossbow / wand (thrown is excluded: short range, the mob runs in).
    //   3. the creature template knows a DAMAGING spell whose max range exceeds
    //      PullRangedSpellRangeFloor (mirrors Creature::reachWithSpellAttack's
    //      effect test, minus the live mana/distance gating) — this catches a
    //      caster that the engine classed as a melee unit_class.
    // `bot` is used only to read the (per-run overridable) range-floor setting.
    // Non-creatures return false.
    static bool IsRangedAttacker(Player* bot, Unit* u);

    // True when the server is presenting `u` to every client AS A CORPSE
    // (UNIT_DYNFLAG_DEAD), even though it is technically alive. Two producers:
    // a creature_template with `dynamicflags & 32` — decorative bodies strewn
    // across a wing — and the feign-death aura effect, which sets the same bit
    // on a dormant scripted mob.
    //
    // Such a unit is NOT a pack. It does not notice you, does not chase, and
    // does not fight, so the two things this predicate guards are both wrong for
    // it: giving it an AggroRangeOf avoidance sphere (there is no aggro to
    // avoid), and picking it as blocking trash to walk over and kill. Note it
    // still passes IsAlive() and, for the corpse props, IsHostileTo() — which is
    // exactly why it needs its own test rather than falling out of the existing
    // liveness/hostility gates.
    //
    // ROOT CAUSE of tr-20260801-204608-7 (Arcatraz heroic): entries 21303
    // "Defender Corpse" / 21304 "Warder Corpse" are 1-HP proximity bombs already
    // on the DcHazardRegistry table as avoidance-only emitters, but the engage
    // layer read them as live hostile elites and handed each a 20.4yd sphere.
    // The advance glide then declined to honour a sphere it was already standing
    // in ("too close to honour") and glided an ESCORT window, while the engage
    // rung picked a corpse as blocking trash and issued a POINT detour around a
    // second corpse — and DcMoveTo's ResolveEscortConflict cancels an in-flight
    // escort, so the two rungs clobbered each other's movement generator ~2x/sec
    // for 8.5 minutes at posDelta=0.00yd until the party wiped.
    //
    // Deliberately generic rather than a registry lookup: the world has ~97 such
    // templates over ~1000 spawns (Auchenai's "Slain Auchenai Warrior", Naxx's
    // corpse piles, …), so keying on the flag fixes every wing at once instead of
    // waiting for each to be hand-authored onto the hazard table. Where a prop
    // IS also a registered hazard, that registry keeps owning its keep-out radius
    // — this only stops the aggro layer from inventing a second, conflicting one.
    static bool IsDisplayedDead(Unit const* u);

    // True when `go` is a navigation-blocking door currently in its CLOSED
    // (corridor-blocking) visual state. Encapsulates the startOpen-inverted
    // GOState test shared by the blocking-door scan and the door-reopened
    // auto-resume trigger: the GOState->open/closed mapping is inverted by the
    // door.startOpen template flag, so "closed" is `GO_STATE_READY xor
    // startOpen` (mirrors the client). Non-door GOs, doors flagged
    // ignoredByPathing (authored always-passable), and null/out-of-world GOs
    // all read NOT closed.
    static bool IsDoorClosed(GameObject const* go);

    // True only when a COMPLETE navmesh route (PATHFIND_NORMAL) exists from `bot`
    // to `p`. The camp/trail walk-back pickers choose points off the breadcrumb
    // trail, but a trail can span a navmesh seam (a drop-down, a ledge, a doorway)
    // that is short in plan view yet not walkable in a straight line; probing the
    // candidate with the same PathGenerator the move itself uses guarantees every
    // committed point is reachable over a generated path, so the move never
    // straight-lines under the map. Bounded cost: one Detour query per probe, and
    // callers probe only points they would return. Single shared home for what
    // were byte-identical file-local twins in DcPullPlanner and DcLeaderSignal.
    static bool IsNavReachable(Player* bot, Position const& p);

    // IsNavReachable PLUS a bound on how far round the route is allowed to go:
    // the generated path must also be no longer than
    // max(straight * ratio, straight + slack).
    //
    // "A path exists" and "this point is near me" are different questions, and a
    // caller that wants a SHORT move has to ask the second one. A point a few
    // yards away on the far side of a wall is perfectly PATHFIND_NORMAL — the
    // route just leaves the room, goes round, and comes back — so a picker that
    // stops at IsNavReachable will happily commit a bot to a 60yd walk it thinks
    // is a 14yd step. Same shape as the detour gate in IsLevelReachable; see
    // DC_VACATE_DETOUR_RATIO/SLACK for the retreat's numbers and what allowing
    // the unbounded version cost.
    //
    // Also reports the accepted path length through `pathLen` when non-null, so a
    // caller can log what it committed to without re-running the query.
    static bool IsNavReachableWithin(Player* bot, Position const& p,
                                     float ratio, float slack,
                                     float* pathLen = nullptr);

    // True if a closed `GAMEOBJECT_TYPE_DOOR` sits on the straight 2D line from
    // `from` to (tx,ty), within `corridorWidth` of it, on the from/target floor
    // (Z), and projecting to the INTERIOR of that chord. Used to veto engaging a
    // boss/pack on the FAR side of a shut door (the navmesh may let the tank clip
    // through, so it would otherwise bee-line onto/through the door). Computed
    // FRESH per call on purpose — the cached "dungeon clear blocking door" value
    // (500ms) can still read empty at the moment a scan first sees the far-side
    // target, which let the tank run through, clear it, and walk back. The Z and
    // interior-projection gates keep the straight chord from grazing a door on
    // another deck or in a parallel corridor and vetoing a valid near-side pull.
    // `from` is any world object (usually the bot, but the dynamic-pull chain gate
    // passes the PACK so the door test is independent of where the tank stands).
    static bool ClosedDoorBetween(WorldObject* from, float tx, float ty, float tz,
                                  float corridorWidth = 8.0f);

    // True if a closed `GAMEOBJECT_TYPE_DOOR` sits within `radius` (2D) of the
    // point (x,y,z) and on its floor (Z band). Companion to ClosedDoorBetween:
    // that one tests a door BETWEEN two points and deliberately ignores a door
    // projecting to a chord ENDPOINT, so a point sitting squarely IN a doorway
    // slips past it. This catches that — a pull camp must never be planted in a
    // still-shut doorway (the navmesh is blind to script/event doors, so the
    // party would "stand in"/clip through a door it has not opened yet). `ref`
    // supplies the map to scan; its own position is not used.
    static bool ClosedDoorNear(WorldObject* ref, float x, float y, float z,
                               float radius = 8.0f);

    // Distance TRAVELLED ALONG the long-path (from the bot) to where the route
    // first comes within the door band of the door at (doorX,doorY), or FLT_MAX
    // if it never does within `maxLookAhead`. The door-blocked handler parks the
    // tank a short stand-off before this so it stops on the NEAR side of the
    // doorway. Measured along the path on purpose: GetExactDist to a door's GO
    // origin (hinge/jamb) is unreliable — the origin can sit past the doorway
    // gap, so "within Nyd of the origin" can require walking THROUGH the gap.
    // Targets ONE specific door (the blocking one the caller already resolved):
    // considering every nearby door returned whichever the path grazed first,
    // often an off-route side door reached via a long detour, masking the real
    // blocker until the tank was already on top of it.
    static float DistAlongPathToClosedDoor(
        Player* bot, ChunkedPathfinder::Result const& path,
        float doorX, float doorY, float doorZ, float maxLookAhead);

    // Returns true if at least one navigable chunk of the path to (x, y, z)
    // exists. Delegates to ChunkedPathfinder::IsReachable, which is more
    // permissive than the legacy strict-PATHFIND_NORMAL check — partial
    // paths are acceptable because Advance now chunks long routes hop by hop.
    // The strict per-tick "actually making forward progress" check lives in
    // the position-based stuck detector inside DungeonClearAdvanceAction.
    static bool IsReachable(Player* bot, float x, float y, float z);

    // Vertical-aware reachability gate for trash/proximity target selection.
    // A candidate within DC_Z_LEVEL_TOLERANCE of the bot's Z is treated as
    // same-level and accepted with no pathfinder probe (the cheap path for
    // flat corridors). Beyond that tolerance the candidate is on a different
    // level — a balcony above, a pit below — where line-of-sight through
    // railings or floor gaps routinely reports a target the bot cannot path
    // to. For those we run a single short-range PathGenerator probe and accept
    // only a PATHFIND_NORMAL route whose endpoint actually lands on the
    // candidate's level AND whose length is commensurate with the straight-line
    // distance (DC_TRASH_DETOUR_RATIO/SLACK): a mob below a ledge that "looks"
    // 15yd away but is really a 70yd ramp detour reads NOT reachable, so the
    // tank neither locks onto an unreachable above/below mob and wedges against
    // the geometry, nor wanders off on a huge detour to one it technically can
    // reach. Trash is always within a corridor/cone lookahead well under
    // PathGenerator's single-call range cap, so one direct probe is both
    // cheaper and more precise here than the strided boss pathfinder behind
    // IsReachable.
    static bool IsLevelReachable(Player* bot, Unit* u);

    // STRICT variant of IsLevelReachable with NO same-level fast path — always
    // runs the PathGenerator probe. Requires a complete PATHFIND_NORMAL route
    // that ends on the candidate's level; for callers that pick targets from a
    // bare point-radius scan with no corridor/cone 2D pre-filter (event
    // ClearRadius seeks) a straight-line-near mob in an adjacent room/tunnel —
    // even on the SAME level — must be rejected or EngageDirect drives the tank
    // into the dividing wall.
    //
    // requireDirect (default): also reject a pathological detour (path length far
    // exceeds straight-line) — right for a room pre-clear, whose trash is meant to
    // sit AT the anchor, so a long detour means "wrong region / premature fire".
    // Pass false for a deliberate SEEK that may legitimately walk a long way to a
    // specific objective creature (KillCreature.engage): there only the
    // through-a-wall / wrong-level / no-route cases should be rejected.
    static bool IsEngageReachable(Player* bot, Unit* u, bool requireDirect = true);

    // Computes the mmap path from the bot's current position to (bx, by, bz).
    // Returns true and fills `out` only when the result is PATHFIND_NORMAL —
    // a complete navigable route. Empty/incomplete/shortcut results return
    // false. The caller uses the polyline to test "is any hostile within
    // corridorWidth yards of the next maxLookAhead yards of the path."
    static bool ComputeCorridor(Player* bot,
                                float bx, float by, float bz,
                                Movement::PointsArray& out);

};

#endif  // _DC_ENGAGE_GEOMETRY_H
