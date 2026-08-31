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


#include "Auth/AuthCrypt.h"

#include "Config/Config.h"
#include "World.h"
#include "AccountMgr.h"
#include "SharedDefines.h"
#include "WorldSession.h"
#include "WorldSocket.h"
#include "WorldSocketMgr.h"
#include "AddonHandler.h"
#include "Anticheat/Anticheat.h"


#include "Opcodes.h"
#include "MangosSocketImpl.h"

#include <ace/INET_Addr.h>

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{
    char const WorldPreAuthTimerTag = 0;

    struct WorldAuthLimiter
    {
        std::mutex lock;
        std::unordered_map<std::string, uint32> preauthConnections;
        uint32 totalPreauthConnections = 0;
        std::deque<std::chrono::steady_clock::time_point> authAttempts;
        std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> authAttemptsByPeer;
    };

    WorldAuthLimiter& GetWorldAuthLimiter()
    {
        static WorldAuthLimiter* limiter = new WorldAuthLimiter();
        return *limiter;
    }

    using AttemptQueue = std::deque<std::chrono::steady_clock::time_point>;

    uint32 GetNonNegativeWorldConfig(char const* name, uint32 defaultValue)
    {
        int32 const value = sConfig.GetIntDefault(name, static_cast<int32>(defaultValue));
        return value < 0 ? defaultValue : static_cast<uint32>(value);
    }

    void PruneWorldAuthAttempts(AttemptQueue& attempts, std::chrono::steady_clock::time_point now,
        std::chrono::seconds window)
    {
        while (!attempts.empty() && now - attempts.front() >= window)
            attempts.pop_front();
    }

    void CleanupWorldAuthAttempts(WorldAuthLimiter& limiter, std::chrono::steady_clock::time_point now,
        std::chrono::seconds window)
    {
        PruneWorldAuthAttempts(limiter.authAttempts, now, window);
        for (auto itr = limiter.authAttemptsByPeer.begin(); itr != limiter.authAttemptsByPeer.end();)
        {
            PruneWorldAuthAttempts(itr->second, now, window);
            if (itr->second.empty())
                itr = limiter.authAttemptsByPeer.erase(itr);
            else
                ++itr;
        }
    }

    bool AcquireWorldPreAuthConnection(std::string const& peerAddress)
    {
        WorldAuthLimiter& limiter = GetWorldAuthLimiter();
        std::lock_guard<std::mutex> guard(limiter.lock);

        uint32 const perIpLimit = GetNonNegativeWorldConfig("Network.MaxPreAuthConnectionsPerIp", 8);
        uint32 const processLimit = GetNonNegativeWorldConfig("Network.MaxPreAuthConnections", 256);
        auto const itr = limiter.preauthConnections.find(peerAddress);
        uint32 const perIpConnections = itr == limiter.preauthConnections.end() ? 0 : itr->second;

        if ((perIpLimit && perIpConnections >= perIpLimit) ||
            (processLimit && limiter.totalPreauthConnections >= processLimit))
            return false;

        ++limiter.preauthConnections[peerAddress];
        ++limiter.totalPreauthConnections;
        return true;
    }

    void ReleaseWorldPreAuthConnection(std::string const& peerAddress)
    {
        WorldAuthLimiter& limiter = GetWorldAuthLimiter();
        std::lock_guard<std::mutex> guard(limiter.lock);

        auto itr = limiter.preauthConnections.find(peerAddress);
        if (itr != limiter.preauthConnections.end())
        {
            if (itr->second > 1)
                --itr->second;
            else
                limiter.preauthConnections.erase(itr);
        }

        if (limiter.totalPreauthConnections > 0)
            --limiter.totalPreauthConnections;
    }

    bool BeginWorldAuthAttempt(std::string const& peerAddress)
    {
        WorldAuthLimiter& limiter = GetWorldAuthLimiter();
        std::lock_guard<std::mutex> guard(limiter.lock);
        auto const now = std::chrono::steady_clock::now();
        auto const window = std::chrono::seconds(GetNonNegativeWorldConfig("Network.AuthAttemptWindow", 300));
        CleanupWorldAuthAttempts(limiter, now, window);

        auto peerItr = limiter.authAttemptsByPeer.find(peerAddress);
        if (peerItr != limiter.authAttemptsByPeer.end())
            PruneWorldAuthAttempts(peerItr->second, now, window);

        size_t const peerAttemptCount = peerItr == limiter.authAttemptsByPeer.end() ? 0 : peerItr->second.size();

        uint32 const processAttemptLimit = GetNonNegativeWorldConfig("Network.MaxAuthAttempts", 4096);
        uint32 const perIpAttemptLimit = GetNonNegativeWorldConfig("Network.MaxAuthAttemptsPerIp", 15);
        if ((processAttemptLimit && limiter.authAttempts.size() >= processAttemptLimit) ||
            (perIpAttemptLimit && peerAttemptCount >= perIpAttemptLimit))
            return false;

        limiter.authAttempts.push_back(now);
        if (peerItr == limiter.authAttemptsByPeer.end())
            peerItr = limiter.authAttemptsByPeer.emplace(peerAddress, AttemptQueue()).first;
        peerItr->second.push_back(now);
        return true;
    }

    void LogWorldAuthRejection(std::string const& peerAddress, char const* reason)
    {
        WorldAuthLimiter& limiter = GetWorldAuthLimiter();
        std::lock_guard<std::mutex> guard(limiter.lock);
        static std::chrono::steady_clock::time_point nextReport;
        static uint32 suppressed = 0;
        auto const now = std::chrono::steady_clock::now();
        if (nextReport.time_since_epoch().count() == 0 || now >= nextReport)
        {
            BASIC_LOG("World authentication rejection from '%s': %s (suppressed %u similar rejections in the last minute)",
                      peerAddress.c_str(), reason, suppressed);
            suppressed = 0;
            nextReport = now + std::chrono::seconds(60);
        }
        else
            ++suppressed;
    }
}

