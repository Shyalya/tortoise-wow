// cmangos/playerbots → Penqle/tortoise-wow compatibility shim.
//
// Provides the cmangos-side names/constants the vendored bot module references
// but Penqle either names differently or doesn't expose. Included by botpch.h
// as the first header in the PCH chain so all bot TUs see it.
//
// What's here:
//   - Type renames / typedef forwards
//   - Define mappings (cmangos constants → Penqle equivalents)
//   - Standard-library headers cmangos uses without explicit include
//
// What's NOT here (handled by per-call-site rewrites because they need
// contextual changes, not name remapping):
//   - DBC-store globals (sMapStore ↔ sMapStorage architecture)
//   - WorldPacket move-only assignment sites
//   - CreatureData::id (single field) vs Penqle's creature_id (array)
//   - PlayerbotAI internal signature mismatches
//   - GuidPosition diamond-inheritance ambiguity

#pragma once

// === Standard library headers the bot module uses without explicit includes ===
// PlayerbotLoginMgr.h, SayAction.h, and TravelValues.h use std::future
// without including <future>. Penqle's botpch.h already pulls in many std
// headers but not this one.
#include <future>
#include <chrono>
#include <random>
#include <cstdio>

// === Type renames ===
// cmangos's Transport class is called GenericTransport in WotLK builds and
// Transport in Classic. Penqle uses Transport. Provide both names.
class Transport;
typedef Transport GenericTransport;

// cmangos's CreatureAI base is named UnitAI; Penqle uses CreatureAI. Same shape.
class CreatureAI;
typedef CreatureAI UnitAI;

// cmangos uses GuidSet typedef. Penqle uses ObjectGuidSet.
// Pull ObjectGuid header transitively to ensure the typedef target is visible
// before the alias is used.
#include "ObjectGuid.h"
typedef ObjectGuidSet GuidSet;

// cmangos uses AreaTableEntry; Penqle has AreaEntry (defined in Maps/Map.h).
// They model the same data. Forward-declare and typedef.
struct AreaEntry;
typedef AreaEntry AreaTableEntry;

// cmangos uses AreaTrigger; Penqle has AreaTriggerEntry (DBCStructure.h).
struct AreaTriggerEntry;
typedef AreaTriggerEntry AreaTrigger;

// === Define mappings ===
// cmangos's ItemClass enum has ITEM_CLASS_MISC at value 15. Penqle renamed
// this to ITEM_CLASS_JUNK (also at 15). The bot module's ahbot/Category.h
// uses the cmangos name.
#ifndef ITEM_CLASS_MISC
#define ITEM_CLASS_MISC ITEM_CLASS_JUNK
#endif

// cmangos defines DEFAULT_MAX_LEVEL per-expansion (60 for Classic). The bot
// module's PlayerbotAIConfig.h and PlayerbotLoginMgr.h use this for array
// sizing. Penqle uses MAX_LEVEL/STRONG_MAX_LEVEL but not this exact name.
#ifndef DEFAULT_MAX_LEVEL
#define DEFAULT_MAX_LEVEL 60
#endif

// cmangos's Team enum has TEAM_BOTH_ALLOWED for queries that span both factions.
// Penqle's Team enum has TEAM_NONE=0 (used as "no faction filter" sentinel).
// Map TEAM_BOTH_ALLOWED to TEAM_NONE so default-arg conversions work.
#ifndef TEAM_BOTH_ALLOWED
#define TEAM_BOTH_ALLOWED TEAM_NONE
#endif

// cmangos has DIST_CALC_COMBAT_REACH for Unit::GetDistance variants.
// Penqle uses DIST_CALC_BOUNDING_RADIUS / DIST_CALC_COMBAT_REACH naming.
// Need to verify per use site; for now define as a passthrough constant.
#ifndef DIST_CALC_COMBAT_REACH
enum DistanceCalculation { DIST_CALC_NONE = 0, DIST_CALC_BOUNDING_RADIUS = 1, DIST_CALC_COMBAT_REACH = 2 };
#endif

// cmangos has UNIT_FLAG_CLIENT_CONTROL_LOST. Penqle may use a different name
// or omit it entirely. Define as 0 so the bit-flag operations parse, even if
// they're effectively no-ops at runtime
#ifndef UNIT_FLAG_CLIENT_CONTROL_LOST
#define UNIT_FLAG_CLIENT_CONTROL_LOST 0
#endif

// cmangos has movementFlagsMask as "all movement flags" sentinel. Penqle uses
// MOVEMENTFLAG_MASK_MOVING or specific flag combos. Define as all bits.
#ifndef movementFlagsMask
constexpr uint32 movementFlagsMask = 0xFFFFFFFFu;
#endif

// cmangos has BarGoLink (console progress bar). Penqle has no equivalent.
// Define as a complete stub class so the bot's BarGoLink pointer dereferences
// and method calls compile (no-op at runtime).
class BarGoLink {
public:
    BarGoLink() {}
    template<typename T> BarGoLink(T /*total*/) {}
    void step() {}
    void Step() {}
    static void SetOutputState(bool /*state*/) {}
};

// cmangos has InstanceTemplate (sObjectMgr.GetInstanceTemplate). Penqle has
// no equivalent class. Define a minimal stub with the fields the bot reads,
// all zero; bot's getInstanceTemplate() returns nullptr in WorldPosition.h
// so the fields are never read at runtime.
struct InstanceTemplate {
    uint32 levelMin = 0;
    uint32 levelMax = 0;
    uint32 maxPlayers = 0;
    uint32 reset_delay = 0;
    uint32 parent = 0;
};

// === DBC store aliases ===
// cmangos accesses spell DBC via `sSpellTemplate.LookupEntry<SpellEntry>(id)`.
// Penqle uses `sSpellMgr.GetSpellEntry(id)`. The bot's `sSpellTemplate` is used
// in 600+ call sites; rather than rewrite each, provide a header-only wrapper
// object that exposes a templated LookupEntry() forwarding to Penqle's API.
//
// The forward-decls below need ObjectMgr / SpellMgr access. Because this header
// is included EARLY in botpch.h (before SpellMgr.h), we declare the proxy class
// inline-only — its methods get instantiated at the call sites, after Penqle's
// SpellMgr/ObjectMgr are already in scope via later botpch.h includes.

// Note: this shim is included AFTER Penqle's SpellMgr.h / ObjectMgr.h /
// SpellEntry / ItemPrototype headers in botpch.h, so we can call those APIs
// directly in inline bodies.

