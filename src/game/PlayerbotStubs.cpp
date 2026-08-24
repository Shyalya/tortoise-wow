// Empty implementations for builds without the playerbots module
// (BUILD_PLAYERBOTS=OFF). The host calls these unconditionally; without the
// stubs the link step fails on missing symbols. When the module is built it
// provides the real ones and this file is excluded (src/game/CMakeLists.txt).
//
// This list used to be three times as long. Everything the bots reached the
// core through is a module hook now, and a hook needs no stub - the virtual
// has an empty body and nothing registers it. What is left are the few
// symbols that are genuinely core-side: the module bootstrap, the damage-log
// probes compiled into Unit.cpp, and the chat commands Chat.cpp registers
// whether or not the module exists.

#include "Objects/Player.h"
#include "Objects/Unit.h"
#include "World.h"
#include "WorldPacket.h"
#include "Chat/Chat.h"

#if defined(__GNUC__) || defined(__clang__)
#define TORTOISE_BOT_STUB_WEAK __attribute__((weak))
#else
#define TORTOISE_BOT_STUB_WEAK
#endif

TORTOISE_BOT_STUB_WEAK void BotActionLog_LogCastStart  (WorldObject*, uint32, uint64, uint32)      {}
TORTOISE_BOT_STUB_WEAK void BotActionLog_LogCastResult (WorldObject*, uint32, uint8, const char*)  {}
TORTOISE_BOT_STUB_WEAK void BotActionLog_LogDamage     (Unit*, Unit*, uint32, uint32, const char*) {}
TORTOISE_BOT_STUB_WEAK void BotActionLog_LogAuraAttempt(Unit*, uint32, int32, uint64)              {}
TORTOISE_BOT_STUB_WEAK void BotActionLog_LogAuraApply  (Unit*, uint32, int32, uint64)              {}
TORTOISE_BOT_STUB_WEAK void BotActionLog_LogAuraRemove (Unit*, uint32, uint64)                     {}

#undef TORTOISE_BOT_STUB_WEAK

// ChatHandler bot-command stubs. Chat.cpp registers `.bot`, `.rndbot`,
// `.ahbot`, and `.perfmon` in the command table unconditionally (no
// #ifdef BUILD_PLAYERBOTS gate), so the host must link these symbols even
// when the bot module isn't compiled in. Return true and inform the user.
bool ChatHandler::HandlePlayerbotCommand(char*)
    { SendSysMessage("Playerbots not built (BUILD_PLAYERBOTS=OFF)."); return true; }
bool ChatHandler::HandleRandomPlayerbotCommand(char*)
    { SendSysMessage("Random playerbots not built (BUILD_PLAYERBOTS=OFF)."); return true; }
bool ChatHandler::HandleAhBotCommand(char*)
    { SendSysMessage("AHBot not built (BUILD_PLAYERBOTS=OFF)."); return true; }
bool ChatHandler::HandlePerfMonCommand(char*)
    { SendSysMessage("Bot performance monitor not built (BUILD_PLAYERBOTS=OFF)."); return true; }
