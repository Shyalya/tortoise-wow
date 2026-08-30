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

/** \addtogroup u2w User to World Communication
 * @{
 * \file WorldSocket.h
 * \author Derex <derex101@gmail.com>
 */

#ifndef _WORLDSOCKET_H
#define _WORLDSOCKET_H
#include "MangosSocket.h"
#include "Auth/AuthCrypt.h"

template <typename T>
class ReactorRunnable;
template <typename T>
class MangosSocketMgr;

class WorldSocket : public MangosSocket<WorldSession, WorldSocket, AuthCrypt>
{
        using Base = MangosSocket<WorldSession, WorldSocket, AuthCrypt>;

        friend class MangosSocket<WorldSession, WorldSocket, AuthCrypt>;
        friend class MangosSocketMgr<WorldSocket>;
        friend class WorldSocketMgr;
        friend class ReactorRunnable< WorldSocket >;
    public:
        ~WorldSocket() override;

    public:
        int open(void*) override;
        int handle_close(ACE_HANDLE = ACE_INVALID_HANDLE,
            ACE_Reactor_Mask = ACE_Event_Handler::ALL_EVENTS_MASK) override;
        int handle_timeout(const ACE_Time_Value&, const void* = 0) override;

    protected:
        int OnSocketOpen();
        int SendStartupPacket();

        int ProcessIncoming (WorldPacket* new_pct);

        /// Called by ProcessIncoming() on CMSG_AUTH_SESSION.
        int HandleAuthSession (WorldPacket& recvPacket);

        /// Called by ProcessIncoming() on CMSG_PING.
        int HandlePing (WorldPacket& recvPacket);

    private:
        void CompleteAuthentication();
        void ReleasePreAuthConnection();
        void CancelPreAuthTimeout();

        std::string _peerAddress;
        bool _preAuthConnectionTracked = false;
        long _preAuthTimerId = -1;
};

#endif  /* _WORLDSOCKET_H */

/// @}
