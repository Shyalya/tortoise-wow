/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearActions.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
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
#include "InstanceScript.h"
#include "Log.h"
#include "LootMgr.h"
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
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearApproach.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearApproachIo.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/RoomAggroRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"
#include "Ai/Dungeon/DungeonClear/Trigger/DungeonClearTriggers.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcDoorPolicy.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcMovement.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPartyState.h"
#include "Ai/Dungeon/DungeonClear/Util/DcBreadcrumb.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPathWorker.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPullPlanner.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRezRecovery.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStrandedRecovery.h"
#include "Ai/Dungeon/DungeonClear/Util/DcSmartRest.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTankForm.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"
#include "Ai/Dungeon/DungeonClear/Util/DcWaitAtBossDecision.h"
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

namespace
{

    // Shared tuning constants (DC_ENGAGE_RANGE, the cone scan range/angle,
    // DC_USE_CORRIDOR_SCAN, DC_CORRIDOR_*,
    // DC_PULL_START_RANGE) now live in DungeonClearTuning.h, shared with the
    // trigger ladder so the two layers cannot drift. The old per-context cone
    // names (DC_ENGAGE_CONE_RANGE / DC_ENGAGE_CONE_HALF_ANGLE) are the trash-cone
    // scan; use DC_TRASH_CONE_RANGE / DC_TRASH_CONE_HALF_ANGLE.

    // Any hostile already inside this radius of the bot will pull as the tank
    // moves, regardless of whether it sits on the corridor line. The engage
    // selector hits the closest such mob first so the tank never runs past an
    // adjacent pack to reach a farther on-corridor target.
    constexpr float DC_PROXIMITY_ENGAGE_RANGE = 20.0f;

    // Stand-off before a blocking door, as distance TRAVELLED ALONG the path to
    // where the route first reaches the doorway (see
    // DcEngageGeometry::DistAlongPathToClosedDoor) — NOT straight-line to the
    // door's GO origin, which can sit past the gap and made the walk-in glide
    // straight through it. The door-blocked action parks when the remaining
    // along-path travel to the door drops to this, every tick before the glide is
    // re-issued, so the tank halts on the near side of the doorway.
    constexpr float DC_DOOR_STOP_DISTANCE = 10.0f;

    // Once parked at a blocking door, open it with GameObject::Use (the exact
    // path a client right-click takes) — but ONLY when the bot is actually
    // entitled to, as decided by BotCanOpenDoorLikePlayer (a faithful mirror
    // of the core's lock adjudication; see DcDoorPolicy.h for the rules). The
    // raw GameObject::Use door branch toggles the GO state with no lock and no
    // script/event check, so calling it on the wrong door desyncs the
    // encounter; the entitlement gate is what keeps us honest. Lock-free
    // (script/encounter) doors are left shut and we wait for the human. Flip
    // to false to never auto-open anything.
    constexpr bool DC_ATTEMPT_DOOR_OPEN = true;

    // Minimum gap between Use() clicks on the same door. Auto-closing gates
    // (door.autoCloseTime — Strat's King's Square Gate re-shuts 3000ms after
    // opening) must be re-clicked when they shut again, but never spammed
    // every AI tick while one Use is still swinging the door open.
    constexpr uint32 DC_DOOR_REUSE_MS = 2500;

    // Never Use() a door the bot isn't actually standing at. GameObject::Use
    // has NO server-side range check, so a mis-flagged far door (stale value /
    // phantom blocker) would otherwise swing open from across the map — a
    // silent event desync no real player could cause. Generous vs. the ~5yd
    // client interact range because parking happens along-path at up to
    // DC_DOOR_STOP_DISTANCE before the 8yd door band, and a wide gate's GO
    // origin (its hinge) sits yards off the walkable centerline — legitimate
    // parks measure up to ~20yd from the origin. The failure class this guards
    // against is 50yd+, so room-local is the right cut.
    constexpr float DC_DOOR_USE_RANGE = 25.0f;

    // Observation-gap re-arm for the Blocked-state watchdog (DcApproachState::
    // doorStall*). A stall older than this with no fresh observation is treated
    // as ended — the door opened or the run moved on — so the next stall on the
    // same door starts a fresh DungeonClear.DoorBlockedTimeout window instead
    // of inheriting stale accrual. Deliberately LONGER than door.autoCloseTime
    // cycles (~3s shut/open on auto-closing gates): a bot livelocked clicking a
    // gate open and watching it re-shut keeps accruing across cycles and still
    // times out into the pause.
    constexpr uint32 DC_DOOR_STALL_REARM_MS = 10000;


    // Per-approach counter resets now live as named subset methods on the owning
    // struct — DcApproachState::OnEnteredEngageRange (clear the dead-end
    // escalation counter + direct-pursuit give-up latch) and ::OnBossChange (wipe
    // every per-approach counter/latch + the position sentinel). The sticky
    // engage-trash target is a separate value, reset alongside OnBossChange at the
    // call site in Execute.

    // True only when the bot is genuinely ENTITLED to open this door — i.e. a
    // human player at this keyboard could open it by clicking. The slot
    // adjudication mirrors the core's Spell::CanOpenLock and lives in
    // DcDoorPolicy::CanOpenSlots (pure, unit-tested); see that header for the
    // full rules. The short version:
    //
    //   - lockId == 0 -> NEVER. A closed lock-free door in an instance is
    //     script/encounter-controlled (the Uldaman Ironaya seal and friends
    //     carry no lock and no NOT_SELECTABLE flag until their event runs).
    //     Clickable doors always reference a Lock.dbc row, even an empty one.
    //   - An empty lock (a row with no typed slots, e.g. Deadmines lock 85) or
    //     a bare-hands locktype slot (Quick/Slow Open, e.g. lock 86) opens for
    //     anyone — these were wrongly refused before, which is why the tank
    //     paused at every plain Deadmines door.
    //   - Key items (Scarlet Key, Key to the City) and lockpicking open their
    //     locks exactly as a player would — EXCEPT for the doors on
    //     DcEventDoorRegistry::IsKeyExempt (the SM Armory/Cathedral wing gates
    //     plus every keyed door in Scholomance, Stratholme and Dire Maul
    //     North), where the key requirement is deliberately waived so a keyless
    //     tank can still clear the dungeon.
    //   - GO_FLAG_LOCKED suppresses the bare-hands slots: flagged gates demand
    //     the real key/skill (Strat's King's Square Gate carries a Quick Open
    //     slot yet requires the Key to the City — that gate is now key-exempt,
    //     but the rule still governs every flagged gate not on the list).
    //
    // This remains the gate that keeps the tank from force-opening doors it
    // has no business opening: GameObject::Use's door branch toggles the GO
    // state with NO lock and NO script/event check, so it must only ever be
    // called on a door this returns true for.
    bool BotCanOpenDoorLikePlayer(Player* bot, GameObject* go)
    {
        if (!bot || !go)
            return false;
        GameObjectTemplate const* info = go->GetGOInfo();
        if (!info || info->type != GAMEOBJECT_TYPE_DOOR)
            return false;
        // Not-selectable / can't-interact doors are driven purely by the
        // instance/boss scripting (encounter gates, "kill the boss" doors).
        // A player can't click them; never force them.
        if (go->HasGameObjectFlag(GO_FLAG_NOT_SELECTABLE) ||
            go->HasGameObjectFlag(GO_FLAG_INTERACT_COND))
            return false;

        // Key-exempt traversal gates (SM Armory/Cathedral): treated as if the
        // bot held the key. See DcEventDoorRegistry::IsKeyExempt.
        if (DcEventDoorRegistry::IsKeyExempt(go->GetEntry()))
            return true;

        uint32 const lockId = info->GetLockId();
        if (!lockId)
        {
            // Lock-free is the shape of BOTH plain clickable traversal gates and
            // script/event seals (Uldaman's Seal of Khaz'Mul) the bot must not
            // pop. Default to refusing; open only entries the registry verifies
            // are ordinary player-clickable doors (Scholomance's Iron Gates).
            return DcEventDoorRegistry::IsLockFreeClickable(go->GetEntry());
        }

        LockEntry const* lock = sLockStore.LookupEntry(lockId);
        if (!lock)
            return false;           // unknown lock — don't force it open

        DcDoorPolicy::LockSlot slots[DcDoorPolicy::LOCK_SLOT_COUNT];
        for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
        {
            slots[i].keyType = lock->Type[i];
            slots[i].index = lock->Index[i];
            slots[i].requiredSkill = lock->Skill[i];
        }

        int32 const lockpick = bot->HasSkill(SKILL_LOCKPICKING)
                                   ? static_cast<int32>(bot->GetSkillValue(SKILL_LOCKPICKING))
                                   : -1;
        return DcDoorPolicy::CanOpenSlots(
            slots, DcDoorPolicy::LOCK_SLOT_COUNT,
            go->HasGameObjectFlag(GO_FLAG_LOCKED),
            // Keys aren't consumed by opening, so possession is the requirement.
            [bot](uint32 itemEntry) { return bot->HasItemCount(itemEntry, 1); },
            lockpick);
    }

    // Returns the stored sticky engage-trash target if it's still a valid pull
    // candidate. Nullptr means "re-pick from scratch".
    //
    // COMMIT-AND-HOLD. Release only on conditions that make this mob genuinely
    // no longer our target: gone, dead, off-map, no longer hostile, or evading
    // (reset and leashing back to spawn). It deliberately does NOT drop on
    // distance or on a per-tick reachability re-probe. Both of those fluctuate
    // as the bot walks and rounds corners, and re-picking on them flip-flops
    // the target between two nearby mobs — leaving the tank bouncing in place
    // between competing MoveTo commands, the exact bug this sticky exists to
    // prevent. (The old 25yd distance gate was narrower than the 35yd
    // corridor/cone pick range, so any mob picked in the 25-35yd band was
    // dropped the very next tick and re-picked — a built-in oscillation.)
    //
    // The two concerns those gates addressed are handled elsewhere without a
    // noisy per-tick check: the pick is corridor/cone- and level-reachability-
    // filtered at selection time (see Execute and FindNearestHostileWithin),
    // and "engage a nearer pack before running to a far mob" is the proximity
    // preempt in Execute, which commits to whatever it preempts to.
    Unit* ResolveStickyTrashTarget(Player* bot, ObjectGuid stickyGuid)
    {
        if (!bot || stickyGuid.IsEmpty())
            return nullptr;
        Unit* u = ObjectAccessor::GetUnit(*bot, stickyGuid);
        if (!u || !u->IsInWorld() || !u->IsAlive())
            return nullptr;
        if (u->GetMapId() != bot->GetMapId())
            return nullptr;
        if (!bot->IsHostileTo(u))
            return nullptr;
        // A creature that has reset and is leashing home can't be pulled where
        // it stands; give up the chase and let the selector re-pick. This is
        // the clean "stop sticking" signal that replaces the old distance gate.
        if (Creature* c = u->ToCreature())
            if (c->IsInEvadeMode())
                return nullptr;
        return u;
    }

    // True when `target` sits at the end of a single complete PathGenerator
    // route (PATHFIND_NORMAL). A long, winding route — the classic case being a
    // pack at the foot of a ramp the tank is standing atop — overruns
    // PathGenerator's hop cap and resolves only as PATHFIND_INCOMPLETE. The raw
    // MoveTo that EngageDirect issues can then only build the truncated prefix
    // of that route, which dead-ends against the geometry: the tank glides a
    // few yards (or not at all) and then sits in "clearing trash" forever,
    // never closing to aggro range. Such far targets must be approached via the
    // no-hop-cap long-path that Advance drives, so engage-trash yields to it
    // rather than bee-lining (see DungeonClearEngageTrashAction::Execute).
    bool IsDirectlyReachable(Player* bot, Unit* target)
    {
        if (!bot || !target)
            return false;
        PathGenerator gen(bot);
        gen.CalculatePath(target->GetPositionX(), target->GetPositionY(),
                          target->GetPositionZ(), /*forceDest*/ false);
        return gen.GetPathType() == PATHFIND_NORMAL;
    }

    // Closest valid hostile from `candidates` within `range` of the bot,
    // LOS-checked. Drives the proximity preempt in DungeonClearEngageTrashAction
    // so the tank engages a mob already inside its aggro bubble before running
    // to a farther on-corridor target. LOS is checked here because the
    // far-targets candidate list is built with LOS ignored.
    Unit* FindNearestHostileWithin(Player* bot, float range, GuidVector const& candidates)
    {
        if (!bot)
            return nullptr;
        Unit* best = nullptr;
        float bestDist = range;
        for (ObjectGuid guid : candidates)
        {
            Unit* u = ObjectAccessor::GetUnit(*bot, guid);
            if (!u || !u->IsAlive())
                continue;
            if (!bot->IsHostileTo(u))
                continue;
            float const dist = bot->GetDistance(u);
            if (dist >= bestDist)
                continue;
            if (!bot->IsWithinLOSInMap(u))
                continue;
            // A mob a floor above/below can sit inside the proximity bubble and
            // pass LOS through a railing/gap yet have no path — engaging it
            // wedges the tank. Require a real route before letting it preempt.
            if (!DcEngageGeometry::IsLevelReachable(bot, u))
                continue;
            best = u;
            bestDist = dist;
        }
        return best;
    }

}