// Singleton-like wrapper for cmangos's sSpellTemplate. Inline LookupEntry<>()
// forwards to Penqle's sSpellMgr.GetSpellEntry().
struct CmangosSpellTemplateProxy
{
    template<typename T = SpellEntry>
    T const* LookupEntry(uint32 id) const { return sSpellMgr.GetSpellEntry(id); }
    // cmangos's DBCStorage exposes GetMaxEntry. Bot uses it to iterate spells.
    // Penqle's sSpellMgr exposes GetMaxSpellId() — same purpose.
    uint32 GetMaxEntry() const { return sSpellMgr.GetMaxSpellId(); }
};
inline CmangosSpellTemplateProxy sSpellTemplate;

// Singleton-like wrapper for cmangos's sItemStorage. Forwards to sObjectMgr.GetItemPrototype().
// Iteration-upper-bound stubs (this proxy + CmangosCreatureStorageProxy,
// CmangosGOStorageProxy, CmangosFactionStoreProxy, CmangosAreaTriggerStoreProxy
// below): cmangos's stores expose GetMaxEntry/GetNumRows; Penqle's don't have
// a tight maximum. The bot module only uses these as upper bounds for
// `for (i=0; i<max; ++i) LookupEntry(i)` scans where LookupEntry returns
// nullptr for unknown ids — so a generous overestimate is safe (a few thousand
// wasted lookups during one-shot init). Bump if a future caller actually
// depends on a tight bound.
struct CmangosItemStorageProxy
{
    template<typename T = ItemPrototype>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetItemPrototype(id); }
    uint32 GetMaxEntry() const { return 100000; }
};
inline CmangosItemStorageProxy sItemStorage;

// Singleton-like wrapper for cmangos's sMapStore. Penqle uses sMapStorage (SQLStorage).
struct MapEntry;  // defined in Maps/Map.h
struct CmangosMapStoreProxy
{
    template<typename T = MapEntry>
    T const* LookupEntry(uint32 id) const { return sMapStorage.LookupEntry<MapEntry>(id); }
    uint32 GetNumRows() const { return sMapStorage.GetMaxEntry(); }
};
inline CmangosMapStoreProxy sMapStore;

// Singleton-like wrapper for cmangos's sFactionTemplateStore.
struct FactionTemplateEntry;  // defined in Database/DBCStructure.h
struct CmangosFactionTemplateStoreProxy
{
    template<typename T = FactionTemplateEntry>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetFactionTemplateEntry(id); }
    uint32 GetNumRows() const { return 1500; } // upper bound stub
};
inline CmangosFactionTemplateStoreProxy sFactionTemplateStore;

// === Other defines ===
// cmangos has ITEM_FLAG_HAS_LOOT (lootable item). Penqle uses ITEM_FLAG_HAS_LOOT or ITEM_FLAG_OPENABLE.
#ifndef ITEM_FLAG_HAS_LOOT
#define ITEM_FLAG_HAS_LOOT ITEM_FLAG_LOOTABLE
#endif

// cmangos has SPELL_ATTR_NO_IMMUNITIES; Penqle uses SPELL_ATTR_EX_NO_IMMUNITIES or similar.
#ifndef SPELL_ATTR_NO_IMMUNITIES
#define SPELL_ATTR_NO_IMMUNITIES 0
#endif

// === Type renames (cmangos→Penqle struct name diffs) ===
// cmangos's ItemPrototype has _Spell substruct (older naming);
// Penqle uses _ItemSpell (current naming). They're the same shape.
typedef _ItemSpell _Spell;

// cmangos has TEMPSPAWN_* enum values; Penqle has TEMPSUMMON_*. Map.
#ifndef TEMPSPAWN_TIMED_DESPAWN
#define TEMPSPAWN_TIMED_DESPAWN TEMPSUMMON_TIMED_DESPAWN
#endif
#ifndef TEMPSPAWN_TIMED_OR_DEAD_DESPAWN
#define TEMPSPAWN_TIMED_OR_DEAD_DESPAWN TEMPSUMMON_TIMED_OR_DEAD_DESPAWN
#endif
#ifndef TEMPSPAWN_TIMED_OR_CORPSE_DESPAWN
#define TEMPSPAWN_TIMED_OR_CORPSE_DESPAWN TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN
#endif
#ifndef TEMPSPAWN_DEAD_DESPAWN
#define TEMPSPAWN_DEAD_DESPAWN TEMPSUMMON_DEAD_DESPAWN
#endif
#ifndef TEMPSPAWN_CORPSE_DESPAWN
#define TEMPSPAWN_CORPSE_DESPAWN TEMPSUMMON_CORPSE_DESPAWN
#endif
#ifndef TEMPSPAWN_CORPSE_TIMED_DESPAWN
#define TEMPSPAWN_CORPSE_TIMED_DESPAWN TEMPSUMMON_CORPSE_TIMED_DESPAWN
#endif
#ifndef TEMPSPAWN_MANUAL_DESPAWN
#define TEMPSPAWN_MANUAL_DESPAWN TEMPSUMMON_MANUAL_DESPAWN
#endif

// === Spells namespace functions hoisted to global scope ===
// cmangos's bot calls IsPositiveSpell / GetDispellMask without namespace.
// Penqle wraps these in `namespace Spells`. Bring them into global scope
// for the bot's consumption.
using Spells::IsPositiveSpell;
using Spells::GetDispellMask;
using Spells::IsPassiveSpell;
// SpellEntry* overload: bot passes spellInfo directly.
inline bool IsPositiveSpell(SpellEntry const* spellInfo) { return spellInfo && spellInfo->IsPositiveSpell(); }
inline bool IsPositiveSpell(SpellEntry const* spellInfo, WorldObject const* caster, WorldObject const* victim) { return spellInfo && spellInfo->IsPositiveSpell(caster, victim); }

