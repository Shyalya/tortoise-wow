/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearChatActions.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "InstanceScript.h"
#include "Ai/Dungeon/DungeonClear/Action/DcActionShared.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/RoomAggroRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonWingRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPlayerbotCompat.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRezRecovery.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearStateValues.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

namespace
{
    // Refuse a dc command with a reason the issuer can actually receive.
    //
    // TellError whispers the bot's MASTER, and PlayerbotAI::TellError drops the
    // message outright when that master is itself a bot
    // (`GET_PLAYERBOT_AI(master)` bails before the error map). That is exactly
    // the headless `.dc test` case — the party's master is the test driver, a
    // fake-session bot — so every refusal reason vanished silently and the
    // harness's "see the server log for the bot's error chat" pointed at a line
    // nothing ever wrote. Log the ones that could reach nobody; a refusal a
    // real player can read still goes to chat only, so normal play stays quiet.
    void DcRefuse(PlayerbotAI* botAI, Player* bot, std::string const& why)
    {
        botAI->TellError(why);

        Player* master = botAI->GetMaster();
        if (!master || GET_PLAYERBOT_AI(master))
            LOG_WARN("playerbots.dungeonclear", "DC command refused for {}: {}",
                     bot ? bot->GetName() : "<unknown>", why);
    }

    bool IsAuthorized(Player* bot, Event const& event)
    {
        Player* owner = const_cast<Event&>(event).getOwner();
        if (!owner)
            return false;
        // Reject true bots, but allow self-bot players (a real player whose
        // own PlayerbotAI has master == bot). Without this exception the
        // moment a player enables bot self mode they would be silently
        // locked out of every dc command.
        if (GET_PLAYERBOT_AI(owner))
            if (!DcPlayerbotCompat::IsSelfBot(owner))
                return false;
        if (!bot || !bot->GetGroup())
            return false;
        if (bot->GetGroup()->IsMember(owner->GetObjectGuid()))
            return true;
        // GM allowance: the `.dc test` harness (and a GM debugging a run)
        // issues dc commands at a bot party it is deliberately NOT a member
        // of — the whole point of a test run is a pure-bot group.
        return owner->GetSession() &&
               owner->GetSession()->GetSecurity() >= SEC_GAMEMASTER;
    }

    bool AnyPartyMemberDead(Player* bot)
    {
        if (!bot)
            return false;
        if (bot->isDead())
            return true;
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot)
                continue;
            if (member->GetMapId() != bot->GetMapId())
                continue;
            if (member->isDead())
                return true;
        }
        return false;
    }

    std::string FirstDeadName(Player* bot)
    {
        if (!bot)
            return "Someone";
        if (bot->isDead())
            return bot->GetName();
        if (Group* group = bot->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || member == bot)
                    continue;
                if (member->GetMapId() != bot->GetMapId())
                    continue;
                if (member->isDead())
                    return member->GetName();
            }
        }
        return "Someone";
    }

    // Forcefully halt the tank's in-flight DC navigation when DC is turned off,
    // paused, or retargeted. The Advance/DoorBlocked glide is an escort spline
    // (MoveSplinePath -> EscortMovementGenerator at MOTION_SLOT_ACTIVE), and
    // stopping it is harder than it looks:
    //   - MotionMaster::Clear() removes the generator, but
    //     EscortMovementGenerator::DoFinalize() only clears unit states — it
    //     never stops the launched movespline, so the server keeps gliding the
    //     bot to the spline's endpoint.
    //   - Unit::StopMoving() early-returns once movespline->Finalized(), so it is
    //     NOT a reliable way to cancel a spline that is mid-flight or between
    //     windows.
    // StopMovingOnCurrentPos() is the reliable primitive: it DisableSpline()s and
    // launches a zero-length spline at the current position, forcibly overriding
    // whatever was playing. Clear() first so the escort generator can't recalc /
    // relaunch, then the override stops the unit dead. Stock follow reclaims the
    // slot on the next tick.
    void HaltAllMovement(Player* bot)
    {
        if (!bot)
            return;
        if (MotionMaster* mm = bot->GetMotionMaster())
            mm->Clear();
        bot->StopMovingOnCurrentPos();
    }

    // Clears the in-flight advanced-pull maneuver (phase / dwell timer / camp /
    // abort target) WITHOUT touching the pull-mode toggle. Used wherever the run
    // is interrupted (pause, skip, resume, go) so any maneuver mid-flight ends and
    // the party is released (the reaper sees a non-holding phase next tick), while
    // the player's pull-mode preference for the rest of the run is preserved.
    void ResetPullTransient(AiObjectContext* context)
    {
        // One owned struct, one reset: the whole advanced-pull FSM — phase, dwell
        // timer, camp, breadcrumb trail, abort + tag latches, and the Dynamic
        // verdict (decision + per-pack latch + re-check throttle) — clears in
        // lockstep. This is what makes the "stale latch survives pause/skip/resume"
        // bug class structurally impossible: there is exactly one thing to reset,
        // so a field can never be forgotten at one of the 8 call sites. The tag
        // latch and breadcrumb trail used to be omitted here, which is why a stale
        // tag from the previous engagement could make the next pull skip its run-in.
        context->GetValue<DcPullContext&>(DcKey::PullContext)->Get().Reset();
    }

    // Applies an advanced-pull preference (0 Off / 1 On / 2 Dynamic) to the live
    // run state: stores the tri-state setting, keeps the behavioral bool in
    // lock-step (true only for On — Dynamic, like Off, starts disarmed and hands
    // the bool to the per-tick governor), arms/revokes the leader's pull-session
    // daze immunity, and seeds the party camp at the tank's feet when arming On
    // so the party has a hold point before the first real pull marks a camp.
    // Leaving the armed On state (Off / Dynamic) tears down any in-flight
    // maneuver. Shared by `dc on` (which applies whatever preference was pre-set
    // while disabled) and DcPullAction (live toggles during a run).
    void ApplyPullSetting(Player* bot, AiObjectContext* context, uint32 setting)
    {
        bool const active = (setting == 1u);
        context->GetValue<uint32>(DcKey::PullSetting)->Set(setting);
        context->GetValue<bool>(DcKey::PullMode)->Set(active);
        DcLeaderSignal::SetLeaderDazeImmunity(bot, active);
        if (active)
            context->GetValue<DcPullContext&>(DcKey::PullContext)->Get().camp =
                Position(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        else
            ResetPullTransient(context);
    }

    // Shared resume path. Rebuilds the transient navigation cache so Advance
    // restarts fresh from the bot's current position, then clears the pause
    // flags (paused / reason / the auto-paused door). Boss progress (selected
    // boss, skipped set, sticky boss) is deliberately left intact — that is the
    // whole point of resume vs. a fresh `dc on`. Used by both the manual
    // `dc pause` resume branch and the door auto-resume; the caller announces
    // afterward (the two phrase it differently).
    void ResetForResume(AiObjectContext* context)
    {
        // One reset clears the whole approach FSM in lockstep — the stuck/
        // recovery counters, the pursuit/dead-end latches, the loot-yield anchor,
        // the position sentinel + committed boss, and the long-path cache state
        // (so the first Advance tick rebuilds fresh from here). See DcApproachState.
        context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get().Reset();
        context->GetValue<std::string&>(DcKey::StallReason)->Get().clear();
        context->GetValue<std::string&>(DcKey::LastSaidReason)->Get().clear();
        context->GetValue<ObjectGuid>(DcKey::FallbackTarget)->Set(ObjectGuid::Empty);
        context->GetValue<ObjectGuid>(DcKey::EngageTrashTarget)->Set(ObjectGuid::Empty);
        context->GetValue<uint32>(DcKey::CurrentHop)->Set(0u);
        context->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Reset();
        context->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get() = DungeonFollowerState{};

        // Named pause-cluster teardown: paused flag + reason + auto-paused door in
        // lockstep. Boss progress is deliberately untouched — resume, not fresh run.
        DcRun::Of(context).OnResume();
        ResetPullTransient(context);
    }
}