template class MangosSocket<WorldSession, WorldSocket, AuthCrypt>;

int WorldSocket::open(void* arg)
{
    ACE_INET_Addr addr;
    if (peer().get_remote_addr(addr) == -1)
        return -1;

    char address[1024];
    addr.get_host_addr(address, sizeof(address));
    _peerAddress = address;

    if (!AcquireWorldPreAuthConnection(_peerAddress))
    {
        LogWorldAuthRejection(_peerAddress, "authentication capacity reached");
        return -1;
    }

    _preAuthConnectionTracked = true;
    if (Base::open(arg) == -1)
    {
        ReleasePreAuthConnection();
        return -1;
    }

    uint32 const timeout = GetNonNegativeWorldConfig("Network.PreAuthTimeout", 15);
    if (timeout)
        _preAuthTimerId = reactor()->schedule_timer(this, &WorldPreAuthTimerTag, ACE_Time_Value(timeout));

    return 0;
}

WorldSocket::~WorldSocket()
{
    CancelPreAuthTimeout();
    ReleasePreAuthConnection();
}

void WorldSocket::ReleasePreAuthConnection()
{
    if (_preAuthConnectionTracked)
    {
        ReleaseWorldPreAuthConnection(_peerAddress);
        _preAuthConnectionTracked = false;
    }
}

void WorldSocket::CancelPreAuthTimeout()
{
    if (reactor() && _preAuthTimerId != -1)
    {
        reactor()->cancel_timer(this, _preAuthTimerId);
        _preAuthTimerId = -1;
    }
}

void WorldSocket::CompleteAuthentication()
{
    CancelPreAuthTimeout();
    ReleasePreAuthConnection();
}

int WorldSocket::handle_close(ACE_HANDLE handle, ACE_Reactor_Mask mask)
{
    CancelPreAuthTimeout();
    ReleasePreAuthConnection();
    return Base::handle_close(handle, mask);
}

int WorldSocket::handle_timeout(const ACE_Time_Value&, const void* act)
{
    if (act == &WorldPreAuthTimerTag)
    {
        _preAuthTimerId = -1;
        ReleasePreAuthConnection();
        CloseSocket();
        // CloseSocket() detaches the session and stops output, but the ACE
        // handler must also be deregistered when the peer remains silent.
        shutdown();
    }

    return 0;
}



