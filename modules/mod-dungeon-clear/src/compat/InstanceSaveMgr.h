#ifndef DC_COMPAT_INSTANCESAVEMGR_H
#define DC_COMPAT_INSTANCESAVEMGR_H

// AzerothCore routes player-instance binds through a manager singleton keyed
// by guid and difficulty. This core keeps the binds on the Player (one
// difficulty), so the shim resolves the player and forwards. Offline players
// cannot be reached this way - every caller in the module operates on the
// bots and watchers of a running test, all of them online; a miss is a no-op
// rather than a crash.
#include "Maps/MapPersistentStateMgr.h"
#include "ObjectAccessor.h"
#include "Objects/Player.h"

using InstanceSave = DungeonPersistentState;

struct DcInstanceSaveMgrShim
{
    DcInstanceSaveMgrShim* operator->() { return this; }

    void PlayerUnbindInstance(ObjectGuid const& guid, uint32 mapId, Difficulty /*diff*/,
                              bool /*deleteFromDB*/, Player* /*resolved*/ = nullptr)
    {
        if (Player* p = sObjectAccessor.FindPlayer(guid))
            p->UnbindInstance(mapId);
    }

    InstancePlayerBind* PlayerGetBoundInstance(ObjectGuid const& guid, uint32 mapId, Difficulty /*diff*/)
    {
        Player* p = sObjectAccessor.FindPlayer(guid);
        return p ? p->GetBoundInstance(mapId) : nullptr;
    }

    DungeonPersistentState* PlayerGetInstanceSave(ObjectGuid const& guid, uint32 mapId, Difficulty /*diff*/)
    {
        InstancePlayerBind* bind = PlayerGetBoundInstance(guid, mapId, DUNGEON_DIFFICULTY_NORMAL);
        return bind ? bind->state : nullptr;
    }

    void PlayerBindToInstance(ObjectGuid const& guid, DungeonPersistentState* state,
                              bool permanent, Player* /*resolved*/ = nullptr)
    {
        if (Player* p = sObjectAccessor.FindPlayer(guid))
            if (state)
                p->BindToInstance(state, permanent);
    }

    // Only consulted for a difficulty on this core, where there is one; the
    // caller already falls back to normal when this misses.
    DungeonPersistentState* GetInstanceSave(uint32 /*instanceId*/) { return nullptr; }
};

static DcInstanceSaveMgrShim sInstanceSaveMgrShimInstance;
#ifndef sInstanceSaveMgr
#define sInstanceSaveMgr (&sInstanceSaveMgrShimInstance)
#endif

#endif
