/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARUTIL_H
#define _PLAYERBOT_DUNGEONCLEARUTIL_H

#include <optional>
#include <string>
#include <vector>

#include "Log.h"
#include "MoveSplineInitArgs.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
// Umbrella: units split out of the former DungeonClearUtil god-class. Files that
// include DungeonClearUtil.h keep resolving DcEngageGeometry::/DcTargeting::/...
// without per-file include churn during the decomposition.
#include "Ai/Dungeon/DungeonClear/Util/DcEngageGeometry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLootPolicy.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcFollowerLifecycle.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStatusPublisher.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPullPlanner.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPartyState.h"

// --- Advanced-pull log channel --------------------------------------------
// Pull mode (LOS pull-to-camp) gets its OWN log category, a child of the main
// DungeonClear channel (playerbots.dungeonclear.pull). It inherits the parent
// appender by default, but can be split to its own file and raised to Debug /
// Trace independently — without flooding the main channel — via
// Logger.playerbots.dungeonclear.pull in mod_dungeon_clear.conf. The feature is
// young and chatty to debug, so every pull state-machine transition, camp
// decision, leg watchdog, and passive-management step routes through these.
//
//   DC_PULL_INFO  — milestones a maintainer wants even at the default level
//                   (camp marked, aggro confirmed, party released, aborts).
//   DC_PULL_DEBUG — per-decision detail (distances, gate outcomes, phase math).
//   DC_PULL_TRACE — per-tick spam (run-in/return movement, hold-at-camp parking).
#define DC_PULL_LOG_CATEGORY "playerbots.dungeonclear.pull"
#define DC_PULL_INFO(...)  LOG_INFO(DC_PULL_LOG_CATEGORY, __VA_ARGS__)
#define DC_PULL_DEBUG(...) LOG_DEBUG(DC_PULL_LOG_CATEGORY, __VA_ARGS__)
#define DC_PULL_TRACE(...) LOG_TRACE(DC_PULL_LOG_CATEGORY, __VA_ARGS__)

class WorldObject;
class Player;
class Unit;
class Creature;
class GameObject;
// InstanceScript is an alias of InstanceData on this core (compat shim);
// a class forward-declaration would shadow the alias with an empty type.
class InstanceData;
using InstanceScript = InstanceData;
class PlayerbotAI;
namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
struct DungeonBossInfo;
struct Position;

// DcPullPhase + DcPullContext now live in DcPullContext.h (included above) so the
// one owned pull-state struct is visible to both the util layer and the value
// layer without a circular include.

// DungeonClearUtil has been fully decomposed into the cohesive units included
// above (DcTargeting, DcEngageGeometry, DcPullPlanner, DcLeaderSignal,
// DcLootPolicy, DcPartyState, DcFollowerLifecycle, DcStatusPublisher). This
// header now serves only as the umbrella that pulls them in (so existing
// includers need no churn) and as the home of the DC_PULL_* log macros above.

#endif
