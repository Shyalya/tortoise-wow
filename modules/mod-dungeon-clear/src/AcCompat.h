#ifndef DC_AC_COMPAT_H
#define DC_AC_COMPAT_H

/*
 * AzerothCore -> Tortoise (Penqle/mangos-zero) compatibility prelude.
 *
 * mod-dungeon-clear is written against AzerothCore 3.3.5 and liyunfan1223's
 * mod-playerbots. This server runs Penqle's 1.12 core with ike3's cmangos
 * playerbots vendored in. The two bot trees share an ancestor, so the AI-side
 * API lines up almost completely - 23 of the 26 PlayerbotAI methods the module
 * calls exist here already. The core-side API does not, and that is the gap
 * this closes.
 *
 * Force-included ahead of every translation unit (mod-dungeon-clear.cmake),
 * because the names it maps appear inside the module's own headers, not only
 * in code that could be edited file by file.
 *
 * The include order below is not free. It mirrors botpch.h, the bot module's
 * own prelude, for the reason stated there: cmangos-compat-shim.h has to come
 * AFTER the core headers, so its proxies can inline-call into them, and BEFORE
 * any bot-module header, which reach for the typedefs it declares
 * (GuidSet, AreaTableEntry, GenericTransport). Reordering these breaks the
 * build in ways that read as missing types rather than as an ordering fault.
 *
 * What is deliberately NOT here:
 *
 *   Position. This core has one, with x/y/z/o and the AzerothCore accessors,
 *   grown during the playerbots port. Aliasing a second one here does not
 *   shadow it politely - it shadows it inside the CORE headers too, and
 *   SharedDefines.h and Object.h stop compiling. The few AzerothCore methods
 *   that were missing went into the real struct instead.
 */

// --- core, first ----------------------------------------------------------
#include "Common.h"
#include "SharedDefines.h"
#include "Platform/Define.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Database/DatabaseEnv.h"
#include "Database/SQLStorages.h"
#include "Protocol/Opcodes.h"
#include "WorldPacket.h"
#include "World.h"
#include "Objects/Object.h"
#include "Objects/Unit.h"
#include "Objects/Player.h"
#include "Objects/Creature.h"
#include "Objects/GameObject.h"
#include "Objects/ItemPrototype.h"
#include "Group/Group.h"
#include "Maps/Map.h"
#include "Maps/MapManager.h"
#include "Maps/GridDefines.h"
#include "Maps/InstanceData.h"
#include "Maps/PathFinder.h"
#include "Spells/Spell.h"
#include "Spells/SpellMgr.h"
#include "Spells/SpellAuras.h"
#include "Chat/Chat.h"
#include "LootMgr.h"
#include "Movement/MotionMaster.h"
#include "ScriptMgr.h"
#include "ScriptObjects.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

// --- then the cmangos shim, then the bot framework ------------------------
#include "cmangos-compat-shim.h"

#include "playerbot/playerbot.h"
#include "playerbot/BotSlots.h"
// For MovementPriority, which the movement API declares and ported code
// names in files that never include the actions themselves.
#include "playerbot/strategy/actions/MovementActions.h"

// --- and last the AzerothCore spellings -----------------------------------
#include <cmath>
#include <cstdio>
#include <sstream>
#include <future>
#include <optional>
#include <random>
#include <string>
#include <vector>
#include <mutex>
#include <queue>
#include <condition_variable>
#include "LockedQueue.h"

// --- AzerothCore spellings for things this core already has ---------------

// AzerothCore calls the item prototype ItemTemplate.
using ItemTemplate = ItemPrototype;

// Loot roll votes. Same values, unprefixed names on AzerothCore. Declared as
// constants rather than an enum of their own so they compare against RollVote
// without a cast.
constexpr RollVote PASS           = ROLL_PASS;
constexpr RollVote NEED           = ROLL_NEED;
constexpr RollVote GREED          = ROLL_GREED;
constexpr RollVote NOT_EMITED_YET = ROLL_NOT_EMITED_YET;
constexpr RollVote NOT_VALID      = ROLL_NOT_VALID;

