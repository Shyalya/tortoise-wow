// Host-side glue for bot lifecycle and dispatch. Implements:
//   - Player::{Create,Remove}Playerbot{AI,Mgr}, Player::isRealPlayer
//   - Player::UpdatePlayerbotHooks (per-Player tick)
//   - World::{Update,Init}Playerbots* (world-tick driver, startup init)
//   - Player_DispatchBotOutgoing{Packet,ChatCommand} (free functions called
//     from WorldSession; the bot-AI null-check happens here so the host
//     call sites stay unconditional)
//
// Lives in the bot module so it sees both the host headers and the bot
// module's full PlayerbotAI / PlayerbotMgr types — the host declares the
// methods, only the bot module satisfies the linker with real bodies. The
// matching BUILD_PLAYERBOTS=OFF stubs live in src/game/PlayerbotStubs.cpp.

#include "playerbot/playerbot.h"
#include "Objects/Player.h"
#include "World.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/RandomPlayerbotFactory.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotAiExtension.h"
#include "ahbot/AhBot.h"
#include "BotDiagnostics.h"
#include "playerbot/AiFactory.h"
#include "playerbot/strategy/actions/ChangeTalentsAction.h"
#include "Chat/Chat.h"

void Player::CreatePlayerbotAI()
{
    if (!m_playerbotAI)
        m_playerbotAI = new PlayerbotAI(this);
}

void Player::RemovePlayerbotAI()
{
    if (m_playerbotAI)
    {
        delete m_playerbotAI;
        m_playerbotAI = nullptr;
    }
}

void Player::CreatePlayerbotMgr()
{
    if (!m_playerbotMgr)
    {
        m_playerbotMgr = new PlayerbotMgr(this);
        // RandomPlayerbotMgr tracks real players in its own `players` map
        // (used by SyncLevelWithPlayers, RandomBotLoginWithPlayer, LFG
        // auto-queue). Without this call the map never gets populated and
        // those features silently never trigger.
        sRandomPlayerbotMgr.OnPlayerLogin(this);
    }
}

void Player::RemovePlayerbotMgr()
{
    if (m_playerbotMgr)
    {
        // Log out the master's alt bots first; otherwise their PlayerbotAI
        // outlives the mgr and they linger in-world with a dangling master.
        m_playerbotMgr->LogoutAllBots();
        sRandomPlayerbotMgr.OnPlayerLogout(this);
        delete m_playerbotMgr;
        m_playerbotMgr = nullptr;
    }
}

bool Player::isRealPlayer() const
{
    return !m_playerbotAI || m_playerbotAI->IsRealPlayer();
}

// World-tick driver. RandomPlayerbotMgr ticks both the login queue and every
// active bot's PlayerbotAI.
void World::UpdatePlayerbotsTick(uint32 diff)
{
    if (!sPlayerbotAIConfig.enabled)
        return;
    sRandomPlayerbotMgr.UpdateAI(diff);
    auctionbot.Update();
    sPlayerbotAiExtension.RunWorldUpdate(diff);
}

// Per-Player bot tick:
//  - if `this` is a bot (m_playerbotAI != null), tick the AI
//  - if `this` is a real master driving bots (m_playerbotMgr != null), tick
//    the mgr so its alt-bot squad responds to the master's actions
//
// SC_PHASE tags around each call site let the crash handler identify which
// call faulted when bots self-destruct mid-tick (logout, far-teleport).
void Player::UpdatePlayerbotHooks(uint32 diff)
{
    if (!sPlayerbotAIConfig.enabled)
        return;
    if (m_playerbotAI)
    {
        SC_PHASE("Player::UpdatePlayerbotHooks/ai.UpdateAI", GetName());
        m_playerbotAI->UpdateAI(diff);
    }
    if (m_playerbotMgr)
    {
        SC_PHASE("Player::UpdatePlayerbotHooks/mgr.UpdateAI", GetName());
        m_playerbotMgr->UpdateAI(diff);
    }
}

// One-shot startup init. Singleton bot managers (RandomPlayerbotMgr,
// PlayerBotLoginMgr, etc.) lazy-instantiate on first reference; we just
// need the config file loaded here. No-op when AiPlayerbot.Enabled=0.
void World::InitPlayerbotsAtStartup()
{
    sPlayerbotAIConfig.Initialize();
    sPlayerbotAiExtension.RunStartupHooks();
}

void World::FinalizePlayerbotsPostPlayerInfo()
{
    if (!sPlayerbotAIConfig.enabled)
        return;
    RandomPlayerbotFactory::CreateRandomBots();
    // AhBot::Init() scans sItemStorage for sellable items, so it must run
    // after SetInitialWorldSettings() has finished loading world data.
    auctionbot.Init();
}