bool DungeonClearEngageActionBase::EngageDirect(Unit* target)
{
    if (!target || !target->IsInWorld() || !target->IsAlive())
        return false;

    bool const melee = botAI->IsMelee(bot);
    float const attackRange = melee
        ? (bot->GetCombatReach() + target->GetCombatReach() + 1.0f)
        : (botAI->GetRange("spell") - CONTACT_DISTANCE);
    float const distance = bot->GetExactDist(target);

    // A druid tank engages as a BEAR. This walk-in is the OTHER way DC opens a
    // fight (the Leeroy path, boss engages, room clears) and, like the advanced
    // pull, it runs entirely on the non-combat engine — so the combat-only
    // "bear form" trigger doesn't fire until the pack has already landed its
    // opener on a caster-form tank. Shift on the way in; the form is instant and
    // costs the approach nothing. See DcTankForm.
    //
    // Ranged-gated at the pull commit distance rather than fired the moment a
    // target exists: past that the tank is still crossing open ground where the
    // between-pulls machinery (Smart Rest drinking) needs caster form, and
    // nothing can aggro it yet anyway. Inside it, the opener — the class pull
    // spell below, or the committed run-in — is the very next thing to happen.
    if (distance <= DC_PULL_START_RANGE)
        DcTankForm::EnsureBearForm(botAI);

    if (distance > attackRange)
    {
        // Room-aggro skirt: if a flagged boss's aggro sphere sits between us and
        // the target, detour AROUND it first instead of bee-lining through it and
        // waking the whole room mid-clear (the SM Cathedral / Mograine failure).
        // RoomAggroSkirtPoint is a no-op (nullopt) outside an active room clear, so
        // ordinary corridor engages keep their straight line. The detour leg is
        // NORMAL priority (combat may interrupt — we're not committed to a pull
        // yet) and is consumed each tick; the orbit emerges from per-tick re-aiming
        // and ends the moment the direct line clears. A detour that can't be pathed
        // falls through to the straight approach rather than freezing the clear.
        //
        // Then the general case: bystander PACKS whose aggro arcs the walk clips.
        // Same ordering as MoveToSkirtingRoomAggro — the boss sphere is registry-
        // declared and unrecoverable to wake, so it keeps priority, and only one
        // orbit ever drives a tick. THIS is the walk-in the en-route avoidance was
        // meant to cover: every trash/room/boss engage funnels through EngageDirect,
        // whereas MoveToSkirtingRoomAggro is reached only from the pull's room-clear
        // branch, so wiring it there alone left the ordinary "jog across the room at
        // the far pack" — the case the feature exists for — walking a straight line.
        //
        // Not inside the final approach band: those last yards are the committed
        // run-in (COMBAT priority below, deliberately uninterruptible), and there is
        // nothing left to route around by then — a bystander close enough to matter
        // at that range has either already aggroed or is inside its own padded
        // sphere with us, where the orbit has no room to help. It also keeps the
        // per-tick grid search off the ticks that matter most.
        std::optional<Position> wp = RoomAggroSkirtPoint(target);
        if (!wp && distance > attackRange + DC_COMBAT_APPROACH_RANGE)
            wp = DcEngageGeometry::EnRoutePackAvoidPoint(bot, context, target);
        if (wp)
        {
            bool const moved = DcMoveTo(bot->GetMapId(), wp->GetPositionX(),
                                      wp->GetPositionY(), wp->GetPositionZ(),
                                      /*idle*/ false, /*react*/ false,
                                      /*normal_only*/ false, /*exact_waypoint*/ false,
                                      MovementPriority::MOVEMENT_NORMAL);
            if (moved || bot->isMoving() ||
                IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
                return true;
            // else: detour unwalkable — fall through to the direct approach.
        }

        // Optional: try a single class pull-spell opener before walking in.
        // Fire-and-forget — if the cast fails (out of range, on cooldown,
        // silenced) we just fall through to the walk; ResolvePullSpell already
        // dropped the opener entirely if the tank never trained it.
        // Suppress repeats while we're closing on the same target so we
        // don't spam casts each tick.
        if (DC_TRY_PULL_SPELL)
        {
            ObjectGuid const lastPullTarget =
                AI_VALUE(DcPullContext&, DcKey::PullContext).tagTarget;
            if (lastPullTarget != target->GetObjectGuid())
            {
                if (auto pick = ResolvePullSpell(botAI, bot))
                {
                    if (distance >= pick->minRange && distance <= pick->maxRange &&
                        bot->IsWithinLOSInMap(target))
                    {
                        bot->SetSelection(target->GetObjectGuid());
                        if (botAI->CastSpell(pick->spellId, target))
                        {
                            context->GetValue<DcPullContext&>(DcKey::PullContext)
                                ->Get().tagTarget = target->GetObjectGuid();
                            context->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Set(target);
                            // Don't change engine state yet — let combat
                            // get tagged naturally when the pull lands.
                        }
                    }
                }
            }
        }

        // Walk into range. We deliberately skip the pull pipeline's reach-pull
        // mechanism because its threshold arithmetic creates a dead zone
        // (too-far-to-pull, too-close-to-reach) that leaves the tank standing
        // right at the edge of aggro range.
        //
        // COMBAT priority only for the final approach: it can't be interrupted
        // by the bot's combat reflexes (see DungeonClearAdvanceAction), so on a
        // longer walk it makes the tank plow through any pack it aggros without
        // stopping. Past the close-approach band use NORMAL so combat interrupts
        // and the tank fights what it pulls en route.
        MovementPriority const prio =
            (distance <= attackRange + DC_COMBAT_APPROACH_RANGE)
                ? MovementPriority::MOVEMENT_COMBAT
                : MovementPriority::MOVEMENT_NORMAL;
        bool const moved = DcMoveTo(target->GetMapId(),
                                  target->GetPositionX(),
                                  target->GetPositionY(),
                                  target->GetPositionZ(),
                                  /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                                  /*exact_waypoint*/ false, prio);

        // Decisive approach: commit to one continuous run into range instead of
        // the old stutter-creep. MoveTo returns false on a DUPLICATE move (same
        // destination while a prior glide is still in flight) — that means "the
        // run is already happening", NOT a failure. The old `return MoveTo(...)`
        // surfaced that false to the engine, which then fell through to the
        // lower-relevance Advance action; Advance saw us inside engage range and
        // called StopMoving — so every other tick killed the glide and the tank
        // crept in a few yards at a time. Treat an in-flight / freshly-queued
        // move as success (consume the tick, keep gliding); only a genuine
        // "couldn't move AND not moving" falls through so Advance's posStuck /
        // stall escalation can still catch a real wedge. Mirrors the direct-
        // pursuit branch in DungeonClearAdvanceAction.
        if (moved || bot->isMoving() || IsWaitingForLastMove(prio))
            return true;
        return false;
    }

    DcMovement::StopBot(bot, DcMovement::Stop::Soft);

    bot->SetSelection(target->GetObjectGuid());
    if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, target))
        ServerFacade::instance().SetFacingTo(bot, target);

    context->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Set(target);
    bot->Attack(target, melee);
    // Non-aggressive ("yellow"-name, neutral) bosses won't aggro just because
    // the tank is standing in melee range, and bot->Attack() alone doesn't
    // reciprocate combat until a swing lands — which stock combat targeting can
    // drop the target before. Without a ranged pull, the proximity walk-in then
    // never actually engages. Force such a creature into combat with us so the
    // pull is deterministic. Scoped to non-hostile targets only: a "red"-name
    // hostile mob (IsHostileTo, reaction <= REP_HOSTILE) aggros on its own, so
    // we leave its natural pull untouched.
    if (Creature* creature = target->ToCreature())
        if (!bot->IsHostileTo(creature))
            creature->EngageWithTarget(bot);
    botAI->ChangeEngine(BOT_STATE_COMBAT);
    botAI->SetNextCheckDelay(sPlayerbotAIConfig.reactDelay);
    return true;
}

bool DungeonClearEngageActionBase::DriveObjectiveEngage()
{
    uint32 entry = 0;
    float search = 0.0f;
    // anyStep: match the combat trigger (DungeonClearObjectiveEngageCombatTrigger),
    // which arms during a leading MoveTo so a pre-arrival sapper's stealth still
    // gets broken. The action and trigger MUST agree on the target entry/radius.
    if (!DungeonEventExecutor::ActiveEngageStep(context, entry, search, /*anyStep*/ true))
        return false;

    Creature* target = bot->FindNearestCreature(entry, search, /*alive*/ true);
    if (!target)
        return false;
    // FindNearestCreature is a flat 2D scan that can return an instance of the
    // entry across a wall / on another level. Only engage one we can actually
    // reach (a complete on-level route); requireDirect=false keeps a legitimately
    // FAR seek alive, since the seek IS this objective's navigation.
    if (!DcEngageGeometry::IsEngageReachable(bot, target, /*requireDirect*/ false))
        return false;
    // ResolveEscortConflict cancels a launched escort glide but leaves our own
    // approach move intact (unlike StopBot(Hold)).
    DcMovement::ResolveEscortConflict(bot);
    SetPhase(context, "objective");
    return EngageDirect(target);
}

std::optional<Position> DungeonClearEngageActionBase::RoomAggroSkirtPoint(Unit* target)
{
    if (!target)
        return std::nullopt;

    // Skirt whenever a room-aggro pre-clear is in progress for the NEXT boss —
    // independent of whether the tank has reached boss-engage range yet.
    //
    // The old gate (IsRoomClearActive) ALSO required IsAtBossEngage, i.e. the
    // skirt only armed once the tank was already AT the boss's aggro edge. But
    // the room-aggro pre-clear event engages the nearest trash from far out (its
    // condition has no distance gate — see DcRoomAggroPreClearCondition),
    // so during the approach EngageDirect ran with the skirt OFF and bee-lined a
    // straight line that could cut clean through the boss's aggro sphere before
    // the skirt ever armed. That is the live SM Cathedral failure: the tank walks
    // into Commander Mograine's aggro range on its way to far-side Scarlet trash.
    // Gating the skirt on at-boss-engage disabled it during the very approach it
    // exists to protect.
    //
    // Gate instead on "next boss is a room-aggro boss with trash still up". The
    // chord/sphere test (NeedsRoomAggroSkirt) below no-ops when the straight
    // approach already clears the sphere, so ordinary far-out corridor walks pay
    // only a cheap registry + cached-value read and keep their direct line.
    std::optional<DungeonBossInfo> next =
        AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next.has_value())
        return std::nullopt;
    if (!RoomAggroRegistry::Find(bot->GetMapId(), next->entry))
        return std::nullopt;
    if (AI_VALUE(GuidVector, DcKey::RoomTrashRemaining).empty())
        return std::nullopt;

    Creature* boss = DcTargeting::GetLiveBoss(bot, context, next->entry);
    if (!boss)
        return std::nullopt;

    // NEVER skirt the boss itself. When the engage target IS the room-aggro boss
    // (the at-boss engage / a pull-target picker that resolved to the boss), the
    // "detour around the sphere" is incoherent: the target sits at the sphere's
    // CENTRE, so the orbit's exit test (a straight shot at the target clears the
    // sphere) can never become true and the tank orbits the boss forever, drifting
    // into a wall and eventually into the boss's own aggro — the live Jammal'an
    // failure (8s of "skirting ... before approaching Jammal'an the Prophet"). A
    // boss pull walks straight in; only TRASH outside the sphere is ever skirted.
    if (target->GetObjectGuid() == boss->GetObjectGuid())
        return std::nullopt;

    // Single-source avoid sphere (shared with the room-trash exclusion, the
    // follower skirt, and the boss-engage standoff).
    float const safeRadius = DcEngageGeometry::RoomAggroSphereRadius(bot, boss);

    // Orbit-direction latch lives in the per-approach state so it survives across
    // ticks (the whole point — see AggroSafeApproachPoint). Re-key it to the
    // current skirt target: a different pack on the opposite side must start its
    // own orbit rather than inherit a stale "round left" from the last one.
    DcApproachState& appr =
        context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get();
    if (appr.skirtOrbitTarget != target->GetObjectGuid())
    {
        appr.skirtOrbitTarget = target->GetObjectGuid();
        appr.skirtOrbitDir = 0;
    }

    std::optional<Position> wp = DcEngageGeometry::AggroSafeApproachPoint(
        bot, boss->GetPositionX(), boss->GetPositionY(), boss->GetPositionZ(),
        safeRadius, target, &appr.skirtOrbitDir);
    if (wp)
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] room-clear: skirting {}'s aggro sphere (r={:.1f}) -> "
                  "detour ({:.1f}, {:.1f}) before approaching {}",
                  bot->GetName(), boss->GetName(), safeRadius,
                  wp->GetPositionX(), wp->GetPositionY(), target->GetName());
    return wp;
}

bool DungeonClearEngageActionBase::MoveToSkirtingRoomAggro(Unit* target,
                                                           MovementPriority prio)
{
    if (!target)
        return false;

    float dx = target->GetPositionX();
    float dy = target->GetPositionY();
    float dz = target->GetPositionZ();
    // Room-aggro boss sphere first: it is registry-declared, encounter-critical
    // (waking the boss mid-clear is unrecoverable), and its sphere is usually the
    // biggest thing in the room. En-route pack avoidance is the general case and
    // only gets a say once the boss sphere is clear — two orbits fighting over
    // one destination on the same tick is the bounce both latches exist to stop.
    std::optional<Position> wp = RoomAggroSkirtPoint(target);
    if (!wp)
        wp = DcEngageGeometry::EnRoutePackAvoidPoint(bot, context, target);
    if (wp)
    {
        dx = wp->GetPositionX();
        dy = wp->GetPositionY();
        dz = wp->GetPositionZ();
    }

    bool const moved = DcMoveTo(target->GetMapId(), dx, dy, dz,
                              /*idle*/ false, /*react*/ false,
                              /*normal_only*/ false, /*exact_waypoint*/ false, prio);
    // Own the tick while the move is in flight (a duplicate-move returns false but
    // the bot is still gliding) — mirrors EngageDirect's walk-branch semantics.
    return moved || bot->isMoving() || IsWaitingForLastMove(prio);
}