// --- logging --------------------------------------------------------------
//
// AzerothCore logs through fmt with a category as the first argument:
//   LOG_INFO("playerbots.dungeonclear", "bot {} pulled {}", a, b)
// This core has sLog with printf formatting and no categories. The category is
// dropped rather than prefixed onto the message, because it is the same string
// on every call in this module and would only pad the log.
//
// Note the format strings themselves are NOT translated. Upstream uses fmt
// braces; those pass through printf untouched and come out literally. Every
// call site in this module had to be converted to printf style - see the port
// commit. A brace left behind prints as a brace, it does not crash.

#define LOG_TRACE(category, ...) sLog.outDebug(__VA_ARGS__)
#define LOG_DEBUG(category, ...) sLog.outDebug(__VA_ARGS__)
#define LOG_INFO(category, ...)  sLog.outString(__VA_ARGS__)
#define LOG_WARN(category, ...)  sLog.outBasic(__VA_ARGS__)
#define LOG_ERROR(category, ...) sLog.outError(__VA_ARGS__)

// --- things AzerothCore has and this core does not ------------------------

// AzerothCore names these as free functions; here they are static members of
// WorldTimer.
inline uint32 getMSTime() { return WorldTimer::getMSTime(); }
inline uint32 getMSTimeDiff(uint32 oldMSTime, uint32 newMSTime)
{
    return WorldTimer::getMSTimeDiff(oldMSTime, newMSTime);
}

// The cmangos shim declares GuidSet; the vector form is used only by ported
// module code, so it lives here rather than there.
typedef std::vector<ObjectGuid> GuidVector;

// --- more AzerothCore names -----------------------------------------------

// Escort movement is its own generator on AzerothCore. The nearest thing here
// is the patrol generator, which is what an escort is on this core.
#ifndef ESCORT_MOTION_TYPE
#define ESCORT_MOTION_TYPE PATROL_MOTION_TYPE
#endif

// 1.12 locks open by item or by skill. AzerothCore adds a spell key type; no
// lock on this core carries one, so the value exists and never matches.
constexpr uint8 LOCK_KEY_SPELL = 3;

// Arenas arrived with The Burning Crusade. The type exists for the module's
// battleground branches, which this core never enters.
enum ArenaType : uint8
{
    ARENA_TYPE_NONE = 0
};

// AzerothCore's ProducerConsumerQueue blocks the consumer until work or
// cancellation arrives. This core's LockedQueue cannot: its next() returns
// immediately, and the path worker thread would spin a core busy-polling it.
// So this is a real queue, not an alias - mutex, condition variable, and the
// same three calls the worker uses.
template<class T>
class ProducerConsumerQueue
{
public:
    void Push(T const& value)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(value);
        }
        m_wake.notify_one();
    }

    // Blocks until there is work or Cancel() ran. Returns false only when
    // cancelled and drained, which is the worker's signal to exit.
    bool WaitAndPop(T& out)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_wake.wait(lock, [this] { return m_cancelled || !m_queue.empty(); });
        if (m_queue.empty())
            return false;
        out = m_queue.front();
        m_queue.pop();
        return true;
    }

    void Cancel()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cancelled = true;
        }
        m_wake.notify_all();
    }

private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_cancelled = false;
};

// --- line of sight --------------------------------------------------------
//
// AzerothCore parameterises its LOS check: which backends to consult
// (LINEOFSIGHT_CHECK_VMAP and friends) and which model classes to ignore
// (VMAP::ModelIgnoreFlags). This core has one check, WorldObject::IsWithinLOS,
// which always consults the vmaps and ignores nothing. The names exist so
// ported call sites compile; passing a different value changes nothing.

