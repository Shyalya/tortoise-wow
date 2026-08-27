#include "playerbot/playerbot.h"
#include "Util/DungeonClearUtil.h"
#include "Util/DcAddonComm.h"
#include "DcValueKeys.h"
#include "DcRunState.h"
#include <cerrno>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <limits>

namespace
{
    bool ParseUInt(std::string const& text, uint32& value)
    {
        if (text.empty())
            return false;
        for (char c : text)
            if (c < '0' || c > '9')
                return false;

        char* end = nullptr;
        errno = 0;
        unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0' || errno == ERANGE ||
            parsed > std::numeric_limits<uint32>::max())
            return false;
        value = static_cast<uint32>(parsed);
        return true;
    }

    bool ParseFloat(std::string const& text, float& value)
    {
        if (text.empty())
            return false;
        char* end = nullptr;
        errno = 0;
        float parsed = std::strtof(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0' || errno == ERANGE ||
            !std::isfinite(parsed))
            return false;
        value = parsed;
        return true;
    }

    bool ApplySetting(DcRunState& state, std::string const& key,
                      std::string const& value, std::string& error)
    {
        // Float controls (notably PartyMaxSpread) legitimately send half-yard
        // values such as 25.5.  Parse by the setting's declared type before
        // attempting the integer-only validation used by the other controls.
        if (key == "PartyMaxSpread")
        {
            float spread = 0.0f;
            if (!ParseFloat(value, spread) || spread < 10.0f || spread > 60.0f)
            {
                error = "PartyMaxSpread must be between 10 and 60 yards.";
                return false;
            }
            state.hasPartyMaxSpreadOverride = true;
            state.partyMaxSpreadOverride = spread;
            return true;
        }

        uint32 parsed = 0;
        if (!ParseUInt(value, parsed))
        {
            error = "Invalid value for " + key + ".";
            return false;
        }

        if (key == "PreventBotRelease")
        {
            if (parsed > 1)
            {
                error = "PreventBotRelease must be 0 or 1.";
                return false;
            }
            state.hasPreventBotReleaseOverride = true;
            state.preventBotReleaseOverride = parsed != 0;
        }
        else if (key == "LootMinQuality")
        {
            if (parsed > 6)
            {
                error = "LootMinQuality must be between 0 and 6.";
                return false;
            }
            state.hasLootQualityOverride = true;
            state.lootQualityOverride = parsed;
        }
        else if (key == "IgnoreChests")
        {
            if (parsed > 1)
            {
                error = "IgnoreChests must be 0 or 1.";
                return false;
            }
            state.hasIgnoreChestsOverride = true;
            state.ignoreChestsOverride = parsed != 0;
        }
        else if (key == "RestHealthPct")
        {
            if (parsed > 100)
            {
                error = "RestHealthPct must be between 0 and 100.";
                return false;
            }
            if (!parsed)
            {
                state.hasRestHealthOverride = false;
                state.restHealthOverride = 0.0f;
            }
            else
            {
                state.hasRestHealthOverride = true;
                state.restHealthOverride = static_cast<float>(parsed);
            }
        }
        else if (key == "RestManaPct")
        {
            if (parsed > 100)
            {
                error = "RestManaPct must be between 0 and 100.";
                return false;
            }
            if (!parsed)
            {
                state.hasRestManaOverride = false;
                state.restManaOverride = 0.0f;
            }
            else
            {
                state.hasRestManaOverride = true;
                state.restManaOverride = static_cast<float>(parsed);
            }
        }
        else if (key == "PullDynamicMaxLeeroyMobs")
        {
            if (parsed < 1 || parsed > 20)
            {
                error = "PullDynamicMaxLeeroyMobs must be between 1 and 20.";
                return false;
            }
            state.hasPullMaxOverride = true;
            state.pullMaxOverride = parsed;
        }
        else
        {
            error = "Unknown or unsupported setting: " + key + ".";
            return false;
        }
        return true;
    }

    bool ResetSetting(DcRunState& state, std::string const& key, std::string& error)
    {
        if (key.empty())
        {
            state.hasPreventBotReleaseOverride = false;
            state.hasLootQualityOverride = false;
            state.hasIgnoreChestsOverride = false;
            state.hasRestHealthOverride = false;
            state.hasRestManaOverride = false;
            state.hasPartyMaxSpreadOverride = false;
            state.hasPullMaxOverride = false;
            state.preventBotReleaseOverride = true;
            state.lootQualityOverride = 0;
            state.ignoreChestsOverride = true;
            state.partyMaxSpreadOverride = 25.0f;
            state.restHealthOverride = 0.0f;
            state.restManaOverride = 0.0f;
            state.pullMaxOverride = 1;
            return true;
        }

        if (key == "PreventBotRelease")
        {
            state.hasPreventBotReleaseOverride = false;
            state.preventBotReleaseOverride = true;
        }
        else if (key == "LootMinQuality")
        {
            state.hasLootQualityOverride = false;
            state.lootQualityOverride = 0;
        }
        else if (key == "IgnoreChests")
        {
            state.hasIgnoreChestsOverride = false;
            state.ignoreChestsOverride = true;
        }
        else if (key == "RestHealthPct")
        {
            state.hasRestHealthOverride = false;
            state.restHealthOverride = 0.0f;
        }
        else if (key == "RestManaPct")
        {
            state.hasRestManaOverride = false;
            state.restManaOverride = 0.0f;
        }
        else if (key == "PartyMaxSpread")
        {
            state.hasPartyMaxSpreadOverride = false;
            state.partyMaxSpreadOverride = 25.0f;
        }
        else if (key == "PullDynamicMaxLeeroyMobs")
        {
            state.hasPullMaxOverride = false;
            state.pullMaxOverride = 1;
        }
        else
        {
            error = "Unknown or unsupported setting: " + key + ".";
            return false;
        }
        return true;
    }
}

