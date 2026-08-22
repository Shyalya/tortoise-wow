/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARSTRATEGY_H
#define _PLAYERBOT_DUNGEONCLEARSTRATEGY_H

#include "Strategy.h"

class PlayerbotAI;

class DungeonClearStrategy : public Strategy
{
public:
    DungeonClearStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    // Tortoise port: this tree's Strategy dispatches per engine state to the
    // Init*Triggers virtuals and hands a std::list; mod-playerbots has one
    // vector-taking InitTriggers per strategy. The bodies only push_back, so
    // the container change is the whole difference. Non-combat strategy, so
    // the non-combat pair is the right hook.
    std::string getName() override { return "dungeon clear"; }
    int GetType() override { return STRATEGY_TYPE_NONCOMBAT; }
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
};

// Combat-engine companion to "dungeon clear". Holds the advanced-pull maneuver and
// the in-combat follower assist/hold triggers — the in-combat halves that can't live
// in the non-combat strategy because the bot runs its combat engine the instant it
// aggros. Resident on every bot's combat engine but inert unless that bot is the
// leader and mid-pull, or a follower assisting the tank. Its ONE multiplier
// (DungeonClearCombatMultiplier) touches only the stock "drop target" so the
// flip-early assist can hold the combat engine; nothing else in combat is altered.
class DungeonClearCombatStrategy : public Strategy
{
public:
    DungeonClearCombatStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string getName() override { return "dungeon clear combat"; }
    int GetType() override { return STRATEGY_TYPE_COMBAT; }
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
};

#endif