namespace VMAP
{
    enum class ModelIgnoreFlags : uint32
    {
        Nothing = 0,
        M2      = 1
    };
}

enum LineOfSightChecks : uint8
{
    LINEOFSIGHT_NOCHECK       = 0x0,
    LINEOFSIGHT_CHECK_VMAP    = 0x1,
    LINEOFSIGHT_CHECK_GOBJECT = 0x2,
    LINEOFSIGHT_ALL_CHECKS    = 0x3
};

// --- gameobjects ----------------------------------------------------------

// AzerothCore calls the template GameObjectTemplate.
using GameObjectTemplate = GameObjectInfo;

// And the creature one CreatureTemplate.
using CreatureTemplate = CreatureInfo;

// AzerothCore has a flag for "cannot be clicked". This core expresses the same
// state as GO_FLAG_NO_INTERACT, which is the flag scripts actually toggle when
// they want an object left alone.
#ifndef GO_FLAG_NOT_SELECTABLE
#define GO_FLAG_NOT_SELECTABLE GO_FLAG_NO_INTERACT
#endif

// --- last of the AzerothCore spellings ------------------------------------

#include "playerbot/BotState.h"

// mod-playerbots leaves the engine states unscoped; here they live in an
// enum class.
constexpr BotState BOT_STATE_COMBAT     = BotState::BOT_STATE_COMBAT;
constexpr BotState BOT_STATE_NON_COMBAT = BotState::BOT_STATE_NON_COMBAT;
constexpr BotState BOT_STATE_DEAD       = BotState::BOT_STATE_DEAD;

// AzerothCore has a one-argument form for "how long since this timestamp".
inline uint32 GetMSTimeDiffToNow(uint32 then) { return getMSTimeDiff(then, getMSTime()); }

// --- writable value access ------------------------------------------------
//
// mod-playerbots calls RefGet() straight on whatever GetValue<T>() returns.
// Here that is Value<T>*, and RefGet lives one level down on ManualSetValue -
// it cannot sit on the base, because some Value instantiations have T already
// a reference and there is no storage to hand out.
//
// Every key this module reaches for is one it registers itself, as a manually
// set value, so the cast holds. It is checked rather than asserted by comment:
// a key registered as something else would otherwise write through a bad
// pointer and corrupt whatever followed it.

template<class T>
T& DcRefGet(ai::Value<T>* value)
{
    ai::ManualSetValue<T>* settable = dynamic_cast<ai::ManualSetValue<T>*>(value);
    MANGOS_ASSERT(settable && "DcRefGet on a value that is not manually set");
    return settable->RefGet();
}

// --- config ----------------------------------------------------------------
//
// AzerothCore reads settings through sConfigMgr->GetOption<T>(name, default).
// This core reads through sConfig.Get*Default. Same file, same keys, different
// spelling - the shim keeps the module's call shape.

struct DcConfigMgrShim
{
    // AzerothCore's third argument silences the missing-key log line; this
    // reader does not log lookups, so it is accepted and dropped.
    template<class T> T GetOption(std::string const& name, T const& def, bool /*showLogs*/ = true) const;
    DcConfigMgrShim* operator->() { return this; }
};

template<> inline bool DcConfigMgrShim::GetOption<bool>(std::string const& name, bool const& def, bool /*showLogs*/) const
{ return sConfig.GetBoolDefault(name.c_str(), def); }
template<> inline int32 DcConfigMgrShim::GetOption<int32>(std::string const& name, int32 const& def, bool /*showLogs*/) const
{ return sConfig.GetIntDefault(name.c_str(), def); }
template<> inline uint32 DcConfigMgrShim::GetOption<uint32>(std::string const& name, uint32 const& def, bool /*showLogs*/) const
{ return uint32(sConfig.GetIntDefault(name.c_str(), int32(def))); }
template<> inline float DcConfigMgrShim::GetOption<float>(std::string const& name, float const& def, bool /*showLogs*/) const
{ return sConfig.GetFloatDefault(name.c_str(), def); }
template<> inline std::string DcConfigMgrShim::GetOption<std::string>(std::string const& name, std::string const& def, bool /*showLogs*/) const
{ return sConfig.GetStringDefault(name, def); }

