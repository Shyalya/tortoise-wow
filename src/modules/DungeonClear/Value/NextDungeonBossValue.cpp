#include "playerbot/playerbot.h"
#include "Value/NextDungeonBossValue.h"
#include "Value/DungeonBossesValue.h"
#include "DcRunState.h"
#include "Maps/GridSearchers.h"
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
    if (runState.selectedBossEntry)
    {
        for (DungeonBossInfo const& boss : bosses)
        {
            if (boss.entry == runState.selectedBossEntry && !skipped.count(boss.entry) && !cleared.count(boss.entry))
                return boss;
        }
    }

    for (DungeonBossInfo const& boss : bosses)
    {
        uint32 skipKey = boss.entry ? boss.entry : (0x80000000u | boss.encounterIndex);
        if (skipped.count(skipKey) || cleared.count(boss.entry))
            continue;

        if (boss.kind == DungeonAnchorKind::Objective)
            return boss;

        // Boss anchor: if we're already near its recorded spot, make sure it
        // isn't confirmed dead before committing to it - otherwise keep
        // scanning for the next one.
        float distToBoss = bot->GetDistance(boss.x, boss.y, boss.z);
        if (distToBoss <= boss.arriveRadius * 4.0f)
        {
            std::list<Creature*> found;
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