// === TRIGGERED_* spell-cast flags ===
// cmangos's CastSpell takes a TriggerCastFlags bitmask (TRIGGERED_OLD_TRIGGERED, etc.).
// Penqle uses bool triggered. Provide #defines so bot's symbolic constants compile;
// the actual values are arbitrary because Penqle's CastSpell ignores the bitmask
// (it'll pass the int as bool, which defaults to falsy for 0).
// Penqle expects `bool triggered`, not a bitmask. Use `false` for unflagged casts and
// `true` for any "triggered" cast. This is lossy (we can't represent IGNORE_GCD/IGNORE_AURA_SCALING
// distinctly) but matches Penqle's API. NOTE: must be `bool` typed so private template-trap
// overloads (in Object.h) don't catch them.
#ifndef TRIGGERED_NONE
#define TRIGGERED_NONE false
#endif
#ifndef TRIGGERED_OLD_TRIGGERED
#define TRIGGERED_OLD_TRIGGERED true
#endif
#ifndef TRIGGERED_FULL_MASK
#define TRIGGERED_FULL_MASK true
#endif
#ifndef TRIGGERED_IGNORE_GCD
#define TRIGGERED_IGNORE_GCD true
#endif
#ifndef TRIGGERED_IGNORE_AURA_SCALING
#define TRIGGERED_IGNORE_AURA_SCALING true
#endif

// === ClientLootType (cmangos has it in Loot/LootMgr.h; Penqle's LootMgr.h doesn't) ===
// Bot's LootValues.h references this enum. Stub copy from cmangos.
enum ClientLootType
{
    CLIENT_LOOT_NONE            = 0,
    CLIENT_LOOT_CORPSE          = 1,
    CLIENT_LOOT_PICKPOCKETING   = 2,
    CLIENT_LOOT_FISHING         = 3,
    CLIENT_LOOT_DISENCHANTING   = 4,
    CLIENT_LOOT_FISHINGFAIL     = 5,
    CLIENT_LOOT_INSIGNIA        = 6,
    CLIENT_LOOT_FISHINGHOLE     = 8,
};

// === GroupLootRoll / GroupLootRollMap (cmangos types not in Penqle) ===
// Bot's LootValues.h has a GroupLootRollMap field. Stub class so the field declaration parses.
class GroupLootRoll;  // opaque
typedef std::unordered_map<uint32, GroupLootRoll*> GroupLootRollMap;

// === TimePoint (cmangos using; not in Penqle) ===
// Bot uses TimePoint for loot creation timestamps.
#include <chrono>
using TimePoint = std::chrono::system_clock::time_point;

// === BG_AV_NODE_STATUS_ defines ===
// cmangos has these in BattleGroundAV.h; Penqle may use different naming.
// Define as constants so bot's symbolic references compile.
#ifndef BG_AV_NODE_STATUS_ALLY_OCCUPIED
#define BG_AV_NODE_STATUS_ALLY_OCCUPIED 0
#endif
#ifndef BG_AV_NODE_STATUS_HORDE_OCCUPIED
#define BG_AV_NODE_STATUS_HORDE_OCCUPIED 1
#endif

// === Additional cmangos-only DBC store proxies ===
// sFactionStore (faction.dbc) — distinct from sFactionTemplateStore (factiontemplate.dbc).
struct FactionEntry;  // defined in DBCStructure.h
struct CmangosFactionStoreProxy
{
    template<typename T = FactionEntry>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetFactionEntry(id); }
    uint32 GetNumRows() const { return 100; } // stub upper-bound
};
inline CmangosFactionStoreProxy sFactionStore;

// sCreatureStorage (creature_template SQL).
struct CmangosCreatureStorageProxy
{
    template<typename T = CreatureInfo>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetCreatureTemplate(id); }
    uint32 GetMaxEntry() const { return 100000; }
};
inline CmangosCreatureStorageProxy sCreatureStorage;

// === Helpers ===
// strstri overload: bot's PlayerbotAI.cpp forward-declares strstri(std::string, std::string).
// Penqle's playerbot/Helpers.cpp now provides the implementation (added).
// Re-declare here for visibility at all bot TUs.
char* strstri(std::string const& s1, std::string const& s2);

// Overload of strstr taking std::string haystack — bot calls strstr(proto->Name1, "literal")
// where Name1 is std::string. Forward to libc strstr via .c_str().
inline const char* strstr(std::string const& haystack, const char* needle) {
    return std::strstr(haystack.c_str(), needle);
}

// === BattleGroundMgr alias ===
// Done via forwarder in Penqle's BattleGroundMgr.h (BgTemplateId → BGTemplateId).

// === BG_AV_NODE_STATUS_ contested (additional) ===
#ifndef BG_AV_NODE_STATUS_ALLY_CONTESTED
#define BG_AV_NODE_STATUS_ALLY_CONTESTED 2
#endif
#ifndef BG_AV_NODE_STATUS_HORDE_CONTESTED
#define BG_AV_NODE_STATUS_HORDE_CONTESTED 3
#endif

// === TEAM_INDEX_ aliases (cmangos) ===
// Penqle uses BG_TEAM_ALLIANCE/BG_TEAM_HORDE. cmangos uses TEAM_INDEX_ALLIANCE/HORDE/NEUTRAL.
#ifndef TEAM_INDEX_ALLIANCE
#define TEAM_INDEX_ALLIANCE BG_TEAM_ALLIANCE
#endif
#ifndef TEAM_INDEX_HORDE
#define TEAM_INDEX_HORDE BG_TEAM_HORDE
#endif
#ifndef TEAM_INDEX_NEUTRAL
#define TEAM_INDEX_NEUTRAL 2
#endif

// === IsAutocastable (cmangos free function) ===
inline bool IsAutocastable(uint32 /*spellId*/) { return false; }
inline bool IsAutocastable(SpellEntry const* /*spellInfo*/) { return false; }

// === IsSpellAppliesAura / IsSpellHaveEffect / IsAreaAuraEffect (cmangos free functions) ===
inline bool IsSpellAppliesAura(SpellEntry const* spellInfo, uint32 effectMask = 0xFFFFFFFF) {
    return spellInfo && spellInfo->IsSpellAppliesAura(effectMask);
}
inline bool IsSpellHaveEffect(SpellEntry const* spellInfo, uint32 effect) {
    if (!spellInfo) return false;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i) {
        if (spellInfo->Effect[i] == effect) return true;
    }
    return false;
}
inline bool IsAreaAuraEffect(uint32 effect) {
    return effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY || effect == SPELL_EFFECT_APPLY_AREA_AURA_FRIEND
        || effect == SPELL_EFFECT_APPLY_AREA_AURA_ENEMY || effect == SPELL_EFFECT_APPLY_AREA_AURA_PET
        || effect == SPELL_EFFECT_APPLY_AREA_AURA_OWNER;
}

