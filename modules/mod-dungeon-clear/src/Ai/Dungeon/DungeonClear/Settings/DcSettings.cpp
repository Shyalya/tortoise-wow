/*
 * mod-dungeon-clear — DcSettings.cpp
 */

#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <unordered_map>

#include "Config.h"
#include "DBCEnums.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"

namespace
{
    // Per-run override store, keyed by the run's leader-tank GUID. Values are
    // stored as doubles already clamped/normalised by SetOverride. Accessed only
    // from the map thread (config reads during the AI tick, and the addon hook's
    // OnPlayerBeforeSendChatMessage, both run there), so no lock is needed — and
    // the worker-thread centering path never reaches here because its keys are
    // server-only (see GetRaw).
    std::unordered_map<ObjectGuid, std::unordered_map<std::string, double>> g_overrides;

    std::string FullKey(char const* keySuffix)
    {
        return std::string("DungeonClear.") + keySuffix;
    }

    // The heroic sibling of a conf key: "DungeonClear.<Key>.Heroic". Read only
    // while the run is at DUNGEON_DIFFICULTY_HEROIC.
    std::string FullKeyHeroic(char const* keySuffix)
    {
        return FullKey(keySuffix) + ".Heroic";
    }

    // Read one registry key straight from conf (uncached), as a double.
    //
    // showLogs=false on every GetOption call: by default the core logs a
    // "Config: Missing property ..." warning whenever a key is absent from the
    // deployed conf, and EVERY one of these ~55 tunables is optional (the
    // registry holds the authoritative default — a missing conf line is normal,
    // expected operation, not an error). Read once per tick per bot, that
    // warning floods the console. Suppressing it here is the definitive fix: the
    // core never logs regardless of how often or from which thread we read. The
    // ConfValue cache below still applies, so we also avoid the per-call config-
    // map lookup / getenv() / string churn — that addresses the read COST; this
    // flag addresses the LOG. Defaults are documented in the .conf.dist, so the
    // admin loses no information.
    double ConfValueUncached(DcSettingDef const& d)
    {
        std::string const full = FullKey(d.key);
        switch (d.type)
        {
            case DcType::Bool:
                return sConfigMgr->GetOption<bool>(full, d.defVal != 0.0, false) ? 1.0 : 0.0;
            case DcType::UInt:
                return static_cast<double>(
                    sConfigMgr->GetOption<uint32>(full, static_cast<uint32>(d.defVal), false));
            case DcType::Int:
                return static_cast<double>(
                    sConfigMgr->GetOption<int32>(full, static_cast<int32>(d.defVal), false));
            case DcType::Float:
            default:
                return static_cast<double>(
                    sConfigMgr->GetOption<float>(full, static_cast<float>(d.defVal), false));
        }
    }

    // Process-wide cache of every key's conf-resolved value, indexed by the key's
    // position in kDcSettings. ONE copy for the whole server (not thread_local),
    // populated exactly once — lazily on the first read, re-populated on
    // `.reload config` — so warmup costs ~kDcSettingCount config resolutions
    // TOTAL rather than that many per map-update pool thread (the server runs a
    // high MapUpdate.Threads for playerbots, so a per-thread cache multiplied the
    // warmup by the pool size). Each slot is a lock-free atomic<double>: readers
    // (the map-update pool, the centering workers, the path worker) just load,
    // with no per-read locking once the table is warm.
    std::array<std::atomic<double>, kDcSettingCount> g_confCache;
    // The HEROIC-difficulty twin of g_confCache, holding each key's fully-folded
    // heroic value: the "<Key>.Heroic" conf line if the admin wrote one, else the
    // registry's authored heroicVal, else the row's ordinary conf/default value.
    // Folding the whole chain once at warm time is what keeps the per-read cost
    // identical to the normal path — a heroic read is one atomic load, exactly
    // like a normal one, with no extra branching on which layer supplied it.
    std::array<std::atomic<double>, kDcSettingCount> g_confCacheHeroic;
    // Does this row have a heroic layer AT ALL (conf line or registry value)?
    // Rows without one skip the difficulty lookup entirely, so the map/instance
    // read is paid only by the handful of keys that can actually differ.
    std::array<std::atomic<bool>, kDcSettingCount> g_heroicLayer;
    std::atomic<bool> g_confCacheReady{false};
    std::mutex g_confCacheLoadMutex;