bool DcOnAction::Execute(Event& event)
{
    if (!IsAuthorized(bot, event))
    {
        DcRefuse(botAI, bot, "Not authorized to enable dungeon clear");
        return false;
    }
    // Party chat fans `dc on` out to every bot the master owns, so non-leader
    // bots land here too — non-tanks AND, in a raid, non-leader (off-)tanks.
    // They don't lead the clear, but they must follow the leader while it runs.
    // That follow-tank behavior lives in the "dungeon clear" strategy, applied
    // to every bot at login; re-assert it here as a safety net (e.g. a bot that
    // logged in before contexts registered), then stay quiet — only the leader
    // announces. The follower's follow-tank trigger self-activates off the
    // leader's enabled flag via DungeonClearPartyTankValue, so nothing else is
    // needed for them. Leadership (FindLeaderTank) resolves to exactly one tank
    // bot — lowest-GUID in a party, the Main Tank / best-geared tank in a raid —
    // so exactly one bot takes the path below even with several tanks present.
    if (!DcLeaderSignal::IsDungeonClearLeader(bot))
    {
        if (!botAI->HasStrategy("dungeon clear", BOT_STATE_NON_COMBAT))
            botAI->ChangeStrategy("+dungeon clear", BOT_STATE_NON_COMBAT);
        if (!botAI->HasStrategy("dungeon clear combat", BOT_STATE_COMBAT))
            botAI->ChangeStrategy("+dungeon clear combat", BOT_STATE_COMBAT);
        return true;
    }
    if (!bot->FindMap() || !bot->FindMap()->IsDungeon())
    {
        DcRefuse(botAI, bot, "Not in a dungeon.");
        return false;
    }

    auto const& bosses = AI_VALUE(std::vector<DungeonBossInfo>, DcKey::DungeonBosses);
    if (bosses.empty())
    {
        DcRefuse(botAI, bot, "No bosses found for this map.");
        return false;
    }

    // A corpse only refuses enabling when the post-combat rez flow can't
    // recover it (feature off, or no living rez class) — otherwise the run
    // starts and the recovery immediately holds it while the rez happens.
    if (AnyPartyMemberDead(bot) && !DcRezRecovery::CanRecover(bot))
    {
        DcRefuse(botAI, bot, FirstDeadName(bot) + " is dead — rez and try again.");
        return false;
    }

    // Activate the "dungeon clear" strategy on the non-combat engine. This
    // is what installs the advance/engage/stall trigger ladder that actually
    // drives movement and logging; without it, flipping "enabled" below does
    // nothing (the triggers aren't on any engine to fire). The stock
    // playerbots tree no longer adds it via AiFactory — it must be applied
    // here so both input paths (chat keyword and `.dc` command) work.
    if (!botAI->HasStrategy("dungeon clear", BOT_STATE_NON_COMBAT))
        botAI->ChangeStrategy("+dungeon clear", BOT_STATE_NON_COMBAT);
    if (!botAI->HasStrategy("dungeon clear combat", BOT_STATE_COMBAT))
        botAI->ChangeStrategy("+dungeon clear combat", BOT_STATE_COMBAT);

    // Reset transient state and enable.
    DcRun::Of(context).enabled = true;
    // Register this tank with the event-driven status pusher so the server
    // begins emitting STATUS packets on state transitions (replacing the
    // addon's old 2s poll). The matching UnmarkActiveTank lives in every
    // disable path: dc off, skip-to-empty, and DisableDungeonClear.
    DcStatusPublisher::MarkActiveTank(bot->GetObjectGuid());
    // Named pause-cluster teardown so a fresh run can never start paused / with a
    // stale reason or auto-paused door. (enabled was just set true above.)
    DcRun::Of(context).OnResume();
    ResetPullTransient(context);
    // Start the run already in whatever advanced-pull mode was requested. The
    // preference lives in `dungeon clear pull setting` and survives the disabled
    // window, so `dc pull off/on/dynamic` issued BEFORE `dc on` takes effect the
    // moment the run begins (default Off for a bot that never set it). Applied
    // after ResetPullTransient so the On-mode camp seed isn't wiped.
    ApplyPullSetting(bot, context, AI_VALUE(uint32, DcKey::PullSetting));
    DcRun::Of(context).selectedBossEntry = 0u;
    context->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get().clear();
    context->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get().clear();
    context->GetValue<std::unordered_set<uint32>&>(DcKey::SeenBosses)->Get().clear();
    context->GetValue<std::unordered_set<uint32>&>(DcKey::SeenDueEvents)->Get().clear();
    context->GetValue<std::map<ObjectGuid, uint32>&>(DcKey::LootSkip)->Get().clear();
    // Fresh approach FSM: clears every stuck/recovery counter, the pursuit/dead-
    // end latches, the loot-yield anchor, the position sentinel + committed boss,
    // and the long-path cache state in lockstep. The cache reset (expires/target)
    // forces a fresh long-path build on the first Advance tick so a stale path
    // from a previous `dc on`/`dc off` cycle can't be reused. See DcApproachState.
    context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get().Reset();
    context->GetValue<std::string&>(DcKey::StallReason)->Get().clear();
    context->GetValue<std::string&>(DcKey::LastSaidReason)->Get().clear();
    context->GetValue<std::string&>(DcKey::Phase)->Get().clear();
    context->GetValue<ObjectGuid>(DcKey::FallbackTarget)->Set(ObjectGuid::Empty);
    context->GetValue<ObjectGuid>(DcKey::EngageTrashTarget)->Set(ObjectGuid::Empty);
    context->GetValue<uint32>(DcKey::CurrentHop)->Set(0u);
    context->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Reset();
    // The breadcrumb trail is part of the pull context already cleared by
    // ResetPullTransient above.
    context->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get() = DungeonFollowerState{};
    // Drop any active submerged swim leg so a stale 3D route can't resume on the
    // next run (the drive path also self-invalidates, but this is the clean
    // teardown alongside the rest of the run state).
    context->GetValue<DungeonClearSwimState&>(DcKey::SwimState)->Get().Reset();

    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    std::string const target = next.has_value() ? next->name : "the next boss";
    DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tDungeon clear enabled. Heading to " + target + ".");

    // Trigger instant status addon message update
    botAI->DoSpecificAction("dc status", event, true);
    return true;
}