static DcConfigMgrShim sConfigMgrShimInstance;
#ifndef sConfigMgr
#define sConfigMgr (&sConfigMgrShimInstance)
#endif

// --- unit states -----------------------------------------------------------
//
// The first four are renames. The last three name states this core does not
// track as unit-state bits: evade lives on the creature AI, casting is asked
// through IsNonMeleeSpellCast, and there is no combat-movement toggle. They
// are zero, so a HasUnitState test on them is simply false - each call site
// then treats the unit as not-evading / not-casting, which errs toward acting
// (a wasted pull on an evading mob resets it and costs a walk, nothing more).
#define UNIT_STATE_ROOT     UNIT_STAT_ROOT
#define UNIT_STATE_STUNNED  UNIT_STAT_STUNNED
#define UNIT_STATE_CONFUSED UNIT_STAT_CONFUSED
#define UNIT_STATE_FLEEING  UNIT_STAT_FLEEING
#define UNIT_STATE_EVADE               0u
#define UNIT_STATE_CASTING             0u
#define UNIT_STATE_NO_COMBAT_MOVEMENT  0u

// --- string format ---------------------------------------------------------
//
// Acore::StringFormat, printf-flavoured. Lives here rather than only in the
// compat StringFormat.h because ported code calls it from files that never
// include that header.
namespace Acore
{
    // The grid checks carry over by name: qualified lookup only follows this
    // using-directive when Acore itself has no declaration, so the adapters
    // below shadow nothing.
    using namespace MaNGOS;

    // AzerothCore searchers take the search center as their first argument;
    // this tree's take list and check only - the center comes in through the
    // visit call. Accepted and dropped, so the ported call shape stands.
    template<class Check>
    struct CreatureListSearcher : MaNGOS::CreatureListSearcher<Check>
    {
        CreatureListSearcher(WorldObject const* /*center*/, std::list<Creature*>& list, Check& check)
            : MaNGOS::CreatureListSearcher<Check>(list, check) {}
    };

    template<class Check>
    struct UnitListSearcher : MaNGOS::UnitListSearcher<Check>
    {
        UnitListSearcher(WorldObject const* /*center*/, std::list<Unit*>& list, Check& check)
            : MaNGOS::UnitListSearcher<Check>(list, check) {}
    };

    template<class Check>
    struct GameObjectListSearcher : MaNGOS::GameObjectListSearcher<Check>
    {
        GameObjectListSearcher(WorldObject const* /*center*/, std::list<GameObject*>& list, Check& check)
            : MaNGOS::GameObjectListSearcher<Check>(list, check) {}
    };

    // AzerothCore's world-object searcher takes a type mask as its fourth
    // argument. This tree's searcher visits what the grid hands it and the
    // CHECK does the filtering - every ported check already tests the type,
    // so the mask is accepted and dropped.
    template<class Check>
    struct WorldObjectListSearcher : MaNGOS::WorldObjectListSearcher<Check>
    {
        WorldObjectListSearcher(WorldObject const* /*center*/, std::list<WorldObject*>& list, Check& check, uint32 /*typeMask*/ = 0)
            : MaNGOS::WorldObjectListSearcher<Check>(list, check) {}
    };

    // Real {} substitution, not printf. The module's call sites use fmt-style
    // braces throughout ("DungeonClear.{} = {}"); routing those through printf
    // would print the braces literally and drop every argument. Each argument
    // is rendered through an ostringstream, so strings need no .c_str() and
    // numbers need no format letter - which is exactly what makes the 27 call
    // sites compile unchanged. Positional/format-spec braces ({0}, {:.1f}) are
    // NOT understood; none of the call sites use them.
    inline void DcFormatStep(std::string& out, char const*& p) { out += p; p += std::string(p).size(); }