    // Resolve the heroic layer for one row, given its already-resolved normal
    // value. Presence of the "<Key>.Heroic" conf line is detected by reading it
    // as a STRING (an absent line comes back as the empty default): the typed
    // readers can only return "the default", which is indistinguishable from an
    // admin who explicitly wrote the default. Costs two reads, but only at warm
    // time — never per tick. Returns false when the row has no heroic layer, in
    // which case `out` is left as the normal value.
    bool ResolveHeroic(DcSettingDef const& d, double normalValue, double& out)
    {
        out = normalValue;
        std::string const full = FullKeyHeroic(d.key);
        if (!sConfigMgr->GetOption<std::string>(full, "", false).empty())
        {
            switch (d.type)
            {
                case DcType::Bool:
                    out = sConfigMgr->GetOption<bool>(full, false, false) ? 1.0 : 0.0;
                    break;
                case DcType::UInt:
                    out = static_cast<double>(sConfigMgr->GetOption<uint32>(full, 0, false));
                    break;
                case DcType::Int:
                    out = static_cast<double>(sConfigMgr->GetOption<int32>(full, 0, false));
                    break;
                case DcType::Float:
                default:
                    out = static_cast<double>(sConfigMgr->GetOption<float>(full, 0.0f, false));
                    break;
            }
            return true;
        }
        if (DcHasHeroicDefault(d))
        {
            out = d.heroicVal;
            return true;
        }
        return false;
    }

    // Resolve every key from conf into g_confCache and its heroic twin (relaxed
    // stores; an individual slot's load can never tear, and the keys are
    // independent so a reader seeing a half-applied reload for one tick is
    // harmless). Caller holds the load mutex OR runs at a point where no reader
    // races (config load on the world thread).
    void PopulateConfCache()
    {
        for (std::size_t i = 0; i < kDcSettingCount; ++i)
        {
            double const normal = ConfValueUncached(kDcSettings[i]);
            g_confCache[i].store(normal, std::memory_order_relaxed);

            double heroic = normal;
            bool const layered = ResolveHeroic(kDcSettings[i], normal, heroic);
            g_confCacheHeroic[i].store(heroic, std::memory_order_relaxed);
            g_heroicLayer[i].store(layered, std::memory_order_relaxed);
        }
        g_confCacheReady.store(true, std::memory_order_release);
    }

    // Cache slot for a row, or kDcSettingCount when the row is not in the table
    // (every caller passes a row from FindDcSetting / kDcSettings, so this is
    // purely defensive — an unknown row reads uncached rather than risking an
    // out-of-range slot). Warms the table on first use.
    std::size_t CacheSlot(DcSettingDef const& d)
    {
        // First read of the server's life warms the whole table under a one-shot
        // lock; every read after the release-store sees ready and skips the lock.
        if (!g_confCacheReady.load(std::memory_order_acquire))
        {
            std::lock_guard<std::mutex> lock(g_confCacheLoadMutex);
            if (!g_confCacheReady.load(std::memory_order_relaxed))
                PopulateConfCache();
        }

        std::ptrdiff_t const off = &d - &kDcSettings[0];
        if (off < 0 || static_cast<std::size_t>(off) >= kDcSettingCount)
            return kDcSettingCount;
        return static_cast<std::size_t>(off);
    }

    // conf -> registry default, returned as a double regardless of type, cached.
    // `heroic` picks the folded heroic twin (identical to the normal value for
    // any row without a heroic layer, so the flag is safe to pass blindly).
    double ConfValue(DcSettingDef const& d, bool heroic)
    {
        std::size_t const slot = CacheSlot(d);
        if (slot == kDcSettingCount)
            return ConfValueUncached(d);
        return heroic ? g_confCacheHeroic[slot].load(std::memory_order_relaxed)
                      : g_confCache[slot].load(std::memory_order_relaxed);
    }