bool DcOffAction::Execute(Event& event)
{
    if (!IsAuthorized(bot, event))
    {
        DcRefuse(botAI, bot, "Not authorized to disable dungeon clear");
        return false;
    }

    // Non-leader followers have nothing to disable: their follow-tank trigger
    // deactivates on its own the instant the leader clears its enabled flag
    // (DungeonClearPartyTankValue then returns null), so they revert to
    // following the player automatically. Leave the inert strategy installed
    // and stay quiet — they must NOT strip it, or they'd stop following the
    // leader on a subsequent `dc on`.
    if (!DcLeaderSignal::IsDungeonClearLeader(bot))
        return true;

    // The full run/pull teardown — enabled flag, pause state, pull setting→Dynamic,
    // pull-mode bool, daze-immunity revoke, selected boss, approach + pull FSM
    // resets, loot-skip/phase clears, long-path + follower-state resets, and the
    // addon status push — now lives in ONE place so the chat `dc off` path stays in
    // lockstep with the auto-disable paths (party death / all-cleared / map-exit),
    // which all route through DisableDungeonClear. This closes the drift where
    // those paths never revoked the leader's daze immunity. The same "CHAT\t<reason>"
    // addon line and `dc status` refresh are emitted inside it.
    DcActionShared::DisableDungeonClear(botAI, "Dungeon clear disabled.");

    // dc-off-only extras the shared teardown doesn't cover: clear the fallback
    // target, then hard-stop any in-flight advance/engage spline so the bot visibly
    // halts the instant the player says `dc off` (without this a queued spline runs
    // on to its endpoint and the bot looks like it ignored the command for several
    // seconds). See HaltAllMovement for why this can't gate on isMoving(). The
    // "dungeon clear" strategy stays installed but inert — it stays resident on
    // every bot via the login script regardless.
    context->GetValue<ObjectGuid>(DcKey::FallbackTarget)->Set(ObjectGuid::Empty);
    HaltAllMovement(bot);
    return true;
}

bool DcSkipAction::Execute(Event& event)
{
    if (!IsAuthorized(bot, event))
    {
        DcRefuse(botAI, bot, "Not authorized to skip");
        return false;
    }
    // Only the leader owns the run state. Non-leaders reached via the party-chat
    // keyword fan-out stay quiet (the command/addon path already targets only
    // the leader). Without this they'd each error "not enabled".
    if (!DcLeaderSignal::IsDungeonClearLeader(bot))
        return true;
    if (!DcRun::Of(context).enabled)
    {
        DcRefuse(botAI, bot, "Dungeon clear is not enabled.");
        return false;
    }

    // A due off-path CONDITIONAL event preempts the boss pull (trigger rel 31),
    // so a `dc skip` while one is active should retire THAT event — latch its
    // synthetic key cleared+skipped so it stops firing — rather than skipping the
    // boss behind it. Required events that the bot can't drive are unstuck here.
    if (Map* map = bot->FindMap())
    {
        if (DungeonEvent const* ev =
                DungeonEventExecutor::FindDueConditionalEvent(bot, context, map->GetId()))
        {
            uint32 const latchKey = DungeonEventExecutor::ConditionalLatchKey(ev->id);
            auto& cleared =
                context->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get();
            cleared.insert(latchKey);
            context->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get().insert(latchKey);
            context->GetValue<std::string&>(DcKey::StallReason)->Get().clear();
            context->GetValue<std::string&>(DcKey::LastSaidReason)->Get().clear();
            DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tSkipped event '" + ev->name + "'.");
            botAI->DoSpecificAction("dc status", event, true);
            return true;
        }
    }

    std::optional<DungeonBossInfo> current = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!current.has_value())
    {
        DcRefuse(botAI, bot, "No current boss to skip.");
        return false;
    }

    std::unordered_set<uint32>& skipped =
        context->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get();
    skipped.insert(current->entry);

    // New target gets a clean slate (abort any in-flight pull on the old target).
    ResetPullTransient(context);
    DcRun::Of(context).selectedBossEntry = 0u;
    // New target gets a clean approach FSM: counters, latches, loot-yield anchor,
    // position sentinel + committed boss, and the long-path cache (so the next
    // Advance tick rebuilds the route for the new target) all reset in lockstep.
    context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get().Reset();
    context->GetValue<std::string&>(DcKey::StallReason)->Get().clear();
    context->GetValue<std::string&>(DcKey::LastSaidReason)->Get().clear();
    context->GetValue<ObjectGuid>(DcKey::FallbackTarget)->Set(ObjectGuid::Empty);
    context->GetValue<ObjectGuid>(DcKey::EngageTrashTarget)->Set(ObjectGuid::Empty);
    context->GetValue<uint32>(DcKey::CurrentHop)->Set(0u);
    context->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Reset();
    context->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get() = DungeonFollowerState{};

    // Force the cached next-boss value to recompute on next call.
    context->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Reset();
    std::optional<DungeonBossInfo> after = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);

    if (after.has_value())
    {
        DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tSkipped " + current->name + ". Heading to " + after->name + ".");
    }
    else
    {
        DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tSkipped " + current->name + ". No bosses left \xe2\x80\x94 disabling.");
        DcRun::Of(context).enabled = false;
        DcStatusPublisher::UnmarkActiveTank(bot->GetObjectGuid());
    }

    // Trigger instant status addon message update
    botAI->DoSpecificAction("dc status", event, true);
    return true;
}