    template<class A, class... Rest>
    inline void DcFormatStep(std::string& out, char const*& p, A const& a, Rest const&... rest)
    {
        while (*p)
        {
            if (p[0] == '{' && p[1] == '}')
            {
                std::ostringstream os;
                os << a;
                out += os.str();
                p += 2;
                DcFormatStep(out, p, rest...);
                return;
            }
            out += *p++;
        }
    }

    template<class... Args>
    inline std::string StringFormat(char const* fmt, Args const&... args)
    {
        std::string out;
        char const* p = fmt;
        DcFormatStep(out, p, args...);
        return out;
    }

    template<class... Args>
    inline std::string StringFormat(std::string const& fmt, Args const&... args)
    {
        return StringFormat(fmt.c_str(), args...);
    }
}


// 1.12 chat has no leader subtypes; the addon filter lists them for the
// later-client case and the plain type beside them already matches here.
#ifndef CHAT_MSG_PARTY_LEADER
#define CHAT_MSG_PARTY_LEADER CHAT_MSG_PARTY
#endif

#endif

// ---- AzerothCore constants that never got names on this engine -------------
static constexpr uint32 WORLD_TRIGGER = 12999;    // "World Trigger (tiny)" utility npc
static constexpr uint32 MAX_PASS_STR = 16;        // AC's account password cap
static constexpr uint32 FACTION_FRIENDLY = 35;    // faction template friendly to everyone

// ---- AzerothCore CharacterCache -> vmangos PlayerCacheData ------------------
// Name/level/race/class come from sObjectMgr's player cache. Guild membership
// is NOT cached on this engine, so those lookups pay a DB round-trip
// (Player::GetGuildIdFromDB) - every caller sits on the .dc test control path,
// never on a per-tick path.
struct CharacterCacheEntry
{
    uint8 Class = 0;
};

class DcCharacterCacheShim
{
public:
    static DcCharacterCacheShim* instance()
    {
        static DcCharacterCacheShim shim;
        return &shim;
    }

    ObjectGuid GetCharacterGuidByName(std::string const& name) const
    {
        return sObjectMgr.GetPlayerGuidByName(name);
    }

    uint32 GetCharacterGuildIdByGuid(ObjectGuid guid) const
    {
        return Player::GetGuildIdFromDB(guid); // DB hit - control path only
    }

    uint32 GetCharacterLevelByGuid(ObjectGuid guid) const
    {
        PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(guid.GetCounter());
        return data ? data->uiLevel : 0;
    }

    // Value space is Team (ALLIANCE/HORDE), not TeamId. The only caller
    // compares roster members against each other, never against constants,
    // so any stable space works - but don't mix this with GetTeamId().
    uint32 GetCharacterTeamByGuid(ObjectGuid guid) const
    {
        PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(guid.GetCounter());
        return data ? uint32(Player::TeamForRace(uint8(data->uiRace))) : 0;
    }

    CharacterCacheEntry const* GetCharacterCacheByGuid(ObjectGuid guid) const
    {
        static thread_local CharacterCacheEntry entry;
        if (PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(guid.GetCounter()))
        {
            entry.Class = uint8(data->uiClass);
            return &entry;
        }
        return nullptr;
    }

    // vmangos rebuilds cache rows from the DB; the one caller (DcTestDriver)
    // saves the fresh character right before this, so a targeted row reload is
    // the real equivalent, not a stub.
    void AddCharacterCacheEntry(ObjectGuid guid, uint32 /*accountId*/, std::string const& /*name*/,
                                uint8 /*gender*/, uint8 /*race*/, uint8 /*klass*/, uint8 /*level*/)
    {
        sObjectMgr.LoadPlayerCacheData(guid.GetCounter());
    }
};

#define sCharacterCache DcCharacterCacheShim::instance()
