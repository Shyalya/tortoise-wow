/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestDriver.h"

#include <algorithm>

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Util.h"  // urand lives here on this engine
#include "Timer.h"
#include "WorldSession.h"

#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"

namespace DcTestDriver
{
    namespace
    {
        ObjectGuid _guid;           // resolved from the config name, cached
        std::string _resolvedName;  // name _guid was resolved for (conf can change)
        bool _loginIssued = false;
        bool _initialized = false;

        // Auto-provisioning state. _provisionTried latches per conf name so a
        // refusal (bad name, forbidden account) is reported once instead of
        // being retried by every command; _awaitingFlush holds the login back
        // until the character we just created is actually readable from the DB.
        bool _provisionTried = false;
        std::string _provisionRefusal;  // replayed, so the retry keeps the real reason
        bool _awaitingFlush = false;
        uint32 _flushProbedAt = 0;  // getMSTime() of the last "is it there yet" probe

        std::string ConfName()
        {
            // showLogs=false: a missing conf line is normal (the default below is
            // authoritative), and this is re-read on every driver resolve — see
            // DcSettings.h. String-valued, so the numeric registry can't hold it.
            return sConfigMgr->GetOption<std::string>("DungeonClear.TestRun.DriverCharacter",
                                                      "Dcdriver", false);
        }

        // Account that owns the driver character. Empty turns auto-provisioning
        // off entirely, for operators who would rather create it by hand.
        std::string ConfAccount()
        {
            return sConfigMgr->GetOption<std::string>("DungeonClear.TestRun.DriverAccount",
                                                      "dcdriver", false);
        }

        // Resolve (and re-resolve after a conf change) the driver's guid.
        ObjectGuid ResolveGuid()
        {
            std::string const name = ConfName();
            if (name.empty())
                return ObjectGuid::Empty;
            if (_guid && name == _resolvedName)
                return _guid;
            if (name != _resolvedName)
            {
                // A renamed driver is a different character — the new name
                // gets its own provisioning attempt.
                _provisionTried = false;
                _provisionRefusal.clear();
                _awaitingFlush = false;
            }
            _guid = sCharacterCache->GetCharacterGuidByName(name);
            _resolvedName = name;
            _initialized = false;
            _loginIssued = false;
            return _guid;
        }

        // The manual recipe, for every case where we won't or can't do it.
        std::string ManualSetup(std::string const& name)
        {
            return "test driver character '" + name +
                   "' not found — create it on a dedicated bot account and set "
                   "DungeonClear.TestRun.DriverCharacter";
        }

        // A password for an account nobody is meant to log into. The character
        // is logged in headlessly through the fake-session path, which never
        // authenticates, so this is written once and deliberately never
        // recorded anywhere — an operator who wants the account back uses
        // `account set password`.
        std::string RandomPassword()
        {
            static char const alphabet[] =
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            std::string out;
            out.reserve(MAX_PASS_STR - 1);
            for (uint32 i = 0; i < MAX_PASS_STR - 1; ++i)
                out += alphabet[urand(0, sizeof(alphabet) - 2)];
            return out;
        }

