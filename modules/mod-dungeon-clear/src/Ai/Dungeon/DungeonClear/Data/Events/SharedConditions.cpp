/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"

#include "Player.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

// --- Cross-dungeon event activation conditions ---------------------------
// Conditions that are NOT specific to one dungeon live here so several maps can
// reference the same id. Per-dungeon conditions stay in that dungeon's file.

// --- Room-aggro pre-clear condition ---------------------------------------
// Generic across EVERY RoomAggroRegistry boss: DUE while the room-trash value
// still has anything to clear. That value already encodes all the spatial logic
// — next boss is a room-aggro boss, the room radius around the LIVE boss, the
// boss-aggro-sphere / unreachable / door-blocked exclusions, and the
// RoomClearTimeout give-up valve — so the condition is a thin "is the room not
// yet clear?" read and the event only orchestrates "clear before the pull".
// Reads false (event not due, boss pull proceeds) the moment the room is clear
// or the value gives up. See DungeonClearRoomTrashValue. External linkage
// (declared in DungeonEventTables.h) so SM Cathedral / Scholomance / any future
// RoomAggroRegistry dungeon can pass &DcRoomAggroPreClearCondition directly to
// .Conditional() instead of sharing a global condition id.
bool DcRoomAggroPreClearCondition(Player* /*bot*/, AiObjectContext* context)
{
    if (!context)
        return false;
    return !context->GetValue<GuidVector>(DcKey::RoomTrashRemaining)->Get().empty();
}
