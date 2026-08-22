/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCWATCHHOP_H
#define _PLAYERBOT_DCWATCHHOP_H

#include <cstdint>
#include <vector>

// Pure decision core for hopping a `.dc test watch` watcher from wherever they
// are into the instance a test run is using. Engine-free so the instance-bind
// bookkeeping is unit-testable — the live consequences (a GM stuck watching a
// FINISHED run) are expensive to reproduce in-game.
//
// The problem it solves. Entering a run's instance means binding the watcher to
// that instance save, exactly the way `.appear` does. That bind then OUTLIVES
// the run: the core resolves a teleport's destination copy from the player's
// bind (InstanceSaveMgr::PlayerGetDestinationInstanceId), so the next
// `.dc test watch` of the same map silently drops the GM into the dead copy of
// the previous run instead of the live one. Worse, when the watcher is already
// standing in that map, Player::TeleportTo takes its NEAR-teleport branch
// (`GetMapId() == mapid`) and just slides the body around inside the stale
// copy — no map change at all.
//
// So a hop needs three answers, and this computes all three:
//   1. which binds to release first (the stale ones, ours and the core's),
//   2. whether a bind to the target copy still has to be made,
//   3. whether the teleport must be FORCED far (Player::TeleportTo's
//      `newInstance` flag) because the map id is unchanged.
//
// Only binds this module made are tracked in `held` — releasing a lockout the
// human earned themselves is never this command's business.

namespace DcWatchHop
{
    // A player's current standing, or a run's instance.
    struct Where
    {
        uint32_t mapId = 0;
        uint32_t instanceId = 0;    // 0 = not an instanced map
    };

    // One instance bind, keyed the way InstanceSaveMgr wants it back.
    struct Bind
    {
        uint32_t mapId = 0;
        uint8_t  difficulty = 0;
        uint32_t instanceId = 0;
    };

    struct Plan
    {
        // The watcher is already standing in the target copy — take the seat,
        // touch nothing else.
        bool alreadyThere = false;

        // Bind the watcher to the target copy before teleporting.
        bool bindToTarget = false;

        // Pass newInstance=true to Player::TeleportTo: same map id, different
        // copy, so the near-teleport branch would never leave the old one.
        bool forceNewInstance = false;

        // Binds to drop first, in the order given.
        std::vector<Bind> release;
    };

    // `viewer`  — where the watcher stands right now.
    // `target`  — the run's map / difficulty / instance. instanceId 0 means the
    //             destination isn't instanced, so no bind work is possible.
    // `boundInstanceId` — what the core says the watcher is bound to on
    //             target.mapId at target.difficulty right now (0 = unbound).
    //             May name a copy this module never bound them to.
    // `held`    — every bind this module made for this watcher and has not
    //             released yet.
    Plan Decide(Where viewer, Bind target, uint32_t boundInstanceId,
                std::vector<Bind> const& held);
}

#endif  // _PLAYERBOT_DCWATCHHOP_H
