#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;
using PartitionedTravelList = std::vector<int>;
using FutureDestinations = std::future<PartitionedTravelList>;
#include "TravelPending.inc"

void ResetFuture(FutureDestinations* stored)
{
#define AI_VALUE(type, name) stored
#include "TravelReset.inc"
#undef AI_VALUE
}

struct TravelMgr
{
    std::mutex getDestinationMutex;
    std::condition_variable getDestinationVar;
    unsigned availableDestinationWorkers = 5;
    void GetPartitionsLock(bool getLock = true);
} sTravelMgr;
#include "TravelPermit.inc"

void Search(bool fail, std::shared_future<void> release = {})
{
#include "TravelPermitScope.inc"
    if (release.valid()) release.wait();
    if (fail) throw std::runtime_error("search failure");
}

enum class TravelStatus { TRAVEL_STATUS_NONE, TRAVEL_STATUS_PREPARE };
struct Target
{
    TravelStatus status = TravelStatus::TRAVEL_STATUS_PREPARE;
    void SetStatus(TravelStatus value) { status = value; }
};
struct Context { int cleared = 0; void ClearValues(const char*) { ++cleared; } };
struct Bot { unsigned GetGUIDLow() { return 123; } };
struct Log
{
    int errors = 0;
    void outError(const char*, unsigned, const char*) { ++errors; }
} sLog;
bool Consume(FutureDestinations* futureDestinations, Target* travelTarget, Context* context, Bot* bot)
{
#include "TravelResult.inc"
    return true;
}
void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

int main()
{
    FutureDestinations empty;
    Require(!IsTravelSearchPending(empty), "invalid future is not pending");
    ResetFuture(&empty);

    // Actual std::async ownership: the old reset blocks here. Keep the producer
    // releasable even on failure so the regression reports instead of hanging.
    std::promise<void> producerRelease;
    auto release = producerRelease.get_future().share();
    auto future = std::async(std::launch::async, [release] {
        release.wait();
        return PartitionedTravelList{7};
    });
    Require(IsTravelSearchPending(future), "pending producer must reject replacement");
    auto reset = std::async(std::launch::async, [&future] { ResetFuture(&future); });
    const bool resetNonBlocking = reset.wait_for(200ms) == std::future_status::ready;
    producerRelease.set_value();
    reset.get();
    Require(resetNonBlocking, "reset joined unfinished search");
    future.wait();
    Require(!IsTravelSearchPending(future), "completed producer must allow replacement");
    ResetFuture(&future);
    Require(!future.valid(), "completed stale result must be discarded");

    // An exceptional worker must not permanently consume one of the five slots.
    for (unsigned i = 0; i != 12; ++i)
    {
        try { Search(true); } catch (const std::runtime_error&) {}
        Require(sTravelMgr.availableDestinationWorkers == 5, "exception leaked permit");
    }
    Search(false);
    Require(sTravelMgr.availableDestinationWorkers == 5, "success leaked permit");
    for (unsigned i = 0; i != 5; ++i) sTravelMgr.GetPartitionsLock();
    std::promise<void> attempted;
    auto sixth = std::async(std::launch::async, [&attempted] {
        attempted.set_value();
        Search(false);
    });
    attempted.get_future().wait();
    const bool limited = sixth.wait_for(100ms) == std::future_status::timeout;
    sTravelMgr.GetPartitionsLock(false);
    const bool resumed = sixth.wait_for(2s) == std::future_status::ready;
    for (unsigned i = 0; i != 4; ++i) sTravelMgr.GetPartitionsLock(false);
    sixth.get();
    Require(limited && resumed, "native concurrency limit/wakeup changed");
    Require(sTravelMgr.availableDestinationWorkers == 5, "wake cycle leaked permit");

    Target target;
    Context context;
    Bot bot;
    auto failed = std::async(std::launch::async, []() -> PartitionedTravelList {
        Search(true);
        return {};
    });
    failed.wait();
    Require(!Consume(&failed, &target, &context, &bot), "failed result accepted");
    Require(!failed.valid() && target.status == TravelStatus::TRAVEL_STATUS_NONE &&
        context.cleared == 1 && sLog.errors == 1, "failed result not consumed/logged/reset");
    auto good = std::async(std::launch::async, [] { return PartitionedTravelList{9}; });
    good.wait();
    Require(Consume(&good, &target, &context, &bot), "successful retry rejected");
    Require(sLog.errors == 1, "successful retry logged failure");
    std::cout << "Native travel reset, permit failure/wakeup and result retry checks passed\n";
}
