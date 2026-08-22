/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearActions.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Creature.h"
#include "CreatureAI.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSplineInitArgs.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Position.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/Data/BossPullbackRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DcEventDoorRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/ScriptedPullRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearApproach.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearApproachIo.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcDoorPolicy.h"
#include "Ai/Dungeon/DungeonClear/Util/DcMovement.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPathWorker.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTankForm.h"
#include "Ai/Dungeon/DungeonClear/Util/DcSocialQuarantine.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/LongRangePathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/StridedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/SwimPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearStateValues.h"
#include "Playerbots.h"
#include "DcActionShared.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

using namespace DcActionShared;


// --- Advanced pulls -------------------------------------------------------
//
// The maneuver, top to bottom (leader-owned phase, read cross-bot by followers):
//
//   Idle      The tank glides the route normally. When a pullable pack comes
//             within DC_PULL_START_RANGE, pick a SAFE camp back along the already
//             -cleared route (ComputeSafeCamp keeps it clear of other packs and
//             bounds the drag), stamp it, halt the escort glide, -> Forming.
//   Forming   The tank holds where it committed (just outside aggro); the party
//             walks back to the camp and goes passive (hold-at-camp). The tank
//             waits here until the party is actually set at camp (or a timeout),
//             then -> Advancing. The party — not the tank — does the repositioning.
//   Advancing The tank tags the pack: a ranged class pull if it has one and is in
//             range+LOS, else it closes to body-tag. The moment combat starts,
//             control passes to DungeonClearPullManeuverAction on the combat engine.
//   Returning (combat engine) Drag the pack back to the waiting party at camp.
//   Engage    At camp: hand the fight to stock combat; ReapStrandedPassives sees
//             the non-holding phase and releases the party. Out of combat -> Idle.
//
// Camp is anchored in cleared ground BEHIND the tank (where the party already
// trails) rather than at the tank's forward commit spot, and is chosen so the
// fight can't aggro a neighbouring pack — so the tank pulls TOWARD the party
// instead of the party running out to the tank.
namespace
{
    // STATIC FALLBACK commit range (DC_PULL_START_RANGE) — the distance at which
    // the tank stops gliding, holds, and waits in Forming for the party to set at
    // the camp BEFORE it steps in to tag. The live value is normally
    // DcEngageGeometry::PullCommitRange, sized to the pack's REAL aggro radius so
    // the tank Forms just outside where it would face-pull; this constant only
    // applies when DynamicAggroRange is off or the target isn't a creature. It
    // still sits outside a same-level mob's ~20yd aggro so even the fallback
    // doesn't face-pull. Until the pack is this close Advance glides the tank in
    // (blocking-trash stands down in pull mode so it can't engage first). Defined
    // once in DungeonClearTuning.h (shared with the trigger).
    // Per-leg watchdog (tag / return) — abort a leg that makes no progress so a
    // navmesh wedge can never freeze the pull.
    constexpr uint32 DC_PULL_LEG_TIMEOUT_MS = 10000;
    // How long the pulled mob must hold UNIT_STATE_NO_COMBAT_MOVEMENT before the
    // drag is abandoned as unwinnable. The state itself is exact — the core sets it
    // the moment something tells the creature to stop chasing, and clears its chase
    // generator with it — so this is not a confidence threshold, only a debounce
    // against creatures that toggle it TRANSIENTLY (a caster that plants for the
    // duration of a cast and resumes after). 1.5s comfortably outlasts a cast-length
    // pause while staying far inside the 10s leg watchdog it pre-empts, so a
    // genuinely planted pack is answered in the first second or two of the drag
    // rather than after the tank has hauled it the whole way for nothing.
    constexpr uint32 DC_PULL_PLANT_CONFIRM_MS = 1500;
    // Per-leg watchdog for a PULL-BACK boss drag. The flat 10s above is sized to a
    // trash pull, whose legs are a few yards — the tank commits just outside the
    // pack's aggro. A BossPullbackRegistry drag is a different scale: Ghaz'an's
    // anchor is ~150yd of PATH from him (~54yd straight-line), most of it swum out
    // and then run back up a ramp, so a flat 10s would declare the tag leg wedged
    // before the tank had even reached the water.
    //
    // Budget = straight-line leg * kPathDetourFactor (the anchor-to-boss route is
    // ~2.7x its straight line here; 2.5 is the conservative round number) at
    // kDragYardsPerSec, plus the flat timeout as slack. Deliberately still BOUNDED:
    // a genuinely wedged drag must fall out to "fight in place" rather than freeze
    // the run, which is why this is not simply disabled for a pull-back.
    constexpr float DC_PULLBACK_PATH_DETOUR = 2.5f;
    constexpr float DC_PULLBACK_YARDS_PER_SEC = 4.0f;   // pessimistic: swimming + snares

    // Defined further down, next to the phase machine they belong to; declared here
    // because DungeonClearPullAction::Execute reaches the camp-fight teardown before
    // that point in the file.
    void EndCampFight(Player* bot, AiObjectContext* context, DcPullContext& pull);
    bool ScriptedPackStillFighting(Player* bot, ScriptedPullStage const& stage,
                                   float* hpSum = nullptr);

    // Unlatch a scripted stage on an ABORT path — the two places that give a pack up
    // to the normal walk-in engage (the tag watchdog and the navmesh wedge) rather
    // than finishing it. EndCampFight is the success-side teardown and does much more
    // (the fizzle latch, the pull-back flag); all an abort needs is to stop the plan
    // owning the run.
    //
    // Skipping it is what turned a bounded, recoverable abort into a permanent freeze.
    // A latched stage short-circuits every gate above it: ScriptedPullRegistry::Find
    // returns the pinned row whatever the arm gates say, DungeonClearPullTargetValue
    // resolves that row's pack AHEAD of the corridor scan, the advance rung stands
    // down outright and the followers stay camp-held. So the abort set abortTarget to
    // the very GUID the plan then kept handing back, the pull trigger deferred on it
    // every tick, the maneuver never ran again — and nothing was left that could clear
    // either flag.
    //
    // Live (tr-20260803-144046-2): the east stage aborted at 14:44:10 and the run
    // logged the same "target ... is the abort target -> defer to normal engage" line
    // 1145 times over the next four minutes while the whole party stood parked at the
    // camp. Clearing the stage here is what lets the next Idle tick re-derive the plan
    // — or, if the pack really is unpullable, lets the run walk in and fight it.
    void AbandonScriptedStage(DcPullContext& pull)
    {
        pull.scriptedStage = -1;
        pull.scriptedRecall = false;
        pull.scriptedAtStandMs = 0;
        pull.scriptedReturnBest = 0.0f;
        pull.scriptedRecallBest = 0.0f;
        pull.scriptedEngageHp = -1.0f;
        pull.scriptedEngageSince = 0;
    }

    // Is the stage this pull is executing authored as a BODY pull — walk in and take
    // the tag by contact, never with a ranged opener? False for an ordinary pull and
    // for a ranged scripted row.
    //
    // Read at two sites in the Advancing branch, and it must be the same answer at
    // both: the opener resolution (there is no opener on a body-pull row, by
    // construction) and everything downstream that keys on "did an opener resolve" —
    // the stand-spot clamp and the bystander detour. Deriving it twice from the row is
    // cheap; deriving it inconsistently would clamp the tank onto a spot it is
    // supposed to walk off.
    bool ScriptedStageIsBodyPull(Player* bot, DcPullContext const& pull)
    {
        if (!bot || pull.scriptedStage < 0)
            return false;
        ScriptedPullStage const* const stage =
            ScriptedPullRegistry::Find(bot->GetMapId(), pull.scriptedStage);
        return stage && stage->bodyPull;
    }

    uint32 DcPullLegTimeoutMs(DcPullContext const& pull, float legDist)
    {
        // A SCRIPTED PULL's legs are authored, not emergent, and they are long by
        // design: the tag leg walks the tank from the camp to a measured stand spot
        // that may be on the far side of a doorway (Selin's west stand is ~42yd of
        // path from camp), and the return leg brings it all the way back out. The
        // flat 10s is sized to a corridor pull's few yards and would declare a
        // perfectly healthy authored leg wedged, so they share the pull-back's
        // distance-sized budget. Still bounded, for the same reason: a genuinely
        // wedged leg must fall out to "fight in place", never freeze the run.
        if (!pull.bossPullback && pull.scriptedStage < 0)
            return DC_PULL_LEG_TIMEOUT_MS;
        float const travel = std::max(0.0f, legDist) * DC_PULLBACK_PATH_DETOUR;
        uint32 const budget =
            static_cast<uint32>((travel / DC_PULLBACK_YARDS_PER_SEC) * 1000.0f) +
            DC_PULL_LEG_TIMEOUT_MS;
        return std::max(DC_PULL_LEG_TIMEOUT_MS, budget);
    }

    // Put the tank on a boss's threat list — ONE DIRECTION ONLY: the boss attacks
    // the tank, never the tank attacks the boss.
    //
    // That asymmetry is the whole point and it is load-bearing. The obvious-looking
    // other half — bot->Attack(boss) + ChangeEngine(BOT_STATE_COMBAT), which is what
    // EngageDirect and the Black Morass driver do — gives the TANK a victim, and
    // stock combat's MoveChase then drives it AT the boss. For Ghaz'an that means
    // straight into the lake, with the party following. This is not theoretical: it
    // is exactly what an earlier version of this did.
    //
    // So we only ever ask the CREATURE to come to US. Aggro alone flags the tank
    // into combat, which is all the maneuver needs — the drag-back leg then owns the
    // tank's movement at MOVEMENT_COMBAT priority and runs it home to the camp,
    // outranking any chase stock combat may later decide on.
    //
    // PER-ENCOUNTER OPT-IN. The caller only reaches this when the boss's own
    // registry row carries a positive forceAggroRange, which today is exactly one
    // row. Forcing bypasses the boss's aggro logic — normal, tuned behaviour that
    // should be left alone almost everywhere — so it is reserved for a boss that
    // cannot be tagged the ordinary way at all. See BossPullback::forceAggroRange.
    //
    // Ghaz'an is that boss. His script walks him a waypoint lap and parks him on a
    // platform which — along with the pipe leading to it — is MISSING from the
    // extracted navmesh, and he does not reliably finish the lap, so he is often
    // still out in the water when the party is ready. Either way there is no
    // reachable spot inside his aggro bubble: natural aggro can never fire, and the
    // tag leg could only burn its watchdog holding for it before the run stalled.
    //
    // Idempotent: no-ops once he is already in combat. Returns true when the boss
    // is (now) engaged on us.
    bool DcForceBossAggroOnTank(Player* bot, Creature* boss)
    {
        if (!bot || !boss || !boss->IsAlive())
            return false;

        if (!boss->IsInCombat())
        {
            // He may be sitting in the reset/home idle after his waypoint lap;
            // clear it first or the AI settles straight back into it.
            if (boss->IsInEvadeMode())
                boss->ClearUnitState(UNIT_STATE_EVADE);
            // Adds forced threat on the tank (or SetInCombatWith for a threat-less
            // unit) — this is the "get added to his aggro list" half.
            boss->EngageWithTarget(bot);
            // ...and this makes his AI actually pick the tank up and come, rather
            // than merely holding threat while standing still.
            if (boss->AI())
                boss->AI()->AttackStart(bot);
        }

        // Face him so the tank looks like it pulled rather than spinning on the
        // spot, but deliberately NO SetSelection / Attack / engine flip: see above.
        if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, boss))
            ServerFacade::instance().SetFacingTo(bot, boss);

        return boss->IsInCombat();
    }

    // The furthest point along the route toward `boss` that the tank can stand on
    // WITHOUT being in the water — "as close as it can get without getting wet".
    //
    // Derived from the real navmesh route rather than a straight line, because the
    // straight line from the anchor to Ghaz'an cuts through rock: the route runs
    // along the upper ring, down the south-east ramp, and only then meets the
    // water. Walking the route and stopping at the first wet sample lands the tank
    // on the actual shoreline whatever shape it is, and needs no second
    // hand-authored coordinate to maintain.
    //
    // The route is DENSIFIED before testing. PathGenerator returns string-pulled
    // CORNER points, so a single leg can run for tens of yards and dip through
    // water with both of its endpoints dry — testing only the vertices would walk
    // the tank straight through the lake. Sampled every DC_DRY_STANDOFF_STEP yards.
    //
    // Returns nullopt when the route is unusable, or when even the tank's current
    // position is wet (nothing to aim at — the caller keeps what it has). When the
    // whole route is dry it returns the far end, i.e. the boss himself, which is
    // the correct answer for a boss standing on reachable ground.
    constexpr float DC_DRY_STANDOFF_STEP = 3.0f;
    // How far BACK from the water's edge the stand-off is placed. The last dry
    // sample is, by definition, the last spot that is still dry — standing exactly
    // there leaves no room for error, and the tank was observed ending up in the
    // water anyway: the arrival tolerance (DC_PULL_CAMP_ARRIVE) is satisfied
    // anywhere within 5yd of the target, including 5yd PAST it, and a
    // MOVEMENT_COMBAT glide can carry a little further still. Backing the aim point
    // off by more than that tolerance makes the overshoot land on dry ground
    // instead of in the lake. Costs a couple of yards of pull range and nothing
    // else — the force-aggro reaches far past this either way.
    constexpr float DC_DRY_STANDOFF_BACKOFF = 8.0f;

    std::optional<Position> DcDryStandoffToward(Player* bot, Unit* boss)
    {
        if (!bot || !boss)
            return std::nullopt;
        Map* const map = bot->FindMap();
        if (!map)
            return std::nullopt;

        PathGenerator gen(bot);
        gen.CalculatePath(boss->GetPositionX(), boss->GetPositionY(),
                          boss->GetPositionZ(), /*forceDest*/ false);
        Movement::PointsArray const& path = gen.GetPath();
        if (path.size() < 2)
            return std::nullopt;

        uint32 const phase = bot->GetPhaseMask();
        float const collision = bot->GetCollisionHeight();

        // Every dry sample, in route order, so the back-off below can step back
        // along the ROUTE rather than in a straight line — the shoreline here is at
        // the foot of a ramp, and a straight-line back-off would aim into the ramp
        // wall instead of up it.
        std::vector<Position> dry;
        bool hitWater = false;
        for (std::size_t i = 1; i < path.size() && !hitWater; ++i)
        {
            G3D::Vector3 const& a = path[i - 1];
            G3D::Vector3 const& b = path[i];
            float const legLen = (b - a).length();
            uint32 const steps =
                std::max(1u, static_cast<uint32>(std::ceil(legLen / DC_DRY_STANDOFF_STEP)));
            for (uint32 s = 1; s <= steps; ++s)
            {
                float const t = static_cast<float>(s) / static_cast<float>(steps);
                float const x = a.x + (b.x - a.x) * t;
                float const y = a.y + (b.y - a.y) * t;
                float const z = a.z + (b.z - a.z) * t;
                if (map->IsInWater(phase, x, y, z, collision))
                {
                    hitWater = true;    // first wet sample — the route ends here
                    break;
                }
                dry.push_back(Position(x, y, z));
            }
        }

        if (dry.empty())
            return std::nullopt;

        // Whole route dry (the boss is on ground we can walk to): aim at the far
        // end, i.e. him. No back-off — there is no water to keep clear of.
        if (!hitWater)
            return dry.back();

        // Water ahead: walk back from the edge along the samples we just took until
        // we have DC_DRY_STANDOFF_BACKOFF yards of margin. If the dry stretch is
        // shorter than the back-off, the very first dry sample is the best we have.
        float back = 0.0f;
        for (std::size_t i = dry.size(); i-- > 1;)
        {
            back += dry[i].GetExactDist(&dry[i - 1]);
            if (back >= DC_DRY_STANDOFF_BACKOFF)
                return dry[i - 1];
        }
        return dry.front();
    }

    // Tag-leg creep (see the body-tag branch of DcPullPhase::Advancing). How long
    // the tank stands at the aggro edge before it starts stepping inward, and how
    // fast it then steps.
    //
    // DO NOT SHORTEN THESE TO MAKE THE PULL LOOK SNAPPIER. That was tried (400ms /
    // 6yd/s) and it made the pull visibly WORSE — the tank lurched several yards
    // into the middle of the pack before turning around. The ramp is WALL-CLOCK but
    // it is only sampled when the bot ticks, and a bot's think interval is not
    // small: PlayerbotAI::GetReactDelay returns reactDelay*10..30 (1-3 SECONDS) for
    // a bot with no real-player master, out of combat, which is exactly the state
    // the whole pre-combat pull runs in. So the first sample after arrival can
    // easily be 2s into the phase, and the ramp is applied in ONE step: at 3yd/s
    // that is a gentle 1.5yd nudge, at 6yd/s it is a 10yd charge.
    //
    // The dead time these numbers appear to cause was never really the grace — it
    // was the think interval. Fix the tick rate (DcTestRunJob keeps a real-player
    // master installed for exactly this reason) and leave the ramp alone.
    constexpr uint32 DC_PULL_TAG_CREEP_GRACE_MS = 1500;
    constexpr float DC_PULL_TAG_CREEP_YARDS_PER_SEC = 3.0f;

    // How many yards of CREEP a SCRIPTED stage's walk-in may spend, total.
    //
    // On an ordinary pull the creep is unbounded and that is fine: it ends at body
    // contact with the one pack the pull came for. On a scripted stage it ends inside
    // a room of formations the plan has deliberately left standing, and the walk from
    // the aggro edge to the pack's feet crosses every yard of margin the row was
    // authored with. Four is enough approach to generate the relocations the notice
    // test needs and far too few to reach a neighbour.
    constexpr float DC_PULL_SCRIPTED_CREEP_LIMIT = 4.0f;

    // THERE IS NO STEP-IN / RING-AND-CROSSING PAIR HERE ANY MORE, and the reason is
    // worth keeping so it is not re-derived a third time.
    //
    // Two commits tried to bound the tank's overshoot by shortening the LEG: first a
    // 2yd cap over the whole scripted walk-in, then a split that ran the approach at
    // full speed to a ring 1.5yd outside GetAggroRange and stuttered only the ~3.5yd
    // inside it. Both were aimed at the wrong quantity. The overshoot was never the
    // leg's length — it was that nothing was CANCELLING the leg, because every brake on
    // the walk-in used a stop strength that early-outs on bot->isMoving(), which a
    // server-driven spline never sets. DcPullBrake fixed that at the combat flag with
    // an unconditional stop (measured: 18.9yd -> 18.8yd, 0.1yd of coast), and the
    // aggro-edge hold below now takes the same strength.
    //
    // The split also never ran. Its band is holdRing - tagStop = margin + 2 = 3.5yd,
    // and a tank at 7.5yd/s covers 2.4-3.5yd per think, so a running bot steps over the
    // whole band between two samples: zero "crossing the aggro edge" lines in ten
    // minutes of live play across seven concurrent runs, including a body-pull row that
    // ran a full walk-in. What it did do was label every ORDINARY pull's walk-in
    // "approaching the ring" against a ring nothing aimed at, which is worse than dead
    // code — it is dead code that answers a question in the log.

    // Consecutive fizzled pulls of the SAME pack (Engage cleanup found the pull
    // target alive and idle — the drag never delivered it) before the pack is
    // handed to the normal walk-in engage via abortTarget. Casters and planted
    // stragglers ignore the drag-back, evade home once the tank breaks LOS at
    // camp, and a re-pull just repeats the fizzle — the tank bouncing forward
    // and back while the party stands at camp never entering combat.
    constexpr uint32 DC_PULL_FIZZLE_MAX = 2;
    // Longest the tank waits in Forming for the party to park+go passive at camp
    // before tagging anyway. Sized to cover the party walking back to a far camp
    // (PullSetback can be tens of yards); past it the tank tags regardless and the
    // party finishes converging on camp during the drag-back. The between-pulls
    // gate already ensured the party was close and rested before the pull began.
    constexpr uint32 DC_PULL_PARTY_SET_TIMEOUT_MS = 8000;
    // How close to camp the tank must get on the return before releasing.
    constexpr float DC_PULL_CAMP_ARRIVE = 5.0f;
    // ...and, for a PULL-BACK only, how close the BOSS must also be before the
    // party is released. The tank gets home long before a boss it pulled from the
    // water's edge, and releasing on tank-arrival alone sent the whole party
    // sprinting at the inbound boss (into the lake). Sized a little over melee
    // range plus the camp slot spread, so the release lands as he closes on the
    // anchor rather than after he is already swinging.
    constexpr float DC_PULLBACK_RELEASE_RANGE = 18.0f;
    // Where a summoned pull-back boss lands, measured out from the anchor toward
    // the tank. Far enough that he does not materialise inside the party (a
    // knockback or cleave landing on everyone at once), close enough to be inside
    // the release range so the fight starts the same tick.
    constexpr float DC_PULLBACK_SUMMON_OFFSET = 6.0f;
    static_assert(DC_PULLBACK_SUMMON_OFFSET < DC_PULLBACK_RELEASE_RANGE,
                  "a summoned boss must land inside the release range, else the "
                  "party is held at camp with the boss already on top of them");
    // "Party is set" tolerance for the Forming gate. A touch wider than the hold
    // radius so a follower parked at the boundary reliably counts as set instead
    // of flickering in/out and never letting the tank tag.
    constexpr float DC_PULL_SET_RADIUS = 8.0f;

    // Resolve the advanced-pull camp params (setback / camp-clear radius / drag cap)
    // from the run settings, widening the clearance and drag cap to the boss's skirt
    // when a room-aggro pre-clear with a skirt override is active (Sepethrea). Without
    // this the camp only clears the boss by the generic PullCampSafeRadius (~25yd),
    // which can land inside her wider aggro/CallForHelp when a pack kept on her edge
    // (pullOutRadius) is dragged just far enough to clear other packs. Raising the drag
    // cap alongside lets the drag actually reach the wider camp. Everywhere else the
    // skirt is 0 and the settings pass through unchanged.
    void DcResolveCampParams(Player* bot, AiObjectContext* ctx,
                             float& setback, float& safeRadius, float& maxDrag)
    {
        setback = DcSettings::GetFloat(bot, "PullSetback");
        safeRadius = DcSettings::GetFloat(bot, "PullCampSafeRadius");
        maxDrag = DcSettings::GetFloat(bot, "PullMaxDrag");
        float const skirt = DcTargeting::ActiveRoomSkirt(bot, ctx);
        if (skirt <= 0.0f)
            return;
        safeRadius = std::max(safeRadius, skirt);
        maxDrag = std::max(maxDrag, skirt + DcSettings::GetFloat(bot, "RoomAggroPartyMargin"));
    }

    // Thin context adapter: resolves the pull-context value and delegates every
    // phase write to DcPullContext::Transition, where the Engage special-case and
    // the transition invariants live. No phase-write logic remains in this TU.
    void DcSetPullPhase(AiObjectContext* context, DcPullPhase p)
    {
        DcPullContext& pull = context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
        pull.Transition(p, getMSTime());
    }

    // HARD loss of control: the four states under which the drag-back is not happening
    // at all, as opposed to happening slowly. Read purely off Unit state so no aura-id
    // table is needed; the returned string is a stable literal safe to log.
    //
    // Split out of DcDragImpairReason below because the two halves of that predicate
    // want different answers on a SCRIPTED stage — see the CC block in the maneuver for
    // why a slow keeps dragging there and a stun may not.
    char const* DcDragHardCcReason(Player* tank)
    {
        if (!tank)
            return nullptr;
        if (tank->HasUnitState(UNIT_STATE_STUNNED))
            return "stunned";
        if (tank->HasUnitState(UNIT_STATE_FLEEING))
            return "feared";
        if (tank->HasUnitState(UNIT_STATE_CONFUSED))
            return "confused";
        if (tank->IsRooted() || tank->HasUnitState(UNIT_STATE_ROOT))
            return "rooted";
        return nullptr;
    }

    // Why the leader tank can't carry out the pull drag-back right now, or nullptr
    // if it is fine. The hard states above stop the retreat outright; a heavy movement
    // slow (run speed at/below `slowFloor` of base) drags so slowly the pack wins the
    // race home. Daze is already immunized for the pull
    // (DcLeaderSignal::SetLeaderDazeImmunity), so a slow seen here is a genuine debuff
    // — Hamstring, web, a Frostbolt chill. The timing/grace decision lives in the
    // unit-tested DungeonClearMath::ShouldAbortPullForCc.
    char const* DcDragImpairReason(Player* tank, float slowFloor)
    {
        if (!tank)
            return nullptr;
        if (char const* const hard = DcDragHardCcReason(tank))
            return hard;
        if (tank->GetSpeedRate(MOVE_RUN) <= slowFloor)
            return "slowed";
        return nullptr;
    }
}


