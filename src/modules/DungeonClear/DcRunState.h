#pragma once
#include <string>
#include <unordered_set>
#include "ObjectGuid.h"

struct DcRunState
{
    bool enabled = false;
    bool paused = false;
    std::string pauseReason;
    ObjectGuid pausedDoor;
    uint32 selectedBossEntry = 0;
    uint32 selectedBossStateKey = 0;
    // A boss is only latched as cleared after its creature has been observed
    // alive during this run.  This prevents an unloaded or summon-on-approach
    // encounter from being mistaken for a completed encounter.
    std::unordered_set<uint32> observedBosses;
    // Keep the concrete creature we engaged.  A dead creature can remain in
    // the map after combat even when the next value evaluation is no longer
    // at its anchor; this lets completion survive movement and cache expiry.
    ObjectGuid activeBossGuid;
    uint32 activeBossStateKey = 0;
    uint32 activeBossInstanceId = 0;
    uint32 bossStateMapId = 0;
    uint32 bossStateInstanceId = 0;
    uint32 rosterMapId = 0;
    uint32 rosterInstanceId = 0;
    uint8 rosterVariant = 0xff;
    uint32 eventId = 0;
    uint32 eventInstanceId = 0;
    uint32 eventLastDriveAt = 0;
    uint32 eventMaxStep = 0;
    uint32 eventProgressAt = 0;
    bool eventActionSent = false;
    uint32 eventActionMenuId = 0;
    // Pull mode is intentionally persistent across runs, but must still be
    // initialized from DungeonClear.PullMode the first time this AI uses DC.
    bool pullModeInitialized = false;
    bool hasPreventBotReleaseOverride = false;
    bool preventBotReleaseOverride = true;
    bool hasLootQualityOverride = false;
    uint32 lootQualityOverride = 0;
    bool hasIgnoreChestsOverride = false;
    bool ignoreChestsOverride = true;
    bool hasPartyMaxSpreadOverride = false;
    float partyMaxSpreadOverride = 25.0f;
    bool hasRestHealthOverride = false;
    float restHealthOverride = 0.0f;
    bool hasRestManaOverride = false;
    float restManaOverride = 0.0f;
    bool hasPullMaxOverride = false;
    uint32 pullMaxOverride = 1;

    void Reset()
    {
        enabled = false;
        paused = false;
        pauseReason.clear();
        pausedDoor.Clear();
        selectedBossEntry = 0;
        selectedBossStateKey = 0;
        observedBosses.clear();
        activeBossGuid.Clear();
        activeBossStateKey = 0;
        activeBossInstanceId = 0;
        bossStateMapId = 0;
        bossStateInstanceId = 0;
        // Settings overrides belong to one commander/run.  Do not carry them
        // into a later `.dc on` after the tank changes groups or commanders;
        // the addon will re-send the new run's values after startup.
        hasPreventBotReleaseOverride = false;
        hasLootQualityOverride = false;
        hasIgnoreChestsOverride = false;
        hasPartyMaxSpreadOverride = false;
        hasRestHealthOverride = false;
        hasRestManaOverride = false;
        hasPullMaxOverride = false;
        // Clear payloads too; only pull mode is deliberately persistent.
        preventBotReleaseOverride = true;
        lootQualityOverride = 0;
        ignoreChestsOverride = true;
        partyMaxSpreadOverride = 25.0f;
        restHealthOverride = 0.0f;
        restManaOverride = 0.0f;
        pullMaxOverride = 1;
        rosterMapId = 0;
        rosterInstanceId = 0;
        rosterVariant = 0xff;
        eventId = 0;
        eventInstanceId = 0;
        eventLastDriveAt = 0;
        eventMaxStep = 0;
        eventProgressAt = 0;
        eventActionSent = false;
        eventActionMenuId = 0;
    }

    void OnResume()
    {
        paused = false;
        pauseReason.clear();
        pausedDoor.Clear();
    }
};

enum class DcPullMode : uint8
{
    Dynamic = 0,
    Leeroy = 1,
    Advanced = 2
};
