/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARPULLMODECURRENTVALUE_H
#define _PLAYERBOT_DUNGEONCLEARPULLMODECURRENTVALUE_H

#include "Value.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

class PlayerbotAI;

// Order-independent live read of the behavioural `dungeon clear pull mode` bool.
//
// In Dynamic mode (pull setting == 2) the per-pack Leeroy/Advanced verdict is
// computed by DcPullPlanner::UpdateDynamicPullMode, which WRITES the
// `dungeon clear pull mode` bool. That update used to run as a side effect inside
// DungeonClearPullTrigger::IsActive(), and the engage/blocking-trash triggers
// (and the camp gates) then read the freshly-mutated bool LATER in the same
// evaluation pass — correct only because the engine happens to evaluate triggers
// in registration order with the pull trigger first. A side-effecting IsActive()
// that other triggers depend on is brittle: any change to evaluation order,
// caching, or short-circuiting would silently break Dynamic.
//
// This calculated value makes the dependence explicit and order-free: its
// Calculate() runs the verdict update (the single writer for Dynamic) and returns
// the current bool. Every reader consults THIS value, so whichever reader runs
// first refreshes the verdict and all readers in the tick agree — independent of
// trigger registration order. The `dungeon clear pull mode` bool stays the
// behavioural latch (DcPullAction owns it for Off/On; the governor's per-pack
// upgrade-only latch owns it for Dynamic); this value is just the fresh accessor.
//
// checkInterval 1 = recompute on every read. That is cheap: UpdateDynamicPullMode
// is a no-op for Off/On and carries its own ~400ms throttle on the expensive
// pack classification, so repeated reads within a tick only re-run the light
// bookkeeping and return the latched bool.
class DungeonClearPullModeCurrentValue : public CalculatedValue<bool>
{
public:
    DungeonClearPullModeCurrentValue(PlayerbotAI* botAI)
        : CalculatedValue<bool>(botAI, DcKey::PullModeCurrent, 1)
    {
    }

protected:
    bool Calculate() override;
};

#endif
