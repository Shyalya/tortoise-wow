#include "Config/Config.h"
#include "LFTMgr.h"

#include "Group.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"

#include <algorithm>

namespace
{
    static uint32 const LFT_ROLECHECK_TIMEOUT = 90 * IN_MILLISECONDS;
    static uint32 const LFT_OFFER_TIMEOUT = 90 * IN_MILLISECONDS;

    std::vector<std::string> SplitPreserveEmpty(std::string const& value, char delimiter)
    {
        std::vector<std::string> parts;
        std::string current;

        for (char c : value)
        {
            if (c == delimiter)
            {
                parts.push_back(current);
                current.clear();
            }
            else
            {
                current += c;
            }
        }

        parts.push_back(current);
        return parts;
    }

    bool HasInstance(std::vector<std::string> const& instances, std::string const& instance)
    {
        return std::find(instances.begin(), instances.end(), instance) != instances.end();
    }

    bool HasString(std::vector<std::string> const& values, std::string const& value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    uint8 PickRole(uint8 roleMask, uint8 tankCount, uint8 healerCount, uint8 damageCount)
    {
        if ((roleMask & LFT_ROLE_TANK) && tankCount < 1)
            return LFT_ROLE_TANK;

        if ((roleMask & LFT_ROLE_HEALER) && healerCount < 1)
            return LFT_ROLE_HEALER;

        if ((roleMask & LFT_ROLE_DAMAGE) && damageCount < 3)
            return LFT_ROLE_DAMAGE;

        return 0;
    }
}


void LFTManager::HandleQueueJoin(Player* player, std::vector<std::string> const& fields)
{
    if (fields.size() < 3)
        return;

    if (player->GetGroup() && player->GetGroup()->isRaidGroup())
    {
        Send(player, "S2C_QUEUE_ERROR;raid");
        return;
    }

    if (player->GetGroup() && !player->GetGroup()->IsLeader(player->GetObjectGuid()))
    {
        Send(player, "S2C_QUEUE_ERROR;not_leader");
        return;
    }

    std::vector<std::string> instances = GetSharedInstances(SplitPreserveEmpty(fields[1], ':'));
    uint8 roleMask = ParseRoleMask(fields[2]) & AllowedRoleMask(player);

    if (instances.empty() || !roleMask)
    {
        Send(player, "S2C_QUEUE_ERROR;invalid");
        return;
    }

    CleanupPlayer(player->GetObjectGuid());
    StartRolecheck(player, instances);
}

void LFTManager::HandleQueueLeave(Player* player)
{
    if (!player)
        return;

    ObjectGuid guid = player->GetObjectGuid();

    for (RolecheckMap::iterator itr = m_rolechecks.begin(); itr != m_rolechecks.end(); ++itr)
    {
        if (std::find(itr->second.members.begin(), itr->second.members.end(), guid) != itr->second.members.end())
        {
            CancelRolecheck(itr);
            return;
        }
    }

    std::map<ObjectGuid, uint32>::iterator offerItr = m_playerOffers.find(guid);
    if (offerItr != m_playerOffers.end())
    {
        CancelOffer(offerItr->second, true, guid, true);
        TryMakeOffers();
        return;
    }

    QueueMap::iterator queueItr = m_queue.find(guid);
    if (queueItr == m_queue.end())
        return;

    ObjectGuid leaderGuid = queueItr->second.queueLeaderGuid.IsEmpty() ? guid : queueItr->second.queueLeaderGuid;
    std::vector<ObjectGuid> toRemove;
    for (QueueMap::const_iterator itr = m_queue.begin(); itr != m_queue.end(); ++itr)
    {
        ObjectGuid itrLeader = itr->second.queueLeaderGuid.IsEmpty() ? itr->first : itr->second.queueLeaderGuid;
        if (itrLeader == leaderGuid)
            toRemove.push_back(itr->first);
    }

    for (ObjectGuid const& removeGuid : toRemove)
    {
        std::string name = m_queue[removeGuid].name;
        m_queue.erase(removeGuid);
        SendQueueLeft(removeGuid, name);
    }
}

void LFTManager::HandleGetQueueStatus(Player* player)
{
    SendQueueStatus(player);
}

void LFTManager::HandleRolecheckResponse(Player* player, std::vector<std::string> const& fields)
{
    if (fields.size() < 2 || !player)
        return;

    uint8 roleMask = ParseRoleMask(fields[1]) & AllowedRoleMask(player);
    if (!roleMask)
        return;

    ObjectGuid guid = player->GetObjectGuid();
    for (RolecheckMap::iterator itr = m_rolechecks.begin(); itr != m_rolechecks.end(); ++itr)
    {
        if (std::find(itr->second.members.begin(), itr->second.members.end(), guid) == itr->second.members.end())
            continue;

        itr->second.responses[guid] = roleMask;

        std::string roleText = fields[1];
        for (ObjectGuid const& memberGuid : itr->second.members)
            if (Player* member = GetPlayer(memberGuid))
                Send(member, "S2C_ROLECHECK_INFO;" + std::string(player->GetName()) + ";" + roleText);

        if (itr->second.responses.size() == itr->second.members.size())
        {
            PendingRolecheck finished = itr->second;
            m_rolechecks.erase(itr);
            EnqueueRolecheck(finished);
        }

        return;
    }
}

void LFTManager::HandleOfferAccept(Player* player)
{
    if (!player)
        return;

    std::map<ObjectGuid, uint32>::iterator playerOffer = m_playerOffers.find(player->GetObjectGuid());
    if (playerOffer == m_playerOffers.end())
        return;

    OffersMap::iterator offerItr = m_offers.find(playerOffer->second);
    if (offerItr == m_offers.end())
        return;

    offerItr->second.accepted.insert(player->GetObjectGuid());

    uint8 tanks = 0;
    uint8 healers = 0;
    uint8 damage = 0;
    for (ObjectGuid const& acceptedGuid : offerItr->second.accepted)
    {
        uint8 role = offerItr->second.roles[acceptedGuid];
        if (role == LFT_ROLE_TANK)
            ++tanks;
        else if (role == LFT_ROLE_HEALER)
            ++healers;
        else if (role == LFT_ROLE_DAMAGE)
            ++damage;
    }

    std::string countMessage = "S2C_OFFER_UPDATE_COUNT;" + std::to_string(tanks) + ":" + std::to_string(healers) + ":" + std::to_string(damage);
    for (std::map<ObjectGuid, uint8>::const_iterator itr = offerItr->second.roles.begin(); itr != offerItr->second.roles.end(); ++itr)
        if (Player* offerPlayer = GetPlayer(itr->first))
            Send(offerPlayer, countMessage);

    if (offerItr->second.accepted.size() == offerItr->second.roles.size())
        CompleteOffer(offerItr->first);
}

void LFTManager::SendQueueStatus(Player* player) const
{
    if (!player)
        return;

    ObjectGuid guid = player->GetObjectGuid();

    if (m_playerOffers.find(guid) != m_playerOffers.end())
    {
        Send(player, "S2C_UPDATE_QUEUE_STATUS;pending_offer");
        return;
    }

    for (RolecheckMap::const_iterator itr = m_rolechecks.begin(); itr != m_rolechecks.end(); ++itr)
    {
        if (std::find(itr->second.members.begin(), itr->second.members.end(), guid) != itr->second.members.end())
        {
            Send(player, "S2C_UPDATE_QUEUE_STATUS;pending_rolecheck");
            return;
        }
    }

    QueueMap::const_iterator queueItr = m_queue.find(guid);
    if (queueItr != m_queue.end())
        SendQueuedStatus(player, queueItr->second);
}

void LFTManager::SendQueuedStatus(Player* player, QueuedPlayer const& queued) const
{
    uint8 tanks = queued.assignedRole == LFT_ROLE_TANK ? 1 : 0;
    uint8 healers = queued.assignedRole == LFT_ROLE_HEALER ? 1 : 0;
    uint8 damage = queued.assignedRole == LFT_ROLE_DAMAGE ? 1 : 0;

    Send(player, "S2C_UPDATE_QUEUE_STATUS;queued;" + JoinStrings(queued.instances, ':') + ";" +
        std::to_string(uint32(queued.joinTime)) + ";0;" + std::to_string(tanks) + ":" +
        std::to_string(healers) + ":" + std::to_string(damage));
}

void LFTManager::SendQueueJoined(Player* player, QueuedPlayer const& queued) const
{
    Send(player, "S2C_QUEUE_JOINED");
    SendQueuedStatus(player, queued);
}

void LFTManager::SendQueueLeft(ObjectGuid const& guid, std::string const& name)
{
    if (Player* player = GetPlayer(guid))
        Send(player, "S2C_QUEUE_LEFT;" + name);
}

void LFTManager::StartRolecheck(Player* leader, std::vector<std::string> const& instances)
{
    PendingRolecheck rolecheck;
    rolecheck.leaderGuid = leader->GetObjectGuid();
    rolecheck.members = GetPartyMembers(leader);
    rolecheck.instances = instances;
    rolecheck.timer = LFT_ROLECHECK_TIMEOUT;

    m_rolechecks[rolecheck.leaderGuid] = rolecheck;

    std::string joinedInstances = JoinStrings(instances, ':');
    for (ObjectGuid const& guid : rolecheck.members)
    {
        if (Player* member = GetPlayer(guid))
        {
            Send(member, "S2C_ROLECHECK_START;" + joinedInstances);
            Send(member, "S2C_UPDATE_QUEUE_STATUS;pending_rolecheck");
        }
    }
}

void LFTManager::EnqueuePlayer(Player* player, ObjectGuid const& leaderGuid, std::vector<std::string> const& instances, uint8 roleMask)
{
    if (!player)
        return;

    QueuedPlayer queued;
    queued.guid = player->GetObjectGuid();
    queued.queueLeaderGuid = leaderGuid;
    queued.name = player->GetName();
    queued.className = ClassName(player);
    queued.level = player->GetLevel();
    queued.team = player->GetTeam();
    queued.isHardcore = player->IsHardcore();
    queued.joinTime = sWorld.GetGameTime();
    queued.queueOrder = m_nextQueueOrder++;
    queued.instances = instances;
    queued.roleMask = roleMask;
    queued.assignedRole = PickRole(roleMask, 0, 0, 0);

    m_queue[queued.guid] = queued;
    SendQueueJoined(player, m_queue[queued.guid]);
}

void LFTManager::EnqueueRolecheck(PendingRolecheck const& rolecheck)
{
    for (ObjectGuid const& guid : rolecheck.members)
    {
        Player* player = GetPlayer(guid);
        if (!player)
            continue;

        std::map<ObjectGuid, uint8>::const_iterator response = rolecheck.responses.find(guid);
        if (response == rolecheck.responses.end())
            continue;

        EnqueuePlayer(player, rolecheck.leaderGuid, rolecheck.instances, response->second);
    }

    TryMakeOffers();
}

void LFTManager::CancelRolecheck(RolecheckMap::iterator itr)
{
    if (itr == m_rolechecks.end())
        return;

    for (ObjectGuid const& guid : itr->second.members)
        SendQueueLeft(guid, GetPlayer(guid) ? GetPlayer(guid)->GetName() : "");

    m_rolechecks.erase(itr);
}

void LFTManager::CancelOffer(uint32 offerId, bool requeueAccepted, ObjectGuid const& removeGuid, bool requeueOthers)
{
    OffersMap::iterator offerItr = m_offers.find(offerId);
    if (offerItr == m_offers.end())
        return;

    Offer offer = offerItr->second;
    for (std::map<ObjectGuid, uint8>::const_iterator itr = offer.roles.begin(); itr != offer.roles.end(); ++itr)
    {
        m_playerOffers.erase(itr->first);

        bool const keepQueued = requeueAccepted && itr->first != removeGuid &&
            (requeueOthers || offer.accepted.find(itr->first) != offer.accepted.end());
        QueueMap::iterator queued = m_queue.find(itr->first);
        if (keepQueued && queued != m_queue.end())
        {
            queued->second.assignedRole = PickRole(queued->second.roleMask, 0, 0, 0);
            if (Player* player = GetPlayer(itr->first))
                SendQueueJoined(player, queued->second);
        }
        else
        {
            m_queue.erase(itr->first);
            SendQueueLeft(itr->first, GetPlayer(itr->first) ? GetPlayer(itr->first)->GetName() : "");
        }
    }

    m_offers.erase(offerItr);
}

void LFTManager::TryMakeOffers()
{
    std::set<std::string> instances;
    for (QueueMap::const_iterator itr = m_queue.begin(); itr != m_queue.end(); ++itr)
    {
        if (m_playerOffers.find(itr->first) != m_playerOffers.end())
            continue;

        for (std::string const& instance : itr->second.instances)
            instances.insert(instance);
    }

    for (std::string const& instance : instances)
        TryBuildOfferForInstance(instance);
}

bool LFTManager::TryBuildOfferForInstance(std::string const& instance)
{
    std::map<ObjectGuid, uint8> selectedRoles;
    bool foundOffer = false;
    std::vector<ObjectGuid> queueOrder = GetQueueOrder();

    for (ObjectGuid const& seedGuid : queueOrder)
    {
        QueueMap::const_iterator seed = m_queue.find(seedGuid);
        if (seed == m_queue.end())
            continue;

        if (m_playerOffers.find(seed->first) != m_playerOffers.end() || !HasInstance(seed->second.instances, instance))
            continue;

        selectedRoles.clear();
        std::set<ObjectGuid> selected;
        uint8 tanks = 0;
        uint8 healers = 0;
        uint8 damage = 0;

        for (ObjectGuid const& candidateGuid : queueOrder)
        {
            if (selected.size() >= 5)
                break;

            QueueMap::iterator itr = m_queue.find(candidateGuid);
            if (itr == m_queue.end())
                continue;

            if (selected.find(itr->first) != selected.end() || m_playerOffers.find(itr->first) != m_playerOffers.end())
                continue;

            if (!HasInstance(itr->second.instances, instance))
                continue;

            if (!CanQueuedPlayersGroup(seed->second, itr->second))
                continue;

            ObjectGuid blockLeader = itr->second.queueLeaderGuid.IsEmpty() ? itr->first : itr->second.queueLeaderGuid;
            std::vector<ObjectGuid> block;
            for (ObjectGuid const& blockGuid : queueOrder)
            {
                QueueMap::const_iterator blockItr = m_queue.find(blockGuid);
                if (blockItr == m_queue.end())
                    continue;

                ObjectGuid otherLeader = blockItr->second.queueLeaderGuid.IsEmpty() ? blockItr->first : blockItr->second.queueLeaderGuid;
                if (otherLeader == blockLeader)
                    block.push_back(blockItr->first);
            }

            std::map<ObjectGuid, uint8> blockRoles;
            uint8 blockTanks = tanks;
            uint8 blockHealers = healers;
            uint8 blockDamage = damage;
            bool blockFits = true;

            for (ObjectGuid const& guid : block)
            {
                QueueMap::const_iterator queued = m_queue.find(guid);
                if (queued == m_queue.end() || selected.find(guid) != selected.end() ||
                    m_playerOffers.find(guid) != m_playerOffers.end() || !HasInstance(queued->second.instances, instance) ||
                    !CanQueuedPlayersGroup(seed->second, queued->second))
                {
                    blockFits = false;
                    break;
                }

                uint8 role = PickRole(queued->second.roleMask, blockTanks, blockHealers, blockDamage);
                if (!role)
                {
                    blockFits = false;
                    break;
                }

                blockRoles[guid] = role;
                if (role == LFT_ROLE_TANK)
                    ++blockTanks;
                else if (role == LFT_ROLE_HEALER)
                    ++blockHealers;
                else if (role == LFT_ROLE_DAMAGE)
                    ++blockDamage;
            }

            if (!blockFits || blockRoles.empty() || selected.size() + blockRoles.size() > 5)
                continue;

            for (std::map<ObjectGuid, uint8>::const_iterator roleItr = blockRoles.begin(); roleItr != blockRoles.end(); ++roleItr)
            {
                selected.insert(roleItr->first);
                selectedRoles[roleItr->first] = roleItr->second;
            }

            tanks = blockTanks;
            healers = blockHealers;
            damage = blockDamage;
        }

        if (tanks == 1 && healers == 1 && damage == 3 && selectedRoles.size() == 5)
        {
            foundOffer = true;
            break;
        }
    }

    if (!foundOffer)
        return false;

    Offer offer;
    offer.id = m_nextOfferId++;
    offer.instance = instance;
    offer.roles = selectedRoles;
    offer.timer = LFT_OFFER_TIMEOUT;
    m_offers[offer.id] = offer;

    for (std::map<ObjectGuid, uint8>::const_iterator itr = selectedRoles.begin(); itr != selectedRoles.end(); ++itr)
    {
        m_queue[itr->first].assignedRole = itr->second;
        m_playerOffers[itr->first] = offer.id;
        if (Player* player = GetPlayer(itr->first))
        {
            std::string role = itr->second == LFT_ROLE_TANK ? "t" : itr->second == LFT_ROLE_HEALER ? "h" : "d";
            Send(player, "S2C_UPDATE_QUEUE_STATUS;pending_offer");
            Send(player, "S2C_OFFER_NEW;" + instance + ";" + role);
            Send(player, "S2C_OFFER_UPDATE_COUNT;0:0:0");
        }
    }

    return true;
}

namespace
{
    // The finder's instance names are display strings; game_tele stores the
    // entrances under squashed ones. Strip everything that only exists for
    // reading so the two can be compared.
    std::string SquashInstanceName(std::string const& name)
    {
        std::string out;
        for (char c : name)
            if (std::isalnum((unsigned char)c))
                out += (char)std::tolower((unsigned char)c);

        // "The Deadmines" and "The Stockade" are listed without the article.
        if (out.compare(0, 3, "the") == 0 && out.size() > 3)
            out = out.substr(3);

        return out;
    }