int WorldSocket::ProcessIncoming(WorldPacket* new_pct)
{
    MANGOS_ASSERT(new_pct);


    // manage memory ;)
    std::unique_ptr<WorldPacket> aptr(new_pct);

    const ACE_UINT16 opcode = new_pct->GetOpcode();

    if (opcode >= NUM_MSG_TYPES)
    {
        sLog.outError("SESSION: received nonexistent opcode 0x%.4X", opcode);
        return -1;
    }

    if (closing_)
        return -1;

    new_pct->FillPacketTime(WorldTimer::getMSTime());

    // Dump received packet.
    sLog.outWorldPacketDump(get_handle(), new_pct->GetOpcode(),
                            LookupOpcodeName(new_pct->GetOpcode()), new_pct,
                            true);

    try
    {
        switch (opcode)
        {
            case CMSG_PING:
                return HandlePing(*new_pct);
            case CMSG_AUTH_SESSION:
                if (m_Session)
                {
                    sLog.outError("WorldSocket::ProcessIncoming: Player send CMSG_AUTH_SESSION again");
                    return -1;
                }

                return HandleAuthSession(*new_pct);
            default:
            {
                GuardType lock(m_SessionLock);

                if (m_Session != nullptr)
                {
                    // OK ,give the packet to WorldSession
                    aptr.release();
                    // WARNINIG here we call it with locks held.
                    // Its possible to cause deadlock if QueuePacket calls back
                    m_Session->QueuePacket(new_pct);
                    return 0;
                }
                else
                {
                    LogWorldAuthRejection(_peerAddress, "opcode received before authentication");
                    return -1;
                }
            }
        }
    }
    catch (ByteBufferException &)
    {
        if (!m_Session)
            LogWorldAuthRejection(_peerAddress, "malformed pre-auth packet");
        else
            sLog.outError("WorldSocket::ProcessIncoming ByteBufferException occured while parsing an instant handled packet (opcode: %u) from client %s, accountid=%i.",
                          opcode, GetRemoteAddress().c_str(), m_Session ? m_Session->GetAccountId() : -1);
        // Never hex-dump unauthenticated input: even with debug logging
        // enabled, that would turn a malformed-packet flood into synchronous
        // disk I/O proportional to the attacker-controlled payload.
        if (m_Session && sLog.HasLogLevelOrHigher(LOG_LVL_DEBUG))
        {
            DEBUG_LOG("Dumping error-causing packet:");
            new_pct->hexlike();
        }

        if (sWorld.getConfig(CONFIG_BOOL_KICK_PLAYER_ON_BAD_PACKET))
        {
            DETAIL_LOG("Disconnecting session [account id %i / address %s] for badly formatted packet.",
                       m_Session ? m_Session->GetAccountId() : -1, GetRemoteAddress().c_str());

            return -1;
        }
        else
            return 0;
    }

    ACE_NOTREACHED(return 0);
}

