/*
 * mod-dungeon-clear — DungeonClearAddonHook.cpp
 *
 * PlayerScript hook that intercepts addon messages (LANG_ADDON, prefix "DC")
 * sent by the DungeonClear companion addon.  Parses "CMD\t<sub>\t<param>"
 * payloads and dispatches to the same tank-bot actions that the `.dc` slash
 * command uses.
 *
 * The addon sends commands via SendAddonMessage("DC", ..., "PARTY") in a party
 * or "RAID" in a raid, arriving as CHAT_MSG_PARTY / CHAT_MSG_RAID / LANG_ADDON.
 * (A raid must use the RAID channel: a PARTY addon message only reaches the
 * sender's subgroup, so a tank bot in another subgroup would never see it.)
 * Our OnPlayerBeforeSendChatMessage
 * hook fires before the ChatHandler switch statement, parses the command,
 * dispatches it silently (DoSpecificAction with silent=true), then consumes
 * the message so no further chat processing occurs.
 */

#include <cstdlib>

#include "ScriptMgr.h"
#include "PlayerScript.h"
#include "Chat.h"
#include "Log.h"
#include "Player.h"
#include "ServerFacade.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"

#include "DungeonClearDispatch.h"
#include "StringFormat.h"
#include "Util/DcSpectator.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"

namespace
{
    // Send a raw "DC\t..." payload back to the player via the addon channel.
    void SendAddonPayload(Player* player, std::string const& payload)
    {
        if (!player)
            return;

        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, payload.c_str(),
                                     LANG_ADDON, CHAT_TAG_NONE,
                                     player->GetObjectGuid(), player->GetName());

        ServerFacade::instance().SendPacket(player, data);
    }

    // Send an error back to the player via the addon message channel.
    void SendAddonError(Player* player, std::string const& msg)
    {
        SendAddonPayload(player, "DC\tERROR\t" + msg);
    }

    // Tell the addon whether the spectator free-camera is enabled server-side so
    // it can grey out / disable its Spectate button instead of letting the player
    // click into a refusal. DungeonClear.SpectateEnable is a server-only flag, so
    // this is the only way the addon learns it. Sent in answer to the addon's
    // status poll (its panel heartbeat) — tank-independent, and refreshed every
    // time the panel reopens or combat toggles.
    void SendSpectateState(Player* player)
    {
        bool const enabled = DcSettings::GetBool(player, "SpectateEnable");
        SendAddonPayload(player,
            Acore::StringFormat("DC\tSPECTATE\t{}", enabled ? 1 : 0));
    }

    // One "DC\tSETTINGS\t<key>\t<value>\t<min>\t<max>\t<type>\t<overridden>"
    // line describing one player-facing setting's effective value + schema, so
    // the addon can populate (and optionally render) its panel from the server.
    void SendSettingLine(Player* player, ObjectGuid owner, DcSettingDef const& d)
    {
        double const value = DcSettings::GetEffectiveRaw(owner, d);
        bool const overridden = DcSettings::HasOverride(owner, d.key);

        SendAddonPayload(player, Acore::StringFormat(
            "DC\tSETTINGS\t{}\t{}\t{}\t{}\t{}\t{}",
            d.key, value, d.minVal, d.maxVal,
            static_cast<int>(d.type), overridden ? 1 : 0));
    }

    // Push every player-facing setting (the full panel) to the player, framed by
    // a SYNCSTART/SYNCEND pair so the addon knows when it has the complete set.
    void SendSettingsSync(Player* player, ObjectGuid owner)
    {
        SendAddonPayload(player, "DC\tSYNCSTART");
        for (DcSettingDef const& d : kDcSettings)
            if (d.playerFacing)
                SendSettingLine(player, owner, d);
        SendAddonPayload(player, "DC\tSYNCEND");
    }

    // set / reset / sync share the same run-owner resolution. Returns false (and
    // reports an error) when the player has no leader tank to own the overrides.
    void HandleSettingsCommand(Player* player, std::string const& subCmd,
                               std::string const& param)
    {
        // The run owner is this party's leader tank, if one exists. sync works
        // without one (it just reports the server defaults so the addon panel
        // can render anywhere); set/reset need an owner to attach the override
        // to and report an error when there's no tank in the group.
        Player* leader = DcLeaderSignal::FindLeaderTank(player);
        ObjectGuid const owner = leader ? leader->GetObjectGuid() : ObjectGuid::Empty;

        if (subCmd == "sync")
        {
            SendSettingsSync(player, owner);
            return;
        }

        if (owner.IsEmpty())
        {
            SendAddonError(player, "No tank bot found in your group.");
            return;
        }

        if (subCmd == "reset")
        {
            // param is the key to reset, or empty to reset the whole run.
            DcSettings::ResetOverride(owner, param);
            SendSettingsSync(player, owner);
            return;
        }

        // subCmd == "set": param is "<key>\t<value>".
        auto const sep = param.find('\t');
        if (sep == std::string::npos)
        {
            SendAddonError(player, "set requires <key> <value>.");
            return;
        }

        std::string const key = param.substr(0, sep);
        std::string const valStr = param.substr(sep + 1);

        char* end = nullptr;
        double const value = std::strtod(valStr.c_str(), &end);
        if (end == valStr.c_str())
        {
            SendAddonError(player, "Invalid value for " + key + ".");
            return;
        }

        std::string err;
        if (!DcSettings::SetOverride(owner, key, value, &err))
        {
            SendAddonError(player, err);
            return;
        }

        // Echo the stored (clamped) value back so the addon shows the truth.
        if (DcSettingDef const* d = FindDcSetting(key))
            SendSettingLine(player, owner, *d);
    }
}