    // Does this row have a heroic layer at all? Rows that don't (nearly all of
    // them) never pay for the difficulty lookup below.
    bool HasHeroicLayer(DcSettingDef const& d)
    {
        std::size_t const slot = CacheSlot(d);
        if (slot == kDcSettingCount)
            return false;
        return g_heroicLayer[slot].load(std::memory_order_relaxed);
    }

    // Is the run this read belongs to running at heroic difficulty?
    //
    // `mapCtx` is the bot the read came through when there was one — the hot
    // path, since almost every call site is DcSettings::GetX(bot, key) — and
    // reading the difficulty off its own map costs nothing. The GUID-only path
    // (the addon sync payload and `.dc config`, both once-per-request, never
    // per tick) falls back to resolving the leader; server-only keys pass an
    // empty owner and never reach here at all.
    bool IsHeroicRun(ObjectGuid owner, Player* mapCtx)
    {
        Player* p = mapCtx;
        if (!p && !owner.IsEmpty())
            p = ObjectAccessor::FindPlayer(owner);
        Map* map = p ? p->FindMap() : nullptr;
        return map && map->IsDungeon() && map->GetDifficulty() == DUNGEON_DIFFICULTY_HEROIC;
    }

    // The full resolution chain for one registry entry:
    //
    //   per-run override -> conf "<Key>.Heroic" -> registry heroicVal
    //                    -> conf "<Key>"        -> registry defVal
    //
    // The override is deliberately difficulty-BLIND and sits above the heroic
    // layer: a human who typed a value for this run means it for this run, and a
    // default — however well chosen — must never overrule an explicit one.
    double GetRaw(ObjectGuid owner, DcSettingDef const& d, Player* mapCtx = nullptr)
    {
        if (d.playerFacing && !owner.IsEmpty())
        {
            auto const runIt = g_overrides.find(owner);
            if (runIt != g_overrides.end())
            {
                auto const keyIt = runIt->second.find(d.key);
                if (keyIt != runIt->second.end())
                    return keyIt->second;  // already clamped at SetOverride time
            }
        }
        bool const heroic = HasHeroicLayer(d) && IsHeroicRun(owner, mapCtx);
        return ConfValue(d, heroic);
    }

    // Resolve the run owner from any bot in the run. Skipped (Empty) for
    // server-only keys so non-player callers don't pay the group walk.
    ObjectGuid OwnerForBot(Player* bot, DcSettingDef const& d)
    {
        if (!d.playerFacing || !bot)
            return ObjectGuid::Empty;

        Player* leader = DcLeaderSignal::FindLeaderTank(bot);
        return leader ? leader->GetObjectGuid() : ObjectGuid::Empty;
    }
}

namespace DcSettings
{
    bool GetBool(ObjectGuid runOwner, char const* key)
    {
        if (DcSettingDef const* d = FindDcSetting(key))
            return GetRaw(runOwner, *d) != 0.0;
        return sConfigMgr->GetOption<bool>(FullKey(key), false);
    }

    int32 GetInt(ObjectGuid runOwner, char const* key)
    {
        if (DcSettingDef const* d = FindDcSetting(key))
            return static_cast<int32>(std::lround(GetRaw(runOwner, *d)));
        return sConfigMgr->GetOption<int32>(FullKey(key), 0);
    }

    uint32 GetUInt(ObjectGuid runOwner, char const* key)
    {
        if (DcSettingDef const* d = FindDcSetting(key))
            return static_cast<uint32>(std::lround(GetRaw(runOwner, *d)));
        return sConfigMgr->GetOption<uint32>(FullKey(key), 0);
    }