bool DcStatusAction::Execute(Event& event)
{
    // Only the leader reports — both the command/addon dispatch and the internal
    // refresh calls (from dc on/off/skip/go/pause) run on the leader, while the
    // party-chat keyword fans out to every bot. Without this gate a raid's
    // non-leader tanks would each emit a duplicate STATUS line to the addon.
    if (!DcLeaderSignal::IsDungeonClearLeader(bot))
        return true;

    bool const enabled = DcRun::Of(context).enabled;
    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    auto const& skipped = AI_VALUE(std::unordered_set<uint32>&, DcKey::Skipped);
    std::string const& stall = AI_VALUE(std::string&, DcKey::StallReason);

    std::string const param = event.getParam();
    bool const silent = (param == "addon" || param == "silent");

    if (!silent)
    {
        std::ostringstream msg;
        msg << "Dungeon clear: " << (enabled ? "on" : "off")
            << ". Next boss: " << (next.has_value() ? next->name : "none")
            << ". Skipped: " << skipped.size() << ".";
        if (!stall.empty())
            msg << " Stalled: " << stall;
        DcStatusPublisher::SendAddonMessage(botAI, "CHAT\t" + msg.str());
    }

    // The STATUS payload itself is built by the shared helper so the on-demand
    // path here and the event-driven change-detector (TickStatusPushes) emit
    // byte-identical strings and can never drift. PushStatus also refreshes the
    // detector's "last pushed" snapshot for this tank, so an explicit `dc
    // status` won't trigger a redundant push on the following tick.
    DcStatusPublisher::PushStatus(botAI);
    return true;
}