// === LootItem cmangos-only fields ===
// cmangos has lootItemType + LOOTITEM_TYPE_*; Penqle has only the basic LootItem.
// Add stubs to compile bot's checks; behavior degraded (everything looks "normal").
enum LootItemType {
    LOOTITEM_TYPE_NORMAL = 0,
    LOOTITEM_TYPE_QUEST = 1,
    LOOTITEM_TYPE_FFA = 2,
    LOOTITEM_TYPE_CONDITIONNAL = 3,
};

// === NAV_AREA_* / NAV_* (cmangos navmesh area types) ===
#ifndef NAV_AREA_WATER
#define NAV_AREA_WATER 7
#endif
#ifndef NAV_AREA_GROUND
#define NAV_AREA_GROUND 4
#endif
#ifndef NAV_AREA_GROUND_STEEP
#define NAV_AREA_GROUND_STEEP 3
#endif
#ifndef NAV_MAGMA_SLIME
#define NAV_MAGMA_SLIME 0x18
#endif
#ifndef NAV_GROUND_STEEP
#define NAV_GROUND_STEEP 0x02
#endif

// === CONDITION_FROM_AREATRIGGER_TELEPORT (cmangos) ===
#ifndef CONDITION_FROM_AREATRIGGER_TELEPORT
#define CONDITION_FROM_AREATRIGGER_TELEPORT 0
#endif

// === MINIMUM_LOOTING_TIME ===
#ifndef MINIMUM_LOOTING_TIME
#define MINIMUM_LOOTING_TIME 1000
#endif

// === FALL_MOTION_TYPE (cmangos motion type) → use a high stub value ===
#ifndef FALL_MOTION_TYPE
#define FALL_MOTION_TYPE 100
#endif

// === AuctionHouseType (cmangos enum) ===
enum AuctionHouseType {
    AUCTION_HOUSE_ALLIANCE = 0,
    AUCTION_HOUSE_HORDE = 1,
    AUCTION_HOUSE_NEUTRAL = 2,
    MAX_AUCTION_HOUSE_TYPE = 3,
};

// === SPELL_INTERRUPT_FLAG_COMBAT ===
#ifndef SPELL_INTERRUPT_FLAG_COMBAT
#define SPELL_INTERRUPT_FLAG_COMBAT 0x10
#endif

// === SPELL_RANGE_FLAG_MELEE / RANGED (cmangos defines on SpellRangeEntry::Flags) ===
#ifndef SPELL_RANGE_FLAG_MELEE
#define SPELL_RANGE_FLAG_MELEE 1
#endif
#ifndef SPELL_RANGE_FLAG_RANGED
#define SPELL_RANGE_FLAG_RANGED 2
#endif

// === SPELL_ATTR_USES_RANGED_SLOT (cmangos) ===
#ifndef SPELL_ATTR_USES_RANGED_SLOT
#define SPELL_ATTR_USES_RANGED_SLOT 0x00000010
#endif

// === BG_AV_NODE_STATUS_NEUTRAL_OCCUPIED ===
#ifndef BG_AV_NODE_STATUS_NEUTRAL_OCCUPIED
#define BG_AV_NODE_STATUS_NEUTRAL_OCCUPIED 4
#endif

// === CREATURE_EXTRA_FLAG_INVISIBLE ===
#ifndef CREATURE_EXTRA_FLAG_INVISIBLE
#define CREATURE_EXTRA_FLAG_INVISIBLE 0x00040000
#endif

// === TARGET_FLAG_GAMEOBJECT (cmangos) ===
#ifndef TARGET_FLAG_GAMEOBJECT
#define TARGET_FLAG_GAMEOBJECT 0x800
#endif

// === TAXI_MOTION_TYPE (cmangos) → FLIGHT_MOTION_TYPE (Penqle) ===
#ifndef TAXI_MOTION_TYPE
#define TAXI_MOTION_TYPE FLIGHT_MOTION_TYPE
#endif

// === sAreaTriggerStore (cmangos) ===
struct CmangosAreaTriggerStoreProxy
{
    template<typename T = AreaTriggerEntry>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetAreaTrigger(id); }
    uint32 GetNumRows() const { return 10000; } // upper bound stub
};
inline CmangosAreaTriggerStoreProxy sAreaTriggerStore;

// === LfgRoles / LfgRolePriority (cmangos) — bot module's own ClassRoles is similar ===
typedef ClassRoles LfgRoles;
typedef RolesPriority LfgRolePriority;

// === Taxi namespace stub (cmangos has Taxi::Map for in-flight spline tracking) ===
// Penqle has no equivalent; bot uses GetTaxiPathSpline() which we stub to return nullptr/empty.
namespace Taxi {
    struct PathNode {
        uint32 mapid = 0; float x = 0, y = 0, z = 0;
    };
    class Map {
    public:
        Map() {}
        Map(void*) {}  // accept the void* from Player::GetTaxiPathSpline stub
        bool empty() const { return true; }
        PathNode const* back() const { return nullptr; }
        PathNode const* front() const { return nullptr; }
    };
}

// === Other small defines ===
#ifndef ITEM_FLAG_UNIQUE_EQUIPPABLE
#define ITEM_FLAG_UNIQUE_EQUIPPABLE 0
#endif
#ifndef LOOT_SLOT_NORMAL
#define LOOT_SLOT_NORMAL 0
#endif
#ifndef ROLL_DISENCHANT
#define ROLL_DISENCHANT 4
#endif
#ifndef SPELL_STATE_CHANNELING
#define SPELL_STATE_CHANNELING 3
#endif
#ifndef SKILL_FLAG_CAN_UNLEARN
#define SKILL_FLAG_CAN_UNLEARN 0x10
#endif

// === sScriptDevAIMgr (cmangos has ScriptDevAI; Penqle uses sScriptMgr) ===
// Stub so symbol resolves; bot's calls are no-ops. The variadic template
// absorbs whatever cmangos's OnGossipHello signature looks like in the
// vendor tree — we don't care, we just need a callable returning false.
// Penqle's own sScriptMgr is wired separately.
struct CmangosScriptDevAIMgrStub {
    template<typename... Args>
    bool OnGossipHello(Args... /*args*/) { return false; }
};
inline CmangosScriptDevAIMgrStub sScriptDevAIMgr;

