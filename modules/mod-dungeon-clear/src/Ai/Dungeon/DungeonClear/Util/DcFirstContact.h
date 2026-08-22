/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCFIRSTCONTACT_H
#define _PLAYERBOT_DCFIRSTCONTACT_H

class Player;
class Unit;

// "How did this fight START?" — the one question the run record could not answer.
//
// Everything DC writes down about combat is written by the PULL machinery: the
// pull FSM opens a row, samples what turned up, and closes it. So a fight DC did
// not pull produces NO ROW AT ALL. That is not a corner case — it is how bosses
// join fights they were never pulled into, and it is exactly the hole that left
// tr-20260808-170115-46 unexplainable: Priestess Delrissa's retinue is in the
// party's target list at 17:20:02 with five rotunda trash entries still alive, and
// the run has no Delrissa pull record anywhere. Whether the tank walked into her
// aggro radius, a follower did, or another mob chained her in via DoZoneInCombat
// decides which fix is even the right shape, and nothing recorded it.
//
// `PlayerScript::OnPlayerEnterCombat` is the signal, and DC has been registered on
// it (for DcPullBrake) while discarding the `enemy` parameter the whole time. It
// fires from CombatManager::UpdateOwnerCombatState in the same statement that
// raises UNIT_FLAG_IN_COMBAT, and ONLY on the 0->1 transition — so it is one event
// per player per fight, by construction, with no dedupe needed and no per-tick cost.
//
// HONEST CAVEAT, because the line will be read as gospel otherwise: the core hands
// us `CombatManager::GetAnyTarget()`, which walks an UNORDERED ref map and returns
// the first non-suppressed entry. At the transition tick there is normally exactly
// one reference — the thing that just caused it — so in practice this is the causal
// enemy. When it is not, it is still A live holder and never a phantom. The logged
// `refs=` count is what tells the two apart: refs=1 is unambiguous, refs>1 means the
// named enemy is one of several that arrived together.
namespace DcFirstContact
{
    // Record and log the enemy that put `bot` into combat. Silently ignores anyone
    // outside an active, unpaused DC run in a dungeon.
    void OnEnterCombat(Player* bot, Unit* enemy);
}

#endif  // _PLAYERBOT_DCFIRSTCONTACT_H
