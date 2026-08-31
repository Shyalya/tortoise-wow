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

/** \file
  \ingroup realmd
  */

#include "BufferedSocket.h"
#include "Config/Config.h"

#include <ace/OS_NS_string.h>
#include <ace/INET_Addr.h>
#include <ace/SString.h>

#include <cstdint>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace
{
    char const SessionTimerTag = 0;
    char const PreAuthTimerTag = 0;
}

BufferedSocket::BufferedSocket(void):
    input_buffer_(4096),
    remote_address_("<unknown>"),
    peer_address_("<unknown>"),
    session_timer_id_(-1),
    preauth_timer_id_(-1),
    close_notified_(false),
    handoff_pending_(false)
{
}

/*virtual*/ BufferedSocket::~BufferedSocket(void)
{
    cancel_timers();
}

/*virtual*/ int BufferedSocket::open(void * arg)
{
    if(Base::open(arg) == -1)
        return -1;

    if (time_t timer = sConfig.GetIntDefault("MaxSessionDuration", 300))
    {
        ACE_Time_Value interval(timer);
        session_timer_id_ = Base::reactor_timer_interface()->schedule_timer(this, &SessionTimerTag, interval);
    }

    if (time_t timer = sConfig.GetIntDefault("Auth.PreAuthTimeout", 15))
    {
        ACE_Time_Value interval(timer);
        preauth_timer_id_ = Base::reactor_timer_interface()->schedule_timer(this, &PreAuthTimerTag, interval);
    }

    ACE_INET_Addr addr;

    if(peer().get_remote_addr(addr) == -1)
        return -1;

    char address[1024];

    addr.get_host_addr(address, 1024);

    this->peer_address_ = address;
    this->remote_address_ = this->peer_address_;

    this->OnAccept();

    return 0;
}

const std::string& BufferedSocket::get_remote_address(void) const
{
    return this->remote_address_;
}

const std::string& BufferedSocket::get_peer_address(void) const
{
    return this->peer_address_;
}

ACE_HANDLE BufferedSocket::detach_handle(void)
{
    cancel_timers();

    ACE_HANDLE handle = get_handle();
    if (reactor())
        reactor()->remove_handler(this, ACE_Event_Handler::DONT_CALL | ACE_Event_Handler::ALL_EVENTS_MASK);

    set_handle(ACE_INVALID_HANDLE);
    handoff_pending_ = true;
    notify_close();
    return handle;
}

size_t BufferedSocket::recv_len(void) const
{
    return this->input_buffer_.length();
}

bool BufferedSocket::recv_soft(char *buf, size_t len)
{
    if(this->input_buffer_.length() < len)
        return false;

    ACE_OS::memcpy(buf, this->input_buffer_.rd_ptr(), len);

    return true;
}

bool BufferedSocket::recv(char *buf, size_t len)
{
    bool ret = this->recv_soft(buf, len);

    if(ret)
        this->recv_skip(len);

    return ret;
}

void BufferedSocket::recv_skip(size_t len)
{
    this->input_buffer_.rd_ptr(len);
}

ssize_t BufferedSocket::noblk_send(ACE_Message_Block &message_block)
{
    const size_t len = message_block.length();

    if(len == 0)
        return -1;

    // Try to send the message directly.
    ssize_t n = this->peer().send(message_block.rd_ptr(), len, MSG_NOSIGNAL);

    if(n < 0)
    {
        if(errno == EWOULDBLOCK)
            // Blocking signal
            return 0;
        else
            // Error
            return -1;
    }
    else if(n == 0)
    {
        // Can this happen ?
        return -1;
    }

    // return bytes transmitted
    return n;
}

bool BufferedSocket::send(const char *buf, size_t len)
{
    if(buf == nullptr || len == 0)
        return true;

    ACE_Data_Block db(
            len,
            ACE_Message_Block::MB_DATA,
            (const char*)buf,
            0,
            0,
            ACE_Message_Block::DONT_DELETE,
            0);

    ACE_Message_Block message_block(
            &db,
            ACE_Message_Block::DONT_DELETE,
            0);

    message_block.wr_ptr(len);

    if(this->msg_queue()->is_empty())
    {
        // Try to send it directly.
        ssize_t n = this->noblk_send(message_block);

        if(n < 0)
            return false;
        else if(n == len)
            return true;

        // adjust how much bytes we sent
        message_block.rd_ptr((size_t)n);

        // fall down
    }

    // enqueue the message, note: clone is needed cause we cant enqueue stuff on the stack
    ACE_Message_Block *mb = message_block.clone();

    if(this->msg_queue()->enqueue_tail(mb, (ACE_Time_Value *) &ACE_Time_Value::zero) == -1)
    {
        mb->release();
        return false;
    }

    // tell reactor to call handle_output() when we can send more data
    return this->reactor()->schedule_wakeup(this, ACE_Event_Handler::WRITE_MASK) != -1;
}