// === BG_AB GO/banner additional defines (cmangos) ===
#ifndef BG_AB_BANNER_ALLIANCE
#define BG_AB_BANNER_ALLIANCE 0
#endif
#ifndef BG_AB_BANNER_HORDE
#define BG_AB_BANNER_HORDE 1
#endif
#ifndef BG_AB_BANNER_CONTESTED_A
#define BG_AB_BANNER_CONTESTED_A 2
#endif
#ifndef BG_AB_BANNER_CONTESTED_H
#define BG_AB_BANNER_CONTESTED_H 3
#endif

// === BG WSG GO defines (cmangos) — Penqle uses BG_OBJECT_* maybe ===
#ifndef GO_WS_SILVERWING_FLAG
#define GO_WS_SILVERWING_FLAG 179830
#endif
#ifndef GO_WS_WARSONG_FLAG
#define GO_WS_WARSONG_FLAG 179831
#endif
#ifndef GO_WS_SILVERWING_FLAG_DROP
#define GO_WS_SILVERWING_FLAG_DROP 179785
#endif
#ifndef GO_WS_WARSONG_FLAG_DROP
#define GO_WS_WARSONG_FLAG_DROP 179786
#endif

// === BG WSG areatrigger defines (cmangos) ===
#ifndef WS_AT_SILVERWING_ROOM
#define WS_AT_SILVERWING_ROOM 3646
#endif
#ifndef WS_AT_WARSONG_ROOM
#define WS_AT_WARSONG_ROOM 3647
#endif

// === BG_AV node/banner defines (cmangos) ===
#ifndef BG_AV_NODE_CAPTAIN_DEAD_A
#define BG_AV_NODE_CAPTAIN_DEAD_A 0x10
#endif
#ifndef BG_AV_NODE_CAPTAIN_DEAD_H
#define BG_AV_NODE_CAPTAIN_DEAD_H 0x20
#endif
#ifndef BG_AV_GO_BANNER_ALLIANCE
#define BG_AV_GO_BANNER_ALLIANCE 178925
#endif
#ifndef BG_AV_GO_BANNER_ALLIANCE_CONT
#define BG_AV_GO_BANNER_ALLIANCE_CONT 178940
#endif
#ifndef BG_AV_GO_BANNER_HORDE
#define BG_AV_GO_BANNER_HORDE 178943
#endif
#ifndef BG_AV_GO_BANNER_HORDE_CONT
#define BG_AV_GO_BANNER_HORDE_CONT 178944
#endif
#ifndef BG_AV_GO_GY_BANNER_ALLIANCE
#define BG_AV_GO_GY_BANNER_ALLIANCE 180058
#endif
#ifndef BG_AV_GO_GY_BANNER_ALLIANCE_CONT
#define BG_AV_GO_GY_BANNER_ALLIANCE_CONT 180059
#endif
#ifndef BG_AV_GO_GY_BANNER_HORDE
#define BG_AV_GO_GY_BANNER_HORDE 180060
#endif
#ifndef BG_AV_GO_GY_BANNER_HORDE_CONT
#define BG_AV_GO_GY_BANNER_HORDE_CONT 180061
#endif
#ifndef BG_AV_GO_GY_BANNER_SNOWFALL
#define BG_AV_GO_GY_BANNER_SNOWFALL 180062
#endif

// === GetSpellCastResultString stub (cmangos free function) ===
inline char const* GetSpellCastResultString(SpellCastResult /*res*/) { return ""; }

// === TARGET_FLAG_LOCKED / SPELL_STATE_TARGETING (cmangos) ===
#ifndef TARGET_FLAG_LOCKED
#define TARGET_FLAG_LOCKED 0x100
#endif
#ifndef SPELL_STATE_TARGETING
#define SPELL_STATE_TARGETING 0
#endif

// === DIST_CALC_COMBAT_REACH_WITH_MELEE / MAX_GOSSIP_TEXT_OPTIONS ===
#ifndef DIST_CALC_COMBAT_REACH_WITH_MELEE
#define DIST_CALC_COMBAT_REACH_WITH_MELEE 3
#endif
#ifndef MAX_GOSSIP_TEXT_OPTIONS
#define MAX_GOSSIP_TEXT_OPTIONS 8
#endif

// === HasPersistentAuraEffect / CAST_FLAG_PERSISTENT_AA / FACTION_GROUP_MASK ===
inline bool HasPersistentAuraEffect(SpellEntry const* /*spellInfo*/) { return false; }
#ifndef CAST_FLAG_PERSISTENT_AA
#define CAST_FLAG_PERSISTENT_AA 0x40
#endif
#ifndef FACTION_GROUP_MASK_ALLIANCE
#define FACTION_GROUP_MASK_ALLIANCE 0x4
#endif
#ifndef FACTION_GROUP_MASK_HORDE
#define FACTION_GROUP_MASK_HORDE 0x2
#endif

// === BG_AB_BANNER_* / BG_AB_NODE_STATUS_NEUTRAL (cmangos) ===
// Penqle has these in BattleGroundAB.h but with different naming.
#ifndef BG_AB_NODE_STATUS_NEUTRAL
#define BG_AB_NODE_STATUS_NEUTRAL 0
#endif
#ifndef BG_AB_BANNER_STABLE
#define BG_AB_BANNER_STABLE 0
#endif
#ifndef BG_AB_BANNER_BLACKSMITH
#define BG_AB_BANNER_BLACKSMITH 1
#endif
#ifndef BG_AB_BANNER_FARM
#define BG_AB_BANNER_FARM 2
#endif
#ifndef BG_AB_BANNER_LUMBER_MILL
#define BG_AB_BANNER_LUMBER_MILL 3
#endif
#ifndef BG_AB_BANNER_MINE
#define BG_AB_BANNER_MINE 4
#endif

// === SEC_GAMEMASTER alias (cmangos has it; Penqle goes SEC_PLAYER → SEC_ADMINISTRATOR) ===
#ifndef SEC_GAMEMASTER
#define SEC_GAMEMASTER SEC_ADMINISTRATOR
#endif

// === FORCED_MOVEMENT_RUN / ForcedMovement (cmangos) ===
// Penqle uses different movement-flag set; bot only checks symbolic value.
typedef int ForcedMovement;
#ifndef FORCED_MOVEMENT_RUN
#define FORCED_MOVEMENT_RUN 1
#endif
#ifndef FORCED_MOVEMENT_WALK
#define FORCED_MOVEMENT_WALK 0
#endif
#ifndef FORCED_MOVEMENT_FLIGHT
#define FORCED_MOVEMENT_FLIGHT 2
#endif

