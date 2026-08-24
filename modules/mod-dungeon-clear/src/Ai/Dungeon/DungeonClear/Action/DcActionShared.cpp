/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearActions.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "TestRun/DcTestRunManager.h"

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
#include "DBCStores.h"
#include "GameObject.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "MotionMaster.h"
#include "MoveSplineInitArgs.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Position.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/Data/DcEventDoorRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
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
#include "Ai/Dungeon/DungeonClear/Util/DcPathWorker.h"
#include "Ai/Dungeon/DungeonClear/Util/DcSocialQuarantine.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/LongRangePathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Util/StridedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/SwimPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearStateValues.h"
#include "Playerbots.h"
#include "DcActionShared.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

namespace DcActionShared
{

    struct PullSpellPick
    {
        std::string name;
        float minRange;
        float maxRange;
    };

    // Per-class single best opener. Names match the strings the engine
    // already registers via the per-class AiObjectContext (e.g. "heroic
    // throw", "avenger's shield"). No fallback chain — `botAI->CastSpell`
    // returns false cleanly if the spell isn't known/usable, and we'll
    // just auto-attack instead.
    std::optional<PullSpellPick> PickPullSpell(Player* bot)
    {
        if (!bot)
            return std::nullopt;
        switch (bot->getClass())
        {
            case CLASS_WARRIOR:      return PullSpellPick{"heroic throw",       8.0f, 30.0f};
            case CLASS_PALADIN:      return PullSpellPick{"avenger's shield",   8.0f, 30.0f};
            case CLASS_DEATH_KNIGHT: return PullSpellPick{"death grip",         8.0f, 30.0f};
            // Bear-tank druids use Faerie Fire (Feral) — 30yd, no form
            // switch, applies armor debuff + threat. Caster druids would
            // fail this cast (spell not known) and just auto-attack
            // instead, which is fine — DC is tank-only anyway.
            case CLASS_DRUID:        return PullSpellPick{"faerie fire (feral)", 8.0f, 30.0f};
            default:                 return std::nullopt;
        }
    }


    // The EQUIPPED RANGED WEAPON as an opener, when the class ability isn't
    // available. Mirrors playerbots' own CastShootAction: read the ranged slot and
    // use the auto-shot for what is in it — 3018 "Shoot" for a bow/gun/crossbow,
    // 2764 "Throw" for a thrown weapon.
    //
    // This is not a nicety. A warrior's class opener is HEROIC THROW, which is
    // learned at level 71 — so at the level dungeons are run, EVERY warrior tank
    // falls through the class table and body-tags. That is tolerable in a corridor
    // and not tolerable at all for a plan whose entire premise is tagging from a
    // measured spot without moving off it. A thrown/ranged auto-shot covers exactly
    // that gap, at real range, for any class whose ability is missing or untrained.
    std::optional<ResolvedPullSpell> ResolveRangedWeaponPull(PlayerbotAI* botAI, Player* bot)
    {
        if (!botAI || !bot)
            return std::nullopt;
        Item const* const ranged =
            bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        if (!ranged || !ranged->GetTemplate())
            return std::nullopt;

        uint32 shootId = 0;
        uint32 ammoSubClass = 0;
        bool needsAmmo = false;
        switch (ranged->GetTemplate()->SubClass)
        {
            case ITEM_SUBCLASS_WEAPON_GUN:
                shootId = 3018;
                needsAmmo = true;
                ammoSubClass = ITEM_SUBCLASS_BULLET;
                break;
            case ITEM_SUBCLASS_WEAPON_BOW:
            case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                shootId = 3018;
                needsAmmo = true;
                ammoSubClass = ITEM_SUBCLASS_ARROW;
                break;
            case ITEM_SUBCLASS_WEAPON_THROWN:
                shootId = 2764;
                break;
            default: return std::nullopt;   // wand/relic/idol — not a pull
        }

        // DELIBERATELY NO HasSpell GATE HERE, unlike the class opener above.
        //
        // "Shoot" (3018) and "Throw" (2764) are not learned spells in 3.3.5 — they
        // come with the equipped weapon, and nothing puts them in a spellbook. So
        // HasSpell is false for them on EVERY character: a query across this realm's
        // whole `character_spell` table returns zero rows for 3018, 2764, 75 and
        // 5019. This function carried that gate from the day it was written, which
        // made the entire ranged fallback dead code — it could never once have fired,
        // and the level-70 warrior it exists for went on body-tagging (live: prot
        // warrior Ushkuk, tr-20260803-124151-3, with The Boomstick equipped and 1000
        // rounds in the bag).
        //
        // The gate is right for the class opener and wrong here, and the difference
        // is the name lookup. There, the id comes from the "spell id" VALUE resolved
        // from a name, which falls through to the bot's PET spellbook when the bot
        // doesn't know it — so without HasSpell the server is asked to force a pull
        // the tank never learned. Here the id is a literal, there is no name and no
        // pet to confuse it with, and the real precondition is the WEAPON. Which is
        // exactly how playerbots' own CastShootAction does it: shootSpellId straight
        // to CanCastSpell/CastSpell, no HasSpell anywhere.
        //
        // What DOES have to be checked is ammunition. A gun, bow or crossbow with an
        // empty ammo slot cannot fire, and the failure would land on the caller as a
        // silently-failed cast — which for a scripted pull means a body-tag walk into
        // the room the plan exists to stay out of. A thrown weapon needs none.
        //
        // AND IT HAS TO BE THE RIGHT AMMUNITION. "Is the slot non-empty" was the
        // obvious reading and it is not the condition the server checks: a gun takes
        // BULLETS and a bow or crossbow takes ARROWS, and loading one into the other
        // is not a partial success but an outright SPELL_FAILED_NO_AMMO. Because the
        // slot is occupied, every "do I have ammo" test upstream passes and the only
        // symptom is a cast that quietly returns false.
        //
        // Live (tr-20260803-154419-13 and -17): both prot warriors carried Rifle of
        // the Stoic Guardian — a gun — with Timeless Arrows loaded, so the whole
        // ranged fallback resolved happily and then failed at the cast every tick. A
        // warrior has no class opener at 70 either (Heroic Throw is 71), so the tank
        // had nothing at all: it stood on Selin's east stand spot for the full 47s leg
        // budget without pulling and then walked into the room. Every druid and
        // paladin tank in the same plan pulled normally, which is exactly why it read
        // as random rather than as a gear bug.
        //
        // Rejecting the mismatch here is what turns that into an honest fall-through:
        // no pull spell resolves, so the caller stops waiting on a tag that can never
        // land. (The provisioning side of the same bug — InitAmmo re-run only for
        // hunters after the spec re-gear swapped the weapon — is fixed in
        // DcTestRunJob; this is the guard for a real player's mismatched slot.)
        if (needsAmmo)
        {
            uint32 const ammoId = bot->GetUInt32Value(PLAYER_AMMO_ID);
            if (!ammoId)
                return std::nullopt;
            ItemTemplate const* const ammo = sObjectMgr.GetItemPrototype(ammoId);
            if (!ammo || ammo->Class != ITEM_CLASS_PROJECTILE ||
                ammo->SubClass != ammoSubClass)
                return std::nullopt;
        }

        // THE SPELL'S OWN RANGE, from the spell store — the second reason this
        // fallback never worked.
        //
        // It used to ask botAI->GetRange("shoot"), described here as the "real weapon
        // range". It is nothing of the sort: PlayerbotAI::GetRange maps "shoot"
        // straight to sPlayerbotAIConfig.shootDistance, a config knob for how close a
        // bot likes to stand when shooting, which defaults to 5.0 and is not set in
        // this deployment. So the pick came back as min 8 / max 5 — an empty interval.
        // Even with the HasSpell gate gone, the caller's `d >= minRange && d <=
        // maxRange` could never be satisfied at any distance.
        //
        // The number that governs is the one the server will check, and Spell::Check
        // Range uses GetSpellMaxRangeForTarget — the spell's range, full stop.
        // ItemTemplate::RangedModRange is loaded but only ever sent to the client, so
        // the weapon does not scale it. Both auto-shots are 0-30yd, which is why the
        // 8yd floor from the class table carries over unchanged: every entry there is
        // 8/30 too, and this is the same reach by a different route.
        SpellEntry const* const info = sSpellMgr.GetSpellEntry(shootId);
        if (!info)
            return std::nullopt;
        float const maxRange = info->GetMaxRange(/*positive*/ false);
        if (maxRange <= 0.0f)
            return std::nullopt;
        return ResolvedPullSpell{shootId, 8.0f, maxRange};
    }

