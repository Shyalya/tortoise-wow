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

#include "AuthCodes.h"
#include "Log.h"
#include "Common.h"
#include "Timer.h"
#include "PatchHandler.h"
#include "PatchLimiter.hpp"

#ifdef WIN32
#include <filesystem>
#endif

#include <ace/OS_NS_sys_socket.h>
#include <ace/OS_NS_dirent.h>
#include <ace/OS_NS_errno.h>
#include <ace/OS_NS_unistd.h>


#include <ace/os_include/netinet/os_tcp.h>

#include "Policies/SingletonImp.h"
#include "Policies/ThreadingModel.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif

PatchLimiter sPatchLimiter;

extern std::atomic<int32> PatchHandlerKBytesDownloadLimit;

static int GetPatchLimit(char const* name, int defaultValue, int minimum, int maximum)
    {
        int32 const value = sConfig.GetIntDefault(name, defaultValue);
        return value < minimum || value > maximum ? defaultValue : value;
    }

class PatchTransferPool
    {
    public:
        bool Enqueue(PatchHandler* handler)
        {
            if (!handler)
                return false;

            bool startFailed = false;
            {
                std::lock_guard<std::mutex> guard(lock_);
                if (stopping_)
                    return false;

                if (!started_)
                {
                    maxTransfers_ = static_cast<size_t>(GetPatchLimit("Patches.MaxActiveTransfers", 4, 1, 64));
                    maxTransfersPerIp_ = static_cast<size_t>(GetPatchLimit("Patches.MaxTransfersPerIp", 2, 0, 64));
                    if (!StartWorkersLocked())
                        startFailed = true;
                }

                if (!startFailed && (!started_ || transferSlots_ >= maxTransfers_))
                    return false;

                if (!startFailed)
                {
                    uint32 const peerCount = peerTransfers_[handler->peer_address_];
                    if (maxTransfersPerIp_ && peerCount >= maxTransfersPerIp_)
                        return false;

                    ++peerTransfers_[handler->peer_address_];
                    ++transferSlots_;
                    jobs_.push_back(handler);
                }
            }

            if (startFailed)
                JoinWorkers();
            else
                condition_.notify_one();

            return !startFailed;
        }

        bool IsStopping() const
        {
            return stopping_.load(std::memory_order_acquire);
        }

        void Shutdown()
        {
            std::deque<PatchHandler*> abandoned;
            {
                std::lock_guard<std::mutex> guard(lock_);
                stopping_.store(true, std::memory_order_release);
                abandoned.swap(jobs_);
            }

            for (PatchHandler* handler : abandoned)
            {
                ReleaseSlot(handler);
                delete handler;
            }

            condition_.notify_all();
            JoinWorkers();
        }

    private:
        bool StartWorkersLocked()
        {
            try
            {
                for (size_t i = 0; i < maxTransfers_; ++i)
                    workers_.emplace_back([this]() { Worker(); });
                started_ = true;
                return true;
            }
            catch (...)
            {
                stopping_.store(true, std::memory_order_release);
                condition_.notify_all();
                return false;
            }
        }

        void Worker()
        {
            for (;;)
            {
                PatchHandler* handler = nullptr;
                {
                    std::unique_lock<std::mutex> guard(lock_);
                    condition_.wait(guard, [this]() { return stopping_ || !jobs_.empty(); });

                    if (jobs_.empty())
                    {
                        if (stopping_)
                            return;
                        continue;
                    }

                    handler = jobs_.front();
                    jobs_.pop_front();
                }

                if (handler)
                {
                    try
                    {
                        handler->svc();
                    }
                    catch (...)
                    {
                        // Always release the transfer slot and both handles
                        // below, even if an I/O wrapper unexpectedly throws.
                    }

                    ReleaseSlot(handler);
                }

                delete handler;
            }
        }

        void JoinWorkers()
        {
            for (std::thread& worker : workers_)
            {
                if (worker.joinable())
                    worker.join();
            }
            workers_.clear();
        }

        void ReleaseSlot(PatchHandler* handler)
        {
            std::lock_guard<std::mutex> guard(lock_);
            if (transferSlots_ > 0)
                --transferSlots_;

            if (!handler)
                return;

            auto itr = peerTransfers_.find(handler->peer_address_);
            if (itr != peerTransfers_.end())
            {
                if (itr->second > 1)
                    --itr->second;
                else
                    peerTransfers_.erase(itr);
            }
        }

        std::mutex lock_;
        std::condition_variable condition_;
        std::deque<PatchHandler*> jobs_;
        std::unordered_map<std::string, uint32> peerTransfers_;
        std::vector<std::thread> workers_;
        std::atomic_bool stopping_{ false };
        bool started_ = false;
        size_t maxTransfers_ = 0;
        size_t maxTransfersPerIp_ = 0;
        size_t transferSlots_ = 0;
};