// Called from HostHooks when LANG_ADDON / CHAT_MSG_ADDON arrives.
// Accepts both the companion protocol ("DC\tCMD\t<sub>[\t<param>]") and the
// legacy stub form ("DC\tON" / "DC:ON").
bool DungeonClear_HandleAddonMessage(Player* player, std::string const& msg)
{
    if (!player)
        return false;

    if (msg.rfind("DC\t", 0) != 0 && msg.rfind("DC:", 0) != 0)
        return false;

    std::string body = msg.size() > 3 ? msg.substr(3) : "";
    while (!body.empty() && (body[0] == '\t' || body[0] == ':' || body[0] == ' '))
        body.erase(0, 1);

    std::string subCmd;
    std::string param;

    if (body.rfind("CMD\t", 0) == 0)
    {
        std::string rest = body.substr(4);
        auto tab = rest.find('\t');
        if (tab == std::string::npos)
            subCmd = rest;
        else
        {
            subCmd = rest.substr(0, tab);
            param = rest.substr(tab + 1);
        }
    }
    else
    {
        auto tab = body.find('\t');
        if (tab == std::string::npos)
            subCmd = body;
        else
        {
            subCmd = body.substr(0, tab);
            param = body.substr(tab + 1);
        }
    }

    for (char& c : subCmd)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');

    if (subCmd.empty())
        return true;

    if (subCmd == "sync")
    {
        Player* tank = DcUtil::FindGroupTankBot(player);
        if (tank && tank->GetPlayerbotAI())
            DcAddonComm::PushSettings(player, tank->GetPlayerbotAI(), tank);
        else
        {
            // Keep the settings page usable outside a group.  Cached addon
            // defaults remain visible until a tank becomes available.
            DcAddonComm::SendToPlayer(player, "SYNCSTART");
            DcAddonComm::SendToPlayer(player, "SYNCEND");
        }
        return true;
    }
    if (subCmd == "spectate")
    {
        DcAddonComm::SendToPlayer(player, "SPECTATE\t0");
        DcAddonComm::SendError(player, "Spectator camera is not available on this server yet.");
        return true;
    }
    if (subCmd == "status")
        DcAddonComm::SendToPlayer(player, "SPECTATE\t0");

    Player* tank = DcUtil::FindGroupTankBot(player);
    if (!tank)
        tank = player->GetPlayerbotAI() ? player : nullptr;
    if (!tank || !tank->GetPlayerbotAI())
    {
        DcAddonComm::SendError(player, "No tank bot found in your group.");
        return true;
    }

    PlayerbotAI* tai = tank->GetPlayerbotAI();

    if (subCmd == "set" || subCmd == "reset")
    {
        if (!DcUtil::IsRealCommander(player, tank))
        {
            DcAddonComm::SendError(player, "You are not allowed to change this run's settings.");
            return true;
        }

        DcRunState& state = tai->GetAiObjectContext()->GetValue<DcRunState&>(DcKey::RunState)->Get();
        std::string key;
        std::string value;
        auto tab = param.find('\t');
        if (tab == std::string::npos)
            key = param;
        else
        {
            key = param.substr(0, tab);
            value = param.substr(tab + 1);
        }

        std::string error;
        bool ok = subCmd == "set"
            ? ApplySetting(state, key, value, error)
            : ResetSetting(state, key, error);
        if (!ok)
        {
            DcAddonComm::SendError(player, error);
            return true;
        }
        DcAddonComm::PushSettings(player, tai, tank);
        DcAddonComm::PushStatus(tai, tank);
        return true;
    }

    std::string eventParam = param;
    if (subCmd == "status" || subCmd == "bosses" || subCmd == "boss")
        eventParam = "addon";

    Event ev("addon", eventParam, player);

    auto run = [&](char const* actionName) {
        if (Action* act = tai->GetAiObjectContext()->GetAction(actionName))
            act->Execute(ev);
        else
            DcAddonComm::SendError(player, std::string("Unsupported DungeonClear action: ") + actionName);
    };

    if (subCmd == "on")
        run("dc on");
    else if (subCmd == "off")
        run("dc off");
    else if (subCmd == "pause")
        run("dc pause");
    else if (subCmd == "skip")
        run("dc skip");
    else if (subCmd == "pull")
        run("dc pull");
    else if (subCmd == "status")
        run("dc status");
    else if (subCmd == "bosses" || subCmd == "boss")
        run("dc bosses");
    else if (subCmd == "go")
        run("dc go");

    return true;
}