// === SkillLineAbility store proxy ===
// cmangos exposes sSkillLineAbilityStore (DBCStorage<SkillLineAbilityEntry>);
// Penqle exposes sObjectMgr.GetSkillLineAbility(id).
struct SkillLineAbilityEntry;
struct CmangosSkillLineAbilityStoreProxy
{
    template<typename T = SkillLineAbilityEntry>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetSkillLineAbility(id); }
    uint32 GetMaxEntry() const { return sObjectMgr.GetMaxSkillLineAbilityId(); }
    uint32 GetNumRows() const { return GetMaxEntry(); }
};
inline CmangosSkillLineAbilityStoreProxy sSkillLineAbilityStore;

// === sAreaStore proxy (cmangos uses sAreaStore; Penqle has sAreaStorage) ===
// Same shape; keep both names.
#define sAreaStore sAreaStorage

// === sChatChannelsStore proxy ===
// cmangos bots iterate an indexed DBC store (LookupEntry(0..GetNumRows)) to join chat
// channels. Tortoise keeps the same data in ObjectMgr::m_chatChannelsMap (loaded from the
// `chat_channels` table at world init). We expose it here as a one-time, index-addressable
// snapshot. The bot reads `pattern[locale]` as an snprintf format ("General - %s"); Tortoise
// stores that string in `name[locale]`, so we point pattern at the snapshot's own name.
struct CmangosChatChannelsStoreProxy
{
    // Built once on first access (channels load at world init, before any bot logs in;
    // the map is read-only afterwards). The static vector owns its strings for the program
    // lifetime, so the pattern[] c_str() pointers below stay valid.
    static std::vector<ChatChannelsEntry> const& snapshot()
    {
        static std::vector<ChatChannelsEntry> entries = [] {
            std::vector<ChatChannelsEntry> v;
            auto const& m = sObjectMgr.GetChatChannelsMap();
            v.reserve(m.size());
            for (auto const& kv : m)
                v.push_back(kv.second);
            // Second pass: vector is fully sized now (no further reallocation), so wire each
            // entry's pattern[] to its own owned name[] strings.
            for (auto& e : v)
                for (int i = 0; i < 8; ++i)
                    e.pattern[i] = e.name[i].c_str();
            return v;
        }();
        return entries;
    }

    template<typename T = ChatChannelsEntry>
    T const* LookupEntry(uint32 id) const
    {
        auto const& v = snapshot();
        return id < v.size() ? &v[id] : nullptr;
    }
    uint32 GetNumRows() const { return (uint32)snapshot().size(); }
};
inline CmangosChatChannelsStoreProxy sChatChannelsStore;

// === sGOStorage (cmangos) → sObjectMgr.GetGameObjectInfo ===
struct CmangosGOStorageProxy
{
    template<typename T = GameObjectInfo>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetGameObjectInfo(id); }
    uint32 GetMaxEntry() const { return 200000; }
};
inline CmangosGOStorageProxy sGOStorage;

// === sTaxiNodesStore (cmangos) → sObjectMgr.GetTaxiNodeEntry ===
struct TaxiNodesEntry;
struct CmangosTaxiNodesStoreProxy
{
    template<typename T = TaxiNodesEntry>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetTaxiNodeEntry(id); }
    uint32 GetNumRows() const { return sObjectMgr.GetMaxTaxiNodeId(); }
};
inline CmangosTaxiNodesStoreProxy sTaxiNodesStore;

// === TransportAnimation stub (cmangos has TransportAnim.dbc; Penqle doesn't) ===
struct TransportAnimationNode {
    uint32 TimeIndex = 0;
    uint32 TimeSeg = 0;
    float X = 0, Y = 0, Z = 0;
};
struct TransportAnimation {
    // cmangos uses a time-keyed map of pointers (so iterators yield (uint32, TransportAnimationNode*) pairs).
    std::map<uint32, TransportAnimationNode*> Path;
    uint32 TotalTime = 0;
};
typedef std::map<uint32, TransportAnimationNode*> TransportPathContainer;
// Note: Penqle has its own sTransportMgr; we extend TransportMgr inline (see Transports/TransportMgr.h).

// === sLootMgr shim (cmangos global; Penqle has LootStore but no equivalent singleton) ===
// Bot calls sLootMgr.GetLoot(player[, guid]) to fetch the loot the player is currently looking at.
// Penqle stores the Loot object directly on the looted entity (Creature/GameObject/Item/Corpse),
// resolved by the player's current loot guid. This MUST return the real loot: StoreLootAction
// consumes it to actually take items + release. A previous nullptr stub made StoreLoot abort
// before sending CMSG_LOOT_RELEASE, so bots kneeled on an open corpse forever ("crouch loop").
// Mirrors the core resolution in Handlers/LootHandler.cpp (no distance gate: StoreLoot is reacting
// to a server loot response, so the target is already validated).
struct CmangosLootMgrStub
{
    Loot* GetLoot(Player* player, ObjectGuid guid = ObjectGuid()) const
    {
        if (!player || !player->IsInWorld())
            return nullptr;

        if (!guid)
            guid = player->GetLootGuid();
        if (!guid)
            return nullptr;

        switch (guid.GetHigh())
        {
            case HIGHGUID_GAMEOBJECT:
                if (GameObject* go = player->GetMap()->GetGameObject(guid))
                    return &go->loot;
                break;
            case HIGHGUID_CORPSE:
                if (Corpse* bones = player->GetMap()->GetCorpse(guid))
                    return &bones->loot;
                break;
            case HIGHGUID_ITEM:
                if (Item* item = player->GetItemByGuid(guid))
                    return &item->loot;
                break;
            case HIGHGUID_UNIT:
                if (Creature* creature = player->GetMap()->GetCreature(guid))
                    return &creature->loot;
                break;
            default:
                break;
        }

        return nullptr;
    }
};
inline CmangosLootMgrStub sLootMgr;

// === Map::GetHitPosition forwarder (cmangos name) ===
// Penqle uses GetLosHitPosition. The bot module's call sites were patched at
// the source level (TravelMgr.cpp / WorldPosition.h).

