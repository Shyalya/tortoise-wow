#pragma once
#include <string>
#include "Platform/Define.h"

class Player;
class PlayerbotAI;
class Unit;
class GameObject;
struct DcRunState;

namespace DcUtil
{
    bool IsRealCommander(Player* owner, Player* bot);
    Player* FindEnabledTank(Player* anyMember);
    Player* FindGroupTankBot(Player* anyMember);
    DcRunState* LeaderRunState(Player* bot);
    bool IsDungeonClearLeader(PlayerbotAI* ai, Player* bot);
    bool IsEnabledRun(Player* bot);
    bool IsPausedRun(Player* bot);
    void DisableDungeonClear(PlayerbotAI* ai, Player* bot, char const* reason);
    void TellGroup(PlayerbotAI* ai, Player* bot, std::string const& msg);
    Unit* FindHostileNear(Player* bot, float range);
    GameObject* FindGONear(Player* bot, uint32 entry, float range);
    bool CastRezOn(PlayerbotAI* ai, Player* caster, Player* target);
}