// At the boss (close, on its floor) AND no anchored intermediate hops remain
// unresolved → stop walking and let the at-boss trigger fire the pull. With
// anchored routes the bot may be geometrically near the boss but still on the
// wrong side of a wall; we keep walking in that case until anchors are cleared.
// Likewise a boss one floor up is 3D-near but not atBoss, so the tank keeps
// walking the route up to its level instead of parking underneath.
bool DungeonClearEngageTrashAction::Execute(Event& /*event*/)
{
    // Pause guard — same already-queued-action race as DungeonClearAdvanceAction.
    if (DcRun::Of(context).paused)
        return false;

    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next.has_value())
        return false;

    ObjectGuid const stickyGuid =
        AI_VALUE(ObjectGuid, DcKey::EngageTrashTarget);
    Unit* sticky = ResolveStickyTrashTarget(bot, stickyGuid);

    // Prefer the wider DC-gated scan — it sees packs at the far end of
    // long dungeon corridors that fall outside the default 100yd
    // sightDistance cap. Falls back to `possible targets` when far-targets
    // is empty (e.g. very first tick, before its 500ms poll has run).
    GuidVector const& farTargets = AI_VALUE(GuidVector, DcKey::FarTargets);
    GuidVector const& possibleTargets = AI_VALUE(GuidVector, DcKey::Stock::PossibleTargets);
    GuidVector const& candidates = farTargets.empty() ? possibleTargets : farTargets;

    Unit* fresh = nullptr;
    if (DC_USE_CORRIDOR_SCAN)
    {
        // Walk the cached long-path polyline. The polyline spans the full
        // chunked route, so blocking trash beyond a single PathGenerator
        // call is still detected. EnsureLongPath wasn't invoked here —
        // Advance refreshes it every tick; this read sees the same value.
        ChunkedPathfinder::Result const& path =
            AI_VALUE(ChunkedPathfinder::Result&, DcKey::LongPath);
        if (path.reachable && !path.segments.empty())
        {
            fresh = DcTargeting::FindBlockingTrashOnPath(
                bot, path.segments, DC_CORRIDOR_LOOKAHEAD, DC_CORRIDOR_WIDTH, candidates);
        }
        else
        {
            // No usable long-path cache — fall back to single-shot corridor.
            Movement::PointsArray corridor;
            if (DcEngageGeometry::ComputeCorridor(bot, next->x, next->y, next->z, corridor))
            {
                fresh = DcTargeting::FindBlockingTrashCorridor(
                    bot, corridor, DC_CORRIDOR_LOOKAHEAD, DC_CORRIDOR_WIDTH, candidates);
            }
        }
    }
    if (!fresh)
    {
        // Final fallback: cone scan, same predicate as the trigger uses.
        // Keeps the engagement path live when path computation can't
        // produce any usable corridor (boss off-mesh, etc.).
        fresh = DcTargeting::FindBlockingTrash(
            bot, *next, DC_TRASH_CONE_RANGE, DC_TRASH_CONE_HALF_ANGLE, candidates);
    }

    // Proximity preempt: a hostile already inside the bot's aggro bubble will
    // pull as the tank moves regardless of whether it sits on the corridor
    // line, so engage the closest such mob first. This is the run-past-the-pack
    // fix — without it the selector commits to an on-line mob farther ahead and
    // the tank charges through any adjacent pack to reach it.
    Unit* const nearestThreat = FindNearestHostileWithin(bot, DC_PROXIMITY_ENGAGE_RANGE, candidates);

    // Target selection:
    //  1. If the current sticky is still inside the aggro bubble, keep it — it's
    //     close and route-stable. Otherwise take the closest in-bubble hostile.
    //  2. Nothing adjacent: fall back to the corridor/cone pick, preferring the
    //     sticky for route stability. The sticky target prevents the per-tick
    //     bounce between two roughly-equidistant corridor mobs. The one
    //     override: a fresh candidate already in combat with the party
    //     (healer/DPS pulled aggro) beats a quiet sticky — "more reason to
    //     engage, not less" — so the tank doesn't wander toward a quiet mob
    //     while the healer gets clawed.
    Unit* target = nullptr;
    if (sticky && bot->GetDistance(sticky) <= DC_PROXIMITY_ENGAGE_RANGE)
        target = sticky;
    else if (nearestThreat)
        target = nearestThreat;

    if (!target)
    {
        target = sticky;
        if (!target)
            target = fresh;
        else if (fresh && fresh != sticky && fresh->IsInCombat() && !sticky->IsInCombat())
            target = fresh;
    }

    if (!target)
    {
        if (!stickyGuid.IsEmpty())
            context->GetValue<ObjectGuid>(DcKey::EngageTrashTarget)->Set(ObjectGuid::Empty);
        return false;
    }

    // Pin the chosen target so the next tick doesn't reconsider it.
    if (target->GetObjectGuid() != stickyGuid)
        context->GetValue<ObjectGuid>(DcKey::EngageTrashTarget)->Set(target->GetObjectGuid());

    // Far, long-route trash: a pack the navmesh can only reach via a winding
    // route that overruns PathGenerator's hop cap (the tank atop a ramp with
    // the pack at its foot, the route running all the way down and back over).
    // The raw MoveTo EngageDirect issues builds only the truncated
    // PATHFIND_INCOMPLETE prefix of that route and dead-ends against the
    // geometry — the tank freezes in "clearing trash" and never reaches aggro
    // range. Hand the approach to Advance's long-path (LongRangePathfinder, no
    // hop cap): it already routes toward the boss straight past this pack, so
    // returning false here yields the tick to it. As the tank descends and the
    // pack comes within a single-call NORMAL route, this action preempts again
    // (proximity/sticky) and pulls it. Gated on distance so ordinary
    // in-corridor pulls keep their direct bee-line; only genuinely far,
    // out-of-aggro targets are deferred.
    //
    // BOUNDED, because "this action preempts again as the tank descends" is only true
    // while Advance is walking us TOWARD the pack. When Advance is routing AWAY from it
    // — a pack beside or behind the line to the next boss — the hand-off is permanent
    // and nothing ever engages a pack DC has already decided to fight. That is how
    // tr-20260804-153254-2 hung: `dynamic verdict for pack <24683/166>: LEEROY` at
    // 15:52:09, then not one engage line for the rest of the run. The tank glided north
    // to Kael'thas across the 98yd empty stretch after the last pack, the abandoned
    // guard held two members in combat from 68yd back, and the run never recovered.
    //
    // So measure the premise instead of assuming it: keep deferring only while the gap
    // is actually CLOSING. A genuine ramp descent improves the straight-line distance
    // several times a second and never trips this; a tank walking away never improves
    // it at all. Once the deferral has stopped paying for DC_LONGROUTE_DEFER_LIMIT
    // ticks we take the tick back and walk in ourselves — but only for a pack the
    // CHUNKED router says is reachable, so a genuinely unreachable pack (across a gap,
    // behind a gate) still falls through to Advance rather than grinding at it.
    DcApproachState& appr =
        context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get();
    float const targetDist = bot->GetDistance(target);
    if (targetDist > DC_ENGAGE_RANGE && !IsDirectlyReachable(bot, target))
    {
        if (appr.longRouteDeferTarget != target->GetObjectGuid())
        {
            appr.longRouteDeferTarget = target->GetObjectGuid();
            appr.longRouteDeferWatch.Reset();
            appr.longRouteDeferBlown = false;
        }
        // Only measure while the deferral is still on offer. Once it has lost, the
        // verdict LATCHES for as long as this stays the target — it cannot be carried
        // by the budget alone, because the walk-in step taken on the tick the budget
        // blows improves the gap, which re-arms the closing test and hands the pack
        // straight back. That would buy one engage tick per DC_LONGROUTE_DEFER_LIMIT
        // with Advance reversing it in between: a stutter, not an engagement.
        if (!appr.longRouteDeferBlown)
        {
            appr.longRouteDeferWatch.TickClosing(targetDist, DC_STUCK_DISPLACEMENT, getMSTime());
            bool const advanceIsClosingTheGap =
                appr.longRouteDeferWatch.stuckTicks < DC_LONGROUTE_DEFER_LIMIT;
            if (advanceIsClosingTheGap ||
                !DcEngageGeometry::IsReachable(bot, target->GetPositionX(),
                                               target->GetPositionY(), target->GetPositionZ()))
                return false;

            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] engage trash: deferred {} to advance for {} ticks at {:.1f}yd "
                     "without closing -> advance is not going there, walking in ourselves",
                     bot->GetName(), target->GetObjectGuid().ToString(),
                     appr.longRouteDeferWatch.stuckTicks, targetDist);
            appr.longRouteDeferBlown = true;
        }
    }
    else
    {
        appr.longRouteDeferTarget.Clear();
        appr.longRouteDeferWatch.Reset();
        appr.longRouteDeferBlown = false;
    }

    // CHASE LEASH. The sticky above is deliberately commit-and-hold — it never
    // releases on distance, because releasing on distance is what made the tank
    // flip-flop between two equidistant mobs. The cost of that is a target which
    // WALKS: the pick is pinned, EngageDirect re-aims at its live position every
    // tick, and the tank follows a patrol wherever its route goes — including back
    // behind other packs, waking every one it passes.
    //
    // The leash keeps the commitment (the sticky is untouched) and takes away the
    // pursuit: while the mob stays near the ground we picked it on, walk in as
    // before; once it recedes past the leash, stand still and let it come back —
    // a patrol is a loop. Own the tick while holding so Advance can't glide the
    // tank past the very pack we are waiting on. GiveUp re-anchors and walks, so
    // this can only ever pace a chase, never refuse one.
    switch (DcEngageGeometry::ChaseLeash(bot, context, target))
    {
        case DungeonClearMath::ChaseVerdict::Hold:
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
            DcFaceIfNeeded(bot, target);
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] engage trash: holding for {} to come back around "
                      "rather than chasing it ({:.1f}yd)",
                      bot->GetName(), target->GetObjectGuid().ToString(),
                      bot->GetExactDist(target));
            return true;
        default:
            break;
    }

    return EngageDirect(target);
}

bool DungeonClearEngageBossAction::Execute(Event& event)
{
    // Pause guard — same already-queued-action race as DungeonClearAdvanceAction.
    if (DcRun::Of(context).paused)
        return false;

    // Pull-ownership guard — the same race, and STRICTLY worse here: this rung
    // outranks advance (30 vs 15), so wherever both baskets are stale this is the
    // one that wins the post-tag tick, and its destination is the boss itself. A
    // trash pull taken inside engage range would become the boss pull.
    // DungeonClearAtBossTrigger already stands down for the maneuver; this is the
    // action-side half it cannot enforce. See DcActionShared::PullOwnsTheTank.
    if (PullOwnsTheTank(bot, context, "engage boss"))
        return false;

    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next.has_value())
        return false;

    Creature* boss = DcTargeting::GetLiveBoss(bot, context, next->entry);
    if (!boss)
    {
        // A boss a pending event must SUMMON (RFD's gong -> Tuten'kash) isn't on
        // the map until the rings bring him up. Don't stall/"Blocked" — yield and
        // let the gong event (relevance 31) hold + ring; once summoned this is
        // non-null and we engage normally.
        if (DcTargeting::HasPendingSummonEvent(bot, context, next->entry))
            return false;
        StallDungeonClear(botAI,
            "Can't reach " + next->name + ": not spawned on this map. Use 'dc skip' to move to the next boss.");
        return false;
    }

    // Wait at Boss: hold here for the human's go-ahead instead of committing
    // the pull. This is deliberately the LAST gate — the trigger already
    // proved the standoff, the pre-clear, the door check and the between-pulls
    // readiness (incl. the Smart Rest top-off), so a resume pulls instantly.
    // Mirrors the door auto-pause: same paused flag, so the whole pause stack
    // (driving-ladder gate, multiplier hold, follower peel-off, status panel,
    // Resume button) applies unchanged. pausedDoor stays empty — this hold
    // must never be auto-resumed by some door opening.
    {
        DcWaitAtBossDecision::Inputs in;
        in.enabled = DcSettings::GetBool(bot, "WaitAtBoss");
        in.nextIsBoss = next->kind == DungeonAnchorKind::Boss;
        in.paused = DcRun::Of(context).paused;
        in.inCombat = bot->IsInCombat();
        in.bossGuid = boss->GetObjectGuid().GetRawValue();
        in.lastWaitedGuid = DcRun::Of(context).waitedBossGuid.GetRawValue();
        if (DcWaitAtBossDecision::Decide(in).shouldAutoPause)
        {
            DcRunState& run = DcRun::Of(context);
            run.waitedBossGuid = boss->GetObjectGuid();
            run.paused = true;
            run.pauseReason =
                "waiting at " + next->name + " \xE2\x80\x94 resume when ready";
            run.pausedDoor = ObjectGuid::Empty;
            if (MotionMaster* mm = bot->GetMotionMaster())
                mm->Clear();
            bot->StopMovingOnCurrentPos();
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] wait-at-boss: holding at {} '{}' for the player's resume",
                     bot->GetName(), boss->GetObjectGuid().ToString(), next->name);
            DcStatusPublisher::SendAddonMessage(
                botAI, "CHAT\tWaiting at " + next->name +
                           " \xE2\x80\x94 hit Resume when your party is ready.");
            botAI->DoSpecificAction("dc status", event, true);
            return true;
        }
    }

    // PULL-BACK boss (BossPullbackRegistry): NEVER walk in. The whole registry
    // exists because the boss's own ground is lethal — EngageDirect would bee-line
    // the tank at Ghaz'an's live position, i.e. out into the Underbog lake, which
    // is the wipe. The pull pipeline (relevance 35, above this rung's 30) owns the
    // engagement and does it by tag-and-drag; reaching here at all means the pull
    // stood down — a fizzle handoff, or the boss out of reach — so HOLD on the
    // anchor and say why, rather than substituting the one behaviour that kills the
    // party. The pull re-arms on its own the next tick it can (the boss wanders
    // back, the abort latch clears at the Engage cleanup).
    if (BossPullbackRegistry::Find(bot->GetMapId(), next->entry))
    {
        DcMovement::StopBot(bot, DcMovement::Stop::Hold);

        // Both Advancing abort paths latch abortTarget to hand the pack to "the
        // normal walk-in engage so the run never hangs" — which for a pull-back
        // boss is the one thing that must not happen, so the pull trigger stands
        // down on the latch and this rung would otherwise hold forever in silence.
        // Keep the never-hang contract, pay it in a STALL: say the pull failed and
        // point at `dc skip`, instead of either swimming out or wedging quietly.
        // The latch clears at the pull's Engage cleanup, and ClearStall on any
        // later successful engage, so a recovered pull resumes normally.
        if (AI_VALUE(DcPullContext&, DcKey::PullContext).abortTarget == boss->GetObjectGuid())
        {
            StallDungeonClear(botAI,
                "Can't pull " + next->name + " back to safe ground (the drag kept "
                "failing). Use 'dc skip' to move to the next boss.");
            return true;
        }

        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] {} is a pull-back boss and the pull pipeline is not "
                  "holding it this tick — waiting on the anchor rather than walking "
                  "in (boss {:.1f}yd away)",
                  bot->GetName(), next->name, bot->GetExactDist(boss));
        return true;
    }

    if (!EngageDirect(static_cast<Unit*>(boss)))
        return false;

    ClearStall(context);
    return true;
}

bool DungeonClearRoomClearAction::Execute(Event& /*event*/)
{
    // Pause guard — same already-queued-action race as DungeonClearAdvanceAction.
    if (DcRun::Of(context).paused)
        return false;

    // Nearest remaining room-trash unit (the value already excludes the boss,
    // other encounter bosses, anything inside the boss's aggro sphere, and
    // unreachable/door-blocked units). EngageDirect walks to ITS attack range —
    // not toward the boss — so the careful nearest-first clear never face-pulls
    // the boss.
    Unit* target = DcTargeting::NearestRoomTrash(bot, context);
    if (!target)
        return false;

    return EngageDirect(target);
}

bool DungeonClearRoomPreClearHoldAction::Execute(Event& /*event*/)
{
    // We only reach this action when NO higher-relevance driver claimed the tick:
    // pull (35) / run event (31) / room-clear (26) / engage trash (25) were all
    // either inactive or returned false. So the choice is purely "hold here" vs
    // "let it fall through to the room-aggro-blind Advance (15), which would creep
    // at the boss centre via the direct-pursuit shortcut."
    //
    // One legitimate fall-through: the tank has its OWN corpse to loot. The loot
    // pipeline sits BELOW us (rel 8-9) and never drives toward the boss, so defer
    // to it — returning false lets Advance's loot-yield + the loot actions run.
    // (Followers looting just means we should wait, which is exactly a hold, so we
    // do NOT defer for that — we hold.)
    if (AI_VALUE(bool, DcKey::Stock::HasAvailableLoot) || AI_VALUE(bool, DcKey::Stock::CanLoot))
        return false;

    // Second legitimate fall-through: the tank is below its OWN rest target and
    // should top up mana/health during this forced standoff. Owning the tick
    // UNCONDITIONALLY (as we did before) starved the stock drink/eat actions
    // (rel ~3) of every between-pull gap here, so a mana-class tank never
    // regained mana between the careful one-pack-at-a-time room-clear pulls — it
    // just stood at the standoff forever ("holding at standoff (no driver this
    // tick)" spamming the log). The DC rest override (DcRel::NeedsRest, 26.5) is
    // the only rung above this hold that can drink, and it is inert unless the
    // run sets RestManaPct/RestHealthPct (both default 0), so by default nothing
    // topped the tank up.
    //
    // Deferring is safe and symmetric with the loot deferral above: while the
    // tank is below its rest target the party-ready gate is false, so Advance
    // (15) yields at TryBetweenPullsRest — which runs BEFORE its direct-pursuit
    // shortcut — exactly the "advance yielding: party not ready / resting" path.
    // Nothing creeps toward the boss; the tank falls through to the stock rest
    // (rel ~3) and drinks in place. We still StopBot(Hold) first to cancel any
    // residual escort glide so it is parked and can actually sit; once stationary
    // that call no-ops (see DcMovement::StopBot) and never interrupts the drink.
    // The standoff invariant is preserved: we only yield when Advance yields too.
    // Smart Rest swaps the "below my rest target" test for the party latch:
    // a tank at e.g. 40% mana (above its low trigger, below the legacy 65)
    // must NOT yield to a drink the multiplier now suppresses — it would stand
    // down forever. Only a latched rest is a real drink opportunity.
    bool lowRes;
    if (DcSmartRest::Enabled(bot))
        lowRes = DcSmartRest::IsLatched(bot);
    else
    {
        // The floors the between-pulls gate is actually holding for. Under a
        // phantom flag they are 0/0 — deferring to a drink that cannot be taken
        // (eating and drinking need out-of-combat) would park the tank at the
        // standoff forever. See DcPartyState::GetRestGate.
        DcPartyState::RestGate const rest = DcPartyState::GetRestGate(bot, context);
        uint32 const maxMana = bot->GetMaxPower(POWER_MANA);
        bool const lowMana = maxMana > 0 && bot->GetPowerPct(POWER_MANA) < rest.minMp;
        bool const lowHealth = bot->GetHealthPct() < rest.minHp;
        lowRes = lowMana || lowHealth;
    }
    if (lowRes)
    {
        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
        ClearStall(context);
        SetPhase(context, "room pre-clear: resting at standoff");
        return false;
    }

    // Otherwise OWN the tick and hold at the standoff. StopBot(Hold) cancels any
    // in-flight escort glide (a plain StopMoving cannot) and tears down a leftover
    // follow, so the tank parks just outside the boss's avoid sphere while the
    // higher drivers work the room one careful pull at a time. This makes the
    // standoff UNCONDITIONAL — independent of whether Advance's own (conditional)
    // engage-hold rung fires — which is the structural fix: the room-aggro-blind
    // Advance can never take a pre-clear tick. The #1 BossEngageRange standoff is
    // now defense-in-depth rather than the sole guard.
    DcMovement::StopBot(bot, DcMovement::Stop::Hold);
    ClearStall(context);
    SetPhase(context, "room pre-clear: holding at standoff");

    // Per-tick DEBUG (consistent with the existing "advance yielding" lines): this
    // is the line that should APPEAR during a pre-clear gap and the boss-creep
    // "pursuing live <boss>" line that should NOT.
    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    LOG_DEBUG("playerbots.dungeonclear",
              "[DC:{}] room pre-clear: holding at standoff (no driver this tick) -> {}",
              bot->GetName(), next.has_value() ? next->name : "boss");
    return true;
}

