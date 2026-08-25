#include "Data/DungeonEventRegistry.h"
#include "DcValueKeys.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "Log.h"
#include <algorithm>
#include <unordered_set>

namespace ai
{
    namespace
    {
        inline uint64 MakeEventKey(uint32 mapId, uint32 eventId)
        {
            return (static_cast<uint64>(mapId) << 32) | eventId;
        }
    }

    EventBuilder::EventBuilder(uint32 eventId, uint32 mapId, std::string name)
    {
        event.eventId = eventId;
        event.mapId = mapId;
        event.name = std::move(name);
    }

    EventBuilder& EventBuilder::Anchored(uint32 encounterIndex)
    {
        event.encounterIndex = static_cast<int32>(encounterIndex);
        return *this;
    }

    EventBuilder& EventBuilder::Persistent()
    {
        event.persistent = true;
        return *this;
    }

    EventBuilder& EventBuilder::Timeout(uint32 ms)
    {
        event.timeoutMs = ms;
        return *this;
    }

    EventBuilder& EventBuilder::MoveTo(float x, float y, float z, float radius)
    {
        DcEventStep step;
        step.type = DcEventStepType::MoveTo;
        step.x = x; step.y = y; step.z = z;
        step.radius = radius;
        event.steps.push_back(step);
        return *this;
    }

    EventBuilder& EventBuilder::UseGO(uint32 goEntry, float radius)
    {
        DcEventStep step;
        step.type = DcEventStepType::UseGO;
        step.entry = goEntry;
        step.radius = radius;
        event.steps.push_back(step);
        return *this;
    }

    EventBuilder& EventBuilder::TalkTo(uint32 npcEntry, float radius)
    {
        DcEventStep step;
        step.type = DcEventStepType::TalkNpc;
        step.entry = npcEntry;
        step.radius = radius;
        event.steps.push_back(step);
        return *this;
    }

    EventBuilder& EventBuilder::WaitForGOState(uint32 goEntry, uint32 expectedState)
    {
        DcEventStep step;
        step.type = DcEventStepType::WaitGOState;
        step.entry = goEntry;
        step.goState = expectedState;
        event.steps.push_back(step);
        return *this;
    }

    EventBuilder& EventBuilder::Wait(uint32 ms)
    {
        DcEventStep step;
        step.type = DcEventStepType::WaitMs;
        step.waitMs = ms;
        event.steps.push_back(step);
        return *this;
    }

    EventBuilder& EventBuilder::Custom(std::string label, std::function<bool(PlayerbotAI*, Player*)> fn)
    {
        DcEventStep step;
        step.type = DcEventStepType::Custom;
        step.text = std::move(label);
        step.customFn = std::move(fn);
        event.steps.push_back(step);
        return *this;
    }

    namespace DcRoster
    {
        DungeonBossInfo MakeObjective(uint32 entry, uint32 encounterIndex, uint32 mapId, std::string name,
            float x, float y, float z, float arriveRadius, uint32 gateEntry, uint32 eventId, int32 orderOverride)
        {
            DungeonBossInfo info;
            info.entry = entry;
            info.encounterIndex = encounterIndex;
            info.name = std::move(name);
            info.mapId = mapId;
            info.x = x; info.y = y; info.z = z;
            info.kind = DungeonAnchorKind::Objective;
            info.arriveRadius = arriveRadius;
            info.gateEntry = gateEntry;
            info.eventId = eventId;
            info.orderOverride = orderOverride;
            return info;
        }
    }

    DungeonEventRegistry& DungeonEventRegistry::Instance()
    {
        static DungeonEventRegistry instance;
        return instance;
    }

    void DungeonEventRegistry::RegisterEvent(DungeonEvent evt)
    {
        if (!evt.eventId)
        {
            sLog.outError("[DungeonClear] Refusing to register event '%s' with eventId 0.", evt.name.c_str());
            return;
        }
        events[MakeEventKey(evt.mapId, evt.eventId)] = std::move(evt);
    }

    void DungeonEventRegistry::RegisterRosterPatch(BossRosterPatch patch)
    {
        rosterPatches.push_back(std::move(patch));
    }

    void DungeonEventRegistry::Initialize()
    {
        if (initialized)
            return;
        initialized = true;

        std::vector<DungeonEvent> starterEvents, extendedEvents;
        std::vector<BossRosterPatch> starterPatches, extendedPatches;

        RegisterClassicStarterEvents(starterEvents, starterPatches);
        RegisterClassicExtendedEvents(extendedEvents, extendedPatches);

        for (DungeonEvent& evt : starterEvents)
            RegisterEvent(std::move(evt));
        for (DungeonEvent& evt : extendedEvents)
            RegisterEvent(std::move(evt));
        for (BossRosterPatch& patch : starterPatches)
            RegisterRosterPatch(std::move(patch));
        for (BossRosterPatch& patch : extendedPatches)
            RegisterRosterPatch(std::move(patch));

        sLog.outString("[DungeonClear] Registered %u scripted dungeon events, %u roster patches.",
            static_cast<uint32>(events.size()), static_cast<uint32>(rosterPatches.size()));
    }

    DungeonEvent const* DungeonEventRegistry::FindEvent(uint32 mapId, uint32 eventId) const
    {
        if (!eventId)
            return nullptr;
        auto it = events.find(MakeEventKey(mapId, eventId));
        return it == events.end() ? nullptr : &it->second;
    }

    DungeonEvent const* DungeonEventRegistry::FindDueEvent(Player* bot, AiObjectContext* context, uint32 mapId, uint32 encounterIndex) const
    {
        if (!bot || !context)
            return nullptr;

        for (auto const& entry : events)
        {
            DungeonEvent const& evt = entry.second;
            if (evt.mapId != mapId || evt.encounterIndex < 0 || static_cast<uint32>(evt.encounterIndex) != encounterIndex)
                continue;

            if (!evt.persistent)
            {
                std::unordered_set<uint32>& cleared = context->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get();
                if (cleared.count(evt.eventId))
                    continue;
            }

            return &evt;
        }
        return nullptr;
    }

    std::vector<BossRosterPatch> DungeonEventRegistry::GetRosterPatchesForMap(uint32 mapId) const
    {
        std::vector<BossRosterPatch> result;
        for (BossRosterPatch const& patch : rosterPatches)
        {
            if (patch.mapId == mapId)
                result.push_back(patch);
        }
        return result;
    }
}