bool DungeonClearPullAction::Execute(Event& /*event*/)
{
    DcPullContext& pull = context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    Position& camp = pull.camp;
    uint32 const since = pull.phaseSince;
    uint32 const now = getMSTime();
    DcPullPhase const phase = pull.phase;

    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);

    // SOCIAL QUARANTINE upkeep. The advance rung does the same thing, and between
    // them the leader re-asserts it in every state it can be in outside a boss
    // fight — but THIS is the call that matters for a scripted plan, because a
    // stage's release is keyed on it being the due stage and this rung is where
    // the due stage is read and committed. Running it here, ahead of the phase
    // switch, means the pack the tank is about to walk into is aggressive on the
    // same tick the plan picks it, and its four neighbours are not.
    // See DcSocialQuarantine.h.
    DcSocialQuarantine::Update(bot, context);

    // A druid tank pulls as a BEAR. Every rung of this maneuver — commit, the
    // Forming dwell, the tag walk-in — runs on the NON-combat engine, and the
    // only thing that shifts a feral druid is BearDruidStrategy's "bear form"
    // trigger, which is combat-engine only. So without this the druid tags in
    // caster form, eats the pack's opener at caster armour/health, and shifts a
    // beat later once aggro has already flipped the engine — the live "pulls in
    // human form, takes a few hits, then shifts to bear". Fire-and-forget: the
    // shift is instant and doesn't interrupt (or wait on) the approach, and
    // nothing here branches on whether it landed, so a form that can't go up
    // can never hold the pull. See DcTankForm.
    //
    // Gated on a pull actually being live rather than called unconditionally:
    // an Idle tick with no pull target is the tank merely walking the route,
    // where it may still need caster form to drink (Smart Rest). Idle WITH a
    // target does shift — see the Idle branch, which arms it once the target is
    // resolved rather than resolving it twice.
    if (phase != DcPullPhase::Idle)
        DcTankForm::EnsureBearForm(botAI);

    switch (phase)
    {
        case DcPullPhase::Idle:
        {
            // Begin a pull: choose a SAFE camp back along the cleared route, hold
            // here, and let the party reposition to it (the tank doesn't retreat).
            Unit* trash = next.has_value()
                ? DcTargeting::GetPullTarget(botAI) : nullptr;
            if (!trash)
            {
                DC_PULL_TRACE("[DC:{}] pull idle: no pull target -> yield to advance",
                              bot->GetName());
                return false;
            }

            // A pack is in hand — arm the druid tank's bear form now, so it is
            // already up by the time this tick's commit lands (see the top of
            // Execute, which covers every later phase).
            DcTankForm::EnsureBearForm(botAI);

            // PULL-BACK boss (BossPullbackRegistry). Two things differ from a
            // trash pull and both are structural, not tuning:
            //
            //  * The camp is the hand-authored ANCHOR, never ComputeSafeCamp's
            //    trail-derived point. The anchor was chosen because it is the
            //    nearest ground the party can survive on; a trail camp would be
            //    "PullSetback yards back from wherever the tank is", which is not
            //    the same place and carries no such guarantee.
            //  * There is no glide-closer-to-commit phase. The tank is ALREADY on
            //    the anchor — arriving there is what armed this pull — and Advance
            //    deliberately routes to the anchor and never at the boss, so
            //    yielding to it would just stand still forever. Commit now; the
            //    Advancing leg is what covers the (long) distance out to the boss.
            if (BossPullback const* pb =
                    BossPullbackRegistry::Find(bot->GetMapId(), trash->GetEntry()))
            {
                pull.PublishCamp(Position(pb->campX, pb->campY, pb->campZ), now);
                pull.bossPullback = true;
                pull.losPull = false;   // not an LOS-break pull; distinct suppression
                pull.pullTarget = trash->GetObjectGuid();
                DcMovement::StopBot(bot, DcMovement::Stop::Hold);
                DcFaceIfNeeded(bot, trash);
                DcSetPullPhase(context, DcPullPhase::Forming);
                DC_PULL_INFO("[DC:{}] pull-back plan: boss {} at {:.1f}yd | anchor "
                             "({:.1f},{:.1f},{:.1f}) -> forming, waiting for party to "
                             "set on safe ground",
                             bot->GetName(), trash->GetObjectGuid().ToString(),
                             bot->GetExactDist(trash), pb->campX, pb->campY, pb->campZ);
                return true;
            }

            // SCRIPTED PULL STAGE (ScriptedPullRegistry). Same three departures from
            // an ordinary pull as a pull-back, for the same kind of reason — the
            // plan is knowledge the pipeline cannot derive:
            //
            //  * The camp is the row's, never ComputeSafeCamp's. A trail camp is
            //    "PullSetback yards back from wherever the tank is", which in a room
            //    like Selin's is a point in open floor with sight-lines to both
            //    remaining packs and the boss. The authored camp is behind a
            //    specific wall.
            //  * There is no glide-closer-to-commit phase. Commit the moment the
            //    stage arms, so the party starts walking to the camp while the tank
            //    is still where the route left it. Waiting for commit range would
            //    mean gliding the tank at the pack first — i.e. into the room, past
            //    the boss — which is the entire thing being avoided.
            //  * The tag is taken from the row's stand spot (see the Advancing
            //    branch), not from wherever the aggro edge falls.
            //
            // The pull TARGET is already the stage's (DungeonClearPullTargetValue
            // resolves the plan ahead of the corridor scan), so `trash` here is a
            // member of the pack this stage names.
            if (ScriptedPullStage const* stage =
                    DcTickMemoAccess::ScriptedStage(bot, context))
            {
                pull.PublishCamp(Position(stage->campX, stage->campY, stage->campZ), now);
                pull.scriptedStage = static_cast<int32>(stage->order);
                pull.losPull = false;   // the stand spot, not a camp LOS break
                pull.pullTarget = trash->GetObjectGuid();
                DcMovement::StopBot(bot, DcMovement::Stop::Hold);
                DcFaceIfNeeded(bot, trash);
                DcSetPullPhase(context, DcPullPhase::Forming);
                // THE OPENER, ON THE PLAN LINE. On a RANGED row the whole maneuver is
                // "tag from the stand spot without moving off it", so whether the tank
                // has anything to tag WITH is the single most load-bearing fact about
                // the attempt — and it was the one fact the log never carried. A stage
                // with no opener is silent in exactly the same way as a stage with one:
                // the tank walks to the spot, holds, and the leg watchdog eventually
                // hands the pack to the walk-in. Telling those two apart took a
                // database query against the tank's ammo slot (tr-20260803-154419-13).
                // Once per stage, next to the coordinates it belongs with.
                //
                // A BODY-PULL row does not resolve one at all — not "resolves one and
                // ignores it". Asking would put a spell id on the plan line that the
                // stage is never going to cast, which is exactly the kind of log that
                // sends the next reader looking for a cooldown or an ammo slot.
                std::optional<ResolvedPullSpell> const opener =
                    stage->bodyPull ? std::nullopt : ResolvePullSpell(botAI, bot);
                DC_PULL_INFO("[DC:{}] scripted-pull plan [{}]: target {} at {:.1f}yd | "
                             "camp ({:.1f},{:.1f},{:.1f}) | stand "
                             "({:.1f},{:.1f},{:.1f}) | opener {} -> forming, waiting "
                             "for the party to set at camp",
                             bot->GetName(), stage->name ? stage->name : "?",
                             trash->GetObjectGuid().ToString(), bot->GetExactDist(trash),
                             stage->campX, stage->campY, stage->campZ,
                             stage->standX, stage->standY, stage->standZ,
                             stage->bodyPull
                                 ? std::string("none — this row is authored as a BODY "
                                               "PULL, and the stand spot is a waypoint "
                                               "on the way to the pack's aggro edge")
                             : opener ? std::to_string(opener->spellId)
                                      : std::string("NONE (no class opener and no usable "
                                                    "ranged weapon — this stage cannot "
                                                    "tag and will time out)"));
                return true;
            }

            // Patrol-wait hold (decision == 3): the governor has the pull held at
            // commit range to let a patrol clear before committing. Halt and own the
            // tick — the blocking/room-trash engages already stood down for decision
            // 3, so this just keeps the tank planted (no tag, no camp handshake)
            // until the governor flips the verdict (patrol passed -> LEEROY/ADVANCED)
            // or the wait times out. Followers trail normally (pull mode is off).
            if (pull.decision == DcPullDecisionCode::PatrolHold)
            {
                // Hold, not Soft: this branch is only reached at commit range,
                // i.e. the tank arrives here mid escort-spline glide driven by
                // Advance. Stop::Soft is not escort-aware and lets the tank coast
                // on into the pack it is meant to be waiting out; Stop::Hold kills
                // the glide (same reason the commit branch below uses Hold).
                DcMovement::StopBot(bot, DcMovement::Stop::Hold);
                DC_PULL_TRACE("[DC:{}] pull idle: holding for patrol ({:.1f}yd to pack)",
                              bot->GetName(), bot->GetExactDist2d(trash));
                return true;
            }
            // Don't commit until the pack is within reach (the trigger gates on the
            // same range). While it's farther, yield to Advance to glide closer —
            // but PUBLISH a prospective camp NOW so the party walks up to it IN
            // PARALLEL with the tank's approach, instead of holding at the stale
            // camp until the tank arrives and only THEN trudging forward (the old
            // robotic "tank scouts, wait, party moves, wait" stall the player saw).
            //
            // ComputeSafeCamp returns a point `setback` behind the tank along the
            // breadcrumb trail; as the tank glides in it creeps forward, so the
            // party trails at a roughly fixed standoff. We only ADOPT a candidate
            // that sits closer to the pack than the current camp (with a little
            // hysteresis), so the party advances monotonically and is never dragged
            // backward early in the glide — when "setback behind the tank" can still
            // fall behind the previous/seed camp. We still yield (return false) so
            // Advance keeps gliding the tank; the camp update is a pure side effect.
            float const toTrash = bot->GetExactDist2d(trash);
            // Commit only once the pack is within its OWN aggro range (+ margin), so
            // the tank stops to Form just outside where it would otherwise face-pull.
            float const commitRange =
                DcEngageGeometry::PullCommitRange(bot, trash, DC_PULL_START_RANGE);
            if (toTrash > commitRange)
            {
                // Room-wide-aggro pre-clear (RoomAggroRegistry): the pull is aimed
                // at a ROOM mob near a flagged boss, not a corridor pack. Yielding
                // to Advance here would glide the tank toward the BOSS and wake it
                // — exactly what the pre-clear exists to prevent. Instead publish a
                // prospective camp (so the party trails up in parallel) and walk
                // straight to the room-trash unit until it is within commit range,
                // then fall through to the normal Forming handshake below.
                if (DcTargeting::IsRoomClearActive(bot, context))
                {
                    float sb = 0.0f, sr = 0.0f, md = 0.0f;
                    DcResolveCampParams(bot, context, sb, sr, md);
                    float clr = 0.0f, drg = 0.0f;
                    if (std::optional<Position> ahead = DcPullPlanner::ComputeSafeCamp(
                            botAI, trash, sb, sr, md, clr, drg))
                    {
                        if (!pull.HasCamp() || ahead->GetExactDist2d(trash) + 3.0f <
                                                   camp.GetExactDist2d(trash))
                            pull.PublishCamp(*ahead, now);
                        else
                            pull.TouchCampOwnership(now);
                    }
                    // Walk to the room trash, skirting the boss's aggro sphere if
                    // the direct line clips it (RoomAggroSkirtPoint) — the same
                    // detour EngageDirect applies, so the pull-idle approach can't
                    // wake the boss either.
                    MoveToSkirtingRoomAggro(trash, MovementPriority::MOVEMENT_NORMAL);
                    DC_PULL_TRACE("[DC:{}] pull idle (room-clear): closing on room "
                                  "trash {} at {:.1f}yd (commit {:.1f})",
                                  bot->GetName(), trash->GetObjectGuid().ToString(),
                                  toTrash, commitRange);
                    return true;
                }
                // Bystander detour on the way OUT to the pack.
                //
                // The tag leg (Advancing, below) already bends around other packs,
                // but it only begins once the tank is inside commit range — by then
                // the room has already been crossed, and crossing it is where the
                // extra packs come from. Above commit range the walk is Advance's
                // long-path glide, which routes for NAVIGATION and has no notion of
                // anyone's aggro arc: it will happily jog the tank through two
                // sleeping packs to reach the one the classifier correctly sized at
                // three. That is the live heroic "predicted 3, fought 11".
                //
                // So when a bystander sphere genuinely sits on the line, borrow the
                // tick and walk the orbit ourselves. Deliberately narrow:
                //   * only when a sphere is actually violated (nullopt otherwise —
                //     the overwhelmingly common tick yields to Advance untouched);
                //   * only when the pack sits at the end of one complete on-level
                //     route, i.e. the open-room case where Advance's chunked
                //     long-path is buying nothing. A pack up a ramp or through a
                //     corridor still goes to Advance, which is the only thing that
                //     can route to it at all;
                //   * only while the borrow keeps closing distance
                //     (ShouldKeepAvoidDetour) — Advance's wedge/stall ladder is
                //     parked while we hold the tick, so a non-converging orbit hands
                //     the walk straight back instead of freezing the run.
                // A failed snap or a failed move falls through to the yield exactly
                // as before: preference, never refusal.
                auto driveBystanderDetour = [&]() -> bool
                {
                    if (pull.avoidLegTarget != trash->GetObjectGuid())
                    {
                        pull.avoidLegTarget = trash->GetObjectGuid();
                        pull.avoidSinceMs = 0;
                        pull.avoidBestDist = 0.0f;
                        pull.avoidGaveUp = false;
                    }
                    // Given up on this pack already: stay out of Advance's way for
                    // the rest of the approach (and skip the grid search).
                    if (pull.avoidGaveUp)
                        return false;
                    if (!DcEngageGeometry::IsEngageReachable(bot, trash,
                                                             /*requireDirect*/ false))
                        return false;
                    std::optional<Position> const avoid =
                        DcEngageGeometry::EnRoutePackAvoidPoint(bot, context, trash);
                    if (!avoid)
                    {
                        // Line is clear — stop borrowing and let the clock rearm from
                        // scratch on the next obstacle.
                        pull.avoidSinceMs = 0;
                        return false;
                    }
                    if (!DungeonClearMath::ShouldKeepAvoidDetour(
                            now, toTrash, DC_PULL_AVOID_STALL_MS,
                            DC_PULL_AVOID_PROGRESS_YD, pull.avoidSinceMs,
                            pull.avoidBestDist))
                    {
                        pull.avoidGaveUp = true;
                        DC_PULL_INFO("[DC:{}] pull idle: bystander detour stopped "
                                     "closing on {} ({:.1f}yd) -> handing the walk "
                                     "back to advance for this pack",
                                     bot->GetName(), trash->GetObjectGuid().ToString(),
                                     toTrash);
                        return false;
                    }
                    bool const moved =
                        DcMoveTo(trash->GetMapId(), avoid->GetPositionX(),
                                 avoid->GetPositionY(), avoid->GetPositionZ(),
                                 /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                                 /*exact_waypoint*/ false, MovementPriority::MOVEMENT_NORMAL);
                    if (!(moved || bot->isMoving() ||
                          IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL)))
                        return false;
                    DC_PULL_DEBUG("[DC:{}] pull idle: detouring around a bystander pack "
                                  "on the way to {} ({:.1f}yd) -> ({:.1f},{:.1f})",
                                  bot->GetName(), trash->GetObjectGuid().ToString(), toTrash,
                                  avoid->GetPositionX(), avoid->GetPositionY());
                    return true;
                };

                float const setback = DcSettings::GetFloat(bot, "PullSetback");
                float const safeRadius = DcSettings::GetFloat(bot, "PullCampSafeRadius");
                float const maxDrag = DcSettings::GetFloat(bot, "PullMaxDrag");
                float clrAhead = 0.0f;
                float dragAhead = 0.0f;
                if (std::optional<Position> ahead = DcPullPlanner::ComputeSafeCamp(
                        botAI, trash, setback, safeRadius, maxDrag, clrAhead, dragAhead))
                {
                    float const candToTrash = ahead->GetExactDist2d(trash);
                    // +3yd hysteresis: only rewrite when the candidate is meaningfully
                    // more forward, so the party isn't churned by tick-to-tick jitter.
                    // A successful camp computation claims ownership for this
                    // tick even when the hysteresis keeps the old point —
                    // "no change" is still an ownership decision (TouchCampOwnership),
                    // and Advance's scout-trailing must not wrestle the camp meanwhile
                    // (see campPublishedMs / DC_CAMP_PUBLISH_FRESH_MS).
                    if (!pull.HasCamp() || candToTrash + 3.0f < camp.GetExactDist2d(trash))
                    {
                        pull.PublishCamp(*ahead, now);
                        DC_PULL_DEBUG("[DC:{}] pull idle: target {} at {:.1f}yd > start "
                                      "range {:.1f} -> party advances to camp "
                                      "({:.1f},{:.1f},{:.1f}) {:.1f}yd from pack while "
                                      "tank closes", bot->GetName(),
                                      trash->GetObjectGuid().ToString(), toTrash,
                                      commitRange, camp.GetPositionX(),
                                      camp.GetPositionY(), camp.GetPositionZ(),
                                      candToTrash);
                        return driveBystanderDetour();
                    }
                    // Hysteresis kept the old camp: still assert ownership this tick.
                    pull.TouchCampOwnership(now);
                }
                DC_PULL_TRACE("[DC:{}] pull idle: target {} at {:.1f}yd > start range "
                              "{:.1f} -> glide closer before committing",
                              bot->GetName(), trash->GetObjectGuid().ToString(), toTrash,
                              commitRange);
                return driveBystanderDetour();
            }
            // Pick the camp: a generous distance back along the cleared route
            // (PullSetback) so the party gets real room, extended further only if
            // another pack is still within PullCampSafeRadius (ComputeSafeCamp).
            // Dungeon mobs have no leash, so far-back is good; PullMaxDrag is just a
            // sanity cap. During a skirt-override room-clear the clear radius and drag
            // cap widen to the boss's skirt (DcResolveCampParams) so the kept-close
            // pack is dragged clear of the boss before the kill.
            float setback = 0.0f, safeRadius = 0.0f, maxDrag = 0.0f;
            DcResolveCampParams(bot, context, setback, safeRadius, maxDrag);
            float clearance = 0.0f;
            float drag = 0.0f;
            std::optional<Position> camped = DcPullPlanner::ComputeSafeCamp(
                botAI, trash, setback, safeRadius, maxDrag, clearance, drag);
            pull.PublishCamp(camped.has_value()
                                 ? *camped
                                 : Position(bot->GetPositionX(), bot->GetPositionY(),
                                            bot->GetPositionZ()),
                             now);

            // Record whether this is a line-of-sight pull (ranged pack, camp placed
            // to break LOS) so the addon status line can announce it. Mirrors the
            // gate inside ComputeSafeCamp.
            pull.losPull = DcSettings::GetBool(bot, "PullRangedLosBreak") &&
                           DcEngageGeometry::IsRangedAttacker(bot, trash);

            // The pack this pull is about. Survives EnterEngage so the Engage
            // cleanup can detect a fizzled drag (target alive + idle after the
            // camp fight) and latch the pack over to the walk-in engage.
            pull.pullTarget = trash->GetObjectGuid();

            // Anchor the chase leash HERE, not on the tag leg's first tick. This is
            // the moment the plan exists: the camp above was measured against this
            // spot and the aggro estimate was taken here. The tank then stands still
            // for the whole Forming dwell while the party sets, so a lazily-anchored
            // leash would stamp seconds later at wherever the pack had wandered to
            // and measure drift from ground the plan never saw.
            DcEngageGeometry::AnchorChase(bot, context, trash);

            // Halt the escort glide for real before committing. A plain StopMoving
            // does NOT cancel a launched escort spline (the door-blocked park is the
            // other witness), so without StopBot(Hold) the tank keeps gliding
            // forward past the commit spot and the camp is stamped behind a
            // position it's already leaving — the old overshoot / wrong-target pull.
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
            // Square up on the pack for the dwell at the commit spot.
            DcFaceIfNeeded(bot, trash);
            DcSetPullPhase(context, DcPullPhase::Forming);
            // clearance == -1 means no other pack is anywhere near the camp.
            float const clrDisp =
                clearance >= std::numeric_limits<float>::max() ? -1.0f : clearance;
            size_t const trailLen = pull.breadcrumbs.size();
            DC_PULL_INFO("[DC:{}] advanced-pull plan: target {} at {:.1f}yd | camp "
                         "({:.1f},{:.1f},{:.1f}) drag {:.1f}yd | clearance {:.1f}yd "
                         "(safe {:.0f}, setback {:.0f}, trail {}) -> forming, "
                         "waiting for party",
                         bot->GetName(), trash->GetObjectGuid().ToString(), toTrash,
                         camp.GetPositionX(), camp.GetPositionY(), camp.GetPositionZ(),
                         drag, clrDisp, safeRadius, setback, trailLen);
            return true;
        }

        case DcPullPhase::Forming:
        {
            // The tank HOLDS where it committed (just outside aggro). The party
            // walks back to the camp and goes passive (hold-at-camp). Tag only once
            // the party is actually set, so the pull never drags into open ground.
            DcMovement::StopBot(bot, DcMovement::Stop::Soft);
            // Keep facing the pack across the multi-second dwell (idempotent —
            // re-faces only if the pack repositioned us off-axis).
            DcFaceIfNeeded(bot, DcTargeting::GetPullTarget(botAI));

            // A SCRIPTED stage's camp can be a long way behind the tank — Selin's is
            // 42yd back down the corridor — and 8s does not reliably cover a 42yd
            // walk, so the dwell would expire and the tank would tag with the party
            // still strung out along the corridor behind it.
            //
            // The TOLERANCE is deliberately still the flat DC_PULL_SET_RADIUS. It was
            // briefly widened here, on the reasoning that a held follower stops the
            // instant it is inside its own leash and so parks right on this gate's
            // boundary — which was true, and was a symptom: the leash had no business
            // applying while the tank is away tagging. Passive followers now take the
            // tight slot pin (see DcFollowerActions), so they settle far inside this
            // radius and it needs no margin.
            uint32 const setTimeoutMs =
                pull.scriptedStage >= 0
                    ? ScriptedPullTravelBudgetMs(bot->GetExactDist(&camp))
                    : DC_PULL_PARTY_SET_TIMEOUT_MS;

            bool const partySet =
                DcPullPlanner::IsPartySetAtCamp(bot, camp, DC_PULL_SET_RADIUS);
            bool const formingTimedOut = (now - since) > setTimeoutMs;
            if (!partySet && !formingTimedOut)
            {
                DC_PULL_TRACE("[DC:{}] pull forming: waiting for party to set at camp "
                              "({}/{} ms)", bot->GetName(), now - since, setTimeoutMs);
                return true;
            }
            // SCRIPTED: drop anything still driving the tank before the tag leg
            // starts. The route rungs stand down once the phase leaves Idle, but
            // that cannot un-launch a spline issued on the very tick the pull
            // committed — and advance's is 70yd of escort glide aimed at the boss
            // (tr-20260802-222832-1). Killing it here costs nothing (the tank is
            // parked at the commit spot by the Forming dwell) and means the walk to
            // the stand spot starts from a clean slate.
            if (pull.scriptedStage >= 0)
            {
                DcMovement::StopBot(bot, DcMovement::Stop::Hold);
                DcMovement::ClearMovementWait(bot);
            }

            DcSetPullPhase(context, DcPullPhase::Advancing);
            DC_PULL_DEBUG("[DC:{}] pull forming complete ({}) -> advancing (tag)",
                          bot->GetName(),
                          partySet ? "party set" : "timed out waiting for party");
            return true;
        }

        case DcPullPhase::Advancing:
        {
            // Tag the pack. The moment combat starts the non-combat trigger stops
            // firing and DungeonClearPullManeuverAction drags it back to camp.
            Unit* trash = next.has_value() ? DcTargeting::GetPullTarget(botAI) : nullptr;
            if (!trash)
            {
                // Pack died / despawned before we tagged it — nothing to pull.
                DcSetPullPhase(context, DcPullPhase::Idle);
                DC_PULL_DEBUG("[DC:{}] pull advancing: target gone -> idle",
                              bot->GetName());
                return false;
            }

            // Tag-leg budget: the camp-to-target span, so a pull-back's long haul
            // out to the boss gets a proportionate watchdog (see DcPullLegTimeoutMs).
            if ((now - since) > DcPullLegTimeoutMs(pull, camp.GetExactDist(trash)))
            {
                // Stalled without aggro (e.g. the tag resisted and we never closed).
                // Hand this pack to the normal walk-in engage so the run never hangs —
                // which means unlatching a scripted stage too, or "so the run never
                // hangs" is exactly what it stops doing (see AbandonScriptedStage).
                pull.abortTarget = trash->GetObjectGuid();
                AbandonScriptedStage(pull);
                DcSetPullPhase(context, DcPullPhase::Idle);
                DC_PULL_INFO("[DC:{}] advanced-pull: tag timed out (target {} at "
                             "{:.1f}yd) -> normal engage", bot->GetName(),
                             trash->GetObjectGuid().ToString(), bot->GetExactDist(trash));
                return false;
            }

            // CHASE LEASH. The tag leg re-aims at the target's LIVE position every
            // tick, so a pack that WALKS turns it into a pursuit: the tank follows
            // a patrol wherever its route takes it — including back behind other
            // packs — and arrives at the camp with the whole room. The plan this
            // leg is executing (the size estimate, the camp) was measured against
            // where the pack stood at commit; once it has receded from there, the
            // leg is walking at ground the plan never covered.
            //
            // So hold instead of chasing. A patrol is a loop and comes back to us;
            // holding is also what the party is set up for — they are already
            // passive at the camp, so a few seconds of the tank standing at its
            // commit spot costs nothing and risks nothing. GiveUp hands the pack to
            // the normal walk-in engage exactly like the leg watchdog above (which
            // is deliberately the longer of the two clocks), so a patrol that never
            // comes back can never stall the run.
            //
            // SCRIPTED PULL: walk to the row's stand spot before doing anything else.
            //
            // That spot is the whole plan. It is the one place with line of sight to
            // THIS pack and to nothing else, and the tag has to happen from there or
            // the pull is just the generic one that has already been shown not to
            // work in this room. So it takes priority over the tag machinery below,
            // and the leg watchdog above (distance-sized for a scripted stage) bounds
            // it — a stand spot the tank cannot reach falls out to the normal engage
            // rather than hanging.
            //
            // Skipped once the pack is tagged: after that the tank is either about to
            // be flipped to the drag-back by aggro, or it is holding for aggro right
            // where it tagged, and re-issuing a walk to the stand spot would fight
            // both. Arrival tolerance is the camp-arrive radius — the spot is a
            // vantage point, not a pixel.
            //
            // And skipped once the tank has ARRIVED, whether or not anything is tagged
            // yet — DcPullContext::scriptedAtStandMs, stamped below. On a ranged row
            // those two conditions coincide (the tag is taken from the spot, so the
            // tank is standing on it when tagTarget goes live) and the latch changes
            // nothing. On a BODY-PULL row they are opposites: arriving is precisely
            // when the tank starts walking AWAY, out to the pack's aggro edge ~8yd on,
            // and nothing is tagged until it gets there. Without the latch this leg
            // sees the tank drift past the arrive radius a tick or two into that walk
            // and hauls it back, then the tag leg pushes forward again — the reported
            // walk-forward / turn-around / walk-forward shuffle, which in this room
            // also burns the leg budget without ever reaching the pack.
            if (pull.scriptedStage >= 0 && pull.scriptedAtStandMs == 0 &&
                pull.tagTarget != trash->GetObjectGuid())
            {
                if (ScriptedPullStage const* stage =
                        ScriptedPullRegistry::Find(bot->GetMapId(), pull.scriptedStage))
                {
                    Position const stand(stage->standX, stage->standY, stage->standZ);
                    float const toStand = bot->GetExactDist(&stand);
                    if (toStand > DC_PULL_CAMP_ARRIVE)
                    {
                        DC_PULL_TRACE("[DC:{}] scripted-pull: walking to the stand spot "
                                      "({:.1f}yd) before tagging", bot->GetName(), toStand);
                        bool const walked =
                            DcMoveTo(bot->GetMapId(), stand.GetPositionX(),
                                     stand.GetPositionY(), stand.GetPositionZ(),
                                     /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                                     /*exact_waypoint*/ false,
                                     MovementPriority::MOVEMENT_COMBAT);
                        if (walked || bot->isMoving() ||
                            IsWaitingForLastMove(MovementPriority::MOVEMENT_COMBAT))
                            return true;
                        // Couldn't move and not moving: the stand spot is wedged. Fall
                        // through to the ordinary tag rather than burning the whole
                        // leg budget standing still.
                        DC_PULL_DEBUG("[DC:{}] scripted-pull: stand spot unreachable "
                                      "({:.1f}yd) -> tagging from here", bot->GetName(),
                                      toStand);
                    }
                    else
                    {
                        // Arrived. Latch it — the leg is done, and on a body-pull row
                        // the tank is about to leave the spot on purpose.
                        pull.scriptedAtStandMs = now;
                        DC_PULL_DEBUG("[DC:{}] scripted-pull: on the stand spot "
                                      "({:.1f}yd) -> taking the tag from here",
                                      bot->GetName(), toStand);
                        // Drop the walk-in's MOVEMENT_COMBAT wait so it can't refuse
                        // the drag-back the tag is about to trigger (same stall the
                        // ranged-tag branches below clear).
                        DcMovement::StopBot(bot, DcMovement::Stop::Soft);
                        DcMovement::ClearMovementWait(bot);
                    }
                }
            }

            // The pull-back maneuver is exempt: its whole point is a long deliberate
            // haul out to a boss that may be swimming/roaming, and its own
            // distance-sized watchdog already bounds it. A SCRIPTED stage is exempt
            // too, and for the stronger version of the same reason: the plan names
            // the pack AND the spot, so "the pack has receded from where the plan was
            // measured" is not a thing that can happen — the plan was measured
            // against a hand-authored spawn cluster, not against a live snapshot.
            if (!pull.bossPullback && pull.scriptedStage < 0)
            {
                DungeonClearMath::ChaseVerdict const chase =
                    DcEngageGeometry::ChaseLeash(bot, context, trash);
                if (chase == DungeonClearMath::ChaseVerdict::Hold)
                {
                    DcMovement::StopBot(bot, DcMovement::Stop::Hold);
                    DcFaceIfNeeded(bot, trash);
                    DC_PULL_TRACE("[DC:{}] pull advancing: holding at the commit spot "
                                  "for {} to come back ({:.1f}yd)", bot->GetName(),
                                  trash->GetObjectGuid().ToString(), bot->GetExactDist(trash));
                    return true;
                }
                if (chase == DungeonClearMath::ChaseVerdict::GiveUp)
                {
                    pull.abortTarget = trash->GetObjectGuid();
                    DcSetPullPhase(context, DcPullPhase::Idle);
                    DC_PULL_INFO("[DC:{}] advanced-pull: {} kept walking away from the "
                                 "spot we planned the pull against ({:.1f}yd out) -> "
                                 "normal engage", bot->GetName(),
                                 trash->GetObjectGuid().ToString(), bot->GetExactDist(trash));
                    return false;
                }
            }

            // FORCE-AGGRO — per-encounter opt-in, keyed off the boss's own registry
            // row (BossPullback::forceAggroRange > 0), NOT off "this is a pull-back
            // pull". A pull-back boss whose row leaves the field at its 0 default
            // falls straight through to the ordinary tag machinery below, exactly
            // like every other boss in every other dungeon. Today one row opts in.
            //
            // Everything below this point — the ranged tag, the creep to the aggro
            // edge, the hold-for-aggro dwell — assumes the target will notice a tank
            // standing in its bubble. Ghaz'an is the boss that breaks the
            // assumption outright: his platform and its pipe are missing from the
            // navmesh, and he is often still in the water when the party is set, so
            // there is no reachable spot inside his aggro range at all. The tag leg
            // could only burn its watchdog before the run stalled — the reported
            // "tank waits for him until he times out".
            //
            // The shape is: walk out to the water's edge, get on his threat list
            // from there, run home. Three deliberate properties:
            //
            //  * The tank stops at the last DRY point on the route
            //    (DcDryStandoffToward). It goes as close as it can and no closer —
            //    it must never wade in, because the party ends up wherever the tank
            //    takes the fight.
            //  * The aggro is ONE-DIRECTIONAL (DcForceBossAggroOnTank): he attacks
            //    the tank, the tank never attacks him. Giving the tank a victim here
            //    hands it to stock MoveChase, which walks it into the lake with the
            //    party in tow — the observed failure.
            //  * The run home is issued on the SAME tick as the aggro, at
            //    MOVEMENT_COMBAT priority, so the retreat is already in flight
            //    before he arrives and does not wait on the engine flip.
            BossPullback const* const forceRow =
                pull.bossPullback
                    ? BossPullbackRegistry::Find(bot->GetMapId(), trash->GetEntry())
                    : nullptr;
            if (forceRow && forceRow->forceAggroRange > 0.0f)
            {
                float const toBoss = bot->GetExactDist(trash);
                if (toBoss > forceRow->forceAggroRange)
                {
                    // Out of range entirely — fall through to the walk-in below and
                    // force on a later tick.
                    DC_PULL_TRACE("[DC:{}] pull-back: boss at {:.1f}yd > force range "
                                  "{:.0f} -> closing first", bot->GetName(), toBoss,
                                  forceRow->forceAggroRange);
                }
                else
                {
                    // Walk out to the shoreline first, if there is still dry ground
                    // between us and him. `standoff` is the far end of the dry part
                    // of the route (less a back-off margin), so when he is on
                    // reachable ground it simply resolves to him and this
                    // degenerates to a normal walk-in.
                    //
                    // ALREADY WET is checked first and is a hard stop, not a
                    // preference. The stand-off aim point is only ever an aim point:
                    // a glide can overshoot it, the shoreline can move as the boss
                    // does, and the route can change under us. If any of that has
                    // already put the tank in the water then walking further is the
                    // exact failure this branch exists to avoid — pull from where we
                    // stand and get out. (Deliberately no retreat-then-pull dance:
                    // the aggro reaches from here, and the retreat is the very next
                    // thing that happens anyway.)
                    std::optional<Position> const standoff =
                        bot->IsInWater() ? std::nullopt : DcDryStandoffToward(bot, trash);
                    if (bot->IsInWater())
                    {
                        DC_PULL_DEBUG("[DC:{}] pull-back: tank is in the water at the "
                                      "approach — pulling from here rather than "
                                      "wading further", bot->GetName());
                    }
                    if (standoff.has_value() &&
                        bot->GetExactDist(&*standoff) > DC_PULL_CAMP_ARRIVE)
                    {
                        DC_PULL_TRACE("[DC:{}] pull-back: walking to the dry stand-off "
                                      "({:.1f},{:.1f},{:.1f}), {:.1f}yd off, before "
                                      "pulling", bot->GetName(),
                                      standoff->GetPositionX(), standoff->GetPositionY(),
                                      standoff->GetPositionZ(),
                                      bot->GetExactDist(&*standoff));
                        DcMoveTo(bot->GetMapId(), standoff->GetPositionX(),
                                 standoff->GetPositionY(), standoff->GetPositionZ(),
                                 /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                                 /*exact_waypoint*/ false,
                                 MovementPriority::MOVEMENT_COMBAT);
                        return true;
                    }

                    // At the water's edge (or already as close as the route allows):
                    // put ourselves on his threat list and immediately head home.
                    Creature* const bossCreature = trash->ToCreature();
                    if (bossCreature && DcForceBossAggroOnTank(bot, bossCreature))
                    {
                        pull.tagTarget = trash->GetObjectGuid();
                        // Drop the walk-out to the stand-off before turning round.
                        // It was a MOVEMENT_COMBAT MoveTo, so its LastMovement wait
                        // would refuse the equal-priority run-home below for the
                        // rest of its budget while the boss closed on a stationary
                        // tank — the same stall the maneuver's turn-around fixes.
                        DcMovement::ClearMovementWait(bot);
                        // Stamp the return leg here rather than waiting for the
                        // maneuver's first combat tick, so the turn-and-plant /
                        // watchdog arithmetic measures the real leg and the retreat
                        // starts THIS tick.
                        pull.returnLegStartDist = bot->GetExactDist(&camp);
                        pull.plantTicks = 0;
                        DcSetPullPhase(context, DcPullPhase::Returning);
                        DC_PULL_INFO("[DC:{}] pull-back: on {}'s threat list from the "
                                     "dry stand-off ({:.1f}yd out) -> running home to "
                                     "the anchor, {:.1f}yd", bot->GetName(),
                                     trash->GetObjectGuid().ToString(), toBoss,
                                     pull.returnLegStartDist);
                        DcMoveTo(bot->GetMapId(), camp.GetPositionX(), camp.GetPositionY(),
                                 camp.GetPositionZ(), /*idle*/ false, /*react*/ false,
                                 /*normal_only*/ false, /*exact_waypoint*/ false,
                                 MovementPriority::MOVEMENT_COMBAT);
                        return true;
                    }
                }
            }

            // Prefer a RANGED tag: pull from spell range so the tank tags and the
            // pack comes to it, instead of running into the middle of the pack.
            //
            // Resolved ONCE per tick and reused below, because the answer decides more
            // than whether to cast: a scripted stage's whole tag geometry — the clamp
            // onto the stand spot and the suppressed bystander detour — exists to
            // serve a tag the tank can take FROM that spot, and is meaningless when
            // there is nothing to take it with. See the fallback at the clamp.
            //
            // A BODY-PULL row (ScriptedPullStage::bodyPull) declines to resolve one at
            // all, which routes the rest of this branch down the no-opener path
            // deliberately rather than by accident: the clamp comes off, the bystander
            // detour comes back, and the walk-in below closes to the pack's aggro edge.
            // That path already existed and is already the one every openerless tank
            // takes; the row is choosing it rather than discovering it.
            bool const bodyPullStage = ScriptedStageIsBodyPull(bot, pull);
            std::optional<ResolvedPullSpell> const opener =
                (DC_TRY_PULL_SPELL && !bodyPullStage) ? ResolvePullSpell(botAI, bot)
                                                      : std::nullopt;

            ObjectGuid const lastPull = pull.tagTarget;
            if (DC_TRY_PULL_SPELL)
            {
                if (auto const& pick = opener)
                {
                    if (lastPull == trash->GetObjectGuid())
                    {
                        // Already tagged — hold and let aggro flip us to the
                        // combat-engine drag-back. The leg timeout above is the
                        // backstop if the tag somehow drew no aggro.
                        //
                        // The walk-in that got us here is over; drop its
                        // MOVEMENT_COMBAT wait so it can't refuse the NEXT
                        // MOVEMENT_COMBAT move. Two of those are queued up behind
                        // this dwell — the creep-inward step below, and the
                        // drag-back the moment aggro lands — and both would
                        // otherwise be silently held down for whatever is left of
                        // the approach budget while the pack chews on the tank.
                        //
                        // HARDPIN, AND NOT BECAUSE THE GLIDE NEEDS IT. This site spent
                        // a commit on Soft -> Hold "because Soft is not escort-aware",
                        // and that was the wrong axis twice over: DcMoveTo already
                        // calls ResolveEscortConflict on every issuance, so the walk-in
                        // killed Advance's glide with its own first leg and there is
                        // none left here — and what IS in flight is a POINT move
                        // (DcMoveTo -> MoveTo -> DoMovePoint -> MovePoint), which Hold
                        // does not treat any differently from Soft. Both early-out on
                        // bot->isMoving(), so on this branch they are the same code and
                        // that commit changed nothing.
                        //
                        // The stop that has to land here is the UNCONDITIONAL one. The
                        // two-to-three seconds between the tag and aggro flipping the
                        // phase to the drag-back are the window in which neither pull
                        // rung is live to re-issue anything, so a leg left running is a
                        // leg that carries the tank off the authored spot and into the
                        // room — the reported "tags, runs forward a few ticks, turns
                        // around and runs back". HardPin is the only strength that
                        // cancels a point-move whatever isMoving() says, which is the
                        // same reason DcPullBrake takes it.
                        DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
                        DcMovement::ClearMovementWait(bot);
                        DcFaceIfNeeded(bot, trash);
                        DC_PULL_TRACE("[DC:{}] pull advancing: tagged, holding for aggro "
                                      "({:.1f}yd to target)", bot->GetName(),
                                      bot->GetExactDist(trash));
                        return true;
                    }
                    float const d = bot->GetExactDist(trash);
                    if (d >= pick->minRange && d <= pick->maxRange &&
                        bot->IsWithinLOSInMap(trash))
                    {
                        bot->SetSelection(trash->GetObjectGuid());
                        if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, trash))
                            ServerFacade::instance().SetFacingTo(bot, trash);
                        if (botAI->CastSpell(pick->spellId, trash))
                        {
                            pull.tagTarget = trash->GetObjectGuid();
                            // Same as the "already tagged" branch above: the tag has
                            // landed, so the walk-in's MOVEMENT_COMBAT wait must not
                            // survive to refuse the drag-back.
                            //
                            // HARDPIN, for the reason spelled out on the dwell above.
                            // The tick before a tag lands is very often the one that
                            // issued a closing leg — the pack's nearest member sat at
                            // 30.6yd against a 30yd opener in tr-20260803-140306-1, so
                            // the leg stepped forward and tagged at 29.1yd on the next
                            // tick — and the next few seconds are exactly the window
                            // where neither pull rung is live to re-issue anything (the
                            // non-combat trigger has gone quiet on the new combat flag,
                            // the drag-back is not on the combat engine yet). Coasting
                            // anywhere in that window is coasting off the authored spot
                            // toward the pack, and Soft/Hold both let it, because both
                            // early-out on a bot whose movement flags a server-driven
                            // spline never sets.
                            DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
                            DcMovement::ClearMovementWait(bot);
                            DC_PULL_INFO("[DC:{}] advanced-pull: ranged tag spell {} at "
                                         "{:.1f}yd", bot->GetName(), pick->spellId, d);
                            return true;
                        }
                        // Cast failed (cooldown/silence) — fall through to body-tag.
                    }
                    // Not in range/LOS yet — fall through to close the gap; we'll
                    // re-enter and cast once within range and line of sight.
                }
            }

            // No ranged option (or out of range/LOS / cast failed): body-tag by
            // proximity. CRUCIAL: walk only to the EDGE of the pack's aggro bubble
            // and HOLD — let the mob notice and close the last few yards itself —
            // rather than sprinting (COMBAT priority is uninterruptible by combat
            // reflexes) all the way to the pack's centre. The old "MoveTo the mob's
            // exact position" arrived at the spawn before combat even registered, so
            // the drag-back (combat-engine only) took over far too late and an
            // un-trained tank face-pulled the whole pack and ate its opener. This
            // mirrors the ranged "tagged, hold for aggro" path above: stop just
            // inside aggro, let the pack come, then the maneuver drags it to camp.
            Creature* const trashCreature = trash->ToCreature();
            float const toTag = bot->GetExactDist(trash);

            // Distance at which the tank must stop to reliably tag. The core only
            // re-checks a pack's aggro on MOVEMENT (Creature::MoveInLineOfSight,
            // driven by relocation notifiers), and its notice test is the plain
            // CENTER-TO-CENTER distance vs Creature::GetAggroRange (CanStartAttack ->
            // IsWithinDistInMap with BOTH bounding radii excluded). So the tank has
            // to GLIDE to a point strictly INSIDE GetAggroRange — the moving approach
            // is what crosses the threshold and trips the notice. Stop ~2yd inside.
            //
            // The OLD formula ADDED both combat reaches to GetAggroRange, parking the
            // tank ~2yd OUTSIDE the real aggro bubble. Stationary there, no relocation
            // re-fired MoveInLineOfSight, the pack never noticed it, and the leg
            // watchdog timed out — the reported "advanced pull tag timed out" hang.
            float meleeReach = 0.0f;
            float tagStop = 0.0f;
            bool forceTag = false;   // close to body contact and actively swing
            if (trashCreature)
            {
                meleeReach = bot->GetCombatReach() + trash->GetCombatReach() + 1.0f;
                float const aggroRange = trashCreature->GetAggroRange(bot);

                // THE CREEP CLOCK IS "HOW LONG HAVE I BEEN CLOSING", NOT "HOW LONG
                // HAVE I BEEN IN THIS PHASE". On an ordinary pull those are the same
                // thing: Advancing begins at commit range, a couple of yards outside
                // aggro, so the walk-in starts on the phase's first tick. That is the
                // pull the creep was tuned against, and it behaves well.
                //
                // A SCRIPTED stage breaks the identity. Advancing there begins at the
                // CAMP and spends its first leg walking to the row's stand spot — up to
                // 77yd of it, ten seconds and more. By the time the tank turns to face
                // the pack the clock already reads far past the grace and the creep
                // term is bigger than the whole aggro radius, so the stop point lands
                // on the melee floor and "walk to the aggro edge and let the pack
                // notice" silently becomes "walk into the middle of the formation and
                // swing".
                //
                // Live (tr-20260803-232937-1 and every sibling in
                // tp-20260803-232932-1): the rotunda walk-in opened at `stop 4.3`
                // against a 20yd aggro radius — body contact — on every stage, and took
                // neighbouring formations with it every time. 10/10 runs stalled or
                // wiped in that room having cleared the two bosses before it.
                //
                // So on a scripted stage the clock starts when the tank REACHES the
                // stand spot: the moment its next step is the walk-in, which is the
                // same event the ordinary pull has always measured from.
                uint32 const closingSince =
                    (pull.scriptedStage >= 0 && pull.scriptedAtStandMs != 0)
                        ? pull.scriptedAtStandMs
                        : since;

                // AND ON A SCRIPTED STAGE THE CREEP IS BOUNDED — the guard rail behind
                // the clock. An ordinary pull may creep all the way to body contact
                // because over-creeping into one corridor pack costs nothing: the pack
                // it ends up touching is the pack it came for. A scripted stage is
                // standing in a room full of formations the plan has deliberately left
                // up, and the yards between the aggro edge and the pack's feet are
                // exactly the ones that reach them — the rotunda's rows are authored
                // with 12-24yd of margin at the edge and none at all at the spawn.
                float const creepFloor =
                    pull.scriptedStage >= 0
                        ? aggroRange - 2.0f - DC_PULL_SCRIPTED_CREEP_LIMIT
                        : 0.0f;

                tagStop = DungeonClearMath::PullTagStopDistance(
                    aggroRange, meleeReach,
                    now > closingSince ? now - closingSince : 0,
                    DC_PULL_TAG_CREEP_GRACE_MS, DC_PULL_TAG_CREEP_YARDS_PER_SEC,
                    creepFloor, forceTag);
            }

            if (trashCreature && toTag <= tagStop)
            {
                if (forceTag && !bot->IsInCombat())
                {
                    // Pack too high-level to ever notice us on its own: force the tag
                    // with a melee swing so combat starts and the maneuver drag-back
                    // (combat engine) takes over.
                    //
                    // The last two lines are what actually make that sentence true,
                    // and this site was missing both — unlike its two siblings below
                    // (the CC-abort engage and the pack-gathered plant), which carry
                    // the full pattern. Engine transitions in mod-playerbots are
                    // ACTION-DRIVEN, not derived from bot->IsInCombat(): UpdateAI only
                    // auto-switches to/from BOT_STATE_DEAD, and stock reaches the
                    // combat engine solely through AttackAction / PullActions calling
                    // ChangeEngine. Player::Attack alone just hands the bot a victim
                    // and lets the CORE swing it — no AI involvement — so without the
                    // flip the tank stays on the NON-combat engine, where no class
                    // rotation exists to run and DungeonClearPullManeuverTrigger (a
                    // COMBAT-engine trigger) can never fire. The tank would white-hit
                    // the pack in place with no abilities and no drag-back, which is
                    // exactly the "forced attack, but never a real rotation" shape.
                    //
                    // CurrentTarget matters just as much: flipping in with no current
                    // target lets the stock "drop target" action (relevance 99) read
                    // the target as invalid and bounce the bot straight back out — the
                    // 1-tick engine ping-pong DungeonClearMultiplier documents, whose
                    // suppressor is scoped to assisting FOLLOWERS and so does not
                    // cover the leader tank here.
                    //
                    // Narrow by construction: forceTag only when the pack's aggro
                    // range is at or below melee reach, and the branch is gated on
                    // !IsInCombat(), so it fires at most once per pull.
                    bot->SetSelection(trash->GetObjectGuid());
                    if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, trash))
                        ServerFacade::instance().SetFacingTo(bot, trash);
                    context->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Set(trash);
                    bot->Attack(trash, true);
                    botAI->ChangeEngine(BOT_STATE_COMBAT);
                    DC_PULL_TRACE("[DC:{}] pull advancing: force body-tag ({:.1f}yd)",
                                  bot->GetName(), toTag);
                    return true;
                }
                // Inside the aggro bubble — hold and let the pack close / flip the
                // engine to the maneuver drag-back. The leg timeout above is the
                // backstop if nothing aggros (resisted / non-hostile).
                //
                // This is the tick the walk-in ENDS on, and the walk-in was a
                // MOVEMENT_COMBAT MoveTo whose LastMovement wait is still running.
                // Leaving it up means the next MOVEMENT_COMBAT move — the creep
                // step, or the drag-back once aggro lands — is refused
                // (IsWaitingForLastMove yields to a strictly greater priority only)
                // for whatever is left of the approach budget, with the tank stood
                // in the pack taking hits. Neither stop strength zeroes it, so say so.
                //
                // AND THE STOP ITSELF HAS TO BE THE UNCONDITIONAL ONE, because this
                // branch is where "walk to the aggro edge and let the pack come" is
                // actually enforced and it was not enforcing anything. Stop::Soft is
                // `if (bot->isMoving()) StopMoving()`, and a bot moved by a server-side
                // spline carries none of the MovementInfo flags isMoving() reads — the
                // MOVEMENTFLAG_FORWARD that would set them is commented out in
                // mod-playerbots. So the guard was false and no stop was ever issued.
                //
                // Live (20:45:52, rotunda hall-patrol row): the aim point was 19.0yd
                // and this line logged "at aggro edge, hold for aggro" on eight
                // consecutive thinks while the range read 19.0, 16.9, 14.8, 12.7, 10.6,
                // 8.5, 6.5, 4.7 — fifteen yards of closing, held by nothing, ending at
                // body contact. HardPin is the strength that does not ask.
                DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
                DcMovement::ClearMovementWait(bot);
                DcFaceIfNeeded(bot, trash);
                DC_PULL_TRACE("[DC:{}] pull advancing: at aggro edge ({:.1f}yd, "
                              "hold for aggro)", bot->GetName(), toTag);
                return true;
            }

            // Aim for a point `tagStop` yards out from the pack on the tank's side, not
            // the pack's centre, so the run stops at the aggro edge. ONE leg, at speed:
            // the ring-and-crossing split that used to sit here is gone, and the note on
            // DC_PULL_SCRIPTED_CREEP_LIMIT says why.
            float tagX = trash->GetPositionX();
            float tagY = trash->GetPositionY();
            float tagZ = trash->GetPositionZ();
            if (trashCreature && toTag > 0.1f)
            {
                float const f = tagStop / toTag;
                tagX = trash->GetPositionX() + (bot->GetPositionX() - trash->GetPositionX()) * f;
                tagY = trash->GetPositionY() + (bot->GetPositionY() - trash->GetPositionY()) * f;
            }

            // SCRIPTED PULL: the walk-in may not leave the stand spot.
            //
            // Reaching here on a scripted stage means the tank is AT the spot and
            // the ranged tag did not fire — out of range, out of LOS, on cooldown,
            // or the class has no pull spell. The generic answer above is "walk to
            // the pack's aggro edge and creep inward until it notices", and in a
            // room like Selin's that answer is the failure: the aggro edge is
            // inside the room, past the wall the spot was chosen for, in sight of
            // the pack that has not been pulled yet.
            //
            // So clamp the aim point into a tight bubble around the spot. A yard or
            // two of slack still rescues the marginal case (a mob at 30.5yd with a
            // 30yd shield), and the tank cannot travel anywhere the plan did not
            // put it. If even the clamped point can't trip the tag, the leg
            // watchdog above hands the pack to the normal walk-in engage rather
            // than letting this creep forever — bounded, and loud in the log.
            //
            // UNLESS THERE IS NOTHING TO TAG WITH — then body-pull instead.
            //
            // The clamp's entire justification is that the tank has an opener it can
            // fire from the spot; the spot is chosen so that opener reaches this pack
            // and nothing else. A tank with no opener at all is not being held on a
            // vantage point, it is being held on an arbitrary patch of floor waiting
            // for something that can never happen — and it waits out the whole leg
            // budget before the watchdog gives the pack to the walk-in engage, which
            // then routes toward the BOSS and wakes the room on the way. Standing
            // still for 47 seconds and then blundering in is the worst of both.
            //
            // Live (tr-20260803-154419-13, -17): prot warriors, no class opener at 70
            // and a gun loaded with the wrong ammo, so no opener resolved. Reported as
            // "just stood there, did not pull, seemed to give up and ran into the room
            // aggroing everything" — which is precisely the sequence.
            //
            // So drop the clamp AND restore the generic bystander detour below: either
            // the plan's tag is executable and the authored lane governs, or it is not
            // and we fall back to the ordinary pull in full. A body pull is a worse
            // pull than the authored one and a far better one than none: the tank
            // still drags back to the row's camp, so the party fights at the prepared
            // position 83yd out instead of in the doorway.
            //
            // It is NOT free, and the geometry says so plainly. On Selin's east stage
            // the line from the stand spot to the nearest pack member passes 13.0yd
            // from Bruiser 96830 and 15.1yd from Skulker 96825 — the centre pair no
            // stage owns — against a ~19yd elite reach, at every stop distance. A body
            // pull there takes the centre pair too. That is the price of having no
            // opener, it is why the ranged fallback is worth keeping working, and it is
            // still the better of the two available outcomes.
            //
            // A row that ASKED for a body pull lands in the same place by design, and
            // the two are worth telling apart in the log: one is the plan working, the
            // other is a gear/level accident that the plan is coping with.
            bool const canTag = opener.has_value();
            if (pull.scriptedStage >= 0 && !canTag)
            {
                DC_PULL_TRACE("[DC:{}] scripted-pull: {} — walking to the pack's aggro "
                              "edge ({:.1f}yd to target)", bot->GetName(),
                              bodyPullStage
                                  ? "the row is authored as a BODY PULL"
                                  : "no opener resolves, so this falls back to a body "
                                    "pull instead of holding the stand spot",
                              toTag);
            }
            if (pull.scriptedStage >= 0 && canTag)
            {
                if (ScriptedPullStage const* stage =
                        ScriptedPullRegistry::Find(bot->GetMapId(), pull.scriptedStage))
                {
                    float const dx = tagX - stage->standX;
                    float const dy = tagY - stage->standY;
                    float const off = std::sqrt(dx * dx + dy * dy);
                    if (off > DC_SCRIPTED_PULL_CREEP)
                    {
                        float const f = DC_SCRIPTED_PULL_CREEP / off;
                        tagX = stage->standX + dx * f;
                        tagY = stage->standY + dy * f;
                        tagZ = stage->standZ;
                        DC_PULL_DEBUG("[DC:{}] scripted-pull: tag point clamped to "
                                      "{:.1f}yd of the stand spot (aggro edge was "
                                      "{:.1f}yd off it, target {:.1f}yd out)",
                                      bot->GetName(), DC_SCRIPTED_PULL_CREEP, off,
                                      toTag);
                    }
                }
            }

            // The tag leg is the one the tank walks ALONE and the one most worth
            // protecting: it goes out to a pack we have deliberately decided to
            // peel, and any bystander it wakes on the way arrives at the camp with
            // the pack we wanted. Detour around them; a failed snap falls through
            // to the direct line exactly as before.
            //
            // Not for a SCRIPTED stage: this leg starts at the row's stand spot, and
            // the whole point of that spot is that the short line from it to the pack
            // is the one line in the room that wakes nothing else. A generic orbit
            // computed off aggro spheres does not know about the walls the spot was
            // chosen for, and can only bend the tank off the authored lane.
            //
            // But a body-pulling stage (no opener — see the clamp above) has no
            // authored lane left to protect: it is walking the whole way in, straight
            // past the bystanders the spot existed to avoid. The generic detour is
            // exactly the machinery for that walk, and it validates its own
            // destination and falls through to the direct line when it cannot find
            // one, so restoring it here can only help. Same rule as the clamp: on
            // plan, the row governs; off plan, the ordinary pull governs, in full.
            if (std::optional<Position> avoid = (pull.scriptedStage >= 0 && canTag)
                    ? std::optional<Position>()
                    : DcEngageGeometry::EnRoutePackAvoidPoint(bot, context, trash))
            {
                tagX = avoid->GetPositionX();
                tagY = avoid->GetPositionY();
                tagZ = avoid->GetPositionZ();
            }
            // BAIL OUTRIGHT ONCE THE TANK IS FLAGGED.
            //
            // This branch normally goes silent the moment combat starts — the pull
            // trigger is a non-combat trigger — but "normally" is a race, and on the
            // tick it does get, the answer is not "take another step toward the pack
            // that just noticed you". Kill the inbound leg here so the drag-back
            // inherits a stopped tank rather than a coasting one.
            //
            // HardPin, not Hold: Hold early-outs on a bot that is not currently
            // ESCORT-gliding and whose isMoving() reads false, which is every bot on a
            // point-move walk-in. DcPullBrake already stops the tank at the flag itself
            // (earlier than any think, by construction), so this is a backstop for the
            // case the brake missed the 0->1 transition — a tank that entered combat
            // before the walk-in began. Doing it twice is free.
            if (pull.scriptedStage >= 0 && bot->IsInCombat())
            {
                DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
                DcMovement::ClearMovementWait(bot);
                DC_PULL_TRACE("[DC:{}] scripted-pull: aggro landed mid walk-in "
                              "({:.1f}yd to target) -> killing the inbound leg",
                              bot->GetName(), toTag);
                return true;
            }
            DC_PULL_TRACE("[DC:{}] pull advancing: closing to aggro edge ({:.1f}yd, "
                          "stop {:.1f}, leg {:.1f})", bot->GetName(), toTag, tagStop,
                          bot->GetExactDist2d(tagX, tagY));
            bool const moved = DcMoveTo(trash->GetMapId(), tagX, tagY, tagZ,
                                      /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                                      /*exact_waypoint*/ false, MovementPriority::MOVEMENT_COMBAT);
            if (moved || bot->isMoving() || IsWaitingForLastMove(MovementPriority::MOVEMENT_COMBAT))
                return true;

            // ARRIVAL IS NOT A WEDGE.
            //
            // "Refused and not moving" means a wedge only when there is somewhere left
            // to go. Standing ON the aim point produces the identical three signals:
            // DcMoveTo dedupes the destination and refuses, and the bot is not moving
            // because it has got there. Read as a wedge, that hands the pack away one
            // tick after a perfectly successful walk.
            //
            // Which is not an edge case on a SCRIPTED stage — it is the default. The
            // tag point is clamped into a bubble of DC_SCRIPTED_PULL_CREEP around the
            // row's stand spot, and that constant is 0.0, so the aim point IS the spot
            // the tank has just finished walking to. Any stage whose ranged tag does
            // not fire (no ranged weapon, out of range, LOS, cooldown) therefore walks
            // to the spot, aims at its own feet, and aborts.
            //
            // Live (tr-20260803-144046-2): a warrior tank with no ranged tag reached
            // the east stand spot, logged "tag point clamped to 0.0yd of the stand
            // spot" five ticks running and then
            //   move REFUSED and not moving -> IsDuplicateMove (... at 0.0yd)
            //   advanced-pull: tag navmesh-wedged (26.6yd to target)
            // with the target 26.6yd out and the tank exactly where the plan wanted it.
            //
            // So hold instead. There is nothing to escalate on this tick — the tank is
            // in position and the pack simply has not noticed yet — and the leg
            // watchdog above is already the right bound for "in position and nothing
            // happened", handing the pack to the walk-in engage on a clock rather than
            // on a movement artefact.
            float const toAim = bot->GetExactDist2d(tagX, tagY);
            if (toAim <= DC_PULL_CAMP_ARRIVE)
            {
                DC_PULL_TRACE("[DC:{}] pull advancing: on the aim point ({:.1f}yd) — "
                              "holding for aggro, not a wedge", bot->GetName(), toAim);
                return true;
            }

            // Couldn't move, not moving, and not there: navmesh wedge. Abort to normal
            // engage. Unlatch any scripted stage on the way out — see
            // AbandonScriptedStage for what leaving it latched costs.
            pull.abortTarget = trash->GetObjectGuid();
            AbandonScriptedStage(pull);
            DcSetPullPhase(context, DcPullPhase::Idle);
            DC_PULL_INFO("[DC:{}] advanced-pull: tag navmesh-wedged ({:.1f}yd to "
                         "target, {:.1f}yd short of the aim point) -> normal engage",
                         bot->GetName(), bot->GetExactDist(trash), toAim);
            return false;
        }

        case DcPullPhase::Returning:
        {
            // Reached here only out of combat (the maneuver runs in combat): the
            // pack leashed / evaded mid-return. Release the party and reset.
            //
            // A PULL-BACK retreat is the one case where "the tank is out of combat"
            // does NOT mean that. Its Returning leg is entered by the tag branch on
            // the same tick it puts the tank on the boss's threat list, and the
            // tank's own combat flag can lag that by a tick — and the boss is a long
            // way off, so nothing is hitting the tank yet either. Releasing the
            // party on that tick would dump them out of the camp hold and send them
            // at an inbound boss, which is the beeline this whole maneuver exists to
            // stop. Keep running home while the BOSS is still engaged; only fall
            // through to the release once he has genuinely dropped combat (a real
            // evade), and let the leg watchdog bound it either way.
            if (pull.bossPullback)
            {
                Unit* const pulled = pull.pullTarget.IsEmpty()
                    ? nullptr : ObjectAccessor::GetUnit(*bot, pull.pullTarget);
                if (pulled && pulled->IsAlive() && pulled->IsInCombat())
                {
                    DcMoveTo(bot->GetMapId(), camp.GetPositionX(), camp.GetPositionY(),
                             camp.GetPositionZ(), /*idle*/ false, /*react*/ false,
                             /*normal_only*/ false, /*exact_waypoint*/ false,
                             MovementPriority::MOVEMENT_COMBAT);
                    DC_PULL_TRACE("[DC:{}] pull-back: retreating to the anchor "
                                  "({:.1f}yd) — boss engaged, combat flag not on us "
                                  "yet", bot->GetName(), bot->GetExactDist(&camp));
                    return true;
                }
            }
            DcSetPullPhase(context, DcPullPhase::Engage);
            DC_PULL_INFO("[DC:{}] advanced-pull: out of combat mid-return (pack "
                         "leashed/evaded) -> release party", bot->GetName());
            return false;
        }

        case DcPullPhase::Engage:
        default:
        {
            EndCampFight(bot, context, pull);
            return false;
        }
    }
}