bool DungeonClearClearStalledAction::Execute(Event& /*event*/)
{
    Unit* target = DcTargeting::FindNearestReachableHostile(bot);
    if (!target)
    {
        // We're stalled with nothing left to kill. Leave the stall reason in
        // place so `dc status` reports it; the player can `dc skip` or `dc off`.
        std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
        std::string const target_name = next.has_value() ? next->name : "the next boss";
        StallDungeonClear(botAI,
            "Stuck near " + target_name + " and no reachable mobs left to clear. "
            "Use 'dc skip' to move on or 'dc off' to stop.");
        context->GetValue<ObjectGuid>(DcKey::FallbackTarget)->Set(ObjectGuid::Empty);
        return false;
    }

    // Announce target on first selection. Suppress repeats while we're still
    // working on the same one.
    ObjectGuid const lastAnnounced =
        context->GetValue<ObjectGuid>(DcKey::FallbackTarget)->Get();
    if (lastAnnounced != target->GetObjectGuid())
    {
        context->GetValue<ObjectGuid>(DcKey::FallbackTarget)->Set(target->GetObjectGuid());
        DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tClearing path \xe2\x80\x94 pulling " + std::string(target->GetName()) + ".");
    }

    // Don't clear the stall reason here — only a successful Advance does that.
    // If pathing to the boss is still blocked after this kill, we want the
    // stall trigger to fire again next tick.
    return EngageDirect(target);
}

namespace
{
    // --- EscortCreature (Wailing Caverns' Disciple of Naralex) helpers -------

    // Friendly template faction the Disciple carries while IDLE at spawn (creature
    // template 3678, faction 35). His SmartAI flips it to 250 the instant the
    // gossip starts the escort, and back to 35 if he dies and respawns — so this
    // is the single "needs (re)starting" signal the step keys self-heal on.
    constexpr uint32 DC_ESCORT_IDLE_FACTION = 35;

    // The escort owns the tick by FOLLOWING; these size that follow.
    constexpr float DC_ESCORT_FOLLOW_ARRIVE = 3.0f;   // hold once within this of the slot
    constexpr float DC_ESCORT_KEEPUP_SLACK = 10.0f;   // "keeping up" = within standoff+this
    // Dead-air window: stall only after this long FAILING to keep up with the
    // escortee (NOT a flat step timeout — a legitimate long pause keeps the tank
    // parked right next to the escortee, which reads as "keeping up" and never
    // trips this). Generous so a brief stutter never mis-fires.
    constexpr uint32 DC_ESCORT_DEAD_AIR_MS = 15000;

    // How long the escortee must be combat-wedged (in combat, yet zero attackers
    // and zero valid attack targets on its threat list) before the driver force-
    // clears its combat. Long enough that a real fight's transition gaps (wave
    // spawn delay, target mid-death) never trip it; short enough to beat the
    // dead-air watchdog so the wedge self-heals instead of stalling the run.
    constexpr uint32 DC_ESCORT_COMBAT_WEDGE_MS = 5000;

    // Mirror the escortee's run speed onto every BOT in the party (the leader
    // included) so the party keeps up when the escortee mounts and rides off. Old
    // Hillsbrad's Thrall gallops to Tarren Mill at 1.6x and his npc_escortAI HARD-
    // RESETS him if no party member stays within 100yd (DEFAULT_MAX_PLAYER_DISTANCE)
    // — so without this the ride resets the escort every time. Never touches a
    // human (client desync).
    //
    // rate > 1: FORCE-match every bot (the keep-up requirement outranks whatever
    // speed auras they carry). rate <= 1: RESTORE via UpdateSpeed, which recomputes
    // the rate from the bot's real auras — dropping the non-aura escort boost while
    // PRESERVING a legitimate speed buff (Cheetah, Ghost Wolf), which a flat
    // SetSpeed(1.0) would stomp every tick for the whole unmounted escort. Both
    // paths are idempotent per tick (SetSpeed early-outs on an unchanged rate), so
    // this is a no-op for an un-mounted escort (Wailing Caverns' Disciple).
    void ApplyEscortPartyRunSpeed(Player* leader, float rate)
    {
        if (!leader)
            return;
        auto apply = [rate](Player* p)
        {
            if (!p || !p->IsInWorld() || !GET_PLAYERBOT_AI(p))
                return;
            if (rate > 1.01f)
            {
                if (std::fabs(p->GetSpeedRate(MOVE_RUN) - rate) > 0.01f)
                    p->SetSpeed(MOVE_RUN, rate, /*forced*/ true);
            }
            else
                p->UpdateSpeed(MOVE_RUN, /*forced*/ true);
        };
        apply(leader);
        Group* group = leader->GetGroup();
        if (!group)
            return;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == leader)
                continue;
            if (member->GetMapId() != leader->GetMapId())
                continue;
            apply(member);
        }
    }

    // True once the escort's final boss exists (grid scan) or its encounter bit
    // is set — mirrors the RunStep gate so the action and the gate agree. Also
    // completes on the map's monotonic progress counter reaching a threshold
    // (Old Hillsbrad's Thrall escort ends on DATA_ESCORT_PROGRESS == FINISHED,
    // not on a boss going live).
    bool EscortComplete(Player* bot, EventStep const& step)
    {
        if (step.escortDoneEntry &&
            bot->FindNearestCreature(step.escortDoneEntry, 250.0f, /*alive*/ true))
            return true;
        if (step.escortDoneBit >= 0)
        {
            InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
            if (inst && (inst->GetCompletedEncounterMask() &
                         (1u << static_cast<uint32>(step.escortDoneBit))))
                return true;
        }
        if (step.instanceDataId >= 0)
        {
            InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
            if (inst && inst->GetData(static_cast<uint32>(step.instanceDataId)) >=
                            step.instanceDataMin)
                return true;
        }
        return false;
    }

    // The threat to engage to protect the escortee, or nullptr: prefer a mob
    // hitting him RIGHT NOW (the ambushes spawn on him, so it's co-located and
    // safe to reach), else the nearest hostile in a TIGHT radius around him.
    // Strict reachability + LOS before returning an attacker (the winding tunnels
    // wedge a bare-proximity pick against a wall — the Uldaman Grimlok lesson);
    // NearestHostileNearPoint already applies those filters for the scan path.
    Unit* PickEscortThreat(Player* bot, AiObjectContext* ctx, Creature* escortee,
                           EventStep const& step)
    {
        for (Unit* att : escortee->getAttackers())
        {
            if (!att || !att->IsAlive())
                continue;
            if (!bot->IsValidAttackTarget(att))
                continue;
            if (!DcEngageGeometry::IsEngageReachable(bot, att, /*requireDirect*/ true))
                continue;
            if (!bot->IsWithinLOSInMap(att))
                continue;
            return att;
        }

        float const r = step.escortThreatRadius > 0.0f ? step.escortThreatRadius : 18.0f;
        return DcTargeting::NearestHostileNearPoint(bot, ctx, escortee->GetPositionX(),
                                                    escortee->GetPositionY(),
                                                    escortee->GetPositionZ(), r, step.zBand);
    }

    // Distance-to-escortee progress watchdog. While the tank is keeping up (close
    // to the escortee) the clock resets and any stall clears; only sustained
    // failure to keep up (the escortee ran off and the tank can't follow) trips a
    // visible stall the player can `dc skip`. Auto-clears the moment it catches up.
    void EscortWatchdog(PlayerbotAI* botAI, AiObjectContext* ctx,
                        DungeonEventProgress& prog, uint32 now, bool keepingUp,
                        std::string const& whom)
    {
        if (keepingUp || prog.escortProgressMs == 0)
        {
            prog.escortProgressMs = now;
            ClearStall(ctx);
            return;
        }
        if (static_cast<uint32>(now - prog.escortProgressMs) >= DC_ESCORT_DEAD_AIR_MS)
            StallDungeonClear(botAI, "Escort stalled: I can't keep up with " + whom +
                                         ". Clear my path and I'll continue, or `dc skip`.");
    }
}

bool DungeonClearEngageActionBase::DriveEscortCreature(EventStep const& step,
                                                       DungeonEventProgress& prog)
{
    uint32 const now = getMSTime();

    // Final boss exists -> escort is over. Don't own the tick: the caller falls
    // through to Drive, whose RunStep gate returns Done and latches the objective.
    if (EscortComplete(bot, step))
    {
        ApplyEscortPartyRunSpeed(bot, 1.0f);  // drop any mounted-ride speed boost
        return false;
    }

    float const searchR = step.radius > 0.0f ? step.radius : 80.0f;
    Creature* escortee = bot->FindNearestCreature(step.creatureEntry, searchR, /*alive*/ true);

    // Escortee absent (died and mid-respawn, or briefly out of the grid). Hold and
    // let the watchdog flag a genuine never-returns stall; the start branch below
    // re-runs the gossip once he reappears idle at spawn. Drop any ride boost so a
    // brief loss mid-ride can't strand a bot at 1.6x.
    if (!escortee)
    {
        ApplyEscortPartyRunSpeed(bot, 1.0f);
        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
        SetPhase(context, "escort");
        EscortWatchdog(botAI, context, prog, now, /*keepingUp*/ false, "the escort");
        return true;
    }

    // Keep the party at the escortee's pace so a mounted ride never outruns it (and
    // resets his npc_escortAI). Mirrors his live run rate: 1.0 on foot, 1.6 mounted.
    ApplyEscortPartyRunSpeed(bot, escortee->GetSpeedRate(MOVE_RUN));

    // COMBAT-WEDGE UNSTICK — upstream azerothcore-wotlk#25617: at the Durnholde
    // armory, Thrall's scripted Knockout (spell 32890) on the UNATTACKABLE
    // Durnholde Armorer drags him into combat with a unit nobody can hit or
    // kill; escort AIs never advance waypoints while in combat, so his script
    // deadlocks there. Detect the wedge generically instead of hardcoding the
    // armorer: the escortee has been IN COMBAT for a debounced 5s with NO
    // attacker and NO valid attack target on its threat list — a real fight
    // (ambush waves, Skarloc, the Epoch adds) always has one of the two, which
    // resets the clock every tick. Then clear BOTH sides' combat and evade the
    // escortee, whose escort-AI EnterEvadeMode override resumes the waypoint
    // path from where it stopped. Gated to progress-driven escorts
    // (instanceDataId >= 0) so the WC Mutanus escort stays untouched.
    if (step.instanceDataId >= 0 && escortee->IsInCombat())
    {
        bool anyValid = !escortee->getAttackers().empty();
        std::vector<Unit*> wedgedTargets;
        if (!anyValid)
        {
            for (ThreatReference const* ref : escortee->GetThreatMgr().GetUnsortedThreatList())
            {
                Unit* t = ref->GetVictim();
                if (!t)
                    continue;
                if (escortee->IsValidAttackTarget(t))
                {
                    anyValid = true;
                    break;
                }
                wedgedTargets.push_back(t);
            }
        }
        if (anyValid)
            prog.escortCombatWedgeMs = 0;
        else if (!prog.escortCombatWedgeMs)
            prog.escortCombatWedgeMs = now;
        else if (getMSTimeDiff(prog.escortCombatWedgeMs, now) >= DC_ESCORT_COMBAT_WEDGE_MS)
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[dungeon-clear] {}: escortee {} combat-wedged with {} unattackable "
                     "target(s) (upstream #25617) -> clearing combat and resuming the escort",
                     bot->GetName(), escortee->GetName(), wedgedTargets.size());
            for (Unit* t : wedgedTargets)
            {
                t->GetThreatMgr().ClearAllThreat();
                t->CombatStop(true);
            }
            escortee->GetThreatMgr().ClearAllThreat();
            escortee->CombatStop(true);
            if (escortee->AI())
                escortee->AI()->EnterEvadeMode();
            prog.escortCombatWedgeMs = 0;
            prog.escortProgressMs = now;  // unsticking IS progress
            ClearStall(context);
            return true;
        }
    }
    else
        prog.escortCombatWedgeMs = 0;

    // (Re)start: idle at spawn (never started, or reset to idle after dying). Walk
    // to gossip range and start his scripted escort. Folded into the step (not a
    // separate step-0 Gossip) so a Persistent event — which never auto-rewinds to
    // step 0 — still recovers when he dies mid-escort. Idempotent: once started his
    // faction is 250 and this is skipped.
    if (escortee->GetFaction() == DC_ESCORT_IDLE_FACTION)
    {
        SetPhase(context, "escort");
        if (!bot->IsWithinDistInMap(escortee, 5.0f))
        {
            DcMoveTo(bot->GetMapId(), escortee->GetPositionX(), escortee->GetPositionY(),
                     escortee->GetPositionZ(), /*idle*/ false, /*react*/ false,
                     /*normal_only*/ false, /*exact_waypoint*/ false,
                     MovementPriority::MOVEMENT_NORMAL);
            EscortWatchdog(botAI, context, prog, now, /*keepingUp*/ true, escortee->GetName());
            return true;
        }
        DcMovement::StopBot(bot, DcMovement::Stop::Soft);
        if (DungeonEventExecutor::SelectGossip(bot, escortee, step.gossipOption))
            LOG_INFO("playerbots.dungeonclear",
                     "[dungeon-clear] {} started the escort of {} (gossip option {})",
                     bot->GetName(), escortee->GetName(), step.gossipOption);
        prog.escortProgressMs = now;  // starting the escort IS progress
        ClearStall(context);
        return true;
    }

    // RESUME a paused, gossip-offering escortee. Old Hillsbrad's Thrall is not the
    // WC idle-faction model: he keeps his real faction (1747) the whole way and
    // instead raises UNIT_NPC_FLAG_GOSSIP at every checkpoint — freed from his cell,
    // after Skarloc, outside the barn, at Taretha — and again after a death-reset
    // (his AI repositions him to the last checkpoint, paused, awaiting a re-talk).
    // Selecting his gossip routes by the instance's progress (release / mount-up /
    // start-the-waves / Epoch cutscene), advances DATA_ESCORT_PROGRESS, and clears
    // the flag — so this fires once per checkpoint and never loops. Gated on this
    // being a progress-completed escort (instanceDataId >= 0) so the boss-gated WC
    // escort is entirely unaffected. Requires him STILL so a mid-walk gossip (which
    // is a no-op / could interrupt his path) is never attempted.
    if (step.instanceDataId >= 0 && step.gossipOption >= 0 &&
        escortee->HasNpcFlag(UNIT_NPC_FLAG_GOSSIP) && !escortee->isMoving())
    {
        SetPhase(context, "escort");
        if (!bot->IsWithinDistInMap(escortee, 5.0f))
        {
            DcMoveTo(bot->GetMapId(), escortee->GetPositionX(), escortee->GetPositionY(),
                     escortee->GetPositionZ(), /*idle*/ false, /*react*/ false,
                     /*normal_only*/ false, /*exact_waypoint*/ false,
                     MovementPriority::MOVEMENT_NORMAL);
            EscortWatchdog(botAI, context, prog, now, /*keepingUp*/ true, escortee->GetName());
            return true;
        }
        DcMovement::StopBot(bot, DcMovement::Stop::Soft);
        if (DungeonEventExecutor::SelectGossip(bot, escortee, step.gossipOption))
            LOG_INFO("playerbots.dungeonclear",
                     "[dungeon-clear] {} resumed the escort of {} at a checkpoint "
                     "(gossip option {})",
                     bot->GetName(), escortee->GetName(), step.gossipOption);
        prog.escortProgressMs = now;  // resuming IS progress
        ClearStall(context);
        return true;
    }

    // Threat engage: a mob attacking the escortee, else a tight scan around him.
    // EngageDirect flips the tank to the combat engine; the followers then pile in
    // via IsLeaderFightAssistWanted (fires on ANY party-member combat — no follower
    // needs to be targeted, which is exactly why a non-party escortee can be
    // defended at all).
    if (Unit* threat = PickEscortThreat(bot, context, escortee, step))
    {
        DcMovement::ResolveEscortConflict(bot);
        SetPhase(context, "escort");
        prog.escortProgressMs = now;  // engaging a threat IS progress
        ClearStall(context);
        EngageDirect(threat);
        return true;
    }

    // Else FOLLOW a slot behind the escortee (offset so the tank never blocks his
    // movement spline). Metric-matched arrival + hysteresis: the "arrived?" test
    // and the MoveTo target are the SAME slot point (the scout-lag lesson — a test
    // and target on different metrics oscillate). Hold (don't shove) once parked
    // within the slot, so a paused escortee isn't pushed off his waypoint.
    float const standoff = step.escortStandoff > 0.0f ? step.escortStandoff : 5.0f;
    float const o = escortee->GetOrientation();
    float const fx = escortee->GetPositionX() - std::cos(o) * standoff;
    float const fy = escortee->GetPositionY() - std::sin(o) * standoff;
    float const fz = escortee->GetPositionZ();
    float const toSlot = bot->GetExactDist(fx, fy, fz);

    SetPhase(context, "escort");
    if (toSlot > DC_ESCORT_FOLLOW_ARRIVE)
        DcMoveTo(bot->GetMapId(), fx, fy, fz, /*idle*/ false, /*react*/ false,
                 /*normal_only*/ false, /*exact_waypoint*/ false,
                 MovementPriority::MOVEMENT_NORMAL);
    else
        DcMovement::StopBot(bot, DcMovement::Stop::Soft);

    bool const keepingUp = bot->GetExactDist(escortee) <= standoff + DC_ESCORT_KEEPUP_SLACK;
    EscortWatchdog(botAI, context, prog, now, keepingUp, escortee->GetName());
    return true;
}

