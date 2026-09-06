#include "BotTrainerIndex.h"
#include "BoundedBotThrottle.h"
#include <algorithm>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

static void Check(bool value) { if (!value) throw std::runtime_error("upstream integration invariant failed"); }
int main()
{
    BotTrainerIndex trainers;
    trainers.Update(9, 1, false, true);
    trainers.Update(3, 8, false, true);
    trainers.Update(7, 0, true, true);
    trainers.Update(10000000, 0, false, false); // sparse non-trainer cannot inflate login work
    Check(trainers.Snapshot(1) == std::vector<std::uint32_t>({7,9}));
    Check(trainers.Snapshot(8) == std::vector<std::uint32_t>({3,7}));
    auto old = trainers.Snapshot(1);
    trainers.Update(9, 8, false, true); // template reload changes class
    Check(trainers.Snapshot(1) == std::vector<std::uint32_t>({7}));
    Check(old == std::vector<std::uint32_t>({7,9}));
    trainers.Update(7, 0, false, false); // no longer a trainer
    Check(trainers.Snapshot(1).empty());
    std::atomic<bool> stop{false};
    std::thread reader([&] {
        while (!stop.load())
        {
            auto ids = trainers.Snapshot(8);
            Check(std::is_sorted(ids.begin(), ids.end()));
            Check(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
        }
    });
    for (unsigned i=0; i<6000; ++i) trainers.Update(i+20, 8, false, true);
    stop.store(true); reader.join();
    Check(trainers.Snapshot(8).size() == 6002);

    BoundedBotThrottle throttle;
    Check(throttle.Allow(1, 0, 100)); // zero is a valid timestamp, not an empty sentinel
    Check(!throttle.Allow(1, 0, 100));
    Check(!throttle.Allow(1, 99, 100));
    Check(throttle.Allow(1, 100, 100));
    Check(throttle.Allow(1, 101, 100, true)); // standing bot can immediately reissue movement
    Check(throttle.Allow(2, UINT32_MAX-9, 30));
    Check(!throttle.Allow(2, 10, 30));
    Check(throttle.Allow(2, 20, 30)); // timer wrap
    std::atomic<unsigned> admitted{0};
    std::vector<std::thread> workers;
    for (unsigned i=0; i<8; ++i)
        workers.emplace_back([&] { for (unsigned n=0; n<2000; ++n) if (throttle.Allow(3, 1000, 100)) ++admitted; });
    for (auto& worker : workers) worker.join();
    Check(admitted.load() == 1);
    for (unsigned i=0; i<70000; ++i) Check(throttle.Allow(i+100, 2000, 100));
    Check(throttle.Allow(100, 3000, 100)); // capacity churn does not wedge retries
    std::cout << "PASS: indexed trainer reload/class selection, parallel snapshots, atomic bounded retry throttles\n";
}
