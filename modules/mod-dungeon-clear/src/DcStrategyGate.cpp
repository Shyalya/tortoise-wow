/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcStrategyGate.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"

#include "Playerbots.h"
#include "PlayerbotAI.h"

#include "Ai/Dungeon/DungeonClear/Action/DcActionShared.h"
#include "Ai/Dungeon/DungeonClear/Util/DcFollowerLifecycle.h"
#include "Ai/Dungeon/DungeonClear/Util/DcMovement.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

namespace
{
    char const* const kNonCombat = "dungeon clear";
    char const* const kCombat    = "dungeon clear combat";

    // Strip-time cleanup. A bot that is losing the DC strategies must not carry
    // any live run state past the triggers that owned it:
    //   * a LEADER's `dungeon clear enabled` flag would otherwise survive on its
    //     value context and auto-resume the clear the next time it enters a
    //     dungeon and the strategy is re-installed. DisableDungeonClear resets the
    //     whole run (flags + approach/pull FSMs + long-path cache) in lockstep.
    //   * a FOLLOWER's persistent MoveFollow generator (installed by the
    //     follow-tank action) is never self-healed by a self-bot — see the
    //     selfbot-stale-movefollow note. Clear it explicitly, mirroring the
    //     follow-tank teardown tick.
    // Both are gated so the common case (a bot that never ran DC) does no work and
    // emits no addon chatter.
    void TeardownOnStrip(PlayerbotAI* botAI, Player* bot)
    {
        AiObjectContext* ctx = botAI->GetAiObjectContext();

        if (DcRun::Of(ctx).enabled)
            DcActionShared::DisableDungeonClear(
                botAI, "Left the dungeon \xe2\x80\x94 dungeon clear disabled.");

        ObjectGuid& followed =
            DcRefGet(ctx->GetValue<ObjectGuid>(DcKey::FollowedTank));
        if (!followed.IsEmpty())
        {
            DcMovement::StopBot(bot, DcMovement::Stop::Hold);
            followed = ObjectGuid::Empty;
            DcFollowerLifecycle::UnmarkFollowing(bot->GetObjectGuid());
        }
    }
}