bool DungeonClearEngageActionBase::DriveDropInHole(EventStep const& step)
{
    if (!bot)
        return false;

    // Already down on the deep floor -> hand back. The caller drops through to
    // Drive, whose RunStep gate pulls the stranded followers and latches Done.
    if (DungeonEventExecutor::IsOnDropLanding(bot, step))
        return false;

    MotionMaster* mm = bot->GetMotionMaster();

    // Mid-fall: the pure-vertical MoveFall is in flight (its EFFECT generator is
    // still running). Let gravity finish — keep owning the tick so the at-objective
    // Hold can't interrupt the descent. Test the GENERATOR, not MOVEMENTFLAG_FALLING
    // (a server bot never clears that flag, so it would read "falling" forever).
    bool const falling =
        mm && mm->GetCurrentMovementGeneratorType() == EFFECT_MOTION_TYPE;
    if (falling)
        return true;

    SetPhase(context, "objective");

    // How close (2D) the leader must be to the over-hole nudge target before we
    // commit the fall — tight, so the column below really is the open shaft (the
    // landing X/Y), not the shelf or the mid-shaft ledge a yard short of it.
    constexpr float DC_DROP_OVERHOLE_RADIUS = 2.5f;
    float const overDist2d = bot->GetExactDist2d(step.x, step.y);

    // Settled OVER the open hole-mouth at shelf height -> commit the drop. MoveFall
    // searches straight down (vmap) and falls to the first floor it finds; because
    // we are over the open shaft (not the lip), that floor is the deep water, not
    // the intermediate ledge. Pure-vertical => no horizontal travel => no wall clip.
    if (overDist2d <= DC_DROP_OVERHOLE_RADIUS && !bot->isMoving())
    {
        mm->MoveFall();
        LOG_INFO("playerbots.dungeonclear",
                 "[dungeon-clear] {} DropInHole: MoveFall from ({:.1f},{:.1f},{:.1f})",
                 bot->GetName(), bot->GetPositionX(), bot->GetPositionY(),
                 bot->GetPositionZ());
        return true;
    }

    // Still on/at the lip. Glide OUT over the open hole-mouth with a raw spline:
    // MoveSplinePath feeds the two points straight to an EscortMovementGenerator
    // (no per-point navmesh path generation), so it leaves the mesh edge instead
    // of clamping to it. The leg is a few yards of OPEN AIR above the shaft mouth
    // (no wall there — it's the opening), held at shelf Z; the MoveFall above then
    // takes over. Re-issue only when settled, so a launched glide isn't restarted.
    if (!bot->isMoving())
    {
        Movement::PointsArray pts;
        pts.push_back(G3D::Vector3(bot->GetPositionX(), bot->GetPositionY(),
                                   bot->GetPositionZ()));
        pts.push_back(G3D::Vector3(step.x, step.y, step.z));
        mm->MoveSplinePath(&pts, FORCED_MOVEMENT_NONE);
        LOG_INFO("playerbots.dungeonclear",
                 "[dungeon-clear] {} DropInHole: glide to hole-mouth "
                 "({:.1f},{:.1f},{:.1f})",
                 bot->GetName(), step.x, step.y, step.z);
    }
    return true;
}

namespace
{
    // Is `p` below the given HP / mana rest floors? A non-mana class (warrior /
    // rogue tank) has GetMaxPower(POWER_MANA) == 0 and is never held on mana. A
    // 0 threshold means "don't gate on this resource".
    bool BotBelowRest(Player* p, float minHpPct, float minMpPct)
    {
        if (!p)
            return false;
        if (minHpPct > 0.0f && p->GetHealthPct() < minHpPct)
            return true;
        if (minMpPct > 0.0f && p->GetMaxPower(POWER_MANA) > 0 &&
            p->GetPowerPct(POWER_MANA) < minMpPct)
            return true;
        return false;
    }
}

DungeonClearEngageActionBase::EventRest DungeonClearEngageActionBase::EventRestDecision()
{
    // Combat owns its own ticks, and an in-flight plant/gossip cast must never be
    // interrupted mid-channel — in either state the tank keeps driving/fighting.
    if (!bot || bot->IsInCombat() || bot->IsNonMeleeSpellCast(false))
        return EventRest::None;

    // Post-combat rez recovery: a dead same-map member holds the drive outright.
    // Checked AHEAD of the Smart Rest branch split (and outside it) so it binds
    // with SmartRest=0, the default — engage/Wait-at-Boss/event set-pieces can
    // never fire over a corpse while a recovery is in progress.
    if (DcRezRecovery::IsPending(bot))
        return EventRest::Hold;

    if (DcSmartRest::Enabled(bot))
    {
        // SmartRest owns the whole decision. Refresh the party-wide hysteresis
        // latch here — this is its one update site during an event (the between-
        // pulls gate that normally drives it is dormant inside a set-piece), and
        // UpdateLatch is idempotent within a tick. While latched everyone tops to
        // full, so the tank rests until it hits 100/100; once it's full but a
        // groupmate isn't, hold in place so the party finishes.
        bool const latched = DcSmartRest::UpdateLatch(bot, context);
        if (!latched)
            return EventRest::None;
        return BotBelowRest(bot, 100.0f, 100.0f) ? EventRest::Yield : EventRest::Hold;
    }

    // Legacy per-target rest: the run set RestHealthPct / RestManaPct. Spread is
    // the cohesion gate's job, so measure rest ONLY (effectively-infinite spread).
    DcPartyState::RestGate const rest = DcPartyState::GetRestGate(bot, context);
    float const minHp = rest.minHp;
    float const minMp = rest.minMp;
    // No rest target configured -> never pause. Also the phantom-flag answer: the
    // floors are 0/0 there because no one can eat or drink, so a set-piece must
    // not stop for a rest that can never happen.
    if (minHp <= 0.0f && minMp <= 0.0f)
        return EventRest::None;
    if (BotBelowRest(bot, minHp, minMp))
        return EventRest::Yield;
    if (!DcPartyState::IsPartyReady(bot, minHp, minMp, /*maxSpread*/ 100000.0f))
        return EventRest::Hold;
    return EventRest::None;
}

bool DungeonClearEngageActionBase::DriveUseItemOnGO(EventStep const& step)
{
    if (!bot || step.goEntry == 0)
        return false;

    bool const haveAnchor = step.x != 0.0f || step.y != 0.0f || step.z != 0.0f;
    // Interaction reach (mirrors the executor's DC_EVENT_GO_PLANT_REACH — keep in
    // sync): STRICT world distance, well inside the GO interact box the item-use
    // range-checks against (live-measured failing at 6.0yd), so the tank plants
    // from AT the barrel — the forced walk-in below can always deliver that.
    float const castRange = step.radius > 0.0f ? step.radius : 3.5f;

    // Resolve the target GO nearest the step's anchor, capped at 25yd of it (same
    // pick as RunStep's DC_EVENT_GO_ANCHOR_MATCH — keep the two in sync). The cap
    // matters for POOLED spawns (Old Hillsbrad: one barrel per house, at a random
    // one of 3 candidate spots ≤21yd from the house-centroid anchor, neighbour
    // house ≥38yd): without it a scan that only sees a neighbour's barrel matches
    // it and this driver walks the tank to the wrong house.
    std::list<GameObject*> gos;
    bot->GetGameObjectListWithEntryInGrid(gos, step.goEntry, 80.0f);
    GameObject* target = nullptr;
    float best = 1e18f;
    for (GameObject* g : gos)
    {
        if (!g)
            continue;
        float const d = haveAnchor ? g->GetExactDist(step.x, step.y, step.z)
                                   : g->GetExactDist(bot);
        if (haveAnchor && d > 25.0f)
            continue;  // another house's barrel — never this step's target
        if (d < best)
        {
            best = d;
            target = g;
        }
    }

    // Already planted (a landed plant leaves the goober GO_ACTIVATED for its
    // 86400s autoclose) -> don't walk to it at all; RunStep's success latch
    // reports the step Done from wherever the tank stands.
    if (target && target->getLootState() != GO_READY)
        return false;

    // PARTY-COHESION GATE. Unlike the normal advance (spread gate) and the escort
    // (speed-match + slot-follow), this drive had NO cohesion discipline: the tank
    // sprinted house-to-house — the ~170yd Old Hillsbrad courtyard->prison-yard leg
    // especially — leaving followers pinned on courtyard guards while the status
    // panel (which reads this very spread gate) reported "Waiting for the party to
    // recover". That contradiction ("mod says it can't continue, yet the tank runs
    // off alone to plant") is the reported bug. Hold at the current spot whenever
    // the party is out of spread so the group closes up, then resume — a natural
    // leapfrog at each house boundary. Spread-only (minHp/minMp = 0): HP/mana
    // recovery is Smart Rest's job at boss pulls, not between barrels. A lone/duo
    // tank passes trivially (IsPartyReady is vacuously true with no stragglers) so
    // this never self-deadlocks. Applied ONLY to the sustained TRAVEL legs below —
    // never the final in-reach plant or the doorway-threading detour — so a tank
    // essentially at a barrel always completes the plant even if the party lags.
    //
    // Measured through GetSpreadGate, NOT raw tank distance: in pull mode
    // hold-at-camp PINS the followers at the camp, which legitimately sits
    // PullSetback (40 > PartyMaxSpread 25) behind the tank — a tank-anchored
    // check can then never pass, and the tank holds for a party that has been
    // ordered to stand out of range of it. Same circular gate the between-pulls
    // spread check was fixed for; "caught up" here also means "set at the camp
    // they were told to hold" (with GetSpreadGate's own stale-camp/runaway
    // backstop still enforcing an absolute cap on the tank gap).
    auto partyLagging = [&]() -> bool
    {
        DcPartyState::SpreadGate const gate = DcPartyState::GetSpreadGate(bot, context);
        return !DcPartyState::IsPartyReady(bot, /*minHp*/ 0.0f, /*minMp*/ 0.0f,
                                           gate.maxSpread, gate.anchor, gate.maxTankGap);
    };
    auto holdForParty = [&]() -> bool
    {
        SetPhase(context, "objective");
        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
        return true;
    };

    // The step's GO isn't in scan range yet (a far house whose grid hasn't
    // streamed in): OWN THE TICK and drive the ANCHOR approach with the same
    // sustained navigation, instead of handing back to the Hold+HopTo path whose
    // per-tick stop/re-issue stutter-walks the whole 170yd to Durnholde's north
    // houses. Once at the anchor with still no GO, hand back so RunStep (and its
    // step timeout) own the "pooled spawn missing" case.
    if (!target && haveAnchor && bot->GetExactDist(step.x, step.y, step.z) > 6.0f)
    {
        if (partyLagging())
            return holdForParty();
        SetPhase(context, "objective");
        DcRecordBreadcrumb(context, bot);  // followers inherit the centered trail
        DcMoveTo(bot->GetMapId(), step.x, step.y, step.z, /*idle*/ false, /*react*/ false,
                 /*normal_only*/ false, /*exact_waypoint*/ false,
                 MovementPriority::MOVEMENT_NORMAL);
        return true;
    }

    if (!target)
        return false;  // RunStep HopTo's the anchor to load it

    float const gap = bot->GetExactDist(target);
    bool const los = DungeonEventExecutor::HasGameObjectLos(bot, target);

    // Arrived: strictly in reach AND vmap-visible (RunStep's exact predicate) ->
    // hand back to Drive; RunStep fires the GO.
    if (gap <= castRange && los)
        return false;

    // FINAL WALK-IN — only with LINE OF SIGHT. The navmesh thins out at house
    // walls (agent-radius inflation), so the nav move below can run dry on the
    // nearest mesh point just OUTSIDE cast reach — and no amount of re-issuing
    // closes the gap, because NOTHING in the stock movement stack forces its
    // destination (MoveTo/DoMovePoint/HopTo all pass forceDestination=false;
    // live deadlock: tank parked ~6yd out until the step timed out). Once any
    // move has landed this close but still out of reach, walk the last yards on
    // a FORCED straight-appended spline to the barrel itself. LOS-gated so this
    // can never force the tank INTO a wall the barrel sits behind — 3D distance
    // is blind to thin house walls (second live deadlock).
    if (gap <= 15.0f && los)
    {
        SetPhase(context, "objective");
        if (bot->isMoving())
            return true;  // a move (nav or forced) is still running — let it land
        if (!DcMovement::DcMovementAllowed(botAI))
            return false;
        DcMovement::ResolveEscortConflict(bot);
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MovePoint(0, target->GetPositionX(), target->GetPositionY(),
                                          target->GetPositionZ(), FORCED_MOVEMENT_NONE,
                                          0.0f, 0.0f, /*generatePath*/ true,
                                          /*forceDestination*/ true);
        return true;
    }

    // Close but WALLED OFF (no LOS): the barrel is on the other side of a wall,
    // so never walk (or cast) toward it directly — navigate to the step's anchor
    // (the house's candidate centroid ~= its interior/doorway area) until the
    // doorway opens line of sight, then the walk-in above takes over.
    if (gap <= 15.0f && !los && haveAnchor &&
        bot->GetExactDist(step.x, step.y, step.z) > 4.0f)
    {
        SetPhase(context, "objective");
        DcMoveTo(bot->GetMapId(), step.x, step.y, step.z, /*idle*/ false, /*react*/ false,
                 /*normal_only*/ false, /*exact_waypoint*/ false,
                 MovementPriority::MOVEMENT_NORMAL);
        return true;
    }

    // Sustained travel leg to a known-but-distant barrel: keep the party together
    // (see the cohesion gate above) before committing to the walk.
    if (partyLagging())
        return holdForParty();

    // OWN THE TICK: drive a clean, sustained navigation to the barrel via the DC
    // movement system, so the at-objective StopBot(Hold) — which cancels a plain
    // MovePoint spline every tick — never chops the path and the tank actually
    // threads the house doorway to the barrel inside.
    SetPhase(context, "objective");
    DcRecordBreadcrumb(context, bot);  // followers inherit the centered trail
    DcMoveTo(bot->GetMapId(), target->GetPositionX(), target->GetPositionY(),
             target->GetPositionZ(), /*idle*/ false, /*react*/ false,
             /*normal_only*/ false, /*exact_waypoint*/ false,
             MovementPriority::MOVEMENT_NORMAL);
    return true;
}

