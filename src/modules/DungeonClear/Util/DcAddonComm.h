#pragma once
#include <string>
#include "Platform/Define.h"
#include "ObjectGuid.h"

class Player;
class PlayerbotAI;

// Companion-addon wire protocol (prefix "DC"), matching jrad7/mod-dungeon-clear-addon.
// Client→server: "DC\tCMD\t<sub>[\t<param>]"
// Server→client: "STATUS|BOSS_START|BOSS|BOSS_END|CHAT|ERROR|SPECTATE|SYNCSTART|SYNCEND|SETTINGS\t..."
namespace DcAddonComm
{
    // Addon pull-control encoding (STATUS field / pull CMD param).
    // Tortoise stores DcPullMode as Dynamic=0, Leeroy=1, Advanced=2 — convert at the wire.
    uint8 TortoisePullToAddon(uint8 tortoiseMode);
    uint8 AddonPullToTortoise(uint8 addonMode);
    uint8 AddonPullKeywordToTortoise(std::string const& param, uint8 currentTortoise);

    void SendToPlayer(Player* player, std::string const& payload);
    void SendToGroup(PlayerbotAI* ai, Player* bot, std::string const& payload);
    void SendError(Player* player, std::string const& msg);
    void PushSettings(Player* recipient, PlayerbotAI* ai, Player* bot);

    std::string BuildStatusPayload(PlayerbotAI* ai, Player* bot);
    void PushStatus(PlayerbotAI* ai, Player* bot);
    void PushBossList(PlayerbotAI* ai, Player* bot, bool silent);

    void MarkActiveTank(ObjectGuid tank);
    void UnmarkActiveTank(ObjectGuid tank);
    void TickStatusPushes(uint32 diffMs);
}
