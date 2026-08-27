#pragma once
#include "GenericSpellActions.h"
#include "ReachTargetActions.h"
#include "ChooseTargetActions.h"
#include "MovementActions.h"
#include "RemoveAuraAction.h"

namespace ai
{
    class MeleeAction : public AttackAction 
    {
    public:
        MeleeAction(PlayerbotAI* ai) : AttackAction(ai, "melee") {}
        virtual std::string GetTargetName() override { return "current target"; }
        virtual bool isUseful() override;
    };

    class UseLightwellAction : public MovementAction
    {
    public:
        UseLightwellAction(PlayerbotAI* ai) : MovementAction(ai, "lightwell") {}
        ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_NONE; }

        bool isUseful() override
        {
            if (MovementAction::isUseful())
            {
                return (bot->getClass() == CLASS_PRIEST || bot->GetGroup()) && bot->GetHealthPercent() < sPlayerbotAIConfig.mediumHealth && !ai->HasAura("lightwell renew", bot);
            }

            return false;
        }

        bool isPossible() override
        {
            std::list<ObjectGuid> closeObjects = AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");
            if (closeObjects.empty())
                return false;

            for (std::list<ObjectGuid>::iterator i = closeObjects.begin(); i != closeObjects.end(); ++i)
            {
                GameObject* go = ai->GetGameObject(*i);
                if (!go)
                    continue;

                if (!(go->GetEntry() == 181106 || go->GetEntry() == 181165 || go->GetEntry() == 181102 || go->GetEntry() == 181105))
                    continue;

                if (!sServerFacade.isSpawned(go) || go->GetGoState() != GO_STATE_READY || !bot->CanInteract(go))
                    continue;

                if (Unit* owner = go->GetOwner())
                {
                    if (owner->GetTypeId() == TYPEID_PLAYER)
                    {
                        Player* ownerPlayer = (Player*)owner;
                        if (!ownerPlayer)
                            return false;

                        if (!ownerPlayer->IsInGroup(bot))
                            continue;
                    }
                }

                lightwellGameObject = *i;
                return MovementAction::isPossible();
            }

            return false;
        }
        
        virtual bool Execute(Event& event) override
        {
            GameObject* go = ai->GetGameObject(lightwellGameObject);
            if (go)
            {
                if (bot->IsWithinDistInMap(go, INTERACTION_DISTANCE, false))
                {
                    WorldPacket data(CMSG_GAMEOBJ_USE);
                    data << go->GetObjectGuid();
                    bot->GetSession()->HandleGameObjectUseOpcode(data);
                    return true;
                }
                else
                {
                    return MoveNear(go, 4.0f);
                }
            }

            return false;
        }

