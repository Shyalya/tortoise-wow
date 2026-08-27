#pragma once

class PlayerbotAI;
class Player;

namespace DcStrategyGate
{
    enum class Action { None, Install, Strip };

    constexpr Action Decide(bool shouldInstall, bool hasStrategy)
    {
        if (shouldInstall && !hasStrategy) return Action::Install;
        if (!shouldInstall && hasStrategy) return Action::Strip;
        return Action::None;
    }

    void Reconcile(PlayerbotAI* ai, Player* bot);
    void Register();
}