bool DcObjectiveArriveAction::Execute(Event& /*event*/)
{
    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next.has_value() || next->kind != DungeonAnchorKind::Objective)
        return false;

    // We own the leader's tick for as long as this objective runs — including
    // event steps that walk the tank a long way (Durnholde's barrels are ~170yd of
    // courtyard). Keep the party's camp trailing us, exactly as the advance rung
    // does: without it the camp freezes wherever the anchor navigation dropped us,
    // the followers stay pinned there (hold-at-camp, relevance 28), and the
    // cohesion gate below waits on a party that has been told not to come. Live
    // in heroic Old Hillsbrad that stranded the party a terrace above the tank and
    // froze the run — stranded-recovery teleported everyone forward every 60s and
    // hold-at-camp walked them straight back to the stale camp.
    DcPullPlanner::MaintainScoutCamp(botAI, context);

    DungeonEvent const* ev =
        next->eventId ? DungeonEventRegistry::Find(next->mapId, next->eventId) : nullptr;

    // An ENGAGE step (KillCreature with .engage) drives the engage pipeline so the
    // tank actively seeks out and fights the named creature — possibly far from the
    // anchor (down ZulFarrak's stairs to the temple bosses) — instead of merely
    // gating on its death while held. While a live target of the active step's
    // entry exists, EngageDirect it (long-range walk-in + combat) rather than
    // holding; combat owns the tick once aggroed (this non-combat action stops
    // running), so this only initiates/continues the approach between fights. Once
    // none remain alive we fall through to Drive, whose KillCreature gate reports
    // Done and advances the step.
    if (ev)
    {
        auto& prog =
            context->GetValue<DungeonEventProgress&>(DcKey::EventProgress)->Get();
        uint32 const idx = (prog.eventId == ev->id) ? prog.stepIndex : 0;
        if (idx < ev->steps.size())
        {
            EventStep const& step = ev->steps[idx];
            // A GARRISON MoveTo that has already arrived is a CAMP: the tank stands
            // on the spot for as long as the gate takes, which on a wave gauntlet
            // means the whole encounter. This rung sits at DcRel::AtObjective (30),
            // above NeedsRest (26.5), so without an explicit yield the tank alone
            // can never eat or drink between waves — the followers already can (the
            // rest trigger defers them until they have regrouped on the tank, see
            // DungeonClearTriggers' persistent-event branch), which is how a party
            // ends up camped with a full group and an empty-mana tank. Same fix,
            // and the same Yield/Hold pair, as the UseItemOnGO branch below.
            //
            // Only once ARRIVED: while still walking in, the move is the work.
            // Excluded when the garrison carries a WhileHolding hook (the Ring of
            // Law): there the hold's per-tick hook is the only thing that notices
            // the encounter resetting behind the party, so it must not be yielded.
            bool const garrisoned =
                step.kind == EventStepKind::MoveTo && step.hookId == 0 &&
                (step.creatureEntry != 0 || step.instanceDataId >= 0 ||
                 step.persistentDataId >= 0) &&
                bot->GetExactDist(step.x, step.y, step.z) <=
                    (step.radius > 0.0f ? step.radius : 4.0f);
            if (garrisoned)
            {
                switch (EventRestDecision())
                {
                    case EventRest::Yield:
                        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
                        SetPhase(context, "objective");
                        ClearStall(context);
                        return false;  // NeedsRest (26.5) wins -> the tank drinks/eats
                    case EventRest::Hold:
                        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
                        SetPhase(context, "objective");
                        ClearStall(context);
                        return true;   // own the tick; wait for the party to recover
                    case EventRest::None:
                        break;
                }
            }
            if (step.kind == EventStepKind::KillCreature && step.engage && step.creatureEntry)
            {
                float const search = step.radius > 0.0f ? step.radius : 250.0f;
                if (Creature* target =
                        bot->FindNearestCreature(step.creatureEntry, search, /*alive*/ true))
                {
                    // FindNearestCreature is a flat 2D scan that can return an
                    // instance of the entry across a wall / on another level. Only
                    // engage one we can actually reach (a complete on-level route);
                    // requireDirect=false keeps a legitimately FAR seek alive, since
                    // the seek IS this objective's navigation.
                    if (!DcEngageGeometry::IsEngageReachable(bot, target, /*requireDirect*/ false))
                        return false;
                    // ResolveEscortConflict cancels a launched escort glide but
                    // leaves our own approach move intact (unlike StopBot(Hold)).
                    DcMovement::ResolveEscortConflict(bot);
                    SetPhase(context, "objective");
                    return EngageDirect(target);
                }
            }
            // ClearRadius: a point-anchored room pre-clear. Engage the nearest
            // reachable hostile inside the centre's radius/floor band (any entry);
            // combat owns the tick once aggroed, so this just initiates/continues
            // the approach between fights. Once none remain the Drive gate below
            // reports Done and the objective latches.
            else if (step.kind == EventStepKind::ClearRadius)
            {
                float const r = step.radius > 0.0f ? step.radius : 50.0f;
                if (Unit* target = DcTargeting::NearestHostileNearPoint(
                        bot, context, step.x, step.y, step.z, r, step.zBand,
                        &step.entryFilter))
                {
                    DcMovement::ResolveEscortConflict(bot);
                    SetPhase(context, "objective");
                    return EngageDirect(target);
                }
            }
            // EscortCreature: the step OWNS the tick (follow the escortee + engage
            // its attackers + self-heal gossip + watchdog) the whole way to the
            // final boss. DriveEscortCreature returns false only once that boss
            // exists, so we fall through to Drive and the completion gate latches.
            else if (step.kind == EventStepKind::EscortCreature)
            {
                if (DriveEscortCreature(step, prog))
                    return true;
            }
            // DropInHole: the step OWNS the tick while the leader glides out over
            // the hole-mouth and falls. Returning here SKIPS the StopBot(Hold)
            // below, which would otherwise cancel the off-mesh nudge spline every
            // tick and pin the leader on the lip (the prior attempts' failure).
            // DriveDropInHole returns false once landed, so we fall through to
            // Drive and RunStep's gate pulls the followers down + latches Done.
            else if (step.kind == EventStepKind::DropInHole)
            {
                if (DriveDropInHole(step))
                    return true;
            }
            // UseItemOnGO: OWN THE TICK to drive the approach to the target GO with
            // a sustained spline. Without this the StopBot(Hold) below cancels the
            // approach every tick and the tank can't thread a house doorway to a
            // barrel inside (it stalls "close but not inside"). Returns false once in
            // cast range, so we fall through to Drive and RunStep fires the GO.
            else if (step.kind == EventStepKind::UseItemOnGO)
            {
                // Recover between barrels. DriveUseItemOnGO owns the tick at
                // DcRel::AtObjective (30) — above the NeedsRest triggers (26.5) —
                // so without this the tank bombs all five houses on empty mana,
                // never drinking on the ~170yd courtyard->prison-yard run. When
                // out of combat and a rest is warranted, either YIELD the tick so
                // the tank's own drink/food (26.5) runs, or HOLD in place while the
                // rest of the party tops off. The drive resumes automatically once
                // everyone is recovered. See EventRestDecision.
                switch (EventRestDecision())
                {
                    case EventRest::Yield:
                        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
                        SetPhase(context, "objective");
                        ClearStall(context);
                        return false;  // NeedsRest (26.5) wins -> the tank drinks/eats
                    case EventRest::Hold:
                        DcMovement::StopBot(bot, DcMovement::Stop::Hold);
                        SetPhase(context, "objective");
                        ClearStall(context);
                        return true;   // own the tick; wait for the party to recover
                    case EventRest::None:
                        break;
                }
                if (DriveUseItemOnGO(step))
                    return true;
            }
        }
    }

    // Hold at the anchor while the event/hook runs — StopBot(Hold) cancels a
    // launched escort glide so the tank doesn't coast past the objective.
    //
    // Skipped for an event whose steps own the tank's movement. This is the same
    // trap the UseItemOnGO branch above sidesteps by owning the tick: the hold
    // runs BEFORE Drive, so a Custom hook that walks the tank somewhere with its
    // own spline has last tick's glide cancelled before it can even see it, and
    // the bot inches forward one tick at a time while logging healthy spline
    // issues. Generalised onto the event flag so it does not need a new
    // per-step special case each time. See DungeonEvent::stepsOwnMovement.
    if (!ev || !ev->stepsOwnMovement)
        DcMovement::StopBot(bot, DcMovement::Stop::Hold);

    // Prefer a declarative event (DungeonEventRegistry) when the anchor names
    // one; otherwise fall back to the legacy freeform hook (ObjectiveHookRegistry)
    // so existing objectives are unchanged. Both reduce to one drive outcome.
    EventDriveOutcome outcome = EventDriveOutcome::Completed;
    if (ev)
    {
        auto& prog =
            context->GetValue<DungeonEventProgress&>(DcKey::EventProgress)->Get();
        outcome = DungeonEventExecutor::Drive(bot, context, *ev, prog);
    }
    else
    {
        switch (ObjectiveHookRegistry::Run(next->onArriveHook, bot, context, *next))
        {
            case ObjectiveArriveResult::Running: outcome = EventDriveOutcome::Running; break;
            case ObjectiveArriveResult::Blocked: outcome = EventDriveOutcome::Stalled; break;
            case ObjectiveArriveResult::Done:
            default:                             outcome = EventDriveOutcome::Completed; break;
        }
    }

    if (outcome == EventDriveOutcome::Running)
    {
        // Event/hook still working — keep holding; it is driven again next tick.
        // A running event is making progress, so clear any leftover stall string:
        // a transient approach stall (way blocked / path-ends-short) set on the
        // walk in is only cleared by a later successful Advance, which never runs
        // once the tank switches to this event-hold path — leaving the panel stuck
        // on "Blocked" through the whole (correctly-progressing) set-piece.
        ClearStall(context);
        SetPhase(context, "objective");
        return true;
    }

    if (outcome == EventDriveOutcome::Stalled)
    {
        // The event needs the human (something the bot can't drive). Stall so
        // the player can sort it; they can also `dc skip` past the objective.
        StallDungeonClear(botAI, "I can't progress the event at " + next->name +
                                     " on my own. Sort it and I'll continue, or `dc skip`.");
        return true;
    }

    // Completed or Skipped: latch the objective complete so NextDungeonBossValue
    // advances and never re-targets it (objectives have no kill-bit to read).
    auto& cleared =
        context->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get();
    if (cleared.insert(next->entry).second)
    {
        ClearStall(context);
        DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tReached " + next->name + " \xe2\x80\x94 continuing.");
        LOG_DEBUG("playerbots.dungeonclear",
                  "[dungeon-clear] {} reached objective '{}' (entry {}) — marked cleared",
                  bot->GetName(), next->name, next->entry);
    }
    SetPhase(context, "");
    return true;
}

bool DcObjectiveEngageCombatAction::Execute(Event& /*event*/)
{
    // The trigger has already established the deadlock signature: DC leader, in
    // combat, an active KillCreature-engage objective, and an undetected reachable
    // creature of its entry nearby (a stealthed sapper that flagged the party into
    // combat and re-stealthed). Drive EngageDirect BY ENTRY to walk onto it and
    // Attack — the first swing breaks stealth and stock combat takes the kill from
    // here (this rung is inert the instant the target becomes detectable). False if
    // the target slipped out of reach between trigger and action; stock combat then
    // owns the tick.
    return DriveObjectiveEngage();
}

