/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_LOOT_POLICY_H
#define _DC_LOOT_POLICY_H

#include "Define.h"

class Player;
class Creature;
class PlayerbotAI;

class DcLootPolicy
{
public:
    // Sentinel ttl meaning "never expire — give up for the rest of the run."
    // Pass as GiveUpCurrentLoot's ttl when the reason a corpse was skipped is
    // permanent: empty, below the quality floor, skinnable-only, or holding only
    // loot this bot can never take by re-looting — roll-locked / won-by-another
    // (the winner's item auto-delivers, it is never re-looted), round-robin or
    // allowed-looter sets that exclude us, or bags full with no vendor to clear
    // them mid-dungeon. None of these become takeable later, so a sticky skip
    // can never re-arm the loot yield on a backtrack.
    //
    // A real ms ttl (LOOT_SKIP_STICKY's complement) is used only for the
    // residual cases the content inspector can't pre-classify away — a camp or
    // 15s-yield timeout on loot that LOOKED takeable but didn't complete (a
    // momentary second looter on the corpse, or own loot not yet reached). There
    // the ttl just retries once rather than permanently abandon possibly-real
    // loot. Sticky entries clear only with the whole list (DisableDungeonClear).
    static constexpr uint32 LOOT_SKIP_STICKY = 0u;

    // --- Per-corpse loot give-up list ---------------------------------------
    // Prunes expired give-up entries and strips the still-live ones from the
    // stock "available loot" stack, additionally clearing "loot target" when it
    // points at a skipped GUID (can-loot reads the target, not the stack). Call
    // at the top of every DC loot-yield decision — because the advance /
    // follow-tank actions run at higher relevance than the loot pipeline, this
    // executes before stock picks its nearest target, so both the loot flags
    // AND stock's target selection skip loot the bot already gave up on. No-op
    // when the give-up list is empty (the happy path is untouched). Module-only:
    // it mutates the stock loot values, never stock code.
    static void StripSkippedLoot(PlayerbotAI* botAI);

    // Marks the loot the bot is currently committed to — the stock "loot
    // target" if set, else the nearest entry in the available-loot stack — as
    // given-up for `ttlMs` (or permanently when ttlMs == LOOT_SKIP_STICKY).
    // Called when a loot yield times out, or proactively when the loot is
    // un-takeable, so the bot stops re-committing to a corpse/chest it can't
    // finish. No-op when nothing of the bot's own is resolvable (e.g. a tank
    // whose yield is only IsAnyPartyMemberLooting waiting on a follower — that
    // follower gives up its own loot).
    static void GiveUpCurrentLoot(PlayerbotAI* botAI, uint32 ttlMs);

    // Fast-skips a corpse the bot has been "camped" on — standing within
    // interaction range (can-loot true) of one specific plain corpse — for
    // longer than campTimeoutMs. A normal loot transaction clears in a tick or
    // two once the bot is in range, so a long camp means the loot is
    // un-finishable for this bot (group-roll items pending a real player's roll,
    // items reserved for others, bags full). Rather than waiting out the much
    // longer loot-yield timeout on every such corpse — the "stuck on certain
    // corpses" stall — this blacklists it for giveUpTtlMs and strips it right
    // away so the loot flags drop this tick. Gathering nodes (skinning / mining
    // / herbalism) are exempt: their multi-second cast is legitimate camping.
    // Tracks the camped GUID + arrival time in the "dungeon clear loot camp *"
    // values; resets them whenever the bot isn't in range of a corpse. Returns
    // true when it skipped a corpse this call. Module-only: mutates stock loot
    // values via GiveUpCurrentLoot / StripSkippedLoot, never stock code.
    static bool MaybeGiveUpCampedLoot(PlayerbotAI* botAI, uint32 campTimeoutMs, uint32 giveUpTtlMs);

    // Decides, BEFORE the bot walks over, whether the loot it is about to commit
    // to (stock "loot target", else the nearest entry in "available loot") is
    // worth a stop — and if not, blacklists + strips it this tick so the bot
    // never detours to it (or, if already parked, leaves at once) instead of
    // discovering its emptiness by camping out the loot-yield / camp timeouts.
    // This is the proactive, event-driven counterpart to MaybeGiveUpCampedLoot:
    // creature loot is generated at kill time, so the contents are knowable
    // without opening the corpse.
    //
    // Dungeon-clear only ever stops for two kinds of loot: creature CORPSES
    // (normal kill loot) and treasure CHESTS. Every other lootable interactable
    // in the world is ignored so the bot walks straight past it instead of
    // detouring onto — and often getting stuck on — it: herbalism / mining
    // gathering nodes (chest-type gameobjects gated by a profession-skill lock),
    // skinnable-only corpses, fishing holes, levers, quest objects and loose
    // item loot. Anything that isn't a corpse-with-takeable-loot or a real chest
    // is skipped on sight.
    //
    // Drains EVERY in-range unworthy corpse in one call, not just the nearest:
    // it re-evaluates the now-nearest pickup after each skip and repeats until
    // the nearest is worth a stop (or there is none left). This keeps the tank
    // from stutter-stopping once per skipped corpse when it backtracks through a
    // field of below-floor corpses — with all of them stripped in a single tick,
    // "has available loot" never stays armed for loot the bot won't take, so the
    // advance walks straight through, held only by the party-spread gate.
    //
    // A corpse is skipped when it holds nothing this bot can take, which is two
    // overlapping cases:
    //   - un-finishable: every unlooted item is locked in a group-loot roll the
    //     bot has not won (won items are auto-delivered, never re-looted),
    //     reserved for another looter, disallowed for the bot, or the bags are
    //     full — and there is no claimable gold.
    //   - below policy: no unlooted item meets DungeonClear.LootMinQuality (an
    //     ITEM_QUALITY_* floor). Quest / quest-starter items always qualify.
    //     A floor above 0 also stops gold alone from earning a detour, so the
    //     floor actually reduces the number of stops; floor 0 (default)
    //     preserves stock "loot everything" behaviour.
    //
    // The corpse-quality check is deliberately conservative — it skips a corpse
    // only when confident nothing is takeable — because a false skip drops real
    // loot, whereas a missed skip merely falls back to the existing timeout.
    // Gameobjects are skipped unless they are genuine chests; all non-chest and
    // gathering-node interactables are dropped outright. Returns true when it
    // skipped at least one pickup this call. Module-only: mutates stock loot
    // values via GiveUpCurrentLoot / StripSkippedLoot, never stock code.
    //
    // Every reason this function skips a corpse is PERMANENT for the run (empty,
    // below the quality floor, skinnable-only, a gathering node, not a chest) —
    // none of it can become takeable later the way a pending group roll can — so
    // the skips are recorded as LOOT_SKIP_STICKY. That stops the backtrack
    // re-stutter: a field of below-floor / empty corpses stays skipped for the
    // whole run instead of re-arming the loot yield once the give-up ttl lapses.
    static bool MaybeSkipUnworthyLoot(PlayerbotAI* botAI);

    // Returns true if `creature`'s corpse holds at least one item this bot can
    // take right now (allowed, not blocked in / lost to a group roll, meeting
    // the minQuality ITEM_QUALITY_* floor — quest items always pass) or, when
    // minQuality is 0, claimable gold. Inspects the server-side creature loot,
    // filled at death. Used by MaybeSkipUnworthyLoot; returns true (don't skip)
    // for anything it cannot positively classify.
    static bool CorpseHasTakeableLoot(Player* bot, Creature* creature, uint32 minQuality);

};

#endif  // _DC_LOOT_POLICY_H