    private:
        ObjectGuid lightwellGameObject;
    };

    class AssistSummoningRitualAction : public MovementAction
    {
    public:
        AssistSummoningRitualAction(PlayerbotAI* ai) : MovementAction(ai, "assist summoning ritual") {}
        ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_NONE; }

        bool isUseful() override
        {
            return FindNearbySummoningRitual(ai);
        }

        bool Execute(Event& event) override
        {
            GameObject* go = FindNearbySummoningRitual(ai);
            if (!go)
                return false;

            if (!bot->IsWithinDistInMap(go, INTERACTION_DISTANCE, false))
                return MoveNear(go, INTERACTION_DISTANCE - 1.0f);

            if (!bot->CanInteract(go))
                return false;

            WorldPacket data(CMSG_GAMEOBJ_USE);
            data << go->GetObjectGuid();
            bot->GetSession()->HandleGameObjectUseOpcode(data);
            SetDuration(sPlayerbotAIConfig.globalCoolDown);
            return true;
        }

    private:
        static float GetMaxAssistRange() { return INTERACTION_DISTANCE * 3.0f; }

        static GameObject* FindNearbySummoningRitual(PlayerbotAI* ai)
        {
            Player* bot = ai ? ai->GetBot() : nullptr;
            if (!bot || !bot->IsInWorld() || !bot->IsAlive())
                return nullptr;

            AiObjectContext* context = ai->GetAiObjectContext();
            if (!context)
                return nullptr;

            if (!ai->HasActivePlayerMaster() ||
                !bot->GetGroup() ||
                bot->IsBeingTeleported() ||
                bot->IsTaxiFlying() ||
                bot->hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL) ||
                bot->IsNonMeleeSpellCasted(false, false, true) ||
                sServerFacade.IsInCombat(bot))
                return nullptr;

            GameObject* bestGo = nullptr;
            float bestDistance = GetMaxAssistRange() + 1.0f;
            for (const ObjectGuid& guid : context->GetValue<std::list<ObjectGuid> >("nearest game objects no los")->Get())
            {
                GameObject* go = ai->GetGameObject(guid);
                if (!IsValidSummoningRitual(ai, go))
                    continue;

                float distance = bot->GetDistance(go);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestGo = go;
                }
            }

            return bestGo;
        }

        static bool IsValidSummoningRitual(PlayerbotAI* ai, GameObject* go)
        {
            Player* bot = ai ? ai->GetBot() : nullptr;
            if (!bot || !go || !sServerFacade.isSpawned(go))
                return false;

            GameObjectInfo const* info = go->GetGOInfo();
            if (!info || info->type != GAMEOBJECT_TYPE_SUMMONING_RITUAL)
                return false;

            if (bot->GetMapId() != go->GetMapId() ||
                bot->GetInstanceId() != go->GetInstanceId() ||
                bot->GetDistance(go) > GetMaxAssistRange() ||
                go->IsInUse() ||
                go->GetGoState() != GO_STATE_READY ||
                go->HasUniqueUser(bot))
                return false;

            Unit* owner = go->GetOwner();
            if (!owner || owner->GetTypeId() != TYPEID_PLAYER)
                return false;

            Player* ownerPlayer = static_cast<Player*>(owner);
            if (!ownerPlayer ||
                ownerPlayer == bot ||
                !ownerPlayer->IsAlive() ||
                ownerPlayer->IsBeingTeleported() ||
                ownerPlayer->IsTaxiFlying() ||
                sServerFacade.IsInCombat(ownerPlayer) ||
                !ownerPlayer->IsInSameRaidWith(bot))
                return false;

            Spell* ownerSpell = ownerPlayer->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (!ownerSpell || ownerSpell->GetGOTarget() != go)
                return false;

            if (go->GetEntry() == 36727)
            {
                ObjectGuid summonTargetGuid = go->getSummonTarget();
                if (!summonTargetGuid || !summonTargetGuid.IsPlayer())
                    return false;

                Player* summonTarget = sObjectMgr.GetPlayer(summonTargetGuid);
                if (!summonTarget ||
                    summonTarget == bot ||
                    summonTarget == ownerPlayer ||
                    !summonTarget->IsInSameRaidWith(ownerPlayer))
                    return false;
            }

            return true;
        }
    };

    class ChatCommandAction : public Action
    {
    public:
        ChatCommandAction(PlayerbotAI* ai, std::string name, uint32 duration = sPlayerbotAIConfig.reactDelay) : Action(ai, name, duration) {}
    public:
        virtual bool Execute(Event& event) override { return true; }
    };

    class UpdateStrategyDependenciesAction : public Action
    {
        struct StrategyToUpdate
        {
            StrategyToUpdate(BotState inState, std::string inStrategy, std::vector<std::string> inStrategiesRequired = {})
            : state(inState)
            , name(inStrategy)
            , strategiesRequired(inStrategiesRequired) {}

            BotState state;
            std::string name;
            std::vector<std::string> strategiesRequired;
        };

     public:
         UpdateStrategyDependenciesAction(PlayerbotAI* ai, std::string name = "update strategy dependencies") : Action(ai, name) {}
         bool Execute(Event& event) override;
         bool isUseful() override;

    protected:
        std::vector<StrategyToUpdate> strategiesToUpdate;

    private:
        std::vector<const StrategyToUpdate*> strategiesToAdd;
        std::vector<const StrategyToUpdate*> strategiesToRemove;
    };

    class RemoveBlessingOfSalvationAction : public RemoveAuraAction
    {
    public:
        RemoveBlessingOfSalvationAction(PlayerbotAI* ai) : RemoveAuraAction(ai, "blessing of salvation") {}
    };

    class RemoveGreaterBlessingOfSalvationAction : public RemoveAuraAction
    {
    public:
        RemoveGreaterBlessingOfSalvationAction(PlayerbotAI* ai) : RemoveAuraAction(ai, "greater blessing of salvation") {}
    };

    class InitializePetAction : public Action
    {
    public:
        InitializePetAction(PlayerbotAI* ai) : Action(ai, "initialize pet") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    class SetPetAction : public Action
    {
    public:
        SetPetAction(PlayerbotAI* ai) : Action(ai, "pet") {}
        bool Execute(Event& event) override;
    };
}