bool DcBossesAction::Execute(Event& event)
{
    // Only the leader answers the boss-list request — the addon tracks the
    // leader, and the chat-keyword fan-out would otherwise have every raid tank
    // reply with a duplicate list.
    if (!DcLeaderSignal::IsDungeonClearLeader(bot))
        return true;

    // Clear stale completion latches the first time the leader builds the list in a
    // new instance. This action answers the addon's boss-list request even while DC
    // is OFF — when NextDungeonBossValue (the other caller) isn't running — so it
    // must do the new-instance reset itself, or a re-entered dungeon shows a
    // completed objective/event as "Done" until the player toggles dc on.
    DcTargeting::ResetCompletionLatchesForNewInstance(bot, context);

    auto const& bosses = AI_VALUE(std::vector<DungeonBossInfo>, DcKey::DungeonBosses);

    std::string const param = event.getParam();
    bool const silent = (param == "addon" || param == "silent");

    DcStatusPublisher::SendAddonMessage(botAI, "BOSS_START");

    if (bosses.empty())
    {
        if (!silent)
            DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tNo bosses found for this map.");
        DcStatusPublisher::SendAddonMessage(botAI, "BOSS_END");
        return true;
    }

    auto const& skipped = AI_VALUE(std::unordered_set<uint32>&, DcKey::Skipped);
    auto const& cleared = AI_VALUE(std::unordered_set<uint32>&, DcKey::ClearedAnchors);
    auto& seen = AI_VALUE(std::unordered_set<uint32>&, DcKey::SeenBosses);

    // The completed-encounter mask is the authoritative "the group killed it"
    // signal: Map::UpdateEncounterState (driven from KillRewarder) flips bit
    // (1 << DungeonEncounter.dbc index), and BossSpawnIndex stores that exact
    // index in encounterIndex, so the bit lines up directly. This is the same
    // kill record NextDungeonBossValue uses to advance the clear — see the long
    // note there on why GetCompletedEncounterMask is correct and GetBossState is
    // not. Reading kill state from the mask (and not from corpse presence) is the
    // crux of the fix: a boss corpse despawns about a minute after death, so the
    // old "present-but-no-live-creature == dead" heuristic made every killed boss
    // silently flip from "dead" to "missing" the moment its body vanished.
    InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
    uint32 const completedMask = inst ? inst->GetCompletedEncounterMask() : 0u;

    // Resolve the room-aggro anchor boss (the first room-aggro boss in this
    // wing-filtered list) up front — a room-aggro pre-clear event has no boss
    // entry of its own, so it folds onto that boss's row. Computed here (before
    // the boss loop) because the boss rows below carry the folded annotation.
    bool haveRoomAggroBoss = false;
    uint32 roomAggroBossEntry = 0;
    uint32 roomAggroBossKey = 0;
    for (DungeonBossInfo const& info : bosses)
    {
        if (info.kind != DungeonAnchorKind::Boss)
            continue;
        if (!RoomAggroRegistry::Find(bot->GetMapId(), info.entry))
            continue;
        if (!haveRoomAggroBoss || BossOrderKey(info) < roomAggroBossKey)
        {
            roomAggroBossEntry = info.entry;
            roomAggroBossKey = BossOrderKey(info);
            haveRoomAggroBoss = true;
        }
    }

    // A conditional event that gates a specific boss (PanelBeforeBoss, or a
    // room-aggro pre-clear) is shown FOLDED into that boss's row as a small
    // sub-line — not as its own row with a "Go" button that can't resolve (the
    // event isn't a creature, so `dc go <latchKey>` used to fail with "boss
    // doesn't exist"). Build a per-boss-entry annotation here and remember which
    // events were folded so the standalone-event loop below skips them. Events
    // with no gated boss stay standalone (rendered read-only addon-side).
    std::map<uint32, std::string> eventAnnotation;  // boss/objective entry -> note
    std::unordered_set<uint32> foldedEventIds;
    // A faction-specific event (panelTeam set) is shown only to its own team —
    // the activation predicate already gates execution by team, so the other
    // faction would only ever see a row it can never act on.
    TeamId const myTeam = bot->GetTeamId();
    auto const hiddenForTeam = [myTeam](DungeonEvent const* ev)
    {
        return ev->panelTeam != TEAM_NEUTRAL && ev->panelTeam != myTeam;
    };
    // Difficulty-gated events are hidden from the other difficulty's panel.
    Difficulty const panelDifficulty =
        bot->FindMap() ? bot->FindMap()->GetDifficulty() : DUNGEON_DIFFICULTY_NORMAL;

    for (DungeonEvent const* ev : DungeonEventRegistry::Conditional(bot->GetMapId(), panelDifficulty))
    {
        if (hiddenForTeam(ev))
            continue;
        uint32 gatedEntry = ev->panelGatesBossEntry;
        if (!gatedEntry && DungeonEventRegistry::IsRoomAggroPreClear(*ev) && haveRoomAggroBoss)
            gatedEntry = roomAggroBossEntry;
        if (!gatedEntry)
            continue;

        // The gated anchor must actually be in this (wing-filtered) list, else
        // the event belongs to another wing — leave it for the standalone loop,
        // which applies the same wing guard. Capture the gated boss's kill state
        // while we're here (needed for room-aggro pre-clears, below).
        bool present = false;
        bool gatedBossKilled = false;
        for (DungeonBossInfo const& info : bosses)
            if (info.entry == gatedEntry)
            {
                present = true;
                gatedBossKilled = info.encounterIndex < 32 &&
                                  (completedMask & (1u << info.encounterIndex)) != 0u;
                break;
            }
        if (!present)
            continue;

        uint32 const lk = DungeonEventExecutor::ConditionalLatchKey(ev->id);
        // Pending is the common case while clearing — leave it unsuffixed so the
        // (already long) event name fits the panel sub-line without truncating;
        // only the terminal states get a tag.
        std::string note = ev->name;
        bool done = cleared.count(lk) != 0;
        bool wasSkipped = !done && skipped.count(lk) != 0;
        // A room-aggro PRE-CLEAR is intentionally never latched — it is repeatable
        // per boss (see DcEngageActions, "The event is never latched"), so its
        // ConditionalLatchKey never lands in `cleared` and the suffix above would
        // stay pending forever. Its completion is the room being cleared, which is
        // guaranteed once the gated boss is killed (the boss can't be pulled until
        // the room is down). Mirror the gated boss's terminal state so the folded
        // note still flips to (done)/(skipped) on the panel.
        if (DungeonEventRegistry::IsRoomAggroPreClear(*ev))
        {
            if (gatedBossKilled)
                done = true;
            else if (skipped.count(gatedEntry))
                wasSkipped = true;
        }
        if (done)
            note += " (done)";
        else if (wasSkipped)
            note += " (skipped)";

        std::string& slot = eventAnnotation[gatedEntry];
        slot = slot.empty() ? note : slot + " | " + note;
        foldedEventIds.insert(ev->id);
    }

    for (DungeonBossInfo const& info : bosses)
    {
        // Travel objectives (BossRosterRegistry) are not creatures and carry no
        // kill-bit — their completion is the cleared-anchor latch. Report them
        // as a labelled line so the panel shows the event waypoint and whether
        // the tank has reached it.
        if (info.kind == DungeonAnchorKind::Objective)
        {
            std::string const objStatus =
                cleared.count(info.entry) ? "dead"
                : skipped.count(info.entry) ? "skipped"
                                            : "alive";
            std::string const objName = "Objective: " + info.name;
            auto const annIt = eventAnnotation.find(info.entry);
            std::string const ann = annIt != eventAnnotation.end() ? annIt->second : "";

            // The panel sort key is BossOrderKey (the orderOverride when set,
            // else encounterIndex), so a reordered anchor lands in its clear-path
            // slot — e.g. Stratholme's Barthilas ahead of the ziggurats. Field 9
            // (wing) is empty for objectives; field 10 carries any folded event
            // note (e.g. the gong that gates this anchor).
            std::ostringstream objMsg;
            objMsg << "BOSS\t" << info.entry << "\t" << BossOrderKey(info) << "\t"
                   << objName << "\t" << objStatus << "\t"
                   << static_cast<int>(info.x) << "\t" << static_cast<int>(info.y) << "\t"
                   << static_cast<int>(info.z) << "\t\t" << ann;
            DcStatusPublisher::SendAddonMessage(botAI, objMsg.str());

            if (!silent)
            {
                std::ostringstream line;
                line << BossOrderKey(info) << ". " << objName << " @ ("
                     << static_cast<int>(info.x) << ", " << static_cast<int>(info.y) << ", "
                     << static_cast<int>(info.z) << ") [" << objStatus << "]";
                DcStatusPublisher::SendAddonMessage(botAI, "CHAT\t" + line.str());
            }
            continue;
        }

        bool const killed =
            info.encounterIndex < 32 && (completedMask & (1u << info.encounterIndex)) != 0u;
        bool const liveOnMap = DcTargeting::FindLiveCreatureOnMap(bot, info.entry) != nullptr;
        // Only scan a second time for a corpse when there's no live instance.
        bool const corpseOnMap =
            !liveOnMap && DcTargeting::IsCreaturePresentOnMap(bot, info.entry);

        // Remember every boss we've actually seen alive this run so a later
        // disappearance reads as "missing" rather than an unreached boss whose
        // grid simply hasn't loaded yet.
        if (liveOnMap)
            seen.insert(info.entry);

        // alive   — in zone and the group has not killed it
        // dead    — the group has killed it (kill mask, or a corpse on the floor
        //           for the brief window before the mask flips)
        // missing — was seen alive earlier, is now gone, and was not killed
        // skipped — user/AI chose to pass it by (only while it's still alive)
        std::string statusStr;
        if (killed || corpseOnMap)
            statusStr = "dead";
        else if (skipped.count(info.entry))
            statusStr = "skipped";
        else if (liveOnMap)
            statusStr = "alive";
        else if (seen.count(info.entry))
            statusStr = "missing";
        else
            statusStr = "alive";  // exists in the instance, grid not yet loaded

        // Wing label for split maps (empty for single-wing dungeons). On
        // connected maps like Maraudon this is the only place the wing surfaces
        // — the boss list still holds every boss, it's just annotated by region.
        std::string const wing = DungeonWingRegistry::WingName(bot->GetMapId(), info.entry);
        auto const annIt = eventAnnotation.find(info.entry);
        std::string const ann = annIt != eventAnnotation.end() ? annIt->second : "";

        // Wing (field 9) and the folded-event note (field 10) are appended as
        // trailing fields; older addons that read only the leading fields ignore
        // them, so the protocol stays compatible. The event note is the sub-line
        // the panel renders under a boss whose gating event(s) fold into its row.
        std::ostringstream addonMsg;
        addonMsg << "BOSS\t"
                 << info.entry << "\t"
                 << BossOrderKey(info) << "\t"
                 << info.name << "\t"
                 << statusStr << "\t"
                 << static_cast<int>(info.x) << "\t"
                 << static_cast<int>(info.y) << "\t"
                 << static_cast<int>(info.z) << "\t"
                 << wing << "\t" << ann;

        DcStatusPublisher::SendAddonMessage(botAI, addonMsg.str());

        if (!silent)
        {
            std::ostringstream line;
            line << BossOrderKey(info) << ". " << info.name;
            if (!wing.empty())
                line << " [" << wing << "]";
            line << " @ (" << static_cast<int>(info.x) << ", "
                 << static_cast<int>(info.y) << ", "
                 << static_cast<int>(info.z) << ") [" << statusStr << "]";

            DcStatusPublisher::SendAddonMessage(botAI, "CHAT\t" + line.str());
        }
    }

    // A room-aggro PRE-CLEAR event (IsRoomAggroPreClear) must be DONE before its
    // boss is pulled, so it has to sort just BEFORE that boss in the panel — not
    // at the end like an off-path gate. The addon sorts purely by the index field
    // (and renders the ordinal position, never the raw index), so we give such an
    // event a fractional index of (lowest room-aggro boss index in this list)
    // - 0.5: it lands immediately ahead of the boss it gates and the panel
    // renumbers cleanly. Computed here from the wing-filtered boss list so a split
    // map only considers the bosses actually shown.
    bool haveRoomAggroIndex = false;
    uint32 roomAggroBossIndex = 0;
    for (DungeonBossInfo const& info : bosses)
    {
        if (info.kind != DungeonAnchorKind::Boss)
            continue;
        if (!RoomAggroRegistry::Find(bot->GetMapId(), info.entry))
            continue;
        if (!haveRoomAggroIndex || BossOrderKey(info) < roomAggroBossIndex)
        {
            roomAggroBossIndex = BossOrderKey(info);
            haveRoomAggroIndex = true;
        }
    }

    // Off-path CONDITIONAL events (DungeonEventRegistry) are not in the boss list
    // — surface them too so the player can see a pending gate (e.g. "free the
    // prisoner") and `dc skip` past it if a bot can't drive it. Listed as a BOSS
    // line with a synthetic entry (the latch key); status is the latch state.
    // Room-aggro pre-clears sort just before their boss (see above); every other
    // off-path event uses index 99 so it sorts last.
    for (DungeonEvent const* ev : DungeonEventRegistry::Conditional(bot->GetMapId(), panelDifficulty))
    {
        // Faction-specific events are hidden from the other team's panel.
        if (hiddenForTeam(ev))
            continue;

        // Events that gate a boss were folded into that boss's row in the pre-pass
        // above — don't also surface them as their own (Go-less) line.
        if (foldedEventIds.count(ev->id))
            continue;

        // A room-aggro pre-clear gates one specific boss and is meaningful only
        // in that boss's wing. On a split map (Scarlet Monastery) the boss list
        // above is wing-filtered, so haveRoomAggroIndex is true only when the
        // current wing actually holds a room-aggro boss. If it doesn't, this
        // event belongs to another wing — hide it, or "Clear the Cathedral"
        // would surface in the Armory, Library and Graveyard too.
        if (DungeonEventRegistry::IsRoomAggroPreClear(*ev) && !haveRoomAggroIndex)
            continue;

        // Wing scoping for split maps (Dire Maul): an event anchored to a specific
        // boss (panelGatesBossEntry / panelSortAfterBossEntry) belongs to that
        // boss's wing. The boss list above is wing-filtered, so if the anchor boss
        // isn't in it, this event is in another wing — hide it. This is the guard
        // the fold pre-pass promises when it declines an out-of-wing gated event;
        // without it the Gordok doors (anchored to North bosses) surfaced in the
        // Dire Maul East/West panels too.
        if (uint32 const wingBoss = ev->panelGatesBossEntry
                                        ? ev->panelGatesBossEntry
                                        : ev->panelSortAfterBossEntry)
        {
            bool inWing = false;
            for (DungeonBossInfo const& info : bosses)
                if (info.entry == wingBoss)
                {
                    inWing = true;
                    break;
                }
            if (!inWing)
                continue;
        }

        uint32 const latchKey = DungeonEventExecutor::ConditionalLatchKey(ev->id);
        std::string const evStatus =
            cleared.count(latchKey) ? "dead"
            : skipped.count(latchKey) ? "skipped"
                                      : "alive";
        std::string const evName = "Event: " + ev->name;

        // Default off-path position; room-aggro pre-clears slot in ahead of their
        // gated boss with a "<bossIndex>-0.5" fractional index (guarding the
        // unsigned underflow when the boss itself sits at index 0).
        std::string idxField = "99";
        if (DungeonEventRegistry::IsRoomAggroPreClear(*ev) && haveRoomAggroIndex)
            idxField = roomAggroBossIndex == 0
                           ? "-0.5"
                           : std::to_string(roomAggroBossIndex - 1) + ".5";
        else if (ev->panelGatesBossEntry)
        {
            // A prerequisite event (e.g. the gong before Tuten'kash) sorts just
            // ahead of the boss it gates, using the same fractional-index trick
            // as the room-aggro pre-clear so the panel renumbers cleanly.
            for (DungeonBossInfo const& info : bosses)
                if (info.entry == ev->panelGatesBossEntry)
                {
                    uint32 const k = BossOrderKey(info);
                    idxField = k == 0 ? "-0.5" : std::to_string(k - 1) + ".5";
                    break;
                }
        }
        else if (ev->panelSortAfterBossEntry)
        {
            // A follow-on gate (e.g. SFK's courtyard door after Rethilgore) sorts
            // just AFTER the named boss: "<bossKey>.5" lands between that boss and
            // the next one, so the panel renders the event as the very next row.
            for (DungeonBossInfo const& info : bosses)
                if (info.entry == ev->panelSortAfterBossEntry)
                {
                    idxField = std::to_string(BossOrderKey(info)) + ".5";
                    break;
                }
        }

        std::ostringstream evMsg;
        evMsg << "BOSS\t" << latchKey << "\t" << idxField << "\t" << evName << "\t"
              << evStatus << "\t0\t0\t0\t";
        DcStatusPublisher::SendAddonMessage(botAI, evMsg.str());

        if (!silent)
            DcStatusPublisher::SendAddonMessage(botAI, "CHAT\t" + evName + " [" + evStatus + "]");
    }

    DcStatusPublisher::SendAddonMessage(botAI, "BOSS_END");
    return true;
}

