#pragma once
#include "Platform/Define.h"

class DcSettings
{
public:
    static DcSettings& Instance();
    void Load();

    bool moduleEnabled = true;
    float engageRange = 25.0f;
    float trashEngageRange = 18.0f;
    float advanceArriveRange = 12.0f;
    uint8 defaultPullMode = 0;
    uint32 pullDynamicMaxLeeroyMobs = 3;
    uint32 lootQualityMin = 2;
    float restHealth = 60.0f;
    float restMana = 50.0f;
    bool postCombatRez = true;
    uint32 strategyGateSweepMs = 3000;
    bool preventBotRelease = true;
    float objectiveArriveRadius = 10.0f;
    float partyMaxSpread = 25.0f;
};

#define sDcSettings DcSettings::Instance()