bool DcRunEventAction::Execute(Event& /*event*/)
{
    Map* map = bot ? bot->GetMap() : nullptr;
    if (!map)
        return false;

    DungeonEvent const* ev =
        DungeonEventExecutor::FindDueConditionalEvent(bot, context, map->GetId(),
                                                      _requireDrivesInCombat);
    if (!ev)
        return false;  // condition went false between trigger and action — stand down

    // Same camp upkeep as the anchored objective drive: a conditional event drives
    // the leader (room pre-clear, seek-and-kill, a walk to a lever) while advance
    // stands down, so without this the party's camp goes stale behind us.
    DcPullPlanner::MaintainScoutCamp(botAI, context);

    // Milestone 3: a room-aggro PRE-CLEAR event drives the engage pipeline
    // directly. The condition (room trash remains) gated us here; engage the
    // NEAREST room trash so the leader works the room from its edge inward (the
    // value already excludes the boss, its aggro sphere, and unreachable/door-
    // blocked units). Combat then takes over — the event trigger stands down once
    // the leader is in combat, the stock combat engine fights, and the assist-camp
    // rungs bring the followers in. When the room is clear NearestRoomTrash is
    // null and the condition reads false next tick, reopening the at-boss gate.
    // The event is never latched (repeatable per boss). IsBetweenPullsReady keeps
    // it to one careful pull at a time (loot / party catch-up / rest), matching
    // the legacy room-clear path this supersedes.
    if (DungeonEventRegistry::IsRoomAggroPreClear(*ev))
    {
        if (!IsBetweenPullsReady(bot, context))
        {
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] room-clear stand-down: not between-pulls ready "
                      "(loot/rest/catch-up) -> yielding (boss gate stays shut)",
                      bot->GetName());
            return false;
        }
        Unit* trash = DcTargeting::NearestRoomTrash(bot, context);
        if (!trash)
        {
            // The set is empty -> the room is considered clear and the at-boss
            // gate opens. If this fires while Scarlet trash is still alive, the
            // room-trash VALUE pruned them (see its exclusion-breakdown log) and
            // the boss is about to be pulled prematurely.
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] room-clear stand-down: NearestRoomTrash null -> room "
                     "deemed CLEAR, boss gate opening", bot->GetName());
            return false;  // room clear / nothing reachable — let the boss gate open
        }
        DcMovement::ResolveEscortConflict(bot);
        SetPhase(context, "room clear");

        // Engage the nearest room trash. EngageDirect's walk-in skirts the boss's
        // aggro sphere on its own (RoomAggroSkirtPoint) — a straight approach to a
        // pack on the far side of a centre-of-room boss (Mograine) would cut
        // through his aggro range and wake the whole room mid-clear. The room-trash
        // value excludes mobs INSIDE the sphere; the skirt handles the PATH across
        // it. Combat then takes over and the assist-camp rungs bring the followers.
        return EngageDirect(trash);
    }

    auto& prog =
        context->GetValue<DungeonEventProgress&>(DcKey::ConditionalEventProgress)->Get();

    // A conditional-event step that must SEEK its target — ClearRadius (clear an
    // area, e.g. the Stratholme ziggurat acolyte chambers) or KillCreature with
    // .engage — needs the engage pipeline to WALK the leader in. Unlike an
    // anchored event, where boss-nav delivers the tank to its objective before
    // the gate is even evaluated, a conditional event has nothing to navigate the
    // bot to the spot: without this it holds in place, the gate never satisfies,
    // and the step times out to Blocked. Mirror the anchored walk-in
    // (DcObjectiveArriveAction): while a target exists EngageDirect it; combat
    // owns the tick once aggroed, and Drive's gate (below) advances the step once
    // none remain.
    {
        uint32 const idx = (prog.eventId == ev->id) ? prog.stepIndex : 0;
        if (idx < ev->steps.size())
        {
            EventStep const& step = ev->steps[idx];
            if (step.kind == EventStepKind::ClearRadius)
            {
                float const r = step.radius > 0.0f ? step.radius : 50.0f;
                if (Unit* target = DcTargeting::NearestHostileNearPoint(
                        bot, context, step.x, step.y, step.z, r, step.zBand,
                        &step.entryFilter))
                {
                    DcMovement::ResolveEscortConflict(bot);
                    SetPhase(context, "event");
                    return EngageDirect(target);
                }
            }
            else if (step.kind == EventStepKind::KillCreature && step.engage &&
                     step.creatureEntry)
            {
                float const search = step.radius > 0.0f ? step.radius : 250.0f;
                if (Creature* target = bot->FindNearestCreature(
                        step.creatureEntry, search, /*alive*/ true))
                {
                    // Only engage an instance of the entry we can actually reach
                    // (flat 2D FindNearestCreature can return one across a wall /
                    // on another level). requireDirect=false keeps a legitimately
                    // FAR seek alive — the seek is this event's own navigation.
                    if (!DcEngageGeometry::IsEngageReachable(bot, target, /*requireDirect*/ false))
                        return false;
                    DcMovement::ResolveEscortConflict(bot);
                    SetPhase(context, "event");
                    return EngageDirect(target);
                }
            }
        }
    }

    // Hold position while driving the event. ResolveEscortConflict only cancels a
    // launched escort glide (the coast-past from the advance ladder) — it leaves a
    // step's own intra-room MovePoint (HopTo) alone, so MoveTo/Gossip walk-ins
    // still work, unlike the StopMovingOnCurrentPos in StopBot(Hold).
    //
    // UNLESS the event's steps own the tank's movement. This runs BEFORE Drive, so
    // for a Custom hook that delivers the tank across the map on its own
    // long-range spline, the hold cancels last tick's glide before the hook gets
    // to look at it: the hook re-issues, the next tick cancels again, and the bot
    // creeps a tick at a time while every log line reports a healthy "spline
    // issued". That is what kept Black Morass off its portals even after the camp
    // was moved onto LongRangePathfinder. See DungeonEvent::stepsOwnMovement.
    if (!ev->stepsOwnMovement)
        DcMovement::ResolveEscortConflict(bot);

    EventDriveOutcome const outcome = DungeonEventExecutor::Drive(bot, context, *ev, prog);

    char const* outcomeStr =
        outcome == EventDriveOutcome::Running   ? "running"
        : outcome == EventDriveOutcome::Completed ? "completed"
        : outcome == EventDriveOutcome::Stalled ? "stalled"
                                                : "skipped";
    LOG_DEBUG("playerbots.dungeonclear",
              "[DC:{}] run-event '{}' step {}/{} -> {}",
              bot->GetName(), ev->name, prog.stepIndex,
              static_cast<uint32>(ev->steps.size()), outcomeStr);

    if (outcome == EventDriveOutcome::Running)
    {
        // Progress is being made — clear any stale stall string (see the matching
        // note in DcObjectiveArriveAction) so the panel doesn't report "Blocked"
        // while a conditional event runs correctly.
        ClearStall(context);
        SetPhase(context, "event");
        return true;
    }

    if (outcome == EventDriveOutcome::Stalled)
    {
        StallDungeonClear(botAI, "I can't progress the event '" + ev->name +
                                     "' on my own. Sort it and I'll continue, or `dc skip`.");
        return true;
    }

    // Completed or Skipped. A REPEATABLE event is never latched — it must fire
    // again the next time its condition reads true (e.g. the RFD gong, rung once
    // per wave). Its loop is ended by the condition itself going false for good
    // (Tuten'kash spawns), not by a one-shot latch.
    //
    // REWIND THE STEP LIST HERE. Drive's own rewind fires only after a LAPSE —
    // EventStaleGapMs of nobody driving the event — which assumes the condition
    // goes false between repeats. That holds for the short wake/ring events
    // (Zum'rah is awake, the gong is rung: the condition is false on the very
    // next tick, so they lapse and rewind naturally). It does NOT hold for an
    // event whose condition stays true ACROSS repeats: Black Morass's wave event
    // is due while any rift is open or any Infinite lives, so it keeps being
    // driven every tick, lastDriveMs never goes stale, no lapse is ever detected
    // — and stepIndex stays parked at steps.size() (Completed) or on the step
    // that timed out (Skipped). Drive then returns Completed on every subsequent
    // tick without running anything, forever.
    //
    // Live (2026-07-24): the party cleared the first rift, the step list ran to
    // the end, and the tank then stood on the spent portal ignoring every later
    // rift — the event was "running" and doing nothing. Restarting here is what
    // makes Repeatable actually repeat; the steps are idempotent by design (the
    // same property Drive's lapse rewind already relies on).
    if (ev->repeatable)
    {
        // YIELD THE TICK for a driver event (stepsOwnMovement). Engine::DoNextAction
        // executes exactly ONE action per tick — it breaks out of its loop on the
        // first Execute that returns true — so an action that returns true every
        // tick starves every lower rung, and for a driver registered in the COMBAT
        // engine above the stock movers that means the bot never attacks, never
        // casts, and never builds threat. Black Morass wiped parties this way: the
        // tank drove to the portal, the keeper engaged it, and the tank then stood
        // there taking hits with no rotation while the DPS pulled aggro and died.
        //
        // A driver step reports Done precisely when it has nothing to steer this
        // tick, so handing the tick back is the whole point. It still runs every
        // tick (the trigger is unchanged), so its side effects — the Black Morass
        // force-pull — stay responsive; it just stops CLAIMING ticks it does not
        // need. Ordinary repeatable events keep the old own-the-tick behaviour.
        bool const yieldTick = ev->stepsOwnMovement;

        prog.stepIndex = 0;
        prog.attempts = 0;
        prog.stepStartMs = getMSTime();
        // Re-base the forward-progress watchdog too: this is a genuine fresh
        // activation, so the previous pass's high-water mark must not make the
        // new one look wedged (Advance escalates to Failed off progressMs).
        prog.maxStepIndex = 0;
        prog.progressMs = prog.stepStartMs;

        ClearStall(context);
        SetPhase(context, "");
        return !yieldTick;
    }

    // Otherwise latch the event under its synthetic key so the trigger stops
    // re-firing it and the clear proceeds.
    auto& cleared =
        context->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get();
    if (cleared.insert(DungeonEventExecutor::ConditionalLatchKey(ev->id)).second)
    {
        ClearStall(context);
        DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tEvent '" + ev->name + "' done \xe2\x80\x94 continuing.");
        LOG_DEBUG("playerbots.dungeonclear",
                  "[dungeon-clear] {} completed conditional event '{}' (id {}) — latched",
                  bot->GetName(), ev->name, ev->id);
    }
    SetPhase(context, "");
    return true;
}

namespace
{
    // WHOSE run is being disabled. The terminal rungs may now be driven by a member
    // that is NOT the run owner — with the leader dead there is no election left, so
    // any living member can fire them (DcLeaderSignal::FindTerminalDriver) — and
    // DisableDungeonClear resets whatever context it is handed. Handed a follower's,
    // it would clear that follower's already-empty run state, leave the real run
    // enabled, and skip the `.dc test` run-end observer inside it (keyed on the
    // tank's GUID). Resolve the owner instead; identity in the healthy case, where
    // the driver IS the owner.
    PlayerbotAI* RunOwnerAI(Player* bot, PlayerbotAI* fallback)
    {
        Player* const owner = DcLeaderSignal::FindRunOwner(bot);
        PlayerbotAI* const ownerAI = owner ? GET_PLAYERBOT_AI(owner) : nullptr;
        return ownerAI ? ownerAI : fallback;
    }
}

bool DungeonClearDisableOnDeathAction::Execute(Event& /*event*/)
{
    // Re-evaluate for the disable REASON (the kernel is deterministic, so this
    // matches the verdict the trigger fired on). The funnel is untouched — only
    // the message says why recovery wasn't attempted / gave up.
    DcRezRecovery::Plan const plan = bot ? DcRezRecovery::Evaluate(bot)
                                         : DcRezRecovery::Plan{};
    std::string const& deadName = plan.deadName;

    std::string reason;
    switch (plan.verdict.reason)
    {
        case DcRezDecision::Reason::Wipe:
            reason = "The party wiped \xe2\x80\x94 dungeon clear disabled. Type 'dc on' when ready to resume.";
            break;
        case DcRezDecision::Reason::NoRezzer:
            reason = deadName + " died and no one left alive can resurrect \xe2\x80\x94 dungeon clear "
                     "disabled. Type 'dc on' when ready to resume.";
            break;
        case DcRezDecision::Reason::TimedOut:
            reason = "Couldn't get " + deadName + " resurrected in time \xe2\x80\x94 dungeon clear "
                     "disabled. Type 'dc on' when ready to resume.";
            break;
        default:
            // Feature off (Reason::Disabled) or a race resolved the deaths
            // between trigger and action — the classic message.
            reason = deadName + " died \xe2\x80\x94 dungeon clear disabled. Type 'dc on' when ready to resume.";
            break;
    }

    DisableDungeonClear(RunOwnerAI(bot, botAI), reason);
    return true;
}

bool DungeonClearDisableOnClearedAction::Execute(Event& /*event*/)
{
    DisableDungeonClear(RunOwnerAI(bot, botAI), DcActionShared::kReasonAllCleared);
    return true;
}

bool DungeonClearBreakStuckCombatAction::Execute(Event& /*event*/)
{
    // Phantom combat: the trigger has confirmed we've been flagged in combat with
    // nothing fightable for the full StuckCombatTimeout — no holder that is both
    // reachable and actually coming for us. Force-clear it exactly as a GM
    // `.combatstop` does — end combat AND remove ourselves from every threat list, so
    // the far/unreachable/stationary holder that ghost-flagged us releases the
    // reference instead of re-adding it next tick.
    LOG_INFO("playerbots.dungeonclear",
             "[DC:{}] stuck-combat: flagged in combat with nothing reachable and coming "
             "for us past the timeout -> force-clearing combat + threat",
             bot->GetName());
    bot->CombatStop();
    bot->GetThreatMgr().RemoveMeFromThreatLists();
    return true;
}

bool DungeonClearRecoverStrandedAction::Execute(Event& /*event*/)
{
    // The trigger has confirmed (on the leader) that the run has been frozen past
    // the no-progress window with a bot member stuck out of range. Teleport the
    // strays to the tank and re-arm the clock; the whole decision + relocation
    // lives in the glue.
    DcStrandedRecovery::Recover(bot);
    return true;
}

