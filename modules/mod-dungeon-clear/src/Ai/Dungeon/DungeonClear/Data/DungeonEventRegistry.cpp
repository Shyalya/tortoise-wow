/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonEventRegistry.h"

#include <utility>

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"

// --- EventBuilder ---------------------------------------------------------

EventBuilder::EventBuilder(uint32 mapId, uint32 id, std::string name)
{
    _ev.mapId = mapId;
    _ev.id = id;
    _ev.name = std::move(name);
}

EventStep& EventBuilder::Add(EventStepKind kind)
{
    _ev.steps.emplace_back();
    EventStep& s = _ev.steps.back();
    s.kind = kind;
    return s;
}

EventBuilder& EventBuilder::Anchored(uint32 orderIndex)
{
    _ev.activation = EventActivation::Anchored;
    _ev.orderIndex = orderIndex;
    return *this;
}

EventBuilder& EventBuilder::Conditional(EventCondition condition)
{
    _ev.activation = EventActivation::Conditional;
    _ev.condition = std::move(condition);
    return *this;
}

EventBuilder& EventBuilder::Optional()
{
    _ev.required = false;
    return *this;
}

EventBuilder& EventBuilder::Repeatable()
{
    _ev.repeatable = true;
    return *this;
}

EventBuilder& EventBuilder::Persistent()
{
    _ev.persistent = true;
    return *this;
}

EventBuilder& EventBuilder::DrivesInCombat()
{
    _ev.drivesInCombat = true;
    return *this;
}

EventBuilder& EventBuilder::StepsOwnMovement()
{
    _ev.stepsOwnMovement = true;
    return *this;
}

EventBuilder& EventBuilder::HeroicOnly()
{
    _ev.gate = DcDifficultyGate::HeroicOnly;
    return *this;
}

EventBuilder& EventBuilder::NormalOnly()
{
    _ev.gate = DcDifficultyGate::NormalOnly;
    return *this;
}

EventBuilder& EventBuilder::PanelBeforeBoss(uint32 bossEntry)
{
    _ev.panelGatesBossEntry = bossEntry;
    return *this;
}

EventBuilder& EventBuilder::PanelAfterBoss(uint32 bossEntry)
{
    _ev.panelSortAfterBossEntry = bossEntry;
    return *this;
}

EventBuilder& EventBuilder::PanelTeam(TeamId team)
{
    _ev.panelTeam = team;
    return *this;
}

EventBuilder& EventBuilder::Timeout(uint32 ms)
{
    if (!_ev.steps.empty())
        _ev.steps.back().timeoutMs = ms;
    return *this;
}

EventBuilder& EventBuilder::SkipIfTargetMissing()
{
    if (!_ev.steps.empty())
        _ev.steps.back().skipIfMissing = true;
    return *this;
}

EventBuilder& EventBuilder::WaitTargetStill()
{
    if (!_ev.steps.empty())
        _ev.steps.back().waitForStill = true;
    return *this;
}

EventBuilder& EventBuilder::MoveTo(float x, float y, float z, float radius)
{
    EventStep& s = Add(EventStepKind::MoveTo);
    s.x = x;
    s.y = y;
    s.z = z;
    s.radius = radius;
    return *this;
}

EventBuilder& EventBuilder::Jump(float x, float y, float z, float radius)
{
    EventStep& s = Add(EventStepKind::Jump);
    s.x = x;
    s.y = y;
    s.z = z;
    s.radius = radius;
    return *this;
}

EventBuilder& EventBuilder::MoveToHoldUntilSpawn(float x, float y, float z, float radius,
                                                 uint32 creatureEntry, bool wantAlive)
{
    EventStep& s = Add(EventStepKind::MoveTo);
    s.x = x;
    s.y = y;
    s.z = z;
    s.radius = radius;
    s.creatureEntry = creatureEntry;  // non-zero => garrison-until-spawn gate
    s.wantAlive = wantAlive;
    return *this;
}

EventBuilder& EventBuilder::MoveToHoldUntilInstanceData(float x, float y, float z, float radius,
                                                        uint32 dataId, uint32 minValue)
{
    EventStep& s = Add(EventStepKind::MoveTo);
    s.x = x;
    s.y = y;
    s.z = z;
    s.radius = radius;
    s.instanceDataId = static_cast<int32>(dataId);  // >= 0 => instance-data gate
    s.instanceDataMin = minValue;
    return *this;
}

EventBuilder& EventBuilder::MoveToHoldUntilPersistentData(float x, float y, float z, float radius,
                                                          uint32 dataId, uint32 minValue)
{
    EventStep& s = Add(EventStepKind::MoveTo);
    s.x = x;
    s.y = y;
    s.z = z;
    s.radius = radius;
    s.persistentDataId = static_cast<int32>(dataId);  // >= 0 => persistent-data gate
    s.persistentDataMin = minValue;
    return *this;
}

