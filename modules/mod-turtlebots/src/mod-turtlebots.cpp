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
#include "WorldSession.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "Map.h"
#include "Creature.h"
#include "PathFinder.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
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

    // A named world point lifted from ai_playerbot_named_location.
    struct NamedLoc { float x, y, z, o; std::string name; };

    // Orgrimmar hub where residents live (proven-walkable spot from the demo AI).
    const uint32 CITY_MAP = 1;
    const float CITY_X = 1568.0f, CITY_Y = -4405.87f, CITY_Z = 8.13f, CITY_O = 0.0f;

    // Headless bots have no client to acknowledge teleports; do it for them, or
    // they get stuck "being teleported". Mirrors the core PlayerBotAI.
    void CompleteBotTeleport(Player* bot)
    {
        if (bot->IsBeingTeleportedNear())
        {
            WorldPacket data(MSG_MOVE_TELEPORT_ACK, 10);
            data << bot->GetObjectGuid();
            data << uint32(0) << uint32(0);
            bot->GetSession()->HandleMoveTeleportAckOpcode(data);
        }
        if (bot->IsBeingTeleportedFar())
            bot->GetSession()->HandleMoveWorldportAckOpcode();
    }

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

    enum BotRole : uint8 { ROLE_RESIDENT = 0, ROLE_ADVENTURER = 1 };

    // A sane creature to grind: a mob, not a critter / boss / totem, attackable,
    // and not several levels above us.
    bool IsGrindable(Player* bot, Unit* t)
    {
        if (!t || !t->IsAlive() || !t->IsCreature())
            return false;
        Creature* c = t->ToCreature();
        if (!c || c->IsWorldBoss() || c->IsTotem())
            return false;
        if (c->GetCreatureType() == CREATURE_TYPE_CRITTER)
            return false;
        // SelectNearestTarget already returns an attackable enemy; just keep the
        // fight fair by skipping mobs several levels above us.
        if (int32(t->GetLevel()) > int32(bot->GetLevel()) + 2)
            return false;
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
            _enabled   = sConfig.GetBoolDefault("mod-turtlebots.Enable", true);
            _target    = sConfig.GetIntDefault("mod-turtlebots.Count", 3);
            _residents = sConfig.GetIntDefault("mod-turtlebots.Residents", 1);
            _advLevel  = sConfig.GetIntDefault("mod-turtlebots.AdventurerLevel", 10);
            sLog.outString("[mod-turtlebots] loaded (enable=%u, target=%u, residents=%u, advLevel=%u).",
                           _enabled ? 1 : 0, _target, _residents, _advLevel);
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

            // Lift named locations once, after the world is up.
            if (!_locLoaded) { LoadNamedLocations(); _locLoaded = true; }

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
                    _nextAt.erase(low);
                    _role.erase(low);
                    ++processed;
                    continue; // swapped element now sits at _cursor
                }

                ++_cursor;
                ++processed;
                if (bot && bot->IsInWorld())
                {
                    CompleteBotTeleport(bot);      // finish any pending teleport
                    if (RoleOf(low) == ROLE_ADVENTURER)
                        DriveAdventurer(bot);      // handles its own death/revive
                    else if (bot->IsAlive())
                        DriveResident(bot);
                }
            }
        }

        uint8 RoleOf(uint32 low) const
        {
            auto it = _role.find(low);
            return it == _role.end() ? uint8(ROLE_RESIDENT) : it->second;
        }

        void LoadNamedLocations()
        {
            QueryResult* r = WorldDatabase.PQuery(
                "SELECT map_id, position_x, position_y, position_z, orientation, name "
                "FROM ai_playerbot_named_location");
            if (!r)
                return;
            uint32 n = 0;
            do
            {
                Field* f = r->Fetch();
                NamedLoc l;
                l.x = f[1].GetFloat();
                l.y = f[2].GetFloat();
                l.z = f[3].GetFloat();
                l.o = f[4].GetFloat();
                l.name = f[5].GetCppString();
                _locByMap[f[0].GetUInt32()].push_back(l);
                ++n;
            } while (r->NextRow());
            delete r;
            sLog.outString("[mod-turtlebots] loaded %u named locations across %u maps.",
                           n, uint32(_locByMap.size()));
        }

        // Send the bot toward a nearby named location on its map (sampled), or
        // false if none is within range so the caller can wander randomly.
        bool TryRoamToNamedLocation(Player* bot)
        {
            auto it = _locByMap.find(bot->GetMapId());
            if (it == _locByMap.end() || it->second.empty())
                return false;
            std::vector<NamedLoc> const& v = it->second;

            float const bx = bot->GetPositionX();
            float const by = bot->GetPositionY();
            float const bz = bot->GetPositionZ();
            float const minR2 = 20.0f * 20.0f;
            float const maxR2 = 150.0f * 150.0f;   // keep roaming local
            for (int tries = 0; tries < 12; ++tries)
            {
                NamedLoc const& l = v[urand(0, uint32(v.size()) - 1)];
                float dx = l.x - bx, dy = l.y - by, dz = l.z - bz;
                float d2 = dx * dx + dy * dy;
                if (d2 <= minR2 || d2 >= maxR2)
                    continue;
                // Skip targets that sit well above/below us -- avoids sending the
                // bot up cliffs and rock faces.
                if (std::fabs(dz) > 8.0f)
                    continue;

                // Require a clean navmesh path that also avoids steep slopes, and
                // is not much longer than the straight line (a long, winding path
                // means climbing around terrain). No shortcut / partial / no-path.
                PathInfo path(bot);
                path.ExcludeSteepSlopes();
                path.calculate(l.x, l.y, l.z);
                uint32 const t = uint32(path.getPathType());
                if (!(t & PATHFIND_NORMAL) ||
                    (t & (PATHFIND_SHORTCUT | PATHFIND_INCOMPLETE | PATHFIND_NOPATH)))
                    continue;

                float straight = std::sqrt(d2 + dz * dz);
                if (path.Length() > straight * 1.6f)
                    continue;

                bot->GetMotionMaster()->MovePoint(0, l.x, l.y, l.z, MOVE_PATHFINDING);
                return true;
            }
            return false;
        }

        static void StandUp(Player* bot)
        {
            if (bot->GetStandState() != UNIT_STAND_STATE_STAND)
                bot->SetStandState(UNIT_STAND_STATE_STAND);
        }

        void WanderNearbyFlat(Player* bot)
        {
            Map* map = bot->GetMap();
            if (!map)
                return;
            float x = bot->GetPositionX();
            float y = bot->GetPositionY();
            float z = bot->GetPositionZ();
            float const startZ = z;
            if (map->GetWalkRandomPosition(nullptr, x, y, z, frand(6.0f, 18.0f)) &&
                std::fabs(z - startZ) < 6.0f)
                bot->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING);
        }

        // Resident (city-life) behaviour: live in the city, calm ambient roaming.
        void DriveResident(Player* bot)
        {
            uint32 const low = bot->GetGUIDLow();
            // Move the resident into the city once; then they live/roam there.
            if (!_placed.count(low))
            {
                _placed.insert(low);
                bot->TeleportTo(CITY_MAP, CITY_X, CITY_Y, CITY_Z, CITY_O);
                return; // teleport finishes next tick via CompleteBotTeleport
            }

            MotionMaster* mm = bot->GetMotionMaster();
            // Walking -> let the core carry it; only decide when idle.
            if (mm->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
                return;

            uint32 const now = WorldTimer::getMSTime();
            uint32& nextAt = _nextAt[bot->GetGUIDLow()];
            if (nextAt == 0)
                nextAt = now + urand(1000, 6000); // stagger the first decision
            if (now < nextAt)
                return; // standing calmly between actions

            // Calm town-life cadence: mostly stand, sometimes stroll, rarely
            // emote or sit. A dwell timer between decisions keeps it unhurried.
            uint32 const roll = urand(0, 99);
            if (roll < 33)
            {
                StandUp(bot);
                if (!TryRoamToNamedLocation(bot))
                    WanderNearbyFlat(bot);
                nextAt = now + urand(6000, 11000);
            }
            else if (roll < 40)
            {
                StandUp(bot);
                static uint32 const kEmotes[] = {
                    EMOTE_ONESHOT_WAVE, EMOTE_ONESHOT_CHEER, EMOTE_ONESHOT_TALK,
                    EMOTE_ONESHOT_POINT, EMOTE_ONESHOT_LAUGH
                };
                bot->HandleEmoteCommand(kEmotes[urand(0, 4)]);
                nextAt = now + urand(7000, 12000);
            }
            else if (roll < 46)
            {
                // Small talk (Phase 1: canned lines; Ollama comes next).
                StandUp(bot);
                SayCityLine(bot);
                nextAt = now + urand(9000, 15000);
            }
            else if (roll < 50)
            {
                bot->SetStandState(UNIT_STAND_STATE_SIT);
                nextAt = now + urand(9000, 16000);
            }
            else
            {
                StandUp(bot);
                nextAt = now + urand(4000, 9000);
            }
        }

        void SayCityLine(Player* bot)
        {
            // Shared city chatter any resident may say.
            static char const* const kShared[] = {
                "Lok'tar ogar!",
                "Zug zug.",
                "Anyone heading to the Crossroads?",
                "Heard the Warchief has new orders.",
                "Trade goods, cheap! Come see.",
                "Time for a drink at the inn.",
                "Stay sharp, the Alliance grows bold."
            };
            // Martial classes: warrior / rogue / hunter / paladin.
            static char const* const kMartial[] = {
                "Long day guarding the city...",
                "Best forge in Orgrimmar, right here.",
                "My blade's thirsty for Alliance blood.",
                "Anyone up for a scrap in the ring?"
            };
            // Mana users who beg for water: warlock / priest.
            static char const* const kCaster[] = {
                "Need a mage for water over here!",
                "So low on mana... need a drink.",
                "Careful, the arcane grows restless."
            };
            // Mage: offers water and portals instead of begging.
            static char const* const kMage[] = {
                "Fresh water and food, conjured to order!",
                "Need a portal? I can open one.",
                "Mind the sheep - that used to be someone."
            };
            // Nature / spirit: shaman / druid.
            static char const* const kNature[] = {
                "The elements whisper today.",
                "The spirits are uneasy of late.",
                "Nature's balance must be kept."
            };

            char const* const* pool = kShared;
            uint32 n = sizeof(kShared) / sizeof(kShared[0]);
            // 55% shared, otherwise a class-appropriate bucket.
            if (urand(0, 99) >= 55)
            {
                switch (bot->GetClass())
                {
                    case CLASS_WARRIOR:
                    case CLASS_ROGUE:
                    case CLASS_HUNTER:
                    case CLASS_PALADIN:
                        pool = kMartial; n = sizeof(kMartial) / sizeof(kMartial[0]);
                        break;
                    case CLASS_MAGE:
                        pool = kMage; n = sizeof(kMage) / sizeof(kMage[0]);
                        break;
                    case CLASS_WARLOCK:
                    case CLASS_PRIEST:
                        pool = kCaster; n = sizeof(kCaster) / sizeof(kCaster[0]);
                        break;
                    case CLASS_SHAMAN:
                    case CLASS_DRUID:
                        pool = kNature; n = sizeof(kNature) / sizeof(kNature[0]);
                        break;
                    default:
                        break;
                }
            }
            bot->Say(pool[urand(0, n - 1)], LANG_UNIVERSAL);
        }

        // Adventurer (playing) behaviour: a basic grind loop. Improve later with
        // real rotations, looting, travel and questing.
        void DriveAdventurer(Player* bot)
        {
            // Character build (light): give it a level so it can actually win.
            if (_advLevel && bot->GetLevel() < _advLevel)
                bot->GiveLevel(_advLevel);

            // Died grinding -> revive on the spot for now (corpse runs come later).
            if (!bot->IsAlive())
            {
                bot->ResurrectPlayer(1.0f);
                bot->SpawnCorpseBones();
                return;
            }

            // In combat -> keep attacking / chase into melee.
            if (bot->IsInCombat())
            {
                if (Unit* victim = bot->GetVictim())
                {
                    if (!bot->CanReachWithMeleeAutoAttack(victim))
                        bot->GetMotionMaster()->MoveChase(victim);
                    bot->Attack(victim, true);
                }
                else
                {
                    bot->CombatStop();
                }
                return;
            }

            // Out of combat -> pick a nearby mob to grind.
            if (Unit* t = bot->SelectNearestTarget(40.0f))
            {
                if (IsGrindable(bot, t))
                {
                    bot->Attack(t, true);
                    bot->GetMotionMaster()->MoveChase(t);
                    return;
                }
            }

            // Nothing to fight nearby -> drift a little (flat) to find mobs.
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE)
            {
                uint32 const now = WorldTimer::getMSTime();
                uint32& nextAt = _nextAt[bot->GetGUIDLow()];
                if (nextAt == 0)
                    nextAt = now + urand(1000, 4000);
                if (now >= nextAt)
                {
                    WanderNearbyFlat(bot);
                    nextAt = now + urand(3000, 7000);
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
                {
                    _online.push_back(charLow);
                    _role[charLow] = (i < _residents) ? uint8(ROLE_RESIDENT)
                                                      : uint8(ROLE_ADVENTURER);
                }
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
        std::map<uint32, uint32> _nextAt;      // guid low -> ms timestamp of next decision
        std::map<uint32, std::vector<NamedLoc>> _locByMap; // map id -> named locations
        bool _locLoaded = false;
        std::map<uint32, uint8> _role;         // guid low -> BotRole
        std::set<uint32> _placed;              // residents already moved into the city
        uint32 _residents = 1;                 // first N bots are residents
        uint32 _advLevel  = 10;                // level given to adventurers
    };
}

void Addmod_turtlebotsScripts()
{
    new TurtleBotsWorldScript();
}
