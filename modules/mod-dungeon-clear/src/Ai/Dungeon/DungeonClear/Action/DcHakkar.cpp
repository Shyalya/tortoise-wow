/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Sunken Temple (map 109) Avatar of Hakkar encounter orchestration, split out of
// DcEngageActions.cpp (fable2 T2.5): ~320 self-contained lines of encounter
// helpers, the three per-instance registries (mutex-guarded), and the three
// trigger/action pairs that drive suppressor peels, flame dousing, and blood
// looting. The trigger/action CLASSES are declared in the shared headers
// (Trigger/DungeonClearTriggers.h, Action/DungeonClearActions.h) and registered
// by Strategy/DungeonClearStrategy.cpp; only their bodies live here. Keeping the
// encounter in its own TU keeps DcEngageActions.cpp focused on the generic
// engage/skirt/door/objective machinery.

#include "DungeonClearActions.h"

#include <cstdint>
#include <list>
#include <map>
#include <mutex>
#include <unordered_set>

#include "Creature.h"
#include "GameObject.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Position.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "Ai/Dungeon/DungeonClear/Trigger/DungeonClearTriggers.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Playerbots.h"

// --- Sunken Temple (map 109) Avatar of Hakkar encounter ------------------
// In-combat handler for the Sanctum of the Fallen, living in DC's own combat
// strategy (so it runs every tick mid-fight, unlike the declarative event
// executor which is dormant in combat). Gated to the live encounter; inert
// everywhere else. See deployment-files/docs/...sunken-temple-events_plan.md §11.
namespace
{
namespace DcHakkar
{
    constexpr uint32 MAP_SUNKEN_TEMPLE = 109;
    constexpr uint32 NPC_SHADE = 8440;
    constexpr uint32 NPC_SUPPRESSOR = 8497;
    constexpr uint32 NPC_BLOODKEEPER = 8438;
    constexpr uint32 ITEM_HAKKARI_BLOOD = 10460;
    constexpr float SCAN = 100.0f;
    constexpr float FLAME_USE_RANGE = 5.0f;
    constexpr float LOOT_RANGE = 6.0f;
    constexpr uint32 FLAMES[4] = {148418u, 148419u, 148420u, 148421u};

    // Per-instance latches: flame GUIDs already doused (each distinct flame bumps
    // the Shade's douse counter once; four -> the Avatar) and Bloodkeeper corpse
    // GUIDs already harvested for blood. Keyed by instance id and cleared when the
    // encounter isn't live, so a re-run of the same (GUID-stable, DB-spawned)
    // objects starts fresh.
    std::map<uint32, std::unordered_set<ObjectGuid>> g_doused;
    std::map<uint32, std::unordered_set<ObjectGuid>> g_bloodTaken;
    std::map<uint32, uint32> g_lastStatusMs;  // leader-only status heartbeat throttle

    // One lock covering all three registries. They are keyed by instance id, and
    // two concurrent Sunken Temple instances tick on different MapUpdater threads
    // (the realm runs a high MapUpdate.Threads pool), so the map *structure* —
    // operator[] node insertion and erase — races across threads. Same discipline
    // as g_leaderCache/g_leaderCombatSince in DcLeaderSignal. The per-instance set
    // contents are only ever touched by that instance's own map thread, so callers
    // that iterate a set copy it under the lock and scan the snapshot lock-free.
    std::mutex g_hakkarMutex;

    // True while the Avatar encounter is live: in the Sanctum and the Shade (8440,
    // present only during the event) or a Suppressor is up nearby.
    bool EncounterLive(Player* bot)
    {
        if (!bot || bot->GetMapId() != MAP_SUNKEN_TEMPLE)
            return false;
        return bot->FindNearestCreature(NPC_SHADE, SCAN, /*alive*/ true) != nullptr
            || bot->FindNearestCreature(NPC_SUPPRESSOR, SCAN, /*alive*/ true) != nullptr;
    }

