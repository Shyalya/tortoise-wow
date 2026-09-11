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
#include "Timer.h"
#include "Util.h"

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

    struct LlmResult { std::atomic<bool> done; std::string text; LlmResult() : done(false) {} };
    struct LlmJob { uint32 botGuid; std::string fallback; std::shared_ptr<LlmResult> res; time_t deadline; };
    static std::vector<LlmJob> g_llmJobs;

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

    static std::string LlmChat(std::string const& system, std::string const& user)
    {
        std::string body = std::string("{\"model\":\"") + g_llmModel +
            "\",\"messages\":[{\"role\":\"system\",\"content\":\"" + JsonEscape(system) +
            "\"},{\"role\":\"user\",\"content\":\"" + JsonEscape(user) +
            "\"}],\"max_tokens\":80,\"temperature\":0.8}";
        std::string line = ExtractContent(HttpPost(g_llmHost, g_llmPort, g_llmPath, body, g_llmDeadlineMs + 2000));
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

    // Fire an in-character line via the LLM on a worker thread; the fallback is
    // spoken instead if the LLM is off, at capacity, errors, or misses the deadline.
    static void QueueLlmSay(Player* bot, std::string const& system,
                            std::string const& user, std::string const& fallback)
    {
        if (!g_llmEnabled || g_llmJobs.size() >= 8)
        {
            bot->Say(fallback, LANG_UNIVERSAL);
            return;
        }
        auto res = std::make_shared<LlmResult>();
        std::string sys = system, usr = user;
        std::thread([res, sys, usr]() {
            std::string out = LlmChat(sys, usr);
            res->text = out;
            res->done.store(true);
        }).detach();
        LlmJob j;
        j.botGuid = bot->GetGUIDLow();
        j.fallback = fallback;
        j.res = res;
        j.deadline = time(nullptr) + time_t((g_llmDeadlineMs + 999) / 1000);
        g_llmJobs.push_back(j);
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

    static void HandOverItems(Player* caster, Player* plr, uint32 itemId, uint32 count)
    {
        // Open a real trade window, then fill it a beat later: the client needs
        // the window open before the item update arrives, or it renders empty.
        if (caster->GetDistance3dToCenter(plr) <= TRADE_DISTANCE &&
            !caster->GetTradeData() && !plr->GetTradeData() &&
            caster->BeginTradeWith(plr))
        {
            caster->StopMoving(true); // hold still with the window open
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
            g_llmEnabled    = sConfig.GetBoolDefault("mod-turtlebots.LLM.Enabled", true);
            g_llmUrl        = sConfig.GetStringDefault("mod-turtlebots.LLM.Url", "http://100.69.207.60:11434/v1/chat/completions");
            g_llmModel      = sConfig.GetStringDefault("mod-turtlebots.LLM.Model", "qwen2.5:32b-instruct-q4_K_M");
            g_llmDeadlineMs = sConfig.GetIntDefault("mod-turtlebots.LLM.TimeoutMs", 5000);
            ParseLlmUrl();
            sLog.outString("[mod-turtlebots] loaded (enable=%u, target=%u, residents=%u, advLevel=%u).",
                           _enabled ? 1 : 0, _target, _residents, _advLevel);
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

            // Deliver finished (or timed-out) in-character LLM lines on the main thread.
            for (size_t i = 0; i < g_llmJobs.size(); )
            {
                LlmJob& j = g_llmJobs[i];
                bool fire = false; std::string line;
                if (j.res->done.load()) { line = j.res->text.empty() ? j.fallback : j.res->text; fire = true; }
                else if (time(nullptr) >= j.deadline) { line = j.fallback; fire = true; }
                if (fire)
                {
                    if (Player* b = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, j.botGuid)))
                        if (b->IsInWorld())
                            b->Say(line, LANG_UNIVERSAL);
                    g_llmJobs[i] = g_llmJobs.back();
                    g_llmJobs.pop_back();
                }
                else ++i;
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
            g_turtleResidents.insert(low);
            PersonalityFor(low); // assign & persist a personality on first sight
            // Move the resident into the city once; then they live/roam there.
            if (!_placed.count(low))
            {
                _placed.insert(low);
                bot->TeleportTo(CITY_MAP, CITY_X, CITY_Y, CITY_Z, CITY_O);
                return; // teleport finishes next tick via CompleteBotTeleport
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

// Residents react to a player speaking nearby: the closest resident answers
// with a friendly line. Uses PlayerScript's OnChatSay (fired by the core for
// any /say), so it needs no packet delivery to a headless session.
class TurtleBotsChatScript : public PlayerScript
{
public:
    TurtleBotsChatScript()
        : PlayerScript("mod-turtlebots_chat", { PLAYERHOOK_ON_CHAT_SAY })
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

        // General chat -> the nearest resident greets or acknowledges.
        if (Player* res = NearestResident(from, R, 0))
        {
            std::string const& pers = PersonalityFor(res->GetGUIDLow());
            std::string sys = std::string("You are ") + PersonalityDesc(pers) + ", a " +
                ClassWord(res->GetClass()) + " who lives in the city in World of Warcraft. "
                "A passerby speaks to you. Reply with ONE short in-character line in plain English, "
                "under 20 words. No emotes, no asterisks, no quotation marks, no bracketed names.";
            std::string usr = std::string(from->GetName()) + " says: " + msg;
            std::string fallback = RPick({ "Well met, traveler.", "Hm? What is it?",
                                           "Good to see a friendly face.", "Aye, what can I do for you?" });
            QueueLlmSay(res, sys, usr, fallback);
        }
    }

private:
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

void Addmod_turtlebotsScripts()
{
    new TurtleBotsWorldScript();
    new TurtleBotsChatScript();
}