namespace
{
    // Tear the camp fight down and ready the next pull. Extracted because it now has
    // TWO callers that reach it from opposite sides of the combat flag:
    //
    //   * DungeonClearPullAction (the non-combat driver) for the ordinary case — the
    //     tank is out of combat, so the fight is plainly over.
    //   * DungeonClearPullManeuverAction, for a scripted stage whose pack is dead
    //     while the tank is STILL FLAGGED. That path exists because this cleanup used
    //     to be reachable only via the first, and the first is gated on
    //     !IsInCombat() — see the maneuver's Engage branch for what that cost.
    void EndCampFight(Player* bot, AiObjectContext* context, DcPullContext& pull)
    {
            pull.abortTarget = ObjectGuid::Empty;

            // Engage-fizzle latch. The fight "ended" (tank out of combat) with
            // the pulled pack still alive and idle: the drag never delivered it
            // — a caster/planted straggler held its ground and evaded home the
            // moment the tank broke LOS at camp. Re-pulling repeats the exact
            // same fizzle (the tank bounces forward and back while the party
            // stands passive at camp, never entering combat), so after
            // DC_PULL_FIZZLE_MAX consecutive fizzles hand the pack to the
            // normal walk-in engage: blocking-trash exempts the abortTarget
            // from pull-mode standdown, the tank fights it where it stands, and
            // the leader-fight assist drives the party in. A pack that died or
            // is still being fought by the party resets the latch.
            Unit* pulled = pull.pullTarget.IsEmpty()
                ? nullptr : ObjectAccessor::GetUnit(*bot, pull.pullTarget);
            bool const aliveIdle = pulled && pulled->IsAlive() && !pulled->IsInCombat();
            // sameTarget compares against the OLD latch before we re-stamp it.
            bool const sameTarget = aliveIdle && pull.fizzleTarget == pull.pullTarget;
            bool const handoff = DungeonClearMath::ShouldHandoffFizzledPull(
                aliveIdle, sameTarget, DC_PULL_FIZZLE_MAX, pull.fizzleCount);
            if (aliveIdle)
            {
                if (!sameTarget)
                    pull.fizzleTarget = pull.pullTarget;
                // Health and distance discriminate WHY it fizzled: 100% health
                // means the pack never engaged (or fully reset behind the LOS
                // corner); reduced health means it fought and silently dropped
                // combat when its target became unreachable.
                if (handoff)
                {
                    pull.abortTarget = pull.fizzleTarget;
                    DC_PULL_INFO("[DC:{}] advanced-pull: pull of {} fizzled {}x "
                                 "(alive and idle after the camp fight, {:.0f}% hp, "
                                 "{:.1f}yd) -> handing to normal engage",
                                 bot->GetName(), pull.fizzleTarget.ToString(),
                                 pull.fizzleCount, pulled->GetHealthPct(),
                                 bot->GetExactDist(pulled));
                }
                else
                    DC_PULL_DEBUG("[DC:{}] advanced-pull: pull of {} fizzled "
                                  "(alive and idle after the camp fight, {:.0f}% hp, "
                                  "{:.1f}yd, {}/{})", bot->GetName(),
                                  pull.pullTarget.ToString(), pulled->GetHealthPct(),
                                  bot->GetExactDist(pulled), pull.fizzleCount,
                                  DC_PULL_FIZZLE_MAX);
            }
            else
                pull.fizzleTarget = ObjectGuid::Empty;  // count cleared by the kernel
            pull.pullTarget = ObjectGuid::Empty;

            // End the pull-back session with the maneuver that opened it. The flag
            // is otherwise cleared only by Reset() — which runs on run teardown, not
            // between pulls — so leaving it set here would keep the WHOLE pull
            // pipeline force-enabled for the rest of the run: every later pack would
            // be camp-pulled even with the player's pull setting Off, and the party
            // would stay camp-held at Ghaz'an's dead anchor. Per-pull flag, per-pull
            // lifetime.
            pull.bossPullback = false;

            // Same per-pull lifetime for a SCRIPTED stage, and the same consequence
            // if it were skipped: the stage pins the pull target (see
            // ScriptedPullRegistry::SelectOrder) and force-enables the pipeline, so a
            // stage left latched here would hold the party at a finished stage's camp
            // and never let the NEXT stage — or the boss — come due. Clearing it is
            // what advances the plan: the next Idle tick re-derives the due stage,
            // finds this pack's volume empty, and arms the following one.
            AbandonScriptedStage(pull);

            DcSetPullPhase(context, DcPullPhase::Idle);
            DC_PULL_DEBUG("[DC:{}] advanced-pull: camp fight done -> idle, ready for "
                          "next pull", bot->GetName());
    }