    // Once every 5s, the leader emits a single glance-able progress line: flames
    // doused, suppressors alive/channelling, and whether the Shade is still up.
    // Leader-only + time-throttled so the three Hakkar triggers calling this each
    // tick produce exactly one line per window, not three-per-tick-per-bot.
    void MaybeLogStatus(Player* bot)
    {
        if (!bot || !DcLeaderSignal::IsDungeonClearLeader(bot))
            return;
        uint32 const now = getMSTime();
        {
            std::lock_guard<std::mutex> lock(g_hakkarMutex);
            uint32& last = g_lastStatusMs[bot->GetInstanceId()];
            if (last != 0 && (now - last) < 5000)
                return;
            last = now;
        }

        std::list<Creature*> supps;
        bot->GetCreatureListWithEntryInGrid(supps, NPC_SUPPRESSOR, SCAN);
        uint32 suppAlive = 0, channelling = 0;
        for (Creature* c : supps)
            if (c && c->IsAlive())
            {
                ++suppAlive;
                if (!c->IsInCombat())
                    ++channelling;
            }
        bool const shade = bot->FindNearestCreature(NPC_SHADE, SCAN, /*alive*/ true) != nullptr;
        uint32 dousedCount;
        {
            std::lock_guard<std::mutex> lock(g_hakkarMutex);
            dousedCount = static_cast<uint32>(g_doused[bot->GetInstanceId()].size());
        }
        LOG_INFO("playerbots.dungeonclear",
                 "[dungeon-clear] Hakkar status: flames {}/4 | suppressors {} alive ({} channelling) | shade {}",
                 dousedCount, suppAlive, channelling,
                 shade ? "up" : "gone");
    }

    // Drop the per-instance latches once the encounter is over, so the next pull
    // of the (GUID-stable) flames/corpses starts clean. Emits the status heartbeat
    // while live. Returns EncounterLive.
    bool LiveOrReset(Player* bot)
    {
        if (EncounterLive(bot))
        {
            MaybeLogStatus(bot);
            return true;
        }
        if (bot)
        {
            std::lock_guard<std::mutex> lock(g_hakkarMutex);
            g_doused.erase(bot->GetInstanceId());
            g_bloodTaken.erase(bot->GetInstanceId());
            g_lastStatusMs.erase(bot->GetInstanceId());
        }
        return false;
    }

    // Nearest Suppressor still CHANNELLING (alive, not yet in combat). A
    // suppressor's drain is an out-of-combat SmartAI event, so once it is tagged
    // (in combat) it is neutralised and normal combat finishes it — chasing one
    // already in combat would starve the loot/douse work. Only an untagged one is
    // worth the top-priority peel.
    Creature* NearestChannellingSuppressor(Player* bot)
    {
        if (!bot)
            return nullptr;
        std::list<Creature*> supps;
        bot->GetCreatureListWithEntryInGrid(supps, NPC_SUPPRESSOR, SCAN);
        Creature* best = nullptr;
        float bestDist = 0.0f;
        for (Creature* c : supps)
        {
            if (!c || !c->IsAlive() || c->IsInCombat())
                continue;
            float const d = bot->GetDistance(c);
            if (!best || d < bestDist)
            {
                best = c;
                bestDist = d;
            }
        }
        return best;
    }

    // Nearest dead Bloodkeeper corpse this party hasn't harvested yet.
    Creature* NearestUnharvestedBloodkeeper(Player* bot)
    {
        if (!bot)
            return nullptr;
        std::unordered_set<ObjectGuid> taken;
        {
            std::lock_guard<std::mutex> lock(g_hakkarMutex);
            taken = g_bloodTaken[bot->GetInstanceId()];
        }
        std::list<Creature*> keepers;
        bot->GetCreatureListWithEntryInGrid(keepers, NPC_BLOODKEEPER, SCAN);
        Creature* best = nullptr;
        float bestDist = 0.0f;
        for (Creature* c : keepers)
        {
            if (!c || c->IsAlive() || taken.count(c->GetObjectGuid()))
                continue;
            float const d = bot->GetDistance(c);
            if (!best || d < bestDist)
            {
                best = c;
                bestDist = d;
            }
        }
        return best;
    }

