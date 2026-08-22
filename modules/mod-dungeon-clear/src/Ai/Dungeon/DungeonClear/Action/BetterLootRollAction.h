/*
 * mod-dungeon-clear — BetterLootRollAction.h
 *
 * "Better Loot Rolling", improvement #1: a bot in "bot self" mode (master ==
 * bot — the human's own character running on autopilot) must NOT cast an
 * automatic Need/Greed vote on group loot. The bot and the human share one
 * character GUID, so the bot's vote is counted FOR the player and pre-empts
 * their roll dialog — a double roll. Suppressing the bot vote lets only the
 * player roll.
 *
 * Improvement #2: roll on gear the bot will grow into. Stock rolling asks
 * ItemUsageValue, which rejects any weapon/armor whose RequiredLevel is above
 * the bot's current level (BotCanUseItem fails), so the bot greeds or passes
 * on its own future upgrades. Here, when the level requirement is the ONLY
 * thing blocking the item, the vote is computed as if the bot already were
 * that level: Need when the bot will have the proficiency at that level
 * (plate/mail unlocks at 40 included) and the item's stats score for its
 * spec, Greed otherwise. The server's LootNeedRollLevel/LootGreedRollLevel
 * and unique-equipped post-checks still apply, exactly as in stock.
 *
 * Improvement #3 (not in this class): bots roll immediately. Stock reaches
 * "loot roll" only off the "very often" RandomTrigger (a 1-in-3 chance checked
 * at most once per AiPlayerbot.RepeatDelay), so a pending roll sits unanswered
 * for many seconds. DungeonClearLootRollPendingTrigger fires this same action
 * every non-combat tick while a vote is pending — see DungeonClearTriggers.h
 * and the node in DungeonClearStrategy.cpp.
 *
 * Housed in this module (not in mod-playerbots) so the stock module stays
 * unedited and conflict-free on upstream pulls. The wiring is the same override
 * seam DungeonClear already uses for "auto release" (see StayDeadAction.h):
 * DungeonClearActionContext registers the "loot roll" creator name, and because
 * the engine's shared creator map keeps the LAST registration for a given name
 * (SharedNamedObjectContextList::Add) and the DungeonClear contexts are appended
 * AFTER playerbots builds its own, this creator wins for every bot of every
 * class.
 *
 * Gated by the config flag DungeonClear.BetterLootRolling (default off), which
 * leaves this behaving exactly like the stock LootRollAction. With the flag on
 * there are two cases and they do not overlap: a self-bot casts no vote at all
 * (improvement #1), and every other bot gets improvement #2 on the over-level
 * items and stock's own answer on everything else.
 *
 * Execute votes on every pending roll it is given, matching stock since
 * mod-playerbots #2496 — which replaced "one item per Execute" with all of
 * them. Matching matters here because, unlike stock, this action runs off a
 * per-tick trigger (improvement #3): one item per tick would hold the action
 * slot for as many ticks as the boss dropped items.
 */

#ifndef _DUNGEONCLEAR_BETTERLOOTROLLACTION_H
#define _DUNGEONCLEAR_BETTERLOOTROLLACTION_H

#include "LootRollAction.h"
#include "playerbot/strategy/values/ItemUsageValue.h"

class PlayerbotAI;

class DungeonClearBetterLootRollAction : public ai::LootRollAction
{
public:
    DungeonClearBetterLootRollAction(PlayerbotAI* botAI)
        : ai::LootRollAction(botAI, "loot roll") {}

    bool isUseful() override;

    // Tortoise port: the improvement hangs off CalculateRollVote, this tree's
    // own per-item hook, instead of overriding Execute and walking the roll
    // list by hand as upstream must. See the .cpp header comment.
    RollVote CalculateRollVote(ItemQualifier& itemQualifier) override;

private:
    bool IsFutureWearable(ItemPrototype const* proto) const;
    RollVote CalculateFutureVote(ItemPrototype const* proto, int32 randomProperty);
};

#endif
