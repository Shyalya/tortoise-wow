#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
using uint32 = uint32_t;
void Require(bool ok) { if (!ok) std::abort(); }
class PlayerbotAIBase
{
public:
    uint32 aiInternalUpdateDelay = 0;
    void AdvanceMinimalUpdateDelay(uint32);
    uint32 GetAIInternalUpdateDelay() const { return aiInternalUpdateDelay; }
};
#include "AIAdvance.inc"
class PlayerbotAI : public PlayerbotAIBase
{
public:
    bool pending = false;
    bool HasPendingTransition() const { return pending; }
};
struct Player
{
    uint32 m_lastAIUpdateMs = 1000;
    PlayerbotAI* ai = nullptr;
#include "AIClock.inc"
};
struct { bool enabled = true; } sPlayerbotAIConfig;
PlayerbotAI* GetBotAI(Player* player) { return player->ai; }
struct ScriptBase { virtual bool IsAIUpdateDue(Player*, uint32) = 0; };
struct Script : ScriptBase
{
#include "AIDue.inc"
};
// The source contract test separately checks this snapshot/admit/consume wiring
// in Map.cpp. This harness exercises the actual native clock/delay functions,
// with deterministic budget decisions instead of starting a server.
bool Tick(Player& p, Script& script, uint32 now, bool budget, bool legacy = false,
          bool immediate = false, bool valid = true, uint32 cap = 1000)
{
    uint32 elapsed = std::min(cap, legacy ? p.ConsumeAIElapsed(now) : p.GetAIElapsed(now));
    bool due = immediate || script.IsAIUpdateDue(&p, elapsed);
    if (!due) { if (!legacy) p.ConsumeAIElapsed(now); return false; }
    if ((!budget && !immediate) || !valid) return false;
    if (!legacy) p.ConsumeAIElapsed(now);
    p.ai->AdvanceMinimalUpdateDelay(elapsed);
    return true;
}
int main()
{
    Script script;
    PlayerbotAI ai; Player p; p.ai = &ai;
    ai.aiInternalUpdateDelay = 150;
    Require(!Tick(p, script, 1200, false));
    Require(p.m_lastAIUpdateMs == 1000 && ai.aiInternalUpdateDelay == 150);
    Require(Tick(p, script, 1250, true));
    Require(ai.aiInternalUpdateDelay == 0 && p.m_lastAIUpdateMs == 1250);

    // Reproduce the pre-fix failure using the same native clock and due hook.
    p.m_lastAIUpdateMs = 1000; ai.aiInternalUpdateDelay = 150;
    Require(!Tick(p, script, 1200, false, true));
    Require(!Tick(p, script, 1250, true, true));
    Require(ai.aiInternalUpdateDelay == 100);

    p.m_lastAIUpdateMs = 1000; ai.aiInternalUpdateDelay = 500;
    Require(!Tick(p, script, 1100, true));
    Require(ai.aiInternalUpdateDelay == 400 && p.m_lastAIUpdateMs == 1100);
    Require(!Tick(p, script, 1300, true));
    Require(ai.aiInternalUpdateDelay == 200 && p.m_lastAIUpdateMs == 1300);
    Require(!Tick(p, script, 1500, false));
    Require(!Tick(p, script, 1800, false));
    Require(Tick(p, script, 1900, true));
    Require(ai.aiInternalUpdateDelay == 0);

    p.m_lastAIUpdateMs = 2000; ai.aiInternalUpdateDelay = 100;
    Require(!Tick(p, script, 2200, true, false, false, false)); // stale map request
    Require(p.m_lastAIUpdateMs == 2000);
    Require(Tick(p, script, 2300, true));
    p.m_lastAIUpdateMs = 0xfffffff0; ai.aiInternalUpdateDelay = 32;
    Require(Tick(p, script, 0x20, true)); // native unsigned wrap

    p.m_lastAIUpdateMs = 1000; ai.aiInternalUpdateDelay = 5000;
    Require(!Tick(p, script, 10000, true, false, false, true, 1000));
    Require(ai.aiInternalUpdateDelay == 4000); // existing catch-up cap unchanged
    ai.pending = true;
    Require(Tick(p, script, 10001, true));
    ai.pending = false;
    sPlayerbotAIConfig.enabled = false;
    Require(!Tick(p, script, 11000, true));
    sPlayerbotAIConfig.enabled = true;
    Require(Tick(p, script, 11100, false, false, true)); // foreground bypasses idle budget
    std::cout << "Native elapsed accounting: deferral, not-due, stale, wrap, cap, transition passed\n";
}