bool DcGoAction::Execute(Event& event)
{
    if (!IsAuthorized(bot, event))
    {
        DcRefuse(botAI, bot, "Not authorized to run go command");
        return false;
    }
    // Leader owns the run; non-leaders reached via the chat-keyword fan-out
    // stay quiet (the command/addon path already targets only the leader).
    if (!DcLeaderSignal::IsDungeonClearLeader(bot))
        return true;
    if (!DcRun::Of(context).enabled)
    {
        DcRefuse(botAI, bot, "Dungeon clear is not enabled. Please enable it first.");
        return false;
    }

    std::string const param = event.getParam();
    if (param.empty())
    {
        DcRefuse(botAI, bot, "Usage: .dc go <boss name or entry>");
        return false;
    }

    auto const& bosses = AI_VALUE(std::vector<DungeonBossInfo>, DcKey::DungeonBosses);
    DungeonBossInfo const* matched = nullptr;

    bool isNumeric = !param.empty() && std::all_of(param.begin(), param.end(), ::isdigit);
    if (isNumeric)
    {
        uint32 entry = std::stoul(param);
        for (auto const& info : bosses)
        {
            if (info.entry == entry)
            {
                matched = &info;
                break;
            }
        }
    }

    if (!matched)
    {
        std::string query = param;
        std::transform(query.begin(), query.end(), query.begin(), ::tolower);
        for (auto const& info : bosses)
        {
            std::string name = info.name;
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name.find(query) != std::string::npos)
            {
                matched = &info;
                break;
            }
        }
    }

    // Still no match? It may be a conditional EVENT, which isn't in the boss
    // vector. The panel now folds boss-gating events into their boss row (so a
    // "Go" sends the boss entry, not the event), but a chat `.dc go <eventname>`
    // or an older addon can still arrive here with an event. Resolve it: if the
    // event gates a boss, redirect the drive there (passing it fires the event);
    // otherwise it's an off-path gate that triggers automatically — say so
    // plainly instead of the misleading "boss doesn't exist".
    if (!matched)
    {
        std::string query = param;
        std::transform(query.begin(), query.end(), query.begin(), ::tolower);
        Difficulty const goDifficulty =
            bot->FindMap() ? bot->FindMap()->GetDifficulty() : DUNGEON_DIFFICULTY_NORMAL;
        for (DungeonEvent const* ev : DungeonEventRegistry::Conditional(bot->GetMapId(), goDifficulty))
        {
            bool keyMatch = isNumeric &&
                std::stoul(param) == DungeonEventExecutor::ConditionalLatchKey(ev->id);
            std::string evName = ev->name;
            std::transform(evName.begin(), evName.end(), evName.begin(), ::tolower);
            bool nameMatch = !isNumeric && evName.find(query) != std::string::npos;
            if (!keyMatch && !nameMatch)
                continue;

            if (ev->panelGatesBossEntry)
                for (auto const& info : bosses)
                    if (info.entry == ev->panelGatesBossEntry)
                    {
                        matched = &info;
                        break;
                    }

            if (!matched)
            {
                DcRefuse(botAI, bot, ev->name +
                                 " happens on the way — there's no separate spot to send the tank to.");
                return false;
            }
            break;
        }
    }

    if (!matched)
    {
        DcRefuse(botAI, bot, "Could not find boss matching: " + param);
        return false;
    }

    if (matched->kind == DungeonAnchorKind::Objective &&
        AI_VALUE(std::unordered_set<uint32>&, DcKey::ClearedAnchors).count(matched->entry))
    {
        DcRefuse(botAI, bot, "Objective " + matched->name + " is already done.");
        return false;
    }

    InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
    uint32 const completedMask = inst ? inst->GetCompletedEncounterMask() : 0u;
    if (matched->kind == DungeonAnchorKind::Boss &&
        matched->encounterIndex < 32 && (completedMask & (1u << matched->encounterIndex)))
    {
        DcRefuse(botAI, bot, "Boss " + matched->name + " is already dead (encounter complete).");
        return false;
    }

    Map* map = bot->FindMap();
    bool alive = true;
    bool present = false;
    if (map)
    {
        for (auto const& kv : map->GetCreatureBySpawnIdStore())
        {
            Creature* c = kv.second;
            if (c && c->GetEntry() == matched->entry)
            {
                present = true;
                if (!c->IsAlive())
                    alive = false;
            }
        }
    }
    if (present && !alive)
    {
        DcRefuse(botAI, bot, "Boss " + matched->name + " is dead.");
        return false;
    }

    std::unordered_set<uint32>& skipped =
        context->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get();
    skipped.erase(matched->entry);

    DcRun::Of(context).selectedBossEntry = matched->entry;

    // Explicitly targeting a boss is a "go now" intent — clear any pause so the
    // tank actually starts moving toward it instead of holding.
    DcRun::Of(context).paused = false;
    // Abort any in-flight pull on the old target before routing to the new one.
    ResetPullTransient(context);

    // Route to the new target with a clean approach FSM: counters, latches,
    // loot-yield anchor, position sentinel + committed boss, and the long-path
    // cache (forcing a rebuild for the new target) all reset in lockstep.
    context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get().Reset();
    context->GetValue<uint32>(DcKey::CurrentHop)->Set(0u);
    context->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Reset();
    context->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get() = DungeonFollowerState{};

    context->GetValue<std::string&>(DcKey::StallReason)->Get().clear();
    context->GetValue<std::string&>(DcKey::LastSaidReason)->Get().clear();
    context->GetValue<ObjectGuid>(DcKey::FallbackTarget)->Set(ObjectGuid::Empty);
    context->GetValue<ObjectGuid>(DcKey::EngageTrashTarget)->Set(ObjectGuid::Empty);

    context->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Reset();

    // Kill any parked escort glide before building the route to the new target
    // (see HaltAllMovement) so the tank doesn't coast down the old path first.
    HaltAllMovement(bot);

    DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tTargeting boss: " + matched->name + ". Navigating...");

    botAI->DoSpecificAction("dc status", event, true);
    return true;
}