int WorldSocket::HandleAuthSession(WorldPacket& recvPacket)
{
    // NOTE: ATM the socket is singlethread, have this in mind ...
    uint8 digest[20];
    uint32 clientSeed;
    uint32 serverId;
    uint32 BuiltNumberClient;
    uint32 id, security;
    LocaleConstant locale;
    std::string account, os, platform;
    BigNumber v, s, g, N, K;
    WorldPacket packet, SendAddonPacked;

    if (!BeginWorldAuthAttempt(_peerAddress))
    {
        packet.Initialize(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_FAILED);
        SendPacket(packet);
        LogWorldAuthRejection(_peerAddress, "authentication attempt limit reached");
        return -1;
    }

    // Read the content of the packet
    recvPacket >> BuiltNumberClient;
    recvPacket >> serverId;
    recvPacket >> account;

    recvPacket >> clientSeed;
    recvPacket.read(digest, 20);

    DEBUG_LOG("WorldSocket::HandleAuthSession: client %u, serverId %u, account %s, clientseed %u",
              BuiltNumberClient,
              serverId,
              account.c_str(),
              clientSeed);

    // Check the version of client trying to connect
    if (!IsAcceptableClientBuild(BuiltNumberClient))
    {
        packet.Initialize(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_VERSION_MISMATCH);

        SendPacket(packet);

        LogWorldAuthRejection(_peerAddress, "client version mismatch");
        return -1;
    }

    // Get the account information from the realmd database
    std::string safe_account = account; // Duplicate, else will screw the SHA hash verification below
    LoginDatabase.escape_string(safe_account);
    // No SQL injection, username escaped.

	QueryResult* result = LoginDatabase.PQuery("SELECT a.id, a.rank, a.sessionkey, a.last_ip, a.locked, a.v, a.s, a.mutetime, a.locale, a.os, a.platform, a.flags, a.email, a.username, UNIX_TIMESTAMP(a.joindate), a.queue_skip, "
		"ab.unbandate > UNIX_TIMESTAMP() OR ab.unbandate = ab.bandate FROM account a "
		"LEFT JOIN account_banned ab ON a.id = ab.id AND ab.active = 1 WHERE a.username = '%s' LIMIT 1", safe_account.c_str());

    // Stop if the account is not found
    if (!result)
    {
        packet.Initialize(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_UNKNOWN_ACCOUNT);

        SendPacket(packet);

        LogWorldAuthRejection(_peerAddress, "unknown account");
        return -1;
    }

    Field* fields = result->Fetch();

    N.SetHexStr("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7");
    g.SetDword(7);

    v.SetHexStr(fields[5].GetString());
    s.SetHexStr(fields[6].GetString());

    const char* sStr = s.AsHexStr();                        //Must be freed by OPENSSL_free()
    const char* vStr = v.AsHexStr();                        //Must be freed by OPENSSL_free()

    DEBUG_LOG("WorldSocket::HandleAuthSession: (s,v) check s: %s v: %s",
              sStr,
              vStr);

    OPENSSL_free((void*) sStr);
    OPENSSL_free((void*) vStr);

    auto const remote_ip = fields[3].GetCppString();

    ///- Re-check ip locking (same check as in realmd).

    //This should always be checked regardless of IP locking.
    //If the last_ip that was just modified by authserver is different than the client sending CMSG_AUTH_SESSION that's never okay.

   /*if (strcmp(remote_ip.c_str(), GetRemoteAddress().c_str()))
    {
        packet.Initialize(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_FAILED);
        SendPacket(packet);

        delete result;
        BASIC_LOG("WorldSocket::HandleAuthSession: Sent Auth Response (Account IP differs).");
        return -1;
    }*/

    id = fields[0].GetUInt32();
    security = sAccountMgr.GetSecurity(id); //fields[1].GetUInt16 ();
    if (security > SEC_SIGMACHAD)                       // prevent invalid security settings in DB
        security = SEC_SIGMACHAD;

    if (sAccountMgr.IsTraineeGM(id))
        security = SEC_DEVELOPER;

    auto str = fields[2].GetString();

    K.SetHexStr(fields[2].GetString());

    auto vec = K.AsByteArray();

    if (K.AsByteArray().empty())
    {
        delete result;
        return -1;
    }

    time_t mutetime = time_t (fields[7].GetUInt64());

    locale = LocaleConstant(fields[8].GetUInt8());
    if (locale >= MAX_LOCALE)
        locale = LOCALE_enUS;
    os = fields[9].GetCppString();
    platform = fields[10].GetCppString();
    uint32 accFlags = fields[11].GetUInt32();
    std::string email = fields[12].GetCppString();
    std::string username = fields[13].GetCppString();
    uint32 joinTimestamp = fields[14].GetUInt32();
    bool canQueueSkip = fields[15].GetBool();
    bool isBanned = fields[16].GetBool();
    delete result;

    
    if (isBanned || sAccountMgr.IsIPBanned(GetRemoteAddress()))
    {
        packet.Initialize(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_BANNED);
        SendPacket(packet);

        LogWorldAuthRejection(_peerAddress, "banned account or address");
        return -1;
    }

    // Check locked state for server
    AccountTypes allowedAccountType = sWorld.GetPlayerSecurityLimit();

    if (allowedAccountType > SEC_PLAYER && AccountTypes(security) < allowedAccountType)
    {
        WorldPacket Packet(SMSG_AUTH_RESPONSE, 1);
        Packet << uint8(AUTH_UNAVAILABLE);

        SendPacket(packet);

        LogWorldAuthRejection(_peerAddress, "account security level is not permitted");
        return -1;
    }

    // Check that Key and account name are the same on client and server
    Sha1Hash sha;

    uint32 t = 0;
    uint32 seed = m_Seed;

    sha.UpdateData(account);
    sha.UpdateData((uint8 *) & t, 4);
    sha.UpdateData((uint8 *) & clientSeed, 4);
    sha.UpdateData((uint8 *) & seed, 4);
    sha.UpdateBigNumbers(&K, nullptr);
    sha.Finalize();

    if (memcmp(sha.GetDigest(), digest, 20))
    {
        packet.Initialize(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_FAILED);

        SendPacket(packet);

        LogWorldAuthRejection(_peerAddress, "authentication proof failed");
        return -1;
    }

    std::string address = GetRemoteAddress();

    DEBUG_LOG("WorldSocket::HandleAuthSession: Client '%s' authenticated successfully from %s.",
              account.c_str(),
              address.c_str());

    ClientOSType clientOs;
    if (os == "niW")
        clientOs = CLIENT_OS_WIN;
    else if (os == "XSO")
        clientOs = CLIENT_OS_MAC;
    else
    {
        LogWorldAuthRejection(_peerAddress, "unrecognized client operating system");
        return -1;
    }

    ClientPlatformType clientPlatform;
    if (platform == "68x")
        clientPlatform = CLIENT_PLATFORM_X86;
    else if (platform == "CPP" && clientOs == CLIENT_OS_MAC)
        clientPlatform = CLIENT_PLATFORM_PPC;
    else
    {
        LogWorldAuthRejection(_peerAddress, "unrecognized client platform");
        return -1;
    }

    // NOTE ATM the socket is single-threaded, have this in mind ...
    ACE_NEW_RETURN(m_Session, WorldSession(id, this, AccountTypes(security), mutetime, locale, remote_ip, m_BinaryAddress), -1);

    m_Crypt.SetKey(K.AsByteArray());
    m_Crypt.Init();

    m_Session->SetShouldBackupCharacters(sAccountMgr.UpdateAccountIP(id, GetRemoteAddress()));
    m_Session->SetJoinTimeStamp(joinTimestamp);
    m_Session->SetUsername(account);
    m_Session->SetEmail(email);
    m_Session->SetGameBuild(BuiltNumberClient);
    m_Session->SetAccountFlags(accFlags);
    m_Session->SetOS(clientOs);
    m_Session->SetPlatform(clientPlatform);
    m_Session->LoadTutorialsData();
    m_Session->SetQueueSkip(canQueueSkip);
    m_Session->InitAntiCheatSession(&K);


    //TWoW Jamey
    //Clear out sessionkey once we're done with it. Keep it in memory only.
    //Ideally sessionkey shouldn't even be in the DB. It's only used for auth to communicate to world.
    //Exposing it leaves a big security hole as it allows free login.
    //Should be sent over IPC / MMAP. 


    //LoginDatabase.DirectPExecute("UPDATE `account` SET `sessionkey` = '' WHERE `username` = '%s'", safe_account.c_str());

    //m_Session->InitWarden(&K);

    // In case needed sometime the second arg is in microseconds 1 000 000 = 1 sec
    ACE_OS::sleep(ACE_Time_Value(0, 10000));

    // just refresh always..
    auto accountData = sWorld.GetAccountData(id);
    accountData->id = id;
    accountData->email = email;
    accountData->username = username;

    sWorld.AddSession(m_Session);

    // when false, the client sent invalid addon data.  kick!
    WorldPacket addonPacket;
    if (!m_Session->GetAntiCheat()->ReadAddonInfo(&recvPacket, addonPacket))
    {
        sLog.out(LOG_ANTICHEAT_BASIC, "WorldSocket::HandleAuthSession: Account %s (id %u) IP %s sent bad addon info.  Kicking.",
            account.c_str(), id, GetRemoteAddress().c_str());
        return -1;
    }

    // if anything was written to the packet, send it
    if (addonPacket.wpos())
        SendPacket(addonPacket);

    CompleteAuthentication();

    return 0;
}

