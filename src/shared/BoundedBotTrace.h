#pragma once
#include <array>
#include <cstdint>

// Temporary diagnostic admission only. Caller serializes access; no entity pointers.
class BoundedBotTrace
{
public:
    bool Take(uint32_t now, uint32_t guid, bool allowNew = true, bool periodic = false, bool action = false)
    {
        if (finished || !guid) return false;
        if ((started && uint32_t(now - start) >= 600000) || emitted >= 8000)
        { finished = true; return false; }
        Slot* selected = nullptr;
        Slot* available = nullptr;
        for (auto& slot : slots)
        {
            if (slot.guid == guid) { selected = &slot; break; }
            if (!available && (!slot.guid || uint32_t(now - slot.lastSeen) >= 30000)) available = &slot;
        }
        if (!selected)
        {
            if (!allowNew) return false;
            if (!available) { ++suppressed; return false; }
            *available = Slot{}; available->guid = guid; selected = available;
        }
        if (!started) { started = true; start = now; }
        selected->lastSeen = now;
        uint32_t second = now / 1000;
        if (selected->second != second) { selected->second = second; selected->count = 0; }
        // Reserve one of the eight records for physical progress, even when
        // action/retry events fill the other seven. Generic action text must
        // not exhaust a full-journey sample before a bot leaves the first hub.
        if (periodic)
        {
            if (selected->hasPeriodic && uint32_t(now - selected->lastPeriodic) < 5000) return false;
        }
        else if (selected->count >= 7 || (action && selected->hasAction && uint32_t(now - selected->lastAction) < 5000))
        { ++suppressed; return false; }
        if (periodic) { selected->hasPeriodic = true; selected->lastPeriodic = now; }
        if (action) { selected->hasAction = true; selected->lastAction = now; }
        ++selected->count; ++emitted;
        return true;
    }
    bool Finished() const { return finished; }
    uint32_t Emitted() const { return emitted; }
    uint64_t Suppressed() const { return suppressed; }
private:
    struct Slot
    {
        uint32_t guid=0, lastSeen=0, second=0, count=0, lastPeriodic=0, lastAction=0;
        bool hasPeriodic=false, hasAction=false;
    };
    std::array<Slot, 12> slots{};
    bool started=false, finished=false;
    uint32_t start=0, emitted=0;
    uint64_t suppressed=0;
};
