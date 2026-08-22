/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_NEXTDUNGEONBOSSVALUE_H
#define _PLAYERBOT_NEXTDUNGEONBOSSVALUE_H

#include <optional>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;

class NextDungeonBossValue : public CalculatedValue<std::optional<DungeonBossInfo>>
{
public:
    NextDungeonBossValue(PlayerbotAI* botAI)
        : CalculatedValue<std::optional<DungeonBossInfo>>(botAI, DcKey::NextDungeonBoss, 2)
    {
    }

protected:
    std::optional<DungeonBossInfo> Calculate() override;
};

#endif
