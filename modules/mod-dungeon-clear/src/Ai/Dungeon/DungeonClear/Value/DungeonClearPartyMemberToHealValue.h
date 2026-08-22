/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARPARTYMEMBERTOHEALVALUE_H
#define _PLAYERBOT_DUNGEONCLEARPARTYMEMBERTOHEALVALUE_H

#include "PartyMemberToHeal.h"

class PlayerbotAI;
class Unit;

// DC decorator over the stock `party member to heal` value. It is registered
// under the SAME name, so it fronts the stock value for every bot: DC's value
// context is Add()ed after playerbots' base context on the first world tick, and
// the merged creators map is last-wins (see NamedObjectContext::Add), so this
// creator replaces the stock one.
//
// It delegates to the stock calculation unchanged and then — ONLY while this
// bot's group is running a DC creature escort (DcLeaderSignal::GetLeaderEscortee
// resolves the live escortee: Old Hillsbrad's Thrall, Wailing Caverns' Disciple,
// any future escort) — folds the escortee into the candidate set, returning
// whichever of {stock most-hurt member, escortee} is more hurt. Every heal action
// reads `party member to heal` for its target, so this makes the whole stock heal
// rotation treat the escortee exactly like a party member with zero new casting
// code. Off-escort it returns the stock result verbatim, so healing for every
// other pull / dungeon / non-DC bot is byte-identical to before.
class DungeonClearPartyMemberToHealValue : public PartyMemberToHeal
{
public:
    DungeonClearPartyMemberToHealValue(PlayerbotAI* botAI)
        : PartyMemberToHeal(botAI, "party member to heal")
    {
    }

protected:
    Unit* Calculate() override;
};

#endif
