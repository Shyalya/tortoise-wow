#pragma once
#include <cstdint>
#include <mutex>
#include <unordered_map>

// Shared across map workers. A rate-limit check and its timestamp update are
// one transaction; no references escape the lock. Memory is bounded across
// account churn. Eviction may permit an early retry, never suppress one forever.
class BoundedBotThrottle
{
public:
    bool Allow(std::uint64_t key, std::uint32_t now, std::uint32_t interval, bool force = false)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = times_.find(key);
        if (found != times_.end())
        {
            if (!force && std::uint32_t(now - found->second) < interval) return false;
            found->second = now;
        }
        else
        {
            if (times_.size() >= 65536) times_.erase(times_.begin());
            times_.emplace(key, now);
        }
        return true;
    }
private:
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::uint32_t> times_;
};