// === FormationSlotData / SpawnGroupFormationSlotType (cmangos formation system) ===
// Penqle has no formation system; these are stubs; flesh out if we want bot squads.
struct FormationSlotData {
    uint32 slotId = 0;
    FormationSlotData() = default;
    // bot calls make_shared<FormationSlotData>(int, ObjectGuid, nullptr, uint32).
    // Stub ctor accepts those 4 args; only slotId tracked.
    FormationSlotData(int /*idx*/, ObjectGuid const& /*guid*/, void* /*nullptr*/, uint32 slotId_)
        : slotId(slotId_) {}
};
typedef std::shared_ptr<FormationSlotData> FormationSlotDataSPtr;
namespace SpawnGroupFormationSlotType {
    constexpr uint32 SPAWN_GROUP_FORMATION_SLOT_TYPE_STATIC = 0;
    constexpr uint32 SPAWN_GROUP_FORMATION_SLOT_TYPE_SCRIPT = 1;
}
using SpawnGroupFormationSlotType::SPAWN_GROUP_FORMATION_SLOT_TYPE_STATIC;
using SpawnGroupFormationSlotType::SPAWN_GROUP_FORMATION_SLOT_TYPE_SCRIPT;

// === Free-function helpers (cmangos style) wrapping Penqle SpellEntry methods ===
// cmangos exposes these as free functions; Penqle wraps them in SpellEntry::method.
inline uint32 GetSpellCastTime(SpellEntry const* spellInfo, Spell const* spell = nullptr) {
    return spellInfo ? spellInfo->GetCastTime(nullptr, const_cast<Spell*>(spell)) : 0;
}
// 3-arg form: cmangos signature is GetSpellCastTime(SpellEntry, caster, Spell).
inline uint32 GetSpellCastTime(SpellEntry const* spellInfo, WorldObject* caster, Spell const* spell = nullptr) {
    return spellInfo ? spellInfo->GetCastTime(caster, const_cast<Spell*>(spell)) : 0;
}
// IsNextMeleeSwingSpell: cmangos free function checking SPELL_ATTR_ON_NEXT_SWING_1/_2.
inline bool IsNextMeleeSwingSpell(SpellEntry const* spellInfo) {
    return spellInfo && (spellInfo->Attributes & (SPELL_ATTR_ON_NEXT_SWING_1 | SPELL_ATTR_ON_NEXT_SWING_2));
}
inline uint32 GetSpellRecoveryTime(SpellEntry const* spellInfo) {
    return spellInfo ? spellInfo->GetRecoveryTime() : 0;
}
inline int32 GetSpellDuration(SpellEntry const* spellInfo) {
    return spellInfo ? spellInfo->GetDuration() : 0;
}
inline bool IsChanneledSpell(SpellEntry const* spellInfo) {
    return spellInfo && spellInfo->IsChanneledSpell();
}
inline SpellSchoolMask GetSpellSchoolMask(SpellEntry const* spellInfo) {
    return spellInfo ? SpellSchoolMask(spellInfo->GetSpellSchoolMask()) : SpellSchoolMask(0);
}
inline bool IsNonCombatSpell(SpellEntry const* spellInfo) {
    return spellInfo && spellInfo->IsNonCombatSpell();
}
inline bool IsPositiveEffect(SpellEntry const* spellInfo, SpellEffectIndex eff) {
    return spellInfo && spellInfo->IsPositiveEffect(eff);
}

// === GetAreaEntryByAreaID free function (cmangos) → AreaEntry::GetById (Penqle) ===
inline AreaEntry const* GetAreaEntryByAreaID(uint32 id) { return AreaEntry::GetById(id); }
inline AreaEntry const* GetAreaEntryByMapId(uint32 mapId) {
    auto* mapEntry = sMapStorage.LookupEntry<MapEntry>(mapId);
    return mapEntry ? AreaEntry::GetById(mapEntry->linkedZone) : nullptr;
}

// === MeetingStoneInfo / MeetingStoneSet (cmangos LFG) ===
// Definition lives in LFGMgr.h so both host (game.vcxproj) and bot module see the same type.
typedef std::vector<MeetingStoneInfo> MeetingStoneSet;

// === CharSections (cmangos-only DBC) ===
// Penqle has no CharSections.dbc loader, but the file exists in the data dir.
// We load it manually here so bots get real randomized appearances.
//
// CharSections.dbc vanilla 1.12 layout (10 uint32 fields, record size 40):
//   [0] ID  [1] Race  [2] Gender  [3] BaseSection  [4] VariationIndex
//   [5] ColorIndex  [6-8] texture file string offsets  [9] Flags
//
// SECTION_TYPE_* (cmangos CharSection types)
enum CharSectionType {
    SECTION_TYPE_SKIN = 0,
    SECTION_TYPE_FACE = 1,
    SECTION_TYPE_FACIAL_HAIR = 2,
    SECTION_TYPE_HAIR = 3,
    SECTION_TYPE_UNDERWEAR = 4,
};
struct CharSectionsEntry {
    uint32 Race = 0;
    uint32 Gender = 0;
    uint32 BaseSection = 0;
    uint32 VariationIndex = 0;
    uint32 ColorIndex = 0;
    // Legacy aliases used by MANGOSBOT_ZERO code paths in RandomPlayerbotFactory.cpp
    uint32 Color = 0;        // same value as ColorIndex
    uint32 Type = 0;
    uint32 Section = 0;
};
typedef std::map<uint32, CharSectionsEntry const*> CharSectionsMap;
inline CharSectionsMap sCharSectionMap;

// Loads CharSections.dbc from dataPath into sCharSectionMap.
// Safe to call multiple times; second call is a no-op.
inline void LoadCharSectionsDbc(const std::string& dataPath)
{
    if (!sCharSectionMap.empty())
        return;

    std::string path = dataPath + "/dbc/CharSections.dbc";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
    {
        sLog.outError("CharSections.dbc not found at '%s'; bot appearances will use defaults.", path.c_str());
        return;
    }

    // WDBC header: magic(4) + records(4) + fields(4) + recordSize(4) + stringBlockSize(4)
    char magic[4];
    uint32 records, fields, recordSize, stringBlockSize;
    if (fread(magic, 1, 4, f) != 4 ||
        fread(&records, 4, 1, f) != 1 ||
        fread(&fields, 4, 1, f) != 1 ||
        fread(&recordSize, 4, 1, f) != 1 ||
        fread(&stringBlockSize, 4, 1, f) != 1 ||
        magic[0] != 'W' || magic[1] != 'D' || magic[2] != 'B' || magic[3] != 'C' ||
        fields != 10 || recordSize != 40)
    {
        sLog.outError("CharSections.dbc has unexpected format (fields=%u recordSize=%u); skipping.", fields, recordSize);
        fclose(f);
        return;
    }

    for (uint32 i = 0; i < records; ++i)
    {
        uint32 row[10];
        if (fread(row, 4, 10, f) != 10)
            break;

        // row[3] == BaseSection: only skin(0), face(1), facial hair(2), hair(3) are used
        if (row[3] > SECTION_TYPE_HAIR)
            continue;

        CharSectionsEntry* e = new CharSectionsEntry();
        e->Race          = row[1];
        e->Gender        = row[2];
        e->BaseSection   = row[3];
        e->VariationIndex = row[4];
        e->ColorIndex    = row[5];
        e->Color         = row[5];  // alias
        sCharSectionMap[row[0]] = e;
    }

    fclose(f);
    sLog.outString("Loaded %zu CharSections entries for bot appearance randomization.", sCharSectionMap.size());
}