    // Resolve the opener to a usable spell id, but ONLY one the tank itself
    // trained — a rule that applies to the CLASS OPENER and not to the ranged-weapon
    // fallback below it; see ResolveRangedWeaponPull for why the distinction matters
    // and what it cost to conflate them.
    // botAI->CastSpell(name) is NOT a sufficient gate on its own: the
    // "spell id" value falls back to the bot's PET spellbook when the bot doesn't
    // know the spell, and CastSpell then builds the spell on the bot regardless of
    // knowledge — i.e. the server quietly FORCES the tank to "cast" a pull it never
    // learned. Requiring bot->HasSpell(id) (player-only; never the pet) means an
    // untrained tank falls through to the ranged weapon, and only then walks in.
    std::optional<ResolvedPullSpell> ResolvePullSpell(PlayerbotAI* botAI, Player* bot)
    {
        if (!botAI || !bot)
            return std::nullopt;
        if (auto pick = PickPullSpell(bot))
        {
            uint32 const spellId =
                botAI->GetAiObjectContext()->GetValue<uint32>(DcKey::Stock::SpellId, pick->name)->Get();
            if (spellId && bot->HasSpell(spellId))
                return ResolvedPullSpell{spellId, pick->minRange, pick->maxRange};
        }
        // No class opener, or the tank never trained it (the level-70 warrior case).
        // Fall back to whatever is in the ranged slot before giving up and walking in.
        return ResolveRangedWeaponPull(botAI, bot);
    }


