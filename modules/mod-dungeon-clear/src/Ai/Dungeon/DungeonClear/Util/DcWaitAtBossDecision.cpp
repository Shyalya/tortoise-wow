/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcWaitAtBossDecision.h"

namespace DcWaitAtBossDecision
{
    Result Decide(Inputs const& in)
    {
        Result r;
        r.shouldAutoPause = in.enabled &&
                            in.nextIsBoss &&
                            !in.paused &&
                            !in.inCombat &&
                            in.bossGuid != 0 &&
                            in.bossGuid != in.lastWaitedGuid;
        return r;
    }
}