// === LFGQueue ===
// Penqle has its own LFGQueue in src/game/LFG/LFGMgr.h with stub methods added.
// World::GetLFGQueue() forwards to sLFGMgr. Bot module uses the existing types.

// === GetSpellStore (cmangos) → sSpellMgr (Penqle) ===
// cmangos exposes a global GetSpellStore() returning the DBC store as a POINTER.
inline CmangosSpellTemplateProxy* GetSpellStore() { return &sSpellTemplate; }

// === WORLD_SESSION_STATE_* hoisted into global scope ===
constexpr WorldSession::WorldSessionState WORLD_SESSION_STATE_CREATED = WorldSession::WORLD_SESSION_STATE_CREATED;
constexpr WorldSession::WorldSessionState WORLD_SESSION_STATE_READY = WorldSession::WORLD_SESSION_STATE_READY;
constexpr WorldSession::WorldSessionState WORLD_SESSION_STATE_OFFLINE = WorldSession::WORLD_SESSION_STATE_OFFLINE;
constexpr WorldSession::WorldSessionState WORLD_SESSION_STATE_REMOVING = WorldSession::WORLD_SESSION_STATE_REMOVING;

// === EmotesTextSoundEntry / FindTextSoundEmoteFor (cmangos) — DBC stubs ===
struct EmotesTextSoundEntry { uint32 SoundId = 0; };
inline EmotesTextSoundEntry const* FindTextSoundEmoteFor(uint32 /*textEmoteId*/, uint32 /*race*/, uint32 /*gender*/) { return nullptr; }

// === GetApplicationStartTime (cmangos) — free function returning startup timestamp ===
inline std::chrono::system_clock::time_point GetApplicationStartTime() {
    static auto s_start = std::chrono::system_clock::now();
    return s_start;
}

// === GetTeamIndexByTeamId (cmangos) → BattleGround static method ===
// Provide free-function forwarder. (BattleGround.h has it as a static.)
inline BattleGroundTeamIndex GetTeamIndexByTeamId(Team team) {
    return team == ALLIANCE ? BG_TEAM_ALLIANCE : BG_TEAM_HORDE;
}

// === GetRandomGenerator (cmangos) === stub: cmangos has its own thread-local PRNG;
// Penqle uses urand/frand. Bot module's TravelMgr seeds a default_random_engine via this.
// Returns pointer-style — bot does *GetRandomGenerator() in some sites.
inline std::mt19937* GetRandomGenerator() {
    thread_local std::mt19937 s_rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return &s_rng;
}

// === Loot status flags (cmangos LootMgr.h) ===
// Bot's LootValues.cpp returns bitflags describing loot state. Penqle has no equivalent
// (its Loot just exposes items/gold). Define as bitflags so bot computes a value (which
// is consumed only on the bot side via AI_VALUE comparisons; runtime semantic is harmless).
#ifndef LOOT_STATUS_FAKE_LOOT
enum LootStatusFlags : uint32 {
    LOOT_STATUS_FAKE_LOOT              = 0x01,
    LOOT_STATUS_CONTAIN_GOLD           = 0x02,
    LOOT_STATUS_NOT_FULLY_LOOTED       = 0x04,
    LOOT_STATUS_CONTAIN_FFA            = 0x08,
    LOOT_STATUS_CONTAIN_RELEASED_ITEMS = 0x10,
};
#endif

// === LootMethod sentinel (cmangos has NOT_GROUP_TYPE_LOOT for "no group loot") ===
// Penqle uses NOT_GROUP_TYPE_LOOT or FREE_FOR_ALL — but the enum values may differ.
// Stub as 0xFF so the comparison `lootMethod != NOT_GROUP_TYPE_LOOT` always hits.
#ifndef NOT_GROUP_TYPE_LOOT
constexpr uint32 NOT_GROUP_TYPE_LOOT = 0xFF;
#endif

// === SPELL_ATTR_ON_NEXT_SWING aliases ===
// cmangos has SPELL_ATTR_ON_NEXT_SWING / _NO_DAMAGE; Penqle has SPELL_ATTR_ON_NEXT_SWING_1/_2.
#ifndef SPELL_ATTR_ON_NEXT_SWING
#define SPELL_ATTR_ON_NEXT_SWING SPELL_ATTR_ON_NEXT_SWING_1
#endif
#ifndef SPELL_ATTR_ON_NEXT_SWING_NO_DAMAGE
#define SPELL_ATTR_ON_NEXT_SWING_NO_DAMAGE SPELL_ATTR_ON_NEXT_SWING_2
#endif

// === UNIT_FLAG_UNTARGETABLE / UNIT_FLAG_UNINTERACTIBLE (cmangos names) ===
// Penqle uses UNIT_FLAG_NOT_SELECTABLE for both concepts.
#ifndef UNIT_FLAG_UNTARGETABLE
#define UNIT_FLAG_UNTARGETABLE UNIT_FLAG_NOT_SELECTABLE
#endif
#ifndef UNIT_FLAG_UNINTERACTIBLE
#define UNIT_FLAG_UNINTERACTIBLE UNIT_FLAG_NOT_SELECTABLE
#endif

// === IsAutoRepeatRangedSpell (cmangos free function) ===
// Penqle's SpellEntry has IsAutoRepeatRangedSpell as a method. Wrap as free fn.
inline bool IsAutoRepeatRangedSpell(SpellEntry const* spellInfo) {
    return spellInfo && (spellInfo->AttributesEx2 & SPELL_ATTR_EX2_AUTOREPEAT_FLAG);
}
