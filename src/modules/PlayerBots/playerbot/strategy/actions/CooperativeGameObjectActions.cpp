#include "playerbot/playerbot.h"
#include "CooperativeGameObjectActions.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

CooperativeObjectPolicy ai::GetCooperativeObjectPolicy(PlayerbotAI* ai)
{
    std::string policy = sPlayerbotAIConfig.companionAutonomyPolicy;
    std::transform(policy.begin(), policy.end(), policy.begin(), ::tolower);
    if (policy == "strict") return CooperativeObjectPolicy::Strict;
    if (policy == "independent") return CooperativeObjectPolicy::Independent;
    return ai && ai->HasActivePlayerMaster() ? CooperativeObjectPolicy::Assist : CooperativeObjectPolicy::Independent;
}

bool ai::AllowsCooperativeObjectUse(PlayerbotAI* ai)
{
    return GetCooperativeObjectPolicy(ai) != CooperativeObjectPolicy::Strict;
}

bool UseCooperativeGameObjectAction::IsEligible(GameObject* go) const
{
    if (!bot || !go || !AllowsCooperativeObjectUse(ai) || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || !sServerFacade.isSpawned(go))
        return false;
    if (bot->GetMapId() != go->GetMapId() || bot->GetInstanceId() != go->GetInstanceId() || bot->GetDistance(go) > INTERACTION_DISTANCE * 3.0f)
        return false;
    if (go->IsInUse() || go->GetGoState() != GO_STATE_READY || go->HasUniqueUser(bot))
        return false;

    GameObjectInfo const* info = go->GetGOInfo();
    if (!info || info->type == GAMEOBJECT_TYPE_SUMMONING_RITUAL || sObjectMgr.IsGameObjectForQuests(go->GetEntry()))
        return false;
    // Only cooperative/server-driven object classes are admitted. Doors,
    // buttons, chests and traps must remain explicit/manual interactions.
    if (info->type != GAMEOBJECT_TYPE_SUMMONING_RITUAL && info->type != GAMEOBJECT_TYPE_GOOBER)
        return false;

    // A goober is admitted only when it is an explicitly scripted event or
    // spell focus. Quest-bound goobers (including questId == -1) stay manual;
    // this intentionally leaves quest-item behavior unchanged.
    if (info->type == GAMEOBJECT_TYPE_GOOBER &&
        (info->goober.questId != 0 || (!info->goober.eventId && !info->goober.spellId)))
        return false;

    Unit* owner = go->GetOwner();
    if (owner && owner->GetTypeId() == TYPEID_PLAYER)
    {
        Player* ownerPlayer = static_cast<Player*>(owner);
        if (!ownerPlayer->IsAlive() || ownerPlayer->IsInCombat() || !ownerPlayer->IsInSameRaidWith(bot))
            return false;
    }
    else if (info->type == GAMEOBJECT_TYPE_SUMMONING_RITUAL)
        return false;
    else if (!ai->GetMaster() || !ai->GetMaster()->IsAlive() || !ai->GetMaster()->IsInSameRaidWith(bot) ||
             sServerFacade.GetDistance2d(bot, ai->GetMaster()) > sPlayerbotAIConfig.sightDistance)
        return false;
    return true;
}

bool UseCooperativeGameObjectAction::isUseful()
{
    // The packet-triggered first execution has not captured its GUID yet;
    // packet ownership is verified by Execute. Continuations use the strict
    // pending-intent predicate below and never scan ambient objects.
    return AllowsCooperativeObjectUse(ai) && ai->HasActivePlayerMaster();
}

bool UseCooperativeGameObjectAction::HasPendingIntent()
{
    if (!AllowsCooperativeObjectUse(ai) || !ai->HasActivePlayerMaster() || targetGuid.IsEmpty() || !intentAt || time(nullptr) - intentAt > 15)
    {
        targetGuid.Clear();
        intentAt = 0;
        return false;
    }
    return IsEligible(ai->GetGameObject(targetGuid));
}

bool UseCooperativeGameObjectAction::Execute(Event& event)
{
    if (!AllowsCooperativeObjectUse(ai) || !ai->HasActivePlayerMaster())
        return false;
    ObjectGuid eventGuid = event.getObject();
    if (!event.getOwner() && targetGuid.IsEmpty())
        return false;
    if (event.getOwner() && event.getOwner() != ai->GetMaster())
        return false;
    if (!eventGuid.IsEmpty())
    {
        targetGuid = eventGuid;
        intentAt = time(nullptr);
    }
    if (!intentAt || time(nullptr) - intentAt > 15)
    {
        targetGuid.Clear();
        intentAt = 0;
        return false;
    }
    GameObject* go = ai->GetGameObject(targetGuid);
    if (!IsEligible(go))
    {
        targetGuid.Clear(); intentAt = 0;
        return false;
    }
    time_t now = time(nullptr);
    for (auto it = completed.begin(); it != completed.end(); )
        it = now - it->second > 30 ? completed.erase(it) : ++it;
    for (auto it = attempts.begin(); it != attempts.end(); )
        it = now - it->second.first > 30 ? attempts.erase(it) : ++it;
    if (completed.find(targetGuid) != completed.end())
        return false;
    std::pair<time_t, uint8>& retry = attempts[targetGuid];
    if (retry.second >= 3 || (retry.first && now - retry.first < 3))
        return false;
    if (!bot->IsWithinDistInMap(go, INTERACTION_DISTANCE, false))
        return MoveNear(go, INTERACTION_DISTANCE - 1.0f);
    if (!IsEligible(go))
    {
        targetGuid.Clear(); intentAt = 0;
        return false;
    }
    if (!bot->CanInteract(go))
        return false;

    WorldPacket data(CMSG_GAMEOBJ_USE);
    data << go->GetObjectGuid();
    retry.first = time(nullptr);
    ++retry.second;
    bot->GetSession()->HandleGameObjectUseOpcode(data);
    // Remember successful one-shot transitions; leave a bounded retry budget
    // when the server kept the object ready (cooldown/eligibility may have
    // changed asynchronously).
    if (go->IsInUse() || go->GetGoState() != GO_STATE_READY)
        completed[targetGuid] = time(nullptr);
    targetGuid.Clear();
    intentAt = 0;
    SetDuration(sPlayerbotAIConfig.globalCoolDown);
    return true;
}
