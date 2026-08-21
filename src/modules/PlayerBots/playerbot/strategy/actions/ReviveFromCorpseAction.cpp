
#include "playerbot/playerbot.h"
#include "ReviveFromCorpseAction.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/FleeManager.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/DeadValues.h"

using namespace ai;

bool ReviveFromCorpseAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    Player* master = ai->GetGroupMaster();
    Corpse* corpse = bot->GetCorpse();

    // follow master when master revives
    WorldPacket& p = event.getPacket();
    if (!p.empty() && p.GetOpcode() == CMSG_RECLAIM_CORPSE && master && !corpse && sServerFacade.IsAlive(bot))
    {
        if (sServerFacade.IsDistanceLessThan(AI_VALUE2(float, "distance", "master target"), sPlayerbotAIConfig.farDistance))
        {
            std::string defaultMovementStrategy = ai->GetDefaultMovementStrategy();

            if (!ai->HasStrategy(defaultMovementStrategy, BotState::BOT_STATE_NON_COMBAT))
            {
                ai->TellPlayerNoFacing(requester, "Welcome back!", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                ai->ChangeStrategy("+" + defaultMovementStrategy + ",-stay", BotState::BOT_STATE_NON_COMBAT);
                return true;
            }
        }
    }

    if (!corpse)
        return false;

    if (corpse->GetGhostTime() + bot->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP) > time(nullptr))
    {
        int64 wait = corpse->GetGhostTime() + bot->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP) - time(nullptr);
        sLog.outDetail("[BOT CORPSE] %s: revive from corpse - BLOCKED: reclaim delay not elapsed (%llds left)", bot->GetName(), (long long)wait);
        return false;
    }

    if (master)
    {
        //Revive with master.
        if (bot != master && sServerFacade.UnitIsDead(master) && master->GetCorpse() && sServerFacade.IsDistanceLessThan(AI_VALUE2(float, "distance", "master target"), sPlayerbotAIConfig.farDistance))
        {
            sLog.outDetail("[BOT CORPSE] %s: revive from corpse - BLOCKED: master is dead & nearby, waiting to revive together", bot->GetName());
            return false;
        }
    }

    sLog.outDetail("[BOT CORPSE] %s: revive from corpse - RECLAIMING corpse now", bot->GetName());
    sLog.outDetail("Bot #%d %s:%d <%s> revives at body", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());

    ai->StopMoving();
    WorldPacket packet(CMSG_RECLAIM_CORPSE);
    packet << bot->GetObjectGuid();
    bot->GetSession()->HandleReclaimCorpseOpcode(packet);

    SET_AI_VALUE(bool, "corpse run", false);
    // Deliberately NOT resetting "death count" here. BestGraveyardValue only
    // switches to a graveyard outside the current zone once the count reaches
    // DEATH_COUNT_BEFORE_TRYING_ANOTHER_GRAVEYARD - but a bot resurrecting is
    // exactly the moment the count has to survive. Resetting here pinned it at
    // 1 forever, so the escape hatch was unreachable and bots that died inside
    // an enemy town (Razor Hill, Aerie Peak) resurrected into the same guards
    // indefinitely. The count is still cleared by XpGainAction, i.e. once the
    // bot is alive and earning again.
    sPlayerbotAIConfig.logEvent(ai, "ReviveFromCorpseAction");

    return true;
}