static PatchTransferPool& GetPatchTransferPool()
    {
        // The pool is explicitly stopped by realmd before process shutdown.
        // Keeping the object alive until then avoids static-destruction order
        // problems with ACE and the logger.
        static PatchTransferPool* pool = new PatchTransferPool();
        return *pool;
}

struct Chunk
{
    ACE_UINT8 cmd;
    ACE_UINT16 data_size;
    ACE_UINT8 data[4096]; // 4096 - page size on most arch
};

#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif

PatchHandler::PatchHandler(ACE_HANDLE socket, ACE_HANDLE patch, std::string peerAddress)
{
    reactor(nullptr);
    set_handle(socket);
    patch_fd_ = patch;
    peer_address_ = std::move(peerAddress);

}

PatchHandler::~PatchHandler()
{
    if(patch_fd_ != ACE_INVALID_HANDLE)
        ACE_OS::close(patch_fd_);
}

int PatchHandler::open(void*)
{
    if(get_handle() == ACE_INVALID_HANDLE || patch_fd_ == ACE_INVALID_HANDLE)
        return -1;

    int nodelay = 0;
    if (-1 == peer().set_option(ACE_IPPROTO_TCP,
                TCP_NODELAY,
                &nodelay,
                sizeof(nodelay)))
    {
        return -1;
    }

#if defined(TCP_CORK)
    int cork = 1;
    if (-1 == peer().set_option(ACE_IPPROTO_TCP,
                TCP_CORK,
                &cork,
                sizeof(cork)))
    {
        return -1;
    }
#endif //TCP_CORK

    if (peer().enable(ACE_NONBLOCK) == -1)
        return -1;

    return GetPatchTransferPool().Enqueue(this) ? 0 : -1;
}

int PatchHandler::svc(void)
{
    // Do 1 second sleep, similar to the one in game/WorldSocket.cpp
    // Seems client have problems with too fast sends.
    ACE_OS::sleep(1);

    int flags = MSG_NOSIGNAL | MSG_DONTWAIT;

    auto const transferStart = std::chrono::steady_clock::now();
    auto lastProgress = transferStart;
    auto const idleTimeout = std::chrono::seconds(GetPatchLimit("Patches.IdleTimeout", 15, 1, 3600));
    auto const totalTimeout = std::chrono::seconds(GetPatchLimit("Patches.MaxTransferSeconds", 300, 1, 86400));
    auto windowStart = transferStart;
    uint64_t bytesSentInWindow = 0;

    Chunk data;
    data.cmd = CMD_XFER_DATA;

    ssize_t r = 0;

    while(!GetPatchTransferPool().IsStopping() &&
          (r = ACE_OS::read(patch_fd_, data.data, sizeof(data.data))) > 0)
    {
        data.data_size = (ACE_UINT16)r;

        auto size = ((size_t)r) + sizeof(data) - sizeof(data.data);
        size_t offset = 0;
        while (offset < size && !GetPatchTransferPool().IsStopping())
        {
            auto const now = std::chrono::steady_clock::now();
            if (now - transferStart >= totalTimeout || now - lastProgress >= idleTimeout)
                return -1;

            auto const currentTime = std::chrono::steady_clock::now();
            auto const elapsed = currentTime - windowStart;
            if (elapsed >= std::chrono::seconds(1))
            {
                windowStart = currentTime;
                bytesSentInWindow = 0;
            }

            uint64_t const bytesPerWindow = static_cast<uint64_t>(std::max<int32>(PatchHandlerKBytesDownloadLimit.load(), 1)) * 1024;
            if (bytesSentInWindow >= bytesPerWindow)
            {
                auto const remaining = std::chrono::seconds(1) -
                    std::chrono::duration_cast<std::chrono::seconds>(currentTime - windowStart);
                if (remaining > std::chrono::seconds(0))
                {
                    ACE_Time_Value sleepValue;
                    sleepValue.set_msec(100u);
                    ACE_OS::sleep(sleepValue);
                    continue;
                }

                windowStart = std::chrono::steady_clock::now();
                bytesSentInWindow = 0;
            }

            size_t const requested = std::min<size_t>(size - offset,
                std::min<uint64_t>(bytesPerWindow - bytesSentInWindow, 1024));
            if (!sPatchLimiter.IsAllowed(static_cast<uint32>(requested)))
            {
                ACE_Time_Value sleepValue;
                sleepValue.set_msec(100u);
                ACE_OS::sleep(sleepValue);
                continue;
            }

            ssize_t const sent = ACE_OS::send(get_handle(), reinterpret_cast<char const*>(&data) + offset, requested, flags);
            if (sent > 0)
            {
                offset += static_cast<size_t>(sent);
                bytesSentInWindow += static_cast<size_t>(sent);
                lastProgress = std::chrono::steady_clock::now();
                continue;
            }

            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            {
                ACE_Time_Value sleepValue;
                sleepValue.set_msec(10u);
                ACE_OS::sleep(sleepValue);
                continue;
            }

            return -1;
        }
    }

    if(r == -1)
    {
        return -1;
    }

    return 0;
}