    // Is any live member of THIS STAGE's pack still fighting the party?
    //
    // Entry-only, deliberately: IsStageTarget also requires the mob to be inside the
    // stage's own volume, and by this point the whole point of the maneuver is that it
    // has been dragged OUT of it. Position cannot answer "is this pack still my
    // problem"; being on somebody's threat list can.
    //
    // Scanned across the PARTY, not just the tank: a straggler that peeled onto a
    // follower mid-drag is still this pack, and calling the stage done while a rogue
    // is being chewed on would hand the tank the next pack on top of it.
    //
    // `hpSum`, when asked for, is the same scan's PROGRESS SIGNATURE: the summed
    // health percent of those attackers, deduplicated by GUID (one mob on three
    // party members' lists is one mob). It is what gives the Engage phase a clock —
    // see DC_SCRIPTED_PULL_ENGAGE_STALL_MS. Summed rather than counted because a
    // count only moves when something DIES, and a six-mob pack can be fought for a
    // long time between deaths; health moves every swing.
    bool ScriptedPackStillFighting(Player* bot, ScriptedPullStage const& stage,
                                   float* hpSum)
    {
        GuidSet seen;
        float sum = 0.0f;
        bool any = false;

        auto engaged = [&](Player const* p)
        {
            for (Unit* a : p->getAttackers())
            {
                if (!a || !a->IsAlive() ||
                    !ScriptedPullRegistry::IsPackEntry(stage, a->GetEntry()))
                    continue;
                any = true;
                if (seen.insert(a->GetObjectGuid()).second)
                    sum += a->GetHealthPct();
            }
        };

        engaged(bot);
        if (Group* group = bot->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || member == bot || member->isDead())
                    continue;
                if (member->GetMapId() != bot->GetMapId())
                    continue;
                engaged(member);
            }
        }

        if (hpSum)
            *hpSum = sum;
        return any;
    }

    // The NEAREST live member of this stage's pack standing outside `leash` of the
    // camp, or nullptr when every live member is inside it.
    //
    // Same party-wide attacker scan as ScriptedPackStillFighting and for the same
    // reason: a mob with the party on its threat list is this pack's business wherever
    // it happens to be standing. Nearest rather than farthest because the camp walks
    // one bounded step at a time — the cheapest mob to bring into reach is the one
    // worth aiming the step at, and anything behind it is reached by the next step.
    //
    // Returned as a live pointer for use on THIS tick only (the caller measures two
    // distances off it and drops it); nothing stores it.
    Unit* ScriptedPackNearestStandoff(Player* bot, ScriptedPullStage const& stage,
                                      Position const& camp, float leash)
    {
        GuidSet seen;
        Unit* nearest = nullptr;
        float nearestDist = 0.0f;

        auto scan = [&](Player const* p)
        {
            for (Unit* a : p->getAttackers())
            {
                if (!a || !a->IsAlive() ||
                    !ScriptedPullRegistry::IsPackEntry(stage, a->GetEntry()))
                    continue;
                if (!seen.insert(a->GetObjectGuid()).second)
                    continue;
                float const d = a->GetExactDist(&camp);
                if (d <= leash)
                    continue;
                if (!nearest || d < nearestDist)
                {
                    nearest = a;
                    nearestDist = d;
                }
            }
        };

        scan(bot);
        if (Group* group = bot->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || member == bot || member->isDead())
                    continue;
                if (member->GetMapId() != bot->GetMapId())
                    continue;
                scan(member);
            }
        }
        return nearest;
    }
}