int WorldSocket::HandlePing(WorldPacket& recvPacket)
{
    uint32 ping;
    uint32 latency;

    // Get the ping packet content
    recvPacket >> ping;
    recvPacket >> latency;

    if (m_LastPingTime == ACE_Time_Value::zero)
        m_LastPingTime = ACE_OS::gettimeofday();  // for 1st ping
    else
    {
        ACE_Time_Value cur_time = ACE_OS::gettimeofday();
        ACE_Time_Value diff_time(cur_time);
        diff_time -= m_LastPingTime;
        m_LastPingTime = cur_time;

        if (diff_time < ACE_Time_Value(27))
        {
            ++m_OverSpeedPings;

            uint32 max_count = sWorld.getConfig(CONFIG_UINT32_MAX_OVERSPEED_PINGS);

            if (max_count && m_OverSpeedPings > max_count)
            {
                GuardType lock(m_SessionLock);

                if (m_Session && m_Session->GetSecurity() == SEC_PLAYER)
                {
                    sLog.outError("WorldSocket::HandlePing: Player kicked for "
                                  "overspeeded pings address = %s",
                                  GetRemoteAddress().c_str());

                    return -1;
                }
            }
        }
        else
            m_OverSpeedPings = 0;
    }

    // critical section
    {
        GuardType lock(m_SessionLock);

        if (m_Session)
            m_Session->SetLatency(latency);
        else
        {
            LogWorldAuthRejection(_peerAddress, "ping received before authentication");
            return -1;
        }
    }

    WorldPacket packet(SMSG_PONG, 4);
    packet << ping;
    return SendPacket(packet);
}

int WorldSocket::OnSocketOpen()
{
    return sWorldSocketMgr->OnSocketOpen(this);
}

int WorldSocket::SendStartupPacket()
{
    // Send startup packet.
    WorldPacket packet(SMSG_AUTH_CHALLENGE, 4);
    packet << m_Seed;

    return SendPacket(packet);
}
