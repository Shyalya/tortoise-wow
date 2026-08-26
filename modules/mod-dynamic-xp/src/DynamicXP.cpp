#include "DynamicXPManager.h"

#include "Chat.h"
#include "Player.h"
#include "ScriptObjects.h"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace
{
    std::string FormatRate(float rate)
    {
        std::ostringstream output;
        output << std::fixed << std::setprecision(2) << rate << "x";
        return output.str();
    }

    std::string BuildStatusMessage(Player* player)
    {
        DynamicXPManager& manager = DynamicXPManager::Instance();
        uint32 const playerLevel = player->GetLevel();
        uint32 const progressionLevel = manager.GetProgressionLevel();
        float const rate = manager.GetRate(playerLevel);
        int32 const difference = static_cast<int32>(progressionLevel) - static_cast<int32>(playerLevel);

        std::ostringstream output;
        output << "[Dynamic XP] ";

        if (difference > 0)
        {
            output << "Catch-up bonus active: " << FormatRate(rate) << " XP.\n"
                   << "You are " << difference << " levels below the current progression level ("
                   << static_cast<uint32>(progressionLevel) << ").";
        }
        else if (difference < 0)
        {
            output << "Progression modifier active: " << FormatRate(rate) << " XP.\n"
                   << "You are " << -difference << " levels above the current progression level ("
                   << static_cast<uint32>(progressionLevel) << ").";
        }
        else
        {
            output << "Progression modifier neutral: " << FormatRate(rate) << " XP.\n"
                   << "You are at the current progression level ("
                   << static_cast<uint32>(progressionLevel) << ").";
        }

        return output.str();
    }

    void SendStatusMessage(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        ChatHandler(player).SendSysMessage(BuildStatusMessage(player));
    }

    void SendRateCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
        {
            handler->SendSysMessage("This command must be used in-game.");
            return;
        }

        DynamicXPManager& manager = DynamicXPManager::Instance();
        DynamicXPSettings const& settings = manager.GetSettings();
        uint32 const playerLevel = player->GetLevel();
        uint32 const progressionLevel = manager.GetProgressionLevel();
        float const rate = manager.GetRate(playerLevel);
        int32 const difference = static_cast<int32>(progressionLevel) - static_cast<int32>(playerLevel);

        std::ostringstream output;
        output << "Dynamic XP\n"
               << "Progression level: " << static_cast<uint32>(progressionLevel) << "\n"
               << "Your level: " << static_cast<uint32>(playerLevel) << "\n"
               << "Current XP rate: " << FormatRate(rate) << "\n";

        if (difference > 0)
        {
            output << "\nCatch-up bonus: +" << std::fixed << std::setprecision(0)
                   << (rate - 1.0f) * 100.0f << "%\n"
                   << "(+" << settings.bonusPerLevelBelow * 100.0f << "% x "
                   << difference << " levels below progression)";
        }
        else if (difference < 0)
        {
            output << "\nProgression modifier: " << std::fixed << std::setprecision(0)
                   << (rate - 1.0f) * 100.0f << "%\n"
                   << "(-" << settings.penaltyPerLevelAbove * 100.0f << "% x "
                   << -difference << " levels above progression)";
        }
        else
        {
            output << "\nProgression modifier: neutral (1.00x)";
        }

        handler->SendSysMessage(output.str());
    }
}

class DynamicXPPlayerScript : public PlayerScript
{
public:
    DynamicXPPlayerScript()
        : PlayerScript("DynamicXPPlayerScript", {
            PLAYERHOOK_ON_GIVE_EXP,
            PLAYERHOOK_ON_LOGIN,
            PLAYERHOOK_ON_LEVEL_CHANGED
        }) {}

    void OnGiveXP(Player* player, uint32& amount, Unit* /*victim*/) override
    {
        DynamicXPManager& manager = DynamicXPManager::Instance();
        if (manager.IsEnabled())
            amount = static_cast<uint32>(static_cast<float>(amount) * manager.GetRate(player->GetLevel()));
    }

    void OnLogin(Player* player) override
    {
        if (DynamicXPManager::Instance().IsEnabled())
            SendStatusMessage(player);
    }

    void OnLevelChanged(Player* player, uint8 oldLevel) override
    {
        DynamicXPManager& manager = DynamicXPManager::Instance();
        if (!manager.IsEnabled())
            return;

        float const oldRate = manager.GetRate(oldLevel);
        float const newRate = manager.GetRate(player->GetLevel());
        if (player->GetSession() && std::fabs(oldRate - newRate) > 0.0001f)
        {
            ChatHandler(player).PSendSysMessage(
                "[Dynamic XP] You reached level %u. Your XP rate is now %s.",
                static_cast<uint32>(player->GetLevel()), FormatRate(newRate).c_str());
        }
    }
};

class DynamicXPWorldScript : public WorldScript
{
public:
    DynamicXPWorldScript()
        : WorldScript("DynamicXPWorldScript", {
            WORLDHOOK_ON_STARTUP,
            WORLDHOOK_ON_UPDATE,
            WORLDHOOK_ON_AFTER_CONFIG_LOAD
        }) {}

    void OnStartup() override
    {
        DynamicXPManager& manager = DynamicXPManager::Instance();
        manager.ReloadConfig();
        manager.RefreshProgressionLevel();
    }

    void OnUpdate(uint32 diff) override
    {
        DynamicXPManager::Instance().Update(diff);
    }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        DynamicXPManager::Instance().ReloadConfig();
    }
};

// The Tortoise command table stores ChatHandler member-function pointers, so a
// module cannot add a normal CommandScript handler without changing core. The
// all-command hook is the supported module-side command interception point.
class DynamicXPCommandScript : public AllCommandScript
{
public:
    DynamicXPCommandScript() : AllCommandScript("DynamicXPCommandScript") {}

    bool CanExecuteCommand(ChatHandler* handler, char const* command, char const* /*args*/) override
    {
        if (!command || std::strcmp(command, "xprate") != 0)
            return true;

        if (!DynamicXPManager::Instance().IsEnabled())
        {
            handler->SendSysMessage("Dynamic XP is disabled.");
            return false;
        }

        SendRateCommand(handler);
        return false;
    }
};

void Addmod_dynamic_xpScripts()
{
    new DynamicXPPlayerScript();
    new DynamicXPWorldScript();
    new DynamicXPCommandScript();
}