EventBuilder& EventBuilder::WhileHolding(uint32 hookId)
{
    if (!_ev.steps.empty())
        _ev.steps.back().hookId = hookId;
    return *this;
}

EventBuilder& EventBuilder::UseGO(uint32 goEntry, float searchRadius,
                                  float x, float y, float z)
{
    EventStep& s = Add(EventStepKind::UseGameObject);
    s.goEntry = goEntry;
    s.radius = searchRadius;
    s.x = x;
    s.y = y;
    s.z = z;
    return *this;
}

EventBuilder& EventBuilder::CastSpell(uint32 spellId)
{
    EventStep& s = Add(EventStepKind::CastSpell);
    s.spellId = spellId;
    return *this;
}

EventBuilder& EventBuilder::UseItem(uint32 itemId)
{
    EventStep& s = Add(EventStepKind::UseItem);
    s.itemId = itemId;
    return *this;
}

EventBuilder& EventBuilder::Gossip(uint32 creatureEntry, int32 option, float searchRadius)
{
    EventStep& s = Add(EventStepKind::Gossip);
    s.creatureEntry = creatureEntry;
    s.gossipOption = option;
    s.radius = searchRadius;
    return *this;
}

EventBuilder& EventBuilder::WaitForSpawn(uint32 creatureEntry, bool wantAlive, uint32 timeoutMs)
{
    EventStep& s = Add(EventStepKind::WaitForSpawn);
    s.creatureEntry = creatureEntry;
    s.wantAlive = wantAlive;
    s.timeoutMs = timeoutMs;
    return *this;
}

EventBuilder& EventBuilder::WaitForGOState(uint32 goEntry, uint32 wantState,
                                           uint32 timeoutMs, float searchRadius)
{
    EventStep& s = Add(EventStepKind::WaitForGameObjectState);
    s.goEntry = goEntry;
    s.wantState = wantState;
    s.timeoutMs = timeoutMs;
    s.radius = searchRadius;
    return *this;
}

EventBuilder& EventBuilder::KillCreature(uint32 creatureEntry, uint32 count, float searchRadius)
{
    EventStep& s = Add(EventStepKind::KillCreature);
    s.creatureEntry = creatureEntry;
    s.count = count;
    s.radius = searchRadius;
    return *this;
}

EventBuilder& EventBuilder::KillCreatureEngage(uint32 creatureEntry, uint32 count,
                                               float searchRadius)
{
    EventStep& s = Add(EventStepKind::KillCreature);
    s.creatureEntry = creatureEntry;
    s.count = count;
    s.radius = searchRadius;
    s.engage = true;
    return *this;
}

EventBuilder& EventBuilder::EngageOnlyWhenActive()
{
    if (!_ev.steps.empty())
        _ev.steps.back().engageOnlyWhenActive = true;
    return *this;
}

EventBuilder& EventBuilder::ClearRadius(float x, float y, float z, float radius,
                                        float zBand)
{
    EventStep& s = Add(EventStepKind::ClearRadius);
    s.x = x;
    s.y = y;
    s.z = z;
    s.radius = radius;
    s.zBand = zBand;
    s.engage = true;  // the driving action seeks out and fights in-radius hostiles
    return *this;
}

EventBuilder& EventBuilder::OnlyEntries(std::vector<uint32> entries)
{
    if (!_ev.steps.empty())
        _ev.steps.back().entryFilter = std::move(entries);
    return *this;
}

EventBuilder& EventBuilder::Wait(uint32 durationMs)
{
    EventStep& s = Add(EventStepKind::Wait);
    s.durationMs = durationMs;
    return *this;
}

EventBuilder& EventBuilder::Custom(uint32 hookId)
{
    EventStep& s = Add(EventStepKind::Custom);
    s.hookId = hookId;
    return *this;
}

EventBuilder& EventBuilder::EscortCreature(uint32 escortee, int32 startGossipOption,
                                           uint32 doneEntry, int32 doneBit,
                                           float standoff, float threatRadius,
                                           float threatZBand, float searchRadius,
                                           int32 doneDataId, uint32 doneDataMin)
{
    EventStep& s = Add(EventStepKind::EscortCreature);
    s.creatureEntry = escortee;
    s.gossipOption = startGossipOption;
    s.escortDoneEntry = doneEntry;
    s.escortDoneBit = doneBit;
    s.escortStandoff = standoff;
    s.escortThreatRadius = threatRadius;
    s.zBand = threatZBand;
    s.radius = searchRadius;
    // Reuse the shared instance-data fields as the alternative completion gate:
    // the EscortCreature RunStep/driver read GetData(instanceDataId) >= min.
    s.instanceDataId = doneDataId;
    s.instanceDataMin = doneDataMin;
    return *this;
}

EventBuilder& EventBuilder::UseItemOnGO(uint32 itemId, uint32 spellId, uint32 goEntry,
                                        float x, float y, float z, float radius)
{
    EventStep& s = Add(EventStepKind::UseItemOnGO);
    s.itemId = itemId;
    s.spellId = spellId;
    s.goEntry = goEntry;
    s.x = x;
    s.y = y;
    s.z = z;
    s.radius = radius;
    return *this;
}

