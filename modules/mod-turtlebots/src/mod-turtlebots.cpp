/*
 * mod-turtlebots — playerbots as a pure module on the bot-helpers headless seam.
 *
 * No core weaving: bots are logged in through the generic core API
 * World::StartHeadlessSession(). Account/character bootstrap uses the core's
 * reusable AccountMgr + CharacterCreation helpers. All bot behaviour lives here.
 *
 * The module reconciles toward a target bot count on a periodic tick. This
 *  - tolerates async DB writes (account/char creation lands a tick or two later),
 *  - is idempotent across restarts (accounts/chars persist and are reused),
 *  - and gives a live "target" a later web/panel control can change.
 *
 * Phase 1: bring the target number of bots online (headless). No AI driver yet.
 */

#include "ScriptObjects.h"
#include "Log.h"
#include "World.h"
#include "AccountMgr.h"
#include "CharacterCreation.h"
#include "Database/DatabaseEnv.h"
#include "SharedDefines.h"
#include "Config/Config.h"
#include "ObjectGuid.h"

#include <map>
#include <string>

namespace
{
    // Valid vanilla Horde race/class combos (numeric to avoid enum-name drift).
    // Race: 2=Orc 5=Undead 6=Tauren 8=Troll. Class: 1=Warr 3=Hunter 4=Rogue
    // 5=Priest 7=Shaman 8=Mage 9=Warlock.
    struct RaceClass { uint8 race; uint8 cls; };
    const RaceClass HORDE_COMBOS[] =
    {
        { 2, 1 }, { 8, 3 }, { 6, 7 }, { 5, 8 }, { 2, 9 }, { 8, 4 },
        { 6, 1 }, { 5, 4 }, { 8, 5 }, { 2, 3 },
    };
    const uint32 HORDE_COMBO_COUNT = sizeof(HORDE_COMBOS) / sizeof(HORDE_COMBOS[0]);

    std::string BotAccountName(uint32 i) { return "TBOT" + std::to_string(i); }

    // Letter-only character name (names may not contain digits). <= 12 chars.
    std::string BotCharName(uint32 i)
    {
        std::string name = "Turtlebot";
        name += char('a' + (i / 26) % 26);
        name += char('a' + (i % 26));
        return name;
    }

    // Return account id for bot i, creating the account if needed. May return 0
    // on the tick the async INSERT is still in flight; a later reconcile retries.
    uint32 EnsureBotAccount(uint32 i)
    {
        std::string user = BotAccountName(i);
        if (uint32 accId = sAccountMgr.GetId(user))
            return accId;

        AccountOpResult res = sAccountMgr.CreateAccount(user, "turtlebot");
        if (res != AOR_OK && res != AOR_NAME_ALREDY_EXIST)
            sLog.outError("[mod-turtlebots] CreateAccount('%s') failed (code %u).", user.c_str(), uint32(res));

        // GetId() may still be 0 here (async write); the next reconcile picks it up.
        return sAccountMgr.GetId(user);
    }

    // Existing character guid on this account, or 0.
    uint32 FindExistingChar(uint32 accId)
    {
        QueryResult* result = CharacterDatabase.PQuery(
            "SELECT guid FROM characters WHERE account = %u ORDER BY guid LIMIT 1", accId);
        if (!result)
            return 0;
        uint32 guid = result->Fetch()[0].GetUInt32();
        delete result;
        return guid;
    }

    class TurtleBotsWorldScript : public WorldScript
    {
    public:
        TurtleBotsWorldScript()
            : WorldScript("mod-turtlebots_world", { WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }),
              _enabled(true), _target(3), _startDelayMs(20000), _reconcileTimer(0)
        {
        }

        void OnStartup() override
        {
            _enabled = sConfig.GetBoolDefault("mod-turtlebots.Enable", true);
            _target  = sConfig.GetIntDefault("mod-turtlebots.Count", 3);
            sLog.outString("[mod-turtlebots] loaded (enable=%u, target=%u).", _enabled ? 1 : 0, _target);
        }

