/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcSocialQuarantine.h"

#include "Ai/Dungeon/DungeonClear/Data/DcSocialQuarantineRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/ScriptedPullRegistry.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "AiObjectContext.h"
#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"

namespace
{
    // A creature is a quarantine candidate iff it is a live, DB-spawned member of
    // one of the volumes in force. Summons are never candidates — every zone is
    // authored over static spawns, and a boss's own summoned adds must keep the
    // behaviour their script gave them.
    bool Holdable(Creature const* c)
    {
        return c && c->IsAlive() && !c->IsSummon() && !c->IsPet();
    }

    // Hold / release, logging only the EDGE. The steady state is one enum
    // comparison per creature per tick; a room's worth of edges is a handful of
    // lines per stage, which is what makes the log readable when a pull brings
    // more than the plan said it would.
    void SetHeld(Creature* c, bool held, char const* zoneName, Player const* bot)
    {
        ReactStates const want = held ? REACT_DEFENSIVE : REACT_AGGRESSIVE;
        if (c->GetReactState() == want)
            return;

        // Only ever move a creature between these two states. Anything sitting at
        // REACT_PASSIVE was put there by its own script (a not-yet-activated event
        // mob, a prop) and is none of our business — stomping it to AGGRESSIVE on
        // release would be the registry starting fights rather than preventing
        // them.
        if (c->GetReactState() != REACT_AGGRESSIVE && c->GetReactState() != REACT_DEFENSIVE)
            return;

        c->SetReactState(want);
        LOG_DEBUG("playerbots.dungeonclear",
                  "[DC:{}] social quarantine: {} {} ({}) at ({:.1f},{:.1f}) — {}",
                  bot->GetName(), held ? "holding" : "releasing", c->GetName(),
                  c->GetEntry(), c->GetPositionX(), c->GetPositionY(), zoneName);
    }

    // One volume that is in force this tick, flattened from both sources so the
    // creature walk below is a single pass.
    struct HeldVolume
    {
        char const* name;
        float x, y, z, radius, zBand;
        std::vector<uint32> const* entries;
    };

    bool InVolume(HeldVolume const& v, Creature const* c)
    {
        if (std::fabs(c->GetPositionZ() - v.z) > v.zBand)
            return false;
        float const dx = c->GetPositionX() - v.x;
        float const dy = c->GetPositionY() - v.y;
        if (dx * dx + dy * dy > v.radius * v.radius)
            return false;
        if (!v.entries || v.entries->empty())
            return false;
        return std::find(v.entries->begin(), v.entries->end(), c->GetEntry()) !=
               v.entries->end();
    }
}

namespace DcSocialQuarantine
{
    void Update(Player* bot, AiObjectContext* context)
    {
        if (!bot || !context)
            return;

        uint32 const mapId = bot->GetMapId();
        bool const anyRegistryZone = DcSocialQuarantineRegistry::HasRows(mapId);
        bool const anyScriptedRow  = ScriptedPullRegistry::HasRows(mapId);
        if (!anyRegistryZone && !anyScriptedRow)
            return;

        Map* map = bot->FindMap();
        if (!map)
            return;

        // The boss gate. Everything here is scoped to the approach the run is
        // actually on: with no next boss (all dead / skipped) nothing is in force
        // and the pass below is a pure release.
        std::optional<DungeonBossInfo> const next =
            context->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
        uint32 const bossEntry = next.has_value() ? next->entry : 0u;

        // The one pack that must NOT be held: the stage the plan is walking the
        // tank into. Memoised — this is the same answer the pull trigger, the pull
        // target value and the pull action all read this tick.
        ScriptedPullStage const* const active =
            bossEntry ? DcTickMemoAccess::ScriptedStage(bot, context) : nullptr;

        std::vector<HeldVolume> held;

        if (bossEntry && anyScriptedRow)
            for (ScriptedPullStage const* s : ScriptedPullRegistry::Rows(mapId))
            {
                if (s->bossEntry != bossEntry)
                    continue;
                // The due/in-flight stage is the pull; releasing it is the whole
                // point of naming an active stage at all.
                if (active && active->order == s->order)
                    continue;
                held.push_back({s->name, s->packX, s->packY, s->packZ, s->packRadius,
                                s->packZBand, &s->entries});
            }

        if (bossEntry && anyRegistryZone)
            for (DcQuarantineZone const* z :
                 DcSocialQuarantineRegistry::Zones(mapId, bossEntry))
                held.push_back({z->name, z->x, z->y, z->z, z->radius, z->zBand,
                                &z->entries});

        // Every volume this map can ever hold, so the pass can RELEASE a creature
        // whose volume stopped being in force (its stage armed, or its boss died)
        // without needing to remember that it was ever held.
        std::vector<HeldVolume> known;
        if (anyScriptedRow)
            for (ScriptedPullStage const* s : ScriptedPullRegistry::Rows(mapId))
                known.push_back({s->name, s->packX, s->packY, s->packZ, s->packRadius,
                                 s->packZBand, &s->entries});
        if (anyRegistryZone)
            for (DcQuarantineZone const* z : DcSocialQuarantineRegistry::AllZones(mapId))
                known.push_back({z->name, z->x, z->y, z->z, z->radius, z->zBand,
                                 &z->entries});

        for (auto const& kv : map->GetCreatureBySpawnIdStore())
        {
            Creature* c = kv.second;
            if (!Holdable(c))
                continue;

            // A creature ALREADY FIGHTING is left exactly as it is. Quarantine is
            // about who joins a fight, not about who is in one: CanAssistTo rejects
            // an engaged creature anyway (IsEngaged), and flipping the react state
            // of something currently swinging at the party would only change how it
            // re-acquires after its victim dies.
            if (c->IsEngaged())
                continue;

            HeldVolume const* hold = nullptr;
            for (HeldVolume const& v : held)
                if (InVolume(v, c))
                {
                    hold = &v;
                    break;
                }
            if (hold)
            {
                SetHeld(c, true, hold->name, bot);
                continue;
            }

            for (HeldVolume const& v : known)
                if (InVolume(v, c))
                {
                    SetHeld(c, false, v.name, bot);
                    break;
                }
        }
    }

    void ReleaseAll(Player* bot)
    {
        if (!bot)
            return;

        uint32 const mapId = bot->GetMapId();
        bool const anyRegistryZone = DcSocialQuarantineRegistry::HasRows(mapId);
        bool const anyScriptedRow  = ScriptedPullRegistry::HasRows(mapId);
        if (!anyRegistryZone && !anyScriptedRow)
            return;

        Map* map = bot->FindMap();
        if (!map)
            return;

        std::vector<HeldVolume> known;
        if (anyScriptedRow)
            for (ScriptedPullStage const* s : ScriptedPullRegistry::Rows(mapId))
                known.push_back({s->name, s->packX, s->packY, s->packZ, s->packRadius,
                                 s->packZBand, &s->entries});
        if (anyRegistryZone)
            for (DcQuarantineZone const* z : DcSocialQuarantineRegistry::AllZones(mapId))
                known.push_back({z->name, z->x, z->y, z->z, z->radius, z->zBand,
                                 &z->entries});

        for (auto const& kv : map->GetCreatureBySpawnIdStore())
        {
            Creature* c = kv.second;
            if (!Holdable(c))
                continue;
            for (HeldVolume const& v : known)
                if (InVolume(v, c))
                {
                    SetHeld(c, false, v.name, bot);
                    break;
                }
        }
    }
}
