// Static, data-driven description of the scripted interaction sequences
// ("events") a dungeon needs beyond straight boss-killing: pulling a lever,
// firing a cannon, talking to an NPC to open a door, waiting for a
// GameObject to change state, etc. DungeonClearRunEventAction walks these
// step lists; DungeonClearEventDueTrigger decides when it's time to run one.
#pragma once

#include "Data/DungeonBossInfo.h"
#include "Platform/Define.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class PlayerbotAI;

namespace ai
{
    class AiObjectContext;

    enum class DcEventStepType : uint8
    {
        MoveTo = 0,
        UseGO = 1,
        TalkNpc = 2,
        WaitGOState = 3,
        WaitMs = 4,
        Custom = 5
    };

    struct DcEventStep
    {
        DcEventStepType type = DcEventStepType::MoveTo;
        uint32 entry = 0;              // GameObject/NPC entry for UseGO/TalkNpc/WaitGOState.
        float x = 0.0f, y = 0.0f, z = 0.0f; // Target position for MoveTo.
        float radius = 3.0f;           // Arrival/interaction radius.
        uint32 goState = 0;            // Expected GO state for WaitGOState (informational).
        uint32 waitMs = 0;             // Duration for WaitMs (informational; executor just advances).
        std::string text;              // Gossip text / step label, cosmetic.

        // Optional hook for Custom steps. Not currently invoked by the
        // executor (DungeonClearRunEventAction has a fixed cannon fallback
        // for Custom), but kept for future scripted-step authors.
        std::function<bool(PlayerbotAI*, Player*)> customFn;
    };

    // A scripted sequence tied to a dungeon map: reachable either directly
    // via an explicit eventId stamped on a DungeonBossInfo, or as a fallback
    // via FindDueEvent() when the party is near the anchor's encounterIndex.
    struct DungeonEvent
    {
        uint32 eventId = 0;
        uint32 mapId = 0;
        std::string name;
        int32 encounterIndex = -1;     // -1 = only reachable via explicit eventId lookup.
        bool persistent = false;       // If true, can run again after being marked cleared.
        uint32 timeoutMs = 30000;      // Informational safety-net budget for the whole event.
        std::vector<DcEventStep> steps;
    };

    // Fluent helper for building a DungeonEvent in Register* functions.
    class EventBuilder
    {
    public:
        EventBuilder(uint32 eventId, uint32 mapId, std::string name);

        EventBuilder& Anchored(uint32 encounterIndex);
        EventBuilder& Persistent();
        EventBuilder& Timeout(uint32 ms);
        EventBuilder& MoveTo(float x, float y, float z, float radius = 3.0f);
        EventBuilder& UseGO(uint32 goEntry, float radius = 5.0f);
        EventBuilder& TalkTo(uint32 npcEntry, float radius = 5.0f);
        EventBuilder& WaitForGOState(uint32 goEntry, uint32 expectedState);
        EventBuilder& Wait(uint32 ms);
        EventBuilder& Custom(std::string label, std::function<bool(PlayerbotAI*, Player*)> fn = nullptr);

        DungeonEvent Build() const { return event; }

    private:
        DungeonEvent event;
    };

    // A dungeon-specific tweak to the hardcoded boss table: extra
    // objective/boss anchors to insert (e.g. "Iron Clad Door" in Deadmines)
    // and/or entries to drop because they're wrong or duplicated.
    struct BossRosterPatch
    {
        uint32 mapId = 0;
        std::vector<DungeonBossInfo> add;
        std::vector<uint32> removeEntries;
    };

    namespace DcRoster
    {
        // Builds a DungeonBossInfo describing a non-boss anchor (door, lever,
        // escort trigger, ...) that the engine should walk to/interact with
        // in encounter order alongside real bosses.
        DungeonBossInfo MakeObjective(uint32 entry, uint32 encounterIndex, uint32 mapId, std::string name,
            float x, float y, float z, float arriveRadius = 10.0f, uint32 gateEntry = 0, uint32 eventId = 0,
            int32 orderOverride = -1);
    }

    class DungeonEventRegistry
    {
    public:
        static DungeonEventRegistry& Instance();

        // Populates the registry from RegisterClassicStarterEvents() and
        // RegisterClassicExtendedEvents(). Called once from
        // DungeonClearModule::Initialize(); safe to call more than once.
        void Initialize();

        DungeonEvent const* FindEvent(uint32 mapId, uint32 eventId) const;
        DungeonEvent const* FindDueEvent(Player* bot, AiObjectContext* context, uint32 mapId, uint32 encounterIndex) const;

        std::vector<BossRosterPatch> GetRosterPatchesForMap(uint32 mapId) const;

    private:
        DungeonEventRegistry() = default;

        void RegisterEvent(DungeonEvent evt);
        void RegisterRosterPatch(BossRosterPatch patch);

        std::unordered_map<uint64, DungeonEvent> events; // key: (mapId << 32) | eventId
        std::vector<BossRosterPatch> rosterPatches;
        bool initialized = false;
    };

    // Per expansion-tier registration entry points, implemented in
    // Data/Events/ClassicStarterEvents.cpp and ClassicExtendedEvents.cpp.
    // Each appends 0+ DungeonEvents/BossRosterPatches to the out params;
    // instances without a scripted event yet just add nothing (the engine
    // falls back to pure kill-the-boss navigation for them).
    void RegisterClassicStarterEvents(std::vector<DungeonEvent>& out, std::vector<BossRosterPatch>& patches);
    void RegisterClassicExtendedEvents(std::vector<DungeonEvent>& out, std::vector<BossRosterPatch>& patches);
}

#define sDungeonEventRegistry ai::DungeonEventRegistry::Instance()
