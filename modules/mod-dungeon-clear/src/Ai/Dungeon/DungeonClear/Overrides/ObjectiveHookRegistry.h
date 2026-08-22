/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_OBJECTIVEHOOKREGISTRY_H
#define _PLAYERBOT_OBJECTIVEHOOKREGISTRY_H

#include <functional>
#include <unordered_map>

#include "Common.h"

class Player;
namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
struct DungeonBossInfo;

// Outcome of an objective's on-arrival hook.
enum class ObjectiveArriveResult
{
    Done,     // objective satisfied — latch it cleared and advance the clear
    Running,  // still working (e.g. mid-interaction) — hold and call again next tick
    Blocked,  // cannot complete unaided (needs the human) — stall the run
};

// Optional per-objective behaviour run when the tank reaches a travel objective
// (DungeonBossInfo::onArriveHook indexes this registry). The DEFAULT — no hook,
// id 0, or an unregistered id — is `Done`: the tank simply arrives and the clear
// advances. Registering a hook lets an objective DO something on arrival (use a
// gameobject, wait for a spawn, cast, etc.); the hook returns Running until it's
// finished and the tank holds at the anchor meanwhile.
//
// Hooks live in a hardcoded table (link-safe in the static-lib build, like the
// other DungeonClear registries). The table ships empty with a documented
// example; add a row and reference its id from a BossRosterRegistry objective.
class ObjectiveHookRegistry
{
public:
    using Hook = std::function<ObjectiveArriveResult(Player*, AiObjectContext*, DungeonBossInfo const&)>;
    using HookTable = std::unordered_map<uint32, Hook>;

    // Registers `hook` under `id` in a table under construction. Use this rather
    // than table.emplace() from the per-dungeon appenders below: hook ids are one
    // FLAT space shared by every dungeon (unlike event conditions, which are
    // function pointers and need no ids at all), so a copy-paste that reuses an id
    // is a live hazard. emplace() would silently keep the first row and drop the
    // second — the losing dungeon's objective then latches Done and its event
    // never runs, with nothing in the log to say so. This LOG_ERRORs the collision
    // and keeps the first row; the registry-integrity gtest fails on it at author
    // time. id 0 is reserved for "no hook" and is rejected the same way.
    static void AddHook(HookTable& table, uint32 id, Hook hook);

    // Runs the hook for `hookId`. hookId 0 means "no hook" and returns Done (the
    // arrive-then-continue default). A NON-ZERO hookId that is unregistered is an
    // authoring error — the objective/step meant to DO something — so it returns
    // Blocked (and LOG_WARNs once) rather than silently latching Done. See F2 in
    // the events-system review; the registry-integrity gtest catches it at author
    // time, this is the runtime backstop.
    static ObjectiveArriveResult Run(uint32 hookId, Player* bot, AiObjectContext* context,
                                     DungeonBossInfo const& info);

    // True if `hookId` names a registered hook (cheap authoring sanity gate;
    // hookId 0 is "no hook" and returns false). Used by the registry-integrity
    // gtest and callers that want to distinguish "no hook" from "broken hook".
    static bool Has(uint32 hookId);
};

// --- per-dungeon hook appenders ------------------------------------------
// A dungeon gets its own hook TU only when its on-arrival behaviour is a
// CONTROLLER rather than a one-shot action — i.e. when it re-decides from live
// world state every tick and has to own movement/engagement/selection itself.
// One-shot arrival actions (fire a cannon, grant an item, poke an NPC) stay in
// ObjectiveHookRegistry.cpp's table, where they are homogeneous and short; there
// is no gain in scattering nine ~90-line functions across nine files.
//
// Called EXPLICITLY from Hooks() for the same reason the event tables are: this
// module compiles into a static lib, so a TU whose only output is constructor
// side-effects gets dropped by the linker and its hooks silently vanish.
//
// The Black Morass (map 269) — the wave driver. See BlackMorassDriver.cpp.
void RegisterBlackMorassHooks(ObjectiveHookRegistry::HookTable& out);

#endif