        // Create the driver's account and character when they don't exist.
        //
        // Every `.dc test` that isn't typed by an in-game GM needs this
        // character, and that is all of them from the console, the AC Command
        // Deck and Test Deck (SOAP commands run through the same CLI queue).
        // Without this the harness is unusable until someone reads a config
        // comment, makes an account and logs into the game with a client to
        // make one level-1 character.
        //
        // Mirrors RandomPlayerbotFactory::CreateRandomBot, the core's only
        // headless character-creation path: a detached WorldSession, a
        // CharacterCreateInfo, Player::Create, SaveToDB, then the cache entry
        // that makes the name resolvable. Human warrior with default
        // appearance — nothing about a parked GM stand-in depends on either,
        // and it is a valid race/class pair on every realm.
        //
        // True when the character now exists (its DB write may still be in
        // flight — see _awaitingFlush).
        bool TryProvision(std::string const& name, std::string* why)
        {
            std::string const account = ConfAccount();
            if (account.empty())
            {
                // Explicitly opted out of auto-provisioning.
                *why = ManualSetup(name);
                return false;
            }

            // Refuse a name the lookup could never find again. Player::Create
            // does not validate names, so creating "dcdriver" would succeed
            // and then never resolve through GetCharacterGuidByName's exact
            // match — provisioning forever, one orphan character per attempt.
            std::string normalized = name;
            if (!normalizePlayerName(normalized) || normalized != name ||
                ObjectMgr::CheckPlayerName(name, true) != CHAR_NAME_SUCCESS)
            {
                *why = "DungeonClear.TestRun.DriverCharacter = '" + name +
                       "' is not a usable character name" +
                       (normalized != name ? " (did you mean '" + normalized + "'?)" : "") +
                       " — the driver could not be created";
                return false;
            }

            uint32 accountId = sAccountMgr.GetId(account);
            if (!accountId)
            {
                AccountOpResult const res =
                    sAccountMgr.CreateAccount(account, RandomPassword());
                if (res != AOR_OK)
                {
                    *why = "could not create the test driver account '" + account +
                           "' (error " + std::to_string(static_cast<uint32>(res)) +
                           ") — " + ManualSetup(name);
                    return false;
                }
                accountId = sAccountMgr.GetId(account);
                if (!accountId)
                {
                    // The INSERT travels through this engine's async DB queue,
                    // so an immediate read-back legitimately misses the row.
                    // Un-latch instead of wedging provisioning for good - the
                    // next attempt finds the account and carries on.
                    _provisionTried = false;
                    *why = "test driver account '" + account +
                           "' is still being written — run 'dc test start' "
                           "again in a moment";
                    return false;
                }
                LOG_INFO("playerbots.dungeonclear",
                         "TESTDRIVER created account '{}' ({}) for the test driver "
                         "(password randomised and not recorded; use `account set "
                         "password` if you need it)", account, accountId);
            }

            // The rotation logs its own accounts' characters in and out on its
            // own schedule, which would pull the driver out from under a live
            // run. Refuse rather than produce an intermittently broken harness.
            auto const& rnd = sPlayerbotAIConfig.randomBotAccounts;
            if (std::find(rnd.begin(), rnd.end(), accountId) != rnd.end())
            {
                *why = "test driver account '" + account +
                       "' is one of AiPlayerbot.RandomBotAccounts — the bot rotation "
                       "would log the driver out mid-run. Point "
                       "DungeonClear.TestRun.DriverAccount at a plain account";
                return false;
            }

            // SEC_PLAYER: the driver elevates its own session at init
            // (SetSecurity below in Tick), so the account itself never needs
            // a GM level.
            // vmangos session ctor: (id, socket, sec, mute, locale, ip, binaryIp)
            // - no expansion/os/build knobs to fake.
            WorldSession* session =
                new WorldSession(accountId, nullptr, SEC_PLAYER,
                                 /*mute*/ 0, LOCALE_enUS, /*ip*/ "", 0);

            Player* player = new Player(session);
            player->GetMotionMaster()->Initialize();
            // vmangos creation is parameter-direct (no CharacterCreateInfo):
            // human warrior, every appearance index 0.
            if (!player->Create(sObjectMgr.GeneratePlayerLowGuid(), name,
                                RACE_HUMAN, CLASS_WARRIOR, GENDER_MALE,
                                /*skin*/ 0, /*face*/ 0, /*hairStyle*/ 0,
                                /*hairColor*/ 0, /*facialHair*/ 0))
            {
                player->CleanupsBeforeDelete();
                delete player;
                delete session;
                *why = "could not create the test driver character '" + name +
                       "' on account '" + account + "' — " + ManualSetup(name);
                return false;
            }

            player->setCinematic(2);          // skip the intro movie on login
            player->SetAtLoginFlag(AT_LOGIN_NONE);
            player->SaveToDB(true, false);
            sCharacterCache->AddCharacterCacheEntry(
                player->GetObjectGuid(), accountId, player->GetName(), player->getGender(),
                player->getRace(), player->getClass(), player->GetLevel());

            ObjectGuid const guid = player->GetObjectGuid();
            player->CleanupsBeforeDelete();
            delete player;
            delete session;

            // SaveToDB queues; the login path loads the character back out of
            // the database, so issuing it now would race the write. Tick()
            // releases the hold once the row is readable.
            _awaitingFlush = true;
            _flushProbedAt = getMSTime();
            LOG_INFO("playerbots.dungeonclear",
                     "TESTDRIVER created character '{}' ({}) on account '{}' ({}) — "
                     "waiting for the character save to land", name, guid.ToString(),
                     account, accountId);
            return true;
        }

