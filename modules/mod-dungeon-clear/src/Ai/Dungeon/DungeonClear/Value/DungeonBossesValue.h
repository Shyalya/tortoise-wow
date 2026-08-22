/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONBOSSESVALUE_H
#define _PLAYERBOT_DUNGEONBOSSESVALUE_H

#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;

class DungeonBossesValue : public CalculatedValue<std::vector<DungeonBossInfo>>
{
public:
    DungeonBossesValue(PlayerbotAI* botAI)
        : CalculatedValue<std::vector<DungeonBossInfo>>(botAI, DcKey::DungeonBosses, 5)
    {
    }

protected:
    std::vector<DungeonBossInfo> Calculate() override;
};

#endif
