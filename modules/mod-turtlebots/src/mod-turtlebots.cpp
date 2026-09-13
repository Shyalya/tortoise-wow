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
#include "Group.h"
#include "Item.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "Map.h"
#include "Creature.h"
#include "PathFinder.h"
#include "SpellMgr.h"
#include "SpellAuras.h"
#include "Timer.h"
#include "Util.h"
#include "Chat.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Cell.h"
#include "CellImpl.h"
#include "ObservabilityEmitter.h" // TortoiseBots telemetry (residents feed the dashboard)
#include <set>
#include <sstream>

#include <algorithm>
#include <cmath>
#include <map>
#include <ctime>
#include <set>
#include <string>
#include <vector>
#include <initializer_list>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include <deque>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

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

    // Residents never stay ghosts. A character can log in dead (it died on its last
    // adventurer shift, or a resident wandered into trouble); without this it would
    // never be driven again, because DriveResident only runs for living bots.
    void ReviveResident(Player* bot)
    {
        bot->ResurrectPlayer(1.0f);
        bot->SpawnCorpseBones();
        sLog.outString("[mod-turtlebots] %s logged in dead - revived so the resident can carry on.", bot->GetName());
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

    // Classes that carry a real single-target friendly buff.
    static bool CanBuff(uint8 cls)
    {
        return cls == CLASS_MAGE || cls == CLASS_PRIEST || cls == CLASS_DRUID || cls == CLASS_PALADIN;
    }
    static uint32 ClassPrimaryBuff(uint8 cls)
    {
        switch (cls) { case CLASS_MAGE: return 1459; case CLASS_PRIEST: return 1243;
                       case CLASS_DRUID: return 1126; case CLASS_PALADIN: return 19740; }
        return 0;
    }
    // Cast the resident's class buff(s) on a target, best known rank.
    static void CastClassBuffs(Player* res, Player* target)
    {
        switch (res->GetClass())
        {
            case CLASS_MAGE:   CastByChainBase(res, 1459, target); break;                            // Arcane Intellect
            case CLASS_PRIEST: CastByChainBase(res, 1243, target); CastByChainBase(res, 14752, target); break; // Fortitude + Divine Spirit
            case CLASS_DRUID:  CastByChainBase(res, 1126, target); break;                            // Mark of the Wild
            case CLASS_PALADIN:
            {
                uint8 const tc = target->GetClass();
                bool const caster = (tc == CLASS_MAGE || tc == CLASS_PRIEST || tc == CLASS_WARLOCK ||
                                     tc == CLASS_DRUID || tc == CLASS_SHAMAN);
                CastByChainBase(res, caster ? 19742u : 19740u, target); // Blessing of Wisdom : Might
                break;
            }
            default: break;
        }
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

    static std::set<uint32> g_turtleResidents; // driven residents, for player chat reactions
    struct PendingReaction { uint32 guid; std::string line; time_t at; };
    static std::vector<PendingReaction> g_pendingReactions;
    struct PendingGift { uint32 mage; uint32 player; uint32 castSpell; uint32 itemId; uint32 count; std::string line; uint32 atMs; };
    static std::vector<PendingGift> g_pendingGifts;
    struct PendingPortal { uint32 mage; uint32 player; uint32 spell; std::string city; time_t deadline; };
    static std::vector<PendingPortal> g_pendingPortals;
    static std::map<uint32, time_t> g_botTradeDeadline; // botGuid -> when to auto-cancel a stale trade
    struct PendingTradeFill { uint32 bot; uint32 player; uint32 itemId; uint32 count; uint32 atMs; };
    static std::vector<PendingTradeFill> g_pendingTradeFill; // fill a trade window a beat after opening it
    static std::set<uint32> g_botInitiatedTrade; // botGuid -> we opened this trade (give path)
    static std::set<uint32> g_botBegunTrade;     // botGuid -> we completed a player-opened handshake
    struct SellIntent { uint32 resident; uint32 itemEntry; uint32 deadlineMs; uint32 price; };
    static std::map<uint32, SellIntent> g_sellIntent;  // playerGuid -> pending sell-to-resident
    static std::map<uint32, uint64> g_botBuyPricedFor; // botGuid -> signature of the goods we priced
    static std::map<uint32, uint32> g_botPurseCap;     // botGuid -> coin purse cap (seeded on first deal)
    static std::map<uint32, uint32> g_botEscort;       // botGuid -> buyer it is walking over to trade with
    struct FishSpot { float x, y, z, o; };
    static std::vector<FishSpot> g_fishSpots;          // cached water spots near the city
    static bool g_fishSpotsLoaded = false;
    enum { FISH_MOVE = 1, FISH_CAST = 2, FISH_WAIT = 3 };
    struct FishState { uint8 phase; float x, y, z, o; uint32 atMs; uint8 casts; uint32 endMs; };
    static std::map<uint32, FishState> g_fishing;      // botGuid -> active fishing activity
    enum { COOK_FIRE = 1, COOK_WAIT = 2 };
    struct CookState { uint8 phase; uint32 atMs; };
    static std::map<uint32, CookState> g_cooking;      // botGuid -> active cooking activity
    static std::map<uint32, uint32> g_botBuffAt;       // botGuid -> next ms it may proactively buff
    struct FollowState { uint32 target; uint32 untilMs; };
    static std::map<uint32, FollowState> g_botFollow;  // botGuid -> who it walks with (player or resident)
    enum { ERR_GO = 1, ERR_DWELL = 2 };
    struct Errand { uint8 idx; uint8 phase; uint32 atMs; uint32 dwellMs; };
    static std::map<uint32, Errand> g_errand;          // botGuid -> current city errand
    static std::set<uint32> g_botTrained;              // botGuid -> profession caps raised for its level
    struct Poi { float x, y, z; char const* kind; };
    static Poi const kPois[] = {
        { 1696.f, -4456.f, 20.f, "the auction house" },
        { 1627.f, -4376.f, 12.f, "the bank" },
        { 1629.f, -4433.f, 14.f, "the inn" },
        { 2133.f, -4667.f, 47.f, "the flight master" },
        { 1990.f, -4794.f, 56.f, "the battlemaster" },
        { 1849.f, -4569.f, 25.f, "a trainer" },
        { 1650.f, -4460.f, 20.f, "the mailbox" },
    };
    static uint32 const kPoiCount = 7;
    static uint32 const kCookRaw[4]   = { 6291, 6303, 6289, 6317 }; // raw fish they catch
    static uint32 const kCookDone[4]  = { 6290, 787,  4592, 6316 }; // cooked results
    static uint32 const kCookSpell[4] = { 7751, 7752, 7753, 7754 }; // apprentice recipes
    static std::map<uint32, bool>   g_vendorItemCache; // itemEntry -> sold by some vendor?
    static time_t g_nextPurseRefill = 0;               // next slow top-up of resident purses
    struct PendingWhisper { uint32 bot; uint32 player; std::string line; time_t at; };
    static std::vector<PendingWhisper> g_pendingWhispers; // delayed whisper replies to sell offers
    static uint32 g_marketFactorPct = 100;               // pay this %% of scanned market price
    struct MarketCache { uint32 price; time_t ts; };
    static std::map<uint32, MarketCache> g_marketCache;  // itemEntry -> resolved price (short TTL)
    static std::map<uint32, uint32> g_botTradeAcceptAt; // botGuid -> ms to accept (after items are shown)
    static std::map<uint32, uint32> g_botHoldUntilMs;   // botGuid -> ms to stand still until (trade + a beat)
    static std::map<uint32, uint32> g_warlockShards; // per-warlock soul-shard reserve
    static time_t g_nextShardRefill = 0;             // next time we top the reserves up
    static std::map<uint32, std::string> g_personality; // guid -> personality key (cached)
    static char const* const kPersonalities[] = {
        "gruff", "cheerful", "terse", "haughty", "kind", "sly",
        "weary", "boisterous", "timid", "wise", "mercantile", "zealous"
    };
    static uint32 const kPersonalityCount = sizeof(kPersonalities) / sizeof(kPersonalities[0]);

    // Best conjured water/food the given player level can actually use.
    // The best conjured water/food that the mage can actually make (by its level /
    // conjure rank) AND the player can actually use (by item required level).
    static uint32 ConjuredItemForLevel(uint32 mageLevel, uint32 playerLevel, bool water)
    {
        struct Tier { uint32 mageLearn; uint32 itemReq; uint32 water; uint32 food; };
        static Tier const kT[] = {
            {  5,  1, 5350, 5349 }, // Conjure r1
            { 15,  5, 2288, 1113 }, // r2
            { 25, 25, 3772, 1487 }, // r3
            { 35, 35, 8077, 8075 }, // r4
            { 45, 45, 8078, 8076 }, // r5
            { 55, 55, 8079, 22895 } // r6
        };
        uint32 id = water ? kT[0].water : kT[0].food; // basic fallback
        for (auto const& t : kT)
            if (mageLevel >= t.mageLearn && playerLevel >= t.itemReq)
                id = water ? t.water : t.food;
        return id;
    }
    // Best Create Healthstone rank a warlock of this level can make.
    static void HealthstoneForLevel(uint32 wlLevel, uint32& spell, uint32& item)
    {
        struct Tier { uint32 learn; uint32 spell; uint32 item; };
        static Tier const kH[] = {
            { 10,  6201, 5512 }, // Minor
            { 20,  6202, 5511 }, // Lesser
            { 30,  5699, 5509 }, // Healthstone
            { 40, 11729, 5510 }, // Greater
            { 50, 11730, 9421 }  // Major
        };
        spell = 0; item = 0; // below the first learn level -> cannot make one
        for (auto const& t : kH)
            if (wlLevel >= t.learn) { spell = t.spell; item = t.item; }
    }

    // Healthstones are unique across ranks; true if the player already carries one.
    static bool PlayerHasHealthstone(Player* p)
    {
        static uint32 const kStones[] = { 5509, 5510, 5511, 5512, 9421 };
        for (uint32 id : kStones)
            if (p->HasItemCount(id, 1, true))
                return true;
        return false;
    }

    // Number of stacks requested in a chat line ("2 stacks" -> 2). Defaults to 1
    // and is capped so nobody drains a mage for a whole bag at once.
    static uint32 ParseStacks(std::string const& lower)
    {
        if (lower.find("stack") == std::string::npos)
            return 1;
        uint32 n = 0;
        for (char c : lower)
        {
            if (c >= '0' && c <= '9') { n = n * 10 + uint32(c - '0'); if (n > 99) break; }
            else if (n) break;
        }
        if (!n) n = 1;
        if (n > 4) n = 4;
        return n;
    }

    // Pick one phrasing at random so repeated encounters don't sound canned
    // (ask two out-of-shard warlocks and they won't parrot the same line).
    static char const* RPick(std::initializer_list<char const*> l)
    {
        uint32 const idx = urand(0, uint32(l.size()) - 1);
        uint32 i = 0;
        for (char const* c : l) { if (i++ == idx) return c; }
        return *l.begin();
    }

    // The resident's personality key: rolled once, persisted in the DB and
    // cached. Feeds the LLM prompt later; panel-editable via the table.
    static std::string const& PersonalityFor(uint32 guid)
    {
        auto it = g_personality.find(guid);
        if (it != g_personality.end())
            return it->second;

        std::string chosen;
        if (QueryResult* r = CharacterDatabase.PQuery(
                "SELECT personality FROM mod_turtlebots_personality WHERE guid = %u", guid))
        {
            chosen = r->Fetch()[0].GetCppString();
            delete r;
        }
        else
        {
            chosen = kPersonalities[urand(0, kPersonalityCount - 1)];
            CharacterDatabase.PExecute(
                "INSERT INTO mod_turtlebots_personality (guid, personality) VALUES (%u, '%s')",
                guid, chosen.c_str());
            sLog.outString("[mod-turtlebots] rolled personality '%s' for resident %u.",
                           chosen.c_str(), guid);
        }
        return g_personality.emplace(guid, chosen).first->second;
    }

    // ---- Async LLM voice for residents (worker thread + result queue) --------
    static bool        g_llmEnabled = true;
    static std::string g_llmUrl   = "http://100.69.207.60:11434/v1/chat/completions";
    static std::string g_llmModel = "qwen2.5:32b-instruct-q4_K_M";
    static std::string g_llmHost;
    static uint16_t    g_llmPort  = 11434;
    static std::string g_llmPath  = "/v1/chat/completions";
    static uint32      g_llmDeadlineMs = 5000;

    enum LlmMode : uint8 { LLM_SAY = 0, LLM_WHISPER = 1 };
    struct LlmResult { std::atomic<bool> done; std::string text; uint32 ms; LlmResult() : done(false), ms(0) {} };
    struct LlmJob
    {
        uint32 botGuid;      // who speaks
        uint32 targetGuid;   // player addressed (whisper target / conversation partner); 0 = nobody in particular
        uint8  mode;         // LLM_SAY / LLM_WHISPER
        uint8  depth;        // dialogue depth: 0 = opening line, replies stop at 2
        bool   off;          // no request was made (LLM off / at capacity): the fallback goes out, counted as "off"
        uint32 partnerGuid;  // resident this line answers (dialogue); 0 = none
        uint32 notBeforeMs;  // hold the finished line until then (a human beat); 0 = as soon as it is ready
        char const* tag;     // ambient / dialogue / reply / whisper / service / thanks (for the log)
        std::string fallback;
        std::shared_ptr<LlmResult> res;
        time_t deadline;
    };
    static std::vector<LlmJob> g_llmJobs;
    static bool   g_llmAmbient = true;          // ambient small talk through the LLM (canned pool = fallback)
    static uint32 g_llmAmbientPerMin = 6;       // city-wide cap on ambient lines per minute (LLM or canned)
    static uint32 g_llmDialogueChance = 60;     // % that a neighbour answers a line (bot-to-bot dialogue)
    static bool   g_llmLog = true;              // log every delivered line with its latency
    struct LlmStat { uint32 asked, delivered, ok, timeout, empty, off; uint64 sumMs; };
    static LlmStat g_llmStat = { 0, 0, 0, 0, 0, 0, 0 };
    static uint32  g_llmStatMs = 0;             // ms toward the next once-a-minute summary
    static float   g_ambientTokens = 3.f;       // chatter budget (refilled AmbientPerMinute per minute)
    static std::map<uint32, uint32> g_lastAmbientMs;   // botGuid -> last ambient line (per-bot spacing)
    struct ConvoLine { bool bot; std::string text; time_t at; };
    static std::map<uint64, std::deque<ConvoLine>> g_convo; // (bot<<32|player) -> last lines exchanged
    struct InWhisper { uint32 botLow; uint64 sender; std::string msg; };
    static std::vector<InWhisper> g_whisperInbox;  // filled on the packet hook (any thread), drained on the world thread
    static std::mutex g_whisperInboxMx;
    static std::map<uint32, uint32> g_thankCheckMs;  // botGuid -> last aura scan
    static std::map<uint64, time_t> g_thanked;       // (bot<<32|caster) -> last thank-you

    static void ParseLlmUrl()
    {
        std::string u = g_llmUrl;
        std::string const pre = "http://";
        if (u.rfind(pre, 0) == 0) u = u.substr(pre.size());
        std::string::size_type slash = u.find('/');
        std::string hostport = (slash == std::string::npos) ? u : u.substr(0, slash);
        g_llmPath = (slash == std::string::npos) ? "/" : u.substr(slash);
        std::string::size_type colon = hostport.find(':');
        if (colon == std::string::npos) { g_llmHost = hostport; g_llmPort = 80; }
        else { g_llmHost = hostport.substr(0, colon); g_llmPort = uint16_t(atoi(hostport.substr(colon + 1).c_str())); }
    }

    static std::string JsonEscape(std::string const& in)
    {
        std::string o; o.reserve(in.size() + 8);
        for (char c : in)
        {
            switch (c)
            {
                case '"':  o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n";  break;
                case '\r': break;
                case '\t': o += ' ';    break;
                default:
                    if ((unsigned char)c >= 0x20) o += c;
            }
        }
        return o;
    }

    // Blocking HTTP POST -- ONLY called from a worker thread, never the main loop.
    static std::string HttpPost(std::string const& host, uint16_t port,
                                std::string const& path, std::string const& body,
                                uint32 timeoutMs)
    {
        struct addrinfo hints; memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        char portStr[16]; snprintf(portStr, sizeof(portStr), "%u", unsigned(port));
        struct addrinfo* ai = nullptr;
        if (getaddrinfo(host.c_str(), portStr, &hints, &ai) != 0 || !ai) return "";
        int sock = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) { freeaddrinfo(ai); return ""; }
        struct timeval tv; tv.tv_sec = timeoutMs / 1000; tv.tv_usec = (timeoutMs % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char const*)&tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char const*)&tv, sizeof(tv));
        if (::connect(sock, ai->ai_addr, ai->ai_addrlen) < 0) { ::close(sock); freeaddrinfo(ai); return ""; }
        freeaddrinfo(ai);
        std::string req = "POST " + path + " HTTP/1.1\r\nHost: " + host +
                          "\r\nContent-Type: application/json\r\nContent-Length: " +
                          std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        size_t sent = 0;
        while (sent < req.size())
        {
            ssize_t n = ::send(sock, req.data() + sent, req.size() - sent, 0);
            if (n <= 0) { ::close(sock); return ""; }
            sent += size_t(n);
        }
        std::string resp; char buf[4096];
        for (;;)
        {
            ssize_t n = ::recv(sock, buf, sizeof(buf), 0);
            if (n <= 0) break;
            resp.append(buf, size_t(n));
            if (resp.size() > 262144) break;
        }
        ::close(sock);
        std::string::size_type sep = resp.find("\r\n\r\n");
        if (sep == std::string::npos) return "";
        std::string head = resp.substr(0, sep);
        std::string out = resp.substr(sep + 4);
        std::string hlow = head; for (char& c : hlow) c = char(tolower((unsigned char)c));
        if (hlow.find("transfer-encoding: chunked") != std::string::npos)
        {
            std::string decoded; std::string::size_type pos = 0;
            while (pos < out.size())
            {
                std::string::size_type eol = out.find("\r\n", pos);
                if (eol == std::string::npos) break;
                unsigned long sz = strtoul(out.substr(pos, eol - pos).c_str(), nullptr, 16);
                pos = eol + 2;
                if (sz == 0 || pos + sz > out.size()) break;
                decoded.append(out, pos, sz);
                pos += sz + 2;
            }
            return decoded;
        }
        return out;
    }

    // Pull the first "content":"..." value out of an OpenAI-style JSON body.
    static std::string ExtractContent(std::string const& json)
    {
        std::string const key = "\"content\"";
        std::string::size_type k = json.find(key);
        if (k == std::string::npos) return "";
        std::string::size_type colon = json.find(':', k + key.size());
        if (colon == std::string::npos) return "";
        std::string::size_type start = json.find('"', colon);
        if (start == std::string::npos) return "";
        ++start;
        std::string o;
        for (std::string::size_type i = start; i < json.size(); ++i)
        {
            char c = json[i];
            if (c == '\\' && i + 1 < json.size())
            {
                char n = json[++i];
                if (n == 'n' || n == 't') o += ' ';
                else if (n == '"') o += '"';
                else if (n == '\\') o += '\\';
                else o += n;
            }
            else if (c == '"') break;
            else o += c;
        }
        return o;
    }

    // Runs on a worker thread: everything it needs is passed in (the globals may be
    // rewritten by a live config reload on the world thread meanwhile).
    static std::string LlmChat(std::string const& system, std::string const& user,
                               std::string const& host, uint16_t port, std::string const& path,
                               std::string const& model, uint32 timeoutMs)
    {
        std::string body = std::string("{\"model\":\"") + model +
            "\",\"messages\":[{\"role\":\"system\",\"content\":\"" + JsonEscape(system) +
            "\"},{\"role\":\"user\",\"content\":\"" + JsonEscape(user) +
            "\"}],\"max_tokens\":80,\"temperature\":0.8}";
        std::string line = ExtractContent(HttpPost(host, port, path, body, timeoutMs));
        while (!line.empty() && (line.front() == ' ' || line.front() == '"')) line.erase(line.begin());
        while (!line.empty() && (line.back()  == ' ' || line.back()  == '"')) line.pop_back();
        if (line.size() > 250) line.resize(250);
        return line;
    }

    static char const* PersonalityDesc(std::string const& p)
    {
        if (p == "gruff")      return "gruff and curt";
        if (p == "cheerful")   return "cheerful and warm";
        if (p == "terse")      return "terse and matter-of-fact";
        if (p == "haughty")    return "haughty and condescending";
        if (p == "kind")       return "kind and caring";
        if (p == "sly")        return "sly and sarcastic";
        if (p == "weary")      return "weary and world-worn";
        if (p == "boisterous") return "loud and boisterous";
        if (p == "timid")      return "shy and timid";
        if (p == "wise")       return "calm and wise";
        if (p == "mercantile") return "shrewd and business-minded";
        if (p == "zealous")    return "dutiful and martial";
        return "plain-spoken";
    }

    static char const* ClassWord(uint8 c)
    {
        switch (c)
        {
            case CLASS_WARRIOR: return "warrior";
            case CLASS_PALADIN: return "paladin";
            case CLASS_HUNTER:  return "hunter";
            case CLASS_ROGUE:   return "rogue";
            case CLASS_PRIEST:  return "priest";
            case CLASS_SHAMAN:  return "shaman";
            case CLASS_MAGE:    return "mage";
            case CLASS_WARLOCK: return "warlock";
            case CLASS_DRUID:   return "druid";
            default:            return "adventurer";
        }
    }

    // Ask the LLM for an in-character line on a worker thread; the line (or the
    // fallback if the LLM is off, at capacity, errors, or misses the deadline) is
    // delivered on the world thread by the OnUpdate loop, as /say or as a whisper.
    static void QueueLlm(Player* bot, std::string const& system, std::string const& user,
                         std::string const& fallback, uint8 mode, uint32 targetGuid,
                         uint32 notBeforeMs, char const* tag, uint8 depth, uint32 partnerGuid)
    {
        LlmJob j;
        j.botGuid = bot->GetGUIDLow(); j.targetGuid = targetGuid; j.mode = mode; j.depth = depth;
        j.partnerGuid = partnerGuid; j.notBeforeMs = notBeforeMs; j.tag = tag; j.fallback = fallback;
        j.res = std::make_shared<LlmResult>();
        j.deadline = time(nullptr) + time_t((g_llmDeadlineMs + 999) / 1000);
        j.off = !g_llmEnabled || g_llmJobs.size() >= 8;
        if (j.off)
            j.res->done.store(true); // nothing to wait for: the fallback goes out on the next tick
        else
        {
            ++g_llmStat.asked;
            auto res = j.res;
            std::string const sys = system, usr = user, host = g_llmHost, path = g_llmPath, model = g_llmModel;
            uint16_t const port = g_llmPort;
            uint32 const timeoutMs = g_llmDeadlineMs + 2000;
            std::thread([res, sys, usr, host, port, path, model, timeoutMs]() {
                auto const t0 = std::chrono::steady_clock::now();
                std::string out = LlmChat(sys, usr, host, port, path, model, timeoutMs);
                res->ms = uint32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
                res->text = out;
                res->done.store(true);
            }).detach();
        }
        g_llmJobs.push_back(j);
    }

    // Service lines: spoken out loud, no dialogue chain behind them.
    static void QueueLlmSay(Player* bot, std::string const& system,
                            std::string const& user, std::string const& fallback)
    {
        QueueLlm(bot, system, user, fallback, LLM_SAY, 0, 0, "service", 2, 0);
    }

    // Map a "portal to <city>" request to the Horde city portal spell + the
    // mage level it needs. Returns 0 if no known city was named.
    // True if `w` appears in `s` as a whole word (so short codes like "uc" don't
    // fire inside unrelated words).
    static bool ContainsWord(std::string const& s, char const* w)
    {
        std::string word(w);
        std::string::size_type pos = 0;
        while ((pos = s.find(word, pos)) != std::string::npos)
        {
            bool leftOk  = (pos == 0) || !isalnum((unsigned char)s[pos - 1]);
            bool rightOk = (pos + word.size() >= s.size()) || !isalnum((unsigned char)s[pos + word.size()]);
            if (leftOk && rightOk) return true;
            pos += word.size();
        }
        return false;
    }

    static uint32 PortalSpellForCity(std::string const& lower, uint32& reqLevel, std::string& city)
    {
        // Full city names first (unambiguous as substrings).
        if (lower.find("orgrimmar") != std::string::npos || lower.find("ogrimmar") != std::string::npos)
            { reqLevel = 40; city = "Orgrimmar";     return 11417; }
        if (lower.find("undercity") != std::string::npos || lower.find("under city") != std::string::npos)
            { reqLevel = 40; city = "Undercity";     return 11418; }
        if (lower.find("thunder") != std::string::npos || lower.find("bluff") != std::string::npos)
            { reqLevel = 50; city = "Thunder Bluff"; return 11420; }
        if (lower.find("stormwind") != std::string::npos || lower.find("storm wind") != std::string::npos)
            { reqLevel = 40; city = "Stormwind";     return 10059; }
        if (lower.find("ironforge") != std::string::npos || lower.find("iron forge") != std::string::npos)
            { reqLevel = 40; city = "Ironforge";     return 11416; }
        if (lower.find("darnassus") != std::string::npos)
            { reqLevel = 50; city = "Darnassus";     return 11419; }
        // Short codes, matched as whole words.
        if (ContainsWord(lower, "og") || ContainsWord(lower, "org"))
            { reqLevel = 40; city = "Orgrimmar";     return 11417; }
        if (ContainsWord(lower, "uc"))
            { reqLevel = 40; city = "Undercity";     return 11418; }
        if (ContainsWord(lower, "tb"))
            { reqLevel = 50; city = "Thunder Bluff"; return 11420; }
        if (ContainsWord(lower, "sw"))
            { reqLevel = 40; city = "Stormwind";     return 10059; }
        if (ContainsWord(lower, "if"))
            { reqLevel = 40; city = "Ironforge";     return 11416; }
        if (ContainsWord(lower, "darn") || ContainsWord(lower, "dn"))
            { reqLevel = 50; city = "Darnassus";     return 11419; }
        reqLevel = 0; city.clear(); return 0;
    }

    // Speak a service line in character via the LLM (personality-flavoured),
    // falling back to the given canned line if the LLM is off/slow/at capacity.
    static void ServiceSay(Player* bot, std::string const& situation, std::string const& fallback)
    {
        std::string const& pers = PersonalityFor(bot->GetGUIDLow());
        std::string sys = std::string("You are ") + PersonalityDesc(pers) + ", a " +
            ClassWord(bot->GetClass()) + " resident who helps folk in a city in World of Warcraft. "
            "Reply with ONE short in-character line in plain English, under 20 words. "
            "No emotes, no asterisks, no quotation marks, no bracketed names.";
        QueueLlmSay(bot, sys, situation, fallback);
    }

    // Open the real city portal: the mage casts it (reagent supplied so the
    // cast plays), the portal spawns at the mage for the group to step through.
    static void ClearConjured(Player* bot); // fwd decl (defined near HandOverItems)

    static void OpenPortal(Player* mage, uint32 spell, std::string const& city)
    {
        ServiceSay(mage, std::string("A traveler wants a portal to ") + city + "; you begin opening it.",
                   std::string("One moment - opening a portal to ") + city + "...");
        if (!mage->HasSpell(spell)) mage->LearnSpell(spell, false);
        ClearConjured(mage); // free the bag so the Rune of Portals reagent fits
        mage->StoreNewItemInInventorySlot(17032, 1); // Rune of Portals reagent
        mage->StopMoving(true); // stand still so the long portal cast isn't cancelled
        CastByChainBase(mage, spell, mage);
    }

    // Bot-side group invite (mirrors WorldSession::HandleGroupInviteOpcode).
    // Returns true if an invite was sent; false if the player can't be invited.
    static bool BotInvitePlayer(Player* bot, Player* player)
    {
        if (!player || player->GetGroup() || player->GetGroupInvite())
            return false;
        Group* group = bot->GetGroup();
        if (!group)
        {
            group = new Group;
            if (!group->AddLeaderInvite(bot)) { delete group; return false; }
            if (!group->AddInvite(player))    { delete group; return false; }
        }
        else
        {
            if (group->IsFull() || !group->AddInvite(player))
                return false;
        }
        WorldPacket data(SMSG_GROUP_INVITE, 10);
        data << bot->GetName();
        if (player->GetSession())
            player->GetSession()->SendPacket(&data);
        return true;
    }

    // Hand `count` of `itemId` to the player. Prefers a real trade window (the
    // bot conjures into its own bag, opens a trade, offers the goods, accepts);
    // if the player is too far, either side is busy, or the item can't be traded,
    // it falls back to placing the items straight in the player's bags.
    // Remove leftover conjured food/water from a bot's bag so a fresh conjure
    // (or a trade fill) always has room -- cancelled trades used to pile up.
    static void ClearConjured(Player* bot)
    {
        static uint32 const kConjured[] = { 5350, 2288, 3772, 8077, 8078, 8079,   // water
                                            5349, 1113, 1487, 8075, 8076, 22895,  // food
                                            5512, 5511, 5509, 5510, 9421,         // healthstones
                                            6265, 17032 };                        // soul shard, rune of portals
        for (uint32 cid : kConjured)
            bot->DestroyItemCount(cid, 0xFFFFFFFFu, true);
    }

    // Is this item sold by any vendor? (reagents/vendor stock are excluded from buying.)
    static bool IsVendorItem(uint32 entry)
    {
        auto it = g_vendorItemCache.find(entry);
        if (it != g_vendorItemCache.end())
            return it->second;
        bool sold = false;
        if (QueryResult* r = WorldDatabase.PQuery("SELECT 1 FROM npc_vendor WHERE item=%u LIMIT 1", entry))
        {
            sold = true;
            delete r;
        }
        g_vendorItemCache[entry] = sold;
        return sold;
    }

    // First item entry in a chat message's |Hitem:<entry>:...| link, or 0.
    static uint32 ItemEntryFromLink(std::string const& msg)
    {
        size_t p = msg.find("Hitem:");
        if (p == std::string::npos) return 0;
        p += 6; uint32 e = 0; bool any = false;
        while (p < msg.size() && msg[p] >= '0' && msg[p] <= '9') { e = e * 10 + uint32(msg[p] - '0'); ++p; any = true; }
        return any ? e : 0;
    }

    // Give a resident a one-time coin purse the first time it strikes a deal.
    static void SeedPurse(Player* bot)
    {
        uint32 const low = bot->GetGUIDLow();
        if (g_botPurseCap.find(low) != g_botPurseCap.end())
            return;
        uint32 const lvl = bot->GetLevel();
        uint32 const cap = (lvl < 40 ? 20u : 20u + (lvl - 40) * 5u) * 10000u; // <40: mount money; +5g/lvl after
        g_botPurseCap[low] = cap;
        if (bot->GetMoney() < cap)
            bot->ModifyMoney(int32(cap - bot->GetMoney()));
    }

    // Approximate a realistic market value from quality + item level. The live AH is the
    // real source when stocked; this fallback keeps vendor BuyPrice from undervaluing gear.
    // Calibrated so an epic like Staff of Jordan (q4, ilvl40) is ~50g; never below 120%% of
    // the vendor buy-back.
    static uint32 MarketValue(ItemPrototype const* pr)
    {
        static uint32 const kPerIlvl2[] = { 5, 10, 40, 120, 312, 800, 800 }; // by quality 0..6
        uint32 q = pr->Quality; if (q > 6) q = 6;
        uint32 il = pr->ItemLevel ? pr->ItemLevel : (pr->RequiredLevel ? pr->RequiredLevel : 1);
        uint64 v = uint64(il) * il * kPerIlvl2[q];
        uint64 floor = uint64(pr->SellPrice) * 6 / 5;
        return uint32(v > floor ? v : floor);
    }

    // Realistic buy price from the scanned Eversong Wilds market (min buyout, else average)
    // times the configured factor. Items never scanned are queued for the offline refresher
    // and priced by the heuristic until real data arrives.
    static uint32 MarketPrice(uint32 entry, ItemPrototype const* proto)
    {
        time_t const now = time(nullptr);
        auto ci = g_marketCache.find(entry);
        if (ci != g_marketCache.end() && now - ci->second.ts < 300)
            return ci->second.price;
        uint32 base = 0; bool scanned = false;
        if (QueryResult* r = CharacterDatabase.PQuery(
                "SELECT min_buyout, avg_price FROM mod_turtlebots_market WHERE item_entry=%u", entry))
        {
            scanned = true;
            auto row = r->Fetch();
            uint32 const mb = row[0].GetUInt32();
            uint32 const avg = row[1].GetUInt32();
            base = mb ? mb : avg;
            delete r;
        }
        if (!scanned)
            CharacterDatabase.PExecute("INSERT IGNORE INTO mod_turtlebots_market_want (item_entry) VALUES (%u)", entry);
        uint32 const price = base ? uint32(uint64(base) * g_marketFactorPct / 100)
                                  : MarketValue(proto);
        g_marketCache[entry] = { price, now };
        return price;
    }

    // Copper -> "12g 34s 56c", skipping zero parts.
    static std::string MoneyStr(uint32 copper)
    {
        uint32 g = copper / 10000, sv = (copper % 10000) / 100, c = copper % 100;
        std::string out;
        if (g) out += std::to_string(g) + "g";
        if (sv) { if (!out.empty()) out += " "; out += std::to_string(sv) + "s"; }
        if (c || out.empty()) { if (!out.empty()) out += " "; out += std::to_string(c) + "c"; }
        return out;
    }

    static void QueueWhisper(Player* bot, Player* player, std::string const& line)
    {
        g_pendingWhispers.push_back({ bot->GetGUIDLow(), player->GetGUIDLow(), line,
                                      time(nullptr) + time_t(urand(2, 5)) });
    }

    // A real player whisper (not an NPC monster-whisper) so the recipient can click the
    // sender's name to reply or invite. Sent straight to the target's session.
    static void SendBotWhisper(Player* bot, Player* player, std::string const& text)
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, text, LANG_UNIVERSAL, CHAT_TAG_NONE,
            bot->GetObjectGuid(), bot->GetName(), player->GetObjectGuid(), player->GetName());
        player->GetSession()->SendPacket(&data);
    }

    // ---- Residents' voice: context for the LLM, ambient talk, dialogue, whispers, thanks ----
    void ProcessWhisperInbox(); // defined after the chat script (service requests are handed to it)

    // LLM knobs are live: read at startup and on every reconcile (5 s), so the panel
    // can switch the voice, the model or the chatter budget with "reload config".
    static void ReadLlmConfig()
    {
        g_llmEnabled        = sConfig.GetBoolDefault("mod-turtlebots.LLM.Enabled", true);
        g_llmUrl            = sConfig.GetStringDefault("mod-turtlebots.LLM.Url", "http://100.69.207.60:11434/v1/chat/completions");
        g_llmModel          = sConfig.GetStringDefault("mod-turtlebots.LLM.Model", "qwen2.5:32b-instruct-q4_K_M");
        g_llmDeadlineMs     = sConfig.GetIntDefault("mod-turtlebots.LLM.TimeoutMs", 5000);
        g_llmAmbient        = sConfig.GetBoolDefault("mod-turtlebots.LLM.Ambient", true);
        g_llmAmbientPerMin  = sConfig.GetIntDefault("mod-turtlebots.LLM.AmbientPerMinute", 6);
        g_llmDialogueChance = sConfig.GetIntDefault("mod-turtlebots.LLM.DialogueChance", 60);
        g_llmLog            = sConfig.GetBoolDefault("mod-turtlebots.LLM.Log", true);
        ParseLlmUrl();
    }

    static char const* RaceWord(uint8 r)
    {
        switch (r)
        {
            case 1: return "human";   case 2: return "orc";    case 3: return "dwarf";  case 4: return "night elf";
            case 5: return "undead";  case 6: return "tauren"; case 7: return "gnome";  case 8: return "troll";
            case 9: return "goblin";  case 10: return "high elf";
            default: return "traveler";
        }
    }

    static char const* TimeOfDay()
    {
        time_t const t = time(nullptr);
        struct tm lt; localtime_r(&t, &lt);
        int const h = lt.tm_hour;
        return h < 6 ? "night" : h < 12 ? "morning" : h < 18 ? "afternoon" : h < 22 ? "evening" : "night";
    }

    static std::string AreaName(uint32 id)
    {
        AreaEntry const* a = AreaEntry::GetById(id);
        return (a && a->Name) ? std::string(a->Name) : std::string();
    }

    // The named parts of a city (its sub-areas), so the model anchors itself to real
    // places instead of inventing buildings. Built once per zone from the area table.
    static std::string const& CityPlaces(uint32 zoneId, uint32 mapId)
    {
        static std::map<uint32, std::string> cache;
        auto it = cache.find(zoneId);
        if (it != cache.end())
            return it->second;
        std::string out; uint32 n = 0;
        for (auto itr = sAreaStorage.begin<AreaEntry>(); itr < sAreaStorage.end<AreaEntry>(); ++itr)
        {
            AreaEntry const* a = *itr;
            if (!a || a->ZoneId != zoneId || a->MapId != mapId || !a->Name || !*a->Name)
                continue;
            if (n++) out += ", ";
            out += a->Name;
            if (n >= 14) break;
        }
        return cache.emplace(zoneId, out).first->second;
    }

    static std::string ProfessionsOf(Player* bot)
    {
        static struct { uint16 id; char const* word; } const k[] = {
            { 171, "alchemist" }, { 164, "blacksmith" }, { 333, "enchanter" }, { 202, "engineer" }, { 182, "herbalist" },
            { 165, "leatherworker" }, { 186, "miner" }, { 393, "skinner" }, { 197, "tailor" } };
        std::string s;
        for (auto const& e : k)
            if (bot->HasSkill(e.id)) { if (!s.empty()) s += " and "; s += e.word; }
        return s;
    }

    // What the resident is doing right now, from the module's own state.
    static std::string ActivityOf(Player* bot)
    {
        uint32 const low = bot->GetGUIDLow();
        if (g_fishing.count(low)) return "fishing at the water's edge";
        if (g_cooking.count(low)) return "cooking your catch over a campfire";
        if (bot->GetTradeData()) return "trading with someone";
        auto e = g_errand.find(low);
        if (e != g_errand.end())
        {
            Poi const& p = kPois[e->second.idx % kPoiCount];
            return std::string(e->second.phase == ERR_GO ? "walking over to " : "standing at ") + p.kind;
        }
        auto f = g_botFollow.find(low);
        if (f != g_botFollow.end())
            if (Player* m = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, f->second.target)))
                return std::string("walking along with ") + m->GetName();
        if (bot->GetStandState() == UNIT_STAND_STATE_SIT) return "sitting down for a rest";
        return "idling in the street";
    }

    // Who is around: the nearest fellow resident, a real player, an NPC at arm's length.
    static std::string NearbyOf(Player* bot)
    {
        std::string s;
        Player* best = nullptr; float bestD = 15.f;
        for (uint32 low2 : g_turtleResidents)
        {
            if (low2 == bot->GetGUIDLow()) continue;
            Player* r = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, low2));
            if (!r || !r->IsInWorld() || r->GetMapId() != bot->GetMapId()) continue;
            float const d = bot->GetDistance(r);
            if (d < bestD) { bestD = d; best = r; }
        }
        if (best)
            s += std::string("Your neighbour ") + best->GetName() + " (a " + RaceWord(best->GetRace()) + " " +
                 ClassWord(best->GetClass()) + ") is right beside you. ";
        std::list<Player*> players;
        MaNGOS::AnyPlayerInObjectRangeCheck pchk(bot, 15.0f);
        MaNGOS::PlayerListSearcher<MaNGOS::AnyPlayerInObjectRangeCheck> psrch(players, pchk);
        Cell::VisitWorldObjects(bot, psrch, 15.0f);
        for (Player* p : players)
            if (p != bot && p->GetSession() && !p->GetSession()->IsHeadless())
            {
                s += std::string("The adventurer ") + p->GetName() + " is nearby. ";
                break;
            }
        std::list<Unit*> units;
        MaNGOS::AnyUnitInObjectRangeCheck uchk(bot, 8.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> usrch(units, uchk);
        Cell::VisitGridObjects(bot, usrch, 8.0f);
        for (Unit* u : units)
        {
            Creature* c = u->ToCreature();
            if (!c || c->IsPet() || c->IsTotem() || !c->GetName() || !*c->GetName()) continue;
            s += std::string(c->GetName()) + " stands close by. ";
            break;
        }
        return s;
    }

    // The role prompt: who the resident is, where, doing what, with whom, and the rules.
    // Behaviour is described, never quoted (a quoted example becomes the universal answer).
    static std::string ResidentSystemPrompt(Player* bot)
    {
        std::string const& pers = PersonalityFor(bot->GetGUIDLow());
        std::string const city = AreaName(bot->GetZoneId());
        std::string const sub  = AreaName(bot->GetAreaId());
        std::string const& places = CityPlaces(bot->GetZoneId(), bot->GetMapId());
        std::string const profs = ProfessionsOf(bot);
        std::string s = std::string("You are ") + bot->GetName() + ", a " + PersonalityDesc(pers) + " level " +
            std::to_string(bot->GetLevel()) + " " + RaceWord(bot->GetRace()) + " " + ClassWord(bot->GetClass()) +
            " who lives in " + (city.empty() ? std::string("the city") : city) +
            " in World of Warcraft (vanilla era, Turtle WoW server). ";
        if (!profs.empty()) s += "By trade you are a " + profs + ". ";
        s += "Right now you are " + ActivityOf(bot) + (sub.empty() ? std::string() : " in " + sub) +
             "; it is " + TimeOfDay() + ". ";
        s += NearbyOf(bot);
        if (!places.empty() && !city.empty())
            s += "Parts of " + city + " you know: " + places + ". Never invent other buildings or places. ";
        s += "Speak as this character in plain English: one short line, under 20 words, varied phrasing, "
             "stay in the era. No emotes, no asterisks, no quotation marks, no name prefixes, never mention being an AI.";
        return s;
    }

    // A short memory of one conversation (resident <-> player), so follow-ups make sense.
    static void ConvoPush(uint32 botLow, uint32 playerLow, bool fromBot, std::string const& text)
    {
        auto& d = g_convo[(uint64(botLow) << 32) | playerLow];
        d.push_back({ fromBot, text, time(nullptr) });
        while (d.size() > 6) d.pop_front();
    }

    static std::string ConvoContext(Player* bot, Player* player)
    {
        auto it = g_convo.find((uint64(bot->GetGUIDLow()) << 32) | player->GetGUIDLow());
        if (it == g_convo.end()) return std::string();
        time_t const now = time(nullptr);
        std::string s;
        for (ConvoLine const& l : it->second)
        {
            if (now - l.at > 900) continue;
            s += (l.bot ? std::string("You said: ") : std::string(player->GetName()) + " said: ") + l.text + " ";
        }
        return s.empty() ? s : "Earlier in this conversation: " + s + "Now: ";
    }

    static bool HasServiceKeyword(std::string const& lower)
    {
        static char const* const k[] = { "water", "drink", "mana", "food", "eat", "hungry", "bread", "healthstone",
                                         "health stone", "portal", "buff", "bless", "follow me", "come with",
                                         "come along", "stop following", "stay here", "wait here" };
        for (char const* w : k)
            if (lower.find(w) != std::string::npos) return true;
        return ContainsWord(lower, "port");
    }

    // Say a line out loud, with the talk gesture, and log it like any player's chat.
    static void ResidentSay(Player* b, std::string const& line)
    {
        b->Say(line, LANG_UNIVERSAL);
        b->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
        if (b->GetSession())
            sWorld.LogChat(b->GetSession(), "Say", line);
    }

    // Models like to prefix the speaker or wrap the line in an emote; the client shows the name already.
    static std::string CleanLine(std::string line, char const* botName)
    {
        std::string const pre = std::string(botName) + ":";
        if (line.compare(0, pre.size(), pre) == 0) line.erase(0, pre.size());
        while (!line.empty() && (line.front() == ' ' || line.front() == '"' || line.front() == '*')) line.erase(line.begin());
        while (!line.empty() && (line.back() == ' ' || line.back() == '"' || line.back() == '*')) line.pop_back();
        return line;
    }

    static char const* PickCityLine(Player* bot)
    {
        // Canned city chatter: the fallback when the LLM is off or does not answer in time.
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
        return pool[urand(0, n - 1)];
    }

    static void MaybeAnswerLine(Player* speaker, std::string const& line, uint8 depth, uint32 preferLow);

    // Finished line -> the world: /say or whisper, conversation memory, stats, log,
    // and possibly a neighbour's answer (dialogue).
    static void DeliverLlmLine(LlmJob const& j, std::string line, char const* how)
    {
        Player* b = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, j.botGuid));
        if (!b || !b->IsInWorld())
            return;
        line = CleanLine(line, b->GetName());
        if (line.empty())
            return; // a dialogue turn with nothing to say stays silent
        if (j.mode == LLM_WHISPER)
        {
            Player* t = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, j.targetGuid));
            if (!t || !t->IsInWorld() || !t->GetSession())
                return;
            SendBotWhisper(b, t, line);
            if (b->GetSession())
                sWorld.LogChat(b->GetSession(), "Whisp", line);
        }
        else
            ResidentSay(b, line);
        if (j.targetGuid)
            ConvoPush(j.botGuid, j.targetGuid, true, line);
        ++g_llmStat.delivered;
        if (j.off) { ++g_llmStat.off; how = "off"; }
        else if (strcmp(how, "ok") == 0) { ++g_llmStat.ok; g_llmStat.sumMs += j.res->ms; }
        else if (strcmp(how, "timeout") == 0) ++g_llmStat.timeout;
        else ++g_llmStat.empty;
        if (g_llmLog)
            sLog.outString("[mod-turtlebots] llm %s/%s %s: %s (%u ms, %s)", j.tag,
                           j.mode == LLM_WHISPER ? "whisper" : "say", b->GetName(), line.c_str(), j.res->ms, how);
        if (!j.off && (strcmp(j.tag, "ambient") == 0 || strcmp(j.tag, "dialogue") == 0))
            MaybeAnswerLine(b, line, j.depth, j.partnerGuid);
    }

    // Ambient small talk within the city-wide budget: at most AmbientPerMinute lines a
    // minute across all residents, and one per resident per minute. Returns false
    // when the budget said no (the resident just stays quiet this beat).
    static bool AmbientSay(Player* bot)
    {
        uint32 const now = WorldTimer::getMSTime();
        uint32 const low = bot->GetGUIDLow();
        auto la = g_lastAmbientMs.find(low);
        if (la != g_lastAmbientMs.end() && now - la->second < 60000)
            return false;
        if (g_ambientTokens < 1.f)
            return false;
        g_ambientTokens -= 1.f;
        g_lastAmbientMs[low] = now;
        char const* canned = PickCityLine(bot);
        if (!g_llmEnabled || !g_llmAmbient)
        {
            ResidentSay(bot, canned);
            return true;
        }
        QueueLlm(bot, ResidentSystemPrompt(bot),
                 "Say one line of small talk out loud, fitting what you are doing and who is around you right now.",
                 canned, LLM_SAY, 0, 0, "ambient", 0, 0);
        return true;
    }

    // Bot-to-bot dialogue: a neighbour within earshot answers a spoken line after a
    // human beat; the answer may be answered once more, then the exchange ends.
    static void MaybeAnswerLine(Player* speaker, std::string const& line, uint8 depth, uint32 preferLow)
    {
        if (depth >= 2 || !g_llmEnabled || !g_llmAmbient)
            return;
        uint32 const chance = depth == 0 ? g_llmDialogueChance : g_llmDialogueChance / 2;
        if (urand(0, 99) >= chance)
            return;
        Player* partner = nullptr;
        if (preferLow)
        {
            Player* p = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, preferLow));
            if (p && p->IsInWorld() && p->GetMapId() == speaker->GetMapId() && speaker->GetDistance(p) <= 15.f)
                partner = p;
        }
        if (!partner)
        {
            float bestD = 12.f;
            for (uint32 low2 : g_turtleResidents)
            {
                if (low2 == speaker->GetGUIDLow()) continue;
                Player* r = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, low2));
                if (!r || !r->IsInWorld() || r->GetMapId() != speaker->GetMapId()) continue;
                if (g_fishing.count(low2) || g_cooking.count(low2) || r->GetTradeData() || r->IsNonMeleeSpellCasted(false))
                    continue; // busy residents don't join in
                float const d = speaker->GetDistance(r);
                if (d < bestD) { bestD = d; partner = r; }
            }
        }
        if (!partner)
            return;
        partner->SetFacingTo(partner->GetAngle(speaker));
        std::string usr = std::string(speaker->GetName()) + ", standing next to you, just said: " + line +
                          " Answer them in one short line.";
        QueueLlm(partner, ResidentSystemPrompt(partner), usr, std::string(), LLM_SAY, 0,
                 WorldTimer::getMSTime() + urand(2500, 5000), "dialogue", uint8(depth + 1), speaker->GetGUIDLow());
    }

    // A real player's buff earns a thank-you (once per person per ten minutes).
    static void ThankForBuffs(Player* bot, uint32 nowMs)
    {
        uint32 const low = bot->GetGUIDLow();
        uint32& last = g_thankCheckMs[low];
        if (last && nowMs - last < 3000)
            return;
        last = nowMs;
        time_t const tnow = time(nullptr);
        for (auto const& kv : bot->GetSpellAuraHolderMap())
        {
            SpellAuraHolder* h = kv.second;
            if (!h || !h->IsPositive()) continue;
            ObjectGuid const& cg = h->GetCasterGuid();
            if (!cg.IsPlayer() || cg == bot->GetObjectGuid()) continue;
            if (tnow - h->GetAuraApplyTime() > 20) continue; // only a fresh buff
            Player* caster = sObjectAccessor.FindPlayer(cg);
            if (!caster || !caster->GetSession() || caster->GetSession()->IsHeadless()) continue;
            uint64 const key = (uint64(low) << 32) | caster->GetGUIDLow();
            auto t = g_thanked.find(key);
            if (t != g_thanked.end() && tnow - t->second < 600) continue;
            g_thanked[key] = tnow;
            SpellEntry const* se = h->GetSpellProto();
            std::string const spell = (se && !se->SpellName[0].empty()) ? se->SpellName[0] : std::string("a blessing");
            bot->SetFacingTo(bot->GetAngle(caster));
            QueueLlm(bot, ResidentSystemPrompt(bot),
                     std::string(caster->GetName()) + " just cast " + spell + " on you. Thank them in one short line.",
                     RPick({ "Thanks for the buff, friend!", "Much obliged - that helps.", "Kind of you, traveler. Thanks!" }),
                     LLM_SAY, caster->GetGUIDLow(), nowMs + urand(1000, 2500), "thanks", 2, 0);
            return;
        }
    }

    // The nine primary trade skills and the three secondaries. Residents (who are also
    // adventurers when they rotate out) get real professions so they can gather in the
    // world and craft in the city.
    static uint16 const kPrimaryProfs[]   = { 171, 164, 333, 202, 182, 165, 186, 393, 197 };
                                            // Alch BS Ench Eng Herb LW Mine Skin Tailor
    static uint16 const kSecondaryProfs[] = { 185, 356, 129, 142 }; // Cooking, Fishing, First Aid, Survival

    // Set a real skill and learn every non-superseded recipe/ability up to that value
    // (respecting race/class masks) so the profession is genuinely usable, not cosmetic.
    static void LearnSkillRecipes(Player* bot, uint16 skillId, uint16 value)
    {
        uint16 const cap = value <= 75 ? 75 : value <= 150 ? 150 : value <= 225 ? 225 : 300;
        bot->SetSkill(skillId, value, cap, uint16(cap / 75));
        uint32 const rmask = 1u << (bot->GetRace() - 1);
        uint32 const cmask = 1u << (bot->GetClass() - 1);
        if (QueryResult* r = WorldDatabase.PQuery(
                "SELECT spell_id FROM skill_line_ability WHERE skill_id=%u AND req_skill_value<=%u "
                "AND superseded_by_spell=0 AND (race_mask=0 OR race_mask & %u) "
                "AND (class_mask=0 OR class_mask & %u)", skillId, value, rmask, cmask))
        {
            do {
                uint32 const spellId = r->Fetch()[0].GetUInt32();
                if (spellId && !bot->HasSpell(spellId))
                    bot->LearnSpell(spellId, false);
            } while (r->NextRow());
            delete r;
        }
    }

    // Give a resident real, level-scaled professions once (persisted as normal character
    // skills -> editable via DB/GM, and carried over when the bot rotates out to adventure).
    static void AssignProfessions(Player* bot)
    {
        uint16 const value = 1; // start at apprentice -- bots skill their professions up themselves
        bool hasPrimary = false;
        for (uint16 pr : kPrimaryProfs)
            if (bot->HasSkill(pr)) { hasPrimary = true; break; }
        if (!hasPrimary) // roll two distinct primaries once
        {
            uint16 const a = kPrimaryProfs[urand(0, 8)];
            uint16 b = a;
            while (b == a) b = kPrimaryProfs[urand(0, 8)];
            LearnSkillRecipes(bot, a, value);
            LearnSkillRecipes(bot, b, value);
        }
        for (uint16 sp : kSecondaryProfs) // everyone keeps all secondaries (idempotent top-up)
            if (!bot->HasSkill(sp))
                LearnSkillRecipes(bot, sp, value);
    }

    // Race-appropriate basic mount spell (Orc wolf, Undead skeletal horse, Tauren kodo, Troll raptor).
    static uint32 RaceMountSpell(uint8 race)
    {
        switch (race)
        {
            case 1:  return 458;   // Human - Brown Horse
            case 2:  return 6654;  // Orc - Brown Wolf
            case 3:  return 6777;  // Dwarf - Gray Ram
            case 4:  return 8394;  // Night Elf - Striped Frostsaber
            case 5:  return 17464; // Undead - Brown Skeletal Horse
            case 6:  return 18990; // Tauren - Brown Kodo
            case 7:  return 10969; // Gnome - Blue Mechanostrider
            case 8:  return 10796; // Troll - Turquoise Raptor
            case 10: return 458;   // High Elf - horse (Alliance default)
        }
        return 0;
    }
    // Give a mount at level 40+ (learned as a real spell, like a trained mount).
    // Raise each known profession's cap to the tier the resident's level allows
    // (75/150/225/300), so professions keep skilling up instead of stalling at apprentice.
    static void TrainSkillTiers(Player* bot)
    {
        uint32 const lvl = bot->GetLevel();
        uint16 const cap = lvl >= 35 ? 300 : lvl >= 20 ? 225 : lvl >= 10 ? 150 : 75;
        static uint16 const kAllProfs[] = { 171,164,333,202,182,165,186,393,197, 185,356,129,142 };
        for (uint16 sk : kAllProfs)
        {
            if (!bot->HasSkill(sk)) continue;
            if (bot->GetSkillMax(sk) < cap)
                bot->SetSkill(sk, bot->GetSkillValuePure(sk), cap, uint16(cap / 75));
        }
    }

    static void AssignMount(Player* bot)
    {
        if (bot->GetLevel() < 40) return;
        uint32 const spell = RaceMountSpell(bot->GetRace());
        if (spell && !bot->HasSpell(spell)) bot->LearnSpell(spell, false);
    }
    // Hop on the mount for a longer journey (pond trip, later city travel).
    static void MountUp(Player* bot)
    {
        if (bot->IsMounted() || bot->IsInCombat() || bot->GetLevel() < 40) return;
        uint32 const spell = RaceMountSpell(bot->GetRace());
        if (spell && bot->HasSpell(spell)) bot->CastSpell(bot, spell, false);
    }

    static bool ItemHasStat(ItemPrototype const* pr, uint32 type)
    {
        for (int i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
            if (pr->ItemStat[i].ItemStatType == type && pr->ItemStat[i].ItemStatValue > 0) return true;
        return false;
    }
    static bool ItemGrantsHealing(ItemPrototype const* pr)
    {
        for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            if (pr->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_EQUIP || !pr->Spells[i].SpellId) continue;
            SpellEntry const* se = sSpellMgr.GetSpellEntry(pr->Spells[i].SpellId);
            if (!se) continue;
            for (int e = 0; e < MAX_EFFECT_INDEX; ++e)
                if (se->EffectApplyAuraName[e] == 135) return true; // SPELL_AURA_MOD_HEALING_DONE
        }
        return false;
    }
    // Beyond CanUseItem (proficiency): does the item's stat role fit the class?
    // Keeps a mage from buying a strength/agility or +healing piece it could merely equip.
    static bool ClassWantsItem(Player* buyer, ItemPrototype const* pr)
    {
        bool const str = ItemHasStat(pr, 4), agi = ItemHasStat(pr, 3), intel = ItemHasStat(pr, 5);
        bool const heal = ItemGrantsHealing(pr);
        switch (buyer->GetClass())
        {
            case CLASS_MAGE: case CLASS_WARLOCK:  return !(str || agi || heal); // pure caster dps
            case CLASS_PRIEST:                    return !(str || agi);         // caster, may heal
            case CLASS_WARRIOR: case CLASS_ROGUE: return !(intel || heal);      // pure melee
            default: return true; // hybrids (hunter/shaman/druid/paladin): be permissive
        }
    }

    static void HandOverItems(Player* caster, Player* plr, uint32 itemId, uint32 count)
    {
        // Open a real trade window, then fill it a beat later: the client needs
        // the window open before the item update arrives, or it renders empty.
        if (caster->GetDistance3dToCenter(plr) <= TRADE_DISTANCE &&
            !caster->GetTradeData() && !plr->GetTradeData() &&
            caster->BeginTradeWith(plr))
        {
            caster->StopMoving(true); // hold still with the window open
            g_botInitiatedTrade.insert(caster->GetGUIDLow());
            g_pendingTradeFill.push_back({ caster->GetGUIDLow(), plr->GetGUIDLow(), itemId, count,
                                           WorldTimer::getMSTime() + 1200u });
            return;
        }
        // Too far or busy -> just place it straight in the player's bags.
        uint32 remaining = count;
        while (remaining)
        {
            uint32 const c = remaining > 20u ? 20u : remaining;
            plr->StoreNewItemInInventorySlot(itemId, c);
            remaining -= c;
        }
    }

    // Put the conjured goods into a trade window that was opened a moment ago.
    static void FillTradeWindow(Player* bot, Player* plr, uint32 itemId, uint32 count)
    {
        TradeData* td = bot->GetTradeData();
        if (!td)
            return;
        ClearConjured(bot); // make room so StoreNewItem can't fail on a full bag
        int slot = 0;
        uint32 remaining = count;
        bool any = false;
        while (remaining && slot < TRADE_SLOT_TRADED_COUNT)
        {
            uint32 const c = remaining > 20u ? 20u : remaining;
            Item* it = bot->StoreNewItemInInventorySlot(itemId, c);
            if (it && it->CanBeTraded())
            {
                td->SetItem(TradeSlots(slot++), it);
                any = true;
            }
            remaining -= c;
        }
        if (any)
        {
            td->SetMoney(0);
            td->SetAccepted(true); // accept with the goods (this is the version that demonstrably worked)
            g_botTradeDeadline[bot->GetGUIDLow()] = time(nullptr) + 40;
        }
        else
            bot->TradeCancel(true);
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
            // Town mode (Kith model): residents are shadows (logged out) until a real player is
            // in their home city; then they are woken (logged in) in staggered batches and put
            // back to sleep (logged out) after a grace period without players.
            _townEnable         = sConfig.GetBoolDefault("mod-turtlebots.Town.Enable", false);
            _townWakeBatch      = sConfig.GetIntDefault("mod-turtlebots.Town.WakeBatch", 5);
            _townWakeIntervalMs = sConfig.GetIntDefault("mod-turtlebots.Town.WakeIntervalMs", 10000);
            _townSleepGraceSec  = sConfig.GetIntDefault("mod-turtlebots.Town.SleepGraceSec", 600);
            ReadTownForceAwake();
            // Stage 3 of the unified population: off-shift adventurers (TortoiseBots
            // RNDBOT characters that are not logged in) come home as residents while a
            // real player is in the city, and are released for their next shift when it
            // falls asleep. Fill caps the residents embodied per awake city (TBOT + pool);
            // 0 means "just the Residents count" (backwards compatible).
            _townFill           = sConfig.GetIntDefault("mod-turtlebots.Town.Fill", 0);
            if (!_townFill) _townFill = _residents;
            _poolPrefix         = sConfig.GetStringDefault("mod-turtlebots.Town.PoolAccountPrefix", "RNDBOT");
            _poolRefreshSec     = sConfig.GetIntDefault("mod-turtlebots.Town.PoolRefreshSec", 300);
            _releaseCooldownSec = sConfig.GetIntDefault("mod-turtlebots.Town.ReleaseCooldownSec", 600);
            sLog.outString("[mod-turtlebots] town pool: fill %u, adventurer prefix %s, refresh %u s, release cooldown %u s.",
                           _townFill, _poolPrefix.c_str(), _poolRefreshSec, _releaseCooldownSec);
            sLog.outString("[mod-turtlebots] town mode %s (wake %u per %u ms, sleep grace %u s, forced-awake zones: %u).",
                           _townEnable ? "ON" : "off", _townWakeBatch, _townWakeIntervalMs, _townSleepGraceSec,
                           uint32(_townForceAwake.size()));
            ReadLlmConfig();
            g_marketFactorPct = sConfig.GetIntDefault("mod-turtlebots.Market.FactorPct", 100);
            sLog.outString("[mod-turtlebots] voice: LLM %s (%s, ambient %s, %u lines/min, dialogue %u%%, timeout %u ms).",
                           g_llmEnabled ? "on" : "off", g_llmModel.c_str(), g_llmAmbient ? "on" : "off",
                           g_llmAmbientPerMin, g_llmDialogueChance, g_llmDeadlineMs);
            sLog.outString("[mod-turtlebots] loaded (enable=%u, target=%u, residents=%u, advLevel=%u).",
                           _enabled ? 1 : 0, _target, _residents, _advLevel);

            // Feed our residents into TortoiseBots' telemetry so the observability
            // dashboard shows them (state/stuck/combat) even with adventurer AI off.
            // Runs on the world thread inside the emitter Update tick.
            sObservabilityEmitter.SetExternalRosterProvider([](std::vector<Player*>& out)
            {
                for (uint32 low : g_turtleResidents)
                    if (Player* p = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, low)))
                        if (p->IsInWorld())
                            out.push_back(p);
            });
        }

        void OnUpdate(uint32 diff) override
        {
            if (!_enabled)
                return;

            // Fire queued resident chat reactions after their small human delay.
            if (!g_pendingReactions.empty())
            {
                time_t now = time(nullptr);
                for (size_t i = 0; i < g_pendingReactions.size(); )
                {
                    if (now >= g_pendingReactions[i].at)
                    {
                        if (Player* r = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, g_pendingReactions[i].guid)))
                            if (r->IsInWorld())
                                r->Say(g_pendingReactions[i].line, LANG_UNIVERSAL);
                        g_pendingReactions[i] = g_pendingReactions.back();
                        g_pendingReactions.pop_back();
                    }
                    else ++i;
                }
            }
            if (!g_pendingWhispers.empty())
            {
                time_t now = time(nullptr);
                for (size_t i = 0; i < g_pendingWhispers.size(); )
                {
                    if (now >= g_pendingWhispers[i].at)
                    {
                        Player* b = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, g_pendingWhispers[i].bot));
                        Player* pl = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, g_pendingWhispers[i].player));
                        if (b && b->IsInWorld() && pl && pl->IsInWorld())
                            SendBotWhisper(b, pl, g_pendingWhispers[i].line);
                        g_pendingWhispers[i] = g_pendingWhispers.back();
                        g_pendingWhispers.pop_back();
                    }
                    else ++i;
                }
            }

            // Deliver queued conjure gifts: the mage conjures (visible) and hands
            // the player a level-appropriate stack of food/water.
            if (!g_pendingGifts.empty())
            {
                uint32 const now = WorldTimer::getMSTime();
                for (size_t i = 0; i < g_pendingGifts.size(); )
                {
                    PendingGift& g = g_pendingGifts[i];
                    if (int32(now - g.atMs) >= 0)
                    {
                        Player* caster = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, g.mage));
                        Player* plr    = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, g.player));
                        if (caster && caster->IsInWorld() && plr && plr->IsInWorld())
                        {
                            ServiceSay(caster, "You hand the traveler what you just conjured for them.", g.line);
                            HandOverItems(caster, plr, g.itemId, g.count); // trade window, or direct if too far
                        }
                        else if (plr && plr->IsInWorld())
                        {
                            uint32 remaining = g.count;
                            while (remaining) { uint32 const c = remaining > 20u ? 20u : remaining; plr->StoreNewItemInInventorySlot(g.itemId, c); remaining -= c; }
                        }
                        g_pendingGifts[i] = g_pendingGifts.back();
                        g_pendingGifts.pop_back();
                    }
                    else
                        ++i;
                }
            }

            // Auto-cancel trade windows a bot opened that the player never accepted,
            // so the bot doesn't stay 'busy' forever.
            // Accept a bot's trade a beat after its goods were shown.
            if (!g_botTradeAcceptAt.empty())
            {
                uint32 const now = WorldTimer::getMSTime();
                for (auto it = g_botTradeAcceptAt.begin(); it != g_botTradeAcceptAt.end(); )
                {
                    if (int32(now - it->second) >= 0)
                    {
                        Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, it->first));
                        if (bot && bot->IsInWorld() && bot->GetTradeData())
                        {
                            WorldPacket ap;
                            ap << uint32(0);
                            bot->GetSession()->HandleAcceptTradeOpcode(ap); // real accept: notifies the player and completes if they already accepted
                        }
                        it = g_botTradeAcceptAt.erase(it);
                    }
                    else ++it;
                }
            }

            // When a player opens a trade with a resident, the bot is clientless and
            // never sends CMSG_BEGIN_TRADE, so the player's window never opens. Complete
            // that handshake for any player-initiated trade we didn't open ourselves.
            for (auto sit = g_botInitiatedTrade.begin(); sit != g_botInitiatedTrade.end(); )
            {
                Player* b = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, *sit));
                if (!b || !b->GetTradeData()) sit = g_botInitiatedTrade.erase(sit); else ++sit;
            }
            for (auto sit = g_botBegunTrade.begin(); sit != g_botBegunTrade.end(); )
            {
                Player* b = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, *sit));
                if (!b || !b->GetTradeData()) sit = g_botBegunTrade.erase(sit); else ++sit;
            }
            for (auto sit = g_botBuyPricedFor.begin(); sit != g_botBuyPricedFor.end(); )
            {
                Player* b = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, sit->first));
                if (!b || !b->GetTradeData()) sit = g_botBuyPricedFor.erase(sit); // purse is real gold, keep it
                else ++sit;
            }
            // Slowly top resident purses back up to their cap (finite gold source).
            if (!g_botPurseCap.empty())
            {
                time_t const tnow = time(nullptr);
                if (tnow >= g_nextPurseRefill)
                {
                    g_nextPurseRefill = tnow + 600; // every 10 minutes
                    for (auto const& pc : g_botPurseCap)
                    {
                        Player* b = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, pc.first));
                        if (!b || b->GetTradeData() || b->GetMoney() >= pc.second) continue;
                        uint32 const target = std::min(pc.second, b->GetMoney() + 50000u); // +5g/tick
                        b->ModifyMoney(int32(target - b->GetMoney()));
                    }
                }
            }
            for (uint32 rlow : g_turtleResidents)
            {
                Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, rlow));
                if (!bot || !bot->IsInWorld())
                    continue;
                TradeData* td = bot->GetTradeData();
                if (!td)
                    continue;
                Player* trader = td->GetTrader();
                if (!trader || trader->GetTypeId() != TYPEID_PLAYER)
                    continue;
                if (g_botInitiatedTrade.count(rlow) || g_botBegunTrade.count(rlow))
                    continue; // our own give-trade, or already opened
                WorldPacket bp; // send OPEN_WINDOW to the player so they can drop in a gift
                bot->GetSession()->HandleBeginTradeOpcode(bp);
                g_botBegunTrade.insert(rlow);
            }

            // A player gifting an item to a resident: the resident accepts the
            // trade (it offers nothing itself) and thanks them in character.
            for (uint32 rlow : g_turtleResidents)
            {
                Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, rlow));
                if (!bot || !bot->IsInWorld())
                    continue;
                TradeData* td = bot->GetTradeData();
                if (!td)
                    continue;
                Player* trader = td->GetTrader();
                if (!trader || trader->GetTypeId() != TYPEID_PLAYER)
                    continue;
                if (g_botInitiatedTrade.count(rlow))
                    continue; // our own give-trade, handled elsewhere

                // Buy path: the player earlier offered (via /yell or the Trade channel,
                // with an item link) to sell this resident specific loot. The bot buys it
                // at the full vendor BuyPrice from its own finite purse (no minting, so a
                // resident can run dry), and only goods it could actually use, never vendor
                // stock. Bought items are dropped afterwards so bags don't fill.
                {
                    uint32 const plow = trader->GetGUIDLow();
                    auto si = g_sellIntent.find(plow);
                    bool const buyMode = si != g_sellIntent.end() &&
                                         si->second.resident == rlow &&
                                         int32(WorldTimer::getMSTime() - si->second.deadlineMs) < 0;
                    TradeData* his = td->GetTraderData();
                    if (buyMode && his)
                    {
                        uint64 sig = 1469598103934665603ull;
                        uint32 price = 0; bool anyItem = false, ineligible = false;
                        uint32 const wantEntry = si->second.itemEntry;
                        uint32 const unit = si->second.price;
                        for (int i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
                        {
                            Item* it = his->GetItem(TradeSlots(i));
                            if (!it) continue;
                            anyItem = true;
                            uint32 const c = it->GetCount();
                            sig = (sig ^ (uint64(it->GetEntry()) * 131 + c)) * 1099511628211ull;
                            ItemPrototype const* pr = it->GetProto();
                            if (!pr || it->GetEntry() != wantEntry || IsVendorItem(pr->ItemId) ||
                                bot->CanUseItem(pr) != EQUIP_ERR_OK || !ClassWantsItem(bot, pr))
                                ineligible = true; // not the quoted item, vendor stock, wrong class/role
                            else
                                price += unit * c;
                        }
                        if (anyItem && (ineligible || price == 0))
                        {
                            if (g_botBuyPricedFor.find(rlow) == g_botBuyPricedFor.end())
                                ServiceSay(bot, "The traveler put up goods you can't use or that any vendor sells; decline politely.",
                                           RPick({ "Sorry - I only buy gear I can use, nothing you'd rebuy from a vendor.",
                                                   "That's not for me, friend - I deal in loot I can put to use.",
                                                   "I'll pass on that - a vendor's your better bet for it." }));
                            bot->TradeCancel(true);
                            g_botBuyPricedFor.erase(rlow); g_sellIntent.erase(plow);
                            continue;
                        }
                        if (anyItem)
                        {
                            if (bot->GetMoney() < price) // purse can't cover it
                            {
                                if (g_botBuyPricedFor.find(rlow) == g_botBuyPricedFor.end())
                                    ServiceSay(bot, "The goods are worth more coin than you carry; say you can't afford it.",
                                               RPick({ "That's more than I can pay right now, friend.",
                                                       "I haven't the coin for that today - sorry.",
                                                       "Beyond my purse just now, I'm afraid." }));
                                bot->TradeCancel(true);
                                g_botBuyPricedFor.erase(rlow); g_sellIntent.erase(plow);
                                continue;
                            }
                            auto pf = g_botBuyPricedFor.find(rlow);
                            if (pf == g_botBuyPricedFor.end() || pf->second != sig)
                            {
                                td->SetMoney(price);
                                td->SetAccepted(true);
                                g_botBuyPricedFor[rlow] = sig;
                                g_botTradeDeadline[rlow] = time(nullptr) + 40;
                                ServiceSay(bot, "You offer full coin for the traveler's loot.",
                                           RPick({ "Fair coin for it - there's my offer.",
                                                   "That I can use; coin's in the window.",
                                                   "Good find - here's your gold for it." }));
                            }
                            if (his->IsAccepted())
                            {
                                std::vector<std::pair<uint32,uint32>> bought;
                                for (int i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
                                    if (Item* it = his->GetItem(TradeSlots(i)))
                                        bought.push_back(std::make_pair(it->GetEntry(), it->GetCount()));
                                WorldPacket ap; ap << uint32(0);
                                bot->GetSession()->HandleAcceptTradeOpcode(ap);
                                if (!bot->GetTradeData()) // deal done, coin already left the purse
                                {
                                    for (size_t b = 0; b < bought.size(); ++b)
                                        bot->DestroyItemCount(bought[b].first, bought[b].second, true);
                                    g_botBuyPricedFor.erase(rlow); g_sellIntent.erase(plow);
                                    ServiceSay(bot, "The deal is done; thank the traveler.",
                                               RPick({ "Pleasure doing business!",
                                                       "There's your coin - good trading!",
                                                       "A fair deal - safe travels!" }));
                                }
                            }
                            continue; // buy handled this tick
                        }
                    }
                }
                // The bot must offer nothing -- otherwise this is our own give-trade.
                bool botOffersNothing = (td->GetMoney() == 0);
                for (int i = 0; i < TRADE_SLOT_TRADED_COUNT && botOffersNothing; ++i)
                    if (td->GetItem(TradeSlots(i)))
                        botOffersNothing = false;
                if (!botOffersNothing)
                    continue;
                // The player must have offered something and accepted.
                TradeData* his = td->GetTraderData();
                if (!his || !his->IsAccepted())
                    continue;
                bool playerGave = (his->GetMoney() > 0);
                for (int i = 0; i < TRADE_SLOT_TRADED_COUNT && !playerGave; ++i)
                    if (his->GetItem(TradeSlots(i)))
                        playerGave = true;
                if (!playerGave)
                    continue;
                WorldPacket ap; // accept the gift through the real handler
                ap << uint32(0);
                bot->GetSession()->HandleAcceptTradeOpcode(ap);
                ServiceSay(bot, "A traveler just handed you a gift in a trade; thank them warmly.",
                           RPick({ "Oh - thank you kindly, friend!",
                                   "For me? That's good of you - thank you!",
                                   "Much appreciated, traveler!" }));
            }

            // Fill trade windows a short beat after they were opened.
            if (!g_pendingTradeFill.empty())
            {
                uint32 const now = WorldTimer::getMSTime();
                for (size_t i = 0; i < g_pendingTradeFill.size(); )
                {
                    PendingTradeFill& f = g_pendingTradeFill[i];
                    if (int32(now - f.atMs) >= 0)
                    {
                        Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, f.bot));
                        Player* plr = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, f.player));
                        if (bot && bot->IsInWorld() && bot->GetTradeData() && plr && plr->IsInWorld())
                            FillTradeWindow(bot, plr, f.itemId, f.count);
                        else if (plr && plr->IsInWorld())
                        {
                            uint32 rem = f.count;
                            while (rem) { uint32 const c = rem > 20u ? 20u : rem; plr->StoreNewItemInInventorySlot(f.itemId, c); rem -= c; }
                        }
                        g_pendingTradeFill[i] = g_pendingTradeFill.back();
                        g_pendingTradeFill.pop_back();
                    }
                    else ++i;
                }
            }

            if (!g_botTradeDeadline.empty())
            {
                time_t const tnow = time(nullptr);
                for (auto it = g_botTradeDeadline.begin(); it != g_botTradeDeadline.end(); )
                {
                    Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, it->first));
                    if (!bot || !bot->GetTradeData())
                        it = g_botTradeDeadline.erase(it);        // trade already done/cancelled
                    else if (tnow >= it->second)
                    {
                        bot->TradeCancel(true);
                        it = g_botTradeDeadline.erase(it);
                    }
                    else
                        ++it;
                }
            }

            // Once the invited player has joined the mage's group, open the portal.
            for (size_t i = 0; i < g_pendingPortals.size(); )
            {
                PendingPortal& pp = g_pendingPortals[i];
                Player* mage = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, pp.mage));
                Player* plr  = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, pp.player));
                bool done = false;
                if (mage && plr && mage->IsInWorld() && plr->IsInWorld() &&
                    mage->GetGroup() && mage->GetGroup() == plr->GetGroup())
                {
                    OpenPortal(mage, pp.spell, pp.city); // joined -> open it
                    done = true;
                }
                else if (mage && mage->IsInWorld() && mage->GetGroupInvite())
                {
                    WorldPacket gp; // the player invited the mage -> accept it (no client to click Yes)
                    mage->GetSession()->HandleGroupAcceptOpcode(gp);
                    // OpenPortal fires next tick, once membership is set
                }
                else if (time(nullptr) >= pp.deadline)
                {
                    if (mage && mage->IsInWorld())
                        ServiceSay(mage, "The traveler you invited never joined your group in time; you shrug it off.",
                                   "Ah well - the offer stands next time.");
                    done = true;
                }
                if (done) { g_pendingPortals[i] = g_pendingPortals.back(); g_pendingPortals.pop_back(); }
                else ++i;
            }

            // A resident that offered to buy accepts a group invite from that buyer and
            // walks over so they can trade (a global Trade-channel request must lead the
            // roaming resident to the seller). The deal itself needs no group.
            for (auto const& kv : g_sellIntent)
            {
                Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, kv.second.resident));
                if (bot && bot->IsInWorld() && bot->GetGroupInvite())
                {
                    WorldPacket gp; // no client to click Yes -> accept it ourselves
                    bot->GetSession()->HandleGroupAcceptOpcode(gp);
                    g_botEscort[kv.second.resident] = kv.first;
                }
            }
            for (auto it = g_botEscort.begin(); it != g_botEscort.end(); )
            {
                Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, it->first));
                auto si = g_sellIntent.find(it->second);
                bool const live = bot && bot->IsInWorld() && si != g_sellIntent.end() &&
                                  si->second.resident == it->first &&
                                  int32(WorldTimer::getMSTime() - si->second.deadlineMs) < 0;
                if (!live)
                {
                    if (bot && bot->IsInWorld())
                    {
                        if (bot->GetGroup()) bot->RemoveFromGroup();
                        bot->GetMotionMaster()->MoveIdle(); // stop following, resume city life
                    }
                    it = g_botEscort.erase(it);
                }
                else ++it;
            }

            // Chatter budget: refill the city-wide ambient tokens (AmbientPerMinute per minute, capped).
            g_ambientTokens = std::min(float(g_llmAmbientPerMin),
                                       g_ambientTokens + float(g_llmAmbientPerMin) * float(diff) / 60000.f);

            // Whispers that reached a resident's session (captured on the packet hook).
            ProcessWhisperInbox();

            // Deliver finished (or timed-out) in-character LLM lines on the main thread.
            {
                uint32 const nowMs = WorldTimer::getMSTime();
                for (size_t i = 0; i < g_llmJobs.size(); )
                {
                    LlmJob& j = g_llmJobs[i];
                    bool fire = false; std::string line; char const* how = "ok";
                    if (j.res->done.load())
                    {
                        if (j.res->text.empty()) { line = j.fallback; how = "empty"; }
                        else line = j.res->text;
                        fire = true;
                    }
                    else if (time(nullptr) >= j.deadline) { line = j.fallback; how = "timeout"; fire = true; }
                    if (!fire || (j.notBeforeMs && int32(nowMs - j.notBeforeMs) < 0))
                    {
                        ++i; // not ready, or holding the human beat
                        continue;
                    }
                    LlmJob const done = j;
                    g_llmJobs[i] = g_llmJobs.back();
                    g_llmJobs.pop_back();
                    DeliverLlmLine(done, line, how); // may queue a dialogue answer
                }
            }

            // Once a minute: how the residents' voice is doing.
            g_llmStatMs += diff;
            if (g_llmStatMs >= 60000)
            {
                g_llmStatMs = 0;
                if (g_llmStat.asked || g_llmStat.delivered)
                    sLog.outString("[mod-turtlebots] llm/min: asked %u, delivered %u (ok %u avg %u ms, timeout %u, empty %u, off %u), ambient budget %.1f/%u",
                                   g_llmStat.asked, g_llmStat.delivered, g_llmStat.ok,
                                   g_llmStat.ok ? uint32(g_llmStat.sumMs / g_llmStat.ok) : 0u,
                                   g_llmStat.timeout, g_llmStat.empty, g_llmStat.off, g_ambientTokens, g_llmAmbientPerMin);
                g_llmStat = LlmStat{ 0, 0, 0, 0, 0, 0, 0 };
            }

            // Slowly top warlocks' soul-shard reserves back up (one per minute).
            {
                time_t const tnow = time(nullptr);
                if (tnow >= g_nextShardRefill)
                {
                    for (auto& kv : g_warlockShards)
                        if (kv.second < 4) ++kv.second;
                    g_nextShardRefill = tnow + 60;
                }
            }

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
                // Finish any pending teleport BEFORE the in-world check: a far teleport
                // (other continent, e.g. an adventurer coming home from Elwynn) takes the
                // bot out of the world until its worldport ack - which only we can send.
                if (bot)
                    CompleteBotTeleport(bot);
                if (bot && bot->IsInWorld())
                {
                    if (RoleOf(low) == ROLE_ADVENTURER)
                        DriveAdventurer(bot);      // handles its own death/revive
                    else if (bot->IsAlive())
                        DriveResident(bot);
                    else
                        ReviveResident(bot);       // then placed/driven on the next tick
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

        void LoadFishSpots()
        {
            if (g_fishSpotsLoaded) return;
            g_fishSpotsLoaded = true;
            // The Valley of Honor pond by Lumak the fishing trainer -- inside Orgrimmar, so
            // residents never leave the city to fish (leaving town is the adventurers' job).
            g_fishSpots.push_back({ 2000.6f, -4659.7f, 26.5f, 5.31f });
            g_fishSpots.push_back({ 2006.0f, -4666.0f, 26.0f, 5.31f });
            g_fishSpots.push_back({ 1995.0f, -4665.0f, 26.0f, 5.31f });
        }

        // Make sure a fishing pole is in the main hand (grant one if needed).
        bool EnsureFishingPole(Player* bot)
        {
            Item* mh = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
            if (mh && mh->GetProto()->Class == ITEM_CLASS_WEAPON &&
                mh->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE)
                return true;
            Item* pole = bot->StoreNewItemInInventorySlot(6256, 1); // pole into a bag first
            if (!pole)
                return false;
            uint16 dest = 0;
            if (bot->CanEquipItem(EQUIPMENT_SLOT_MAINHAND, dest, pole, true) != EQUIP_ERR_OK)
                return false;
            // SwapItem is the click-equivalent: it frees the pole's bag slot as it equips it.
            // EquipItem(dest, pole) left the same Item* in BOTH the bag slot and the mainhand
            // (dangling pointer) -> a later StoreNewItem scan deref'd it in
            // Item::CanBeMergedPartlyWith -> SIGSEGV. See kith-doppelter-item-zeiger.
            bot->SwapItem(pole->GetPos(), dest);
            return true;
        }

        // Faithful fishing: walk to the water, cast Fishing, and on the bite use the bobber
        // through the real game-object path -> real catch from the zone loot table + skill-up.
        void HandleFishing(Player* bot, FishState& fs)
        {
            uint32 const low = bot->GetGUIDLow();
            uint32 const now = WorldTimer::getMSTime();
            float const dx = bot->GetPositionX() - fs.x, dy = bot->GetPositionY() - fs.y;
            float const dist2 = dx * dx + dy * dy;
            switch (fs.phase)
            {
                case FISH_MOVE:
                {
                    if (dist2 > 400.0f && now - fs.atMs <= 90000)
                    {
                        MountUp(bot); // ride there if they have a mount
                        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                            bot->GetMotionMaster()->MovePoint(0, fs.x, fs.y, fs.z, MOVE_PATHFINDING);
                        return; // heading to the water (up to 90s)
                    }
                    fs.phase = FISH_CAST; fs.atMs = now; // within ~20yd, or done walking -> fish from here
                    // fall through
                }
                case FISH_CAST:
                    bot->GetMotionMaster()->MoveIdle();
                    bot->StopMoving(true);
                    bot->Unmount(); // can't fish from the saddle
                    bot->SetFacingTo(atan2(fs.y - bot->GetPositionY(), fs.x - bot->GetPositionX())); // face the water
                    EnsureFishingPole(bot);           // best-effort: pole + real cast are just for the visual
                    bot->CastSpell(bot, 7620, false); // if a pole got equipped this places a real bobber
                    fs.phase = FISH_WAIT; fs.atMs = now;
                    return;
                case FISH_WAIT:
                {
                    if (urand(0, 999) < 4)
                        AmbientSay(bot); // now and then a word over the water
                    GameObject* bob = bot->GetGameObject(7620u);
                    if (!bob) { ObjectGuid cg = bot->GetChannelObjectGuid(); if (cg) bob = bot->GetMap()->GetGameObject(cg); }
                    if (bob && bob->getLootState() == GO_READY) // faithful catch: the bite landed
                    {
                        bob->Use(bot);                       // SendLoot(LOOT_FISHING) + skill-up
                        bot->AutoStoreLoot(bob->loot, true); // bag the fish
                        bot->SendLootRelease(bob->GetObjectGuid());
                        if (now >= fs.endMs) { g_fishing.erase(low); return; }
                        fs.phase = FISH_CAST; fs.atMs = now;
                        return;
                    }
                    if (bob && bot->IsNonMeleeSpellCasted(false))
                        return; // real bobber is out; wait for the bite
                    // No bobber placed (shore/water geometry) or the bite window passed ->
                    // pragmatic real catch: proper skill roll + a real zone fish, then cast again.
                    if (now - fs.atMs >= 15000)
                    {
                        bot->UpdateFishingSkill();
                        static uint32 const kFish[] = { 6291, 6289, 6303, 6317 };
                        bot->StoreNewItemInInventorySlot(kFish[urand(0, 3)], 1);
                        if (now >= fs.endMs) { g_fishing.erase(low); return; }
                        fs.phase = FISH_CAST; fs.atMs = now;
                    }
                    return;
                }
            }
        }

        // Cook the catch at a campfire in the city: raw fish -> cooked food + cooking skill.
        // Real items and a real skill roll; Basic Campfire is cast for the visible fire.
        void HandleCooking(Player* bot, CookState& cs)
        {
            uint32 const low = bot->GetGUIDLow();
            uint32 const now = WorldTimer::getMSTime();
            if (cs.phase == COOK_FIRE)
            {
                bot->GetMotionMaster()->MoveIdle();
                bot->StopMoving(true);
                if (!bot->FindNearestGameObject(29784, 8.0f)) // cook at a nearby fire; avoid 50 campfires
                    bot->CastSpell(bot, 818, false); // else light a Basic Campfire
                cs.phase = COOK_WAIT; cs.atMs = now;
                return;
            }
            if (now - cs.atMs < 5000)
                return; // steady cooking cadence
            int idx = -1;
            for (int i = 0; i < 4; ++i)
                if (bot->GetItemCount(kCookRaw[i], false) > 0) { idx = i; break; }
            if (idx < 0) { g_cooking.erase(low); return; } // nothing left to cook
            bot->DestroyItemCount(kCookRaw[idx], 1, true);
            bot->StoreNewItemInInventorySlot(kCookDone[idx], 1);
            bot->UpdateCraftSkill(kCookSpell[idx]); // real cooking skill-up roll
            cs.atMs = now;
        }

        // City errand: walk to a point of interest, linger a beat with a fitting gesture,
        // then let the next decision pick another. Purposeful movement, not jitter.
        void HandleErrand(Player* bot, Errand& er)
        {
            uint32 const low = bot->GetGUIDLow();
            uint32 const now = WorldTimer::getMSTime();
            Poi const& poi = kPois[er.idx % kPoiCount];
            float const dx = bot->GetPositionX() - poi.x, dy = bot->GetPositionY() - poi.y;
            float const d2 = dx * dx + dy * dy;
            if (er.phase == ERR_GO)
            {
                if (d2 > 25.0f && now - er.atMs <= 60000)
                {
                    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                        bot->GetMotionMaster()->MovePoint(0, poi.x, poi.y, poi.z, MOVE_PATHFINDING);
                    return; // walking to the POI (human pace, up to 60s)
                }
                bot->GetMotionMaster()->MoveIdle();
                bot->StopMoving(true);
                StandUp(bot);
                bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK); // browse / read on arrival
                er.phase = ERR_DWELL; er.atMs = now; er.dwellMs = urand(10000, 30000);
                if (urand(0, 99) < 40)
                    AmbientSay(bot); // a word about the place on arrival (context says "standing at ...")
                return;
            }
            if (now - er.atMs < er.dwellMs)
                return; // lingering at the POI
            g_errand.erase(low); // done; next decision picks the next errand
        }

        // Resident (city-life) behaviour: live in the city, calm ambient roaming.
        void DriveResident(Player* bot)
        {
            uint32 const low = bot->GetGUIDLow();
            g_turtleResidents.insert(low);
            PersonalityFor(low); // assign & persist a personality on first sight
            AssignProfessions(bot); // give real, level-scaled professions on first sight
            AssignMount(bot); // give a race-appropriate mount at level 40+
            if (!g_botTrained.count(low)) { TrainSkillTiers(bot); g_botTrained.insert(low); }
            // Move the resident into the city once; then they live/roam there.
            if (!_placed.count(low))
            {
                _placed.insert(low);
                bot->TeleportTo(CITY_MAP, CITY_X, CITY_Y, CITY_Z, CITY_O);
                return; // teleport finishes next tick via CompleteBotTeleport
            }

            {
                auto fit = g_fishing.find(low);
                if (fit != g_fishing.end())
                {
                    HandleFishing(bot, fit->second);
                    return; // fishing owns the tick
                }
            }
            {
                auto cit = g_cooking.find(low);
                if (cit != g_cooking.end())
                {
                    HandleCooking(bot, cit->second);
                    return; // cooking owns the tick
                }
            }

            // Don't wander off mid-cast: a portal has a long cast time and any
            // movement cancels the spell. Hold still until the cast finishes.
            if (bot->IsNonMeleeSpellCasted(false))
                return;
            if (bot->GetTradeData()) // hold still while a trade window is open...
            {
                g_botHoldUntilMs[low] = WorldTimer::getMSTime() + 2000; // ...and 2s past it
                return;
            }
            {
                auto h = g_botHoldUntilMs.find(low);
                if (h != g_botHoldUntilMs.end())
                {
                    if (int32(WorldTimer::getMSTime() - h->second) < 0)
                        return;                 // still in the post-trade hold
                    g_botHoldUntilMs.erase(h);
                }
            }

            {
                auto e = g_botEscort.find(low);
                if (e != g_botEscort.end())
                {
                    Player* buyer = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, e->second));
                    if (buyer && buyer->IsInWorld() && buyer->GetMapId() == bot->GetMapId())
                    {
                        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                            bot->GetMotionMaster()->MoveFollow(buyer, 2.0f, 0.0f);
                        return; // heading to the buyer; skip ambient roaming
                    }
                }
            }

            // Following a player (on request) or ambling alongside another resident.
            {
                auto f = g_botFollow.find(low);
                if (f != g_botFollow.end())
                {
                    Player* tgt = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, f->second.target));
                    if (tgt && tgt->IsInWorld() && tgt->GetMapId() == bot->GetMapId() &&
                        int32(WorldTimer::getMSTime() - f->second.untilMs) < 0)
                    {
                        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                            bot->GetMotionMaster()->MoveFollow(tgt, 2.0f, float(low % 6));
                        return; // walking with them; skip ambient roaming
                    }
                    bot->GetMotionMaster()->MoveIdle();
                    g_botFollow.erase(low);
                }
            }

            {
                auto er = g_errand.find(low);
                if (er != g_errand.end())
                {
                    HandleErrand(bot, er->second);
                    return; // running an errand; skip ambient roaming
                }
            }

            MotionMaster* mm = bot->GetMotionMaster();
            // Walking -> let the core carry it; only decide when idle.
            if (mm->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
                return;

            uint32 const now = WorldTimer::getMSTime();
            uint32& nextAt = _nextAt[bot->GetGUIDLow()];
            if (nextAt == 0)
                nextAt = now + urand(1000, 6000); // stagger the first decision
            ThankForBuffs(bot, now); // a real player's buff earns a thank-you, even between decisions
            if (now < nextAt)
                return; // standing calmly between actions

            // Living city: a caster resident occasionally buffs a nearby real player.
            if (CanBuff(bot->GetClass()))
            {
                uint32& buffAt = g_botBuffAt[low];
                if (now >= buffAt)
                {
                    buffAt = now + urand(20000, 45000);
                    std::list<Player*> nearby;
                    MaNGOS::AnyPlayerInObjectRangeCheck chk(bot, 20.0f);
                    MaNGOS::PlayerListSearcher<MaNGOS::AnyPlayerInObjectRangeCheck> srch(nearby, chk);
                    Cell::VisitWorldObjects(bot, srch, 20.0f);
                    uint32 const pb = ClassPrimaryBuff(bot->GetClass());
                    uint32 const pbRank = pb ? HighestKnownRankInChain(bot, pb) : 0;
                    for (Player* pl : nearby)
                    {
                        if (!pl->GetSession() || pl->GetSession()->IsHeadless() || !pl->IsAlive())
                            continue; // real, living players only
                        if (pbRank && pl->HasAura(pbRank))
                            continue; // already carries our buff
                        bot->StopMoving(true);
                        CastClassBuffs(bot, pl);
                        return; // buffed someone this beat
                    }
                }
            }

            // Cook the catch: if they hold raw fish and know cooking, sit at a campfire and cook.
            if (bot->HasSkill(185))
            {
                bool hasRaw = false;
                for (int i = 0; i < 4 && !hasRaw; ++i)
                    if (bot->GetItemCount(kCookRaw[i], false) > 0) hasRaw = true;
                if (hasRaw && urand(0, 99) < 40)
                {
                    g_cooking[low] = CookState{ uint8(COOK_FIRE), now };
                    nextAt = now + urand(20000, 40000);
                    return;
                }
            }

            // Sometimes go fish: residents with the skill wander to the water and fish for real.
            LoadFishSpots();
            if (!g_fishSpots.empty() && bot->HasSkill(356) && urand(0, 99) < 20)
            {
                FishSpot const& sp = g_fishSpots[urand(0, uint32(g_fishSpots.size()) - 1)];
                g_fishing[low] = FishState{ uint8(FISH_MOVE), sp.x, sp.y, sp.z, sp.o, now, 0, now + urand(600u, 3600u) * 1000u };
                nextAt = now + urand(30000, 60000);
                return;
            }

            // Ambient grouping: now and then, amble alongside a nearby resident a while.
            if (urand(0, 99) < 8)
            {
                Player* mate = nullptr; float best = 25.0f;
                for (uint32 low2 : g_turtleResidents)
                {
                    if (low2 == low) continue;
                    Player* r = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, low2));
                    if (!r || !r->IsInWorld() || r->GetMapId() != bot->GetMapId()) continue;
                    float const d = bot->GetDistance(r);
                    if (d < best) { best = d; mate = r; }
                }
                if (mate)
                {
                    g_botFollow[low] = FollowState{ mate->GetGUIDLow(), now + urand(20000u, 45000u) };
                    nextAt = now + urand(25000, 50000);
                    return;
                }
            }

            // Talk happens alongside whatever comes next (people chat while they walk):
            // a fair share of decisions opens with a line, within the city-wide budget.
            if (urand(0, 99) < 15)
                AmbientSay(bot);

            // Calm town-life cadence: mostly stand, sometimes stroll, rarely
            // emote or sit. A dwell timer between decisions keeps it unhurried.
            // Mostly run a purposeful errand to a POI; jitter is gone.
            if (urand(0, 99) < 70)
            {
                g_errand[low] = Errand{ uint8(urand(0, kPoiCount - 1)), uint8(ERR_GO), now, 0 };
                nextAt = now + urand(2000, 5000);
                return;
            }

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
                // Small talk: one line through the LLM with what the resident is doing
                // and who is around (canned pool as fallback), within the city-wide budget.
                StandUp(bot);
                AmbientSay(bot);
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

        // Ops lever: comma-separated zone ids kept awake even without players
        // (observation/streaming, like the Kith "Messbett"). Re-read every reconcile
        // so a config reload can flip it without a restart.
        void ReadTownForceAwake()
        {
            _townForceAwake.clear();
            std::string list = sConfig.GetStringDefault("mod-turtlebots.Town.ForceAwake", "");
            std::stringstream ss(list);
            std::string tok;
            while (std::getline(ss, tok, ','))
            {
                uint32 z = uint32(atoi(tok.c_str()));
                if (z) _townForceAwake.insert(z);
            }
        }

        // Count real players (non-headless sessions with a player in the world) per zone.
        // Real players are few, so a full session scan every 5 s is negligible and cannot
        // drift the way incrementally maintained counters would.
        void ScanRealPlayers()
        {
            _zonePlayers.clear();
            for (auto const& kv : sWorld.GetAllSessions())
            {
                WorldSession* sess = kv.second;
                if (!sess || sess->IsHeadless()) continue;
                Player* p = sess->GetPlayer();
                if (!p || !p->IsInWorld()) continue;
                ++_zonePlayers[p->GetZoneId()];
            }
        }

        // Wake a home city while a real player is in it (or it is forced awake); put it
        // to sleep once no player has been there for the grace period.
        void UpdateTownState(uint32 nowMs)
        {
            std::set<uint32> zones = _townForceAwake;
            for (uint32 i = 0; i < _residents; ++i) zones.insert(HomeZoneOf(i));
            for (uint32 z : zones)
            {
                uint32 const players = _zonePlayers.count(z) ? _zonePlayers[z] : 0;
                bool const forced = _townForceAwake.count(z) != 0;
                if (players || forced)
                {
                    _zoneLastSeenMs[z] = nowMs;
                    if (!_zoneAwake[z])
                    {
                        _zoneAwake[z] = true;
                        sLog.outString("[mod-turtlebots] town: zone %u awake (%u real player(s)%s) - waking residents.",
                                       z, players, forced ? ", forced" : "");
                    }
                }
                else if (_zoneAwake[z] && (nowMs - _zoneLastSeenMs[z]) >= _townSleepGraceSec * 1000u)
                {
                    _zoneAwake[z] = false;
                    sLog.outString("[mod-turtlebots] town: zone %u asleep (no real player for %u s) - residents log out.",
                                   z, _townSleepGraceSec);
                }
            }
            // Refill the staggered wake/sleep budget.
            _townRefillMs += RECONCILE_INTERVAL_MS;
            if (_townRefillMs >= _townWakeIntervalMs) { _townRefillMs = 0; _townBudget = _townWakeBatch; }
        }

        // The adventurer pool: every character on an account with the adventurer prefix
        // (TortoiseBots RNDBOT) whose race is Horde - Orgrimmar is our only home city until
        // #151. Refreshed from the DB every PoolRefreshSec; everything else (in the world?
        // session in flight? cooling down?) is decided in memory on each reconcile.
        void RefreshPool()
        {
            _pool.clear();
            std::string accIds;
            if (QueryResult* r = LoginDatabase.PQuery("SELECT id FROM account WHERE username LIKE '%s%%'", _poolPrefix.c_str()))
            {
                do { if (!accIds.empty()) accIds += ","; accIds += std::to_string(r->Fetch()[0].GetUInt32()); } while (r->NextRow());
                delete r;
            }
            if (accIds.empty())
                return;
            if (QueryResult* r = CharacterDatabase.PQuery("SELECT guid, account, race, name FROM characters WHERE account IN (%s)", accIds.c_str()))
            {
                do
                {
                    Field* f = r->Fetch();
                    PoolChar pc;
                    pc.low  = f[0].GetUInt32();
                    pc.acc  = f[1].GetUInt32();
                    pc.race = uint8(f[2].GetUInt32());
                    pc.name = f[3].GetCppString();
                    if (Player::TeamForRace(pc.race) == HORDE)
                        _pool.push_back(pc);
                } while (r->NextRow());
                delete r;
            }
            sLog.outString("[mod-turtlebots] town: adventurer pool refreshed - %u Horde characters on %s accounts.",
                           uint32(_pool.size()), _poolPrefix.c_str());
        }

        std::string PoolNameOf(uint32 low) const
        {
            for (PoolChar const& pc : _pool)
                if (pc.low == low) return pc.name;
            return std::to_string(low);
        }

        // Residents currently embodied (TBOT and pool alike) - the Fill cap covers both.
        uint32 ResidentsOnline() const
        {
            uint32 n = 0;
            for (uint32 low : _online)
                if (RoleOf(low) == ROLE_RESIDENT) ++n;
            return n;
        }

        // Off-shift adventurers come home: while the city is awake and below Fill, embody
        // pool characters that are not in the world, have no session in flight and are past
        // their release cooldown. TortoiseBots skips any character whose session is not
        // NotFound, so a resident is never double-logged; once we release it, the character
        // is free for its next shift again.
        void EmbodyPoolResidents(uint32 nowMs)
        {
            // Adopt freshly Active pool sessions into the roster; forget ones that vanished.
            for (auto it = _poolResidents.begin(); it != _poolResidents.end(); )
            {
                uint32 const low = *it;
                ObjectGuid guid(HIGHGUID_PLAYER, low);
                HeadlessSessionState st = sWorld.GetHeadlessSessionState(guid);
                if (st == HeadlessSessionState::Active)
                {
                    if (std::find(_online.begin(), _online.end(), low) == _online.end())
                    {
                        _online.push_back(low);
                        _role[low] = uint8(ROLE_RESIDENT);
                    }
                }
                else if (st == HeadlessSessionState::NotFound && !sObjectAccessor.FindPlayer(guid))
                {
                    _placed.erase(low);
                    _releasedAtMs[low] = nowMs;
                    it = _poolResidents.erase(it);
                    continue;
                }
                ++it;
            }

            uint32 have = ResidentsOnline();
            for (PoolChar const& pc : _pool)
            {
                if (have >= _townFill || !_townBudget)
                    break;
                if (_poolResidents.count(pc.low))
                    continue;
                ObjectGuid guid(HIGHGUID_PLAYER, pc.low);
                if (sObjectAccessor.FindPlayer(guid))
                    continue; // on shift right now
                if (sWorld.GetHeadlessSessionState(guid) != HeadlessSessionState::NotFound)
                    continue; // a login is in flight (ours or TortoiseBots)
                auto rel = _releasedAtMs.find(pc.low);
                if (rel != _releasedAtMs.end() && (nowMs - rel->second) < _releaseCooldownSec * 1000u)
                    continue; // just released: leave it to its next shift for a while
                if (sWorld.StartHeadlessSession(pc.acc, guid, LOCALE_enUS, "turtlebot") != HeadlessSessionStartResult::Started)
                    continue; // e.g. grabbed for a shift this very tick - try another
                --_townBudget;
                ++have;
                _poolResidents.insert(pc.low);
                _role[pc.low] = uint8(ROLE_RESIDENT);
                sLog.outString("[mod-turtlebots] town: %s comes home to Orgrimmar as a resident (off-shift adventurer).", pc.name.c_str());
            }
        }

        // The city fell asleep: pool residents log out (staggered) and are free for their
        // next shift. Their placement is dropped so the next homecoming teleports them to
        // the hub again from wherever they adventured in between.
        void ReleasePoolResidents(uint32 nowMs)
        {
            for (auto it = _poolResidents.begin(); it != _poolResidents.end(); )
            {
                uint32 const low = *it;
                ObjectGuid guid(HIGHGUID_PLAYER, low);
                HeadlessSessionState st = sWorld.GetHeadlessSessionState(guid);
                if (st == HeadlessSessionState::NotFound)
                {
                    _placed.erase(low);
                    _releasedAtMs[low] = nowMs;
                    it = _poolResidents.erase(it);
                    continue;
                }
                if (st == HeadlessSessionState::Active && _townBudget)
                {
                    --_townBudget;
                    sWorld.StopHeadlessSession(guid, true);
                    _placed.erase(low);
                    _releasedAtMs[low] = nowMs;
                    sLog.outString("[mod-turtlebots] town: %s goes back on shift (released).", PoolNameOf(low).c_str());
                    it = _poolResidents.erase(it);
                    continue;
                }
                ++it;
            }
        }

        // Live-tunable knobs: the panel writes the module conf and issues the console
        // command reload config; reading them on every reconcile picks a change up
        // within 5 s without a restart. Cheap: a handful of config lookups.
        void RereadLiveConfig()
        {
            _target             = sConfig.GetIntDefault("mod-turtlebots.Count", 3);
            _residents          = sConfig.GetIntDefault("mod-turtlebots.Residents", 1);
            _townEnable         = sConfig.GetBoolDefault("mod-turtlebots.Town.Enable", false);
            _townWakeBatch      = sConfig.GetIntDefault("mod-turtlebots.Town.WakeBatch", 5);
            _townWakeIntervalMs = sConfig.GetIntDefault("mod-turtlebots.Town.WakeIntervalMs", 10000);
            _townSleepGraceSec  = sConfig.GetIntDefault("mod-turtlebots.Town.SleepGraceSec", 600);
            _townFill           = sConfig.GetIntDefault("mod-turtlebots.Town.Fill", 0);
            if (!_townFill) _townFill = _residents;
            if (_target > _maxIndexSeen) _maxIndexSeen = _target;
            ReadLlmConfig(); // the voice knobs are live too
        }

        // The target was lowered live: bots with an index at or above the new target
        // log out (they stay provisioned and come back if the target rises again).
        void ShrinkToTarget()
        {
            for (uint32 i = _target; i < _maxIndexSeen; ++i)
            {
                if (!_charByIndex.count(i))
                    continue;
                ObjectGuid guid(HIGHGUID_PLAYER, _charByIndex[i]);
                if (sWorld.GetHeadlessSessionState(guid) == HeadlessSessionState::Active)
                {
                    sWorld.StopHeadlessSession(guid, true);
                    sLog.outString("[mod-turtlebots] target lowered to %u: %s logs out.", _target, BotCharName(i).c_str());
                }
            }
        }

        void Reconcile()
        {
            RereadLiveConfig();
            ShrinkToTarget();
            if (_townEnable)
            {
                ReadTownForceAwake();
                ScanRealPlayers();
                UpdateTownState(WorldTimer::getMSTime());
            }
            uint32 online = 0;
            for (uint32 i = 0; i < _target; ++i)
                if (EnsureBotOnline(i))
                    ++online;

            if (_townEnable)
            {
                if (_poolRefreshMs <= RECONCILE_INTERVAL_MS) { RefreshPool(); _poolRefreshMs = _poolRefreshSec * 1000u; }
                else _poolRefreshMs -= RECONCILE_INTERVAL_MS;
                uint32 const nowMs = WorldTimer::getMSTime();
                if (_zoneAwake[kOrgZone]) EmbodyPoolResidents(nowMs);
                else                      ReleasePoolResidents(nowMs);
            }

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
            bool const resident  = (i < _residents);
            bool const townGated = _townEnable && resident;
            bool const cityAwake = !townGated || _zoneAwake[HomeZoneOf(i)];
            if (state == HeadlessSessionState::Active)
            {
                if (townGated && !cityAwake)
                {
                    // City fell asleep: log the resident out (staggered); it becomes a shadow.
                    if (_townBudget)
                    {
                        --_townBudget;
                        sWorld.StopHeadlessSession(charGuid, true);
                        sLog.outString("[mod-turtlebots] town: %s goes to sleep (zone %u).",
                                       BotCharName(i).c_str(), HomeZoneOf(i));
                    }
                    return false;
                }
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

            if (townGated)
            {
                if (!cityAwake)   return false; // stays a shadow until a player is in the city
                if (!_townBudget) return false; // staggered wake: wait for the next budget refill
                --_townBudget;
            }
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
        uint32 _maxIndexSeen = 0;              // highest bot index provisioned so far (live shrink)
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
        // --- Town mode state (see OnStartup) ---
        bool     _townEnable = false;
        uint32   _townWakeBatch = 5;
        uint32   _townWakeIntervalMs = 10000;
        uint32   _townSleepGraceSec = 600;
        std::set<uint32>         _townForceAwake;  // zones kept awake without players (ops/observation)
        std::map<uint32, uint32> _zonePlayers;     // zone -> real (non-headless) players, rescanned each reconcile
        std::map<uint32, bool>   _zoneAwake;       // zone -> residents may be embodied
        std::map<uint32, uint32> _zoneLastSeenMs;  // zone -> last ms a real player was seen (or forced)
        uint32   _townBudget = 0;                  // wake/sleep logins allowed until the next refill
        uint32   _townRefillMs = 0;                // ms accumulated toward the next refill
        static constexpr uint32 kOrgZone = 1637;   // Orgrimmar: the only resident home city for now
        uint32 HomeZoneOf(uint32 /*botIndex*/) const { return kOrgZone; } // per-city assignment comes with #151

        // --- Off-shift adventurers as residents (see OnStartup) ---
        struct PoolChar { uint32 low = 0; uint32 acc = 0; uint8 race = 0; std::string name; };
        std::vector<PoolChar>    _pool;             // Horde adventurer characters eligible as residents
        uint32                   _poolRefreshMs = 0; // countdown to the next DB refresh (0 = now)
        std::set<uint32>         _poolResidents;    // pool characters currently embodied by us
        std::map<uint32, uint32> _releasedAtMs;     // guid low -> ms we last released it (cooldown)
        uint32      _townFill = 0;
        std::string _poolPrefix = "RNDBOT";
        uint32      _poolRefreshSec = 300;
        uint32      _releaseCooldownSec = 600;
        uint32 _advLevel  = 10;                // level given to adventurers
    };
}