bool DungeonClearDoorBlockedAction::Execute(Event& event)
{
    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    std::string const target = next.has_value() ? next->name : "the next boss";
    std::string const pauseReason =
        "A door blocks the path to " + target +
        " and I can't open it. Paused — open it and hit Resume.";
    std::string const timeoutReason =
        "The door to " + target +
        " won't open for me. Paused — open it (or finish its event) and hit Resume.";
    std::string const openingReason = "Opening the door to " + target + ".";

    ObjectGuid const doorGuid = AI_VALUE(ObjectGuid, DcKey::BlockingDoor);
    GameObject* door = doorGuid.IsEmpty() ? nullptr : botAI->GetGameObject(doorGuid);

    // We've reached the door. Decide what a player in our shoes would do:
    //   - openable (plain door, or we hold the key / have the skill): interact
    //     with it the same way a client right-click does (GameObject::Use at
    //     interaction range), then hold for the tick — the door swings open,
    //     the blocking-door value empties next tick, and Advance resumes.
    //   - not openable (locked with no key/skill, or a script/encounter door):
    //     never touch the GO state — drop straight into pause mode. The player
    //     has to come solve the door anyway, and pause is exactly the right hold:
    //     the whole party stops driving and follows (see DungeonClearMultiplier)
    //     until the door is opened and the player hits Resume. This replaces the
    //     old bespoke "stalled at door" state, which was prone to the bot's stock
    //     AI later walking it straight through the opened door.
    // The Use() is throttled on the announced-reason transition so we don't
    // re-click a door every tick; when Advance resumes it clears the reason, so
    // a later re-close (autoclose doors) re-arms a fresh single attempt.
    // `atDoor` is true ONLY for the park that means "the walk-in finished — the
    // route has carried us to the near side of the doorway". Every other caller
    // is a walk-in FAILURE fallback (no corridor, unresolvable GO, a glide tick
    // that made no progress) and can fire anywhere across the up-to-80yd
    // approach the blocking-door value looks ahead over. The distinction gates
    // the blocked-state watchdog below; see the comment there.
    std::string const waitReason =
        "A gate has closed on us — waiting for it to open.";

    auto parkAndStall = [&](bool atDoor)
    {
        DcMovement::StopBot(bot, DcMovement::Stop::Soft);

        // Self-clearing script barriers (Stratholme's two gate traps) shut for a
        // bounded 20s under instance-script control and reopen themselves. There
        // is nothing to click and nothing for a player to come and solve, so the
        // pause path below is simply wrong: hold, report, and let the door's own
        // timer free us. Checked FIRST so neither the click ladder nor the
        // blocked-state watchdog ever sees one.
        if (door && DcEventDoorRegistry::IsSelfClearing(door->GetEntry()))
        {
            StallDungeonClear(botAI, waitReason);
            return true;
        }

        // Script-only event doors (e.g. SFK's Courtyard Door 18895) wear a
        // plainly-clickable empty-lock template but the client only opens them
        // via their event — a generic Use() here desyncs the client and skips
        // the event. Never auto-open a listed door; fall through to the pause /
        // (preferably) let the dungeon-event free the prisoner that opens it.
        bool const canOpen = DC_ATTEMPT_DOOR_OPEN && door &&
                             !DcEventDoorRegistry::IsScriptOnly(door->GetEntry()) &&
                             BotCanOpenDoorLikePlayer(bot, door);
        bool timedOut = false;

        if (canOpen)
        {
            if (!DcEngageGeometry::IsDoorClosed(door))
            {
                // The door is open NOW (our click landed, or a player beat us
                // to it) — only the 500ms-cached blocking-door value hasn't
                // noticed yet. Drop the stall immediately so the status panel
                // stops reporting Blocked at an open door; Advance resumes the
                // moment the value refreshes empty.
                //
                // Also end the blocked-state watchdog window: we have direct
                // proof the door opens for us, so a later re-close (autoclose
                // gates) must start counting from scratch rather than resume an
                // old window. See DcApproachState::ClearDoorStall.
                context->GetValue<DcApproachState&>(DcKey::ApproachState)
                    ->Get()
                    .ClearDoorStall();
                ClearStall(context);
                return true;
            }

            DcApproachState& doorAppr =
                context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get();
            uint32 const now = getMSTime();

            // Blocked-state watchdog. The entitlement above is template-level
            // truth and CAN be wrong (SFK's Arugal's Lair is an event door
            // wearing the same empty-lock-85 template as a plain clickable
            // Deadmines door), and the click gate below measures range to the
            // GO origin, which on wide gates sits outside DC_DOOR_USE_RANGE of
            // the path-side parking spot — either way the bot would work the
            // door forever. After DoorBlockedTimeout seconds with the door
            // still shut, give up and fall through to the auto-pause below:
            // the stashed GUID still auto-resumes the run the moment the door
            // really opens (event completes, or a player opens it).
            //
            // ONLY accrues once the walk-in has actually reached the doorway
            // (atDoor). The failure-fallback parks reach this lambda from
            // anywhere along the approach, and counting them spent the whole
            // (5s default) budget on TRAVEL time: the run auto-paused at a door
            // it had never touched, with zero Use() calls behind it. Scholomance
            // batch tp-20260815-132009-1 lost two runs to exactly that:
            // tr-20260815-132014-4 auto-paused 77.8yd from the third Iron Gate
            // after 5s of "holding, not clicking", and -10 auto-paused in the
            // same tick it first reached the second gate, having spent its whole
            // budget on the approach. Sibling runs hit the identical hold at the
            // identical gates and finished — theirs merely lasted under 5s.
            // Away from the door we hold and report and leave the watchdog
            // untouched, so arrival always opens a fresh window.
            if (atDoor)
                timedOut = doorAppr.ObserveDoorStall(
                    door->GetObjectGuid(), now, DC_DOOR_STALL_REARM_MS,
                    DcSettings::GetUInt(bot, "DoorBlockedTimeout") * 1000);

            // A click is only legitimate from beside the door. Parked far from
            // it (mis-flag, or the walk-in hasn't closed the gap yet), hold
            // position and report instead of toggling a GO no player could
            // reach — Use() has no range check of its own.
            if (!timedOut && !bot->IsWithinDistInMap(door, DC_DOOR_USE_RANGE))
            {
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] door-blocked: entitled to open {} '{}' but "
                         "{:.1f}yd away (> {:.0f}yd) -> holding, not clicking",
                         bot->GetName(), door->GetObjectGuid().ToString(),
                         door->GetName(), bot->GetExactDist(door), DC_DOOR_USE_RANGE);
                StallDungeonClear(botAI, openingReason);
                return true;
            }

            // Click whenever the door actually reads SHUT, on a per-door
            // cooldown — not the old "once per announced reason" latch. Gates
            // with door.autoCloseTime re-shut themselves seconds after opening
            // (King's Square Gate: 3s); under the latch, a gate that re-closed
            // before Advance ran never got a second click and the run sat
            // "Blocked" at a door it held the key for.
            if (!timedOut)
            {
                if (doorAppr.lastDoorUseGuid != door->GetObjectGuid() ||
                    getMSTimeDiff(doorAppr.lastDoorUseMs, now) >= DC_DOOR_REUSE_MS)
                {
                    LOG_INFO("playerbots.dungeonclear",
                             "[DC:{}] door-blocked: opening {} '{}' as a player would (entitled)",
                             bot->GetName(), door->GetObjectGuid().ToString(), door->GetName());
                    door->Use(bot);
                    doorAppr.lastDoorUseGuid = door->GetObjectGuid();
                    doorAppr.lastDoorUseMs = now;
                }
                StallDungeonClear(botAI, openingReason);
                return true;
            }
        }

        // Can't open it ourselves — or timed out working a door we thought we
        // could — auto-pause and force the navigation dead.
        // Set the flag once on transition: the door-blocked trigger gates on
        // !paused, so it won't re-fire and re-announce every tick.
        if (!DcRun::Of(context).paused)
        {
            DcRun::Of(context).paused = true;
            // Record the cause so the status panel shows the door reason rather
            // than a generic hold (manual pause stamps its own reason instead).
            DcRun::Of(context).pauseReason =
                "a closed door is blocking the path";
            // Stash THIS door's GUID so DungeonClearDoorReopenedTrigger can poll
            // it and auto-resume the instant a player opens it (door is non-null
            // here — the can-open branch above already required it). The null-door
            // fallback path below leaves this empty, so it simply stays manual.
            DcRun::Of(context).pausedDoor =
                door ? door->GetObjectGuid() : ObjectGuid::Empty;
            if (MotionMaster* mm = bot->GetMotionMaster())
                mm->Clear();
            bot->StopMovingOnCurrentPos();
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] door-blocked: {} {} -> auto-pausing",
                     bot->GetName(),
                     timedOut ? "timed out working" : "can't open",
                     door ? door->GetObjectGuid().ToString() : "(none)");
            DcStatusPublisher::SendAddonMessage(
                botAI, "CHAT\t" + (timedOut ? timeoutReason : pauseReason));
            botAI->DoSpecificAction("dc status", event, true);
        }
        return true;
    };

    if (!door)
    {
        if (doorGuid.IsEmpty())
        {
            // The blocking-door value went EMPTY — the corridor is clear: the
            // door opened (e.g. a dungeon event freed the prisoner who unlocked
            // the courtyard door) or there was never a real blocker. Do NOT
            // auto-pause; drop any stall and stand down so Advance walks the now-
            // open doorway next tick. Without this, the one-tick race between the
            // door opening and the cached blocking-door value clearing paused the
            // run "the way is blocked by a door" the instant the event succeeded.
            ClearStall(context);
            return true;
        }
        // A non-empty guid we couldn't resolve (the GO's grid isn't resident) —
        // genuinely unknown; fall back to the in-place stall so the reason is
        // still reported.
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] door-blocked: door guid {} unresolved -> parking in place",
                 bot->GetName(), doorGuid.ToString());
        return parkAndStall(/*atDoor*/ false);
    }

    // GetExactDist is to the door's GO origin (hinge/jamb) — kept only for log
    // lines. It is NOT used to decide when to park: the origin can sit past the
    // doorway gap, so "within Nyd of the origin" made the walk-in glide carry the
    // tank THROUGH the gap to reach it. Parking is decided below on distance
    // travelled ALONG the path to the door (DistAlongPathToClosedDoor).
    float const distToDoor = bot->GetExactDist(door);

    // --- Walk the WHOLE corridor up to the door --------------------------
    // The door is detected as much as 80yd ahead along the route, so we must
    // close that whole gap before parking. The cached long-path to the next
    // boss is itself truncated by this very door (the closed GO blocks the
    // navmesh), so its polyline already terminates right at the door — glide
    // that polyline to its end and the tank lands AT the door. The previous
    // code did MoveTo(door): that overload bee-lines to the door and clamps
    // each step to spellDistance, projecting an intermediate point straight at
    // the door through any wall on a winding corridor, so the tank stopped at
    // the last reachable spot far short of it. Reuse the same escort-spline
    // follower Advance drives (shared follower state, same long-path value).
    DcApproachState& appr =
        context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get();
    if (next.has_value())
    {
        // Match Advance: route toward the boss's EFFECTIVE position (live
        // creature coords when loaded, else the static anchor) so this shares
        // the same cached long-path Advance builds. Targeting the static anchor
        // here while Advance targets the live boss would flip the shared cache's
        // built-toward position every tick and thrash rebuilds.
        Creature* const liveBoss = DcTargeting::GetLiveBoss(bot, context, next->entry);
        DungeonBossInfo effectiveTarget = *next;
        if (liveBoss)
        {
            effectiveTarget.x = liveBoss->GetPositionX();
            effectiveTarget.y = liveBoss->GetPositionY();
            effectiveTarget.z = liveBoss->GetPositionZ();
        }
        EnsureLongPath(bot, context, appr, effectiveTarget);
    }

    ChunkedPathfinder::Result const& path =
        AI_VALUE(ChunkedPathfinder::Result&, DcKey::LongPath);
    if (!path.reachable || path.segments.empty())
    {
        // No corridor to follow (boss-side route gone). Hold the door line
        // from wherever we are rather than thrash. With no route there is no
        // along-path arrival test, so straight-line range to the GO is the only
        // "are we actually at it" signal left.
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] door-blocked: no long-path corridor ({:.1f}yd from door) "
                  "-> park in place",
                  bot->GetName(), distToDoor);
        return parkAndStall(bot->IsWithinDistInMap(door, DC_DOOR_USE_RANGE));
    }

    // Park on the NEAR side: stop once the route is within DC_DOOR_STOP_DISTANCE
    // (travel distance) of reaching the doorway. Measured along the path, so it
    // halts before the gap even when the door's GO origin sits past it and the
    // navmesh runs the corridor straight through the shut door. Without this the
    // walk-in glided through the gap to get "close to the origin", then the pack
    // beyond aggroed the now-through tank.
    float const distAlongToDoor =
        DcEngageGeometry::DistAlongPathToClosedDoor(
            bot, path, door->GetPositionX(), door->GetPositionY(),
            door->GetPositionZ(), /*lookAhead*/ 100.0f);
    // "Actually at the doorway", the predicate the blocked-state watchdog runs
    // on. Primary signal is the along-path arrival below. The straight-line
    // fallback covers a door the along-path scan can't place (the route's
    // polyline never enters its band, or the band entry reads as already behind
    // the progress cursor, both of which return FLT_MAX): standing on top of a
    // shut gate is at it whatever the route says, and without the fallback such
    // a door could be worked forever with the watchdog never arming.
    bool const atDoor = distAlongToDoor <= DC_DOOR_STOP_DISTANCE ||
                        bot->IsWithinDistInMap(door, DC_DOOR_USE_RANGE);

    if (distAlongToDoor <= DC_DOOR_STOP_DISTANCE)
    {
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] door-blocked: at door ({:.1f}yd along path) -> parking, "
                  "waiting for it to open",
                  bot->GetName(), distAlongToDoor);
        return parkAndStall(atDoor);
    }

    DungeonFollowerState& follower =
        context->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get();

    // Walk the rest of the corridor to the door with the shared glide driver —
    // the same wedge-detect / off-path-resnap / ride-guard / jump / rejoin /
    // spline / fallback ladder Advance runs, so the walk-in inherits every
    // hard-won fix (the "door walk-in" stutter dance, the momentary-isMoving
    // ride-guard tolerance, the off-line wall-clip rejoin) instead of a hand-clone
    // that drifts. The door's watchdog instance keeps its wedge counter separate
    // from Advance's route glide. The driver leaves park/stall bookkeeping to us:
    //   - Moved:  a fresh move was issued -> clear the stall, own the tick.
    //   - Riding: an in-flight glide is still travelling -> own the tick, and
    //             deliberately do NOT clear the stall (a prior Moved tick did).
    //   - ReachedEnd: corridor end = as close as the navmesh allows (the door's
    //             collision truncates the route here) -> the real "at the door".
    //   - OffPathLost / Blocked: can't make progress -> park and report.
    GlideOutcome const outcome =
        DriveGlideToEnd(path, follower, appr, appr.doorWalkInWatch, bot->GetMapId(),
                        "door walk-in");
    char const* outcomeName = "?";
    switch (outcome)
    {
        case GlideOutcome::Moved:
            ClearStall(context);
            return true;
        case GlideOutcome::Riding:
            return true;
        case GlideOutcome::ReachedEnd:
            outcomeName = "reached-end";
            break;  // can't make progress at the door -> park and report below.
        case GlideOutcome::OffPathLost:
            outcomeName = "off-path-lost";
            break;
        case GlideOutcome::Blocked:
            outcomeName = "blocked";
            break;
    }

    // Name the no-progress outcome. All three are silent inside the driver, so a
    // walk-in frozen short of the doorway used to leave nothing in the log but a
    // repeating "holding, not clicking" at an unchanging distance — no way to
    // tell a truncated route from an off-path loss from an impaired bot.
    // distAlongToDoor is FLT_MAX when the scan couldn't place the door on the
    // route at all (its band is past the look-ahead, or reads as already behind
    // the progress cursor); report that as -1 rather than a nonsense distance.
    LOG_DEBUG("playerbots.dungeonclear",
              "[DC:{}] door-blocked: walk-in made no progress ({}) {:.1f}yd from door, "
              "{:.1f}yd along path (-1 = unplaced) -> parking",
              bot->GetName(), outcomeName, distToDoor,
              distAlongToDoor >= std::numeric_limits<float>::max() ? -1.0f : distAlongToDoor);
    return parkAndStall(atDoor);
}
