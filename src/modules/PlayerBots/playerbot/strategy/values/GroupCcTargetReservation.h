#pragma once

#include "ObjectGuid.h"

class Player;
class PlayerbotAI;
class Unit;

namespace ai
{
    // Short-lived per-group claim so unmarked CC fallback does not let two bots
    // start crowd control on the same add. Raid icons remain the assignment when
    // set and are never claimed here.
    class GroupCcTargetReservation
    {
    public:
        static bool IsClaimedByOther(Player* bot, ObjectGuid targetGuid);
        static bool IsSkipped(Player* bot, ObjectGuid targetGuid);
        static bool IsOwnedBy(Player* bot, ObjectGuid targetGuid);
        static bool IsInFlight(Player* bot, ObjectGuid targetGuid);
        static ObjectGuid GetOwnedTarget(Player* bot);
        static void Claim(Player* bot, ObjectGuid targetGuid);
        // Grouped unmarked fallback must own a live claim before the real cast.
        // Explicit RTI CC and ungrouped bots bypass fallback claims.
        static bool PrepareFallbackCast(PlayerbotAI* ai, Unit* target);
        static void RecordCast(Player* bot, Unit* target, bool castStarted);
        static void Release(Player* bot, ObjectGuid targetGuid);
    };
}