// Residents react to a player speaking nearby: the closest resident answers
// with a friendly line. Uses PlayerScript's OnChatSay (fired by the core for
// any /say), so it needs no packet delivery to a headless session.
class TurtleBotsChatScript : public PlayerScript
{
public:
    TurtleBotsChatScript()
        : PlayerScript("mod-turtlebots_chat", { PLAYERHOOK_ON_CHAT_SAY, PLAYERHOOK_ON_CHAT_YELL, PLAYERHOOK_ON_CHAT_CHANNEL })
    {
    }

    void OnChatSay(Player* from, float range, char const* msg) override
    {
        if (!from || !msg || !*msg)
            return;
        if (!from->GetSession() || from->GetSession()->IsHeadless())
            return; // only a real player triggers a reaction

        std::string lower(msg);
        for (char& c : lower)
            if (c >= 'A' && c <= 'Z')
                c = char(c + 32);

        bool const wantsWater = lower.find("water") != std::string::npos ||
                                lower.find("drink") != std::string::npos ||
                                lower.find("mana")  != std::string::npos;
        bool const wantsFood  = lower.find("food")   != std::string::npos ||
                                lower.find("eat")    != std::string::npos ||
                                lower.find("hungry") != std::string::npos ||
                                lower.find("bread")  != std::string::npos;

        bool const wantsHealthstone = lower.find("healthstone") != std::string::npos ||
                                      lower.find("health stone") != std::string::npos ||
                                      (lower.find("heal") != std::string::npos && lower.find("stone") != std::string::npos);

        float const R = (range > 0.f ? range : 30.f);

        // A request for a city portal goes to the nearest mage resident, who
        // opens the real portal (reagent supplied so the cast can play).
        if (lower.find("portal") != std::string::npos || ContainsWord(lower, "port"))
        {
            uint32 reqLvl = 0; std::string city;
            uint32 const spell = PortalSpellForCity(lower, reqLvl, city);
            if (Player* mage = NearestResident(from, R, CLASS_MAGE))
            {
                g_fishing.erase(mage->GetGUIDLow()); g_cooking.erase(mage->GetGUIDLow()); // stop the hobby to serve
                if (!spell)
                {
                    ServiceSay(mage, "A traveler wants a portal but didn't say which city; ask which one, and note you "
                                     "can open portals to Orgrimmar, Undercity, Thunder Bluff, Stormwind, Ironforge or Darnassus.",
                               "Which city? Orgrimmar, Undercity, Thunder Bluff, Stormwind, Ironforge or Darnassus.");
                    return;
                }
                if (mage->GetLevel() < reqLvl)
                {
                    ServiceSay(mage, "A traveler asks for a city portal, but you are not yet skilled enough to open one.",
                               RPick({ "I can't open that portal yet - not skilled enough.",
                                       "That portal's beyond me for now - come back when I'm stronger." }));
                    return;
                }
                // Portals only work within the mage's group. A real client won't
                // stay in a bot-led group, so the PLAYER must lead: ask them to
                // invite the mage, who then accepts on its own (below).
                if (mage->GetGroup() && mage->GetGroup() == from->GetGroup())
                {
                    OpenPortal(mage, spell, city); // already grouped -> open now
                    return;
                }
                ServiceSay(mage, std::string("Ask the traveler to invite YOU into their group; then open a portal to ") + city + ".",
                           std::string("Invite me to your group and I'll open a portal to ") + city + ".");
                {
                    bool exists = false;
                    for (auto const& q : g_pendingPortals)
                        if (q.mage == mage->GetGUIDLow() && q.player == from->GetGUIDLow()) { exists = true; break; }
                    if (!exists)
                        g_pendingPortals.push_back({ mage->GetGUIDLow(), from->GetGUIDLow(), spell, city, time(nullptr) + 40 });
                }
                return;
            }
            // No mage nearby -> point them to one.
            if (Player* res = NearestResident(from, R, 0))
            {
                ServiceSay(res, "A traveler asks you for a portal, but portals are a mage's craft, not yours.",
                           RPick({ "You'll want a mage for portals, friend.",
                                   "Portals are mage work - seek one out." }));
                return;
            }
        }

        // A request for a healthstone goes to the nearest warlock resident, who
        // shapes one and hands it over after a short beat.
        if (wantsHealthstone)
        {
            if (Player* wl = NearestResident(from, R, CLASS_WARLOCK))
            {
                g_fishing.erase(wl->GetGUIDLow()); g_cooking.erase(wl->GetGUIDLow()); // stop the hobby to serve
                uint32 spell = 0, item = 0;
                HealthstoneForLevel(wl->GetLevel(), spell, item);
                if (!item) // too low a level to shape one
                {
                    ServiceSay(wl, "A traveler asks for a healthstone, but you are not yet skilled enough to make one.",
                               RPick({ "I'm not skilled enough to shape a healthstone yet.",
                                       "Ask me again when I've honed my craft - too green for that yet.",
                                       "That's beyond me for now, friend - I've more to learn." }));
                    return;
                }
                if (PlayerHasHealthstone(from)) // they are unique across ranks
                {
                    ServiceSay(wl, "A traveler asks for a healthstone, but they already carry one and they are unique.",
                               RPick({ "You already carry a healthstone, friend - one's all it takes.",
                                       "You've one on you already - it won't abide a second.",
                                       "Check your pack - you're already carrying one." }));
                    return;
                }
                uint32 const wlow = wl->GetGUIDLow();
                auto shardIt = g_warlockShards.find(wlow);
                if (shardIt == g_warlockShards.end())
                    shardIt = g_warlockShards.emplace(wlow, urand(1, 4)).first; // seed a reserve
                if (shardIt->second == 0) // genuinely out until it refills
                {
                    ServiceSay(wl, "A traveler asks for a healthstone, but you have no soul shards left right now.",
                               RPick({ "Sorry, friend - I'm fresh out of soul shards just now.",
                                       "My shard pouch is empty, I'm afraid - try another of my kind.",
                                       "No shards left on me. Give me a while to gather more.",
                                       "I've spent my last shard - none to spare right now." }));
                    return;
                }
                --shardIt->second;
                ServiceSay(wl, "A traveler asks for a healthstone; you begin shaping one from a soul shard.",
                           RPick({ "One moment - shaping a healthstone...",
                                   "Hold still - drawing one from the shard now...",
                                   "Aye, a healthstone coming up - give me a breath..." }));
                if (!wl->HasSpell(spell)) wl->LearnSpell(spell, false);
                ClearConjured(wl); // free the bag so shard/healthstone fit
                wl->StoreNewItemInInventorySlot(6265, 1); // soul shard so the cast can play
                wl->StopMoving(true); // stand still so the cast isn't cancelled
                CastByChainBase(wl, spell, wl); // visible cast during the wait
                g_pendingGifts.push_back({ wl->GetGUIDLow(), from->GetGUIDLow(),
                                           spell, item, 1u,
                                           RPick({ "Here - a healthstone for the road.",
                                                   "There you are - keep it for a tight spot.",
                                                   "Take it, friend - may it save your skin." }),
                                           WorldTimer::getMSTime() + 3000u });
                return;
            }
            // No warlock nearby: point the player to one instead of answering as
            // the wrong class (a mage saying "ask a mage" makes no sense here).
            if (Player* res = NearestResident(from, R, 0))
            {
                ServiceSay(res, "A traveler asks you for a healthstone, but that is warlock work, not yours.",
                           RPick({ "Healthstones are warlock work - you'll want to find one of them.",
                                   "That's a warlock's trade, not mine - seek one out.",
                                   "You'll need a warlock for that, friend." }));
                return;
            }
        }

        // A request for water/food goes to the nearest mage resident, who conjures
        // it and hands over a level-appropriate stack.
        if (wantsWater || wantsFood)
        {
            Player* mage = NearestResident(from, R, CLASS_MAGE);
            if (mage)
            {
                g_fishing.erase(mage->GetGUIDLow()); g_cooking.erase(mage->GetGUIDLow()); // stop the hobby to serve
                bool const water = wantsWater; // if both are asked, water first
                uint32 const item = ConjuredItemForLevel(mage->GetLevel(), from->GetLevel(), water);
                std::string line = water ? "Here's your water, friend - freshly conjured!"
                                         : "Freshly conjured - help yourself!";
                uint32 const spell = water ? 5504u : 587u;
                uint32 const stacks = ParseStacks(lower);
                uint32 const count  = stacks * 20u;
                ServiceSay(mage, water ? "A traveler asks you for water; you begin conjuring it."
                                       : "A traveler asks you for food; you begin conjuring it.",
                           water ? "Certainly - one moment, conjuring..." : "Of course - a moment...");
                if (!mage->HasSpell(spell)) mage->LearnSpell(spell, false);
                ClearConjured(mage); // free the bag so the conjure cast doesn't fail
                mage->StopMoving(true); // stand still so the cast isn't cancelled
                CastByChainBase(mage, spell, mage); // visible cast during the wait
                g_pendingGifts.push_back({ mage->GetGUIDLow(), from->GetGUIDLow(),
                                           spell, item, count, line,
                                           WorldTimer::getMSTime() + 3000u });
                return;
            }
            // no mage in earshot -> fall through to a normal reaction
        }

        // A traveler asks for a buff -> every nearby resident who can buff obliges.
        bool const wantsBuff = ContainsWord(lower, "buff") || ContainsWord(lower, "bless") ||
                               lower.find("buff me") != std::string::npos;
        if (wantsBuff)
        {
            Player* speaker = nullptr;
            for (uint32 low : g_turtleResidents)
            {
                Player* res = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, low));
                if (!res || res == from || !res->IsInWorld() || res->GetMapId() != from->GetMapId())
                    continue;
                if (from->GetDistance(res) > R || !CanBuff(res->GetClass()))
                    continue;
                res->StopMoving(true);
                CastClassBuffs(res, from);
                if (!speaker) speaker = res;
            }
            if (speaker)
                ServiceSay(speaker, "A traveler asks for a buff; oblige them warmly.",
                           RPick({ "There you go - buffed up!", "Blessings upon you, traveler!",
                                   "Stay strong out there, friend!" }));
            else if (Player* any = NearestResident(from, R, 0))
                ServiceSay(any, "A traveler asks for a buff, but no one here can grant one.",
                           RPick({ "You'll want a mage, priest or druid for that, friend.",
                                   "None of us here can buff you - ask a caster." }));
            return;
        }

        // A traveler asks a resident to come along -> nearest one follows for a while.
        if (lower.find("follow me") != std::string::npos || lower.find("come with") != std::string::npos ||
            lower.find("come along") != std::string::npos)
        {
            if (Player* res = NearestResident(from, R, 0))
            {
                g_botFollow[res->GetGUIDLow()] = FollowState{ from->GetGUIDLow(), WorldTimer::getMSTime() + 300000u };
                ServiceSay(res, "A traveler asks you to come along; agree and follow.",
                           RPick({ "Lead on, friend!", "Right behind you.", "Aye, I'll come along." }));
            }
            return;
        }
        if (lower.find("stop following") != std::string::npos || lower.find("stay here") != std::string::npos ||
            lower.find("wait here") != std::string::npos)
        {
            bool released = false;
            for (uint32 low2 : g_turtleResidents)
            {
                auto it = g_botFollow.find(low2);
                if (it == g_botFollow.end() || it->second.target != from->GetGUIDLow()) continue;
                g_botFollow.erase(it);
                if (Player* r = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, low2)))
                {
                    r->GetMotionMaster()->MoveIdle();
                    ServiceSay(r, "The traveler asks you to wait here; agree.",
                               RPick({ "I'll wait here.", "Right, staying put.", "As you say, friend." }));
                }
                released = true;
            }
            if (released) return;
        }

        // Meme completion: a passerby starts a classic line, a resident finishes it.
        static const struct { char const* trig; char const* done; } kMemes[] = {
            { "what is love",   "Baby don't hurt me, don't hurt me... no more!" },
            { "thunderfury",    "Blessed Blade of the Windseeker!" },
            { "leeroy",         "JENKINS!!!" },
            { "mankrik",        "...still can't find his wife, poor fellow." },
            { "you have died",  "Should've forded the river." },
            { "gnomes",         "...can't trust 'em. Little green-haired troublemakers." },
        };
        for (auto const& m : kMemes)
            if (lower.find(m.trig) != std::string::npos)
            {
                if (Player* res = NearestResident(from, R, 0))
                    g_pendingReactions.push_back({ res->GetGUIDLow(), std::string(m.done),
                                                   time(nullptr) + time_t(urand(1, 2)) });
                return;
            }

        // General chat -> the nearest resident answers in character, with its context
        // (place, activity, company) and a short memory of this conversation.
        if (Player* res = NearestResident(from, R, 0))
        {
            std::string usr = ConvoContext(res, from) + std::string(from->GetName()) + " says to you: " + msg;
            ConvoPush(res->GetGUIDLow(), from->GetGUIDLow(), false, msg);
            std::string fallback = RPick({ "Well met, traveler.", "Hm? What is it?",
                                           "Good to see a friendly face.", "Aye, what can I do for you?" });
            res->SetFacingTo(res->GetAngle(from));
            QueueLlm(res, ResidentSystemPrompt(res), usr, fallback, LLM_SAY, from->GetGUIDLow(),
                     WorldTimer::getMSTime() + urand(1200, 2500), "reply", 2, 0);
        }
    }