void PatchHandler::ShutdownPool()
{
    GetPatchTransferPool().Shutdown();
}

PatchCache::~PatchCache()
{
    for (Patches::iterator i = patches_.begin (); i != patches_.end (); i++)
        delete i->second;
}

PatchCache::PatchCache()
{
    LoadPatchesInfo();
}

using PatchCacheLock = MaNGOS::ClassLevelLockable<PatchCache, std::mutex>;
INSTANTIATE_SINGLETON_2(PatchCache, PatchCacheLock);
INSTANTIATE_CLASS_MUTEX(PatchCache, std::mutex);

PatchCache* PatchCache::instance()
{
    return &MaNGOS::Singleton<PatchCache, PatchCacheLock>::Instance();
}

void PatchCache::LoadPatchMD5(const char* szFileName)
{
    // Try to open the patch file
    std::string path = szFileName;
    FILE* pPatch = fopen(path.c_str(), "rb");
    DEBUG_LOG("Loading patch info from file %s", path.c_str());

    if(!pPatch)
        return;

    // Calculate the MD5 hash
    MD5_CTX ctx;
    MD5_Init(&ctx);

    const size_t check_chunk_size = 4*1024;

    ACE_UINT8 buf[check_chunk_size];

    while(!feof (pPatch))
    {
        size_t read = fread(buf, 1, check_chunk_size, pPatch);
        MD5_Update(&ctx, buf, read);
    }

    fclose(pPatch);

    // Store the result in the internal patch hash map
    patches_[path] = new PATCH_INFO;
    MD5_Final((ACE_UINT8 *) & patches_[path]->md5, &ctx);
}

bool PatchCache::GetHash(const char * pat, ACE_UINT8 mymd5[MD5_DIGEST_LENGTH])
{
    for (Patches::iterator i = patches_.begin (); i != patches_.end (); i++)
        if (!stricmp(pat, i->first.c_str ()))
        {
            memcpy(mymd5, i->second->md5, MD5_DIGEST_LENGTH);
            return true;
        }

    return false;
}

#ifdef WIN32
#define fssystem std::filesystem
#endif

void PatchCache::LoadPatchesInfo()
{
#ifdef WIN32
	fssystem::path PatchesDir = "./patches/";

	if (!fssystem::exists(PatchesDir))
	{
		return;
	}

	fssystem::directory_iterator iter(PatchesDir);

	for (const fssystem::directory_entry& DirEntry : fssystem::directory_iterator(PatchesDir))
	{
		const fssystem::path& filePath = DirEntry.path();
		fssystem::path clearFilename = filePath.filename();
		std::string strClearFilename = clearFilename.string();

		if (strClearFilename.size() < 8)
		{
			continue;
		}

		if (clearFilename.extension().compare("mpq"))
		{
			LoadPatchMD5(strClearFilename.c_str());
		}
	}
#else
    std::string path = sConfig.GetStringDefault("PatchesDir", "./patches") + "/";
    std::string fullpath;
    ACE_DIR* dirp = ACE_OS::opendir(ACE_TEXT(path.c_str()));
    DEBUG_LOG("Loading patch info from folder %s", path.c_str());

	if (!dirp)
		return;

	ACE_DIRENT* dp;

	while ((dp = ACE_OS::readdir(dirp)) != nullptr)
	{
		int l = strlen(dp->d_name);
		if (l < 8)
			continue;

        if (!memcmp(&dp->d_name[l - 4], ".mpq", 4))
        {
            fullpath = path + dp->d_name;
            LoadPatchMD5(fullpath.c_str());
}
	}

	ACE_OS::closedir(dirp);
#endif
}
