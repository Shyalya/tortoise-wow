/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_STATUS_PUBLISHER_H
#define _DC_STATUS_PUBLISHER_H

#include <functional>
#include <string>
#include "Define.h"
#include "ObjectGuid.h"

class PlayerbotAI;

class DcStatusPublisher
{
public:
    // Send a structured addon message with prefix "DC" to all real players in the bot's group.
    static void SendAddonMessage(PlayerbotAI* botAI, std::string const& msg);

    // --- Event-driven status pushes -----------------------------------------
    // The companion addon used to poll `CMD\tstatus` every 2s. Instead the
    // server now recomputes status cheaply each world tick for the handful of
    // tanks actually running a clear and pushes a STATUS packet only when the
    // meaningful state changes (entered combat, pulled a boss, a boss died,
    // stalled, started looting, party recovered, …). BuildStatusPayload
    // produces the same "STATUS\t..." string DcStatusAction sends; it is shared
    // by the on-demand `dc status` action and the change-detector so the two
    // can never drift. MarkActiveTank / UnmarkActiveTank maintain the small
    // registry of clearing tanks (mirrors the follow-reaper pattern below);
    // TickStatusPushes is the throttled detector driven from the world tick.
    static std::string BuildStatusPayload(PlayerbotAI* botAI);

    // Unconditionally send the current STATUS payload and refresh the
    // change-detector's snapshot for this bot, so an explicit request never
    // provokes a duplicate push on the following tick.
    static void PushStatus(PlayerbotAI* botAI);

    static void MarkActiveTank(ObjectGuid tank);

    static void UnmarkActiveTank(ObjectGuid tank);

    static void TickStatusPushes(uint32 diff);

    // Optional server-side observer for changed STATUS payloads. Addon
    // messages only reach real players in the bot's GROUP; the `.dc test`
    // harness monitors a party it is not in, so the change-detector hands it
    // each changed frame here as well. Injected as a callback (registered
    // once at module startup) so this Util unit never includes TestRun code.
    using StatusObserver = std::function<void(ObjectGuid, std::string const&)>;
    static void SetStatusObserver(StatusObserver observer);
};

#endif  // _DC_STATUS_PUBLISHER_H