        Player* FindOnline()
        {
            if (!_guid)
                return nullptr;
            Player* p = ObjectAccessor::FindPlayer(_guid);
            return p && p->IsInWorld() ? p : nullptr;
        }
    }

    Player* Get()
    {
        ResolveGuid();
        return _initialized ? FindOnline() : nullptr;
    }

    Readiness Ensure(std::string* why)
    {
        if (Get())
            return Readiness::Ready;

        if (!ResolveGuid())
        {
            std::string const name = ConfName();
            if (name.empty())
            {
                if (why)
                    *why = "DungeonClear.TestRun.DriverCharacter is empty — the "
                           "harness has no GM to run as";
                return Readiness::Unavailable;
            }

            // One attempt per conf name: a refusal is a standing condition, and
            // retrying it on every command would just repeat the message.
            if (_provisionTried)
            {
                if (why)
                    *why = _provisionRefusal.empty() ? ManualSetup(name)
                                                     : _provisionRefusal;
                return Readiness::Unavailable;
            }
            _provisionTried = true;

            std::string provisionWhy;
            if (!TryProvision(name, &provisionWhy))
            {
                LOG_WARN("playerbots.dungeonclear", "TESTDRIVER {}", provisionWhy);
                _provisionRefusal = provisionWhy;
                if (why)
                    *why = provisionWhy;
                return Readiness::Unavailable;
            }

            // Resolve again so _guid picks up the cache entry we just added.
            if (!ResolveGuid())
            {
                if (why)
                    *why = ManualSetup(name);
                return Readiness::Unavailable;
            }
            if (why)
                *why = "created the test driver character '" + name + "' — it is "
                       "logging in";
            return Readiness::PendingLogin;
        }

        if (_awaitingFlush)
        {
            if (why)
                *why = "test driver '" + _resolvedName + "' was just created and is "
                       "still being written to the database";
            return Readiness::PendingLogin;
        }

        if (!_loginIssued)
        {
            // Masterless fake-session login (the random-bot path; the driver's
            // account is not a random account, so the rotation ignores it).
            sRandomPlayerbotMgr.AddPlayerBot(_guid, 0);
            _loginIssued = true;
            LOG_INFO("playerbots.dungeonclear", "TESTDRIVER logging in '{}' ({})",
                     _resolvedName, _guid.ToString());
        }

        if (why)
            *why = "test driver '" + _resolvedName + "' is logging in";
        return Readiness::PendingLogin;
    }

    bool EnsureOnline(std::string* whyPending)
    {
        std::string why;
        if (Ensure(&why) == Readiness::Ready)
            return true;
        if (whyPending)
            *whyPending = why + " — retry in a few seconds";
        return false;
    }

