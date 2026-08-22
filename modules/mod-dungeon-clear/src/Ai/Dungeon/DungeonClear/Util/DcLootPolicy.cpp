/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcLootPolicy.h"

#include "DungeonClearMath.h"
#include "DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "AttackersValue.h"
#include "CellImpl.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "ItemTemplate.h"
#include "LootMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "InstanceScript.h"
#include "LootObjectStack.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "Chat.h"
#include "ServerFacade.h"
#include "Timer.h"
#include "World.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearLiveBossValue.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

void DcLootPolicy::StripSkippedLoot(PlayerbotAI* botAI)
{
    if (!botAI)
        return;

    AiObjectContext* ctx = botAI->GetAiObjectContext();
    std::map<ObjectGuid, uint32>& skip =
        ctx->GetValue<std::map<ObjectGuid, uint32>&>(DcKey::LootSkip)->Get();
    if (skip.empty())
        return;  // happy path: nothing was ever given up on.

    uint32 const now = getMSTime();
    LootObjectStack* stack = ctx->GetValue<LootObjectStack*>(DcKey::Stock::AvailableLoot)->Get();

    for (auto it = skip.begin(); it != skip.end();)
    {
        // Sticky entries (permanent skip reason — empty / below-floor / un-
        // takeable by nature) never expire; only DisableDungeonClear clears them.
        if (it->second != LOOT_SKIP_STICKY && now >= it->second)
        {
            // Expired — drop it so a residual transient block can be retried
            // once (a second looter who has since left the corpse, own loot
            // we couldn't reach in time). NOT for group rolls: a roll we lose
            // is gone for us and a roll we win auto-delivers, so roll-locked
            // loot is recorded sticky upstream, never here.
            it = skip.erase(it);
            continue;
        }
        if (stack)
            stack->Remove(it->first);  // keep it out of "has available loot"
        ++it;
    }

    // can-loot reads "loot target", not the stack, so a bot parked within 3yd
    // of a skipped corpse would keep can-loot true (and keep yielding) unless we
    // also drop the committed target here.
    LootObject const target = ctx->GetValue<LootObject>(DcKey::Stock::LootTarget)->Get();
    if (!target.guid.IsEmpty() && skip.find(target.guid) != skip.end())
        ctx->GetValue<LootObject>(DcKey::Stock::LootTarget)->Set(LootObject());
}
void DcLootPolicy::GiveUpCurrentLoot(PlayerbotAI* botAI, uint32 ttlMs)
{
    if (!botAI)
        return;

    AiObjectContext* ctx = botAI->GetAiObjectContext();

    // Prefer the target stock already committed to; otherwise the nearest loot
    // we'd pick next. Either is what kept the yield armed.
    ObjectGuid guid = ctx->GetValue<LootObject>(DcKey::Stock::LootTarget)->Get().guid;
    if (guid.IsEmpty())
        if (LootObjectStack* stack = ctx->GetValue<LootObjectStack*>(DcKey::Stock::AvailableLoot)->Get())
            guid = stack->GetLoot(sPlayerbotAIConfig.lootDistance).guid;
    if (guid.IsEmpty())
        return;  // nothing of our own to give up on (tank waiting on a follower)

    std::map<ObjectGuid, uint32>& skip =
        ctx->GetValue<std::map<ObjectGuid, uint32>&>(DcKey::LootSkip)->Get();
    if (ttlMs == LOOT_SKIP_STICKY)
    {
        skip[guid] = LOOT_SKIP_STICKY;  // never expires (permanent skip reason)
        return;
    }
    // A real ttl: store the expiry. Guard the one-tick window every ~49 days
    // where getMSTime() + ttlMs wraps to exactly the sticky sentinel — bump it
    // off 0 so a transient skip is never mistaken for a permanent one.
    uint32 expiry = getMSTime() + ttlMs;
    if (expiry == LOOT_SKIP_STICKY)
        expiry = 1u;
    skip[guid] = expiry;
}
bool DcLootPolicy::MaybeGiveUpCampedLoot(PlayerbotAI* botAI, uint32 campTimeoutMs, uint32 giveUpTtlMs)
{
    if (!botAI)
        return false;

    AiObjectContext* ctx = botAI->GetAiObjectContext();
    ObjectGuid& campGuid = DcRefGet(ctx->GetValue<ObjectGuid>(DcKey::LootCampGuid));

    // Only meaningful once the bot is standing in interaction range of a corpse
    // (can-loot true). While merely walking toward one (has-available-loot), the
    // broader loot-yield timeout — which budgets for the walk — applies instead.
    if (!ctx->GetValue<bool>(DcKey::Stock::CanLoot)->Get())
    {
        campGuid = ObjectGuid::Empty;  // not camping -> reset the clock
        return false;
    }

    LootObject const target = ctx->GetValue<LootObject>(DcKey::Stock::LootTarget)->Get();
    if (target.guid.IsEmpty())
    {
        campGuid = ObjectGuid::Empty;
        return false;
    }

    // Gathering nodes (skinning / mining / herbalism) carry a non-zero skillId
    // and a legitimate multi-second cast — don't mistake the channel for a stuck
    // corpse. Only plain creature / chest loot is subject to the camp cutoff.
    if (target.skillId != 0)
        return false;

    uint32& campStart = DcRefGet(ctx->GetValue<uint32>(DcKey::LootCampStart));
    uint32 const now = getMSTime();

    if (campGuid != target.guid)
    {
        // Just arrived at a (new) corpse -> start its camp clock.
        campGuid = target.guid;
        campStart = now;
        return false;
    }

    if (now - campStart < campTimeoutMs)
        return false;  // still within the brief grace a normal pickup needs

    // Standing on the same corpse, in range, well past a normal loot's window:
    // its loot is un-finishable for this bot. Blacklist it now and strip it so
    // the loot flags drop this tick, instead of burning the full loot-yield
    // timeout (and, for the tank, holding the whole party) on it.
    GiveUpCurrentLoot(botAI, giveUpTtlMs);
    StripSkippedLoot(botAI);
    campGuid = ObjectGuid::Empty;
    return true;
}
bool DcLootPolicy::MaybeSkipUnworthyLoot(PlayerbotAI* botAI)
{
    if (!botAI)
        return false;

    Player* bot = botAI->GetBot();
    AiObjectContext* ctx = botAI->GetAiObjectContext();

    uint32 const minQuality = DcSettings::GetUInt(bot, "LootMinQuality");
    // When set, the tank never stops for chests (or any other world object) —
    // only creature corpses are worth a detour. Default on: chests routinely
    // sat off the route and snared the navmesh approach. See DungeonClear.conf.
    bool const ignoreChests = DcSettings::GetBool(bot, "IgnoreChests");

    // Drain EVERY in-range unworthy corpse this tick, not just the single
    // nearest. Each pass judges the loot the bot would commit to next — stock's
    // chosen target if set, else the nearest in the stack (the same selection
    // GiveUpCurrentLoot blacklists, so the two stay aligned). When that corpse
    // is below the floor, we blacklist + strip it so the loot flags drop for it,
    // then re-evaluate the now-nearest remaining corpse and repeat.
    //
    // Judging only one per tick (the old behavior) made the tank stutter when
    // backtracking through a field of skipped corpses: it stripped one corpse
    // but "has available loot" stayed true for the rest, so the advance loot-
    // yield halted it (StopMoving) for as many ticks as there were corpses. By
    // clearing them all in a single tick the tank never stops for loot it won't
    // take — it walks straight through, held only by the party-spread gate. The
    // loop terminates because every skip removes the corpse from the stack (via
    // StripSkippedLoot), so GetLoot can't return it again; the cap is a belt-
    // and-braces guard against an unexpectedly large cluster.
    bool skippedAny = false;
    constexpr int kMaxDrain = 32;
    for (int i = 0; i < kMaxDrain; ++i)
    {
        LootObject target = ctx->GetValue<LootObject>(DcKey::Stock::LootTarget)->Get();
        if (target.guid.IsEmpty())
            if (LootObjectStack* stack = ctx->GetValue<LootObjectStack*>(DcKey::Stock::AvailableLoot)->Get())
                target = stack->GetLoot(sPlayerbotAIConfig.lootDistance);
        if (target.guid.IsEmpty())
            break;  // nothing left to judge

        // Classify the next pickup. Dungeon-clear stops for creature CORPSES
        // that hold loot we'd take, and (only when IgnoreChests is off) for
        // genuine treasure CHESTS; everything else is skipped so the bot never
        // detours onto it.
        bool keep = false;
        if (Creature* creature = botAI->GetCreature(target.guid))
        {
            // A corpse. Worth a stop only if it carries loot this bot can
            // actually take (above the quality floor, not locked in someone
            // else's roll). A skinnable-only corpse has no normal loot here, so
            // CorpseHasTakeableLoot is false and it is skipped like a gathering
            // node — the bot does not stop merely to skin.
            keep = CorpseHasTakeableLoot(bot, creature, minQuality);
        }
        else if (GameObject* go = botAI->GetGameObject(target.guid))
        {
            // A gameobject. With IgnoreChests on (the default) no world object
            // is ever worth a detour — the bot stops only for corpses. With it
            // off, stop only for real chests. Herbalism / mining gathering veins
            // are also chest-type gameobjects, but are gated by a profession-
            // skill lock — skillId carries that profession — so exclude them;
            // every non-chest gameobject (fishing hole, lever, quest object) is
            // excluded by type.
            keep = !ignoreChests && go->GetGoType() == GAMEOBJECT_TYPE_CHEST &&
                   target.skillId != SKILL_HERBALISM && target.skillId != SKILL_MINING;
        }
        // else: loose item loot or an unresolvable guid -> not a corpse or chest.

        if (keep)
            break;  // nearest is worth a stop -> let the loot pipeline run

        // Not a corpse-with-loot or a chest -> blacklist + strip now so the loot
        // flags drop this tick and the bot skips the detour entirely (the
        // proactive analogue of the camp/yield timeouts firing after a wasted
        // walk). Sticky: every reason we get here is permanent for the run, so
        // the corpse must never re-arm the yield on a later backtrack.
        GiveUpCurrentLoot(botAI, LOOT_SKIP_STICKY);
        StripSkippedLoot(botAI);
        skippedAny = true;
    }
    return skippedAny;
}
bool DcLootPolicy::CorpseHasTakeableLoot(Player* bot, Creature* creature, uint32 minQuality)
{
    if (!bot || !creature)
        return true;  // can't classify -> never skip on our account

    Loot const& loot = creature->loot;

    // Bags full -> only money is takeable (it needs no slot). Managed bots
    // auto-sell so this is rare, but it's a genuine un-finishable case the camp
    // timeout used to absorb. "bag space" is percent USED; 100 == no free slot.
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    bool const bagsFull =
        botAI && botAI->GetAiObjectContext()->GetValue<uint8>(DcKey::Stock::BagSpace)->Get() >= 100;

    for (LootItem const& item : loot.items)
    {
        if (item.is_looted)
            continue;

        // Won by / reserved for someone else. (A roll we WIN delivers the item
        // automatically; we never come back to the corpse for it, so skipping
        // here loses nothing.)
        if (item.lootOwner && item.lootOwner != bot->GetObjectGuid())
            continue;

        // Above-threshold group-loot item under an unresolved roll: not
        // free-lootable by us now (and won items arrive without re-looting).
        if (item.is_blocked && item.lootOwner != bot->GetObjectGuid())
            continue;

        // Round-robin / explicitly-allowed looter set that excludes us.
        // Tortoise port: this core stores ONE assigned looter (lootOwner), not
        // a set - same gate, narrower storage.
        if (!item.freeforall && !item.lootOwner.IsEmpty() &&
            item.lootOwner != bot->GetObjectGuid())
            continue;

        // Faction / condition / quest eligibility (mirrors the server's own
        // visibility check).
        if (!item.AllowedForPlayer(bot, creature))
            continue;

        ItemTemplate const* proto = sObjectMgr.GetItemPrototype(item.itemid);
        if (!proto)
            continue;

        // Quest / quest-starter items always qualify, regardless of the floor.
        bool const questItem = item.needs_quest || proto->StartQuest != 0;
        if (!questItem && proto->Quality < minQuality)
            continue;

        if (bagsFull)
            continue;  // would take it, but nowhere to put it

        return true;  // at least one item worth a stop
    }

    // No qualifying item. Gold earns a stop ONLY with no quality floor set
    // (minQuality 0 == stock "loot everything"); once a floor is set, gold-only
    // corpses are skipped so the floor genuinely cuts the number of stops.
    return loot.gold > 0 && minQuality == 0;
}
