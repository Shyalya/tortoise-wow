#include "ChannelBroadcaster.h"

#include "Channel.h"
#include "ChannelMgr.h"

#include <utility>
#include <vector>


ChannelBroadcaster::~ChannelBroadcaster()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_messageQueue.clear();
    m_queuedMessageBytes = 0;
}

void ChannelBroadcaster::EnableSendingMessages()
{
    // EnqueueMessage is always available. This hook marks the point before map
    // workers start, but deliberately performs no channel operation itself.
}

void ChannelBroadcaster::DisableSendingMessages()
{
    // MapManager invokes this on the world thread after waiting for every map
    // worker. ChannelMgr and Channel remain exclusively world-thread owned.
    DrainMessages();
}

bool ChannelBroadcaster::EnqueueMessage(std::string&& message, std::string const& channelName,
    ObjectGuid playerGuid, uint32 language, Team channelTeam, bool skipChecks)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_messageQueue.size() >= MaxQueuedMessages ||
        message.size() > MaxQueuedMessageBytes || channelName.size() > MaxQueuedChannelNameBytes ||
        m_queuedMessageBytes > MaxQueuedBytes - message.size())
    {
        m_droppedMessages.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    m_messageQueue.push_back(ChannelMessage{std::move(message), channelName, playerGuid,
        language, channelTeam, skipChecks});
    m_queuedMessageBytes += m_messageQueue.back().Message.size();
    return true;
}

void ChannelBroadcaster::DrainMessages()
{
    std::vector<ChannelMessage> messages;
    messages.reserve(MaxMessagesPerDrain);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_messageQueue.empty() && messages.size() < MaxMessagesPerDrain)
        {
            m_queuedMessageBytes -= m_messageQueue.front().Message.size();
            messages.push_back(std::move(m_messageQueue.front()));
            m_messageQueue.pop_front();
        }
    }

    for (ChannelMessage const& message : messages)
    {
        ChannelMgr* channelManager = channelMgr(message.ChannelTeam);
        if (!channelManager)
            continue;

        // Never recreate an attacker-controlled custom channel merely because
        // an old queued message still names it. Reserved server channels are
        // safe to lazily create on this world-thread-owned path.
        Channel* targetChannel = nullptr;
        if (ChannelMgr::IsReservedChannelName(message.ChannelName))
            targetChannel = channelManager->GetOrCreateChannel(message.ChannelName);
        else
            targetChannel = channelManager->GetChannel(message.ChannelName, PlayerPointer(), false);
        if (!targetChannel)
            continue;

        targetChannel->Say(message.PlayerGuid, message.Message.c_str(), message.Language, message.bSkipChecks);
    }
}
