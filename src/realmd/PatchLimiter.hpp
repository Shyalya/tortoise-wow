#pragma once
#include <atomic>
#include <stdint.h>

#include "Database/DatabaseEnv.h"

extern std::atomic<uint64_t> MaxDataPerSecond;

class PatchLimiter
{
public:

	void Update(uint32_t diff)
	{
		if (timeDiff < diff)
		{
			timeDiff = 1000;
			totalCurrentBandwidth.store(0, std::memory_order_seq_cst);
		}
		else
			timeDiff -= diff;
	}

	bool IsAllowed(uint32_t bytes)
	{
		uint64_t const limit = MaxDataPerSecond.load(std::memory_order_relaxed);
		if (limit == 0)
			return true;

		uint64_t current = totalCurrentBandwidth.load(std::memory_order_relaxed);
		for (;;)
		{
			if (current > limit || bytes > limit - current)
				return false;

			if (totalCurrentBandwidth.compare_exchange_weak(current, current + bytes,
				std::memory_order_relaxed, std::memory_order_relaxed))
				return true;
		}
	}

private:
	std::atomic_uint64_t totalCurrentBandwidth{ 0 };
	uint32_t timeDiff = 1000;
};

extern PatchLimiter sPatchLimiter;
