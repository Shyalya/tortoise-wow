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

    void Reset()
    {
        enabled = false;
        paused = false;
        pauseReason.clear();
        pausedDoor.Clear();
        selectedBossEntry = 0;
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
