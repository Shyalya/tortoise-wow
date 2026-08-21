#ifndef BOT_SLOTS_H
#define BOT_SLOTS_H

#include "ModuleSlots.h"
#include "Objects/Player.h"

class PlayerbotAI;
class PlayerbotMgr;

// Typed access to the two player slots this module claims.
//
// These replace Player::GetPlayerbotAI() and Player::GetPlayerbotMgr(). The
// pointers live in the same place they always did - one load from a fixed
// offset in Player - so reading them costs what it used to. What changed is
// that Player.h no longer names a bot type.
//
// Free functions rather than members because a module cannot add members to a
// core class, which is the whole point of the slots.

inline PlayerbotAI* GetBotAI(Player const* player)
{
    return player ? player->GetModuleSlotAs<PlayerbotAI>(MODULE_SLOT_BOT_AI) : nullptr;
}

inline PlayerbotMgr* GetBotMgr(Player const* player)
{
    return player ? player->GetModuleSlotAs<PlayerbotMgr>(MODULE_SLOT_BOT_MGR) : nullptr;
}

inline void SetBotAI(Player* player, PlayerbotAI* ai)
{
    if (player)
        player->SetModuleSlot(MODULE_SLOT_BOT_AI, ai);
}

inline void SetBotMgr(Player* player, PlayerbotMgr* mgr)
{
    if (player)
        player->SetModuleSlot(MODULE_SLOT_BOT_MGR, mgr);
}

// Lifecycle. Were Player::Create/RemovePlayerbotAI and ...Mgr.
void CreateBotAI(Player* player);
void RemoveBotAI(Player* player);
void CreateBotMgr(Player* player);
void RemoveBotMgr(Player* player);

// Was Player::isRealPlayer(). Out of line because it has to ask the AI whether
// a real session sits behind it - having an AI attached is not the same as
// being machine driven.
bool IsRealPlayer(Player const* player);

#endif