private:
    void OnChatYell(Player* from, float range, char const* msg) override
    {
        if (!from || !msg || !*msg) return;
        if (!from->GetSession() || from->GetSession()->IsHeadless()) return;
        HandleSellOffer(from, msg, range > 0.f ? range : 40.f);
    }

    void OnChatChannel(Player* from, char const* channel, char const* msg) override
    {
        if (!from || !msg || !*msg || !channel) return;
        if (!from->GetSession() || from->GetSession()->IsHeadless()) return;
        std::string ch(channel);
        for (char& c : ch) if (c >= 'A' && c <= 'Z') c = char(c + 32);
        if (ch.find("trade") == std::string::npos) return; // only the Trade channel
        HandleSellOffer(from, msg, 1000.f); // global request: nearest resident to the player
    }

    // A player offered loot for sale (item link) via yell or the Trade channel. Pick the
    // nearest resident who can use it, isn't buying vendor stock, and can afford the full
    // vendor BuyPrice; that resident quotes and remembers the offer for the coming trade.
    void HandleSellOffer(Player* from, char const* msg, float R)
    {
        uint32 const entry = ItemEntryFromLink(msg);
        if (!entry) return; // no item linked -> not a sell offer
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
        if (!proto) return;
        Player* best = nullptr; float bestDist = R;
        for (uint32 low : g_turtleResidents)
        {
            Player* res = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, low));
            if (!res || res == from || !res->IsInWorld() || res->GetMapId() != from->GetMapId())
                continue;
            float const d = from->GetDistance(res);
            if (d > bestDist) continue;
            if (res->CanUseItem(proto) != EQUIP_ERR_OK || !ClassWantsItem(res, proto)) continue; // wrong class/role
            best = res; bestDist = d;
        }
        if (!best)
        {
            if (Player* any = NearestResident(from, R, 0))
                QueueWhisper(any, from, RPick({ "That's not for my kind - find someone who'd wear it.",
                                                "No use to me, friend - ask around.",
                                                "Not my sort of gear, sorry." }));
            return;
        }
        if (IsVendorItem(entry))
        {
            QueueWhisper(best, from, RPick({ "A vendor sells those - I only buy real finds.",
                                             "That's common stock, friend - a merchant's your man.",
                                             "I don't deal in what any vendor carries." }));
            return;
        }
        uint32 const price = MarketPrice(entry, proto);
        SeedPurse(best);
        if (best->GetMoney() < price)
        {
            char const* line = best->GetLevel() < 40
                ? RPick({ "Can't afford that - still saving for my first mount, friend.",
                          "Too rich for me; I'm scraping together mount money as it is.",
                          "No chance - every copper's going toward my mount right now." })
                : RPick({ "That's beyond my purse right now, friend.",
                          "I can't cover that today - too rich for me.",
                          "More than I can pay, sorry." });
            QueueWhisper(best, from, line);
            return;
        }
        g_sellIntent[from->GetGUIDLow()] = SellIntent{ best->GetGUIDLow(), entry, WorldTimer::getMSTime() + 120000u, price };
        std::string line = RPick({ "I'll give you %s for it - bring it and trade me.",
                                   "That I can use. %s if you bring it over to trade.",
                                   "Good find - %s for it; come trade me." });
        size_t ph = line.find("%s"); if (ph != std::string::npos) line.replace(ph, 2, MoneyStr(price));
        QueueWhisper(best, from, line);
    }

    Player* NearestResident(Player* from, float R, uint8 wantClass)
    {
        Player* best = nullptr;
        float bestDist = R;
        for (uint32 low : g_turtleResidents)
        {
            Player* res = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, low));
            if (!res || res == from || !res->IsInWorld() || res->GetMapId() != from->GetMapId())
                continue;
            if (wantClass && res->GetClass() != wantClass)
                continue;
            float d = from->GetDistance(res);
            if (d <= bestDist)
            {
                best = res;
                bestDist = d;
            }
        }
        return best;
    }

    void React(Player* resident, std::string const& lower)
    {
        bool greet = lower.find("hello") != std::string::npos ||
                     lower.find("hey") != std::string::npos ||
                     lower.find("hi ") == 0 || lower == "hi" ||
                     lower.find("greet") != std::string::npos ||
                     lower.find("lok'tar") != std::string::npos;

        static char const* const kGreet[] = {
            "Well met, traveler!",
            "Lok'tar! Good to see a friendly face.",
            "Hah, greetings to you too!",
            "Zug zug. What brings you here?"
        };
        static char const* const kReply[] = {
            "Aye? What can I do for ye?",
            "Heh, is that so?",
            "Mind how you go out there.",
            "The city's busy today, friend.",
            "Need water or food? Just ask a mage."
        };

        char const* line = greet
            ? kGreet[urand(0, sizeof(kGreet) / sizeof(kGreet[0]) - 1)]
            : kReply[urand(0, sizeof(kReply) / sizeof(kReply[0]) - 1)];
        g_pendingReactions.push_back({ resident->GetGUIDLow(), std::string(line), time(nullptr) + time_t(urand(2, 3)) });
    }
};