    float GetFloat(ObjectGuid runOwner, char const* key)
    {
        if (DcSettingDef const* d = FindDcSetting(key))
            return static_cast<float>(GetRaw(runOwner, *d));
        return sConfigMgr->GetOption<float>(FullKey(key), 0.0f);
    }

    // The bot is passed through as the map context so the difficulty comes off
    // the caller's own map — no owner lookup, no ObjectAccessor, on the path that
    // runs every tick.
    bool GetBool(Player* bot, char const* key)
    {
        if (DcSettingDef const* d = FindDcSetting(key))
            return GetRaw(OwnerForBot(bot, *d), *d, bot) != 0.0;
        return sConfigMgr->GetOption<bool>(FullKey(key), false);
    }

    int32 GetInt(Player* bot, char const* key)
    {
        if (DcSettingDef const* d = FindDcSetting(key))
            return static_cast<int32>(std::lround(GetRaw(OwnerForBot(bot, *d), *d, bot)));
        return sConfigMgr->GetOption<int32>(FullKey(key), 0);
    }

    uint32 GetUInt(Player* bot, char const* key)
    {
        if (DcSettingDef const* d = FindDcSetting(key))
            return static_cast<uint32>(std::lround(GetRaw(OwnerForBot(bot, *d), *d, bot)));
        return sConfigMgr->GetOption<uint32>(FullKey(key), 0);
    }

    float GetFloat(Player* bot, char const* key)
    {
        if (DcSettingDef const* d = FindDcSetting(key))
            return static_cast<float>(GetRaw(OwnerForBot(bot, *d), *d, bot));
        return sConfigMgr->GetOption<float>(FullKey(key), 0.0f);
    }

    bool SetOverride(ObjectGuid runOwner, std::string const& key, double rawValue,
                     std::string* err)
    {
        DcSettingDef const* d = FindDcSetting(key);
        if (!d)
        {
            if (err)
                *err = "unknown setting";
            return false;
        }
        if (!d->playerFacing)
        {
            if (err)
                *err = "setting is not overridable";
            return false;
        }
        if (runOwner.IsEmpty())
        {
            if (err)
                *err = "no active dungeon run";
            return false;
        }

        double v = std::clamp(rawValue, d->minVal, d->maxVal);
        // Discrete types snap to whole numbers (bool collapses to 0/1 via clamp).
        if (d->type != DcType::Float)
            v = std::round(v);

        g_overrides[runOwner][d->key] = v;
        LOG_DEBUG("playerbots.dungeonclear",
                  "DcSettings: override {} = {} for run {}", d->key, v,
                  runOwner.ToString());
        return true;
    }

    void ResetOverride(ObjectGuid runOwner, std::string const& key)
    {
        auto const runIt = g_overrides.find(runOwner);
        if (runIt == g_overrides.end())
            return;

        if (key.empty())
        {
            g_overrides.erase(runIt);
            return;
        }

        runIt->second.erase(key);
        if (runIt->second.empty())
            g_overrides.erase(runIt);
    }

    void ClearRun(ObjectGuid runOwner)
    {
        g_overrides.erase(runOwner);
    }

    bool HasOverride(ObjectGuid runOwner, char const* key)
    {
        auto const runIt = g_overrides.find(runOwner);
        if (runIt == g_overrides.end())
            return false;
        return runIt->second.find(key) != runIt->second.end();
    }

    double GetEffectiveRaw(ObjectGuid runOwner, DcSettingDef const& def)
    {
        return GetRaw(runOwner, def);
    }

    void InvalidateConfCache()
    {
        // Called from WorldScript::OnAfterConfigLoad on `.reload config`, on the
        // world thread between map updates. Re-resolve every key in place so an
        // edited conf value takes effect live; the lock serialises against a
        // concurrent first-read warm, and the per-slot atomic stores publish to
        // readers without a generation dance. Also primes the table at startup so
        // the first in-dungeon read never has to take the warm lock.
        std::lock_guard<std::mutex> lock(g_confCacheLoadMutex);
        PopulateConfCache();
    }
}
