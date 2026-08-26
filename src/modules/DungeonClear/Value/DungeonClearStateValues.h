#pragma once
#include <unordered_set>
#include <string>
#include "playerbot/strategy/Value.h"
#include "DcRunState.h"
#include "DcValueKeys.h"

namespace ai
{
    // ManualSetValue<T&> so mutations through AI_VALUE(T&, ...) persist
    // (same pattern as LastMovementValue).

    class DungeonClearRunStateValue : public ManualSetValue<DcRunState&>
    {
    public:
        DungeonClearRunStateValue(PlayerbotAI* ai)
            : ManualSetValue<DcRunState&>(ai, data, DcKey::RunState) {}
    private:
        DcRunState data;
    };

    class DungeonClearSkippedValue : public ManualSetValue<std::unordered_set<uint32>&>
    {
    public:
        DungeonClearSkippedValue(PlayerbotAI* ai)
            : ManualSetValue<std::unordered_set<uint32>&>(ai, data, DcKey::Skipped) {}
    private:
        std::unordered_set<uint32> data;
    };

    class DungeonClearPullModeValue : public ManualSetValue<uint8&>
    {
    public:
        DungeonClearPullModeValue(PlayerbotAI* ai)
            : ManualSetValue<uint8&>(ai, data, DcKey::PullMode), data(0) {}
    private:
        uint8 data;
    };

    class DungeonClearStallReasonValue : public ManualSetValue<std::string&>
    {
    public:
        DungeonClearStallReasonValue(PlayerbotAI* ai)
            : ManualSetValue<std::string&>(ai, data, DcKey::StallReason) {}
    private:
        std::string data;
    };

    class DungeonClearClearedAnchorsValue : public ManualSetValue<std::unordered_set<uint32>&>
    {
    public:
        DungeonClearClearedAnchorsValue(PlayerbotAI* ai)
            : ManualSetValue<std::unordered_set<uint32>&>(ai, data, DcKey::ClearedAnchors) {}
    private:
        std::unordered_set<uint32> data;
    };

    class DungeonClearEventProgressValue : public ManualSetValue<uint32&>
    {
    public:
        DungeonClearEventProgressValue(PlayerbotAI* ai)
            : ManualSetValue<uint32&>(ai, data, DcKey::EventProgress), data(0) {}
    private:
        uint32 data;
    };

    class DungeonClearEventStartedAtValue : public ManualSetValue<uint32&>
    {
    public:
        DungeonClearEventStartedAtValue(PlayerbotAI* ai)
            : ManualSetValue<uint32&>(ai, data, DcKey::EventStartedAt), data(0) {}
    private:
        uint32 data;
    };

    class DungeonClearEventStepStartedAtValue : public ManualSetValue<uint32&>
    {
    public:
        DungeonClearEventStepStartedAtValue(PlayerbotAI* ai)
            : ManualSetValue<uint32&>(ai, data, DcKey::EventStepStartedAt), data(0) {}
    private:
        uint32 data;
    };
}
