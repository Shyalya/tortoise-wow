#include <thread>
#include <chrono>
#include "ChannelBroadcaster.h"
#include "ChannelMgr.h"
#include "World.h"


ChannelBroadcaster::ChannelBroadcaster() : MessageQueue(15)
{
	StartThread();
}

ChannelBroadcaster::~ChannelBroadcaster()
{
	Stop();
}

void ChannelBroadcaster::StartThread()
{
	Worker = new std::thread([this]()
	{
		ThreadProc();
	});
}

void ChannelBroadcaster::Stop()
{
	if (Worker == nullptr)
	{
		return;
	}
	
	if (Worker->joinable())
	{
		Worker->join();
	}

	delete Worker;
	Worker = nullptr;
}

void ChannelBroadcaster::EnableSendingMessages()
{
	bShouldSentMessages.store(true);
	// sleep_for(0) yields rather than waits, so this handshake spun too. It runs
	// at startup and shutdown only, where a millisecond of granularity costs
	// nothing.
	while (!bIsWorking.load() && !sWorld.IsStopped())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

void ChannelBroadcaster::DisableSendingMessages()
{
	bShouldSentMessages.store(false);
	while (bIsWorking.load())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

void ChannelBroadcaster::EnqueueMessage(std::string&& Message, const std::string& ChannelName, ObjectGuid PlayerGuid, uint32 Language, Team ChannelTeam, bool bSkipChecks)
{
	MessageQueue.enqueue(ChannelMessage{std::move(Message), ChannelName, PlayerGuid, Language, ChannelTeam, bSkipChecks });
}

void ChannelBroadcaster::ThreadProc()
{
	while (!sWorld.IsStopped())
	{
		while (bShouldSentMessages.load() && !sWorld.IsStopped())
		{
			bIsWorking.store(true);


			constexpr int32 MessageLimit = 5;
			int32 MessageIterator = 0;


			ChannelMessage msg;
			while (MessageIterator < MessageLimit && MessageQueue.try_dequeue(msg))
			{
				ChannelMessage& ChanMsg = msg;

				ChannelMgr* ChannelManager = channelMgr(ChanMsg.ChannelTeam);
				Channel* TargetChannel = ChannelManager->GetOrCreateChannel(ChanMsg.ChannelName);
				TargetChannel->Say(ChanMsg.PlayerGuid, ChanMsg.Message.c_str(), ChanMsg.Language, ChanMsg.bSkipChecks);
				MessageIterator++;
			}

			// Nothing was waiting. Without this the loop simply asks again, and
			// again, with no sleep and no yield anywhere inside it - the one
			// millisecond below sits outside and is only reached once sending is
			// switched off, which during normal operation never happens. The
			// thread therefore spins for as long as the server is up: measured on
			// a realm with ~990 bots, 1212 seconds of CPU over 1321 seconds of
			// uptime, 91.7% of a core, state R and wchan 0 throughout. That was
			// about a third of everything the process was doing.
			//
			// Sleeping only on an empty queue keeps a busy channel as responsive
			// as before - the wait is skipped entirely whenever there is traffic.
			// A condition variable signalled from EnqueueMessage would be tidier
			// still and would drop even the idle wakeups, but it would have to
			// reach into the enqueue path; this is the smaller change for
			// essentially the same saving.
			if (MessageIterator == 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		bIsWorking.store(false);

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}
