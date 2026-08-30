/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "BossRosterRegistry.h"

#include <algorithm>
#include <unordered_set>

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"

static_assert(BossRosterRegistry::ObjectiveEntry(0) == 0x4F000000u,
              "objective entry base must stay in the synthetic range");

namespace
{
    // ---------------------------------------------------------------------
    // The patch table. Each dungeon that corrects the auto-derived boss list
    // owns its BossRosterPatch as a Register<Dungeon>Roster appender defined in
    // its Data/Events/<Dungeon>Events.cpp — so a dungeon's whole clear
    // definition (event rows + conditions + roster patch) lives in one file. The
    // appenders build their patches with the DcRoster:: helpers
    // (DungeonRosterBuilders.h). This aggregator calls each EXPLICITLY (same
    // reason as the event table: a self-registering static initializer in a TU
    // the program never references is stripped from the module static lib). Add
    // a dungeon's patch by defining Register<Dungeon>Roster + adding one call
    // below (declaration in DungeonEventTables.h).
    // ---------------------------------------------------------------------
    std::vector<BossRosterPatch> const& PatchTable()
    {
        static std::vector<BossRosterPatch> const kPatches = []
        {
            std::vector<BossRosterPatch> t;
            // Order here is table-scan order only; lookups key on mapId, so it
            // never affects behaviour.
            RegisterScarletMonasteryRoster(t);
            RegisterScholomanceRoster(t);
            RegisterSunkenTempleRoster(t);
            RegisterRazorfenDownsRoster(t);
            RegisterZulFarrakRoster(t);
            RegisterBlackrockDepthsRoster(t);
            RegisterDeadminesRoster(t);
            RegisterWailingCavernsRoster(t);
            RegisterStratholmeRoster(t);
            RegisterDireMaulRoster(t);
            RegisterUldamanRoster(t);
            RegisterHellfireRampartsRoster(t);
            RegisterSlavePensRoster(t);
            RegisterUnderbogRoster(t);
            RegisterOldHillsbradRoster(t);
            RegisterMechanarRoster(t);
            RegisterShatteredHallsRoster(t);
            RegisterSteamvaultRoster(t);
            RegisterArcatrazRoster(t);
            RegisterSethekkHallsRoster(t);
            RegisterBlackMorassRoster(t);
            RegisterMaraudonRoster(t);
            RegisterBlackfathomDeepsRoster(t);
            RegisterUtgardeKeepRoster(t);
            RegisterNexusRoster(t);
            RegisterAzjolNerubRoster(t);
            return t;
        }();
        return kPatches;
    }

    BossRosterPatch const* FindPatch(uint32 mapId)
    {
        for (BossRosterPatch const& p : PatchTable())
            if (p.mapId == mapId)
                return &p;
        return nullptr;
    }

    // Apply one patch to `base`. Factored out so the difficulty-aware Apply can
    // run several matching patches (Any + HeroicOnly) over the same list.
    std::vector<DungeonBossInfo> ApplyOne(BossRosterPatch const& patch,
                                          std::vector<DungeonBossInfo> base)
    {
        // 1. Resolve inherited completion bits from the (still present) base
        //    list before anything is removed, then drop the removed entries.
        std::vector<DungeonBossInfo> adds = patch.add;
        for (DungeonBossInfo& a : adds)
        {
            if (a.inheritCompletionFrom)
            {
                for (DungeonBossInfo const& b : base)
                {
                    if (b.entry == a.inheritCompletionFrom)
                    {
                        a.encounterIndex = b.encounterIndex;
                        break;
                    }
                }
                a.inheritCompletionFrom = 0;
            }
        }

        std::unordered_set<uint32> const remove(patch.remove.begin(), patch.remove.end());
        std::vector<DungeonBossInfo> result;
        result.reserve(base.size() + adds.size());
        for (DungeonBossInfo const& b : base)
            if (!remove.count(b.entry))
                result.push_back(b);

        // 1b. Reorder auto-derived bosses in place: stamp the requested
        //     orderOverride onto each kept entry so it sorts by its clear-path
        //     key while its real DBC kill-bit (encounterIndex) stays as-is.
        //     Lets a dungeon whose DBC encounter order doesn't match the travel
        //     path be fixed without remove+re-add (cf. ZulFarrak: Velratha's
        //     DBC bit 0 would otherwise send the tank to the far sacred pool
        //     first).
        for (auto const& [entry, order] : patch.reorder)
            for (DungeonBossInfo& b : result)
                if (b.entry == entry)
                {
                    b.orderOverride = order;
                    break;
                }

        // 2. Append the added anchors and re-sort into clear order so
        //    objectives and replacement bosses slot in. Ordering keys on
        //    BossOrderKey (the explicit orderOverride when set, else
        //    encounterIndex) so a reordered real boss lands in its path slot
        //    without disturbing its kill-bit.
        for (DungeonBossInfo& a : adds)
            result.push_back(std::move(a));

        // Sort by order key. Tie-break: at an equal key an Objective sorts
        // BEFORE a Boss, so an objective sharing a boss's bit is reached first.
        // The candidate picker (NextDungeonBossValue::PickTarget) advances to
        // the lowest key STRICTLY greater than the one just finished, so an
        // objective tied with the boss it must precede (e.g. ZulFarrak's summit
        // event at bit 7, the same bit as Chief Ukorz) would otherwise be
        // skipped past; ordering it first makes the picker hand it over before
        // the boss.
        std::stable_sort(result.begin(), result.end(),
                         [](DungeonBossInfo const& l, DungeonBossInfo const& r)
                         {
                             uint32 const lk = BossOrderKey(l);
                             uint32 const rk = BossOrderKey(r);
                             if (lk != rk)
                                 return lk < rk;
                             return l.kind == DungeonAnchorKind::Objective &&
                                    r.kind == DungeonAnchorKind::Boss;
                         });

        return result;
    }
}

std::vector<BossRosterPatch> const& BossRosterRegistry::AllPatches()
{
    return PatchTable();
}

bool BossRosterRegistry::HasPatch(uint32 mapId)
{
    return FindPatch(mapId) != nullptr;
}

std::vector<DungeonBossInfo> BossRosterRegistry::Apply(uint32 mapId, Difficulty difficulty,
                                                       std::vector<DungeonBossInfo> base)
{
    for (BossRosterPatch const& patch : PatchTable())
    {
        if (patch.mapId != mapId || !DcGateMatches(patch.gate, difficulty))
            continue;
        base = ApplyOne(patch, std::move(base));
    }
    return base;
}