bool FindCorpseAction::Execute(Event& event)
{
    if (bot->InBattleGround())
        return false;

    Corpse* corpse = bot->GetCorpse();
    if (!corpse)
    {
        sLog.outDetail("[BOT CORPSE] %s: find corpse - no corpse, abort", bot->GetName());
        return false;
    }

    // Manual override: the master commanded "corpse run", so ignore the wait-for-master gate
    // below and run to the corpse regardless of master proximity. Useful when the master cannot
    // resurrect the bot (no res spell / too low level) and would otherwise leave it waiting.
    bool manualCorpseRun = AI_VALUE(bool, "corpse run");

    Player* master = ai->GetGroupMaster();
    if (master && !manualCorpseRun)
    {
        float masterTargetDist = AI_VALUE2(float, "distance", "master target");
        if (!GetBotAI(master) && sServerFacade.IsDistanceLessThan(masterTargetDist, sPlayerbotAIConfig.farDistance))
        {
            sLog.outDetail("[BOT CORPSE] %s: find corpse - BLOCKED: real-player master within farDistance (dist=%.1f < %.1f). Waiting for master to resurrect. Say 'corpse run' to override.",
                bot->GetName(), masterTargetDist, sPlayerbotAIConfig.farDistance);
            return false;
        }
    }

    WorldPosition botPos(bot), corpsePos(corpse), moveToPos = corpsePos, masterPos(master);
    float reclaimDist = CORPSE_RECLAIM_RADIUS - 5.0f;
    float corpseDist = botPos.distance(corpsePos);

    //If player fell through terrain move corpse to player position.
    if (IsRealPlayer(bot) && botPos.getMapId() == moveToPos.getMapId())
    {
        //Try to correct the position upward.
        if (!moveToPos.ClosestCorrectPoint(5.0f, 500.0f, bot->GetInstanceId()))
        {
            //Revive in place.
            corpse->Relocate(botPos.getX(), botPos.getY(), botPos.getZ());
            corpsePos = corpse;
            corpseDist = botPos.distance(corpsePos);
        }
        else
        {
            corpse->Relocate(moveToPos.getX(), moveToPos.getY(), moveToPos.getZ());
            corpsePos = corpse;
            corpseDist = botPos.distance(corpsePos);
        }
    }

    int64 deadTime = time(nullptr) - corpse->GetGhostTime();

    bool moveToMaster = master && master != bot && masterPos.fDist(corpsePos) < reclaimDist;

    sLog.outDetail("[BOT CORPSE] %s: find corpse - corpseDist=%.1f reclaimDist=%.1f reactDist=%.1f moveToMaster=%d deadTime=%llds",
        bot->GetName(), corpseDist, reclaimDist, sPlayerbotAIConfig.reactDistance, moveToMaster ? 1 : 0, (long long)deadTime);

    //Should we ressurect? If so, return false.
    if (corpseDist < reclaimDist)
    {
        if (moveToMaster) //We are near master.
        {
            if (botPos.fDist(masterPos) < sPlayerbotAIConfig.spellDistance)
            {
                sLog.outDetail("[BOT CORPSE] %s: find corpse - within reclaimDist & near master, yielding to revive-from-corpse", bot->GetName());
                return false;
            }
        }
        else if (deadTime > 8 * MINUTE) //We have walked too long already.
        {
            sLog.outDetail("[BOT CORPSE] %s: find corpse - within reclaimDist & deadTime>8min, yielding to revive-from-corpse", bot->GetName());
            return false;
        }
        else
        {
            std::list<ObjectGuid> units = AI_VALUE(std::list<ObjectGuid>, "possible targets no los");

            if (botPos.getUnitsAggro(units, bot) == 0) //There are no mobs near.
            {
                sLog.outDetail("[BOT CORPSE] %s: find corpse - within reclaimDist & no mobs near, yielding to revive-from-corpse", bot->GetName());
                return false;
            }
        }
    }

    //If we are getting close move to a save ressurrection spot instead of just the corpse.
    if (corpseDist < sPlayerbotAIConfig.reactDistance)
    {
        if (moveToMaster)
        {
            if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "Moving to revive near master.";
                ai->TellPlayerNoFacing(GetMaster(), out);
            }
            moveToPos = masterPos;
        }
        else
        {
            FleeManager manager(bot, reclaimDist, 0.0, urand(0, 1), moveToPos);

            if (manager.isUseful())
            {
                float rx, ry, rz;
                if (manager.CalculateDestination(&rx, &ry, &rz))
                {
                    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "Moving to revive some where safe.";
                        ai->TellPlayerNoFacing(GetMaster(), out);
                    }
                    moveToPos = WorldPosition(moveToPos.getMapId(), rx, ry, rz, 0.0);
                }
                else if (!moveToPos.GetReachableRandomPointOnGround(bot, reclaimDist, urand(0, 1)))
                {
                    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "Moving to revive at corpse.";
                        ai->TellPlayerNoFacing(GetMaster(), out);
                    }
                    moveToPos = corpsePos;
                }
            }
        }
    }
    else
    {
        if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "Moving towards corpse.";
            ai->TellPlayerNoFacing(GetMaster(), out);
        }
    }

    //Actual mobing part.
    bool moved = false;

    if (!ai->AllowActivity(DETAILED_MOVE_ACTIVITY) && !ai->HasPlayerNearby(moveToPos))
    {
        uint32 delay = sServerFacade.GetDistance2d(bot, corpse) / bot->GetSpeed(MOVE_RUN); //Time a bot would take to travel to it's corpse.
        delay = std::min(delay, uint32(10 * MINUTE)); //Cap time to get to corpse at 10 minutes.

        if (deadTime > delay)
        {
            sLog.outDetail("[BOT CORPSE] %s: find corpse - no detailed-move activity, teleporting to corpse (deadTime=%llds > delay=%us)",
                bot->GetName(), (long long)deadTime, delay);
            bot->GetMotionMaster()->Clear();
            bot->TeleportTo(moveToPos.getMapId(), moveToPos.getX(), moveToPos.getY(), moveToPos.getZ(), 0);
            if (IsRealPlayer(bot))
                bot->SendHeartBeat();
        }
        else
        {
            sLog.outDetail("[BOT CORPSE] %s: find corpse - no detailed-move activity, waiting out teleport delay (deadTime=%llds < delay=%us)",
                bot->GetName(), (long long)deadTime, delay);
        }

        moved = true;
    }
    else
    {
#ifndef MANGOSBOT_ZERO
        if (bot->IsMovingIgnoreFlying())
            moved = true;
#else
        if (bot->IsMoving())
            moved = true;
#endif
        if (moved)
        {
            sLog.outDetail("[BOT CORPSE] %s: find corpse - already moving towards corpse", bot->GetName());
        }
        else
        {

            moved = MoveTo(moveToPos.getMapId(), moveToPos.getX(), moveToPos.getY(), moveToPos.getZ(), false, false);
            sLog.outDetail("[BOT CORPSE] %s: find corpse - MoveTo(%.1f,%.1f,%.1f) returned %s",
                bot->GetName(), moveToPos.getX(), moveToPos.getY(), moveToPos.getZ(), moved ? "true" : "false");

            if (!moved && !ai->HasActivePlayerMaster()) //We could not move to coprse. Try spirithealer instead.
            {
                sLog.outDetail("[BOT CORPSE] %s: find corpse - MoveTo failed & no active player master, trying spirit healer", bot->GetName());
                moved = ai->DoSpecificAction("spirit healer", Event(), true);
            }
            else if (!moved)
            {
                sLog.outDetail("[BOT CORPSE] %s: find corpse - MoveTo failed but has active player master, NOT using spirit healer -> FAILED loop", bot->GetName());
            }
        }
    }

    return moved;
}