    // Longest game_tele name that starts the instance name. Longest on purpose:
    // the wings share an entrance, so "Scarlet Monastery Library" has to land on
    // "ScarletMonastery" and not on some shorter accident. The minimum length
    // keeps a very short tele entry from matching half the list.
    GameTele const* FindInstanceEntrance(std::string const& instance)
    {
        std::string const wanted = SquashInstanceName(instance);
        GameTele const* best = nullptr;
        size_t bestLength = 0;

        for (auto const& entry : sObjectMgr.GetGameTeleMap())
        {
            std::string const candidate = SquashInstanceName(entry.second.name);
            if (candidate.size() < 5 || candidate.size() <= bestLength)
                continue;

            if (wanted.compare(0, candidate.size(), candidate) == 0)
            {
                best = &entry.second;
                bestLength = candidate.size();
            }
        }

        return best;
    }
}

// A group that formed without anybody real in it has nowhere to be but the
// instance, and its members are scattered across two continents - the seed for
// one Scarlet Monastery run stood in Stranglethorn with a member in Feralas.
// Walking that is half an hour of travel that mostly ends in a death. Move them
// to the door instead; from there they play normally.
//
// Never with a person in the group. They did not ask to be moved, and bots that
// have a real player as master follow them anyway.
void LFTManager::TeleportBotGroupToInstance(Offer const& offer)
{
    if (!sConfig.GetBoolDefault("LFT.BotFill.SeedTeleport", false))
        return;

    for (auto const& role : offer.roles)
    {
        Player* member = GetPlayer(role.first);
        if (!member || !Script_IsAIControlled(member))
            return;
    }

    GameTele const* entrance = FindInstanceEntrance(offer.instance);
    if (!entrance)
    {
        sLog.outError("LFT: no game_tele entrance for '%s' - group left where it stands", offer.instance.c_str());
        return;
    }

    for (auto const& role : offer.roles)
    {
        Player* member = GetPlayer(role.first);
        if (!member || member->IsBeingTeleported())
            continue;

        // Skipping anyone in combat left a member of the first group standing in
        // Feralas while the other four waited at the door. The manager already
        // teleports bots out of fights on its own schedule, so there is nothing
        // to protect here - break off the fight and bring them along.
        if (member->IsInCombat())
            member->CombatStop(true);

        member->TeleportTo(entrance->mapId, entrance->x, entrance->y, entrance->z, entrance->o);
    }

    sLog.outBasic("LFT: bot group moved to the entrance of '%s' (%s)", offer.instance.c_str(), entrance->name.c_str());
}

bool LFTManager::CompleteOffer(uint32 offerId)
{
    OffersMap::iterator offerItr = m_offers.find(offerId);
    if (offerItr == m_offers.end())
        return false;

    Offer offer = offerItr->second;
    ObjectGuid leaderGuid;
    for (std::map<ObjectGuid, uint8>::const_iterator itr = offer.roles.begin(); itr != offer.roles.end(); ++itr)
    {
        QueueMap::const_iterator queued = m_queue.find(itr->first);
        if (queued != m_queue.end() && !queued->second.queueLeaderGuid.IsEmpty())
        {
            leaderGuid = queued->second.queueLeaderGuid;
            break;
        }
    }

    if (leaderGuid.IsEmpty())
    {
        for (std::map<ObjectGuid, uint8>::const_iterator itr = offer.roles.begin(); itr != offer.roles.end(); ++itr)
        {
            if (itr->second == LFT_ROLE_TANK)
            {
                leaderGuid = itr->first;
                break;
            }
        }
    }

    if (leaderGuid.IsEmpty())
        leaderGuid = offer.roles.begin()->first;

    Group* group = nullptr;
    for (std::map<ObjectGuid, uint8>::const_iterator itr = offer.roles.begin(); itr != offer.roles.end(); ++itr)
    {
        if (!AddPlayerToGroup(group, leaderGuid, itr->first))
        {
            CancelOffer(offerId, false);
            return false;
        }
    }

    for (std::map<ObjectGuid, uint8>::const_iterator itr = offer.roles.begin(); itr != offer.roles.end(); ++itr)
    {
        m_queue.erase(itr->first);
        m_playerOffers.erase(itr->first);
        if (Player* player = GetPlayer(itr->first))
            Send(player, "S2C_OFFER_COMPLETE");
    }

    TeleportBotGroupToInstance(offer);

    m_offers.erase(offerId);
    return true;
}

bool LFTManager::AddPlayerToGroup(Group*& group, ObjectGuid const& leaderGuid, ObjectGuid const& memberGuid)
{
    Player* member = GetPlayer(memberGuid);
    if (!member)
        return false;

    if (!group)
    {
        Player* leader = GetPlayer(leaderGuid);
        if (!leader)
            return false;

        group = leader->GetGroup();
        if (!group)
        {
            group = new Group;
            if (!group->Create(leader->GetObjectGuid(), leader->GetName()))
            {
                delete group;
                group = nullptr;
                return false;
            }
            sObjectMgr.AddGroup(group);
        }
    }

    // Formed or adopted by this matcher either way - remember it as OURS
    // (TakeFromBotOnlyGroup refuses to raid unregistered bot groups).
    m_lftGroupIds.insert(group->GetId());

    if (group->IsMember(memberGuid))
        return true;

    if (member->GetGroup() && member->GetGroup() != group)
        return false;

    Player* leader = GetPlayer(leaderGuid);
    if (leader && !CanPlayersGroup(leader, member))
        return false;

    if (group->IsFull())
        return false;

    return group->AddMember(memberGuid, member->GetName(), GROUP_LFG);
}

bool LFTManager::CanQueuedPlayersGroup(QueuedPlayer const& seed, QueuedPlayer const& candidate) const
{
    Player* seedPlayer = GetPlayer(seed.guid);
    Player* candidatePlayer = GetPlayer(candidate.guid);
    if (seedPlayer && candidatePlayer)
        return CanPlayersGroup(seedPlayer, candidatePlayer);

    if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GROUP) && seed.team != candidate.team)
        return false;

    return seed.isHardcore == candidate.isHardcore;
}

