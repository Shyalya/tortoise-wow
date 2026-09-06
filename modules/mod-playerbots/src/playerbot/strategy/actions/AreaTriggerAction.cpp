
#include "playerbot/playerbot.h"
#include "AreaTriggerAction.h"
#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

bool ReachAreaTriggerAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    uint32 triggerId;

    if (ai->IsRealPlayer() || ai->IsAreaTriggerRelaySuppressed()) // Respect Turtle dungeon exit control.
        return false;

    WorldPacket p(event.getPacket());
    p.rpos(0);
    p >> triggerId;

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if(!atEntry)
        return false;

    AreaTrigger const* at = sObjectMgr.GetAreaTrigger(triggerId);
    if (!at)
    {
        WorldPacket p1(CMSG_AREATRIGGER);
        p1 << triggerId;
        p1.rpos(0);
        bot->GetSession()->HandleAreaTriggerOpcode(p1);

        return true;
    }

    if (bot->GetMapId() != atEntry->mapid || bot->GetDistance(atEntry->x, atEntry->y, atEntry->z) > sPlayerbotAIConfig.sightDistance)
    {
        ai->TellError(requester, "I won't follow: too far away");
        return true;
    }

    if (::IsPointInAreaTriggerZone(atEntry, bot->GetMapId(), bot->GetPositionX(),
        bot->GetPositionY(), bot->GetPositionZ(), 0.5f))
    {
        WorldPacket triggerPacket(CMSG_AREATRIGGER);
        triggerPacket << triggerId;
        triggerPacket.rpos(0);
        bot->GetSession()->HandleAreaTriggerOpcode(triggerPacket);
        return true;
    }

    uint32 const generation = ai->GetTransitionGeneration();
    uint32 const mapId = bot->GetMapId();
    uint32 const instanceId = bot->GetInstanceId();

    // Let the core PathFinder approach the official DBC trigger center. A
    // portal center can be behind collision; the core may therefore return an
    // incomplete path ending at the closest reachable navmesh point. That is
    // valid only when the actual endpoint is inside the official trigger
    // volume. Shortcuts and maps without mmaps are never accepted here.
    PathFinder path(bot);
    if (!path.calculate(atEntry->x, atEntry->y, atEntry->z, false, false))
    {
        context->GetValue<LastMovement&>("last area trigger")->Get().clear();
        ai->StopMoving();
        ai->TellError(requester, "I can't safely reach the instance portal");
        return true;
    }

    PathType const pathType = path.getPathType();
    PointsArray portalPath = path.getPath();
    Vector3 const pathEnd = path.getActualEndPosition();
    bool const safePath = !(pathType & (PATHFIND_NOPATH | PATHFIND_SHORTCUT |
        PATHFIND_NOT_USING_PATH)) && portalPath.size() >= 2 &&
        ::IsPointInAreaTriggerZone(atEntry, mapId, pathEnd.x, pathEnd.y, pathEnd.z, 0.5f) &&
        ai->IsTransitionContextCurrent(generation, mapId, instanceId);

    if (!safePath)
    {
        context->GetValue<LastMovement&>("last area trigger")->Get().clear();
        ai->StopMoving();
        ai->TellError(requester, "I can't safely reach the instance portal");
        return true;
    }

    float distance = 0.0f;
    for (size_t i = 1; i < portalPath.size(); ++i)
        distance += (portalPath[i] - portalPath[i - 1]).magnitude();

    MotionMaster& mm = *bot->GetMotionMaster();
    mm.MovePath(portalPath, 0, false, false);
    const float duration = 1000.0f * distance / bot->GetSpeed(MOVE_RUN) + sPlayerbotAIConfig.reactDelay;
    ai->TellError(requester, "Wait for me");
    SetDuration(duration);
    context->GetValue<LastMovement&>("last area trigger")->Get().lastAreaTrigger = triggerId;

    return true;
}



bool AreaTriggerAction::Execute(Event& event)
{
    LastMovement& movement = context->GetValue<LastMovement&>("last area trigger")->Get();

    uint32 triggerId = movement.lastAreaTrigger;
    movement.lastAreaTrigger = 0;

    // Module gate: while an exit-sensitive run owns this bot, a teleport
    // trigger underfoot must NOT be relayed (the consume above still clears
    // it so the trigger cannot fire later either). Non-teleport triggers are
    // not this action's business anyway - it returns before the relay for
    // those below.
    if (ai->IsAreaTriggerRelaySuppressed())
        return false;

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if(!atEntry)
        return false;

    AreaTrigger const* at = sObjectMgr.GetAreaTrigger(triggerId);
    if (!at)
        return true;

    WorldPacket p(CMSG_AREATRIGGER);
    p << triggerId;
    p.rpos(0);
    bot->GetSession()->HandleAreaTriggerOpcode(p);
    return true;
}
