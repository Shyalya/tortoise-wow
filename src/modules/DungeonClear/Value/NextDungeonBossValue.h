// Picks the next boss/objective the run should head for: the first entry in
// "dungeon bosses" order that isn't skipped or already cleared and, for
// bosses the bot is already close enough to check, isn't confirmed dead. A
// manually pinned boss (via "dc go") wins over natural order as long as it's
// still valid.
#pragma once

#include "playerbot/strategy/Value.h"
#include "Data/DungeonBossInfo.h"
#include "DcValueKeys.h"
#include <optional>

namespace ai
{
    class NextDungeonBossValue : public CalculatedValue<std::optional<DungeonBossInfo>>
    {
    public:
        NextDungeonBossValue(PlayerbotAI* ai) : CalculatedValue<std::optional<DungeonBossInfo>>(ai, DcKey::NextDungeonBoss, 3) {}

        std::string Format() override;

    protected:
        std::optional<DungeonBossInfo> Calculate() override;
    };
}