std::vector<ObjectGuid> LFTManager::GetPartyMembers(Player* leader) const
{
    std::vector<ObjectGuid> members;
    if (!leader)
        return members;

    Group* group = leader->GetGroup();
    if (!group)
    {
        members.push_back(leader->GetObjectGuid());
        return members;
    }

    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        if (GetPlayer(slot.guid))
            members.push_back(slot.guid);
    }

    return members;
}

std::vector<ObjectGuid> LFTManager::GetQueueOrder() const
{
    std::vector<ObjectGuid> order;
    order.reserve(m_queue.size());

    for (QueueMap::const_iterator itr = m_queue.begin(); itr != m_queue.end(); ++itr)
        order.push_back(itr->first);

    std::sort(order.begin(), order.end(), [this](ObjectGuid const& leftGuid, ObjectGuid const& rightGuid)
    {
        QueueMap::const_iterator left = m_queue.find(leftGuid);
        QueueMap::const_iterator right = m_queue.find(rightGuid);
        if (left == m_queue.end() || right == m_queue.end())
            return leftGuid < rightGuid;

        if (left->second.queueOrder != right->second.queueOrder)
            return left->second.queueOrder < right->second.queueOrder;

        return leftGuid < rightGuid;
    });

    return order;
}

std::vector<std::string> LFTManager::GetSharedInstances(std::vector<std::string> const& requested) const
{
    std::vector<std::string> instances;
    for (std::string const& instance : requested)
    {
        if (!instance.empty() && !HasString(instances, instance))
            instances.push_back(instance);
    }

    return instances;
}