EventBuilder& EventBuilder::DropInHole(float overX, float overY, float overZ,
                                       float landX, float landY, float landZ)
{
    EventStep& s = Add(EventStepKind::DropInHole);
    s.x = overX;
    s.y = overY;
    s.z = overZ;
    s.landX = landX;
    s.landY = landY;
    s.landZ = landZ;
    return *this;
}

EventBuilder& EventBuilder::TeleportParty(float checkpointX, float checkpointY,
                                          float checkpointZ, float landX, float landY,
                                          float landZ, float radius)
{
    EventStep& s = Add(EventStepKind::TeleportParty);
    s.x = checkpointX;
    s.y = checkpointY;
    s.z = checkpointZ;
    s.landX = landX;
    s.landY = landY;
    s.landZ = landZ;
    s.radius = radius;
    return *this;
}

// --- The event table ------------------------------------------------------
// The rows themselves live one-file-per-dungeon under Data/Events/, each
// exposing a Register<Dungeon>Events appender (declared in DungeonEventTables.h).
// This aggregator calls every appender to assemble the table — the explicit
// calls are what keep each per-dungeon TU linked into the module static lib (a
// self-registering static initializer would be dropped). Add a dungeon by
// creating its file + appender and adding one call below. Event ids are per-map
// and are referenced from the matching travel-objective anchor's
// DungeonBossInfo::eventId (BossRosterRegistry).

namespace
{
    std::vector<DungeonEvent> const& EventTable()
    {
        static std::vector<DungeonEvent> const kEvents = []
        {
            std::vector<DungeonEvent> t;
            // Order here is table-scan order only; lookups key on mapId + id, so
            // it never affects behaviour.
            RegisterShadowfangKeepEvents(t);
            RegisterScarletMonasteryEvents(t);
            RegisterRazorfenDownsEvents(t);
            RegisterSunkenTempleEvents(t);
            RegisterZulFarrakEvents(t);
            RegisterBlackrockDepthsEvents(t);
            RegisterDeadminesEvents(t);
            RegisterWailingCavernsEvents(t);
            RegisterStratholmeEvents(t);
            RegisterUldamanEvents(t);
            RegisterScholomanceEvents(t);
            RegisterDireMaulEvents(t);
            RegisterHellfireRampartsEvents(t);
            RegisterBloodFurnaceEvents(t);
            RegisterSlavePensEvents(t);
            RegisterUnderbogEvents(t);
            RegisterOldHillsbradEvents(t);
            RegisterMechanarEvents(t);
            RegisterShatteredHallsEvents(t);
            RegisterSteamvaultEvents(t);
            RegisterArcatrazEvents(t);
            RegisterSethekkHallsEvents(t);
            RegisterBlackMorassEvents(t);
            RegisterUtgardeKeepEvents(t);
            RegisterNexusEvents(t);
            RegisterAzjolNerubEvents(t);
            return t;
        }();
        return kEvents;
    }
}

std::vector<DungeonEvent> const& DungeonEventRegistry::AllEvents()
{
    return EventTable();
}

DungeonEvent const* DungeonEventRegistry::Find(uint32 mapId, uint32 id)
{
    if (id == 0)
        return nullptr;
    for (DungeonEvent const& e : EventTable())
        if (e.mapId == mapId && e.id == id)
            return &e;
    return nullptr;
}

bool DungeonEventRegistry::HasEvents(uint32 mapId)
{
    for (DungeonEvent const& e : EventTable())
        if (e.mapId == mapId)
            return true;
    return false;
}

std::vector<DungeonEvent const*> DungeonEventRegistry::Conditional(uint32 mapId)
{
    std::vector<DungeonEvent const*> out;
    for (DungeonEvent const& e : EventTable())
        if (e.mapId == mapId && e.activation == EventActivation::Conditional)
            out.push_back(&e);
    return out;
}

std::vector<DungeonEvent const*> DungeonEventRegistry::Conditional(uint32 mapId, Difficulty difficulty)
{
    std::vector<DungeonEvent const*> out;
    for (DungeonEvent const& e : EventTable())
        if (e.mapId == mapId && e.activation == EventActivation::Conditional &&
            DcGateMatches(e.gate, difficulty))
            out.push_back(&e);
    return out;
}

bool DungeonEventRegistry::IsRoomAggroPreClear(DungeonEvent const& ev)
{
    return ev.activation == EventActivation::Conditional &&
           ev.steps.size() == 1 &&
           ev.steps[0].kind == EventStepKind::KillCreature &&
           ev.steps[0].creatureEntry == 0;
}

bool DungeonEventRegistry::HasRoomAggroEvent(uint32 mapId)
{
    for (DungeonEvent const& e : EventTable())
        if (e.mapId == mapId && IsRoomAggroPreClear(e))
            return true;
    return false;
}