// Outgoing-packet interceptor (called from WorldSession::SendPacket). For a
// real player returns false → packet goes to the network normally. For a
// bot Player returns true after handing the packet to the AI to react to
// (group invites → auto-accept, BG status, vendor errors, ...); the AI
// suppresses the network send.
bool Player_DispatchBotOutgoingPacket(Player* player, WorldPacket const& packet)
{
    if (!player) return false;
    PlayerbotAI* ai = player->GetPlayerbotAI();
    if (!ai) return false;
    ai->HandleBotOutgoingPacket(packet);
    return true;
}

// Incoming-packet interceptor (called from WorldSession::ProcessPackets). Feeds the
// player's action packets to PlayerbotMgr::HandleMasterIncomingPacket so his bots can
// mirror quest accepts, gossip, quest shares, etc. Never suppresses the packet.
void Player_DispatchMasterIncomingPacket(Player* player, WorldPacket const& packet)
{
    if (!player) return;
    if (PlayerbotMgr* mgr = player->GetPlayerbotMgr())
        mgr->HandleMasterIncomingPacket(packet);
}

// Chat dispatcher: feeds the master's chat to every bot that listens. Without
// this, in-party "+heal" / "stay" / "co" commands don't reach any bot. Bots
// owned by the master and matching random bots both get the message.
void Player_DispatchBotChatCommand(Player* master, uint32 type, std::string const& msg, uint32 lang, std::string const& to)
{
    if (!master || !sPlayerbotAIConfig.enabled)
        return;

    // Addon / whisper channel: DungeonClear companion addon messages.
    if (lang == LANG_ADDON || type == CHAT_MSG_ADDON)
    {
        if (sPlayerbotAiExtension.HandleAddonMessage(master, msg))
            return;
    }

    if (PlayerbotMgr* mgr = master->GetPlayerbotMgr())
        mgr->HandleCommand(type, msg, lang, to);

    sRandomPlayerbotMgr.HandleCommand(type, msg, *master, "", master->GetTeam(), lang, to);
}

uint8 Playerbot_GetAllowedRoles(Player* bot)
{
    if (!bot || !bot->GetPlayerbotAI())
        return 0;

    // BotRoles and LFT_ROLE_* share their bit values - tank 1, healer 2, dps 4 -
    // so the mask carries over unchanged.
    return uint8(AiFactory::GetPlayerRoles(bot));
}

void Playerbot_SetForcedRole(Player* bot, uint8 role)
{
    if (!bot)
        return;

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    if (!ai)
        return;

    if (ai->GetForcedRole() == role)
        return;

    ai->SetForcedRole(role);

    // Strategies alone are not enough. A balance druid handed the tank slot
    // keeps balance talents and tanks in caster form; AutoSelectTalents does
    // know about roles, but only when picking a spec for the first time - with
    // one already stored it continues that one and the role never comes up.
    //
    // So when the tree cannot fill the role at all, drop the stored choice and
    // let it choose again with the role in hand. Only for random bots: someone
    // else's bot keeps the spec its owner gave it. And only on a real
    // contradiction - a feral druid does not respec just because it alternates
    // between tanking and dps.
    if (role != BotRoles::BOT_ROLE_NONE &&
        bot->GetLevel() >= 10 &&
        sRandomPlayerbotMgr.IsRandomBot(bot))
    {
        BotRoles const current = AiFactory::GetPlayerRoles(bot);

        // And only when the class can actually reach the role. AutoSelectTalents
        // does not give up if no premade spec matches: it falls back to every
        // spec of the class and picks one. So a shaman told to tank had its
        // talents wiped and came back as restoration - worse off than before,
        // and still not a tank. Ask first, and leave the bot alone if the answer
        // is no.
        bool const reachable =
            !ChangeTalentsAction::getPremadePaths(bot->getClass(), "", (BotRoles)role).empty();

        if (!(current & role) && reachable)
        {
            sRandomPlayerbotMgr.SetValue(bot->GetGUIDLow(), "specNo", 0);
            sRandomPlayerbotMgr.SetValue(bot->GetGUIDLow(), "specLink", 0, "");

            bot->ResetTalents(true);

            std::ostringstream out;
            ChangeTalentsAction::AutoSelectTalents(bot, &out, (BotRoles)role);

            sLog.outBasic("LFT: %s respecced for role %u: %s",
                bot->GetName(), uint32(role), out.str().c_str());
        }
    }

    ai->ResetStrategies();
}

bool ChatHandler::HandleDungeonClearCommand(char* args)
{
    return sPlayerbotAiExtension.HandleDcCommand(this, args);
}
