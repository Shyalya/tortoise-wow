#pragma once

#include "SharedDefines.h"
#include "ObjectGuid.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

struct ChannelMessage
{
    std::string Message;
    std::string ChannelName;
    ObjectGuid PlayerGuid;

    uint32 Language;
    Team ChannelTeam;
    bool bSkipChecks;
};

class ChannelBroadcaster
{
    public:
        ChannelBroadcaster() = default;
        ~ChannelBroadcaster();

        // MapManager calls these around asynchronous map updates. Messages are
        // collected while map workers run and drained by the world thread once
        // all of those workers have joined.
        void EnableSendingMessages();
        void DisableSendingMessages();

        // Drop the newest message when the queue is full. Returning false lets
        // future callers apply their own backpressure without changing current
        // call sites or channel protocol behavior.
        bool EnqueueMessage(std::string&& message, std::string const& channelName,
            ObjectGuid playerGuid, uint32 language, Team channelTeam, bool skipChecks);
        uint64 GetDroppedMessageCount() const { return m_droppedMessages.load(std::memory_order_relaxed); }

    private:
        void DrainMessages();

        static constexpr size_t MaxQueuedMessages = 256;
        static constexpr size_t MaxMessagesPerDrain = 32;
        static constexpr size_t MaxQueuedMessageBytes = 4096;
        static constexpr size_t MaxQueuedBytes = MaxQueuedMessages * MaxQueuedMessageBytes;
        static constexpr size_t MaxQueuedChannelNameBytes = 64;

        std::mutex m_queueMutex;
        std::deque<ChannelMessage> m_messageQueue;
        size_t m_queuedMessageBytes = 0;
        std::atomic<uint64> m_droppedMessages{0};
};