bool DcPauseAction::Execute(Event& event)
{
    if (!IsAuthorized(bot, event))
    {
        DcRefuse(botAI, bot, "Not authorized to pause dungeon clear");
        return false;
    }

    // Followers have nothing to toggle: their follow-tank trigger reacts to the
    // leader's paused flag via DungeonClearPartyTankValue. Stay quiet.
    if (!DcLeaderSignal::IsDungeonClearLeader(bot))
        return true;

    if (!DcRun::Of(context).enabled)
    {
        DcRefuse(botAI, bot, "Dungeon clear is not enabled.");
        return false;
    }

    bool const paused = DcRun::Of(context).paused;

    // Explicit intent ("dc pause pause" / "dc pause resume", sent by the addon
    // button): a bare `dc pause` is a stateless TOGGLE, which races the
    // auto-pause sites — a click aimed at "Pause" that lands just after the run
    // auto-paused (door, wait-at-boss) reads the flipped flag and RESUMES, and
    // vice versa. With an intent that already holds, do nothing but re-push
    // status so the addon's button label resyncs. Chat stays a plain toggle.
    std::string const intent = event.getParam();
    if ((intent == "pause" && paused) || (intent == "resume" && !paused))
    {
        botAI->DoSpecificAction("dc status", event, true);
        return true;
    }

    if (!paused)
    {
        // Pause: hold in place. The driving ladder, the multiplier, and the
        // follow-tank party-tank lookup all gate on this flag, so the tank
        // stops navigating and followers peel off — exactly like dc off — but
        // all boss progress is preserved for resume.
        DcRun::Of(context).paused = true;
        // Record why so the status panel can distinguish a manual pause from the
        // tank auto-pausing on a door it can't open. Read by BuildStatusPayload.
        DcRun::Of(context).pauseReason =
            "paused at your request";
        // A manual hold is never tied to a door — clear any stale auto-paused
        // door so opening some unrelated door can't auto-resume this pause.
        DcRun::Of(context).pausedDoor = ObjectGuid::Empty;
        // Abort any in-flight pull so the party is released as we hold (the reaper
        // un-passives once the phase is no longer holding). Pull mode itself is
        // kept so resume re-enables the feature.
        ResetPullTransient(context);

        // Stop the in-flight advance glide so it doesn't coast to its endpoint.
        // Only out of combat: mid-fight the combat engine owns movement and we
        // want the current fight to finish before holding.
        if (bot && !bot->IsInCombat())
            HaltAllMovement(bot);

        DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tDungeon clear paused.");
        botAI->DoSpecificAction("dc status", event, true);
        return true;
    }

    // Resume on the same boss. Refuse if anyone is dead (mirrors dc on) so we
    // don't unpause straight into a wipe — unless the post-combat rez flow can
    // recover them, in which case resuming hands the corpse to the recovery.
    if (AnyPartyMemberDead(bot) && !DcRezRecovery::CanRecover(bot))
    {
        DcRefuse(botAI, bot, FirstDeadName(bot) + " is dead — rez and try again.");
        return false;
    }

    // Rebuild the transient navigation cache and clear the pause flags. Boss
    // progress is preserved (see ResetForResume).
    ResetForResume(context);

    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    std::string const target = next.has_value() ? next->name : "the next boss";
    DcStatusPublisher::SendAddonMessage(botAI, "CHAT\tResumed. Heading to " + target + ".");
    botAI->DoSpecificAction("dc status", event, true);
    return true;
}