    void DisableDungeonClear(PlayerbotAI* botAI, std::string const& reason)
    {
        AiObjectContext* ctx = botAI->GetAiObjectContext();
        Player* bot = botAI->GetBot();
        // One reset clears the whole run-level state — enabled flag, the pause
        // cluster (paused / reason / auto-paused door), the selected-boss override,
        // and the two leader-fight latches (leader-combat-since / party-engaged) —
        // in lockstep. See DcRunState. The pull preference/bool + daze revoke below
        // are deliberately separate (their pre-run lifetime differs).
        DcRun::Of(ctx).Reset();
        if (bot)
        {
            DcStatusPublisher::UnmarkActiveTank(bot->GetObjectGuid());
            // Hand the room back. The social quarantine is the one piece of run
            // state that lives OUTSIDE the bot — it is stored as react state on the
            // creatures themselves — so a reset that only clears context values
            // would leave a wing of the instance permanently unable to call for
            // help behind a party that has stopped clearing it. See
            // DcSocialQuarantine.h.
            DcSocialQuarantine::ReleaseAll(bot);
        }
        ctx->GetValue<ObjectGuid>(DcKey::EngageTrashTarget)->Set(ObjectGuid::Empty);
        ctx->GetValue<std::string&>(DcKey::StallReason)->Get().clear();
        ctx->GetValue<std::string&>(DcKey::LastSaidReason)->Get().clear();
        ctx->GetValue<std::string&>(DcKey::Phase)->Get().clear();
        ctx->GetValue<uint32>(DcKey::CurrentHop)->Set(0u);
        ctx->GetValue<std::map<ObjectGuid, uint32>&>(DcKey::LootSkip)->Get().clear();
        ctx->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Reset();
        ctx->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get() = DungeonFollowerState{};
        // One reset clears the whole approach FSM (the stuck/recovery counters,
        // the pursuit/dead-end latches, the loot-yield anchor, the position
        // sentinel + committed-boss entry, and the long-path cache state) in
        // lockstep — see DcApproachState.
        ctx->GetValue<DcApproachState&>(DcKey::ApproachState)->Get().Reset();
        // One reset clears the whole advanced-pull FSM (phase / dwell timer / camp /
        // breadcrumb trail / abort + tag latches / Dynamic verdict) in lockstep.
        ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get().Reset();
        // Pull-session teardown, previously only on the chat `dc off` path: revert
        // the pull preference to Dynamic, disarm the behavioral bool, and REVOKE the
        // leader's pull-session daze immunity. Folding these in here is the fix for a
        // party-death / dungeon-exit disable (which routes through this function)
        // leaving the tank permanently daze-immune — the revoke was formerly
        // reachable only via ApplyPullSetting and DcOffAction.
        ctx->GetValue<uint32>(DcKey::PullSetting)->Set(2u);
        ctx->GetValue<bool>(DcKey::PullMode)->Set(false);
        if (bot)
            DcLeaderSignal::SetLeaderDazeImmunity(bot, false);
        // Escort mounted-ride safety net: the escort driver speed-matches party
        // BOTS to a mounted escortee (Old Hillsbrad's Thrall, 1.6x) and restores
        // them when the leg ends — but a `dc off` / party-death disable mid-ride
        // routes through HERE, after which the driver never runs again and the
        // boost would be stranded until some aura recalc happens to rewrite the
        // rate. Mirror the daze-immunity revoke above: recompute every party bot's
        // run speed from its real auras (UpdateSpeed drops the non-aura escort
        // boost and is a no-op when the rate is already correct).
        if (bot)
        {
            auto unboost = [](Player* p)
            {
                if (p && p->IsInWorld() && GET_PLAYERBOT_AI(p))
                    p->UpdateSpeed(MOVE_RUN, /*forced*/ true);
            };
            unboost(bot);
            if (Group* group = bot->GetGroup())
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    if (Player* member = ref->GetSource(); member && member != bot &&
                                                           member->GetMapId() == bot->GetMapId())
                        unboost(member);
        }
        DcStatusPublisher::SendAddonMessage(botAI, "CHAT\t" + reason);
        botAI->DoSpecificAction("dc status", Event(), true);

        // Run-end observer for the `.dc test` harness — cheap GUID compare,
        // no-op unless this leader is the monitored test run's tank.
        DcTestRunManager::Instance().OnRunDisabled(bot, reason);
    }


    // Mode stays enabled. Sets a stall reason that the fallback trigger uses
    // to fire, and that `dc status` can report. Announces the reason in party
    // chat the first time it changes — repeats are suppressed until either
    // the reason text changes or `dc skip`/`dc on`/`dc off` clears it.
    void StallDungeonClear(PlayerbotAI* botAI, std::string const& reason)
    {
        AiObjectContext* ctx = botAI->GetAiObjectContext();
        ctx->GetValue<std::string&>(DcKey::StallReason)->Get() = reason;

        std::string& lastSaid = ctx->GetValue<std::string&>(DcKey::LastSaidReason)->Get();
        if (lastSaid != reason)
        {
            lastSaid = reason;
            DcStatusPublisher::SendAddonMessage(botAI, "CHAT\t" + reason);
            botAI->DoSpecificAction("dc status", Event(), true);
        }
    }


    void ClearStall(AiObjectContext* ctx)
    {
        ctx->GetValue<std::string&>(DcKey::StallReason)->Get().clear();
        ctx->GetValue<std::string&>(DcKey::LastSaidReason)->Get().clear();
    }


    // Record the navigation micro-activity for this advance tick so the ~2s
    // status poll can report a fine-grained state to the addon (see
    // DungeonClearPhaseValue / DcStatusAction). Cheap string assignment; called
    // from each terminal movement/recovery branch of Advance::Execute. Tokens:
    // "moving", "pursuing", "recovering".
    void SetPhase(AiObjectContext* ctx, std::string const& phase)
    {
        ctx->GetValue<std::string&>(DcKey::Phase)->Get() = phase;
    }


    // Party-readiness gate for resuming the advance (HP/MP/spread). Shared body
    // in DcPartyState (one implementation with the trigger ladder, which had
    // drifted as two copies). requireNoLoot is false here: loot is handled
    // separately in Execute so it can enforce a commit-timeout; see the
    // loot-yield block there.
    bool IsBetweenPullsReady(Player* bot, AiObjectContext* context)
    {
        // Memoised within the tick (loose variant); see DcTickMemo.
        return DcTickMemoAccess::BetweenPullsReady(bot, context, /*requireNoLoot*/ false);
    }


