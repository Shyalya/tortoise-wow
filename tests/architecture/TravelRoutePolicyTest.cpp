#include "../../modules/mod-playerbots/src/playerbot/TravelRoutePolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>

#define CHECK(x) do { if (!(x)) { std::cerr << "line " << __LINE__ << ": " #x << '\n'; std::exit(1); } } while (0)

int main()
{
    float const southshoreAerieDistance = 2139.057f;
    float const corrected = ai::GetTaxiRouteCost(southshoreAerieDistance);
    CHECK(std::fabs(corrected - 0.594183f) < 0.01f);
    CHECK(ai::GetTaxiRouteCost(0.0f) == 0.0f);

    CHECK(std::fabs(ai::GetWalkTravelTime(800.0f, 80.0f, 8.0f, 4.0f) - 120.0f) < 0.01f);
    CHECK(ai::GetTaxiRouteCost(southshoreAerieDistance) <
        ai::GetWalkTravelTime(southshoreAerieDistance, 0.0f, 8.0f, 4.0f));
    CHECK(std::fabs(ai::GetWalkTravelTime(800.0f, 400.0f, 8.0f, 4.0f) - 410.0f) < 0.01f);
    CHECK(ai::GetWalkTravelTime(0.0f, 0.0f, 0.0f, 0.0f) == 0.0f);

    std::uint32_t const selection = ai::GetStableTravelSelectionSeed(100, 4, 0, -715.146f, -512.134f);
    CHECK(selection == ai::GetStableTravelSelectionSeed(100, 4, 0, -715.146f, -512.134f));
    CHECK(selection != ai::GetStableTravelSelectionSeed(101, 4, 0, -715.146f, -512.134f));
    CHECK(selection != ai::GetStableTravelSelectionSeed(100, 8, 0, -715.146f, -512.134f));

    float const first = ai::GetStableRouteCostMultiplier(100, 0, -715.146f, -512.134f,
        0, 282.096f, -2001.28f);
    float const repeat = ai::GetStableRouteCostMultiplier(100, 0, -715.146f, -512.134f,
        0, 282.096f, -2001.28f);
    CHECK(first == repeat);
    CHECK(first >= 1.0f && first <= 1.25f);

    std::set<int> buckets;
    for (std::uint32_t party = 1; party <= 128; ++party)
    {
        float const multiplier = ai::GetStableRouteCostMultiplier(party, 0, -715.146f, -512.134f,
            0, 282.096f, -2001.28f);
        CHECK(multiplier >= 1.0f && multiplier <= 1.25f);
        buckets.insert(static_cast<int>((multiplier - 1.0f) * 10000.0f));
    }
    CHECK(buckets.size() > 80);

    std::cout << "Native travel timing and stable party route variation passed\n";
}