class DungeonClearAddonHookScript : public PlayerScript
{
public:
    DungeonClearAddonHookScript()
        : PlayerScript("DungeonClearAddonHookScript", {
            PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE,
            PLAYERHOOK_CAN_USE_GROUP_CHAT,
            PLAYERHOOK_ON_LOGOUT
        }) {}

    // True for the addon's client->server control messages, which ride in as an
    // addon chat line of the form "DC\tCMD\t<sub>[\t<param>]". Shared by the
    // parse hook (OnPlayerBeforeSendChatMessage, which acts on them) and the
    // relay-block hook (OnPlayerCanUseChat, which stops the core forwarding
    // them to the rest of the group).
    //
    // WHISPER is the solo transport. The addon's normal channel is PARTY/RAID,
    // which simply does not exist for a player with no group — so a GM watching
    // a test run from outside the bot party could not press any button at all,
    // spectate included, even though the spectator camera itself has never had
    // a group requirement. A whisper to oneself is the standard addon-message
    // channel for that case. The server dispatch self-authorizes either way
    // (bot commands still need a tank bot in the sender's group and say so),
    // so accepting the extra channel grants no new authority.
    static bool IsDcAddonCommand(uint32 type, uint32 lang, std::string const& msg)
    {
        if (lang != LANG_ADDON)
            return false;
        if (type != CHAT_MSG_PARTY && type != CHAT_MSG_PARTY_LEADER &&
            type != CHAT_MSG_RAID && type != CHAT_MSG_RAID_LEADER &&
            type != CHAT_MSG_WHISPER)
            return false;
        // "DC\t" (3) + "CMD\t" (4) = 7-byte prefix.
        return msg.compare(0, 7, "DC\tCMD\t") == 0;
    }

    // Per-run overrides are keyed by the leader tank's GUID; drop them when that
    // player logs out so stale leader GUIDs don't accumulate. A no-op for any
    // player who never owned a run.
    void OnLogout(Player* player) override
    {
        if (player)
            DcSettings::ClearRun(player->GetObjectGuid());
    }

