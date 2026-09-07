#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <vector>

// IDs only: never retain creature/trainer pointers across database reloads.
class BotTrainerIndex
{
public:
    void Update(std::uint32_t entry, std::uint32_t trainerClass, bool allClasses, bool eligible)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        common_.erase(entry);
        for (auto& bucket : classes_) bucket.second.erase(entry);
        if (eligible)
        {
            if (allClasses) common_.insert(entry);
            else classes_[trainerClass].insert(entry);
        }
    }
    std::vector<std::uint32_t> Snapshot(std::uint32_t playerClass) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::set<std::uint32_t> entries = common_;
        auto const found = classes_.find(playerClass);
        if (found != classes_.end()) entries.insert(found->second.begin(), found->second.end());
        return {entries.begin(), entries.end()};
    }
private:
    mutable std::mutex mutex_;
    std::set<std::uint32_t> common_;
    std::map<std::uint32_t, std::set<std::uint32_t>> classes_;
};
