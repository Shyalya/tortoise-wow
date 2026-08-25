#pragma once
#include "playerbot/strategy/actions/MovementActions.h"
#include "playerbot/strategy/actions/AttackAction.h"

namespace ai
{
    class DungeonClearAdvanceAction : public MovementAction
    {
    public:
        DungeonClearAdvanceAction(PlayerbotAI* ai) : MovementAction(ai, "dungeon clear advance") {}
        bool Execute(Event& event) override;
    };

    class DungeonClearEngageTrashAction : public AttackAction
    {
    public:
        DungeonClearEngageTrashAction(PlayerbotAI* ai) : AttackAction(ai, "dungeon clear engage trash") {}
        bool Execute(Event& event) override;
    };

    class DungeonClearEngageBossAction : public AttackAction
    {
    public:
        DungeonClearEngageBossAction(PlayerbotAI* ai) : AttackAction(ai, "dungeon clear engage boss") {}
        bool Execute(Event& event) override;
    };

    class DungeonClearObjectiveArriveAction : public Action
    {
    public:
        DungeonClearObjectiveArriveAction(PlayerbotAI* ai) : Action(ai, "dungeon clear objective arrive") {}
        bool Execute(Event& event) override;
    };

    class DungeonClearRunEventAction : public MovementAction
    {
    public:
        DungeonClearRunEventAction(PlayerbotAI* ai) : MovementAction(ai, "dungeon clear run event") {}
        bool Execute(Event& event) override;
    };

    class DungeonClearFollowTankAction : public MovementAction
    {
    public:
        DungeonClearFollowTankAction(PlayerbotAI* ai) : MovementAction(ai, "dungeon clear follow tank") {}
        bool Execute(Event& event) override;
    };

    class DungeonClearAssistTankAction : public AttackAction
    {
    public:
        DungeonClearAssistTankAction(PlayerbotAI* ai) : AttackAction(ai, "dungeon clear assist tank") {}
        bool Execute(Event& event) override;
    };

    class DungeonClearDisableOnDeathAction : public Action
    {
    public:
        DungeonClearDisableOnDeathAction(PlayerbotAI* ai) : Action(ai, "dungeon clear disable on death") {}
        bool Execute(Event& event) override;
    };

    class DungeonClearDisableOnClearedAction : public Action
    {
    public:
        DungeonClearDisableOnClearedAction(PlayerbotAI* ai) : Action(ai, "dungeon clear disable on cleared") {}
        bool Execute(Event& event) override;
    };

    class DungeonClearRezPartyAction : public MovementAction
    {
    public:
        DungeonClearRezPartyAction(PlayerbotAI* ai) : MovementAction(ai, "dungeon clear rez party") {}
        bool Execute(Event& event) override;
    };

    class DungeonClearFilterLootAction : public Action
    {
    public:
        DungeonClearFilterLootAction(PlayerbotAI* ai) : Action(ai, "dungeon clear filter loot") {}
        bool Execute(Event& event) override;
    };

    class DungeonClearPullAction : public AttackAction
    {
    public:
        DungeonClearPullAction(PlayerbotAI* ai) : AttackAction(ai, "dungeon clear pull") {}
        bool Execute(Event& event) override;
    };
}