    // Commit a freshly-built path into the cache and reset the follower so we
    // don't index off the end of a shorter polyline. Shared by the sync and
    // async install sites.
    // `builtToward` is the position the route was actually built to reach; the
    // retarget baseline (longPathTargetPos, measured by EnsureLongPath's `moved`
    // drift check) must be stamped from it, NOT the current-tick target coords.
    // They differ on the async path: the route was built toward the submit-time
    // coords (raw.tx/ty/tz) while `target` may have drifted during the pending
    // window (a patrolling boss). nullptr = built toward the target's live coords
    // (all synchronous sites).
    void InstallLongPath(Player* bot, AiObjectContext* ctx, DcApproachState& appr,
                         DungeonBossInfo const& target,
                         ChunkedPathfinder::Result&& built, uint32 now, char const* how,
                         Position const* builtToward = nullptr)
    {
        // PHANTOM-DECK GUARD. Map 36's navmesh carries the Westfall surface
        // ~200y above the mine as valid-looking walkable mesh. A route whose
        // start poly snapped onto that deck builds a "complete" polyline that
        // runs the deck and ends in mid-air over the boss (live
        // tr-20260822-232939-1: the whole party marched at Z 263-297 toward
        // Sneed at Z 49 until the no-progress watchdog fired). The boss's own
        // spawn Z is ground truth for the target floor: a complete route
        // whose final point ends far off it never reaches the boss - blank it
        // to NOPATH so the caller's recovery (floor-banded column snap) runs
        // instead of walking the deck.
        if (built.complete && !built.segments.empty() &&
            !built.segments.back().polyline.empty())
        {
            float const endZ = built.segments.back().polyline.back().z;
            if (std::fabs(endZ - target.z) > 40.0f)
            {
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] route to {} rejected: ends {:.0f}y off the boss floor "
                         "(phantom deck) -> forcing recovery",
                         bot->GetName(), target.name, std::fabs(endZ - target.z));
                built.segments.clear();
                built.complete = false;
                built.reachable = false;
                built.failureReason = "ends off the boss floor (phantom deck)";
            }
        }

        ChunkedPathfinder::Result& path =
            ctx->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Get();
        path = std::move(built);
        appr.longPathTargetEntry = target.entry;
        appr.longPathTargetPos = builtToward ? *builtToward : Position(target.x, target.y, target.z);
        appr.longPathExpiresMs = now + DC_LONG_PATH_TTL_MS;

        size_t const firstSegPts = path.segments.empty() ? 0 : path.segments.front().polyline.size();
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] path rebuilt -> {} ({}): segs={} firstSegPts={} complete={}{}",
                 bot->GetName(), target.name, how,
                 path.segments.size(), firstSegPts, path.complete,
                 path.reachable ? "" : (" UNREACHABLE: " + path.failureReason));

        ctx->GetValue<uint32>(DcKey::CurrentHop)->Set(0u);
        ctx->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get() = DungeonFollowerState{};

        // Re-baseline the TTL-defer progress cursor to the freshly-reset follower
        // (segment 0, point 0) so the first real advance reads as progress.
        appr.lastProgressSegmentIdx = 0;
        appr.lastProgressPointIdx = 0;
    }


    // Rebuild the cached long-path when the boss entry changes or the TTL
    // expires. Idempotent — safe to call every Advance tick.
    //
    // With DungeonClear.AsyncPathfinding ON (default) the heavy navmesh A* runs
    // on DcPathWorker's background thread so it can't micro-stutter the map
    // tick. This call then becomes: drain a finished job and install it, or (if
    // a rebuild is due and nothing is in flight) submit one and keep serving the
    // current path until it lands. Boss changes briefly clear the cache (no
    // valid path for the new target exists yet); TTL refreshes keep walking.
    //
    // Hand-tuned anchor routes are cheap (snap-only, no A*) and must take
    // precedence over the LongRange corridor, so when one is registered for the
    // boss we build synchronously and skip the worker entirely. The worker only
    // ever runs LongRangePathfinder::BuildCoreFromMesh.
    void EnsureLongPath(Player* bot, AiObjectContext* ctx, DcApproachState& appr,
                        DungeonBossInfo const& target)
    {
        uint32& cachedEntry = appr.longPathTargetEntry;
        uint32& expiresAt = appr.longPathExpiresMs;
        uint64& pendingJob = appr.pendingPathJob;
        uint32& pendingSince = appr.pendingPathSinceMs;
        uint32 const now = getMSTime();

        // Mid-teleport (NearTeleportTo ack still pending), the bot's readable
        // position is the PRE-teleport one — any route submitted or built from it
        // starts in the wrong place (the post-Brazen wrong-direction sprint).
        // Wait the tick or two until the ack lands and the position is real.
        if (bot->IsBeingTeleported())
            return;

        bool const asyncEnabled = DcSettings::GetBool(bot, "AsyncPathfinding");

        // ---- 1. Drain a completed async build ----
        if (pendingJob)
        {
            LongRangePathfinder::RawResult raw;
            uint32 jobEntry = 0;
            uint32 jobMap = 0;
            if (!DcPathWorker::Instance().TryTake(pendingJob, raw, jobEntry, jobMap))
            {
                // No result yet. A real build is a single sub-tick Detour query,
                // so a multi-second wait means the result was lost (swept by the
                // reaper after an afk dc-off, since not every reset path clears
                // the pending jobId) or the worker is wedged/dead. Abandon the
                // job and rebuild synchronously this tick — otherwise step 1
                // short-circuits forever and no toggle/skip can recover it.
                if (getMSTimeDiff(pendingSince, now) < DC_ASYNC_PATH_PENDING_TIMEOUT_MS)
                    return;  // still building — keep serving the cached path

                // INFO so it's trackable: this is the only path that runs a full
                // long-range build on the world thread in async mode, which is
                // exactly what async exists to avoid. It should be rare (lost
                // result / wedged worker); frequent hits mean the worker is
                // unhealthy and want investigating.
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] ASYNC PATH SYNC-FALLBACK: job {} gave no result in {}ms — "
                         "abandoning and rebuilding on the world thread (worker swept/stalled?)",
                         bot->GetName(), pendingJob, getMSTimeDiff(pendingSince, now));
                pendingJob = 0;
                pendingSince = 0;
                ChunkedPathfinder::Result built =
                    ChunkedPathfinder::Build(bot, target.mapId, target.entry, target.x, target.y, target.z);
                InstallLongPath(bot, ctx, appr, target, std::move(built), now, "sync (async timeout)");
                return;
            }

            pendingJob = 0;
            pendingSince = 0;

            // Discard a result the world has moved past: boss changed, the bot
            // zoned, or the bot was RELOCATED since submit (TeleportParty / event
            // repositioning) so the route's start is no longer where the bot is —
            // installing that one re-enters the polyline with a straight opening
            // spline back toward the old start and the tank sprints the wrong way.
            bool const startDrifted =
                bot->GetExactDist(&appr.pendingPathStartPos) > DC_ASYNC_PATH_START_DRIFT_MAX;
            bool const stale = (jobEntry != target.entry) || (jobMap != bot->GetMapId()) ||
                               startDrifted;
            if (!stale)
            {
                ChunkedPathfinder::Result built = LongRangePathfinder::Finalize(bot, raw);
                if (!built.reachable && !built.startFarFromPoly)
                {
                    // LongRange couldn't reach the boss; run the lighter
                    // anchor/bee-line/arc/spawn-graph fallback tiers on the map
                    // thread WITHOUT redoing the heavy A* we already offloaded.
                    built = StridedPathfinder::Build(bot, target.mapId, target.entry,
                                                     target.x, target.y, target.z, 16, /*skipLongRange*/ true);
                }
                // Stamp the retarget baseline from the coords the route was BUILT
                // toward (raw.tx/ty/tz), not target's possibly-drifted live coords,
                // so EnsureLongPath's `moved` check measures live-boss drift against
                // the right anchor.
                Position const builtToward(raw.tx, raw.ty, raw.tz);
                InstallLongPath(bot, ctx, appr, target, std::move(built), now, "async", &builtToward);
                return;
            }
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] discarded stale async path (built for boss {} map {}, now boss {} "
                      "map {}, start drift {:.0f}yd)",
                      bot->GetName(), jobEntry, jobMap, target.entry, bot->GetMapId(),
                      bot->GetExactDist(&appr.pendingPathStartPos));
            // fall through to re-evaluate need and maybe resubmit
        }

        // ---- 2. Is a (re)build due? ----
        bool const targetChanged = cachedEntry != target.entry;
        // The live boss relocated (pool/wandering boss) or its live position
        // just streamed in far from the static anchor the current path was
        // built toward — retarget early instead of walking a stale route the
        // full TTL. Only meaningful once a path actually exists (expiresAt!=0).
        Position const& builtToward = appr.longPathTargetPos;
        bool const moved = expiresAt != 0 &&
            builtToward.GetExactDist(Position(target.x, target.y, target.z)) >
                DC_LONG_PATH_RETARGET_DIST;

        // TTL rebuild, deferred while the follower is visibly progressing. The
        // three reasons the TTL is kept short are all now covered out-of-band:
        // live-boss drift by `moved` above, boss change by `targetChanged`, and
        // stuck recovery by the expiresAt=0 forced invalidations. So a bare TTL
        // expiry on a route the bot is actively walking only triggers a full
        // A* + Finalize rebuild of a perfectly good path (and resets the
        // follower cursor). Honour the TTL only once forward progress has
        // stalled: while the cursor has advanced past the last baseline AND the
        // bot isn't position-stuck, treat the route as fresh and re-arm the
        // deadline from now. expiresAt==0 (no usable path yet) always rebuilds.
        DungeonFollowerState const& follower =
            ctx->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get();
        bool const cursorAdvanced =
            follower.segmentIdx > appr.lastProgressSegmentIdx ||
            (follower.segmentIdx == appr.lastProgressSegmentIdx &&
             follower.pointIdx > appr.lastProgressPointIdx);
        bool const progressing = cursorAdvanced && appr.routeGlideWatch.stuckTicks == 0;
        bool const ttlPassed = expiresAt != 0 && now >= expiresAt;
        bool const expired = expiresAt == 0 || (ttlPassed && !progressing);

        if (!targetChanged && !expired && !moved)
        {
            // Not rebuilding this tick. If we deferred a lapsed TTL because the
            // bot is progressing, re-arm the deadline and roll the progress
            // baseline forward so the backstop measures the NEXT window and only
            // fires a full TTL after progress actually stalls.
            if (ttlPassed && progressing)
            {
                expiresAt = now + DC_LONG_PATH_TTL_MS;
                appr.lastProgressSegmentIdx = follower.segmentIdx;
                appr.lastProgressPointIdx = follower.pointIdx;
            }
            return;
        }

        // ---- 3. Anchor route OR sync mode → build inline ----
        // Anchor-route lookup is O(1)-ish and navmesh-only; a registered route
        // means the synchronous build is cheap (no A*), so there's nothing to
        // offload. Sync mode (toggle OFF) always builds inline.
        Map* map = bot->FindMap();
        bool hasAnchorRoute = false;
        if (map)
            hasAnchorRoute = DungeonClearRouteRegistry::Get(target.mapId, map->GetDifficulty(),
                                                            target.entry) != nullptr;

        if (!asyncEnabled || hasAnchorRoute)
        {
            ChunkedPathfinder::Result built =
                ChunkedPathfinder::Build(bot, target.mapId, target.entry, target.x, target.y, target.z);
            InstallLongPath(bot, ctx, appr, target, std::move(built), now,
                            !asyncEnabled ? "sync" : "sync (anchor route)");
            return;
        }

        // ---- 4. Async submit ----
        // On a true boss change there is no valid path for the new target, so
        // clear the cache + follower (Advance stalls briefly until the job
        // lands). On a TTL-only refresh leave the cache walking.
        if (targetChanged)
        {
            ctx->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Reset();
            cachedEntry = target.entry;   // stop re-detecting "boss changed" every tick
            expiresAt = 0;                // mark "no usable path yet"
            ctx->GetValue<uint32>(DcKey::CurrentHop)->Set(0u);
            ctx->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get() = DungeonFollowerState{};
        }

        // One in-flight job per bot; nothing to do until it returns.
        if (pendingJob)
            return;

        // Pin the navmesh alive across the worker round-trip. If the map has no
        // navmesh, fall back to a synchronous build so the bot never wedges.
        std::shared_ptr<dtNavMesh> meshRef =
            map ? map->GetMapCollisionData().GetMMapNavMeshSharedPtr() : std::shared_ptr<dtNavMesh>();
        if (!meshRef)
        {
            ChunkedPathfinder::Result built =
                ChunkedPathfinder::Build(bot, target.mapId, target.entry, target.x, target.y, target.z);
            InstallLongPath(bot, ctx, appr, target, std::move(built), now, "sync (no navmesh)");
            return;
        }

        pendingJob = DcPathWorker::Instance().Submit(
            bot->GetMapId(), target.entry, bot->GetObjectGuid(), std::move(meshRef),
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
            target.x, target.y, target.z);
        pendingSince = now;
        appr.pendingPathStartPos = bot->GetPosition();  // drain-time start-drift baseline

        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] async path submitted job={} -> {} ({})",
                  bot->GetName(), pendingJob, target.name, targetChanged ? "boss changed" : "TTL");
    }


    // The escort-spline stop primitives (StopActiveSplineGlide / HaltForHold)
    // now live in DcMovement, the single movement funnel. Call sites use
    // DcMovement::StopBot(bot, Stop::Hold) (the old HaltForHold), the
    // DcMoveTo funnel (which folds in the escort-conflict teardown), or
    // DcMovement::ResolveEscortConflict (a bare glide-kill before a non-move).

    // Face `unit` if we're meaningfully off-axis. One facing packet when needed,
    // none when already facing (the HasInArc guard keeps it idempotent across a
    // multi-second hold), so it is safe to call every hold tick. Used at the
    // pull's park-and-wait sites: a tank parked side-on to the pack it is about
    // to pull, or a party staring in random directions at camp, are instant bot
    // tells. Never call this mid-glide — facing packets on a moving bot cause
    // rubber-banding.
    void DcFaceIfNeeded(Player* bot, Unit* unit)
    {
        if (!bot || !unit || unit == bot || bot->HasInArc(CAST_ANGLE_IN_FRONT, unit))
            return;
        ServerFacade::instance().SetFacingTo(bot, unit);
    }

    // See the header for why a recall has to ask this before it hauls anyone home.
    bool DcInGroundEffect(AiObjectContext* ctx)
    {
        if (!ctx)
            return false;

        Aura* const aura = ctx->GetValue<Aura*>(DcKey::Stock::AreaDebuff)->Get();
        // Same staleness guard AvoidAoeAction applies to the same value: the read is
        // cached for a tick and the dynamic object can be gone inside it.
        return aura && !aura->IsRemoved() && !aura->IsExpired();
    }

    // See the header for why a rung whose destination is the ROUTE has to re-ask
    // this after its own trigger already answered it.
    bool PullOwnsTheTank(Player* bot, AiObjectContext* ctx, char const* rung)
    {
        if (!bot || !ctx)
            return false;

        DcPullContext const& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();

        // A latched scripted row owns the tank outright, with no timing window:
        // the plan decides where it stands, walks and drags, and its destination
        // is never the boss. Unbounded is safe because the maneuver retires the
        // row as soon as its pack is off the party, whatever the combat flag says.
        bool const scriptedLatched = pull.scriptedStage >= 0;
        // Any other maneuver owns it for the bounded valve — long enough to cover
        // the legs, short enough that a wedged phase can never silence the run's
        // only driver forever.
        bool const midManeuver =
            pull.phase != DcPullPhase::Idle &&
            getMSTimeDiff(pull.phaseSince, getMSTime()) < DC_PULL_ADVANCE_STANDDOWN_MAX_MS;

        if (!scriptedLatched && !midManeuver)
            return false;

        // Logged because REACHING this line is itself the diagnosis: the trigger
        // stood this rung down and the engine ran it anyway off a stale basket.
        // If this line never appears again the race is closed; if it does, it names
        // the rung that would have driven the tank.
        DC_PULL_DEBUG("[DC:{}] {} stood down: a pull owns the tank (phase {}, "
                      "scripted stage {}) — already-queued basket",
                      bot->GetName(), rung ? rung : "route rung",
                      static_cast<uint32>(pull.phase), pull.scriptedStage);
        return true;
    }

}  // namespace DcActionShared