    void Tick()
    {
        // A just-created driver is not loadable until its queued INSERTs have
        // run, and the login path reads the character back out of the
        // database — a login issued too early fails and latches _loginIssued
        // with nothing to un-latch it. So hold until the row is really there.
        //
        // Asking for the row beats waiting on the write queue to empty: the
        // queue carries every other character save on the realm and on a busy
        // one it may never read zero, which would strand the driver offline.
        if (_awaitingFlush)
        {
            // Throttled: this is a synchronous query on the world thread, and
            // the answer cannot change faster than the write behind it lands.
            if (GetMSTimeDiffToNow(_flushProbedAt) < 500)
                return;
            _flushProbedAt = getMSTime();

            // No prepared-statement layer here - the probe is the same
            // "is the row visible yet" SELECT, escaped by hand.
            std::string escapedName = _resolvedName;
            CharacterDatabase.escape_string(escapedName);
            QueryResult* probe = CharacterDatabase.PQuery(
                "SELECT guid FROM characters WHERE name = '%s'", escapedName.c_str());
            if (!probe)
                return;  // still queued — poll again next tick
            // The row is real now - refresh sObjectMgr's player cache from
            // it: every name lookup here reads that CACHE, and the refresh at
            // creation time ran before the async save had landed, so the cache
            // still thought the character did not exist.
            uint32 const landedLowGuid = (*probe)[0].GetUInt32();
            delete probe;
            sObjectMgr.LoadPlayerCacheData(landedLowGuid);
            _awaitingFlush = false;
            LOG_INFO("playerbots.dungeonclear",
                     "TESTDRIVER character save landed — '{}' can log in now",
                     _resolvedName);
        }

        if (_initialized)
        {
            // A vanished driver (kick, crash recovery) re-arms the login so the
            // next EnsureOnline can bring it back.
            if (_loginIssued && !FindOnline())
            {
                _initialized = false;
                _loginIssued = false;
            }
            return;
        }
        if (!_loginIssued)
            return;

        Player* driver = FindOnline();
        PlayerbotAI* ai = driver ? GET_PLAYERBOT_AI(driver) : nullptr;
        if (!driver || !ai)
            return;  // still loading — poll again next tick

        // One-time setup, in dependency order:
        // 1. Its own PlayerbotMgr, so GET_PLAYERBOT_MGR(driver) resolves for
        //    AddPlayerBot / LogoutPlayerBot (the AI map and mgr map are
        //    separate registries — both can exist for one guid).
        if (!GET_PLAYERBOT_MGR(driver))
            CreateBotMgr(driver); // this tree's registry seam (BotSlots/HostHooks)

        // 2. Self-mastered: the stock real-player-master gate resolves the
        //    master via IsSelfBot(master) (master == bot; pre-PR-2592 this was
        //    masterBotAI->IsRealPlayer()), so this one line is what keeps the
        //    stock fast path (react delay etc.) for every run the driver issues.
        ai->SetMaster(driver);

        // 3. Neutralize. The masterless login installed the random-bot
        //    strategy set (grind/travel/rpg) — re-derive for the now
        //    self-mastered "real player" first (the .playerbots self flow),
        //    then pin it in place. GM mode also drops it from mob
        //    threat/visibility entirely.
        // 2b. Actually a GM. The playerbots fake-session login hardcodes
        // SEC_PLAYER (PlayerbotMgr.cpp: `new WorldSession(..., SEC_PLAYER,
        // ...)`) regardless of the account's real gmlevel, so the driver
        // looked like a plain player to every security check — including the
        // dc commands' GM allowance, which is what lets the harness drive a
        // bot party it is deliberately not a member of. Without this, every
        // `dc on` the harness issued was refused and each run died at setup.
        // SetGameMaster below is only the GM *mode* flag; it does not touch
        // session security.
        if (WorldSession* session = driver->GetSession())
            session->SetSecurity(SEC_GAMEMASTER);

        ai->ResetStrategies();
        ai->ChangeStrategy("+stay", BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+passive", BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+passive", BOT_STATE_COMBAT);
        driver->SetGameMaster(true);

        _initialized = true;
        LOG_INFO("playerbots.dungeonclear",
                 "TESTDRIVER ready: '{}' online (account {}, security {}), self-mastered, "
                 "parked at map {} {:.1f} {:.1f} {:.1f}",
                 driver->GetName(), driver->GetSession()->GetAccountId(),
                 static_cast<uint32>(driver->GetSession()->GetSecurity()), driver->GetMapId(),
                 driver->GetPositionX(), driver->GetPositionY(), driver->GetPositionZ());
    }
}
