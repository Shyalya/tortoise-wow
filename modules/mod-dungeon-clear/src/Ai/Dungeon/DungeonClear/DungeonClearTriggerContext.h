/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARTRIGGERCONTEXT_H
#define _PLAYERBOT_DUNGEONCLEARTRIGGERCONTEXT_H

#include "ChatCommandTrigger.h"
#include "NamedObjectContext.h"
#include "Ai/Dungeon/DungeonClear/Trigger/DungeonClearTriggers.h"

class DungeonClearTriggerContext : public NamedObjectContext<Trigger>
{
public:
    DungeonClearTriggerContext()
    {
        // Engine triggers (state predicates).
        creators["dungeon clear idle"] = &DungeonClearTriggerContext::idle;
        creators["dungeon clear at boss"] = &DungeonClearTriggerContext::at_boss;
        creators["dungeon clear at objective"] = &DungeonClearTriggerContext::at_objective;
        creators["dungeon clear event due"] = &DungeonClearTriggerContext::event_due;
        creators["dungeon clear event due combat"] = &DungeonClearTriggerContext::event_due_combat;
        creators["dungeon clear blocking trash"] = &DungeonClearTriggerContext::blocking_trash;
        creators["dungeon clear room trash"] = &DungeonClearTriggerContext::room_trash;
        creators["dungeon clear room preclear hold"] = &DungeonClearTriggerContext::room_preclear_hold;
        creators["dungeon clear party died"] = &DungeonClearTriggerContext::party_died;
        creators["dungeon clear rez party"] = &DungeonClearTriggerContext::rez_party;
        creators["dungeon clear all cleared"] = &DungeonClearTriggerContext::all_cleared;
        creators["dungeon clear recover stranded"] = &DungeonClearTriggerContext::recover_stranded;
        creators["dungeon clear stalled"] = &DungeonClearTriggerContext::stalled;
        creators["dungeon clear follow tank"] = &DungeonClearTriggerContext::follow_tank;
        creators["dungeon clear door blocked"] = &DungeonClearTriggerContext::door_blocked;
        creators["dungeon clear door reopened"] = &DungeonClearTriggerContext::door_reopened;
        creators["dungeon clear needs drink"] = &DungeonClearTriggerContext::needs_drink;
        creators["dungeon clear needs eat"] = &DungeonClearTriggerContext::needs_eat;
        creators["dungeon clear filter loot"] = &DungeonClearTriggerContext::filter_loot;
        creators["dungeon clear pull"] = &DungeonClearTriggerContext::pull;
        creators["dungeon clear pull maneuver"] = &DungeonClearTriggerContext::pull_maneuver;
        creators["dungeon clear hold at camp"] = &DungeonClearTriggerContext::hold_at_camp;
        creators["dungeon clear stay at camp"] = &DungeonClearTriggerContext::stay_at_camp;
        creators["dungeon clear assist camp"] = &DungeonClearTriggerContext::assist_camp;
        creators["dungeon clear assist camp combat"] = &DungeonClearTriggerContext::assist_camp_combat;
        creators["dungeon clear leader assist"] = &DungeonClearTriggerContext::leader_assist;
        creators["dungeon clear objective engage combat"] = &DungeonClearTriggerContext::objective_engage_combat;
        creators["dungeon clear regroup combat"] = &DungeonClearTriggerContext::regroup_combat;
        creators["dungeon clear break stuck combat"] = &DungeonClearTriggerContext::break_stuck_combat;
        creators["dungeon clear heal reposition"] = &DungeonClearTriggerContext::heal_reposition;
        creators["dungeon clear hazard vacate"] = &DungeonClearTriggerContext::hazard_vacate;
        creators["dungeon clear hakkar suppressor"] = &DungeonClearTriggerContext::hakkar_suppressor;
        creators["dungeon clear hakkar flame"] = &DungeonClearTriggerContext::hakkar_flame;
        creators["dungeon clear hakkar loot blood"] = &DungeonClearTriggerContext::hakkar_loot_blood;
        creators["dungeon clear loot roll pending"] = &DungeonClearTriggerContext::loot_roll_pending;

        // Chat-command triggers (one per keyword/alias).
        creators["dc on"] = &DungeonClearTriggerContext::dc_on;
        creators["dungeon clear on"] = &DungeonClearTriggerContext::dungeon_clear_on;
        creators["dc off"] = &DungeonClearTriggerContext::dc_off;
        creators["dungeon clear off"] = &DungeonClearTriggerContext::dungeon_clear_off;
        creators["dc skip"] = &DungeonClearTriggerContext::dc_skip;
        creators["dc status"] = &DungeonClearTriggerContext::dc_status;
        creators["dc bosses"] = &DungeonClearTriggerContext::dc_bosses;
        creators["dc pause"] = &DungeonClearTriggerContext::dc_pause;
        creators["dungeon clear pause"] = &DungeonClearTriggerContext::dungeon_clear_pause;
        creators["dc pull"] = &DungeonClearTriggerContext::dc_pull;
        creators["dungeon clear pull keyword"] = &DungeonClearTriggerContext::dungeon_clear_pull_keyword;
    }

private:
    static Trigger* idle(PlayerbotAI* ai) { return new DungeonClearIdleTrigger(ai); }
    static Trigger* at_boss(PlayerbotAI* ai) { return new DungeonClearAtBossTrigger(ai); }
    static Trigger* at_objective(PlayerbotAI* ai) { return new DungeonClearAtObjectiveTrigger(ai); }
    static Trigger* event_due(PlayerbotAI* ai) { return new DungeonClearEventDueTrigger(ai); }
    static Trigger* event_due_combat(PlayerbotAI* ai) { return new DungeonClearEventDueCombatTrigger(ai); }
    static Trigger* blocking_trash(PlayerbotAI* ai) { return new DungeonClearBlockingTrashTrigger(ai); }
    static Trigger* room_trash(PlayerbotAI* ai) { return new DungeonClearRoomTrashTrigger(ai); }
    static Trigger* room_preclear_hold(PlayerbotAI* ai) { return new DungeonClearRoomPreClearHoldTrigger(ai); }
    static Trigger* party_died(PlayerbotAI* ai) { return new DungeonClearPartyDiedTrigger(ai); }
    static Trigger* rez_party(PlayerbotAI* ai) { return new DungeonClearRezPartyTrigger(ai); }
    static Trigger* all_cleared(PlayerbotAI* ai) { return new DungeonClearAllClearedTrigger(ai); }
    static Trigger* recover_stranded(PlayerbotAI* ai) { return new DungeonClearRecoverStrandedTrigger(ai); }
    static Trigger* stalled(PlayerbotAI* ai) { return new DungeonClearStalledTrigger(ai); }
    static Trigger* follow_tank(PlayerbotAI* ai) { return new DungeonClearFollowTankTrigger(ai); }
    static Trigger* door_blocked(PlayerbotAI* ai) { return new DungeonClearDoorBlockedTrigger(ai); }
    static Trigger* door_reopened(PlayerbotAI* ai) { return new DungeonClearDoorReopenedTrigger(ai); }
    static Trigger* needs_drink(PlayerbotAI* ai) { return new DungeonClearNeedsDrinkTrigger(ai); }
    static Trigger* needs_eat(PlayerbotAI* ai) { return new DungeonClearNeedsEatTrigger(ai); }
    static Trigger* filter_loot(PlayerbotAI* ai) { return new DungeonClearFilterLootTrigger(ai); }
    static Trigger* pull(PlayerbotAI* ai) { return new DungeonClearPullTrigger(ai); }
    static Trigger* pull_maneuver(PlayerbotAI* ai) { return new DungeonClearPullManeuverTrigger(ai); }
    static Trigger* hold_at_camp(PlayerbotAI* ai) { return new DungeonClearHoldAtCampTrigger(ai); }
    static Trigger* stay_at_camp(PlayerbotAI* ai) { return new DungeonClearHoldAtCampCombatTrigger(ai); }
    static Trigger* assist_camp(PlayerbotAI* ai) { return new DungeonClearAssistCampTrigger(ai); }
    static Trigger* assist_camp_combat(PlayerbotAI* ai) { return new DungeonClearAssistCampCombatTrigger(ai); }
    static Trigger* leader_assist(PlayerbotAI* ai) { return new DungeonClearLeaderAssistTrigger(ai); }
    static Trigger* objective_engage_combat(PlayerbotAI* ai) { return new DungeonClearObjectiveEngageCombatTrigger(ai); }
    static Trigger* regroup_combat(PlayerbotAI* ai) { return new DungeonClearRegroupCombatTrigger(ai); }
    static Trigger* break_stuck_combat(PlayerbotAI* ai) { return new DungeonClearBreakStuckCombatTrigger(ai); }
    static Trigger* heal_reposition(PlayerbotAI* ai) { return new DungeonClearHealRepositionTrigger(ai); }
    static Trigger* hazard_vacate(PlayerbotAI* ai) { return new DungeonClearHazardVacateTrigger(ai); }
    static Trigger* hakkar_suppressor(PlayerbotAI* ai) { return new DungeonClearHakkarSuppressorTrigger(ai); }
    static Trigger* hakkar_flame(PlayerbotAI* ai) { return new DungeonClearHakkarFlameTrigger(ai); }
    static Trigger* hakkar_loot_blood(PlayerbotAI* ai) { return new DungeonClearHakkarLootBloodTrigger(ai); }
    static Trigger* loot_roll_pending(PlayerbotAI* ai) { return new DungeonClearLootRollPendingTrigger(ai); }

    static Trigger* dc_on(PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc on"); }
    static Trigger* dungeon_clear_on(PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dungeon clear on"); }
    static Trigger* dc_off(PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc off"); }
    static Trigger* dungeon_clear_off(PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dungeon clear off"); }
    static Trigger* dc_skip(PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc skip"); }
    static Trigger* dc_status(PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc status"); }
    static Trigger* dc_bosses(PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc bosses"); }
    static Trigger* dc_pause(PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc pause"); }
    static Trigger* dungeon_clear_pause(PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dungeon clear pause"); }
    static Trigger* dc_pull(PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dc pull"); }
    static Trigger* dungeon_clear_pull_keyword(PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "dungeon clear pull"); }
};

#endif