bool FindCorpseAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    return bot->GetCorpse();
}

bool SpiritHealerAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    Corpse* corpse = bot->GetCorpse();
    if (!corpse)
    {
        ai->TellPlayerNoFacing(requester, "I am not a spirit");
        return false;
    }

    uint32 dCount = AI_VALUE(uint32, "death count");
    GuidPosition grave = AI_VALUE(GuidPosition, "best graveyard");

    //something went wrong
    if (!grave)
    {
        //prevent doing weird stuff OR GOING TO 0,0,0
        sLog.outDetail(
            "ERROR: no graveyard in SpiritHealerAction for bot #%d %s:%d <%s>, evacuating to prevent weird behavior",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
        ai->DoSpecificAction("repop");
        return false;
    }

    if (grave && grave.fDist(bot) < sPlayerbotAIConfig.sightDistance)
    {
        bool foundSpiritHealer = false;
        std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
        for (std::list<ObjectGuid>::iterator i = npcs.begin(); i != npcs.end(); i++)
        {
            Unit* unit = ai->GetUnit(*i);
            if (unit && unit->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER))
            {
                foundSpiritHealer = true;
                break;
            }
        }

        if (!foundSpiritHealer)
        {
            sLog.outDetail("Bot #%d %s:%d <%s> can't find a spirit healer", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
            ai->TellPlayerNoFacing(requester, "Cannot find any spirit healer nearby");
        }


        sLog.outDetail("Bot #%d %s:%d <%s> revives at spirit healer", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
        PlayerbotChatHandler ch(bot);
        bot->ResurrectPlayer(0.5f, !ai->HasCheat(BotCheatMask::repair));
        bot->DurabilityLossAll(0.25f, true);

        bot->SpawnCorpseBones();
        bot->SaveToDB();
        SET_AI_VALUE(bool, "corpse run", false);
        // Deliberately NOT resetting "death count" here. BestGraveyardValue only
        // switches to a graveyard outside the current zone once the count reaches
        // DEATH_COUNT_BEFORE_TRYING_ANOTHER_GRAVEYARD - but a bot resurrecting is
        // exactly the moment the count has to survive. Resetting here pinned it at
        // 1 forever, so the escape hatch was unreachable and bots that died inside
        // an enemy town (Razor Hill, Aerie Peak) resurrected into the same guards
        // indefinitely. The count is still cleared by XpGainAction, i.e. once the
        // bot is alive and earning again.
        context->GetValue<Unit*>("current target")->Set(nullptr);
        bot->SetSelectionGuid(ObjectGuid());
        ai->TellPlayer(requester, BOT_TEXT("hello"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        sPlayerbotAIConfig.logEvent(ai, "ReviveFromSpiritHealerAction");

        return true;
    }

    bool shouldTeleportToGY = false;

    const int64 deadTime = time(nullptr) - corpse->GetGhostTime();

    // Prevent taking too long to go to corpse (10 mins)
    // no need to wait longer, because bot is probably stuck in navigating issues
    shouldTeleportToGY = deadTime > uint32(10 * MINUTE);

    // Check if we can teleport to the graveyard when nobody is looking
    if (!shouldTeleportToGY && !ai->AllowActivity(DETAILED_MOVE_ACTIVITY) && !ai->HasPlayerNearby(WorldPosition(grave)))
    {
        //Time a bot would take to travel to it's corpse.
        uint32 delay = sServerFacade.GetDistance2d(bot, corpse) / bot->GetSpeed(MOVE_RUN);
        //Cap time to get to corpse at 10 minutes.
        delay = std::min(delay, uint32(10 * MINUTE));

        shouldTeleportToGY = deadTime > delay;
    }

    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "Moving towards graveyard.";
        ai->TellPlayerNoFacing(GetMaster(), out);
    }

    if (shouldTeleportToGY)
    {
        bot->GetMotionMaster()->Clear();
        bot->TeleportTo(grave.getMapId(), grave.getX(), grave.getY(), grave.getZ(), 0);
        if (IsRealPlayer(bot))
            bot->SendHeartBeat();
        return true;
    }
    else
    {
        return MoveTo(grave.getMapId(), grave.getX(), grave.getY(), grave.getZ(), false, false);
    }
}

bool SpiritHealerAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    return bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST);
}
