
#include "playerbot/playerbot.h"
#include "playerbot/BotDiagnostics.h"
#include "MoveToRpgTargetAction.h"
#include "ChooseRpgTargetAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/strategy/values/PossibleRpgTargetsValue.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/TravelMgr.h"
#include "StableTargetPosition.h"

using namespace ai;

bool MoveToRpgTargetAction::Execute(Event& event)
{
    GuidPosition guidP = AI_VALUE(GuidPosition, "rpg target");
    Unit* unit = ai->GetUnit(guidP);
    GameObject* go = ai->GetGameObject(guidP);
    Player* player = guidP.GetPlayer();

    WorldObject* wo;
    if (unit)
    {
        wo = unit;
    }
    else if(go)
        wo = go;
    else
    {
        RESET_AI_VALUE(GuidPosition, "rpg target");
        return false;
    }

    if (guidP.IsPlayer())
    {
        Player* player = guidP.GetPlayer();

        if (player && ai->IsSafe(player) && GetBotAI(player))
        {
            GuidPosition guidPP = PAI_VALUE(GuidPosition, "rpg target");

            if (guidPP.IsPlayer())
            {
                ai::botdiag::TraceBehavior(ai, "rpg_drop", "player target is pursuing another player");
                AI_VALUE(std::set<ObjectGuid>&,"ignore rpg target").insert(AI_VALUE(GuidPosition, "rpg target"));

                RESET_AI_VALUE(GuidPosition, "rpg target");

                if (ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT))
                {
                    ai->TellPlayerNoFacing(GetMaster(), "Rpg player target is targeting me. Drop target");
                }
                return false;
            }
        }
    }

    if (unit && unit->IsMoving() && !urand(0, 20) && guidP.sqDistance2d(bot) < INTERACTION_DISTANCE * INTERACTION_DISTANCE * 2)
    {
        ai::botdiag::TraceBehavior(ai, "rpg_drop", "moving target random abandonment");
        AI_VALUE(std::set<ObjectGuid>&,"ignore rpg target").insert(AI_VALUE(GuidPosition, "rpg target"));

        RESET_AI_VALUE(GuidPosition,"rpg target");

        if (ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT))
        {
            ai->TellPlayerNoFacing(GetMaster(), "Rpg target is moving. Random drop target.");
        }
        return false;
    }

    if (!CanFreeMoveValue::CanFreeMoveTo(ai, wo))
    {
        ai::botdiag::TraceBehavior(ai, "rpg_drop", "outside free movement range");
        AI_VALUE(std::set<ObjectGuid>&, "ignore rpg target").insert(AI_VALUE(GuidPosition, "rpg target"));

        RESET_AI_VALUE(GuidPosition, "rpg target");

        if (ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT))
        {
            ai->TellPlayerNoFacing(GetMaster(), "Rpg target is far from mater. Random drop target.");
        }
        return false;
    }

    if (guidP.distance(bot) > sPlayerbotAIConfig.reactDistance * 2)
    {
        ai::botdiag::TraceBehavior(ai, "rpg_drop", "beyond reaction range");
        AI_VALUE(std::set<ObjectGuid>&, "ignore rpg target").insert(AI_VALUE(GuidPosition, "rpg target"));

        RESET_AI_VALUE(GuidPosition, "rpg target");

        if (ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT))
        {
            ai->TellPlayerNoFacing(GetMaster(), "Rpg target is beyond react distance. Drop target");
        }
        return false;
    }

    if (guidP.IsGameObject() && guidP.sqDistance2d(bot) < INTERACTION_DISTANCE * INTERACTION_DISTANCE && guidP.distance(bot) > INTERACTION_DISTANCE * 1.5 && !urand(0, 5))
    {
        ai::botdiag::TraceBehavior(ai, "rpg_drop", "object height separation");
        AI_VALUE(std::set<ObjectGuid>&, "ignore rpg target").insert(AI_VALUE(GuidPosition, "rpg target"));

        RESET_AI_VALUE(GuidPosition, "rpg target");

        if (ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT))
        {
            ai->TellPlayerNoFacing(GetMaster(), "Under/above object drop rpg target");
        }
        return false;
    }

    if (!urand(0, 50))
    {
        ai::botdiag::TraceBehavior(ai, "rpg_drop", "native random abandonment");
        AI_VALUE(std::set<ObjectGuid>&, "ignore rpg target").insert(AI_VALUE(GuidPosition, "rpg target"));

        RESET_AI_VALUE(GuidPosition, "rpg target");

        if (ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT))
        {
            ai->TellPlayerNoFacing(GetMaster(), "Random drop rpg target");
        }
        return false;
    }

    float x = wo->GetPositionX();
    float y = wo->GetPositionY();
    float z = wo->GetPositionZ();
    float mapId = wo->GetMapId();

    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
    {
        std::string name = chat->formatWorldobject(wo);

        ai->Poi(x, y, name);
    }
	
	if (sPlayerbotAIConfig.RandombotsWalkingRPG)
        if (!bot->GetTerrain()->IsOutdoors(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
            bot->m_movementInfo.AddMovementFlag(MOVEFLAG_WALK_MODE);

    float angle;
    float distance = 1.0f;

    // Static service NPCs and game objects need one stable approach point per
    // bot. Re-rolling the angle whenever movement stopped caused the visible
    // queue-shaped oscillation at flight masters and other town services.
    bool const movingUnit = unit && unit->IsMoving();
    if (!movingUnit)
    {
        StableTargetOffset const offset = GetStableTargetOffset(
            bot->GetGUIDLow(), guidP.GetCounter(), x, y);
        angle = offset.angle;
        distance = offset.scale;
    }
    else if (bot->IsWithinLOS(x, y, z, true))
    {
        if (!unit->HasInArc(bot))
            angle = wo->GetOrientation() + (M_PI * irand(-10, 10) / 100.0); //20 degrees infront of target (leading it's movement)
        else
            angle = wo->GetAngle(bot); //Current approuch angle.

        if (guidP.sqDistance2d(bot) < INTERACTION_DISTANCE * INTERACTION_DISTANCE)
            distance = sqrt(guidP.sqDistance2d(bot)); //Stay at this distance.
        else
            distance = frand(0.5, 1);
    }
    else
        angle = 2 * M_PI * urand(0, 100) / 100.0; //A circle around the target.

    x += cos(angle) * INTERACTION_DISTANCE * distance;
    y += sin(angle) * INTERACTION_DISTANCE * distance;

    WorldPosition movePos(mapId, x, y, z);
    
    if (movePos.distance(bot) < sPlayerbotAIConfig.sightDistance)
    {
        if (!movePos.ClosestCorrectPoint(5.0f, 5.0f, bot->GetInstanceId()) || abs(movePos.getZ()- z) > 10.0f)
        {
            ai->TellDebug(GetMaster(), "Can not path to desired location around " + chat->formatWorldobject(guidP.GetWorldObject(bot->GetInstanceId())) + " trying again later.", "debug move");
            // Reuse native target rejection so the next choice can make progress.
            ai::botdiag::TraceBehavior(ai, "rpg_drop", "no corrected navigation point");
            AI_VALUE(std::set<ObjectGuid>&, "ignore rpg target").insert(guidP);
            RESET_AI_VALUE(GuidPosition, "rpg target");
            return false;
        }
    }

    bool couldMove;

    if (unit && unit->GetTypeId() == TYPEID_UNIT && unit->IsMoving() && bot->GetDistance(unit) < INTERACTION_DISTANCE * 2 && unit->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
    {

        Creature* creature = static_cast<Creature*>(unit);


        if (creature)
            if (uint32 pauseTimer = creature->GetInteractionPauseTimer())
                creature->GetMotionMaster()->PauseWaypoints(pauseTimer);
    }
    // ClosestCorrectPoint modifies movePos; do not discard the navigable point.
    couldMove = MoveTo(movePos.getMapId(), movePos.getX(), movePos.getY(), movePos.getZ(), false, false);
    ai::botdiag::TraceBehavior(ai, "rpg_move", couldMove ? "accepted" : "rejected");

    if (!couldMove && movePos.distance(bot) > INTERACTION_DISTANCE)
    {
        AI_VALUE(std::set<ObjectGuid>&,"ignore rpg target").insert(AI_VALUE(GuidPosition, "rpg target"));

        RESET_AI_VALUE(GuidPosition, "rpg target");

        if (ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT))
        {
            ai->TellPlayerNoFacing(GetMaster(), "Could not move to rpg target. Drop rpg target");
        }

        return false;
    }

    if ((ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT)) && guidP.GetWorldObject(bot->GetInstanceId()))
    {
        if (couldMove)
        {
            std::ostringstream out;
            out << "Heading to: ";
            out << chat->formatWorldobject(guidP.GetWorldObject(bot->GetInstanceId()));
            ai->TellPlayerNoFacing(GetMaster(), out);
        }
        else
        {
            std::ostringstream out;
            out << "Near: ";
            out << chat->formatWorldobject(guidP.GetWorldObject(bot->GetInstanceId()));
            ai->TellPlayerNoFacing(GetMaster(), out);
        }
    }

    return couldMove;
}

bool MoveToRpgTargetAction::isUseful()
{
    GuidPosition guidP = AI_VALUE(GuidPosition, "rpg target");
    WorldPosition oldPosition = guidP;

    if (!guidP)
        return false;

    WorldObject* wo = guidP.GetWorldObject(bot->GetInstanceId());

    if (!wo)
    {
        RESET_AI_VALUE(GuidPosition, "rpg target");

        if (ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT))
        {
            ai->TellPlayerNoFacing(GetMaster(), "Target could not be found. Drop rpg target");
        }
        return false;
    }

    if(MEM_AI_VALUE(WorldPosition, "current position")->LastChangeDelay() < 60)
        if (bot->IsMoving() && bot->GetMotionMaster() && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
            return false;

    if (AI_VALUE(bool, "travel target traveling"))
        return false;

    if (AI_VALUE2(float, "distance", "rpg target") < INTERACTION_DISTANCE)
        return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    if (AI_VALUE(bool, "has available loot"))
    {
        LootObject lootObject = AI_VALUE(LootObjectStack*, "available loot")->GetLoot(sPlayerbotAIConfig.lootDistance);
        if (lootObject.IsLootPossible(bot))
            return false;
    }

    return true;
}


