// Returns the ordered list of bosses/objectives for the bot's current map:
// the hardcoded Classic instance table when one exists for the map (patched
// with any registered door/lever/escort objectives), merged with nearby
// elite/rare-elite hostiles that aren't already listed so unlisted or
// renamed unique mobs still get engaged instead of ignored.
#pragma once

#include "playerbot/strategy/Value.h"
#include "Data/DungeonBossInfo.h"
#include "DcValueKeys.h"
#include <vector>

class Player;

namespace ai
{
    class DungeonBossesValue : public CalculatedValue<std::vector<DungeonBossInfo>>
    {
    public:
        DungeonBossesValue(PlayerbotAI* ai) : CalculatedValue<std::vector<DungeonBossInfo>>(ai, DcKey::DungeonBosses, 10) {}

        std::string Format() override;

    protected:
        std::vector<DungeonBossInfo> Calculate() override;
    };

    // Hardcoded Classic instance boss tables, keyed by mapId. Coordinates and
    // a handful of entries are approximate - good enough for pathing and
    // engagement, not meant to be pixel-perfect spawn data.
    std::vector<DungeonBossInfo> GetHardcodedBossTable(uint32 mapId);

    // Applies every registered BossRosterPatch for mapId onto bosses: drops
    // any entries marked for removal, inserts the patch's extra objective
    // anchors, then re-sorts everything by BossOrderKey.
    void ApplyRosterPatches(std::vector<DungeonBossInfo>& bosses, uint32 mapId);

    // Returns the wing selected from the bot's current position for maps that
    // host multiple dungeons under one map ID.  0xff means unknown.
    uint8 DetermineDungeonRosterVariant(Player* bot);
}