    // Nearest of the four corner flames this party has not yet doused.
    GameObject* NearestUndousedFlame(Player* bot)
    {
        if (!bot)
            return nullptr;
        std::unordered_set<ObjectGuid> used;
        {
            std::lock_guard<std::mutex> lock(g_hakkarMutex);
            used = g_doused[bot->GetInstanceId()];
        }
        GameObject* best = nullptr;
        float bestDist = 0.0f;
        for (uint32 entry : FLAMES)
        {
            GameObject* go = bot->FindNearestGameObject(entry, SCAN);
            if (!go || used.count(go->GetObjectGuid()))
                continue;
            float const d = bot->GetDistance(go);
            if (!best || d < bestDist)
            {
                best = go;
                bestDist = d;
            }
        }
        return best;
    }
}
}

bool DungeonClearHakkarSuppressorTrigger::IsActive()
{
    return DcHakkar::LiveOrReset(bot)
        && DcHakkar::NearestChannellingSuppressor(bot) != nullptr;
}

bool DungeonClearHakkarSuppressorAction::Execute(Event& /*event*/)
{
    // Only a suppressor not yet in combat is worth peeling onto — tagging it
    // (entering combat) silences its out-of-combat drain channel, after which
    // normal combat finishes it and the party returns to loot/douse.
    Creature* supp = DcHakkar::NearestChannellingSuppressor(bot);
    if (!supp)
        return false;
    return EngageDirect(supp);
}

bool DungeonClearHakkarFlameTrigger::IsActive()
{
    if (!DcHakkar::LiveOrReset(bot))
        return false;
    if (!bot->HasItemCount(DcHakkar::ITEM_HAKKARI_BLOOD, 1))
        return false;
    return DcHakkar::NearestUndousedFlame(bot) != nullptr;
}

bool DungeonClearHakkarFlameAction::Execute(Event& /*event*/)
{
    if (!bot->HasItemCount(DcHakkar::ITEM_HAKKARI_BLOOD, 1))
        return false;
    GameObject* flame = DcHakkar::NearestUndousedFlame(bot);
    if (!flame)
        return false;
    if (!bot->IsWithinDistInMap(flame, DcHakkar::FLAME_USE_RANGE))
    {
        // Walk the blood carrier to the flame (mid-combat; NORMAL priority).
        return DcMoveTo(bot->GetMapId(), flame->GetPositionX(), flame->GetPositionY(),
                        flame->GetPositionZ(), /*idle*/ false, /*react*/ false,
                        /*normal_only*/ false, /*exact_waypoint*/ false,
                        MovementPriority::MOVEMENT_NORMAL);
    }
    // In range — use the flame (fires its GOSSIP_HELLO event: SET_COUNTER 1 +1 on
    // the Shade, the douse that counts toward 4 -> Avatar).
    bot->SetFacingToObject(flame);
    flame->Use(bot);
    // Spend ONE Hakkari Blood per flame, by force. There is NO clean built-in
    // consume path to reuse: nothing in the core decrements a lock's key item on
    // GO use, and the flame's lock (520 = item 10460) only GATES use client-side.
    // The "authentic" consume is the blood's own on-use spell 12253 "Dowse Eternal
    // Flame" (an OPEN_LOCK with a 1s cast), but that is split from the counter —
    // the +1 fires only on the flame's GossipHello (SmartAI event 64, GossipHello-
    // only filter), which flame->Use() above already drives, while 12253's
    // OPEN_LOCK path does NOT — and a 1s mid-combat cast is interruptible, which
    // would let a single blood douse several flames again. So we just burn it:
    // each douse costs a blood (4 gathered across the party) and the flame
    // trigger's HasItemCount gate re-arms so the carrier loots more once empty.
    bot->DestroyItemCount(DcHakkar::ITEM_HAKKARI_BLOOD, 1, /*update*/ true);
    uint32 doused;
    {
        std::lock_guard<std::mutex> lock(DcHakkar::g_hakkarMutex);
        auto& set = DcHakkar::g_doused[bot->GetInstanceId()];
        set.insert(flame->GetObjectGuid());
        doused = static_cast<uint32>(set.size());
    }
    LOG_INFO("playerbots.dungeonclear",
             "[dungeon-clear] {} doused Eternal Flame {} ({}/4 flames doused{})",
             bot->GetName(), flame->GetObjectGuid().ToString(), doused,
             doused >= 4 ? " -> Avatar should spawn" : "");
    return true;
}