    // Block the core from relaying our own control messages to the rest of the
    // group. We already act on them in OnPlayerBeforeSendChatMessage; returning
    // false here silently drops the packet. This REPLACES the old "consume"
    // trick of rewriting `type` to CHAT_MSG_ADDON (= 0xFFFFFFFF): that value has
    // no case in HandleMessagechatOpcode's `switch (type)`, so every command
    // fell through to the default and logged
    // "CHAT: unknown message type 4294967295, lang: 4294967295" — once per
    // command, and constantly from the addon's status-poll heartbeat.
    // Tortoise port: the group argument is gone - the hook fires from the
    // chat handler before any group fan-out, so there is nothing to pass.
    bool CanUseGroupChat(Player* /*player*/, uint32 type, uint32 lang, std::string& msg) override
    {
        return !IsDcAddonCommand(type, lang, msg);
    }

    void OnBeforeSendChatMessage(Player* player, uint32& type, uint32& lang, std::string& msg) override
    {
        // Only our addon control messages ("DC\tCMD\t...", on party/raid addon
        // chat). The addon sends on RAID when the player is in a raid so the
        // command reaches a tank bot in any subgroup — on PARTY it would only
        // reach the sender's own subgroup. The matching relay-suppression lives
        // in OnPlayerCanUseChat above; here we only act on the command.
        if (!IsDcAddonCommand(type, lang, msg))
            return;

        // Parse "DC\tCMD\t<subcommand>[\t<param>]" — strip the 7-byte prefix.
        std::string const cmdPayload = msg.substr(7);
        std::string subCmd;
        std::string param;

        auto const tabPos = cmdPayload.find('\t');
        if (tabPos == std::string::npos)
        {
            subCmd = cmdPayload;
        }
        else
        {
            subCmd = cmdPayload.substr(0, tabPos);
            param = cmdPayload.substr(tabPos + 1);
        }

        if (subCmd.empty())
            return;

        // Per-run settings overrides (set/reset/sync) are handled in-process
        // rather than dispatched as a tank-bot action.
        if (subCmd == "set" || subCmd == "reset" || subCmd == "sync")
        {
            HandleSettingsCommand(player, subCmd, param);
            return;
        }

        // Spectator camera: acts on the sending player directly (session
        // plumbing, not a tank-bot action) — never dispatched. Bare = the
        // free-flying camera; "follow" rides the run's tank instead.
        if (subCmd == "spectate")
        {
            std::string whyNot;
            bool ok = true;
            if (param == "follow")
                ok = DcSpectator::ToggleFollow(player, nullptr, &whyNot);
            else if (param == "next")
                ok = DcSpectator::CycleFollow(player, +1, &whyNot);
            else if (param == "prev")
                ok = DcSpectator::CycleFollow(player, -1, &whyNot);
            else
                ok = DcSpectator::Toggle(player, &whyNot);
            if (!ok)
                SendAddonError(player, whyNot);
            return;
        }

        // Piggyback the spectate-enabled flag on the addon's status poll: it's
        // the panel's heartbeat (sent on open and on combat transitions), so the
        // button stays in sync regardless of whether a tank bot is present.
        if (subCmd == "status")
            SendSpectateState(player);

        // Map subcommand strings to action names.
        std::string action;
        if (subCmd == "on")         action = "dc on";
        else if (subCmd == "off")   action = "dc off";
        else if (subCmd == "skip")  action = "dc skip";
        else if (subCmd == "pause") action = "dc pause";
        else if (subCmd == "pull")  action = "dc pull";
        else if (subCmd == "status") action = "dc status";
        else if (subCmd == "bosses") action = "dc bosses";
        else if (subCmd == "go")    action = "dc go";
        else
        {
            LOG_DEBUG("module", "mod-dungeon-clear: unknown addon subcommand '{}' from {}",
                      subCmd, player->GetName());
            return;
        }

        // Dispatch to the tank bot(s) silently (no PlaySound emotes). Relay
        // suppression is handled by OnPlayerCanUseChat — see the note there.
        if (!DungeonClearDispatch::DispatchToTankBots(player, action, param))
            SendAddonError(player, "No tank bot found in your group.");
    }
};

void AddSC_dungeon_clear_addon_hook()
{
    new DungeonClearAddonHookScript();
}