static TurtleBotsChatScript* g_chatScript = nullptr;

// Whispers addressed to a resident arrive as SMSG_MESSAGECHAT on its headless session
// (MasterPlayer::Whisper -> WorldSession::SendPacket -> this hook). The hook may run on
// any thread, so it only parses and queues; the world thread answers (ProcessWhisperInbox).
class TurtleBotsPacketScript : public ServerScript
{
public:
    TurtleBotsPacketScript() : ServerScript("mod-turtlebots_packets", { SERVERHOOK_CAN_PACKET_SEND }) {}

    bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override
    {
        if (!session || !session->IsHeadless() || packet.GetOpcode() != SMSG_MESSAGECHAT || packet.size() < 17)
            return true;
        try
        {
            WorldPacket p(packet);
            p.rpos(0);
            uint8 type; uint32 lang;
            p >> type >> lang;
            if (type != CHAT_MSG_WHISPER || lang == uint32(LANG_ADDON))
                return true;
            ObjectGuid sender; uint32 len; std::string msg;
            p >> sender >> len >> msg;
            Player* bot = session->GetPlayer();
            if (!bot || msg.empty())
                return true;
            std::lock_guard<std::mutex> guard(g_whisperInboxMx);
            g_whisperInbox.push_back({ bot->GetGUIDLow(), sender.GetRawValue(), msg });
        }
        catch (...) {}
        return true; // never swallow the packet
    }
};