/*virtual*/ int BufferedSocket::handle_output(ACE_HANDLE /*= ACE_INVALID_HANDLE*/)
{
    ACE_Message_Block *mb = 0;

    if(this->msg_queue()->is_empty())
    {
        // if no more data to send, then cancel notification
        this->reactor()->cancel_wakeup(this, ACE_Event_Handler::WRITE_MASK);
        return 0;
    }

    if(this->msg_queue()->dequeue_head(mb, (ACE_Time_Value *) &ACE_Time_Value::zero) == -1)
        return -1;

    ssize_t n = this->noblk_send(*mb);

    if(n < 0)
    {
        mb->release();
        return -1;
    }
    else if(n == mb->length())
    {
        mb->release();
        return 1;
    }
    else
    {
        mb->rd_ptr(n);

        if(this->msg_queue()->enqueue_head(mb, (ACE_Time_Value *) &ACE_Time_Value::zero) == -1)
        {
            mb->release();
            return -1;
        }

        return 0;
    }

    ACE_NOTREACHED(return -1);
}

/*virtual*/ int BufferedSocket::handle_input(ACE_HANDLE /*= ACE_INVALID_HANDLE*/)
{
    const ssize_t space = this->input_buffer_.space();

    ssize_t n = this->peer().recv(this->input_buffer_.wr_ptr(), space);

    if(n < 0)
    {
        // blocking signal or error
        return errno == EWOULDBLOCK ? 0 : -1;
    }
    else if(n == 0)
    {
        // EOF
        return -1;
    }

    this->input_buffer_.wr_ptr((size_t)n);

    this->OnRead();

    if (handoff_pending_)
    {
        // OnRead may transfer the socket to another owner. Destroy this
        // handler only after the callback has returned and before touching
        // any more members.
        this->handle_close();
        return 0;
    }

    // move data in the buffer to the beginning of the buffer
    this->input_buffer_.crunch();

    // return 1 in case there might be more data to read from OS
    return n == space ? 1 : 0;
}

int BufferedSocket::handle_close(ACE_HANDLE /*h*/, ACE_Reactor_Mask /*m*/)
{
    cancel_timers();
    notify_close();

    Base::handle_close();

    return 0;
}

int BufferedSocket::handle_timeout(const ACE_Time_Value& current_time, const void* act)
{
    ACE_UNUSED_ARG(current_time);

    if (act == &PreAuthTimerTag)
        preauth_timer_id_ = -1;
    else if (act == &SessionTimerTag || act == nullptr)
        session_timer_id_ = -1;

    this->close_connection();
    notify_close();

    Base::handle_close();

    return 0;
}

void BufferedSocket::close_connection(void)
{
    cancel_timers();
    this->peer().close_reader();
    this->peer().close_writer();

    reactor()->remove_handler(this, ACE_Event_Handler::DONT_CALL | ACE_Event_Handler::ALL_EVENTS_MASK);
    notify_close();
}

void BufferedSocket::complete_preauth(void)
{
    if (reactor() && preauth_timer_id_ != -1)
    {
        reactor()->cancel_timer(this, preauth_timer_id_);
        preauth_timer_id_ = -1;
    }
}

void BufferedSocket::cancel_timers(void)
{
    if (!reactor())
        return;

    if (session_timer_id_ != -1)
    {
        reactor()->cancel_timer(this, session_timer_id_);
        session_timer_id_ = -1;
    }

    if (preauth_timer_id_ != -1)
    {
        reactor()->cancel_timer(this, preauth_timer_id_);
        preauth_timer_id_ = -1;
    }
}

void BufferedSocket::notify_close(void)
{
    if (!close_notified_)
    {
        close_notified_ = true;
        this->OnClose();
    }
}
