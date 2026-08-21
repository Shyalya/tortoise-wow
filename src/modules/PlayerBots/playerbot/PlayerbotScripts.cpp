// Module-side hook implementations for the vendored playerbots tree.
//
// Everything the host used to reach through a free function or a Player::
// method now arrives here through Penqle module system hooks. The one call the
// core still makes unconditionally is World::InitPlayerbotsAtStartup(), which
// registers the objects below: a bootstrap has to come from somewhere, and a
// file-scope instance would race the ScriptRegistry containers during static
// initialisation.
//
// The hooks carry names that mean something to the core, not to this module -
// IsAIControlled, HasAIFollowers, GetAllowedRoles. A second module answering
// the same questions plugs into the same names.
//
// Bodies below are the former HostHooks.cpp implementations moved verbatim,
// instrumentation and all. Nothing is reworded here; behaviour changes belong
// in their own commit.

#include "playerbot/playerbot.h"
#include "Objects/Player.h"
#include "World.h"
#include "ScriptObjects.h"
#include "ScriptMgr.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/RandomPlayerbotFactory.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/AiFactory.h"
#include "playerbot/strategy/actions/ChangeTalentsAction.h"
#include "ahbot/AhBot.h"
#include "BotDiagnostics.h"
#include "playerbot/BotSlots.h"

class PlayerbotWorldScript : public WorldScript
{
    public:
        PlayerbotWorldScript() : WorldScript("PlayerbotWorldScript") {}

        // Was World::FinalizePlayerbotsPostPlayerInfo(). Fires at the end of
        // SetInitialWorldSettings(), where the old call sat - AhBot::Init()
        // scans sItemStorage and needs the world data already loaded.
        void OnStartup() override
        {
            if (!sPlayerbotAIConfig.enabled)
                return;
            RandomPlayerbotFactory::CreateRandomBots();
            auctionbot.Init();
        }

        // Was World::UpdatePlayerbotsTick(diff).
        void OnUpdate(uint32 diff) override
        {
            if (!sPlayerbotAIConfig.enabled)
                return;
            sRandomPlayerbotMgr.UpdateAI(diff);
            auctionbot.Update();
        }
};

class PlayerbotServerScript : public ServerScript
{
    public:
        PlayerbotServerScript() : ServerScript("PlayerbotServerScript") {}

        // Was Player_DispatchBotOutgoingPacket(). Note the inverted sense: the old
        // free function returned true to suppress, CanPacketSend returns false to
        // suppress. A bot Player never reaches the network - the AI reacts to the
        // packet instead (group invites -> auto-accept, BG status, vendor errors).
        //
        // One observable change from the move: the hook fires above the packet-stats
        // counters rather than below them, so bot packets no longer show up in
        // m_packetsCount. They never went out on a socket, so counting them as sent
        // was misleading anyway.
        bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override
        {
            if (!session)
                return true;

            Player* player = session->GetPlayer();
            if (!player)
                return true;

            PlayerbotAI* ai = GetBotAI(player);
            if (!ai)
                return true;

            ai->HandleBotOutgoingPacket(packet);
            return false;
        }

        // Was Player_DispatchMasterIncomingPacket(). Runs after the handler for this
        // opcode, so the puppets mirror an action their master has already taken.
        void OnPacketHandled(WorldSession* session, WorldPacket const& packet) override
        {
            if (!session)
                return;

            Player* player = session->GetPlayer();
            if (!player)
                return;

            if (PlayerbotMgr* mgr = GetBotMgr(player))
                mgr->HandleMasterIncomingPacket(packet);
        }
};

class PlayerbotPlayerScript : public PlayerScript
{
    public:
        PlayerbotPlayerScript() : PlayerScript("PlayerbotPlayerScript") {}

        bool IsAIControlled(Player const* player) override
        {
            return player && GetBotAI(player) != nullptr;
        }

        bool HasAIFollowers(Player const* player) override
        {
            return player && GetBotMgr(player) != nullptr;
        }

        // BotRoles and LFT_ROLE_* share their bit values - tank 1, healer 2,
        // dps 4 - so the mask carries over unchanged.
        bool GetAllowedRoles(Player const* player, uint8& roles) override
        {
            Player* bot = const_cast<Player*>(player);
            if (!bot || !GetBotAI(bot))
                return false;

            roles = uint8(AiFactory::GetPlayerRoles(bot));
            return true;
        }

        void SetForcedRole(Player* bot, uint8 role) override
        {
            if (!bot)
                return;

            PlayerbotAI* ai = GetBotAI(bot);
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

        // Was Player_DispatchBotChatCommand().
        void OnChatCommand(Player* master, uint32 type, std::string const& msg,
                           uint32 lang, std::string const& to) override
        {
            if (!master || !sPlayerbotAIConfig.enabled)
                return;

            if (PlayerbotMgr* mgr = GetBotMgr(master))
                mgr->HandleCommand(type, msg, lang, to);
        }

        // Was the CreatePlayerbotMgr() call in HandlePlayerLogin. Only a person
        // at a client gets a controller for their alts.
        //
        // The AI is not attached yet at this point - OnBotLogin runs after
        // HandlePlayerLogin returns - so asking whether the character has one
        // tells us nothing, and the old call site handed a controller to every
        // bot as well. The session address does tell us: a bot session carries
        // "disconnected/bot" or "<BOT>". Same marker the core already uses to
        // keep bots out of challenge setup.
        void OnLogin(Player* player) override
        {
            if (!player || !player->GetSession())
                return;

            std::string const& addr = player->GetSession()->GetRemoteAddress();
            if (addr == "disconnected/bot" || addr == "<BOT>")
                return;

            CreateBotMgr(player);
        }

        // Was the RemovePlayerbotAI/Mgr pair at the head of LogoutPlayer.
        void OnBeforeLogout(Player* player) override
        {
            RemoveBotAI(player);
            RemoveBotMgr(player);
        }

        // A real client is taking the character over. Stop driving it: an AI
        // still ticking on the new owner fights the login handshake and the
        // client hangs on the loading screen.
        void OnReleaseToClient(Player* player) override
        {
            RemoveBotAI(player);
        }

        bool IsMachineDriven(Player const* player) override
        {
            return !IsRealPlayer(player);
        }

        // Was Player::UpdatePlayerbotHooks(diff).
        void OnUpdate(Player* player, uint32 diff) override
        {
            if (!player || !sPlayerbotAIConfig.enabled)
                return;

            if (PlayerbotAI* ai = GetBotAI(player))
            {
                SC_PHASE("Player::UpdatePlayerbotHooks/ai.UpdateAI", player->GetName());
                ai->UpdateAI(diff);
            }
            if (PlayerbotMgr* mgr = GetBotMgr(player))
            {
                SC_PHASE("Player::UpdatePlayerbotHooks/mgr.UpdateAI", player->GetName());
                mgr->UpdateAI(diff);
            }
        }
};

void AddSC_playerbot_hooks()
{
    new PlayerbotWorldScript();
    new PlayerbotServerScript();
    new PlayerbotPlayerScript();
}