namespace
{
    // World thread: answer the whispers the packet hook collected. A service request
    // from someone standing here is handled like a /say (the mage conjures, the
    // warlock shapes a stone...); anything else gets an in-character whisper back.
    void ProcessWhisperInbox()
    {
        std::vector<InWhisper> batch;
        {
            std::lock_guard<std::mutex> guard(g_whisperInboxMx);
            batch.swap(g_whisperInbox);
        }
        for (InWhisper const& w : batch)
        {
            if (!g_turtleResidents.count(w.botLow))
                continue;
            Player* bot  = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, w.botLow));
            Player* from = sObjectAccessor.FindPlayer(ObjectGuid(w.sender));
            if (!bot || !bot->IsInWorld() || !from || !from->IsInWorld() || !from->GetSession() || from->GetSession()->IsHeadless())
                continue;
            std::string lower(w.msg);
            for (char& c : lower)
                if (c >= 'A' && c <= 'Z') c = char(c + 32);
            bool const service = HasServiceKeyword(lower);
            bool const near = from->GetMapId() == bot->GetMapId() && from->GetDistance(bot) <= 30.f;
            sLog.outString("[mod-turtlebots] whisper %s -> %s: %s", from->GetName(), bot->GetName(), w.msg.c_str());
            if (service && near && g_chatScript)
            {
                g_chatScript->OnChatSay(from, 30.f, w.msg.c_str());
                continue;
            }
            std::string usr = ConvoContext(bot, from) + from->GetName() +
                (near ? " whispers to you: " : " whispers to you from somewhere else in the world: ") + w.msg;
            if (service && !near)
                usr += " (They want a service from you but are not here beside you: tell them where to find you.)";
            ConvoPush(bot->GetGUIDLow(), from->GetGUIDLow(), false, w.msg);
            std::string fallback = RPick({ "Aye? What can I do for you?", "Hm, what is it, friend?",
                                           "I hear you. What do you need?" });
            QueueLlm(bot, ResidentSystemPrompt(bot), usr, fallback, LLM_WHISPER, from->GetGUIDLow(),
                     WorldTimer::getMSTime() + urand(1500, 3000), "whisper", 2, 0);
        }
    }
}

void Addmod_turtlebotsScripts()
{
    new TurtleBotsWorldScript();
    g_chatScript = new TurtleBotsChatScript();
    new TurtleBotsPacketScript();
}