// Arbiter-funneled point move. The single seam through which DC actions issue a
// MovementAction::MoveTo: refuse while the run is paused (killing the
// queued-action race), drop a stale escort glide that would otherwise coast
// under the new move, then delegate. Same own-the-tick semantics as the inherited
// MoveTo. Lives here in Part 1; moves to DcActionShared with the file split.
bool DcMovementAction::DcMoveTo(uint32 mapId, float x, float y, float z, bool idle, bool react,
                                bool normal_only, bool exact_waypoint, MovementPriority priority,
                                bool lessDelay, bool backwards)
{
    if (!DcMovement::DcMovementAllowed(botAI))
        return false;
    DcMovement::ResolveEscortConflict(bot);

    // (1) GROUND-SNAP THE DESTINATION Z.
    // Stock SearchForBestPath only takes its fast path when the requested z is within
    // 0.5yd of GetMapHeight at that xy (MovementActions.cpp:1734). Miss that window and
    // it drops into a z-search that samples just THREE candidates
    // (AiPlayerbot.MaxMovementSearchTime = 3, a count despite the name) — so a
    // destination whose z is merely a yard off the local floor can fail outright. DC
    // hands out exactly such points: the heal-reposition fallback interpolates x/y
    // toward the target but keeps the TARGET's z (DcFollowerActions.cpp), which is not
    // the ground height at the interpolated xy. Snap here, once, so every DC caller
    // gets a well-formed destination instead of each fixing it by hand.
    //
    // Take only the SNAPPED Z, and only when the snap landed essentially under the
    // requested point — this must correct height, never relocate the destination. A
    // caller that asked for an exact waypoint gets its literal point untouched.
    //
    // The Z delta is capped deliberately. NavmeshSnap searches a 10yd VERTICAL extent,
    // so in stacked geometry (Steamvault's walkways over the pump room, any multi-level
    // instance) the nearest poly can belong to a different FLOOR — applying that would
    // silently retarget the move one storey off. The miss we are correcting is the
    // 0.5yd fast-path window, so real corrections are small; anything larger means the
    // caller's z is probably the honest one and we leave it alone.
    constexpr float kMaxSnapZCorrection = 3.0f;
    float destZ = z;
    if (!exact_waypoint)
    {
        NavmeshSnap::Result const snap = NavmeshSnap::Snap(bot, x, y, z, /*maxRadius*/ 5.0f);
        if (snap.ok && std::fabs(snap.x - x) < 1.0f && std::fabs(snap.y - y) < 1.0f &&
            std::fabs(snap.z - z) < kMaxSnapZCorrection)
            destZ = snap.z;
    }

    bool moved = MoveTo(mapId, x, y, destZ, idle, react, normal_only, exact_waypoint, priority,
                        lessDelay, backwards);

    // (2) BYPASS THE Z-SEARCH ON REFUSAL.
    // SearchForBestPath seeds `min_length` from its FIRST path attempt whether or not
    // that attempt succeeded, then only accepts a later candidate that is STRICTLY
    // SHORTER (MovementActions.cpp:1755). Once that seed is a short degenerate path,
    // no genuine route can ever beat it: `found` stays false, `modified_z` keeps its
    // INVALID_HEIGHT initialiser, and MoveTo returns false on that destination
    // forever. Live, this froze bots for thousands of consecutive ticks — including
    // Xomja refused 45x on a destination 1.7yd away, which is how we know it is not
    // route length or connectivity. The map-545 nav probe
    // (t/fixtures/nav/steamvault_stranded_origin.json) confirms the mesh under every
    // one of those strand origins is walkable and routable.
    //
    // exact_waypoint routes stock to DoMovePoint and skips SearchForBestPath entirely
    // (MovementActions.cpp:215), so one retry defeats the ratchet whatever seeded it.
    // Gated on "refused AND standing still" so healthy movement never reaches here: a
    // refusal mid-walk is the ordinary duplicate/last-move guard and must stay a no-op.
    // Path generation is still on inside DoMovePoint — this trades stock's z-search for
    // MotionMaster's own pathing, it does not straight-line the bot through geometry.
    if (!moved && !bot->isMoving() && !exact_waypoint)
    {
        moved = MoveTo(mapId, x, y, destZ, idle, react, normal_only, /*exact_waypoint*/ true,
                       priority, lessDelay, backwards);
        if (moved)
            DC_PULL_TRACE("[DC:{}] move refused by the z-search -> exact-waypoint retry "
                          "RESCUED it (dest {:.1f},{:.1f},{:.1f} at {:.1f}yd)",
                          bot->GetName(), x, y, destZ, bot->GetExactDist(x, y, destZ));
    }

    // A refused move that leaves the bot STANDING STILL is the signature behind every
    // "follower stranded at a fixed spot" deadlock we have chased, and the logs never
    // said which of stock MoveTo's several early-outs did the refusing — so the cause
    // stayed a guess across multiple fix attempts. Name it. A refusal while the bot is
    // already walking is normal (the duplicate/last-move guards reject a re-issue), so
    // only the standing-still case is worth a line. These are protected members of
    // MovementAction, which we derive from, so no core change is needed. Note the
    // guards are re-evaluated here: cheap, but they are a SECOND read, so treat a
    // disagreement with the real call as a timing artifact rather than a lie.
    if (!moved && !bot->isMoving())
    {
        char const* why;
        if (!IsMovingAllowed())
            why = "IsMovingAllowed=false (CanMove: rooted/charmed/frozen/stunned, a "
                  "CONTROLLED motion slot, or being teleported)";
        else if (IsDuplicateMove(x, y, destZ))
            why = "IsDuplicateMove (same destination re-issued inside maxWaitForMove)";
        else if (IsWaitingForLastMove(priority))
            why = "IsWaitingForLastMove (an equal-or-higher priority move still holds "
                  "the delay window — this one is being starved)";
        else
            why = "path/other — and the exact-waypoint retry ALSO failed, so this is "
                  "not the SearchForBestPath z-search ratchet";
        // Report the z actually attempted (post-snap) plus the raw request, so a
        // destination the snap moved is still traceable back to its caller.
        // Promoted TRACE -> throttled INFO (runs 32-40): this line is the
        // whole answer to "route complete, cursor 0/0, party healthy, bot
        // standing" and it sat below the live log level the entire hunt.
        static uint32 s_lastRefusalLogMs = 0;
        uint32 const nowRefMs = getMSTime();
        if (nowRefMs - s_lastRefusalLogMs > 3000)
        {
            s_lastRefusalLogMs = nowRefMs;
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] move REFUSED and not moving -> {} (dest {:.1f},{:.1f},{:.1f} "
                     "[requested z {:.1f}] at {:.1f}yd, prio={})",
                     bot->GetName(), why, x, y, destZ, z, bot->GetExactDist(x, y, destZ),
                     static_cast<uint32>(priority));
        }
    }
    return moved;
}

