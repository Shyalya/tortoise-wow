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
#include "ObjectAccessor.h"
#include "Player.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "Map.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Util.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

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

    // --- Cast-by-name primitive (M3 foundation, used by M4 city-life) ---
    // Given a base spell id, return the highest rank of that spell chain the bot
    // actually knows (0 if none). Lets us say "cast conjure water" and get the
    // best rank without tracking rank ids by hand.
    uint32 HighestKnownRankInChain(Player* bot, uint32 baseSpellId)
    {
        uint32 chainRoot = sSpellMgr.GetFirstSpellInChain(baseSpellId);
        uint32 best = 0;
        uint8  bestRank = 0;
        for (auto const& kv : bot->GetSpellMap())
        {
            uint32 sid = kv.first;
            if (!bot->HasSpell(sid))
                continue;
            if (sSpellMgr.GetFirstSpellInChain(sid) != chainRoot)
                continue;
            uint8 r = sSpellMgr.GetSpellRank(sid);
            if (r >= bestRank)
            {
                bestRank = r;
                best = sid;
            }
        }
        return best;
    }

    // Cast the best known rank of a spell chain at target (or self). The name->id
    // map that fronts this (conjure/portal/mount by name) lands with M4, where we
    // can verify the actual Turtle spell ids against a live cast.
    bool CastByChainBase(Player* bot, uint32 baseSpellId, Unit* target)
    {
        uint32 sid = HighestKnownRankInChain(bot, baseSpellId);
        if (!sid)
            return false;
        bot->CastSpell(target ? target : bot, sid, false);
        return true;
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

            // Reconcile the roster occasionally; drive behaviour every tick.
            if (_reconcileTimer > diff)
                _reconcileTimer -= diff;
            else
            {
                _reconcileTimer = RECONCILE_INTERVAL_MS;
                Reconcile();
            }

            DriveBots();
        }

    private:
        static const uint32 RECONCILE_INTERVAL_MS = 5000;
        // How many bots we make a decision for per world tick (staggered so the
        // per-tick cost stays flat regardless of roster size). Movement itself
        // is carried by the core once a destination is set.
        static const uint32 DRIVE_SLICE = 40;

        // M3: staggered driver + simple activity state machine + roster lifecycle.
        // Each tick we make a decision for a slice of the roster. A moving bot is
        // left alone (the core carries it); an idle bot either wanders to a nearby
        // point or takes a short pause (sit / one-shot emote).
        void DriveBots()
        {
            uint32 processed = 0;
            uint32 const budget = std::min<uint32>(DRIVE_SLICE, uint32(_online.size()));
            while (processed < budget && !_online.empty())
            {
                if (_cursor >= _online.size())
                    _cursor = 0;
                uint32 low = _online[_cursor];
                ObjectGuid guid(HIGHGUID_PLAYER, low);
                Player* bot = sObjectAccessor.FindPlayer(guid);

                // Lifecycle: drop bots whose headless session is gone. Reconcile
                // re-adds them if they come back Active.
                if ((!bot || !bot->IsInWorld()) &&
                    sWorld.GetHeadlessSessionState(guid) != HeadlessSessionState::Active)
                {
                    _online[_cursor] = _online.back();
                    _online.pop_back();
                    _pauseUntil.erase(low);
                    ++processed;
                    continue; // swapped element now sits at _cursor
                }

                ++_cursor;
                ++processed;
                if (bot && bot->IsInWorld() && bot->IsAlive())
                    DriveOne(bot);
            }
        }

        void DriveOne(Player* bot)
        {
            MotionMaster* mm = bot->GetMotionMaster();
            // Busy moving -> let the core carry it; decide again once idle.
            if (mm->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
                return;

            uint32 const now = WorldTimer::getMSTime();
            uint32& pauseUntil = _pauseUntil[bot->GetGUIDLow()];
            if (now < pauseUntil)
                return; // mid-pause

            if (urand(0, 99) < 65)
            {
                // Wander to a nearby walkable point.
                bot->SetStandState(UNIT_STAND_STATE_STAND);
                Map* map = bot->GetMap();
                float x = bot->GetPositionX();
                float y = bot->GetPositionY();
                float z = bot->GetPositionZ();
                if (map && map->GetWalkRandomPosition(nullptr, x, y, z, frand(6.0f, 22.0f)))
                    mm->MovePoint(0, x, y, z, MOVE_PATHFINDING);
            }
            else
            {
                // Short pause: sit, or play a one-shot emote.
                pauseUntil = now + urand(3000, 8000);
                if (urand(0, 3) == 0)
                {
                    bot->SetStandState(UNIT_STAND_STATE_SIT);
                }
                else
                {
                    static uint32 const kEmotes[] = {
                        EMOTE_ONESHOT_WAVE, EMOTE_ONESHOT_CHEER, EMOTE_ONESHOT_TALK,
                        EMOTE_ONESHOT_POINT, EMOTE_ONESHOT_LAUGH
                    };
                    bot->HandleEmoteCommand(kEmotes[urand(0, 4)]);
                }
            }
        }

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
            {
                if (std::find(_online.begin(), _online.end(), charLow) == _online.end())
                    _online.push_back(charLow);
                return true;
            }
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
        std::vector<uint32> _online;           // character guids currently driven
        uint32 _cursor = 0;                    // round-robin position for DriveBots
        std::map<uint32, uint32> _pauseUntil;  // guid low -> ms timestamp of pause end
    };
}

void Addmod_turtlebotsScripts()
{
    new TurtleBotsWorldScript();
}