        void OnUpdate(uint32 diff) override
        {
            if (!_enabled)
                return;

            // Initial settle so the world/maps are ready before we bring bots in.
            if (_startDelayMs)
            {
                if (_startDelayMs > diff) { _startDelayMs -= diff; return; }
                _startDelayMs = 0;
            }

            if (_reconcileTimer > diff) { _reconcileTimer -= diff; return; }
            _reconcileTimer = RECONCILE_INTERVAL_MS;

            Reconcile();
        }

    private:
        static const uint32 RECONCILE_INTERVAL_MS = 5000;

        void Reconcile()
        {
            uint32 online = 0;
            for (uint32 i = 0; i < _target; ++i)
                if (EnsureBotOnline(i))
                    ++online;

            if (online != _lastReportedOnline)
            {
                sLog.outString("[mod-turtlebots] %u/%u bots online.", online, _target);
                _lastReportedOnline = online;
            }
        }

        // Drive bot i one step toward being online. Returns true if it is Active.
        bool EnsureBotOnline(uint32 i)
        {
            uint32 accId = EnsureBotAccount(i);
            if (!accId)
                return false; // account INSERT still in flight; retry next tick

            // Resolve (and remember) the bot's character guid.
            uint32 charLow = _charByIndex.count(i) ? _charByIndex[i] : 0;
            if (!charLow)
            {
                charLow = FindExistingChar(accId);
                if (!charLow)
                {
                    CreateBotChar(i, accId); // async; picked up next reconcile
                    return false;
                }
                _charByIndex[i] = charLow;
            }

            ObjectGuid charGuid(HIGHGUID_PLAYER, charLow);
            HeadlessSessionState state = sWorld.GetHeadlessSessionState(charGuid);
            if (state == HeadlessSessionState::Active)
                return true;
            if (state != HeadlessSessionState::NotFound)
                return false; // Pending/Loading — in progress

            HeadlessSessionStartResult r =
                sWorld.StartHeadlessSession(accId, charGuid, LOCALE_enUS, "turtlebot");
            if (r != HeadlessSessionStartResult::Started)
            {
                sLog.outError("[mod-turtlebots] StartHeadlessSession(acc %u char %u) failed (code %u).",
                              accId, charLow, uint32(r));
                return false;
            }
            sLog.outString("[mod-turtlebots] bot %u logging in: acc=%u char=%u (%s).",
                           i, accId, charLow, BotCharName(i).c_str());
            return false; // becomes Active on a later tick
        }

        void CreateBotChar(uint32 i, uint32 accId)
        {
            RaceClass const& rc = HORDE_COMBOS[i % HORDE_COMBO_COUNT];
            CharacterCreateInfo info;
            info.name    = BotCharName(i);
            info.race    = rc.race;
            info.class_  = rc.cls;
            info.gender  = uint8(i & 1);
            info.currentRealmCharacterCountProvided = false; // core counts for us

            CharacterCreateOutcome outcome = CharacterCreation::CreateCharacter(accId, info);
            if (outcome.result != CHAR_CREATE_SUCCESS || outcome.guid.IsEmpty())
            {
                sLog.outError("[mod-turtlebots] CreateCharacter('%s' race %u class %u) failed (result %u).",
                              info.name.c_str(), rc.race, rc.cls, uint32(outcome.result));
                return;
            }
            _charByIndex[i] = outcome.guid.GetCounter();
            sLog.outString("[mod-turtlebots] created char '%s' (guid %u) for bot %u.",
                           info.name.c_str(), outcome.guid.GetCounter(), i);
        }

        bool   _enabled;
        uint32 _target;
        uint32 _startDelayMs;
        uint32 _reconcileTimer;
        uint32 _lastReportedOnline = 0xFFFFFFFF;
        std::map<uint32, uint32> _charByIndex; // bot index -> character guid low
    };
}

void Addmod_turtlebotsScripts()
{
    new TurtleBotsWorldScript();
}
