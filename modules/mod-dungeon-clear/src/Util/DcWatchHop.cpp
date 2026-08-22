/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Util/DcWatchHop.h"

namespace
{
    bool SameBind(DcWatchHop::Bind const& a, DcWatchHop::Bind const& b)
    {
        return a.mapId == b.mapId && a.difficulty == b.difficulty &&
               a.instanceId == b.instanceId;
    }

    bool Contains(std::vector<DcWatchHop::Bind> const& v, DcWatchHop::Bind const& b)
    {
        for (DcWatchHop::Bind const& e : v)
            if (SameBind(e, b))
                return true;
        return false;
    }
}

namespace DcWatchHop
{
    Plan Decide(Where viewer, Bind target, uint32_t boundInstanceId,
                std::vector<Bind> const& held)
    {
        Plan plan;

        // Nothing to plan for a non-instanced destination: no copy to pick, so
        // no bind can decide where the teleport lands.
        if (!target.instanceId)
        {
            plan.alreadyThere = viewer.mapId == target.mapId;
            return plan;
        }

        // Standing in the target copy already. Deliberately NOT short-circuited
        // by map id alone — same map, different copy is the case this whole
        // unit exists for.
        if (viewer.mapId == target.mapId && viewer.instanceId == target.instanceId)
        {
            plan.alreadyThere = true;
            return plan;
        }

        // A bind on the target map pointing at some OTHER copy is the thing
        // that would hijack the teleport, whether or not we made it. It has to
        // go first — PlayerBindToInstance is a no-op while a bind is present.
        if (boundInstanceId && boundInstanceId != target.instanceId)
            plan.release.push_back({target.mapId, target.difficulty, boundInstanceId});

        // Everything else we bound this watcher to on the way through earlier
        // runs. Left alone these accumulate silently, hold finished copies open
        // and — on a heroic map — burn its daily lockout on a run that is over.
        for (Bind const& b : held)
        {
            if (b.instanceId == target.instanceId)
                continue;   // the copy we're heading into; keep it
            if (!Contains(plan.release, b))
                plan.release.push_back(b);
        }

        // Bind unless the core already has us on exactly the right copy.
        plan.bindToTarget = boundInstanceId != target.instanceId;

        // Same map id: Player::TeleportTo would take its near-teleport branch
        // and never leave the copy we're in.
        plan.forceNewInstance = viewer.mapId == target.mapId;

        return plan;
    }
}
