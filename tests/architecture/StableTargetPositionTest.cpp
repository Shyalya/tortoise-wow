#include "../../modules/mod-playerbots/src/playerbot/strategy/actions/StableTargetPosition.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "line " << __LINE__ << ": " #x << '\n'; std::exit(1); } } while (0)

int main()
{
    ai::StableTargetOffset const first = ai::GetStableTargetOffset(100, 2432, -715.146f, -512.134f);
    ai::StableTargetOffset const repeat = ai::GetStableTargetOffset(100, 2432, -715.146f, -512.134f);
    CHECK(first.angle == repeat.angle);
    CHECK(first.scale == repeat.scale);
    CHECK(first.scale >= 0.55f && first.scale <= 0.95f);

    ai::StableTargetOffset const otherBot = ai::GetStableTargetOffset(101, 2432, -715.146f, -512.134f);
    ai::StableTargetOffset const otherTarget = ai::GetStableTargetOffset(100, 2433, -715.146f, -512.134f);
    CHECK(first.angle != otherBot.angle || first.scale != otherBot.scale);
    CHECK(first.angle != otherTarget.angle || first.scale != otherTarget.scale);

    // Tiny coordinate noise must not move the committed approach point.
    ai::StableTargetOffset const noisy = ai::GetStableTargetOffset(100, 2432, -715.14f, -512.13f);
    CHECK(first.angle == noisy.angle);
    CHECK(first.scale == noisy.scale);

    std::cout << "Stable per-bot destination offsets passed\n";
}