bool DcResumeOnDoorOpenedAction::Execute(Event& event)
{
    // Leader-only: a follower has no paused flag of its own to clear — it mirrors
    // the leader via the multiplier / party-tank value.
    if (!DcLeaderSignal::IsDungeonClearLeader(bot))
        return false;
    if (!DcRun::Of(context).enabled || !DcRun::Of(context).paused)
        return false;

    // Don't unpause straight into a wipe (mirrors `dc on` / the manual resume).
    // The trigger keeps firing while the door stays open, so this retries on its
    // own once everyone is back up — or resumes at once when the post-combat rez
    // flow can recover the corpse (the resumed run immediately holds for it).
    if (AnyPartyMemberDead(bot) && !DcRezRecovery::CanRecover(bot))
        return false;

    ResetForResume(context);

    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    std::string const target = next.has_value() ? next->name : "the next boss";
    DcStatusPublisher::SendAddonMessage(
        botAI, "CHAT\tDoor opened \xe2\x80\x94 resuming. Heading to " + target + ".");
    botAI->DoSpecificAction("dc status", event, true);
    return true;
}

bool DcPullAction::Execute(Event& event)
{
    if (!IsAuthorized(bot, event))
    {
        DcRefuse(botAI, bot, "Not authorized to toggle advanced pull");
        return false;
    }

    // Followers have nothing to toggle: pull behavior is driven entirely off the
    // leader's flag (DcLeaderSignal::GetLeaderPullInfo). Stay quiet.
    if (!DcLeaderSignal::IsDungeonClearLeader(bot))
        return true;

    // Pull mode is settable BEFORE the run starts: when DC is off we just store
    // the preference (no live arming — there's no run to arm) and `dc on` applies
    // it the moment the run begins. When DC is on we apply it live.
    bool const enabled = DcRun::Of(context).enabled;

    // Tri-state preference: 0 Off, 1 On, 2 Dynamic (see DungeonClearPullSetting-
    // Value). The addon sends an explicit "off"/"on"/"dynamic"; a bare toggle
    // cycles Off -> On -> Dynamic -> Off for the chat keyword / .dc pull case.
    std::string const param = event.getParam();
    uint32 const current = AI_VALUE(uint32, DcKey::PullSetting);
    uint32 setting;
    if (param == "off")
        setting = 0u;
    else if (param == "on")
        setting = 1u;
    else if (param == "dynamic" || param == "dyn")
        setting = 2u;
    else
        setting = (current + 1u) % 3u;  // bare toggle cycles the three states

    char const* const label =
        setting == 1u ? "ON" : (setting == 2u ? "DYNAMIC" : "OFF");

    if (enabled)
    {
        // ApplyPullSetting keeps the behavioral bool in lock-step (On arms the
        // pull-to-camp maneuver and seeds a camp; Off/Dynamic disarm and tear
        // down any in-flight maneuver), and arms/revokes the tank's pull-session
        // daze immunity so the drag-back can't be Daze-slowed.
        ApplyPullSetting(bot, context, setting);
        DC_PULL_INFO("[DC:{}] advanced-pull mode {} by {}", bot->GetName(),
                     label, param.empty() ? "toggle" : param);
    }
    else
    {
        // Run not started — remember the preference only; `dc on` applies it.
        context->GetValue<uint32>(DcKey::PullSetting)->Set(setting);
        DC_PULL_INFO("[DC:{}] advanced-pull mode pre-set to {} by {} (applies on dc on)",
                     bot->GetName(), label, param.empty() ? "toggle" : param);
    }

    std::string chat = "CHAT\tAdvanced pull ";
    char const* const when = enabled ? "" : " (applies when dungeon clear starts)";
    if (setting == 1u)
        chat += std::string("enabled") + when + ".";
    else if (setting == 2u)
        chat += std::string("set to dynamic (auto-deciding Leeroy vs pull per pack)") + when + ".";
    else
        chat += std::string("disabled") + when + ".";
    DcStatusPublisher::SendAddonMessage(botAI, chat);
    botAI->DoSpecificAction("dc status", event, true);
    return true;
}

