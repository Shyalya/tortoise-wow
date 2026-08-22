/*
 * mod-dungeon-clear — StayDeadAction.cpp
 */

#include "StayDeadAction.h"

#include "Map.h"
#include "Player.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"

bool DungeonClearStayDeadAction::isUseful()
{
    // Dungeon/raid maps only. The stay-dead override exists for the wipe-recovery
    // case; a bot dead out in the open world keeps stock auto-release.
    //
    // A MAPLESS bot (mid-teleport / login limbo - the bot rotation produces
    // these constantly) must return false here and NEVER fall through to the
    // stock isUseful: the stock body reads bot->FindMap(), and this engine's
    // GetMap THROWS on a missing map. That exception unwinding through this
    // frame was the 11:0x crash wave (SIGSEGV in _Unwind_Resume on the map
    // pool thread). Deciding nothing for one tick is free - the bot is not
    // even on a map to release from.
    Map const* map = bot ? bot->FindMap() : nullptr;
    if (!map)
        return false;
    if (!map->IsDungeon())
        return AutoReleaseSpiritAction::isUseful();

    // Read live (not cached) so .reload config and per-run overrides both take
    // effect without a restart.
    if (DcSettings::GetBool(bot, "PreventBotRelease"))
        return false;  // never auto-release; bot stays a corpse until rezzed

    return AutoReleaseSpiritAction::isUseful();
}


