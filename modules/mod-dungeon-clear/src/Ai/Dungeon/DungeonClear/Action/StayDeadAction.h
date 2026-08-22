/*
 * mod-dungeon-clear — StayDeadAction.h
 *
 * Overrides mod-playerbots' "auto release" action so dead bots on a dungeon or
 * raid map do NOT release their spirit to the graveyard — they stay dead (a
 * corpse on the ground) until something resurrects them: a player/party rez, a
 * self-resurrect (soulstone / Reincarnation / etc.), or summon-and-revive.
 * Outside instances stock auto-release always applies.
 *
 * Housed in this module (not in mod-playerbots) so the stock module stays
 * unedited and conflict-free on upstream pulls. The wiring is the same override
 * seam DungeonClear already uses: DungeonClearActionContext registers the
 * "auto release" creator name, and because the engine's shared creator map keeps
 * the LAST registration for a given name (SharedNamedObjectContextList::Add) and
 * the DungeonClear contexts are appended AFTER playerbots builds its own, this
 * creator wins for every bot of every class.
 *
 * Gated by the config flag DungeonClear.PreventBotRelease (default on). When the
 * flag is off — or the bot is not on a dungeon/raid map — this behaves exactly
 * like the stock AutoReleaseSpiritAction.
 *
 * NOTE: this only suppresses the routine spirit-release. The DeadStrategy's
 * anti-stuck "repop" paths (falling far / location stuck) are intentionally left
 * intact as a safety net against a dead bot getting wedged in geometry.
 */

#ifndef _DUNGEONCLEAR_STAYDEADACTION_H
#define _DUNGEONCLEAR_STAYDEADACTION_H

#include "ReleaseSpiritAction.h"

class PlayerbotAI;

class DungeonClearStayDeadAction : public AutoReleaseSpiritAction
{
public:
    DungeonClearStayDeadAction(PlayerbotAI* botAI)
        : AutoReleaseSpiritAction(botAI, "auto release") {}

    bool isUseful() override;
};

#endif
