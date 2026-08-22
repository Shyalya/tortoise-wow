/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTAREATRIGGERS_H
#define _PLAYERBOT_DCTESTAREATRIGGERS_H

#include <vector>

// `uint32` is AzerothCore's own typedef, not the <cstdint> one — Define.h is
// what declares it. (<cstdint> gives uint32_t, which is a different name.)
#include "Define.h"

class Group;
class Player;

// Areatrigger relay for a headless test run.
//
// An areatrigger is a CLIENT-side volume. Nothing server-side notices a unit
// entering one: the ONLY thing that ever runs an `at_*` script is
// WorldSession::HandleAreaTriggerOpcode, fed by the CMSG_AREATRIGGER a game
// client sends when its own player walks in. Stock playerbots does not close
// that gap either — PlayerbotAI registers CMSG_AREATRIGGER on
// `masterIncomingPacketHandlers`, so a bot only ever relays a HUMAN MASTER's
// packet, and the position-based WithinAreaTrigger returns false for anything
// that is not an `areatrigger_teleport`.
//
// A `.dc test` party is five bots with no game client behind any of them, so in
// a test run scripted areatriggers never fire AT ALL. That is not cosmetic —
// those scripts are boss set-pieces. The Underbog's Ghaz'an (18105) is the
// clean example: `at_underbog_ghazan` (trigger 4302) is the only thing in the
// entire core that calls his ACTION_MOVE_TO_PLATFORM. Without it he keeps
// swimming his DB patrol path (1383920 — every node between z 32 and z 47,
// i.e. underwater) for the whole run instead of climbing the ramp onto his
// platform (path 1383921, ending at z 81.4). The party then has to fight a boss
// the encounter never put where it belongs, and every downstream symptom (the
// 150yd force-aggro and summon-if-stuck rows this module carries for him in
// BossPullbackRegistry) is compensation for that one missing packet.
//
// So the run sends the packet itself, on behalf of the member standing in the
// volume, at the moment a client would have sent it.
//
// SCOPE — deliberately the narrowest set that fixes the gap:
//
//   * test runs only. This is a harness component, wired from DcTestRunJob's
//     Monitoring stage and nowhere else. A normal party is not touched.
//   * bot-only groups only. If ANY member is human-controlled (a real player,
//     or a self-bot — see DcPlayerbotCompat::IsHumanControlled) their client is
//     already sending CMSG_AREATRIGGER, and relaying on top of it would fire
//     every script twice. Checked every tick, not once, because a human can
//     join a group mid-run.
//   * SCRIPTED, NON-TELEPORT triggers only. A trigger with no
//     `areatrigger_scripts` row has nothing to run, and one with an
//     `areatrigger_teleport` row would move the party — the instance exit is an
//     areatrigger, and relaying it would end the run by walking past it. Both
//     are filtered out at Arm() so they can never be reached.
//
//     The teleport filter is the DB one, and it is worth being honest about its
//     limit: a SCRIPT can teleport without an `areatrigger_teleport` row
//     (at_frozen_throne_teleport is one). No such trigger exists on any map in
//     DcTestDungeonRegistry — as of writing the registry's 51 maps carry 37
//     relayable triggers across 15 of them, all boss/RP set-pieces (the
//     Underbog's 4302, Nethekurse's RP, the Shattered Halls execution, BRD's
//     Ring of Law, Quagmirran's lair, HoL's Hall of Watchers, HoR, Tyrannus) —
//     but a new registry entry is the thing that could bring one in, so check
//     the map's `areatrigger_scripts` rows when adding one.
//
// Relaying is what a real party already does, so these set-pieces firing is the
// harness getting MORE faithful, not less. It is still a behaviour change on
// those 15 maps: baselines taken before it are not comparable with ones after.
// (Not every one is a set-piece — Deadmines 3746 and Wailing Caverns 3766 only
// respawn a Mysterious Chest.)
//
// OVERLAPS WITH EXISTING DC HOOKS — three triggers already had a bespoke
// bot-side workaround in ObjectiveHookRegistry, written before this relay
// existed. All three are safe to fire twice, but know they are there:
//
//   * BRD 1526 `at_ring_of_law` — EnsureRingStarted (hook 1) forges the SAME
//     packet. The core script self-guards (returns false when the encounter is
//     IN_PROGRESS/DONE, and honours the 2-minute post-wipe cooldown), so the
//     duplicate is a no-op.
//   * Shattered Halls 4347 `at_rp_nethekurse` — StartNethekurseIntro (hook 9)
//     calls DoAction(ACTION_START_INTRO) directly, gated on IsImmuneToPC(), and
//     the boss carries its own `_introStarted` guard.
//   * ZulFarrak 962 — WakeZumrah (hook 5) sets faction 37 directly *because a
//     forged packet did not work there in a live run* and nobody could tell
//     which stage swallowed it. Gated on the faction still being 35.
//
// What the relay changes for those three is TIMING, not outcome: a hook fires
// when the objective step runs, the relay fires the moment any member —
// including a scout well ahead of the party — enters the volume. That is what a
// real client does, but it can start a set-piece earlier in the approach than
// the hook did. Worth one confirmation run each rather than assuming.
//
// The Zum'rah case is also the one free diagnostic here: the relay logs every
// packet it sends, so a ZF run now says whether a forged CMSG_AREATRIGGER
// drives a SmartAI ON_TRIGGER row at all — the question the earlier attempt
// could not answer. If it does, hook 5 can be retired.
//
// Edge-triggered, exactly like a client: the packet goes out on the tick a
// member first appears inside a volume and not again until the party has
// vacated it. That keeps a repeatable script repeatable while a member loitering
// in the volume cannot spam it, and the one-shot scripts
// (OnlyOnceAreaTriggerScript) latch themselves in the core regardless.
class DcTestAreaTriggers
{
public:
    // Collect the relayable triggers on `mapId` and start relaying. Called once
    // per run, when the run enters Monitoring — the map is fixed for a run's
    // lifetime, and the areatrigger stores only change on `.reload`.
    void Arm(uint32 mapId);

    // Stop relaying and drop the candidate list.
    void Disarm();

    // One world tick. `leader` is the run's tank, used only to reach the group;
    // the relay itself is per-member, since any member may be the one who walks
    // in first. Cheap enough to run unthrottled (a dungeon has a handful of
    // relayable triggers at most, usually one or none) — and it must be, since
    // a 1s sample can carry a running bot clean through a small trigger box.
    void Tick(Player* leader);

    // Teardown log line. Relayed() counts PACKETS, not distinct volumes — a
    // route that leaves and re-enters one legitimately fires it twice — so it
    // is reported against Armed(), the number of relayable volumes the map had.
    // Both matter: "0 of 1" is a party that never reached the volume, which is
    // a different failure from a script that fired and did nothing.
    uint32 Relayed() const { return _relayed; }
    uint32 Armed() const { return static_cast<uint32>(_volumes.size()); }

private:
    // One relayable trigger and whether the party was standing in it last tick.
    struct Volume
    {
        uint32 entry = 0;
        bool occupied = false;
    };

    // Is every member of `group` bot-driven? A human-controlled member's own
    // client sends the packet, so the relay must stand down for the whole group
    // the moment one is present.
    static bool BotOnly(Group* group);

    std::vector<Volume> _volumes;
    uint32 _relayed = 0;
    bool _armed = false;
};

#endif
