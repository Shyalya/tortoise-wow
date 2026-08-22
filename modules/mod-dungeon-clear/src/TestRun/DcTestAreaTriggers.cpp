/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestAreaTriggers.h"

#include <string>

#include "Group.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "Ai/Dungeon/DungeonClear/Util/DcPlayerbotCompat.h"

void DcTestAreaTriggers::Arm(uint32 mapId)
{
    _volumes.clear();
    _relayed = 0;
    _armed = true;

    // The script store IS the candidate list: a trigger with no
    // `areatrigger_scripts` row has nothing for the relay to accomplish, so
    // walking it instead of every areatrigger in the game keeps this to the few
    // that matter. Both stores are read-only after loading, so one pass here
    // covers the whole run.
    for (auto const& [triggerId, scriptId] : sScriptMgr.GetAllAreaTriggerScripts())
    {
        if (!scriptId)
            continue;

        AreaTrigger const* at = sObjectMgr.GetAreaTrigger(triggerId);
        if (!at || at->map != mapId)
            continue;

        // Never relay a trigger that also teleports. HandleAreaTriggerOpcode
        // runs the script AND the transfer, and a dungeon's exit trigger is
        // exactly such a row — relaying it would teleport the party out of the
        // instance the first time the route brushed the entrance and end the
        // run as a false abort.
        if (sObjectMgr.GetAreaTriggerTeleport(triggerId))
            continue;

        _volumes.push_back(Volume{triggerId, false});
    }

    if (!_volumes.empty())
        LOG_INFO("playerbots.dungeonclear",
                 "TESTRUN areatrigger relay armed on map {}: {} scripted trigger(s)", mapId,
                 _volumes.size());
}

void DcTestAreaTriggers::Disarm()
{
    _volumes.clear();
    _armed = false;
}

bool DcTestAreaTriggers::BotOnly(Group* group)
{
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (DcPlayerbotCompat::IsHumanControlled(member))
                return false;

    return true;
}

void DcTestAreaTriggers::Tick(Player* leader)
{
    if (!_armed || _volumes.empty() || !leader)
        return;

    Group* group = leader->GetGroup();
    if (!group)
        return;

    // Re-checked every tick rather than latched at Arm(): a human can join the
    // party mid-run (and on a roster run an owner logging in takes their
    // character back), and from that moment their client is sending the packets
    // itself. Standing down is the safe side of the race — a missed relay costs
    // one set-piece, a double relay runs every script on the map twice.
    if (!BotOnly(group))
        return;

    for (Volume& volume : _volumes)
    {
        AreaTrigger const* at = sObjectMgr.GetAreaTrigger(volume.entry);
        if (!at)
            continue;

        // Whoever is standing in it — any member, not just the leader, because
        // the scout reaches a volume well before the party does. Members mid-
        // teleport are skipped: DC's own scripted events hop the party across
        // the map (the Underbog two-hop drop is one), and a position read
        // inside that window is the position they left.
        Player* inside = nullptr;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld() || member->IsBeingTeleported())
                continue;
            if (!member->GetSession())
                continue;
            // Same containment test (and 5y slack) the areatrigger opcode
            // handler applies - there is no Player::IsInAreaTriggerRadius here.
            if (IsPointInAreaTriggerZone(at, member->GetMapId(), member->GetPositionX(),
                                         member->GetPositionY(), member->GetPositionZ(), 5.0f))
            {
                inside = member;
                break;
            }
        }

        // Edge, not level: fire on the tick the party first occupies the volume
        // and stay quiet until it has left and come back, which is what a client
        // does.
        if (!inside)
        {
            volume.occupied = false;
            continue;
        }

        if (volume.occupied)
            continue;

        volume.occupied = true;
        ++_relayed;

        // Read the diagnostic off the member BEFORE handing the packet over: an
        // `at_*` script is arbitrary content code and may teleport or remove the
        // very player that tripped it, and a log line is not worth a dangling
        // pointer.
        std::string const who = inside->GetName();
        float const x = inside->GetPositionX();
        float const y = inside->GetPositionY();
        float const z = inside->GetPositionZ();

        // Byte-for-byte what the client sends. Handing it to the real opcode
        // handler rather than calling sScriptMgr->OnAreaTrigger directly is the
        // point: the handler is where the radius re-check, the GM skip, the
        // tavern/battleground/outdoor-pvp branches and the script dispatch all
        // live, so the bot goes through exactly the path a player would and
        // there is no second copy of that logic to drift.
        WorldPacket packet(CMSG_AREATRIGGER, 4);
        packet << uint32(volume.entry);
        packet.rpos(0);
        inside->GetSession()->HandleAreaTriggerOpcode(packet);

        LOG_INFO("playerbots.dungeonclear",
                 "TESTRUN areatrigger {} relayed for {} at ({:.1f}, {:.1f}, {:.1f})", volume.entry,
                 who, x, y, z);
    }
}
