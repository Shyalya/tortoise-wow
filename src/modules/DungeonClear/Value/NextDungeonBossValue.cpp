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
#include <utility>

using namespace ai;

std::optional<DungeonBossInfo> NextDungeonBossValue::Calculate()
{
    std::vector<DungeonBossInfo> bosses = AI_VALUE(std::vector<DungeonBossInfo>, DcKey::DungeonBosses);
    if (bosses.empty())
        return std::nullopt;

    std::unordered_set<uint32>& skipped = AI_VALUE(std::unordered_set<uint32>&, DcKey::Skipped);
    std::unordered_set<uint32>& cleared = AI_VALUE(std::unordered_set<uint32>&, DcKey::ClearedAnchors);
    DcRunState& runState = AI_VALUE(DcRunState&, DcKey::RunState);

    // Cleared/observed encounter keys are local to one dungeon instance.  A
    // bot can retain the same AI context while entering another instance; in
    // that case carrying the old set would skip real bosses (or make a
    // never-spawned summon look previously observed).
    uint32 const mapId = bot->GetMapId();
    uint32 const instanceId = bot->GetInstanceId();
    if (runState.bossStateMapId != mapId || runState.bossStateInstanceId != instanceId)
    {
        runState.bossStateMapId = mapId;
        runState.bossStateInstanceId = instanceId;
        runState.observedBosses.clear();
        runState.activeBossGuid.Clear();
        runState.activeBossStateKey = 0;
        runState.activeBossInstanceId = 0;
        cleared.clear();
        skipped.clear();
        runState.selectedBossEntry = 0;
        runState.selectedBossStateKey = 0;
    }

    auto clearBoss = [&](DungeonBossInfo const& boss, uint32 stateKey)
    {
        cleared.insert(stateKey);
        if (runState.activeBossStateKey == stateKey)
        {
            runState.activeBossGuid.Clear();
            runState.activeBossStateKey = 0;
            runState.activeBossInstanceId = 0;
        }
        if (runState.selectedBossStateKey == stateKey
            || (!runState.selectedBossStateKey && runState.selectedBossEntry == boss.entry))
        {
            runState.selectedBossEntry = 0;
            runState.selectedBossStateKey = 0;
        }
    };

    auto findBossState = [&](DungeonBossInfo const& boss) -> std::pair<bool, bool>
    {
        std::list<Creature*> found;
        float const radius = boss.arriveRadius * 4.0f;
        for (uint32 entry : boss.alternateEntries)
            GetCreatureListWithEntryInGrid(found, bot, entry, radius);
        GetCreatureListWithEntryInGrid(found, bot, boss.entry, radius);

        bool deadCreature = false;
        for (Creature* creature : found)
        {
            if (!creature)
                continue;
            if (creature->IsAlive())
                return {true, false};

            // CORPSE/JUST_DIED/CORPSE_FALLING are an observed death.  A DEAD
            // creature may simply be a spawn disabled by the instance/linking
            // system (or a forced despawn after a reset), so it is not by
            // itself evidence that a summon-on-approach encounter was killed.
            DeathState const deathState = creature->GetDeathState();
            if (deathState == JUST_DIED || deathState == CORPSE
                || deathState == CORPSE_FALLING)
                deadCreature = true;
        }
        return {false, deadCreature};
    };

    auto considerBoss = [&](DungeonBossInfo const& boss) -> bool
    {
        uint32 const stateKey = DungeonBossStateKey(boss);
        if (skipped.count(stateKey) || cleared.count(stateKey))
            return false;

        if (boss.kind == DungeonAnchorKind::Objective)
            return true;

        Map* map = bot->GetMap();

        // Keep completion tied to the concrete creature we engaged.  Its
        // corpse/death state remains authoritative even after the party has
        // moved outside the anchor grid and the normal value is recalculated.
        if (map && runState.activeBossStateKey == stateKey
            && runState.activeBossInstanceId == map->GetInstanceId()
            && runState.activeBossGuid)
        {
            if (Creature* creature = map->GetCreature(runState.activeBossGuid))
            {
                DeathState const deathState = creature->GetDeathState();
                if (deathState == JUST_DIED || deathState == CORPSE
                    || deathState == CORPSE_FALLING)
                {
                    runState.observedBosses.insert(stateKey);
                    clearBoss(boss, stateKey);
                    return false;
                }
            }
        }

        // A grid search only sees loaded cells.  Not finding a creature in an
        // unloaded boss grid means "walk there and load it", never "dead".
        if (!map || !map->IsLoaded(boss.x, boss.y))
            return true;

        float const distance = bot->GetDistance(boss.x, boss.y, boss.z);
        if (distance <= boss.arriveRadius * 4.0f)
        {
            bool const observed = runState.observedBosses.count(stateKey) != 0;
            auto const presence = findBossState(boss);
            if (presence.first)
            {
                runState.observedBosses.insert(stateKey);
                // A live creature in the loaded anchor is enough to retain a
                // durable handle; the engage action refreshes it with the
                // exact target GUID when combat starts.
                std::list<Creature*> live;
                for (uint32 entry : boss.alternateEntries)
                    GetCreatureListWithEntryInGrid(live, bot, entry, boss.arriveRadius * 4.0f);
                GetCreatureListWithEntryInGrid(live, bot, boss.entry, boss.arriveRadius * 4.0f);
                for (Creature* creature : live)
                {
                    if (creature && creature->IsAlive())
                    {
                        runState.activeBossGuid = creature->GetObjectGuid();
                        runState.activeBossStateKey = stateKey;
                        runState.activeBossInstanceId = map->GetInstanceId();
                        break;
                    }
                }
                return true;
            }

            // Summon-on-approach encounters are allowed to be absent until
            // their trigger fires.  Once an encounter has actually spawned,
            // however, its disappearance is a durable completion signal.
            if (boss.spawnOnApproach && !observed && !presence.second)
                return true;

            // Static encounters are considered dead only in a loaded grid and
            // at the anchor.  This handles bosses killed before the bot's
            // first value evaluation without treating unloaded cells as dead.
            clearBoss(boss, stateKey);
            return false;
        }

        return true;
    };

    // A manually pinned boss (via "dc go") always wins if it's still present,
    // not skipped and not already cleared.
    if (runState.selectedBossStateKey || runState.selectedBossEntry)
    {
        bool selectedFound = false;
        for (DungeonBossInfo const& boss : bosses)
        {
            uint32 const stateKey = DungeonBossStateKey(boss);
            bool const selected = runState.selectedBossStateKey
                ? stateKey == runState.selectedBossStateKey
                : boss.entry == runState.selectedBossEntry;
            if (selected)
            {
                selectedFound = true;
                if (considerBoss(boss))
                    return boss;
                // The selected encounter was skipped or completed.  Drop the
                // pin so ordinary ordered routing can continue.
                runState.selectedBossEntry = 0;
                runState.selectedBossStateKey = 0;
                break;
            }
        }
        if (!selectedFound)
        {
            // A wing/roster refresh can remove the pinned entry.  Do not keep
            // a stale pin that could unexpectedly win a later recalculation.
            runState.selectedBossEntry = 0;
            runState.selectedBossStateKey = 0;
        }
    }

    for (DungeonBossInfo const& boss : bosses)
    {
        uint32 const stateKey = DungeonBossStateKey(boss);
        if (considerBoss(boss))
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