namespace DcStrategyGate
{
    void Reconcile(Player* bot)
    {
        if (!bot)
            return;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            return;  // real player (no bot AI) — nothing to gate

        // FindMap, not GetMap: the world-sweep hits bots mid-login/teleport
        // (no map yet), and this engine's GetMap ASSERTS on a missing map -
        // the null-tolerant read the next line expects is FindMap.
        //
        // And NO DECISION on a mapless or teleporting bot at all. A bot in a
        // teleport window read as "not in a dungeon", so the sweep STRIPPED
        // it - and TeardownOnStrip runs DisableDungeonClear, which erases the
        // leader's `dungeon clear enabled` run flag. The strategy came back
        // on the next sweep, the flag did not, and every DC trigger sat dark
        // for the rest of the run (tr-20260822-223938-1: tank went silent at
        // t~563 right after a far-from-poly NearTeleportTo recovery; watchdog
        // ended the run 600s later with zero progress). Skip the tick; the
        // next sweep sees the landed bot.
        if (bot->IsBeingTeleported())
            return;
        Map* map = bot->FindMap();
        if (!map)
            return;
        bool const inDungeon = map->IsDungeon();

        bool const hasNon = botAI->HasStrategy(kNonCombat, BOT_STATE_NON_COMBAT);
        bool const hasCmb = botAI->HasStrategy(kCombat, BOT_STATE_COMBAT);

        // Each strategy in the engine it does NOT belong to. Never correct; see
        // the Plan comment in the header for how a bot gets into that state and
        // why it is otherwise permanent.
        bool const strayInCmb = botAI->HasStrategy(kNonCombat, BOT_STATE_COMBAT);
        bool const strayInNon = botAI->HasStrategy(kCombat, BOT_STATE_NON_COMBAT);

        // Per-engine decision via the pure kernel. The two engines are installed
        // and stripped together, but each is checked independently so a partial
        // state (e.g. a reset that rebuilt only one engine) self-heals.
        Plan const plan = MakePlan(inDungeon, hasNon, hasCmb, strayInCmb, strayInNon);

        if (plan.nonCombat == Action::None && plan.combat == Action::None &&
            !plan.stripStrayInCombat && !plan.stripStrayInNonCombat)
            return;  // already compliant — the hot path

        // DIAG(riddle): every sweep sees hasNon=0 while the DC triggers
        // demonstrably run - dump what the engine ACTUALLY carries, one line
        // per ~60s, so the mismatch names itself.
        {
            static uint32 s_lastDumpMs = 0;
            uint32 const nowMs = getMSTime();
            if (plan.nonCombat == Action::Install && nowMs - s_lastDumpMs > 60000)
            {
                s_lastDumpMs = nowMs;
                if (Engine* nc = botAI->GetEngine(BOT_STATE_NON_COMBAT))
                    LOG_INFO("playerbots.dungeonclear",
                             "[DC-GATE] {} nc engine carries: {}",
                             bot->GetName(), nc->ListStrategies());
            }
        }

        // State changes are rare (enter/leave dungeon, post-ResetStrategies
        // repair) - log them so a silent strategy thief shows up as a stream
        // of re-installs in the journal instead of nothing at all.
        LOG_INFO("playerbots.dungeonclear",
                 "[DC-GATE] {}: nonCombat={} combat={} (inDungeon={} hasNon={} hasCmb={})",
                 bot->GetName(),
                 plan.nonCombat == Action::Install ? "install" : (plan.nonCombat == Action::Strip ? "strip" : "-"),
                 plan.combat == Action::Install ? "install" : (plan.combat == Action::Strip ? "strip" : "-"),
                 inDungeon ? 1 : 0, hasNon ? 1 : 0, hasCmb ? 1 : 0);

        // Run the strip cleanup once, before removing any strategy, so the run
        // state is torn down while its values/actions still exist.
        if (plan.teardown)
            TeardownOnStrip(botAI, bot);

        // Install/strip and the stock-driver swap ride ONE ChangeStrategy
        // call each: the engine batches the trigger rebuild across the
        // comma list and rebuilds once over the final state. The first cut
        // issued a second ChangeStrategy right after the first, and a bot
        // crashed in ProcessTriggers on a half-rebuilt trigger list
        // seconds later - keep this atomic.
        switch (plan.nonCombat)
        {
            case Action::Install:
                botAI->ChangeStrategy("+dungeon clear,-grind,-travel,-rpg,-rpg jump,-follow,-wander,-bg,-battleground,-lfg",
                                      BOT_STATE_NON_COMBAT);
                // DIAG(riddle, binary test): does the call bite on THIS
                // object? after=1 and next sweep still hasNon=0 => something
                // resets the engines between sweeps (hunt the caller).
                // after=0 => the add fell through (context cannot resolve
                // "dungeon clear" for this bot - hunt the registry).
                LOG_INFO("playerbots.dungeonclear",
                         "[DC-GATE] {} install verdict: afterNon={} afterGrind={}",
                         bot->GetName(),
                         botAI->HasStrategy(kNonCombat, BOT_STATE_NON_COMBAT) ? 1 : 0,
                         botAI->HasStrategy("grind", BOT_STATE_NON_COMBAT) ? 1 : 0);
                break;
            case Action::Strip:
                botAI->ChangeStrategy("-dungeon clear,+grind,+travel,+rpg,+rpg jump,+follow,+bg,+battleground,+lfg",
                                      BOT_STATE_NON_COMBAT);
                break;
            case Action::None:
                break;
        }

        // The relay suppression rides the strategy: a DC party stands on top
        // of instance entrance/exit triggers, and stock's area-trigger relay
        // ported the run's tank out through the exit mid-run.
        // Why the stock drivers get stripped at all: the random-bot
        // noncombat set carries grind/travel/rpg - movement drivers that
        // FIGHT the DC advance for the mover. Live, the run's tank was
        // dragged back up the entrance tunnel toward the exit over and
        // over (parked at Z 86/101/216 above the floor) and runs crawled
        // at the entrance for their whole 600s window in that tug-of-war.
        // AiFactory only strips these for bots with a real human master; a
        // dc-owned bot needs them gone the same way (swap is in the atomic
        // ChangeStrategy above).
        //
        // -follow/-wander joined the strip after runs 34-36: every bot with a
        // master carries stock "follow" (or "wander" under
        // useWanderAsDefaultFollowStrategy), and the run master Dcdriver is
        // parked OUTSIDE on map 0 - so whenever advance yielded a tick
        // (loot/rest), follow walked the tank toward the point of the
        // instance nearest the unreachable master: the ENTRANCE at
        // (-14,-393). Three runs sawtoothed 50yd forward / 40yd back on
        // exactly that spot. DC's own follow-tank drives the followers; the
        // tank needs no follow target at all while a run owns it.
        if (plan.nonCombat == Action::Install)
            botAI->SetSuppressAreaTriggerRelay(true);
        else if (plan.nonCombat == Action::Strip)
            botAI->SetSuppressAreaTriggerRelay(false);
        switch (plan.combat)
        {
            case Action::Install: botAI->ChangeStrategy("+dungeon clear combat", BOT_STATE_COMBAT); break;
            case Action::Strip:   botAI->ChangeStrategy("-dungeon clear combat", BOT_STATE_COMBAT); break;
            case Action::None:    break;
        }

        if (plan.stripStrayInCombat)
            botAI->ChangeStrategy("-dungeon clear", BOT_STATE_COMBAT);
        if (plan.stripStrayInNonCombat)
            botAI->ChangeStrategy("-dungeon clear combat", BOT_STATE_NON_COMBAT);
    }

    void ReconcileAllBots()
    {
        // Iterate every online player and reconcile the ones that are bots.
        // Reconcile() no-ops on real players (no bot AI) and on already-compliant
        // bots, so this is cheap. Runs on the world thread inside World::Update,
        // the same thread that adds/removes players, so the container is stable
        // for the duration of the loop.
        // Instance access on this core; the container is the same player map.
    for (auto const& kv : sObjectAccessor.GetPlayers())
            Reconcile(kv.second);
    }
}