bool DungeonClearPullManeuverAction::Execute(Event& /*event*/)
{
    DcPullContext& pull = context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    Position& camp = pull.camp;
    uint32 const now = getMSTime();
    DcPullPhase const phase = pull.phase;

    // Keep the tank daze-proof for the whole drag. Immunity (set with pull mode)
    // should stop Daze landing at all; strip it here too as a backstop so a
    // retreat is never slowed to a crawl by a hit from behind.
    bot->RemoveAurasDueToSpell(1604);

    // A druid tank must take the run-home hits in bear form, not caster form.
    // Shapeshift is instant and not interrupted by the drag-back run, so refresh
    // it every tick of the maneuver (no-op once shifted / for non-druids).
    DcFollowerLifecycle::EnsureTankBearForm(bot);

    // SCRIPTED PULL — CAMP LEASH. For the whole camp fight, keep the tank on the
    // authored camp.
    //
    // Everything before this point got the party OUT of the room; nothing so far
    // keeps them out. Once the maneuver releases, the fight belongs to stock
    // combat, and stock combat's answer to a victim 25yd away is MoveChase — so
    // the tank walks back through the doorway it just retreated through, and the
    // pack that has not been pulled yet is right there. That is the reported
    // "natural combat still pulled the tank into the room".
    //
    // Deliberately a LATCH and two radii, not a per-tick threshold. Correcting
    // only while outside the leash and releasing the moment the tank crosses back
    // in would hand the tick straight back to the chase at the boundary and
    // produce the in-out shuffle; the latch runs the recall all the way home.
    //
    // And deliberately NOT true every tick — an action that always claims the tick
    // starves the combat engine outright (no target, no swings, no rotation), so
    // an in-position tank yields and simply fights.
    // FINISH THE STAGE EVEN IF THE TANK IS STILL FLAGGED.
    //
    // The camp-fight teardown (EndCampFight) lives in the NON-COMBAT pull driver,
    // whose trigger returns early on `!bot->IsInCombat()`. So for the whole time the
    // tank holds a combat flag from ANY source the stage cannot retire — and a
    // scripted stage that cannot retire is not merely slow, it wedges the run: the
    // stage pins the pull target, force-enables the pipeline, and keeps the party
    // camp-held, so no later pack and no boss can ever come due.
    //
    // Live (tr-20260803-133734-1): the tank crept off the east stand spot far enough
    // to body-pull the two centre mobs, dragged them home and killed them, and then
    // never left combat — "camp fight done" appears ZERO times in the run. The stage
    // stayed latched for the remaining two and a half minutes while the camp leash
    // hauled the tank back from 42.8yd over and over. A phantom flag does it too
    // (see DcCombatFlag): flagged with nothing engaged is a state the rest of the run
    // deliberately drives through, and this teardown was the one rung that could not.
    //
    // So the maneuver — which by definition runs WHILE in combat — retires the stage
    // itself the moment this stage's pack is off the party. Whatever else has the tank
    // is a normal fight and the ordinary rungs handle it; what matters is that the
    // PLAN is complete and the pipeline is free again.
    if (pull.scriptedStage >= 0 && phase == DcPullPhase::Engage)
    {
        if (ScriptedPullStage const* const stage =
                ScriptedPullRegistry::Find(bot->GetMapId(), pull.scriptedStage))
        {
            float hpSum = 0.0f;
            if (!ScriptedPackStillFighting(bot, *stage, &hpSum))
            {
                DC_PULL_INFO("[DC:{}] scripted-pull: stage [{}] pack is done while the "
                             "tank is still flagged -> retiring the stage anyway "
                             "(combat from elsewhere is not this plan's business)",
                             bot->GetName(), stage->name ? stage->name : "?");
                EndCampFight(bot, context, pull);
                return false;
            }

            // AND THE SAME EXIT WHEN THE PREDICATE ABOVE IS SIMPLY WRONG.
            //
            // "Still fighting" is an attacker-list read, and an attacker list can say
            // yes while nothing is happening: a pack member that is alive, has the
            // party on its threat, and can neither reach it nor be reached stays on
            // that list indefinitely. The party is camp-held and the tank is leashed
            // to the camp, so neither side closes and neither side lets go — a
            // standoff both halves of the maneuver are actively maintaining.
            //
            // Nothing else can break it. Every other rung that might have (the
            // non-combat teardown, the advance rung, the approach gate) is either
            // gated on the combat flag or stood down by the latched stage. Which is
            // why this is the phase's own clock rather than a watchdog bolted on
            // outside it.
            //
            // The health signature is the arbiter — see
            // DC_SCRIPTED_PULL_ENGAGE_STALL_MS for why progress and not a wall clock.
            // Retire through EndCampFight, exactly as the rung above does: the plan is
            // over either way, and whatever combat is genuinely left belongs to the
            // ordinary rungs.
            if (pull.scriptedEngageSince == 0 ||
                std::fabs(hpSum - pull.scriptedEngageHp) > 0.01f)
            {
                pull.scriptedEngageHp = hpSum;
                pull.scriptedEngageSince = now;
            }
            else if (ScriptedPullEngageStalled(pull.scriptedEngageSince, now))
            {
                DC_PULL_INFO("[DC:{}] scripted-pull: stage [{}] camp fight has not "
                             "moved the pack's health ({:.0f}%) in {}s — it has "
                             "stopped happening, not slowed down -> retiring the "
                             "stage and handing what is left to the ordinary rungs",
                             bot->GetName(), stage->name ? stage->name : "?", hpSum,
                             (now - pull.scriptedEngageSince) / 1000);
                EndCampFight(bot, context, pull);
                return false;
            }
            // THE CAMP WALKS AT WHAT WILL NOT COME TO IT.
            //
            // Runs in front of the retirement above, on the same evidence and a much
            // shorter clock (DC_SCRIPTED_PULL_STANDOFF_MS). The pack's health has
            // stopped moving AND a live member of it is standing outside the tank's
            // leash: that is the SmartAI range-mode caster this camp cannot reach —
            // a Sunblade Magister holds station 35yd from its victim and will neither
            // close nor back off, against a 14yd tank leash and a 12yd follower leash.
            // See DC_SCRIPTED_PULL_CAMP_STEP for the measurement and for why the answer
            // is to move the camp rather than to lengthen either leash.
            //
            // One bounded step per stall, along camp -> the row's own stand spot and no
            // further: that spot is the only forward ground the plan has cleared
            // against the packs it has not pulled, and the tank already walked to it
            // once this pull to take the tag. Two guards keep a step honest — it must
            // actually CLOSE on the mob that is standing off (otherwise the mob is
            // behind us and the stand spot is the wrong way), and the landing point is
            // snapped through the navmesh exactly as ComputeCampSlot does, so a
            // straight-line interpolation across a neck can never park the party in a
            // wall.
            //
            // Re-arms the progress clock, so the retirement above can only fire once
            // the camp has run out of segment to give.
            else if (ScriptedPullStandoffStalled(pull.scriptedEngageSince, now))
            {
                Unit* const held = ScriptedPackNearestStandoff(
                    bot, *stage, camp, DC_SCRIPTED_PULL_LEASH);
                Position const spot(stage->standX, stage->standY, stage->standZ);
                float const toSpot = camp.GetExactDist(&spot);
                if (held && toSpot > DC_PULL_CAMP_ARRIVE)
                {
                    float const t = std::min(DC_SCRIPTED_PULL_CAMP_STEP, toSpot) / toSpot;
                    float const nx = camp.GetPositionX() +
                                     (spot.GetPositionX() - camp.GetPositionX()) * t;
                    float const ny = camp.GetPositionY() +
                                     (spot.GetPositionY() - camp.GetPositionY()) * t;
                    float const nz = camp.GetPositionZ() +
                                     (spot.GetPositionZ() - camp.GetPositionZ()) * t;

                    PathGenerator gen(bot);
                    gen.CalculatePath(nx, ny, nz, /*forceDest*/ false);
                    if (!(gen.GetPathType() & (PATHFIND_NOPATH | PATHFIND_FARFROMPOLY)))
                    {
                        G3D::Vector3 const end = gen.GetActualEndPosition();
                        Position const next(end.x, end.y, end.z, camp.GetOrientation());
                        float const before = held->GetExactDist(&camp);
                        float const after = held->GetExactDist(&next);
                        float const stepped = camp.GetExactDist(&next);
                        if (after < before - 1.0f)
                        {
                            pull.PublishCamp(next, now);
                            // The recall in flight was aimed at the OLD camp; drop the
                            // latch so the leash re-arms against the new one instead of
                            // marching the tank back to a point that has moved.
                            pull.scriptedRecall = false;
                            pull.scriptedRecallBest = 0.0f;
                            pull.scriptedEngageSince = now;
                            DC_PULL_INFO("[DC:{}] scripted-pull: stage [{}] — {} is "
                                         "holding {:.1f}yd off the camp and will not "
                                         "close, and the pack has taken nothing in {}s "
                                         "-> walked the camp {:.1f}yd up its stand-spot "
                                         "line ({:.1f}yd of it left), gap now {:.1f}yd",
                                         bot->GetName(), stage->name ? stage->name : "?",
                                         held->GetObjectGuid().ToString(), before,
                                         DC_SCRIPTED_PULL_STANDOFF_MS / 1000, stepped,
                                         next.GetExactDist(&spot), after);
                        }
                    }
                }
            }
        }
    }

    if (pull.scriptedStage >= 0 && phase == DcPullPhase::Engage)
    {
        float const toCamp = bot->GetExactDist(&camp);
        // A TANK STANDING IN FIRE IS ALLOWED TO BE OFF THE CAMP. The leash and a
        // ground-effect step-out are both right and they want different places, so
        // the leash is the one that gives — see DcInGroundEffect. It suppresses the
        // ARM (below) and cancels an armed recall (further down); the predicate goes
        // false the instant the tank is clear, so the leash re-arms by itself if the
        // step-out left it genuinely out of position.
        bool const inGroundEffect = DcInGroundEffect(context);
        if (!pull.scriptedRecall && toCamp > DC_SCRIPTED_PULL_LEASH && !inGroundEffect)
        {
            pull.scriptedRecall = true;
            // Arm the ground ratchet at the distance the leash tripped at (see the
            // losing-ground check below).
            pull.scriptedRecallBest = toCamp;
            // Kill the chase generator and its movement wait before re-pointing:
            // a plain MoveTo layered under a live MoveChase is how the drag-back
            // used to lose to an in-flight spline.
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
            DcMovement::ClearMovementWait(bot);
            DC_PULL_INFO("[DC:{}] scripted-pull: tank strayed {:.1f}yd from the camp "
                         "mid-fight (leash {:.0f}) -> recalling",
                         bot->GetName(), toCamp, DC_SCRIPTED_PULL_LEASH);
        }
        if (pull.scriptedRecall)
        {
            // Home is a BAND, not the anchor point — see DC_SCRIPTED_PULL_RECALL_HOME.
            // Walking the last ten yards back onto the exact coordinate is what put
            // the tank back inside whatever pushed it off in the first place.
            if (toCamp <= DC_SCRIPTED_PULL_RECALL_HOME)
            {
                pull.scriptedRecall = false;
                pull.scriptedRecallBest = 0.0f;
                DcMovement::StopBot(bot, DcMovement::Stop::Soft);
                DC_PULL_DEBUG("[DC:{}] scripted-pull: back on the camp ({:.1f}yd) -> "
                              "fighting", bot->GetName(), toCamp);
                return false;
            }

            // Walked into a ground effect on the way home (or the effect landed on
            // us): drop the recall and hand the tick to the step-out. Cancelling
            // rather than pausing keeps the losing-ground ratchet honest — there is
            // no leg in flight to measure — and the leash re-arms on its own once we
            // are clear and still outside it.
            if (inGroundEffect)
            {
                pull.scriptedRecall = false;
                pull.scriptedRecallBest = 0.0f;
                DC_PULL_INFO("[DC:{}] scripted-pull: recall cancelled at {:.1f}yd — "
                             "standing in a ground effect, so the step-out owns the "
                             "tick; the leash re-arms once we are clear",
                             bot->GetName(), toCamp);
                return false;
            }

            // THE RECALL MAY NOT LOSE GROUND. Same ratchet as the drag-back and the
            // follower hold, and this was the last of the three to get it — which is
            // why the tank still ended up in the room.
            //
            // Live (tr-20260803-125341-1, the first pull): the leash tripped at
            // 13.2yd and the very next line was
            //   move REFUSED ... IsMovingAllowed=false (... a CONTROLLED motion slot ...)
            // because the release to Engage one second earlier had handed the tank to
            // stock combat and MoveChase had it. After that single tick the recall
            // went SILENT for twenty-one seconds — not refused, DEDUPED. DcMoveTo
            // dedupes on destination, the camp never changes, so every later tick
            // reported "already going there" and issued nothing while the chase drove
            // the tank to X~216, into Selin's room, and eventually back out. Twenty-one
            // seconds of an action returning true and doing nothing.
            //
            // Neither existing guard can see that. The standing-still backstop below
            // requires !isMoving(), and the bot was moving beautifully — outward. And
            // a dedupe is not a refusal, so nothing was even logged. Only DISTANCE can
            // tell us the leg was taken away, so ratchet on the best-so-far: give up
            // more than DC_SCRIPTED_PULL_LOSE_GROUND against it and hard-pin, which
            // kills the chase generator so the next MoveTo genuinely issues.
            //
            // Re-arm on the trip rather than holding the original best, so a long haul
            // home isn't re-cancelled every tick while it legitimately works its way
            // back.
            if (ScriptedPullLostGround(pull.scriptedRecallBest, toCamp))
            {
                DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
                DcMovement::ClearMovementWait(bot);
                DC_PULL_INFO("[DC:{}] scripted-pull: recall lost ground to camp "
                             "({:.1f}yd vs best {:.1f}) — something else is driving "
                             "the tank -> cancelled it and re-issuing the walk home",
                             bot->GetName(), toCamp, pull.scriptedRecallBest);
                pull.scriptedRecallBest = toCamp;
            }
            else if (toCamp < pull.scriptedRecallBest || pull.scriptedRecallBest <= 0.0f)
                pull.scriptedRecallBest = toCamp;

            bool const moved =
                DcMoveTo(bot->GetMapId(), camp.GetPositionX(), camp.GetPositionY(),
                         camp.GetPositionZ(), /*idle*/ false, /*react*/ false,
                         /*normal_only*/ false, /*exact_waypoint*/ false,
                         MovementPriority::MOVEMENT_COMBAT);
            // Same starvation the drag-back guards against, and the recall needs it
            // just as badly: the wait is cleared once when the leash trips, but a
            // combat mover that grabs the tank a moment later records a NEW
            // equal-priority wait, and IsWaitingForLastMove only yields to a
            // strictly greater one. Every later recall tick is then refused
            // silently. Live: a recall at 22:57:19 was starved for thirty-one
            // seconds, logging "move REFUSED and not moving ... prio=3" the whole
            // way. Break the wait whenever we are refused while standing still.
            if (!moved && !bot->isMoving() &&
                IsWaitingForLastMove(MovementPriority::MOVEMENT_COMBAT))
            {
                DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
                DcMovement::ClearMovementWait(bot);
                DC_PULL_DEBUG("[DC:{}] scripted-pull: recall refused while standing "
                              "at {:.1f}yd from camp -> cleared the stale movement "
                              "wait", bot->GetName(), toCamp);
            }
            return true;
        }
        return false;   // on the camp — the rotation owns the tick
    }

    // First combat tick of the pull: aggro confirmed, turn around for camp.
    // Forming counts too — combat can be taken while the tank holds at the commit
    // spot waiting for the party to set (the pack wandered into it / a patrol).
    // Idle counts as well: an unplanned aggro while merely scouting toward the
    // next pack must ALSO retreat to the held party rather than fight in place.
    // Either way we drag back to the camp and release the party there rather than
    // letting the tank solo in place.
    if (phase == DcPullPhase::Advancing || phase == DcPullPhase::Forming ||
        phase == DcPullPhase::Idle)
    {
        // Unplanned aggro while scouting (Idle): stamp a FRESH safe camp just
        // behind the tank (away from the aggressor) and drag the pack THERE. The
        // party then converges on the NEW camp via hold-at-camp — we deliberately
        // do NOT haul the pack all the way back to the stale camp the party still
        // sits at. The fight happens on safe ground near where the aggro started
        // and the party comes up to it, even though it isn't there yet.
        // Forming/Advancing already carry the freshly-committed pull camp.
        if (phase == DcPullPhase::Idle)
        {
            Unit* aggressor = bot->GetVictim();
            for (Unit* a : bot->getAttackers())
            {
                if (!a || !a->IsAlive())
                    continue;
                if (!aggressor ||
                    bot->GetExactDist2d(a) < bot->GetExactDist2d(aggressor))
                    aggressor = a;
            }
            if (aggressor)
            {
                // Same fizzle bookkeeping as a planned pull: if this aggressor
                // evades home mid-drag over and over, the Engage cleanup must
                // see it and eventually hand it to the walk-in engage.
                pull.pullTarget = aggressor->GetObjectGuid();

                float setback = 0.0f, safeRadius = 0.0f, maxDrag = 0.0f;
                DcResolveCampParams(bot, context, setback, safeRadius, maxDrag);
                float clr = 0.0f;
                float drag = 0.0f;
                if (std::optional<Position> fresh = DcPullPlanner::ComputeSafeCamp(
                        botAI, aggressor, setback, safeRadius, maxDrag, clr, drag))
                {
                    pull.PublishCamp(*fresh, now);
                    DC_PULL_INFO("[DC:{}] advanced-pull: unplanned aggro while scouting "
                                 "-> fresh camp ({:.1f},{:.1f},{:.1f}) drag {:.1f}yd, "
                                 "party converges", bot->GetName(),
                                 camp.GetPositionX(), camp.GetPositionY(),
                                 camp.GetPositionZ(), drag);
                }
            }
        }

        // If the camp was never stamped and none could be computed, fall back to
        // fighting in place rather than dragging the pack to the map origin.
        if (!pull.HasCamp())
            pull.PublishCamp(Position(bot->GetPositionX(), bot->GetPositionY(),
                                      bot->GetPositionZ()),
                             now);

        // KILL THE INBOUND LEG BEFORE TURNING AROUND. This is the difference
        // between "aggro -> turn -> run" and the sluggish "aggro -> keep walking
        // into the pack -> stand there -> turn -> run" the player sees, and it is
        // not cosmetic: every one of those wasted ticks is free swings on the tank.
        //
        // Two distinct things are still in flight at this instant, both left over
        // from the tag leg, and both have to go:
        //
        //  1. The point-move itself. The tag leg was issued as a MOVEMENT_COMBAT
        //     MoveTo toward the aggro edge. Aggro normally lands at the very START
        //     of that leg — PullCommitRange deliberately Forms the tank a hair
        //     outside the pack's real bubble, so the first few yards of the walk-in
        //     are what trip it — and the instant the tank is in combat the
        //     non-combat pull trigger goes silent. Nobody cancels the move, so the
        //     MotionMaster happily finishes carrying the tank INTO the pack while
        //     the engine flip is happening.
        //
        //  2. The LastMovement wait. That same MoveTo recorded a wait sized to the
        //     whole leg's travel time at MOVEMENT_COMBAT priority.
        //     MovementAction::IsWaitingForLastMove refuses a new move whose
        //     priority is not STRICTLY GREATER than the recorded one, so the
        //     run-home below — also MOVEMENT_COMBAT — is silently refused for the
        //     remainder of that budget. The maneuver still returns true (it owns
        //     the tick), so the tank just stands and eats the pack until the stale
        //     clock runs out.
        //
        // Hold clears (1): it kills a coasting glide, and no-ops when the tank is
        // already parked at the aggro edge. ClearMovementWait clears (2)
        // unconditionally — which is the half Hold's standing-still early-out would
        // otherwise skip, and the half that actually blocks the retreat. Deliberately
        // NOT StopBot(HardPin): this is a leg being REPLACED, not halted, and the
        // run-home issued at the bottom of this same tick re-points the MotionMaster
        // by itself. Hard-pinning first would only add a stop spline the very next
        // statement overwrites.
        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
        DcMovement::ClearMovementWait(bot);

        // Stamp the return-leg length and clear the plant latch: the turn-and-plant
        // gate (below) requires at least half of THIS leg covered, and the debounce
        // must start fresh for the new drag.
        pull.returnLegStartDist = bot->GetExactDist(&camp);
        pull.plantTicks = 0;
        // Fresh leg, fresh streak: the pack-cannot-follow debounce must measure THIS
        // drag, never a stale reading carried in from a previous one.
        pull.packPlantedSince = 0;
        // Arm the scripted drag's ground ratchet at the leg's start distance (see
        // the losing-ground check on the return leg below).
        pull.scriptedReturnBest = pull.scriptedStage >= 0 ? pull.returnLegStartDist : 0.0f;

        DcSetPullPhase(context, DcPullPhase::Returning);
        DC_PULL_INFO("[DC:{}] advanced-pull: aggro confirmed at {:.1f}yd from camp "
                     "(from {}) -> dragging to camp", bot->GetName(),
                     bot->GetExactDist(&camp),
                     phase == DcPullPhase::Forming ? "forming"
                         : phase == DcPullPhase::Idle ? "scouting" : "tag");
    }

    // CC-assist. If the tank is crowd-controlled mid-drag (stunned / feared /
    // confused / rooted, or slowed below PullCcSlowFloor) the retreat is failing —
    // it can't reach camp and just eats the pack while the party sits passive. Once
    // the CC has persisted past the grace, ABORT the pull: the tank STOPS the
    // run-home (cancelling the in-flight glide so it doesn't resume to camp when
    // the CC clears) and engages the pack where it stands, and the phase flips to
    // Engage — which both drops the party's passive camp-hold and trips
    // IsLeaderCampFightActive so the followers pile onto the pack and help. The
    // grace ignores a brief micro-CC so a single stutter-stun doesn't throw an
    // otherwise-fine pull away.
    //
    // A SCRIPTED stage does not take the abort, but it does take HALF of it.
    //
    // The abort's answer to a failing drag is "stop and fight where you stand, party
    // piles in" — right when the ground under the tank is ordinary corridor, and
    // catastrophic when the plan exists precisely because that ground is not
    // survivable. Selin's room is the case: a slow read mid-drag aborted the pull,
    // the tank turned and fought the pack back at its own spawn (228.2,-21.0), and
    // the whole room came with it (tr-20260802-215715-3). So a scripted drag keeps
    // dragging, bounded by the return-leg watchdog below.
    //
    // But "keep dragging" is only an answer while the tank is ACTUALLY DRAGGING. The
    // suppression was written against a SLOW, which still walks the pack home — just
    // slowly — and it swept a hard stun in with it, where the tank covers no ground
    // at all and simply stands in the pack. Live (tr-20260808-211502-15, the rotunda
    // south row, whose authored body-pull target IS a Sister of Torment): tag at
    // 21:26:34, Deadly Embrace (44547 — 6s stun plus ~500 a tick, fired exactly 3s
    // after the pack enters combat, and the solo puller is the only thing on its
    // threat list) landed at 21:26:41 with 22.1yd still to run, and the tank was dead
    // at 21:26:47 with the party still passive at a camp it never reached. The same
    // row killed the same tank three times in a row.
    //
    // So on a scripted stage a hard CC RELEASES THE PARTY without touching the pull
    // (DcPullContext::SafetyRelease — the camp-safety valve's "let them fight, don't
    // abandon the maneuver" half). The drag survives, so the fight still ends up at
    // the authored camp rather than in the room; what changes is that the healer
    // unpins and the DPS engage during the seconds the tank cannot move. A slow on a
    // scripted stage still changes nothing at all.
    if (DcSettings::GetBool(bot, "PullCcAssist"))
    {
        bool const scripted = pull.scriptedStage >= 0;
        float const slowFloor = DcSettings::GetFloat(bot, "PullCcSlowFloor");
        char const* const ccReason = scripted ? DcDragHardCcReason(bot)
                                              : DcDragImpairReason(bot, slowFloor);
        uint32 const graceMs =
            uint32(DcSettings::GetFloat(bot, "PullCcAssistGrace") * 1000.0f);
        uint32 ccSinceOut = pull.ccSince;
        bool const ccAbort = DungeonClearMath::ShouldAbortPullForCc(
            ccReason != nullptr, pull.ccSince, now, graceMs, ccSinceOut);
        pull.ccSince = ccSinceOut;
        if (ccAbort && scripted)
        {
            // One-shot per maneuver: SafetyRelease is idempotent, but leaving the
            // latch armed would re-log it every grace period for the whole stun.
            // Deliberately does NOT return — the drag owns the rest of this tick.
            if (!pull.partyReleased)
            {
                pull.SafetyRelease(now);
                DC_PULL_INFO("[DC:{}] scripted-pull: tank {} mid-drag (held >{} ms) "
                             "at {:.1f}yd from camp — the drag has stopped, so it is "
                             "not a drag -> releasing the party to it, pull kept",
                             bot->GetName(), ccReason, graceMs,
                             bot->GetExactDist(&camp));
            }
        }
        else if (ccAbort)
        {
            pull.ccSince = 0;

            // Hard-cancel the run-home. The drag-back is a launched MoveTo
            // (MOVEMENT_COMBAT) glide; a plain StopMoving can leave it queued
            // UNDER the active CC generator, so the instant the impairment
            // wears off the tank resumes sprinting to the (now abandoned) camp
            // instead of fighting. StopBot(HardPin) drops any escort spline +
            // clears the LastMovement wait and pins the point-move on the spot
            // (unconditionally, since the bot may not be "moving" under CC).
            DcMovement::StopBot(bot, DcMovement::Stop::HardPin);

            DcSetPullPhase(context, DcPullPhase::Engage);

            // Start combat right here. Flipping to Engage already releases the
            // party (ReapStrandedPassives + IsLeaderCampFightActive), but the
            // tank itself must commit to the pack too: face and attack the
            // nearest attacker so it turns on the mob the moment the CC clears,
            // rather than drifting back toward camp before stock combat
            // re-acquires. Mirrors EngageDirect's in-range branch.
            Unit* aggressor = bot->GetVictim();
            for (Unit* a : bot->getAttackers())
            {
                if (!a || !a->IsAlive())
                    continue;
                if (!aggressor ||
                    bot->GetExactDist2d(a) < bot->GetExactDist2d(aggressor))
                    aggressor = a;
            }
            if (aggressor)
            {
                bot->SetSelection(aggressor->GetObjectGuid());
                if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, aggressor))
                    ServerFacade::instance().SetFacingTo(bot, aggressor);
                context->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Set(aggressor);
                bot->Attack(aggressor, botAI->IsMelee(bot));
            }
            botAI->ChangeEngine(BOT_STATE_COMBAT);

            DC_PULL_INFO("[DC:{}] advanced-pull: tank {} mid-drag (held >{} ms) -> "
                         "abort pull, stop run-home + engage {}, releasing party "
                         "to assist", bot->GetName(), ccReason, graceMs,
                         aggressor ? aggressor->GetObjectGuid().ToString() : "pack");
            return false;
        }
    }

    uint32 const since = pull.phaseSince;
    // `since` is stamped by DcSetPullPhase via its OWN getMSTime() call, which can
    // read a millisecond LATER than the `now` captured at the top of Execute. So on
    // the very tick we transition into Returning (above), now < since and a raw
    // `now - since` underflows to ~4.29e9 — instantly tripping the leg-timeout and
    // dumping the tank into "fight in place", which is why the pull-back to camp
    // worked or failed at random (a millisecond-boundary race). Clamp the elapsed.
    uint32 const legElapsed = now > since ? now - since : 0u;
    float const dist = bot->GetExactDist(&camp);

    if (dist <= DC_PULL_CAMP_ARRIVE)
    {
        // Back at camp. For an ordinary trash pull the pack is glued to the tank by
        // now (it chased it the whole way), so arriving IS the end of the maneuver:
        // stop, flip to Engage, and that flip is what releases the party.
        //
        // A PULL-BACK is different and releasing on tank-arrival alone is what made
        // the whole party beeline into the lake. The tank can be home long before
        // the boss is: it retreats from the water's edge while he is still swimming
        // in, and on a forced pull it may barely have moved at all. Flipping to
        // Engage there drops the camp hold, hands every follower to stock combat,
        // and stock combat's only visible target is a boss 100+yd away in the water
        // — so they all run at him.
        //
        // So hold the release until HE is actually here. Until then the tank waits
        // on the anchor and the party stays pinned and passive behind it, which is
        // the entire point of dragging him out. The leg watchdog below still bounds
        // it: a boss that never arrives falls out to "fight in place" rather than
        // holding the run forever.
        if (pull.bossPullback)
        {
            Unit* const pulled = pull.pullTarget.IsEmpty()
                ? nullptr : ObjectAccessor::GetUnit(*bot, pull.pullTarget);
            if (pulled && pulled->IsAlive() &&
                pulled->GetExactDist(&camp) > DC_PULLBACK_RELEASE_RANGE)
            {
                DcMovement::StopBot(bot, DcMovement::Stop::Soft);
                DcFaceIfNeeded(bot, pulled);

                // SUMMON-IF-STUCK. The tank is home and the boss is engaged and
                // inbound — but if he is still IN THE WATER BELOW the anchor, he is
                // not inbound in any useful sense: the way out of the lake is the
                // pipe, the pipe is absent from the extracted navmesh, and a chase
                // path with no geometry to follow leaves him hanging at the water's
                // edge indefinitely. There is nothing to wait for, so bring him up.
                //
                // The water-and-below test is what keeps this to the actual failure
                // rather than making it a general "boss is slow" shortcut: a boss
                // who has climbed out onto dry ground fails it and walks the rest of
                // the way himself, exactly as he should. Opt-in per row on top of
                // that (summonWhenStuckBelow) — relocating a boss is a big hammer.
                BossPullback const* const row =
                    BossPullbackRegistry::Find(bot->GetMapId(), pulled->GetEntry());
                if (row && row->summonWhenStuckBelow && pulled->IsInWater() &&
                    pulled->GetPositionZ() < camp.GetPositionZ())
                {
                    // Land him a few yards off the anchor on the tank's side rather
                    // than on top of the party: dropping a boss inside the healer is
                    // how a knockback or a cleave catches everyone at once. Snapped
                    // to the navmesh so he arrives on real ground; the anchor itself
                    // is the fallback (it is known-good — the party is standing on
                    // it).
                    float const bearing = camp.GetAngle(bot);
                    float const lx = camp.GetPositionX() + DC_PULLBACK_SUMMON_OFFSET *
                                                               std::cos(bearing);
                    float const ly = camp.GetPositionY() + DC_PULLBACK_SUMMON_OFFSET *
                                                               std::sin(bearing);
                    float lz = camp.GetPositionZ();
                    NavmeshSnap::Result const snap =
                        NavmeshSnap::Snap(bot->FindMap(), lx, ly, lz, 8.0f);
                    float const tx = snap.ok ? snap.x : camp.GetPositionX();
                    float const ty = snap.ok ? snap.y : camp.GetPositionY();
                    float const tz = snap.ok ? snap.z : camp.GetPositionZ();

                    LOG_INFO("playerbots.dungeonclear",
                             "[DC:{}] pull-back: {} is stuck in the water {:.1f}yd "
                             "below the anchor ({:.1f} vs {:.1f}) with no navmesh "
                             "route out — summoning him to ({:.1f},{:.1f},{:.1f})",
                             bot->GetName(), pulled->GetName(),
                             camp.GetPositionZ() - pulled->GetPositionZ(),
                             pulled->GetPositionZ(), camp.GetPositionZ(), tx, ty, tz);

                    pulled->NearTeleportTo(tx, ty, tz, pulled->GetAngle(bot));
                    return true;
                }

                DC_PULL_TRACE("[DC:{}] pull-back: home at the anchor, holding the "
                              "party — boss still {:.1f}yd out (release at {:.0f})",
                              bot->GetName(), pulled->GetExactDist(&camp),
                              DC_PULLBACK_RELEASE_RANGE);
                return true;
            }
        }

        // A SCRIPTED PULL RELEASES ON ARRIVAL — TANK AND PARTY TOGETHER.
        //
        // The tank used to be held on the camp, past its own arrival, until every live
        // attacker had physically run in (a GATHER radius, bounded by the drag's
        // length). The reasoning was that the tag is taken at RANGE from the stand
        // spot, so for Selin's rows the pack starts its run ~42yd out and the tank is
        // home several seconds before it — release there and stock combat's only
        // visible target is a pack halfway up the corridor, so it chases.
        //
        // That describes a fear rather than the geometry. THE ROOM IS ALREADY EMPTY BY
        // THEN: the tank does not reach the camp until it has dragged the pack the
        // whole way out of the room and down the corridor, so at the moment of arrival
        // every mob that matters is loose in the hall, behind it, and safe to fight.
        // The chase the gather gate feared is a chase into open corridor, not into the
        // room — and it is bounded anyway by the camp leash in Engage
        // (DC_SCRIPTED_PULL_LEASH, with the losing-ground ratchet), which is the gate
        // that actually keeps the tank off the doorway for the REST of the fight.
        //
        // What the hold cost was real: the tank stood still, back to an inbound
        // six-mob pack, not building threat, while the DPS — released on this same
        // tick — opened on the runners. Threat starts on whoever shot first, which is
        // the opposite of what a tank's arrival at the camp is for.
        //
        // So arrival is the end of the maneuver here exactly as it is for an ordinary
        // trash pull: stop, flip to Engage, and that flip releases the party. They stay
        // CAMPED either way — the follower hold survives Engage for a scripted stage
        // (GetLeaderCampHold), it is only `passive` that clears.
        DcMovement::StopBot(bot, DcMovement::Stop::Soft);
        DcSetPullPhase(context, DcPullPhase::Engage);
        DC_PULL_INFO("[DC:{}] advanced-pull: at camp ({:.1f}yd) -> engaging, party "
                     "released", bot->GetName(), dist);
        return false;
    }

    // THE PACK CANNOT FOLLOW. A drag assumes the mob chases the tank; a mob holding
    // UNIT_STATE_NO_COMBAT_MOVEMENT has had its chase generator taken away, so it
    // will stand exactly where it was tagged no matter how far or how cleverly the
    // tank retreats. Dragging it is not slow, it is impossible — and the failure is
    // silent, because both sides simply stop: the tank waits at a camp for a pack
    // that is never coming, and the party waits passive behind it.
    //
    // Overwhelmingly this is SmartAI's ALLOW_COMBAT_MOVEMENT(0) fired by a
    // SMART_EVENT_RANGE band — the "shoots you from where it stands" archetype. It
    // is not rare and it is not one dungeon: 293 creature templates in the world DB
    // carry that pattern, ~35 of them spawning across ~17 instances this module
    // runs (Blackrock Depths 8, Maraudon 4, Deadmines 3, Utgarde Keep / Zul'Gurub 3
    // each, ...). Reading the UNIT STATE rather than the script rows also covers the
    // shapes a table of entries would miss — C++ AI calling SetCombatMovement(false),
    // and anything rooted or otherwise immobilised mid-drag.
    //
    // The answer is to go to it. The tank turns around and re-engages where the pack
    // actually is, and the Engage flip releases the party to follow — which is the
    // same fight they would have had with pulls off, and the fight a human would
    // take. Deliberately NOT "drag further until it re-chases": the band that makes
    // one resume is per-creature (Goblin Engineer 622 resumes only outside 30yd),
    // and hauling a pack 30yd+ through a dungeon to find that edge is how the
    // neighbouring pack joins in.
    //
    // Excluded for a PULL-BACK and for a SCRIPTED stage. Both are authored maneuvers
    // whose whole premise is that the fight must happen at a specific anchor, and a
    // scripted plan already has purpose-built machinery for a pack that will not
    // close (the stand-off camp walk). Neither should be second-guessed here.
    if (!pull.bossPullback && pull.scriptedStage < 0)
    {
        Unit* const pulled = pull.pullTarget.IsEmpty()
            ? nullptr : ObjectAccessor::GetUnit(*bot, pull.pullTarget);
        bool const planted = pulled && pulled->IsAlive() &&
                             pulled->HasUnitState(UNIT_STATE_NO_COMBAT_MOVEMENT);

        uint32 plantedSinceOut = pull.packPlantedSince;
        bool const abandon = DungeonClearMath::ShouldAbandonPlantedDrag(
            planted, pull.packPlantedSince, now, DC_PULL_PLANT_CONFIRM_MS,
            plantedSinceOut);
        pull.packPlantedSince = plantedSinceOut;

        if (abandon)
        {
            // Hard-cancel the run-home for the same reason the CC abort does: the
            // drag-back is a launched MOVEMENT_COMBAT glide, and a plain stop can
            // leave it queued so the tank resumes sprinting to the abandoned camp
            // the moment it is free.
            DcMovement::StopBot(bot, DcMovement::Stop::HardPin);

            // Commit to the pack now rather than drifting until stock combat
            // re-acquires. Nearest live attacker if one has reached us, else the mob
            // we tagged — which for a planted pack is the normal case, because
            // nothing has reached us and nothing will.
            Unit* engageOn = nullptr;
            for (Unit* a : bot->getAttackers())
            {
                if (!a || !a->IsAlive())
                    continue;
                if (!engageOn ||
                    bot->GetExactDist2d(a) < bot->GetExactDist2d(engageOn))
                    engageOn = a;
            }
            if (!engageOn)
                engageOn = pulled;

            bot->SetSelection(engageOn->GetObjectGuid());
            if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, engageOn))
                ServerFacade::instance().SetFacingTo(bot, engageOn);
            context->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Set(engageOn);
            bot->Attack(engageOn, botAI->IsMelee(bot));
            botAI->ChangeEngine(BOT_STATE_COMBAT);

            // Report the MEASURED hold, not the threshold — when this fires late
            // (a mob that toggled a few times before settling) the difference is
            // the whole diagnostic.
            uint32 const plantedFor = now > pull.packPlantedSince
                ? now - pull.packPlantedSince : 0u;

            DcSetPullPhase(context, DcPullPhase::Engage);
            DC_PULL_INFO("[DC:{}] advanced-pull: {} has no combat movement (held "
                         "{} ms) — it cannot be dragged, so the drag is abandoned at "
                         "{:.1f}yd from camp -> engaging it where it stands, party "
                         "released", bot->GetName(), pulled->GetName(),
                         plantedFor, dist);
            return false;
        }
    }

    // Return-leg budget off the leg length stamped at the turn-around, so a
    // pull-back's long haul home isn't cut short (see DcPullLegTimeoutMs).
    //
    // For a pull-back the leg covers TWO journeys, not one, and budgeting only the
    // tank's would have made the watchdog fire during a perfectly healthy pull: the
    // tank runs ~60yd home in ~13s and then WAITS at the anchor for the boss, who
    // still has ~150yd of swim-and-ramp to cover. That wait happens inside this
    // leg. Add the boss's remaining distance so the budget covers both; it stays
    // bounded, so a boss who genuinely cannot path still falls out to fight-in-place
    // instead of holding the run open forever.
    float legBudgetDist = pull.returnLegStartDist;
    if (pull.bossPullback)
    {
        if (Unit* const pulled = pull.pullTarget.IsEmpty()
                ? nullptr : ObjectAccessor::GetUnit(*bot, pull.pullTarget))
            legBudgetDist += pulled->GetExactDist(&camp);
    }
    if (legElapsed > DcPullLegTimeoutMs(pull, legBudgetDist))
    {
        // Return leg wedged — fight where we are rather than freeze.
        DcSetPullPhase(context, DcPullPhase::Engage);
        DC_PULL_INFO("[DC:{}] advanced-pull: return leg wedged at {:.1f}yd from camp "
                     "after {} ms -> fighting in place",
                     bot->GetName(), dist, legElapsed);
        return false;
    }

    // Turn-and-plant. A human tank doesn't sprint the WHOLE leg back-turned; once
    // the pack is glued to it and chasing, it stops a few steps in, turns, and
    // fights — the fight happens wherever it actually plants. Stopping early is the
    // single biggest cut to the back-exposure window the daze-immunity cheat exists
    // to paper over. Suppressed for LOS-break pulls (the whole point is reaching
    // the corner) and gated on half the leg covered + a 2-tick debounce so a single
    // noisy distance read can't trip it. The plant point IS the new camp — re-stamp
    // it so the spread gate, status panel, and follower hold all follow the fight.
    // Suppressed outright for a PULL-BACK drag. Turn-and-plant makes the fight
    // happen "wherever the tank plants", which is exactly the freedom a pull-back
    // must not have: the anchor was chosen because it is the only safe ground, and
    // planting halfway home would drop the party back into the water the maneuver
    // exists to leave. Same reasoning as the losPull suppression inside
    // ShouldPlantEarly (there the point is reaching the corner; here it is reaching
    // the anchor) — a distinct flag rather than reusing losPull so the addon status
    // line and the LOS-camp logic keep their own meaning.
    //
    // Suppressed for a SCRIPTED stage on the same grounds. The authored camp is the
    // only ground in reach that the rest of the room cannot see; planting "wherever
    // the pack glues on" would drop a six-mob fight in the doorway or, worse, back
    // inside the room next to the pack that has not been pulled yet.
    if (DcSettings::GetBool(bot, "PullPlantEnable") && !pull.bossPullback &&
        pull.scriptedStage < 0)
    {
        float const glueRadius = DcSettings::GetFloat(bot, "PullPlantGlueRadius");
        std::vector<float> attackerDists;
        for (Unit* a : bot->getAttackers())
            if (a && a->IsAlive())
                attackerDists.push_back(bot->GetExactDist(a));

        if (DungeonClearMath::ShouldPlantEarly(attackerDists, glueRadius,
                /*glueTicksNeeded*/ 2u, pull.losPull, dist,
                pull.returnLegStartDist, pull.plantTicks))
        {
            // Hard-cancel the run-home exactly as the CC-abort branch does: a plain
            // StopMoving can leave the launched MOVEMENT_COMBAT glide queued and the
            // tank resumes sprinting to the abandoned camp instead of fighting.
            DcMovement::StopBot(bot, DcMovement::Stop::HardPin);

            // The camp IS wherever the fight lands. Re-stamp it (fresh publish) so
            // the party converges on the plant point and the addon panel relocates.
            pull.PublishCamp(Position(bot->GetPositionX(), bot->GetPositionY(),
                                      bot->GetPositionZ()),
                             now);

            // Turn on the nearest attacker so the tank commits the moment it stops,
            // rather than drifting before stock combat re-acquires (mirrors the
            // CC-abort engage block).
            Unit* nearest = bot->GetVictim();
            for (Unit* a : bot->getAttackers())
            {
                if (!a || !a->IsAlive())
                    continue;
                if (!nearest ||
                    bot->GetExactDist2d(a) < bot->GetExactDist2d(nearest))
                    nearest = a;
            }
            if (nearest)
            {
                bot->SetSelection(nearest->GetObjectGuid());
                if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, nearest))
                    ServerFacade::instance().SetFacingTo(bot, nearest);
                context->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Set(nearest);
                bot->Attack(nearest, botAI->IsMelee(bot));
            }

            DcSetPullPhase(context, DcPullPhase::Engage);
            botAI->ChangeEngine(BOT_STATE_COMBAT);

            DC_PULL_INFO("[DC:{}] advanced-pull: pack gathered at {:.1f}yd from camp "
                         "-> plant + engage", bot->GetName(), dist);
            return false;
        }
    }

    // SCRIPTED PULL — the drag may not LOSE GROUND.
    //
    // A drag-back is a straight run home, so distance to camp should only ever
    // fall. When it climbs, something else owns the tank's movement — and the
    // tick-by-tick MoveTo below cannot dig itself out, because DcMoveTo DEDUPES on
    // destination: the camp hasn't changed, so it reports "already going there"
    // and issues nothing, while the other mover keeps driving. The maneuver's
    // existing backstop doesn't cover it either — that one only fires when the bot
    // is standing STILL, and here it is moving perfectly happily, just outward.
    //
    // Live (Magisters' Terrace, tr-20260802-222832-1): advance launched a 69.9yd
    // escort spline at Selin on the same tick the west stage committed — inside the
    // one tick the phase was still Idle, so the stand-down could not see it — and
    // the drag then oscillated against it for nineteen seconds (16 -> 6 -> 21 ->
    // 9 -> 19 -> 8 -> 17) before finally reaching camp. That is the reported "in
    // and out of the room as soon as combat started".
    //
    // A ratchet rather than a tick-to-tick delta: path noise and the arc around a
    // doorway both give ground momentarily, and only a sustained loss against the
    // best-so-far means the leg has been taken away from us.
    if (pull.scriptedStage >= 0)
    {
        if (ScriptedPullLostGround(pull.scriptedReturnBest, dist))
        {
            // HardPin, not Hold: kill the foreign spline AND the movement wait, and
            // pin the point-move on the spot so the re-issue below starts from a
            // clean slate. Re-arm the ratchet here so a genuinely long leg isn't
            // re-cancelled every tick while it works its way home.
            DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
            DcMovement::ClearMovementWait(bot);
            DC_PULL_INFO("[DC:{}] scripted-pull: drag lost ground to camp ({:.1f}yd "
                         "vs best {:.1f}) — something else is driving the tank -> "
                         "cancelled it and re-issuing the run home",
                         bot->GetName(), dist, pull.scriptedReturnBest);
            pull.scriptedReturnBest = dist;
        }
        else if (dist < pull.scriptedReturnBest || pull.scriptedReturnBest <= 0.0f)
            pull.scriptedReturnBest = dist;
    }

    // Run to camp. Own the tick (return true even on a duplicate move) so stock
    // combat chase/attack can't grab the tank and fight at the pack instead.
    DC_PULL_TRACE("[DC:{}] pull returning: {:.1f}yd to camp ({} ms into leg)",
                  bot->GetName(), dist, legElapsed);
    bool const moved =
        DcMoveTo(bot->GetMapId(), camp.GetPositionX(), camp.GetPositionY(), camp.GetPositionZ(),
                 /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                 /*exact_waypoint*/ false, MovementPriority::MOVEMENT_COMBAT);

    // BACKSTOP: refused, standing still, and a stale equal-priority LastMovement
    // wait is what's holding the retreat down. Every known source of one is cleared
    // at the turn-around now, so reaching here means a path we haven't found — and
    // what it costs is the tank standing in the pack taking free hits until the
    // clock lapses, invisibly, because the maneuver returns true either way. Break
    // the wait so the next tick can issue, and log it: a silent no-op here is
    // exactly what made this stall so hard to see in the first place.
    //
    // Keyed on the wait itself rather than on "refused", because DcMoveTo also
    // returns false for a duplicate destination (the normal every-tick case while
    // the glide is already running), while the run is paused, and while the bot
    // cannot move at all under CC — none of which this should touch.
    if (!moved && !bot->isMoving() &&
        IsWaitingForLastMove(MovementPriority::MOVEMENT_COMBAT))
    {
        DcMovement::StopBot(bot, DcMovement::Stop::HardPin);
        DC_PULL_DEBUG("[DC:{}] pull returning: run-home refused while standing at "
                      "{:.1f}yd from camp -> cleared the stale movement wait",
                      bot->GetName(), dist);
    }
    return true;
}

