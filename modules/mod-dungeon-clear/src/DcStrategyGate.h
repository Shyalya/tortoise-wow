/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_STRATEGY_GATE_H
#define _DC_STRATEGY_GATE_H

class Player;

// Dungeon-gated install of the DC strategies. The whole module's per-tick cost
// (the 36 non-combat + 8 combat triggers, all checkInterval=1, plus the
// multiplier) used to be paid by EVERY bot in the realm because the strategies
// were injected into the playerbots default strategy set and installed on every
// login. Almost all of that is waste: a bot is only ever a dungeon-clear leader
// or follower while it is inside a dungeon/raid instance.
//
// This gate replaces the global install with a single per-bot invariant:
//
//     a bot has "dungeon clear" (non-combat) and "dungeon clear combat" (combat)
//     installed  <=>  it is currently on a dungeon/raid map (Map::IsDungeon()).
//
// The invariant is enforced from three drivers (all in DungeonClearModule.cpp),
// every one of which funnels through Reconcile():
//   1. OnPlayerLogin       — a bot that logs in inside an instance gets it now.
//   2. OnPlayerMapChanged  — applied on dungeon entry / stripped on exit, within
//                            a frame (responsiveness).
//   3. A throttled sweep in the world OnUpdate — re-asserts the invariant for
//      every bot regardless of how its strategy list was last rebuilt. This is
//      the correctness net: PlayerbotAI::ResetStrategies() (group/talent/LFG/
//      master change, `.playerbots reset`) wipes every strategy and rebuilds
//      from the default set — which no longer carries DC — so a reset WHILE IN A
//      DUNGEON would otherwise silently drop the triggers for the rest of the
//      run. The sweep restores them within one interval, so "any bot in a
//      dungeon has the triggers active" holds no matter the reset path (it also
//      covers self-bots created in-place via `.playerbots bot self`, which no
//      login/map hook sees).
namespace DcStrategyGate
{
    // Pure decision kernel (headless-testable: no game types). Given whether the
    // bot is on a dungeon/raid map and whether it currently has the DC strategy
    // installed, returns what to do to satisfy the invariant.
    enum class Action
    {
        None,     // already correct
        Install,  // in a dungeon, strategy missing -> add it
        Strip     // outside a dungeon, strategy present -> remove it
    };

    constexpr Action Decide(bool inDungeon, bool hasStrategy)
    {
        if (inDungeon && !hasStrategy)
            return Action::Install;
        if (!inDungeon && hasStrategy)
            return Action::Strip;
        return Action::None;
    }

    // The full per-bot reconciliation, composed from the kernel above plus the
    // cross-engine hygiene the two-engine invariant leaves implicit.
    //
    // Each DC strategy belongs to exactly ONE engine ("dungeon clear" is
    // STRATEGY_TYPE_NONCOMBAT, "dungeon clear combat" is STRATEGY_TYPE_COMBAT),
    // but nothing enforces that: both live in Ctx::sharedStrategyContexts, which
    // both engines consult, so `co +dungeon clear` (or the same name pasted into
    // AiPlayerbot.CombatStrategies) instantiates the non-combat strategy inside
    // the COMBAT engine. That is never correct, in a dungeon or out of one, and
    // it is not a state this module can produce on its own.
    //
    // Left alone it is permanent and invisible. PlayerbotRepository::Save writes
    // the combat strategy list to playerbots_db_store keyed on character GUID,
    // and Load replays it AFTER ResetStrategies() has rebuilt the engines — so
    // the bad row outlives config fixes, relogs, and resets. Meanwhile the whole
    // DC non-combat relevance ladder (FollowTank 25 … StrandedRecovery 42, see
    // DcRelevance.h) sits above the entire stock rotation band (ACTION_DEFAULT 5
    // / ACTION_NORMAL 10 / ACTION_HIGH 20), so a bot carrying the stray follows
    // and holds through every combat tick and never casts. That is the reported
    // "DPS stands there doing nothing" (issue #18).
    //
    // So the strays are stripped unconditionally, independent of inDungeon. This
    // does not rewrite the stored row — the gate never writes to the DB — but it
    // runs on login, on map change and on the sweep, so an affected bot is clean
    // for the whole session and the row heals on the next logout Save.
    struct Plan
    {
        Action nonCombat;      // "dungeon clear" in the non-combat engine
        Action combat;         // "dungeon clear combat" in the combat engine
        bool stripStrayInCombat;     // "dungeon clear" found in the combat engine
        bool stripStrayInNonCombat;  // "dungeon clear combat" found in non-combat
        bool teardown;         // no DC strategy survives -> tear the run state down
    };

    constexpr Plan MakePlan(bool inDungeon, bool hasNonCombat, bool hasCombat,
                            bool strayInCombat, bool strayInNonCombat)
    {
        Action const nonCombat = Decide(inDungeon, hasNonCombat);
        Action const combat = Decide(inDungeon, hasCombat);

        // "Will this bot still hold a DC strategy once the plan is applied?" —
        // the teardown condition. Phrased on the resulting state rather than on
        // "did anything get stripped" so that removing a stray from a bot that
        // is legitimately mid-run inside a dungeon does NOT abort its run.
        bool const keepsNonCombat = nonCombat == Action::Install || (nonCombat == Action::None && hasNonCombat);
        bool const keepsCombat = combat == Action::Install || (combat == Action::None && hasCombat);
        bool const anyChange = nonCombat != Action::None || combat != Action::None ||
                               strayInCombat || strayInNonCombat;

        return Plan{ nonCombat, combat, strayInCombat, strayInNonCombat,
                     anyChange && !keepsNonCombat && !keepsCombat };
    }

    // Bring one bot into compliance with the invariant. Idempotent and cheap when
    // already compliant (two HasStrategy reads). Safe to call on a non-bot
    // (no-ops) and on any player. MUST be called outside the bot's own engine
    // tick (login/map-change hooks and the world OnUpdate all qualify) — never
    // from a DC trigger/action.
    void Reconcile(Player* bot);

    // Re-assert the invariant for every online bot. Cheap per bot; call on a
    // throttled cadence from the world tick. This is the correctness net for the
    // reset-while-in-dungeon case described above.
    void ReconcileAllBots();
}

#endif  // _DC_STRATEGY_GATE_H