bool DcMovementAction::FindStandoffPoint(Map* map, Position const& center, float ringRadius,
                                         float maxRadius, float& x, float& y, float& z)
{
    if (!map)
        return false;

    float const cx = center.GetPositionX();
    float const cy = center.GetPositionY();
    float const cz = center.GetPositionZ();

    // Ring of standoff points around the center, ordered bot-side first (shortest
    // reposition / most likely to round the same corner). Take the first that
    // snaps, sits within maxRadius, has LOS to the center, and is reachable.
    std::vector<Position> const cands =
        DungeonClearMath::StandoffCandidates(center, bot->GetPosition(), ringRadius,
                                             /*ringPoints*/ 7);

    constexpr float kEyeBump = 2.0f;  // eye height for the LOS ray (cf. ChordClear)
    for (Position const& c : cands)
    {
        NavmeshSnap::Result const snap =
            NavmeshSnap::Snap(map, c.GetPositionX(), c.GetPositionY(), cz, 8.0f);
        if (!snap.ok)
            continue;

        float const sdx = snap.x - cx;
        float const sdy = snap.y - cy;
        if (std::sqrt(sdx * sdx + sdy * sdy) > maxRadius)
            continue;

        if (!map->isInLineOfSight(snap.x, snap.y, snap.z + kEyeBump, cx, cy,
                                  cz + kEyeBump, bot->GetPhaseMask(),
                                  LINEOFSIGHT_CHECK_VMAP,
                                  VMAP::ModelIgnoreFlags::Nothing))
            continue;

        PathGenerator gen(bot);
        gen.CalculatePath(snap.x, snap.y, snap.z, /*forceDest*/ false);
        if (gen.GetPathType() != PATHFIND_NORMAL)
            continue;

        x = snap.x;
        y = snap.y;
        z = snap.z;
        return true;
    }
    return false;
}

