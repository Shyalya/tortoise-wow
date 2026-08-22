/*
 * mod-dungeon-clear — DcPlayerbotCompat.h
 *
 * "Who is driving this character?", asked so that it means the same thing on
 * every mod-playerbots branch this module is built against.
 *
 * mod-playerbots PR #2592 (on test-staging; not on master as of Aug 2026)
 * reshuffled these predicates AND reused one of the names for something with
 * the opposite meaning:
 *
 *   before #2592                       after #2592
 *   ------------------------------     --------------------------------------
 *   botAI->IsRealPlayer()              IsSelfBot(player)
 *     master == bot, i.e. a SELF-BOT     same meaning, new name
 *   (no equivalent)                    IsRealPlayer(player)
 *                                        no PlayerbotAI at all — a person on
 *                                        a game client. The OPPOSITE of what
 *                                        the old member of that name tested.
 *   botAI->HasRealPlayerMaster()       botAI->HasGameClientMaster()
 *   botAI->HasActivePlayerMaster()     IsRealPlayer(botAI->GetMaster())
 *
 * So a module that must build on both cannot call either name directly: the
 * member is gone on one branch, and on the other the surviving free function
 * of the same name compiles fine and inverts the answer. That is not a
 * hypothetical — it is what makes this a shim rather than a typedef.
 *
 * Nothing here is conditional. Both predicates are written in terms of the two
 * pieces of API that are byte-identical on both branches — the
 * GET_PLAYERBOT_AI macro and the public PlayerbotAI::GetMaster() — and each is
 * literally the body of its branch's own version:
 *
 *   pre-#2592   IsRealPlayer()  { return master ? (master == bot) : false; }
 *   post-#2592  IsSelfBot(p)    { ai = GET_PLAYERBOT_AI(p); return ai && ai->GetMaster() == p; }
 *
 * which are the same test. No SFINAE, no version macro, no #if: there is
 * nothing to detect, so detecting it would only add a way to be wrong.
 *
 * Call these qualified (DcPlayerbotCompat::IsSelfBot(p)). Unqualified would be
 * ambiguous against the global IsSelfBot/IsRealPlayer that exist post-#2592.
 *
 * There is deliberately no HasGameClientMaster() here: DC does not need one,
 * and it is spelled IsHumanControlled(botAI->GetMaster()) if it ever does.
 */

#ifndef _DUNGEONCLEAR_DCPLAYERBOTCOMPAT_H
#define _DUNGEONCLEAR_DCPLAYERBOTCOMPAT_H

#include "Playerbots.h"

class Player;

namespace DcPlayerbotCompat
{
    // A bot whose master is itself: the human's own character on autopilot.
    // They share one GUID, so anything this bot does the player is credited
    // with — see the loot double-roll this module exists to suppress.
    inline bool IsSelfBot(Player* player)
    {
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        return botAI && botAI->GetMaster() == player;
    }

    // No bot AI of any kind: an actual person playing through a game client.
    // The null check is load-bearing — GET_PLAYERBOT_AI(nullptr) returns
    // nullptr, so without it a null Player would read as a real person.
    inline bool IsRealPlayer(Player* player)
    {
        return player && !GET_PLAYERBOT_AI(player);
    }

    // Either of the above: somebody who will act for themselves — walk up and
    // loot, answer their own roll. This is usually the one you want, and it is
    // what the pre-#2592 idiom `!ai || ai->IsRealPlayer()` spelled out.
    //
    // Qualified even here, one scope away from the definitions, and that is
    // not style: Player lives in the global namespace, so ADL on a Player*
    // argument reaches global scope, where post-#2592 there is an
    // ::IsRealPlayer(Player*) with this exact signature. An unqualified call
    // would put both in the overload set — two exact matches, ambiguous, and
    // it would fail to compile on the very branch this shim exists for.
    inline bool IsHumanControlled(Player* player)
    {
        return DcPlayerbotCompat::IsRealPlayer(player)
            || DcPlayerbotCompat::IsSelfBot(player);
    }
}

#endif
