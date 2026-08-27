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
    bool PartyNeedsRest(Player* anyMember);
    bool PartyReadyToResume(Player* anyMember);
    bool PartyNeedsRegroup(Player* anyMember);
    bool EffectivePreventBotRelease(Player* anyMember);
    bool DungeonClearShouldPreventAutoRelease(Player* bot);
    uint32 EffectiveLootQualityMin(Player* anyMember);
    bool EffectiveIgnoreChests(Player* anyMember);
    float EffectivePartyMaxSpread(Player* anyMember);
    float EffectiveRestHealth(Player* anyMember);
    float EffectiveRestMana(Player* anyMember);
    uint32 EffectivePullMax(Player* anyMember);
    void ResetDungeonClearRun(PlayerbotAI* ai, Player* bot);
    void CancelDungeonClearEvent(PlayerbotAI* ai);
    void DisableDungeonClear(PlayerbotAI* ai, Player* bot, char const* reason);
    void TellGroup(PlayerbotAI* ai, Player* bot, std::string const& msg);
    Unit* FindHostileNear(Player* bot, float range);
    uint32 CountHostileNear(Player* observer, Unit* center, float range);
    uint32 CountHostileNear(Player* bot, float range);
    GameObject* FindGONear(Player* bot, uint32 entry, float range);
    bool CastRezOn(PlayerbotAI* ai, Player* caster, Player* target);
}