// The shared "DcGlideDriver". One tick of a continuous escort-spline glide along
// a cached route toward its end, with the full wedge / off-path / ride / jump /
// rejoin / spline / fallback ladder. This is the un-instrumented sibling of
// DcAdvanceAction's Tier-B/C effect handlers (which route the same sequence
// through the pure DecideApproach kernel for replay capture); the door walk-in
// driver, which needs no boss-engage observation, calls this directly instead of
// hand-cloning the machinery. Pure movement — the caller owns stall/park
// bookkeeping via the returned outcome.
DcMovementAction::GlideOutcome DcMovementAction::DriveGlideToEnd(
    ChunkedPathfinder::Result const& path, DungeonFollowerState& follower,
    DcApproachState& appr, DcProgressWatchdog& wedgeWatch, uint32 mapId, char const* tag)
{
    // --- Progress-aware wedge detection, sampled BEFORE NextHop so a recovery
    // re-anchor lands before the hop is computed. Narrow, descending walkways
    // micro-stop the glide; the ride-guard below tolerates a momentary
    // isMoving()==false, so count only genuine no-progress-WHILE-MOVING ticks. On
    // a wedge, halt the stuck spline and re-anchor the cursor onto the nearest
    // forward route point; the fresh NextHop + spline issue below restart the
    // glide from a standstill. ---
    {
        Position const cur(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        bool const lastPosValid =
            appr.lastPos.m_positionX != 0.0f || appr.lastPos.m_positionY != 0.0f ||
            appr.lastPos.m_positionZ != 0.0f;
        float const moved = lastPosValid ? cur.GetExactDist(appr.lastPos) : 0.0f;
        uint32 const wedgeTicks =
            wedgeWatch.TickDisplacement(lastPosValid && bot->isMoving(), moved, DC_STUCK_DISPLACEMENT);
        appr.lastPos = cur;

        if (wedgeTicks >= DC_STUCK_TICK_LIMIT)
        {
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] {} wedged ({} ticks) -> halt + re-anchor",
                      bot->GetName(), tag, wedgeTicks);
            wedgeWatch.stuckTicks = 0;
            DcMovement::ResolveEscortConflict(bot);
            DungeonPathFollower::Resnap(bot, path, follower);
        }
    }

    // --- Off-path recovery (knockback / follower bump): re-anchor onto the
    // existing polyline, or (Resnap failed) invalidate the cached path and tell
    // the caller to park. ---
    if (DungeonPathFollower::IsOffPath(bot, path, follower) &&
        follower.offPathTicks >= DungeonPathFollower::OFF_PATH_TICK_LIMIT)
    {
        if (!DungeonPathFollower::Resnap(bot, path, follower))
        {
            DcMovement::ResolveEscortConflict(bot);
            appr.longPathExpiresMs = 0;
            follower = DungeonFollowerState{};
            return GlideOutcome::OffPathLost;
        }
    }

    DungeonPathFollower::Hop hop = DungeonPathFollower::NextHop(bot, path, follower);
    if (hop.isDone)
        return GlideOutcome::ReachedEnd;

    // --- Leave an in-flight escort glide alone, INCLUDING across a momentary
    // isMoving()==false flicker: the ACTIVE escort generator type alone is the
    // "spline still travelling" signal (the core pops it the instant the spline
    // finishes), and the wedge detector above is what catches a genuinely stalled
    // spline. A wedge recovery just above may have halted the escort, in which
    // case we fall through and re-issue. ---
    MotionMaster* mm = bot->GetMotionMaster();
    if (mm && mm->GetCurrentMovementGeneratorType() == ESCORT_MOTION_TYPE)
        return GlideOutcome::Riding;
    if (!IsMovingAllowed())
        return GlideOutcome::Blocked;

    // A jump leg en route (drop-down corridor) — arc it.
    if (hop.isJump)
    {
        JumpTo(mapId, hop.point.x, hop.point.y, hop.point.z, MovementPriority::MOVEMENT_NORMAL);
        return GlideOutcome::Moved;
    }

    // Re-entry leg must be a GENERATED path: if a bump left the bot off the
    // corridor, the escort spline's opening straight leg back to the route clips
    // wall corners. Rejoin via PathGenerator (MoveTo) while off the line; the
    // glide resumes once RouteDeviation drops back under the on-corridor threshold.
    float const deviation = DungeonPathFollower::RouteDeviation(bot, path, follower);
    if (deviation > DungeonPathFollower::OFF_PATH_THRESHOLD)
    {
        DcMoveTo(mapId, hop.point.x, hop.point.y, hop.point.z,
                 /*idle*/ false, /*react*/ false, /*normal_only*/ false,
                 /*exact_waypoint*/ false, MovementPriority::MOVEMENT_NORMAL);
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] {} off-line {:.1f}yd -> rejoining route via generated path "
                  "(seg {} pt {})",
                  bot->GetName(), tag, deviation, follower.segmentIdx, follower.pointIdx);
        return GlideOutcome::Moved;
    }

    // Continuous escort spline along the upcoming polyline run — linear, wall-safe,
    // no per-point stops. SplinePath owns the stand-up / cast-interrupt /
    // MoveSplinePath ritual + LastMovement record and refuses a <2-point window.
    std::vector<G3D::Vector3> const window =
        DungeonPathFollower::BuildSplineWindow(bot, path, follower);
    Movement::PointsArray points(window.begin(), window.end());
    if (DcMovement::SplinePath(botAI, points))
    {
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] {} spline: {} pts (seg {} pt {})",
                  bot->GetName(), tag, points.size(), follower.segmentIdx, follower.pointIdx);
        return GlideOutcome::Moved;
    }

    // Window < 2 points (lone anchor tail): short single-hop fallback.
    DcMoveTo(mapId, hop.point.x, hop.point.y, hop.point.z,
             /*idle*/ false, /*react*/ false, /*normal_only*/ false,
             /*exact_waypoint*/ false, MovementPriority::MOVEMENT_NORMAL);
    return GlideOutcome::Moved;
}