bool DungeonClearHakkarLootBloodTrigger::IsActive()
{
    if (!DcHakkar::LiveOrReset(bot))
        return false;
    return DcHakkar::NearestUnharvestedBloodkeeper(bot) != nullptr;
}

bool DungeonClearHakkarLootBloodAction::Execute(Event& /*event*/)
{
    Creature* keeper = DcHakkar::NearestUnharvestedBloodkeeper(bot);
    if (!keeper)
        return false;
    if (!bot->IsWithinDistInMap(keeper, DcHakkar::LOOT_RANGE))
    {
        // Walk to the corpse (mid-combat; NORMAL priority).
        return DcMoveTo(bot->GetMapId(), keeper->GetPositionX(), keeper->GetPositionY(),
                        keeper->GetPositionZ(), /*idle*/ false, /*react*/ false,
                        /*normal_only*/ false, /*exact_waypoint*/ false,
                        MovementPriority::MOVEMENT_NORMAL);
    }

    // Loot the blood for real — no fabrication. Hakkari Blood (10460) is a 100%
    // drop on the Bloodkeeper (creature_loot_template 8438), so generating the
    // corpse's own loot (what opening it does) and taking that item is the genuine
    // article, not a granted copy. FillLoot only when the corpse hasn't been
    // opened yet; then StoreLootItem the blood slot.
    Loot& loot = keeper->loot;
    if (loot.items.empty() && loot.gold == 0)
        loot.FillLoot(keeper->GetCreatureTemplate()->loot_id, LootTemplates_Creature,
                      bot, /*personal*/ true, /*noEmptyError*/ true);

    bool got = false;
    bool sawBlood = false;
    for (uint8 slot = 0; slot < loot.items.size(); ++slot)
    {
        LootItem const& li = loot.items[slot];
        if (li.itemid != DcHakkar::ITEM_HAKKARI_BLOOD || li.is_looted)
            continue;
        sawBlood = true;
        // Tortoise port: no Player::StoreLootItem here; storing the item and
        // marking the slot looted is what that call did. NotifyItemRemoved
        // keeps the roll/looter bookkeeping straight, same as the loot handler.
        ItemPosCountVec dest;
        InventoryResult msg = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, li.itemid, li.count);
        if (msg == EQUIP_ERR_OK)
        {
            Item* stored = bot->StoreNewItem(dest, li.itemid, true, li.randomPropertyId);
            bot->SendNewItem(stored, li.count, false, false, true);
            loot.items[slot].is_looted = true;
            loot.NotifyItemRemoved(slot);
        }
        got = (msg == EQUIP_ERR_OK);
        break;
    }

    // A transient store failure (e.g. bags momentarily full) leaves the blood in
    // the corpse — DON'T latch it, so this or another member retries it; the
    // douse draws from the next corpse meanwhile. Latch only once the blood is
    // actually taken, or the corpse genuinely has none left (already harvested by
    // a real player), so the scan doesn't spin on it forever.
    if (!got && sawBlood)
        return false;

    {
        std::lock_guard<std::mutex> lock(DcHakkar::g_hakkarMutex);
        DcHakkar::g_bloodTaken[bot->GetInstanceId()].insert(keeper->GetObjectGuid());
    }
    LOG_INFO("playerbots.dungeonclear",
             "[dungeon-clear] {} looted Hakkari Blood from Bloodkeeper {} (got={})",
             bot->GetName(), keeper->GetObjectGuid().ToString(), got);
    return got;
}

