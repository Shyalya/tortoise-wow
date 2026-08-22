/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_FOLLOWER_LIFECYCLE_H
#define _DC_FOLLOWER_LIFECYCLE_H

#include "ObjectGuid.h"

class Player;

class DcFollowerLifecycle
{
public:
    // --- Orphaned follow-generator reaper -----------------------------------
    // MoveFollow is a persistent MotionMaster generator. A non-tank DC follower
    // installs one to chase the tank; its own follow-tank action tears it down
    // when the DC tank goes away (see DungeonClearFollowTankAction::Execute).
    // But that teardown only runs while the follower's PlayerbotAI is still
    // ticking. When a SELF-bot leaves bot mode (`.playerbots bot self` off),
    // playerbots `delete`s the PlayerbotAI outright — no teardown tick fires,
    // and the leftover follow generator stays installed on the now-human player,
    // gluing it to the tank with no way to self-heal (a real player has no AI to
    // clear it). MarkFollowing records every player that currently has such a
    // generator; ReapOrphanedFollows (driven each world tick from a
    // PlayerbotScript) finds any marked player still in world whose AI has gone
    // away and clears the generator, returning movement control to the player.
    static void MarkFollowing(ObjectGuid player);

    static void UnmarkFollowing(ObjectGuid player);

    static void ReapOrphanedFollows();

    // Keep a DRUID tank in (dire) bear form. No-op for any other class, or if
    // the bot is already shifted. Used during the advanced-pull drag-back so the
    // druid tank takes the run-home hits in bear form (extra armor/HP) instead of
    // caster form. Prefers dire bear form, falling back to bear form when dire
    // bear isn't trained. Shapeshift is instant and not movement-interrupted, so
    // it's safe to call every tick while the tank runs back to camp.
    static void EnsureTankBearForm(Player* bot);

    // --- Advanced-pull passive management -----------------------------------
    // Followers go passive (attack nothing, just hold at camp) during a pull by
    // having the mod-playerbots "passive" strategy added to their COMBAT engine.
    // ApplyFollowerPassive adds it (and sets the bot's pet passive) once, and
    // records the player in a registry; RemoveFollowerPassive reverses both. Both
    // are idempotent and only touch passive that DC itself applied — a passive a
    // player set manually is left alone. HEALERS are exempt: ApplyFollowerPassive
    // skips them so they keep the camp hold but stay free to heal the tank through
    // the drag-back (the camp-hold action yields the tick for a parked healer so
    // its heals fire). ReapStrandedPassives runs every world
    // tick (from the same PlayerbotScript as ReapOrphanedFollows) and is the
    // SINGLE authoritative teardown: it removes DC passive from any registered
    // player whose leader is no longer in a holding pull phase (pull released /
    // dc off / paused / death / leader gone), which reliably un-passives even a
    // follower that got dragged into combat — its own non-combat engine can't run
    // a teardown while the combat engine is passive-locked. The valve also aborts
    // the pull when a held, passive follower drops below the safety HP floor.
    static void ApplyFollowerPassive(Player* follower);

    static void RemoveFollowerPassive(Player* follower);

    static void ReapStrandedPassives();

};

#endif  // _DC_FOLLOWER_LIFECYCLE_H
