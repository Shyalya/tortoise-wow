#include "playerbot/playerbot.h"
#include "Value/NextDungeonBossValue.h"
#include "Value/DungeonBossesValue.h"
#include "DcRunState.h"
#include "Maps/GridSearchers.h"
#include "Maps/Map.h"
#include "Objects/Player.h"
#include "Objects/Creature.h"
#include <unordered_set>
#include <sstream>

using namespace ai;

std::optional<DungeonBossInfo> NextDungeonBossValue::Calculate()
{
    std::vector<DungeonBossInfo> bosses = AI_VALUE(std::vector<DungeonBossInfo>, DcKey::DungeonBosses);
    if (bosses.empty())
        return std::nullopt;

    std::unordered_set<uint32>& skipped = AI_VALUE(std::unordered_set<uint32>&, DcKey::Skipped);
    std::unordered_set<uint32>& cleared = AI_VALUE(std::unordered_set<uint32>&, DcKey::ClearedAnchors);
    DcRunState& runState = AI_VALUE(DcRunState&, DcKey::RunState);

    // A manually pinned boss (via "dc go") always wins if it's still present,
    // not skipped and not already cleared.
    if (runState.selectedBossStateKey || runState.selectedBossEntry)
    {
        for (DungeonBossInfo const& boss : bosses)
        {
            uint32 const stateKey = DungeonBossStateKey(boss);
            bool const selected = runState.selectedBossStateKey
                ? stateKey == runState.selectedBossStateKey
                : boss.entry == runState.selectedBossEntry;
            if (selected && !skipped.count(stateKey) && !cleared.count(stateKey))
                return boss;
        }
    }

    for (DungeonBossInfo const& boss : bosses)
    {
        uint32 const stateKey = DungeonBossStateKey(boss);
        uint32 const skipKey = stateKey;
        uint32 const anchorKey = stateKey;
        if (skipped.count(skipKey) || cleared.count(anchorKey))
            continue;

        if (boss.kind == DungeonAnchorKind::Objective)
            return boss;

        // A grid search only sees loaded cells.  Not finding a creature in an
        // unloaded boss grid means "walk there and load it", not "the boss is
        // dead".  Treating that case as cleared is especially harmful for
        // event-spawned bosses and was the source of silent skipped encounters.
        if (Map* map = bot->GetMap())
        {
            if (!map->IsLoaded(boss.x, boss.y))
                return boss;
        }

        // Boss anchor: if we're already near its recorded spot, make sure it
        // isn't confirmed dead before committing to it - otherwise keep
        // scanning for the next one.
        float distToBoss = bot->GetDistance(boss.x, boss.y, boss.z);
        if (distToBoss <= boss.arriveRadius * 4.0f)
        {
            std::list<Creature*> found;
            for (uint32 entry : boss.alternateEntries)
                GetCreatureListWithEntryInGrid(found, bot, entry, boss.arriveRadius * 4.0f);
            GetCreatureListWithEntryInGrid(found, bot, boss.entry, boss.arriveRadius * 4.0f);

            bool alive = false;
            for (Creature* creature : found)
            {
                if (creature && creature->IsAlive())
                {
                    alive = true;
                    break;
                }
            }

            if (!alive && boss.spawnOnApproach)
                return boss;
            if (!alive)
                continue;
        }

        return boss;
    }

    return std::nullopt;
}

std::string NextDungeonBossValue::Format()
{
    std::optional<DungeonBossInfo> boss = Get();
    if (!boss)
        return "<none>";

    std::ostringstream out;
    out << boss->name << "(" << boss->entry << "@" << boss->encounterIndex << ")";
    return out.str();
}
