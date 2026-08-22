/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARACTIONCONTEXT_H
#define _PLAYERBOT_DUNGEONCLEARACTIONCONTEXT_H

#include "Action.h"
#include "NamedObjectContext.h"
#include "Ai/Dungeon/DungeonClear/Action/BetterLootRollAction.h"
#include "Ai/Dungeon/DungeonClear/Action/DungeonClearActions.h"
#include "Ai/Dungeon/DungeonClear/Action/DungeonClearChatActions.h"
#include "Ai/Dungeon/DungeonClear/Action/StayDeadAction.h"

class DungeonClearActionContext : public NamedObjectContext<Action>
{
public:
    DungeonClearActionContext()
    {
        creators["dungeon clear advance"] = &DungeonClearActionContext::advance;
        creators["dungeon clear engage trash"] = &DungeonClearActionContext::engage_trash;
        creators["dungeon clear engage boss"] = &DungeonClearActionContext::engage_boss;
        creators["dungeon clear objective arrive"] = &DungeonClearActionContext::objective_arrive;
        creators["dungeon clear objective engage combat"] = &DungeonClearActionContext::objective_engage_combat;
        creators["dungeon clear run event"] = &DungeonClearActionContext::run_event;
        creators["dungeon clear run event combat"] = &DungeonClearActionContext::run_event_combat;
        creators["dungeon clear room clear"] = &DungeonClearActionContext::room_clear;
        creators["dungeon clear room preclear hold"] = &DungeonClearActionContext::room_preclear_hold;
        creators["dungeon clear clear stalled"] = &DungeonClearActionContext::clear_stalled;
        creators["dungeon clear follow tank"] = &DungeonClearActionContext::follow_tank;
        creators["dungeon clear disable on death"] = &DungeonClearActionContext::disable_on_death;
        creators["dungeon clear rez party"] = &DungeonClearActionContext::rez_party;
        creators["dungeon clear disable on cleared"] = &DungeonClearActionContext::disable_on_cleared;
        creators["dungeon clear break stuck combat"] = &DungeonClearActionContext::break_stuck_combat;
        creators["dungeon clear recover stranded"] = &DungeonClearActionContext::recover_stranded;
        creators["dungeon clear door blocked"] = &DungeonClearActionContext::door_blocked;
        creators["dungeon clear door reopened"] = &DungeonClearActionContext::door_reopened;
        creators["dungeon clear filter loot"] = &DungeonClearActionContext::filter_loot;
        creators["dungeon clear pull"] = &DungeonClearActionContext::pull;
        creators["dungeon clear pull maneuver"] = &DungeonClearActionContext::pull_maneuver;
        creators["dungeon clear hold at camp"] = &DungeonClearActionContext::hold_at_camp;
        creators["dungeon clear stay at camp"] = &DungeonClearActionContext::stay_at_camp;
        creators["dungeon clear assist camp"] = &DungeonClearActionContext::assist_camp;
        creators["dungeon clear assist camp combat"] = &DungeonClearActionContext::assist_camp_combat;
        creators["dungeon clear leader assist"] = &DungeonClearActionContext::leader_assist;
        creators["dungeon clear regroup combat"] = &DungeonClearActionContext::regroup_combat;
        creators["dungeon clear heal reposition"] = &DungeonClearActionContext::heal_reposition;
        creators["dungeon clear hazard vacate"] = &DungeonClearActionContext::hazard_vacate;
        creators["dungeon clear hakkar suppressor"] = &DungeonClearActionContext::hakkar_suppressor;
        creators["dungeon clear hakkar flame"] = &DungeonClearActionContext::hakkar_flame;
        creators["dungeon clear hakkar loot blood"] = &DungeonClearActionContext::hakkar_loot_blood;

        creators["dc on"] = &DungeonClearActionContext::dc_on;
        creators["dc off"] = &DungeonClearActionContext::dc_off;
        creators["dc skip"] = &DungeonClearActionContext::dc_skip;
        creators["dc pause"] = &DungeonClearActionContext::dc_pause;
        creators["dc pull"] = &DungeonClearActionContext::dc_pull;
        creators["dc status"] = &DungeonClearActionContext::dc_status;
        creators["dc bosses"] = &DungeonClearActionContext::dc_bosses;
        creators["dc go"] = &DungeonClearActionContext::dc_go;

        // Override mod-playerbots' "loot roll": a bot in "bot self" mode does
        // not auto-vote on group loot (it shares the human's GUID and would
        // pre-empt their roll), and bots Need/Greed gear above their level
        // that they will wear once they reach it instead of greed/passing.
        // DungeonClearLootRollPendingTrigger additionally drives this action
        // the tick a roll window opens, instead of stock's randomized poll.
        // Server-wide, gated by DungeonClear.BetterLootRolling; inert (defers
        // to stock LootRollAction) when off. Last-registration-wins.
        // KNOWN ISSUE (2026-08-22): the two stock-name overrides below are
        // DISABLED. Any bot carrying either object crashes the map pool within
        // ~2 minutes of a test-run start: an exception unwinding out of
        // Engine::DoNextAction ends in free()/SIGSEGV, with backtraces landing
        // in a COMDAT-folded .cold clone attributed to TravelValues.h statics.
        // Ruled out: shared-context ownership (per-bot instances), the
        // GetMap->FindMap define (removed; sources rewritten), key collisions
        // (none beyond these two), mapless-bot guards in both isUseful bodies
        // (kept - they are correct regardless). Still unexplained; needs an
        // ASan build to find the real thrower/corruptor. Consequences while
        // disabled: dead dungeon bots release to the graveyard like stock (a
        // wiped test run freezes and the 600s watchdog evicts it), and loot
        // rolls stay fully stock.
        // creators["loot roll"] = &DungeonClearActionContext::better_loot_roll;

        // Override mod-playerbots' "auto release" so dead bots stay dead instead
        // of releasing to the graveyard. Dungeon/raid maps only, gated by
        // DungeonClear.PreventBotRelease; inert (defers to stock) when that flag
        // is off or the bot is outside an instance. Last-registration-wins, so
        // this replaces the playerbots creator for every bot of this class.
        // creators["auto release"] = &DungeonClearActionContext::auto_release;
    }

private:
    static Action* advance(PlayerbotAI* ai) { return new DungeonClearAdvanceAction(ai); }
    static Action* engage_trash(PlayerbotAI* ai) { return new DungeonClearEngageTrashAction(ai); }
    static Action* engage_boss(PlayerbotAI* ai) { return new DungeonClearEngageBossAction(ai); }
    static Action* objective_arrive(PlayerbotAI* ai) { return new DcObjectiveArriveAction(ai); }
    static Action* objective_engage_combat(PlayerbotAI* ai) { return new DcObjectiveEngageCombatAction(ai); }
    static Action* run_event(PlayerbotAI* ai) { return new DcRunEventAction(ai); }
    static Action* run_event_combat(PlayerbotAI* ai) { return new DcRunEventCombatAction(ai); }
    static Action* room_clear(PlayerbotAI* ai) { return new DungeonClearRoomClearAction(ai); }
    static Action* room_preclear_hold(PlayerbotAI* ai) { return new DungeonClearRoomPreClearHoldAction(ai); }
    static Action* clear_stalled(PlayerbotAI* ai) { return new DungeonClearClearStalledAction(ai); }
    static Action* follow_tank(PlayerbotAI* ai) { return new DungeonClearFollowTankAction(ai); }
    static Action* disable_on_death(PlayerbotAI* ai) { return new DungeonClearDisableOnDeathAction(ai); }
    static Action* rez_party(PlayerbotAI* ai) { return new DungeonClearRezPartyAction(ai); }
    static Action* disable_on_cleared(PlayerbotAI* ai) { return new DungeonClearDisableOnClearedAction(ai); }
    static Action* break_stuck_combat(PlayerbotAI* ai) { return new DungeonClearBreakStuckCombatAction(ai); }
    static Action* recover_stranded(PlayerbotAI* ai) { return new DungeonClearRecoverStrandedAction(ai); }
    static Action* door_blocked(PlayerbotAI* ai) { return new DungeonClearDoorBlockedAction(ai); }
    static Action* door_reopened(PlayerbotAI* ai) { return new DcResumeOnDoorOpenedAction(ai); }
    static Action* filter_loot(PlayerbotAI* ai) { return new DungeonClearFilterLootAction(ai); }
    static Action* pull(PlayerbotAI* ai) { return new DungeonClearPullAction(ai); }
    static Action* pull_maneuver(PlayerbotAI* ai) { return new DungeonClearPullManeuverAction(ai); }
    static Action* hold_at_camp(PlayerbotAI* ai) { return new DungeonClearHoldAtCampAction(ai); }
    static Action* stay_at_camp(PlayerbotAI* ai) { return new DungeonClearStayAtCampAction(ai); }
    static Action* assist_camp(PlayerbotAI* ai) { return new DungeonClearAssistCampAction(ai); }
    static Action* assist_camp_combat(PlayerbotAI* ai) { return new DungeonClearAssistCampCombatAction(ai); }
    static Action* leader_assist(PlayerbotAI* ai) { return new DungeonClearLeaderAssistAction(ai); }
    static Action* regroup_combat(PlayerbotAI* ai) { return new DungeonClearRegroupCombatAction(ai); }
    static Action* heal_reposition(PlayerbotAI* ai) { return new DungeonClearHealRepositionAction(ai); }
    static Action* hazard_vacate(PlayerbotAI* ai) { return new DungeonClearHazardVacateAction(ai); }
    static Action* hakkar_suppressor(PlayerbotAI* ai) { return new DungeonClearHakkarSuppressorAction(ai); }
    static Action* hakkar_flame(PlayerbotAI* ai) { return new DungeonClearHakkarFlameAction(ai); }
    static Action* hakkar_loot_blood(PlayerbotAI* ai) { return new DungeonClearHakkarLootBloodAction(ai); }

    static Action* dc_on(PlayerbotAI* ai) { return new DcOnAction(ai); }
    static Action* dc_off(PlayerbotAI* ai) { return new DcOffAction(ai); }
    static Action* dc_skip(PlayerbotAI* ai) { return new DcSkipAction(ai); }
    static Action* dc_pause(PlayerbotAI* ai) { return new DcPauseAction(ai); }
    static Action* dc_pull(PlayerbotAI* ai) { return new DcPullAction(ai); }
    static Action* dc_status(PlayerbotAI* ai) { return new DcStatusAction(ai); }
    static Action* dc_bosses(PlayerbotAI* ai) { return new DcBossesAction(ai); }
    static Action* dc_go(PlayerbotAI* ai) { return new DcGoAction(ai); }

    static Action* better_loot_roll(PlayerbotAI* ai) { return new DungeonClearBetterLootRollAction(ai); }
    static Action* auto_release(PlayerbotAI* ai) { return new DungeonClearStayDeadAction(ai); }
};

#endif