uint8 LFTManager::ParseRoleMask(std::string const& roles) const
{
    uint8 mask = 0;
    for (char role : roles)
    {
        if (role == 't' || role == 'T')
            mask |= LFT_ROLE_TANK;
        else if (role == 'h' || role == 'H')
            mask |= LFT_ROLE_HEALER;
        else if (role == 'd' || role == 'D')
            mask |= LFT_ROLE_DAMAGE;
    }

    return mask;
}

std::string LFTManager::JoinStrings(std::vector<std::string> const& values, char delimiter) const
{
    std::string joined;
    for (std::string const& value : values)
    {
        if (!joined.empty())
            joined += delimiter;
        joined += value;
    }

    return joined;
}

uint8 LFTManager::AllowedRoleMask(Player const* player) const
{
    if (!player)
        return 0;

    switch (player->GetClass())
    {
        case CLASS_DRUID:   return LFT_ROLE_TANK | LFT_ROLE_HEALER | LFT_ROLE_DAMAGE;
        case CLASS_HUNTER:  return LFT_ROLE_DAMAGE;
        case CLASS_MAGE:    return LFT_ROLE_DAMAGE;
        case CLASS_PALADIN: return LFT_ROLE_TANK | LFT_ROLE_HEALER | LFT_ROLE_DAMAGE;
        case CLASS_PRIEST:  return LFT_ROLE_HEALER | LFT_ROLE_DAMAGE;
        case CLASS_ROGUE:   return LFT_ROLE_DAMAGE;
        // No tank here. There is no shaman tank talent tree in 1.12 and the bot
        // module has no tank strategy for one, so offering the slot only led to
        // a shaman being handed the role and respecced into whatever came to
        // hand - enhancement and restoration both turned up in the log under
        // "respecced for role 1".
        case CLASS_SHAMAN:  return LFT_ROLE_HEALER | LFT_ROLE_DAMAGE;
        case CLASS_WARLOCK: return LFT_ROLE_DAMAGE;
        case CLASS_WARRIOR: return LFT_ROLE_TANK | LFT_ROLE_DAMAGE;
        default:            return 0;
    }
}

void LFTManager::CleanupPlayer(ObjectGuid const& guid)
{
    m_queue.erase(guid);

    std::map<ObjectGuid, uint32>::iterator offerItr = m_playerOffers.find(guid);
    if (offerItr != m_playerOffers.end())
        CancelOffer(offerItr->second, true, guid, true);

    for (RolecheckMap::iterator itr = m_rolechecks.begin(); itr != m_rolechecks.end();)
    {
        if (std::find(itr->second.members.begin(), itr->second.members.end(), guid) != itr->second.members.end())
        {
            RolecheckMap::iterator removed = itr++;
            CancelRolecheck(removed);
            continue;
        }

        ++itr;
    }

}
