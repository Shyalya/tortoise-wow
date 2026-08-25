#pragma once

class PlayerbotAI;
class Player;

namespace DcStrategyGate
{
    enum class Action { None, Install, Strip };

    constexpr Action Decide(bool inDungeon, bool hasStrategy)
    {
        if (inDungeon && !hasStrategy) return Action::Install;
        if (!inDungeon && hasStrategy) return Action::Strip;
        return Action::None;
    }

    void Reconcile(PlayerbotAI* ai, Player* bot);
    void Register();
}
