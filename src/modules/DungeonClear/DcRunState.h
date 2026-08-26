#pragma once
#include <string>
#include "ObjectGuid.h"

struct DcRunState
{
    bool enabled = false;
    bool paused = false;
    std::string pauseReason;
    ObjectGuid pausedDoor;
    uint32 selectedBossEntry = 0;
    uint32 selectedBossStateKey = 0;
    uint32 rosterMapId = 0;
    uint32 rosterInstanceId = 0;
    uint8 rosterVariant = 0xff;
    uint32 eventId = 0;
    uint32 eventInstanceId = 0;
    uint32 eventLastDriveAt = 0;
    uint32 eventMaxStep = 0;
    uint32 eventProgressAt = 0;

    void Reset()
    {
        enabled = false;
        paused = false;
        pauseReason.clear();
        pausedDoor.Clear();
        selectedBossEntry = 0;
        selectedBossStateKey = 0;
        rosterMapId = 0;
        rosterInstanceId = 0;
        rosterVariant = 0xff;
        eventId = 0;
        eventInstanceId = 0;
        eventLastDriveAt = 0;
        eventMaxStep = 0;
        eventProgressAt = 0;
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
