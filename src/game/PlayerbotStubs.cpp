// Empty implementations of the bot-subsystem hooks for builds that don't link
// the playerbots module (BUILD_PLAYERBOTS=OFF). The host code calls these
// unconditionally; without these stubs the link step fails on missing symbols.
//
// When BUILD_PLAYERBOTS=ON the bot module provides the real implementations
// (HostHooks.cpp + BotActionLog.cpp) and this file is excluded from the build
// (see src/game/CMakeLists.txt).

#include "Objects/Player.h"
#include "Objects/Unit.h"
#include "World.h"
#include "WorldPacket.h"
#include "Chat/Chat.h"

void Player::CreatePlayerbotAI()              {}
void Player::RemovePlayerbotAI()              {}
void Player::CreatePlayerbotMgr()             {}
void Player::RemovePlayerbotMgr()             {}
// No bot Player can exist when BUILD_PLAYERBOTS=OFF, so every Player is real.
bool Player::isRealPlayer() const             { return true; }
void Player::UpdatePlayerbotHooks(uint32)     {}

void World::UpdatePlayerbotsTick(uint32)      {}
void World::InitPlayerbotsAtStartup()         {}
void World::FinalizePlayerbotsPostPlayerInfo() {}

bool Player_DispatchBotOutgoingPacket(Player*, WorldPacket const&) { return false; }
void Player_DispatchMasterIncomingPacket(Player*, WorldPacket const&) {}
void Player_DispatchBotChatCommand(Player*, uint32, std::string const&, uint32, std::string const&) {}
void Playerbot_SetForcedRole(Player*, uint8) {}
uint8 Playerbot_GetAllowedRoles(Player*) { return 0; }

void BotActionLog_LogCastStart  (WorldObject*, uint32, uint64, uint32)         {}
void BotActionLog_LogCastResult (WorldObject*, uint32, uint8, const char*)     {}
void BotActionLog_LogDamage     (Unit*, Unit*, uint32, uint32, const char*)    {}
void BotActionLog_LogAuraAttempt(Unit*, uint32, int32, uint64)                 {}
void BotActionLog_LogAuraApply  (Unit*, uint32, int32, uint64)                 {}
void BotActionLog_LogAuraRemove (Unit*, uint32, uint64)                        {}

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
