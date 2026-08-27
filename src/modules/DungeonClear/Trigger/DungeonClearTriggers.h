#pragma once
#include "playerbot/strategy/Trigger.h"

namespace ai
{
    class DungeonClearEnabledTrigger : public Trigger
    {
    public:
        DungeonClearEnabledTrigger(PlayerbotAI* ai, std::string name) : Trigger(ai, name, 1) {}
    protected:
        bool LeaderEnabledUnpaused() const;
    };

    class DungeonClearPartyDiedTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearPartyDiedTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear party died") {}
        bool IsActive() override;
    };

    class DungeonClearRecoveryReadyTrigger : public Trigger
    {
    public:
        DungeonClearRecoveryReadyTrigger(PlayerbotAI* ai) : Trigger(ai, "dungeon clear recovery ready", 1) {}
        bool IsActive() override;
    };

    class DungeonClearRestPartyTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearRestPartyTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear rest party") {}
        bool IsActive() override;
    };

    class DungeonClearRegroupTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearRegroupTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear regroup") {}
        bool IsActive() override;
    };

    class DungeonClearAllClearedTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearAllClearedTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear all cleared") {}
        bool IsActive() override;
    };

    class DungeonClearAtBossTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearAtBossTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear at boss") {}
        bool IsActive() override;
    };

    class DungeonClearAtObjectiveTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearAtObjectiveTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear at objective") {}
        bool IsActive() override;
    };

    class DungeonClearBlockingTrashTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearBlockingTrashTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear blocking trash") {}
        bool IsActive() override;
    };

    class DungeonClearNeedAdvanceTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearNeedAdvanceTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear need advance") {}
        bool IsActive() override;
    };

    class DungeonClearFollowTankTrigger : public Trigger
    {
    public:
        DungeonClearFollowTankTrigger(PlayerbotAI* ai) : Trigger(ai, "dungeon clear follow tank", 1) {}
        bool IsActive() override;
    };

    class DungeonClearAssistTankTrigger : public Trigger
    {
    public:
        DungeonClearAssistTankTrigger(PlayerbotAI* ai) : Trigger(ai, "dungeon clear assist tank", 1) {}
        bool IsActive() override;
    };

    class DungeonClearPullTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearPullTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear pull") {}
        bool IsActive() override;
    };

    class DungeonClearEventDueTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearEventDueTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear event due") {}
        bool IsActive() override;
    };

    class DungeonClearRezPartyTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearRezPartyTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear rez party") {}
        bool IsActive() override;
    };

    class DungeonClearFilterLootTrigger : public DungeonClearEnabledTrigger
    {
    public:
        DungeonClearFilterLootTrigger(PlayerbotAI* ai) : DungeonClearEnabledTrigger(ai, "dungeon clear filter loot") {}
        bool IsActive() override;
    };
}
