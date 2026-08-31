/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 * Copyright (C) 2011-2016 Nostalrius <https://nostalrius.org>
 * Copyright (C) 2016-2017 Elysium Project <https://github.com/elysium-project>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ChannelMgr.h"
#include "MapNodes/AbstractPlayer.h"
#include "Policies/SingletonImp.h"
#include "World.h"
#include "Util.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "ChannelBroadcaster.h"

#include <cctype>
#include <set>

INSTANTIATE_SINGLETON_1(AllianceChannelMgr);
INSTANTIATE_SINGLETON_1(HordeChannelMgr);

uint32 ChannelMgr::customChannelCount = 0;

namespace
{
bool MakeChannelKey(std::string const& name, std::wstring& key)
{
    if (!Utf8toWStr(name, key) || key.empty())
        return false;

    wstrToLower(key);
    return true;
}
}

ChannelMgr* channelMgr(Team team)
{
    if (sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_CHANNEL))
        return &MaNGOS::Singleton<AllianceChannelMgr>::Instance();        // cross-faction

    if (team == ALLIANCE)
        return &MaNGOS::Singleton<AllianceChannelMgr>::Instance();

    if (team == HORDE)
        return &MaNGOS::Singleton<HordeChannelMgr>::Instance();

    return nullptr;
}

ChannelMgr::~ChannelMgr()
{
    for (const auto& channel : channels)
    {
        if (channel.second->HasFlag(Channel::CHANNEL_FLAG_CUSTOM) && customChannelCount)
            --customChannelCount;
        delete channel.second;
    }

    channels.clear();
}

bool ChannelMgr::IsValidChannelName(std::string const& name)
{
    // Keep the bound on the wire representation as well as the normalized
    // character count.  Built-in names still have to fit the same packet
    // bound, but are not subject to custom-channel normalization or quotas.
    if (name.empty() || name.size() > CHANNEL_MAX_NAME_LENGTH || name.find('\0') != std::string::npos)
        return false;

    if (uint8(name[0]) <= 127 && !std::isalpha(static_cast<unsigned char>(name[0])))
        return false;

    std::wstring unicodeName;
    if (!Utf8toWStr(name, unicodeName) || unicodeName.empty())
        return false;

    if (sObjectMgr.GetChannelEntryFor(name) || IsReservedChannelName(name))
        return true;

    std::string normalized = name;
    return normalizePlayerName(normalized, CHANNEL_MAX_NAME_LENGTH);
}

bool ChannelMgr::IsReservedChannelName(std::string const& name)
{
    if (sObjectMgr.GetChannelEntryFor(name))
        return true;

    std::wstring key;
    if (!MakeChannelKey(name, key))
        return false;

    // These channels are owned by server features, or intentionally behave as
    // first-class public/national channels despite missing Channel.dbc rows.
    // Classify them before applying attacker-controlled custom-channel quotas.
    static std::set<std::wstring> const reservedNames =
    {
        L"warden", L"anticrash", L"antiflood", L"itemscheck", L"golddupe",
        L"sac", L"mailsac", L"botsdetector", L"chatspam", L"lowlevelbots",
        L"global", L"world", L"worldh", L"worlda", L"english", L"ru",
        L"welt", L"china"
    };
    return reservedNames.find(key) != reservedNames.end();
}

bool ChannelMgr::CanPlayerJoinCustomChannel(ObjectGuid playerGuid)
{
    if (playerGuid.IsEmpty())
        return true;

    uint32 count = 0;
    ChannelMgr* alliance = channelMgr(ALLIANCE);
    if (alliance)
        count += alliance->GetCustomChannelCount(playerGuid);

    ChannelMgr* horde = channelMgr(HORDE);
    if (horde && horde != alliance)
        count += horde->GetCustomChannelCount(playerGuid);

    return count < CHANNEL_MAX_CUSTOM_CHANNELS_PER_PLAYER;
}

