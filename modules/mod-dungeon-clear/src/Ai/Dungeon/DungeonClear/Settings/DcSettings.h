/*
 * mod-dungeon-clear — DcSettings.h
 *
 * Accessor for DungeonClear tunables. Resolution order on every read:
 *
 *     per-run override
 *       ->  conf "DungeonClear.<Key>.Heroic"   |  heroic runs only
 *       ->  registry heroicVal                 |
 *       ->  conf "DungeonClear.<Key>"
 *       ->  registry default
 *
 * The two heroic steps apply only while the reading bot's map is a dungeon at
 * DUNGEON_DIFFICULTY_HEROIC, and only for rows that opt in (a "<Key>.Heroic"
 * conf line, or a non-sentinel `heroicVal` in the registry). A row that opts out
 * — nearly all of them — resolves through the identical code path it did before
 * the layer existed, on BOTH difficulties. Heroic trash is a different game from
 * normal trash (every mob elite, packs that end a run), and the pull safety
 * profile has to change with it; everything else stays one value.
 *
 * The override deliberately sits ABOVE the heroic layer and is itself
 * difficulty-blind: a human who typed a value for this run means it for this
 * run, and a default — however well chosen for heroic — must never overrule an
 * explicit one.
 *
 * The per-run override layer is keyed by the run's leader-tank GUID
 * (DcLeaderSignal::FindLeaderTank), so each dungeon run can carry its own
 * settings pushed from the companion addon while the conf file stays the
 * server-wide default. The override layer is uncached, so an override applied
 * mid-run takes effect on the next tick with no reload.
 *
 * Two things protect the worldserver console / CPU from these per-tick reads:
 *
 *   1. Every conf read passes showLogs=false (see ConfValueUncached), so the
 *      core never emits its "Config: Missing property ..." warning. Every DC
 *      tunable is optional — the registry holds the authoritative default, so a
 *      missing conf line is normal operation, not an error worth logging once
 *      per tick per bot. This is the definitive spam fix.
 *
 *   2. The conf layer (the fallback when a key has no override) is cached in one
 *      process-wide table — populated ONCE (at startup / on `.reload config`),
 *      not per map-update thread — so a read avoids the config-map lookup /
 *      getenv() / string churn GetOption does on every call, and warmup costs
 *      ~one resolution per key total rather than that many times the (high)
 *      MapUpdate.Threads pool size. Re-populated on `.reload config` via
 *      InvalidateConfCache below, so edited values still take effect live.
 *
 * Only player-facing registry rows consult the override store; server-only rows
 * (e.g. PathCenter*) go straight to conf, which keeps them safe to read from the
 * map-update worker threads that run corridor centering.
 *
 * See DcSettingsRegistry.h for the setting table.
 */

#ifndef _DUNGEON_CLEAR_DC_SETTINGS_H
#define _DUNGEON_CLEAR_DC_SETTINGS_H

#include <string>

#include "ObjectGuid.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h"

class Player;

namespace DcSettings
{
    // Effective value for a run, keyed by the leader-tank GUID. Pass
    // ObjectGuid::Empty (or a server-only key) to read the plain conf/default.
    bool   GetBool (ObjectGuid runOwner, char const* key);
    int32  GetInt  (ObjectGuid runOwner, char const* key);
    uint32 GetUInt (ObjectGuid runOwner, char const* key);
    float  GetFloat(ObjectGuid runOwner, char const* key);

    // Convenience overloads that resolve the run owner from any bot in the run
    // (FindLeaderTank). For server-only keys the owner lookup is skipped, so
    // these are cheap to call from non-player contexts too.
    bool   GetBool (Player* bot, char const* key);
    int32  GetInt  (Player* bot, char const* key);
    uint32 GetUInt (Player* bot, char const* key);
    float  GetFloat(Player* bot, char const* key);

    // Validate `key` against the registry (must exist AND be player-facing),
    // clamp `rawValue` to the registry range, normalise discrete types, and
    // store it for `runOwner`. Returns false (and fills `err` if non-null) when
    // the key is unknown or not overridable. Never trusts the raw value.
    bool SetOverride(ObjectGuid runOwner, std::string const& key,
                     double rawValue, std::string* err = nullptr);

    // Drop a single override (empty key = every override for the run), reverting
    // to the conf/default. ClearRun wipes the whole run — call it when a run
    // ends so leader GUIDs don't accumulate.
    void ResetOverride(ObjectGuid runOwner, std::string const& key);
    void ClearRun(ObjectGuid runOwner);

    // True if `runOwner` has an explicit override for `key` (vs. inheriting the
    // conf/default). Used by the addon sync payload to flag overridden values.
    bool HasOverride(ObjectGuid runOwner, char const* key);

    // Effective value as a double, for building the addon sync payload by
    // iterating kDcSettings without a type switch at the call site.
    double GetEffectiveRaw(ObjectGuid runOwner, DcSettingDef const& def);

    // Drop every thread's cached conf-layer value so the next read re-resolves
    // from sConfigMgr. Call once after `.reload config` (WorldScript::
    // OnAfterConfigLoad) so an edited conf takes effect without restart. Cheap:
    // bumps an atomic generation counter that each thread compares lazily.
    void InvalidateConfCache();
}

#endif