Channel *ChannelMgr::GetOrCreateChannel(std::string const& name, bool allowAreaDependantChans, ObjectGuid playerGuid)
{
    ChatChannelsEntry const* ch = sObjectMgr.GetChannelEntryFor(name);
    bool const reserved = ch || IsReservedChannelName(name);
    std::string channelName = name;

    if (!IsValidChannelName(name))
        return nullptr;

    if (!reserved && !normalizePlayerName(channelName, CHANNEL_MAX_NAME_LENGTH))
        return nullptr;

    std::wstring wname;
    if (!MakeChannelKey(channelName, wname))
        return nullptr;

    if (channels.find(wname) == channels.end())
    {
        if (!allowAreaDependantChans && ch && ch->flags & Channel::CHANNEL_DBC_FLAG_ZONE_DEP)
            return nullptr;

        if (!reserved)
        {
            if (customChannelCount >= CHANNEL_MAX_CUSTOM_CHANNELS ||
                (!playerGuid.IsEmpty() && !CanPlayerJoinCustomChannel(playerGuid)))
                return nullptr;
        }

        Channel *nchan = new Channel(channelName, m_team);
        auto result = channels.emplace(wname, nchan);
        if (!result.second)
        {
            delete nchan;
            return result.first->second;
        }

        if (nchan->HasFlag(Channel::CHANNEL_FLAG_CUSTOM))
            ++customChannelCount;

        return nchan;
    }

    return channels.find(wname)->second;
}

uint32 ChannelMgr::GetCustomChannelCount(ObjectGuid playerGuid) const
{
    uint32 count = 0;
    for (const auto& channel : channels)
        if (channel.second->HasFlag(Channel::CHANNEL_FLAG_CUSTOM) && channel.second->IsMember(playerGuid))
            ++count;

    return count;
}

// bot passes Player*; convert via PlayerWrapper.
Channel* ChannelMgr::GetChannel(std::string const& name, Player* p, bool sendPacket)
{
    return GetChannel(name, PlayerPointer(p ? new PlayerWrapper<Player>(p) : nullptr), sendPacket);
}

Channel *ChannelMgr::GetChannel(std::string const& name, PlayerPointer p, bool sendPacket)
{
    std::wstring wname;
    if (!MakeChannelKey(name, wname))
        return nullptr;

    ChannelMap::const_iterator i = channels.find(wname);

    if (i == channels.end())
    {
        if (sendPacket && p)
        {
            WorldPacket data;
            Channel::MakeNotOnPacket(&data, name);
            p->GetSession()->SendPacket(&data);
        }

        return nullptr;
    }
    else
        return i->second;
}

void ChannelMgr::LeftChannel(std::string const& name)
{
    std::wstring wname;
    if (!MakeChannelKey(name, wname))
        return;

    ChannelMap::const_iterator i = channels.find(wname);

    if (i == channels.end())
        return;

    Channel* channel = i->second;

    if (channel->GetNumPlayers() == 0 && !channel->IsConstant() && !channel->GetSecurityLevel())
    {
        if (channel->HasFlag(Channel::CHANNEL_FLAG_CUSTOM) && customChannelCount)
            --customChannelCount;
        channels.erase(i);
        delete channel;
    }
}

void ChannelMgr::CreateDefaultChannels()
{
    auto createServerChannel = [this](char const* name, AccountTypes security)
    {
        if (Channel* channel = GetOrCreateChannel(name))
            channel->SetSecurityLevel(security);
    };

    createServerChannel("Warden", SEC_DEVELOPER);
    createServerChannel("Anticrash", SEC_DEVELOPER);
    createServerChannel("Antiflood", SEC_MODERATOR);
    createServerChannel("ItemsCheck", SEC_DEVELOPER);
    createServerChannel("GoldDupe", SEC_DEVELOPER);
    createServerChannel("SAC", SEC_DEVELOPER);
    createServerChannel("MailsAC", SEC_DEVELOPER);
    createServerChannel("BotsDetector", SEC_DEVELOPER);
    createServerChannel("ChatSpam", SEC_MODERATOR);
    createServerChannel("LowLevelBots", SEC_DEVELOPER);
    createServerChannel("Global", SEC_MODERATOR);
    if (sWorld.IsPvPRealm())
    {
        createServerChannel("WorldH", SEC_MODERATOR);
        createServerChannel("WorldA", SEC_MODERATOR);
    }

    for (const auto& channel : channels)
        channel.second->SetAnnounce(false);
}

void ChannelMgr::AnnounceBothFactionsChannel(std::string const& channelName, ObjectGuid playerGuid, char const* message)
{
    ChannelBroadcaster* broadcaster = sWorld.GetChannelBroadcaster();
    if (!broadcaster || !message)
        return;

    broadcaster->EnqueueMessage(std::string(message), channelName, playerGuid, LANG_UNIVERSAL, HORDE, true);
    if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_CHANNEL))
        broadcaster->EnqueueMessage(std::string(message), channelName, playerGuid, LANG_UNIVERSAL, ALLIANCE, true);
}

AllianceChannelMgr::AllianceChannelMgr()
{
    m_team = ALLIANCE;
}

HordeChannelMgr::HordeChannelMgr()
{
    m_team = HORDE;
}
