/*
 * mod-dungeon-clear — DungeonClearModule.cpp
 *
 * Drop-in glue against STOCK mod-playerbots. Two scripts:
 *
 *  1. WorldScript — on the first world-update tick (guaranteed after
 *     playerbots' OnBeforeWorldInitialized -> PlayerbotAIConfig::Initialize ->
 *     AiObjectContext::BuildAllSharedContexts has built every shared context),
 *     append the four DungeonClear contexts into the engine's shared
 *     registries. Doing it on the first tick (not in our own
 *     OnBeforeWorldInitialized) sidesteps any module hook-ordering race.
 *
 *     CRITICAL: playerbots does NOT use one global registry. Each bot is built
 *     by AiFactory from a PER-CLASS context (WarriorAiObjectContext, …), and
 *     each of those declares its OWN shared static lists. The base
 *     AiObjectContext lists are only the fallback for class 0 (no real bot).
 *     So we must append into all ten class registries — appending into the
 *     base alone reaches no bot (that was the "dc on: unknown action" bug).
 *     The per-class lists are PUBLIC statics, so we touch them directly; only
 *     the base lists are private, reached via AiObjectContextAccess.h.
 *
 *  2. PlayerScript — on login, apply the single "dungeon clear" strategy to a
 *     bot's non-combat engine. It bundles the party-chat keyword listener
 *     (`dc on`, …), the driving advance/engage/stall ladder, and the
 *     follow-tank trigger. Resident on ALL bots but inert unless that bot's own
 *     "dungeon clear enabled" flag is set — every driving trigger and the
 *     multiplier gate on the flag, and follow-tank no-ops on the tank itself.
 *     Keeping it on every bot (mirroring the AiFactory add stock playerbots
 *     used to carry) is what lets non-tank party bots redirect their follow
 *     target to the tank while a tank has DC enabled, and revert to the player
 *     when it's off — driven purely by the tank's flag (see
 *     DungeonClearPartyTankValue / DungeonClearFollowTankTrigger).
 *
 *     This login hook is a zero-config convenience for bots present at login.
 *     The reliable universal path — and the ONLY one that reaches a self-bot
 *     created mid-session via `.playerbots bot self`, OR survives a
 *     ResetStrategies() (master/group change, talent change, instance entry) —
 *     is having the strategy in the playerbots default strategy set. The
 *     registrar above now injects "+dungeon clear" into that set in code (the
 *     four sPlayerbotAIConfig strategy strings), so no manual
 *     `AiPlayerbot.NonCombatStrategies = "+dungeon clear"` conf line is needed.
 *     The `.dc` slash command (DungeonClearCommand.cpp) needs neither, since it
 *     dispatches the action directly.
 */

#include "ScriptMgr.h"
#include "Ai/Dungeon/DungeonClear/Data/BossSpawnIndex.h"
#include "Ai/Dungeon/DungeonClear/Util/DcEncounterMask.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRouteRecorder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "AllCreatureScript.h"
#include "Cell.h"
#include "CellImpl.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"
#include "UnitScript.h"

#include "Playerbots.h"
#include "PlayerbotAI.h"

#include "AiObjectContextAccess.h"
#include "DcStrategyGate.h"

// Per-class context registries — each owns its own shared static lists.
#include "DKAiObjectContext.h"
#include "DruidAiObjectContext.h"
#include "HunterAiObjectContext.h"
#include "MageAiObjectContext.h"
#include "PaladinAiObjectContext.h"
#include "PriestAiObjectContext.h"
#include "RogueAiObjectContext.h"
#include "ShamanAiObjectContext.h"
#include "WarlockAiObjectContext.h"
#include "WarriorAiObjectContext.h"

#include "Util/DcSpectator.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/DungeonClearActionContext.h"
#include "Ai/Dungeon/DungeonClear/DungeonClearStrategyContext.h"
#include "Ai/Dungeon/DungeonClear/DungeonClearTriggerContext.h"
#include "Ai/Dungeon/DungeonClear/DungeonClearValueContext.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPathWorker.h"
#include "Ai/Dungeon/DungeonClear/Util/DcFirstContact.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPullBrake.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "TestRun/DcTestDriver.h"
#include "TestRun/DcTestDungeonRegistry.h"
#include "TestRun/DcTestPlanManager.h"
#include "TestRun/DcTestRunManager.h"

namespace
{
    // Completed-but-uncollected async path results older than this are swept
    // from DcPathWorker's mailbox. Far longer than a normal poll cadence (the
    // owning bot drains its result on its very next AI tick), so this only ever
    // catches results orphaned by a logout / dc-off mid-build.
    constexpr uint32 DC_ASYNC_PATH_RESULT_TTL_MS = 30 * 1000;

    // How often the dungeon-gate correctness sweep re-asserts the
    // strategy-install invariant across all bots (see DcStrategyGate). Well under
    // any human-perceptible activation delay — a tank still has to walk in and
    // `dc on` — and the per-bot cost is two HasStrategy reads, so this is cheap
    // even on a large realm. This is the net that restores the triggers after a
    // ResetStrategies() fired mid-dungeon (group/talent/LFG/reset), which is why
    // it MUST keep running and not depend on any entry event.
    constexpr uint32 DC_STRATEGY_GATE_SWEEP_MS = 3 * 1000;
}

namespace
{
    // Tortoise port: no per-class shared statics on this engine - the augmenter
// seam (AiContextAugment.h) hands every bot context, any class, to
// dc_access::AugmentContext, existing bots included. One registration call
// replaces the ten per-class appends.
}

// Spectator AI-mover window, opening half. Playerbots drives every bot's AI
// (including a self-bot's) from its PlayerScript::OnUpdate, and many
// bot actions are injected into core packet handlers (HandleUseItemOpcode,
// HandleGameObjectUseOpcode, …) that silently drop when the session's m_mover
// isn't the player — exactly the state spectate creates (the mover is the
// camera dummy). Net effect: a spectating warlock self-bot could not soulstone
// itself, use a healthstone, potion, or eat/drink. Fix: bracket playerbots'
// hook with a scoped swap of m_mover back to the player.
//
// ORDERING CONTRACT (what makes the bracket work): hooks run in script
// registration order (ScriptRegistry::EnabledHooks appends in AddScript
// order), and this tree adds module scripts during sScriptMgr.LoadScriptNames,
// well before InitPlayerbotsAtStartup() registers the playerbots hooks at the
// tail of SetInitialWorldSettings — so this Begin script, registered in
// AddSC_dungeon_clear_module(), fires BEFORE playerbots' UpdateAI. The End
// script is registered on the first world tick from
// DungeonClearRegistrarWorldScript, which lands it after every load-time
// script, closing the window AFTER playerbots. All three hooks run
// back-to-back on the player's map thread; client movement packets for the
// dummy are processed in the session phases, never inside the window.
class DungeonClearSpectatorMoverBeginScript : public PlayerScript
{
public:
    DungeonClearSpectatorMoverBeginScript()
        : PlayerScript("DungeonClearSpectatorMoverBeginScript", {
            PLAYERHOOK_ON_UPDATE
        }) {}

    void OnUpdate(Player* player, uint32 /*p_time*/) override
    {
        DcSpectator::BeginBotAiMoverWindow(player);
    }
};

// Closing half of the mover window — see DungeonClearSpectatorMoverBeginScript
// for the full story. NOT registered in AddSC_dungeon_clear_module(): it must
// be instantiated on the first world tick so it sorts after playerbots' hook.
//
// This is also where the spectator camera's own slow tick runs (pending-arrival
// start + follow-target re-resolution). It must sit AFTER the window closes,
// never between Begin and End: the tick can Stop a live camera, and tearing a
// possession down inside the bracket would leave the mover swap half-applied.
class DungeonClearSpectatorMoverEndScript : public PlayerScript
{
public:
    DungeonClearSpectatorMoverEndScript()
        : PlayerScript("DungeonClearSpectatorMoverEndScript", {
            PLAYERHOOK_ON_UPDATE
        }) {}

    void OnUpdate(Player* player, uint32 p_time) override
    {
        DcSpectator::EndBotAiMoverWindow(player);
        DcSpectator::Tick(player, p_time);
    }
};

class DungeonClearRegistrarWorldScript : public WorldScript
{
public:
    DungeonClearRegistrarWorldScript()
        : WorldScript("DungeonClearRegistrarWorldScript") {}

    void OnUpdate(uint32 /*diff*/) override
    {
        if (_registered)
            return;
        _registered = true;

        dc_access::RegisterDungeonClearContexts();

        // NOTE: the DC strategies are deliberately NOT injected into the
        // playerbots default strategy set anymore. They used to be appended to
        // the four sPlayerbotAIConfig strategy strings so they survived a
        // ResetStrategies() and rode along on every bot — but that made every bot
        // in the realm pay the DC trigger ladder (36 non-combat + 8 combat
        // triggers, checkInterval=1) every AI tick, dungeon-bound or not. The
        // install is now gated on Map::IsDungeon() via DcStrategyGate (see the
        // login / map-changed / world-sweep drivers below), which both removes the
        // overworld idle cost AND keeps the invariant ("any bot in a dungeon has
        // the triggers active") through every ResetStrategies path via the sweep.

        // Closing half of the spectator AI-mover window. Registered HERE (first
        // world tick, not AddSC) so its hook sorts after playerbots' UpdateAI
        // hook — see DungeonClearSpectatorMoverEndScript. Safe to register now:
        // map threads only run inside sMapMgr->Update, never concurrently with
        // this world-thread hook.
        new DungeonClearSpectatorMoverEndScript();

        // The dashboard's test-plan start form needs the dungeon catalogue +
        // caps; publish them once the config is final (first world tick).
        DcTestDungeonRegistry::WriteSidecar();

        LOG_INFO("module", "mod-dungeon-clear: registered DungeonClear contexts "
                           "(strategy/action/trigger/value) into all class "
                           "registries of mod-playerbots.");
    }

    // Stop + join the async pathfinding worker before maps unload (this fires
    // during the world shutdown sequence, ahead of OnAfterUnloadAllMaps). After
    // join, the worker's queue and mailbox are cleared, releasing every navmesh
    // shared_ptr it held — so no worker thread can touch a map being torn down.
    // Idempotent and safe even if the worker was never started.
    void OnShutdown() override
    {
        DcPathWorker::Instance().Stop();
    }

    // DcSettings caches each tunable's conf-resolved value per thread so the
    // per-tick reads during a run don't re-walk the config map (and don't re-log
    // sConfigMgr's "Missing property" warning every call). Drop those caches when
    // an admin reloads the config so an edited conf value takes effect live. The
    // `reload` flag is true only for `.reload config`; the initial load needs no
    // invalidation (nothing cached yet), but bumping then is harmless.
    void OnAfterConfigLoad(bool /*reload*/) override
    {
        DcSettings::InvalidateConfCache();
    }

private:
    bool _registered = false;
};

// Dungeon-gate drivers. The DC strategies ("dungeon clear" non-combat +
// "dungeon clear combat" combat) are installed on a bot iff it is on a
// dungeon/raid map — see DcStrategyGate for the why (removing the realm-wide
// idle cost) and the invariant guarantee. This script provides the two
// responsive drivers; the world-tick sweep (DungeonClearReaperScript) is the
// correctness net behind them.
//   * OnPlayerLogin    — a bot that logs in already inside an instance gets the
//                        strategies immediately.
//   * OnPlayerMapChanged — install on dungeon entry, strip on exit, within a
//                        frame of the map change.
// Reconcile() is idempotent and no-ops on real players and on already-compliant
// bots, and runs the dc-off teardown when it strips (so `enabled` can't survive
// to auto-resume on the next entry, and no follower keeps a stale MoveFollow).
class DungeonClearLoginPlayerScript : public PlayerScript
{
public:
    DungeonClearLoginPlayerScript()
        : PlayerScript("DungeonClearLoginPlayerScript", {
            PLAYERHOOK_ON_LOGIN,
            PLAYERHOOK_ON_MAP_CHANGED
        }) {}

    void OnLogin(Player* player) override
    {
        DcStrategyGate::Reconcile(player);
    }

    void OnMapChanged(Player* player) override
    {
        DcStrategyGate::Reconcile(player);
    }
};

// The pull walk-in's brake, wired to the combat flag itself.
//
// This is the one thing in the pull FSM that CANNOT be an AI action, because the
// whole defect is that an AI action arrives too late: the bot is still on the
// non-combat engine when its pack notices it, so a flip has to happen before the
// drag-back can run, and the MotionMaster walks the tank further into the formation
// for the whole of that gap. No player-level combat hook on this engine, but
// UnitScript::OnUnitEnterCombat fires from Unit::SetInCombatState right where
// UNIT_FLAG_IN_COMBAT is raised, edge-gated on !wasInCombat — still earlier
// than any tick, by construction. See DcPullBrake.h.
class DungeonClearPullBrakeScript : public UnitScript
{
public:
    DungeonClearPullBrakeScript()
        : UnitScript("DungeonClearPullBrakeScript", {
            UNITHOOK_ON_UNIT_ENTER_COMBAT
        }) {}

    void OnUnitEnterCombat(Unit* unit, Unit* enemy) override
    {
        // Unit-level hook: creature combat entries are none of the pull FSM's
        // business. (enemy may be null on some SetInCombatState paths;
        // DcFirstContact null-guards it, DcPullBrake never reads it.)
        Player* player = unit->ToPlayer();
        if (!player)
            return;

        DcPullBrake::OnEnterCombat(player);
        // Same hook, second reader: `enemy` was discarded here for as long as this
        // script has existed, and it is the only record anywhere of what STARTED a
        // fight. See DcFirstContact.h for what that cost.
        DcFirstContact::OnEnterCombat(player, enemy);
    }
};

// Spectator-camera teardown safety net. A leaked possession leaves the human
// controlling nothing (their mover is a despawning/orphaned dummy), and only we
// can clean it up — so every exit path below calls DcSpectator::Stop, which is
// idempotent and free to over-call. Note `dc off`/pause deliberately do NOT end
// spectate: the feature is independent of the DC run (useful for watching any
// bot activity, or nothing at all) — don't "fix" that by adding teardown there.
class DungeonClearSpectatorPlayerScript : public PlayerScript
{
public:
    DungeonClearSpectatorPlayerScript()
        : PlayerScript("DungeonClearSpectatorPlayerScript", {
            PLAYERHOOK_ON_BEFORE_TELEPORT,
            PLAYERHOOK_ON_PLAYER_JUST_DIED,
            PLAYERHOOK_ON_BEFORE_LOGOUT
        }) {}

    // Covers hearth, instance exit, summons, `.tele`, AND near-teleports —
    // a near-teleport during spectate would hang un-acked in playerbots
    // (its ack path does bot->GetMover()->ToPlayer(), null while the mover is
    // the camera dummy). Never vetoes the teleport.
    void OnBeforeTeleport(Player* player, uint32 /*mapid*/, float /*x*/,
                          float /*y*/, float /*z*/, float /*orientation*/) override
    {
        // This engine's hook is observe-only (void, no options/target params);
        // upstream's `return true` never vetoed anything either.
        DcSpectator::Stop(player);
    }

    // Body death while the camera is away: release control back so the normal
    // ghost/release flow works. Core may also break the charm itself — Stop is
    // idempotent either way.
    void OnPlayerJustDied(Player* player) override
    {
        DcSpectator::Stop(player);
    }

    // Despawn the dummy before the session goes away; never let the
    // TempSummon outlive its possessor.
    //
    // The second call is the crash guard for the OTHER side of a follow
    // camera: this hook fires for every logging-out player, bots included
    // (PlayerbotHolder::LogoutPlayerBot -> WorldSession::LogoutPlayer, whose
    // very first act is this hook), and a watched bot that is deleted without
    // clearing its viewers leaves their m_seer dangling. See
    // DcSpectator::NotifyWatchedLoggingOut.
    void OnBeforeLogout(Player* player) override
    {
        DcSpectator::Stop(player);
        DcSpectator::NotifyWatchedLoggingOut(player);
    }
};

// Spectator-camera death guard. THE crash fix for "body dies while spectating":
// spectate possesses the camera dummy via a direct SetCharmedBy(player,
// CHARM_TYPE_POSSESS) — an *aura-less* possession. When the player body (the
// charmer) dies, Unit::setDeathState(JustDied) -> Unit::RemoveAllControlled ->
// Player::StopCastingCharm runs FIRST, deep inside Unit::Kill and long before
// Player::KillPlayer fires OnPlayerJustDied. StopCastingCharm only knows how to
// undo possession by removing the possess AURA (Player.cpp RemoveAurasByType
// SPELL_AURA_MOD_POSSESS); with no such aura, GetCharmGUID() stays set and the
// function hits its ABORT() — a hard server crash. The existing OnPlayerJustDied
// -> Stop net cannot prevent it; it runs too late.
//
// Fix: catch the lethal blow at the OnDamage chokepoint (Unit::DealDamage, fired
// with the final post-absorb damage and the victim's health NOT yet reduced —
// well before the `health <= damage` Kill at Unit.cpp). If a spectating player's
// body is about to die, tear the possession down cleanly here. DcSpectator::Stop
// runs the proper RemoveCharmedBy, so by the time setDeathState reaches
// StopCastingCharm there is no charm left to choke on, and the body dies into the
// normal ghost/release flow. Stop is idempotent; the OnPlayerJustDied/teardown
// nets stay as backstops for the rare non-DealDamage instakill paths.
class DungeonClearSpectatorDeathGuardScript : public UnitScript
{
public:
    DungeonClearSpectatorDeathGuardScript()
        : UnitScript("DungeonClearSpectatorDeathGuardScript", {
            UNITHOOK_ON_DAMAGE
        }) {}

    void OnDamage(Unit* /*attacker*/, Unit* victim, uint32& damage) override
    {
        if (!victim || !victim->IsPlayer())
            return;
        // Only a lethal blow matters — a survivable hit must not eject the human
        // from spectate. health is still the pre-damage value at this point.
        if (damage < victim->GetHealth())
            return;

        Player* player = victim->ToPlayer();
        if (DcSpectator::IsActive(player))
            DcSpectator::Stop(player);
    }
};

// Drives the orphaned-follow reaper once per world tick. A non-tank DC follower
// installs a persistent MoveFollow generator to chase the tank; its own
// follow-tank action tears that down when the DC tank goes away — but only while
// the follower's PlayerbotAI is still ticking. A self-bot that leaves bot mode
// (`.playerbots bot self` off) has its PlayerbotAI deleted outright, so the
// teardown never runs and the now-human player stays glued to the tank. The
// reaper detects that orphaned generator and clears it, returning movement
// control to the player. Upstream ran this off playerbots' OnPlayerbotUpdate;
// here WorldScript::OnUpdate is the equivalent global tick — it fires
// regardless of whether the affected player still has an AI (a world tick, not
// a per-bot one), which is exactly what we need since the player we must fix
// no longer has a bot AI.
class DungeonClearReaperScript : public WorldScript
{
public:
    DungeonClearReaperScript() : WorldScript("DungeonClearReaperScript", { WORLDHOOK_ON_UPDATE }) {}

    void OnUpdate(uint32 diff) override
    {
        DcFollowerLifecycle::ReapOrphanedFollows();
        // Authoritative advanced-pull passive teardown: release any follower DC
        // put passive once its leader is no longer mid-pull (released / dc off /
        // paused / death / gone), and trip the camp-safety valve for a held,
        // passive follower taking damage. Runs on the global tick so it fires even
        // for a follower whose own engines are passive-locked in combat.
        DcFollowerLifecycle::ReapStrandedPassives();
        // Drop async path results that were never collected (the bot logged out
        // or toggled dc off before polling). Bounds the mailbox; cheap no-op
        // when empty.
        DcPathWorker::Instance().Sweep(DC_ASYNC_PATH_RESULT_TTL_MS);
        // Drop encounter masks of instances that no longer run (throttled).
        DcEncounterMask::Sweep();
        // Event-driven status: recompute each clearing tank's status and emit a
        // STATUS/BOSS packet only on change (replaces the addon's old poll).
        // Internally throttled; a cheap no-op when no tank is clearing. Runs on
        // the global tick (not a per-bot one) so it keeps firing through boss
        // fights, when the bot's non-combat strategy engine is dormant.
        DcStatusPublisher::TickStatusPushes(diff);

        // `.dc test` harness state machine (spawn -> gear -> group -> teleport
        // -> dc on -> watchdogs -> record). Cheap no-op while no test run is
        // active; global tick for the same reason as the status pusher.
        // Headless-driver login poll first (a console start may be waiting on
        // it), then the plan scheduler (a launch it makes this tick enters the
        // run manager's tick loop immediately below).
        DcTestDriver::Tick();
        DcTestPlanManager::Instance().Tick(diff);
        DcTestRunManager::Instance().Tick(diff);

        // Dungeon-gate correctness net: re-assert "DC strategies installed iff in
        // a dungeon" across all bots on a throttled cadence. The login and
        // map-changed hooks apply/strip responsively on entry/exit; this sweep
        // covers the cases those hooks can't see — a ResetStrategies() that wiped
        // the strategy WHILE the bot is inside an instance (group/talent/LFG/
        // master change, `.playerbots reset`), and self-bots created in-place via
        // `.playerbots bot self`. Without it, the requirement "any bot in a
        // dungeon has the triggers active" would not hold across resets.
        _gateSweepAccumMs += diff;
        if (_gateSweepAccumMs >= DC_STRATEGY_GATE_SWEEP_MS)
        {
            _gateSweepAccumMs = 0;
            DcStrategyGate::ReconcileAllBots();

            // Route recorder: sample every dc leader that is driving a
            // dungeon. Cheap (a distance check, a push_back at most every
            // ~4yd of travel) and strictly passive - it never influences the
            // run it observes.
            for (auto const& kv : sObjectAccessor.GetPlayers())
            {
                Player* p = kv.second;
                if (!p)
                    continue;
                PlayerbotAI* pai = GET_PLAYERBOT_AI(p);
                if (!pai)
                    continue;
                if (!DcRun::Of(pai).enabled)
                    continue;
                DcRouteRecorder::Sample(p);
            }
        }
    }

private:
    uint32 _gateSweepAccumMs = 0;
};

// WORKAROUND for an upstream Zul'Farrak core-script typo (fixed on branch
// fix/zf-sezziz-summon-coords): Shadowpriest Sezz'ziz's summon table lists
// several add coords as x=895 instead of 1895, so during his fight he summons
// Sandfury Acolytes/Zealots ~1000yd OUTSIDE the temple. There they are
// unreachable and never die, holding the whole party in combat indefinitely —
// which freezes the DungeonClear temple event, whose end gossips (open the door,
// then fight Bly) run on the NON-combat engine and so never get a tick. We can't
// fix that in SQL (the coords are hardcoded in C++), so until the core fix is
// deployed this hook despawns the stray adds the instant they spawn, letting
// combat end and the event finish.
//
// Surgical: only Sandfury Acolyte/Zealot SUMMONS on the Zul'Farrak map that
// appear far WEST of the temple (x < 1500; the temple sits at x~1873-1902, so no
// legitimate creature is there). The pyramid-wave summons of the same entries
// spawn at the correct x (>1873) and are untouched. Despawn is deferred a beat so
// it can't dangle the pointer the summon caller uses right after SummonCreature
// (Sezz'ziz calls AttackStart on the fresh add); 1000yd away, the few-hundred-ms
// of phantom threat before despawn is harmless.
class DungeonClearZfStraySummonScript : public AllCreatureScript
{
public:
    DungeonClearZfStraySummonScript() : AllCreatureScript("DungeonClearZfStraySummonScript") {}

    void OnCreatureAddWorld(Creature* creature) override
    {
        if (!creature || creature->GetMapId() != 209 /*Zul'Farrak*/)
            return;
        uint32 const entry = creature->GetEntry();
        if (entry != 8876 /*Sandfury Acolyte*/ && entry != 8877 /*Sandfury Zealot*/)
            return;
        if (!creature->IsSummon() || creature->GetPositionX() >= 1500.0f)
            return;  // a real wave spawn / static trash at the temple — leave it

        LOG_INFO("playerbots.dungeonclear",
                 "[DC] despawning stray Sezz'ziz summon {} (entry {}) at x={:.1f} "
                 "(ZF coord-typo workaround)",
                 creature->GetObjectGuid().ToString(), entry, creature->GetPositionX());
        creature->DespawnOrUnsummon(200);
    }
};

// WORKAROUND for the Sunken Temple (map 109) Shade of Eranikus event holding the
// party in combat forever — the same class of bug as the Zul'Farrak stray-summon
// hook above, and likewise unfixable for us in core. instance_sunken_temple.cpp's
// SetData(DATA_ERANIKUS_FIGHT) throws EVERY loaded dragonkin in the temple into
// combat with the whole zone via Creature::SetInCombatWithZone(). The scattered
// ones that can't path to the party never reach it and never trip their home
// leash, so once the Shade is dead they keep a combat reference on every party
// member indefinitely — killing the adds that DID reach the party never clears the
// stranded ones, and the DungeonClear run then freezes on its "party not ready /
// resting" gate. The core instance script never resets these minions on the
// Shade's own death (its OnUnitDeath handles only Jammal'an), and we can't patch
// core, so do that cleanup here: when the Shade of Eranikus dies, evade every
// still-stranded awakened dragonkin so the party drops combat and the run
// continues. Scoped strictly to the Shade's death on map 109; universal (helps
// real players too), not gated on DungeonClear.
// Feeds DcEncounterMask: a dungeon creature death whose entry is in the boss
// index sets that boss's mask bit for the instance. This IS the kill signal
// every mask reader consumes - without it no boss ever counts as killed on
// this core (no KillRewarder/DungeonEncounter path here).
class DungeonClearEncounterMaskScript : public UnitScript
{
public:
    DungeonClearEncounterMaskScript()
        : UnitScript("DungeonClearEncounterMaskScript", {
            UNITHOOK_ON_UNIT_DEATH
        }) {}

    void OnUnitDeath(Unit* unit, Unit* /*killer*/) override
    {
        if (!unit || !unit->IsCreature())
            return;
        Map* map = unit->FindMap();
        if (!map || !map->IsDungeon())
            return;

        for (DungeonBossInfo const& info :
             BossSpawnIndex::Get(map->GetId(), DUNGEON_DIFFICULTY_NORMAL))
        {
            if (info.entry == unit->GetEntry())
            {
                DcEncounterMask::OnBossKilled(map, info.encounterIndex);
                // Close the recorder's leg for this boss: the path the party
                // just walked becomes a registered anchor route (repo content
                // under modules/mod-dungeon-clear/routes/) instead of being
                // recomputed from the navmesh on every future run.
                DcRouteRecorder::OnBossKilled(map, info.entry, info.name);
                LOG_INFO("playerbots.dungeonclear",
                         "[DC] boss down: {} (entry {}) — encounter bit {} set for instance {}",
                         info.name, info.entry, info.encounterIndex, map->GetInstanceId());
                break;
            }
        }
    }
};

class DungeonClearEranikusCombatReleaseScript : public UnitScript
{
public:
    DungeonClearEranikusCombatReleaseScript()
        : UnitScript("DungeonClearEranikusCombatReleaseScript", {
            UNITHOOK_ON_UNIT_DEATH
        }) {}

    void OnUnitDeath(Unit* unit, Unit* /*killer*/) override
    {
        constexpr uint32 NPC_SHADE_OF_ERANIKUS = 5709;
        constexpr uint32 MAP_SUNKEN_TEMPLE = 109;
        if (!unit || !unit->IsCreature() || unit->GetEntry() != NPC_SHADE_OF_ERANIKUS)
            return;
        if (unit->GetMapId() != MAP_SUNKEN_TEMPLE)
            return;

        // Sweep the temple around the Shade's corpse for the awakened dragonkin.
        // The radius spans the inner sanctum plus the surrounding ring where the
        // SetInCombatWithZone'd dragonkin patrol or strand; the grid visitor is
        // 2D-cell based so it returns them across the event's vertical levels too.
        // One-shot on a boss death, so the broad sweep is cheap.
        constexpr float SWEEP = 400.0f;
        std::list<Creature*> nearby;
        Acore::AnyUnitInObjectRangeCheck check(unit, SWEEP);
        Acore::CreatureListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(unit, nearby, check);
        Cell::VisitObjects(unit, searcher, SWEEP);

        uint32 released = 0;
        for (Creature* c : nearby)
        {
            if (!c || c == unit || !c->IsAlive() || !c->IsInCombat())
                continue;
            if (c->GetCreatureType() != CREATURE_TYPE_DRAGONKIN)
                continue;
            if (c->GetEntry() == NPC_SHADE_OF_ERANIKUS)
                continue;  // the Shade itself / any second copy — never our target
            if (CreatureAI* ai = c->AI())
            {
                ai->EnterEvadeMode();
                ++released;
            }
        }

        if (released)
            LOG_INFO("playerbots.dungeonclear",
                     "[DC] Shade of Eranikus died — evaded {} stranded awakened "
                     "dragonkin to release the party from combat "
                     "(SetInCombatWithZone workaround).", released);
    }
};

void AddSC_dungeon_clear_module()
{
    new DungeonClearRegistrarWorldScript();
    new DungeonClearLoginPlayerScript();
    new DungeonClearPullBrakeScript();
    // Opening half only — the End script registers on the first world tick so
    // it sorts after playerbots' AI-update hook (see the ordering contract).
    new DungeonClearSpectatorMoverBeginScript();
    new DungeonClearSpectatorPlayerScript();
    new DungeonClearSpectatorDeathGuardScript();
    new DungeonClearReaperScript();
    new DungeonClearZfStraySummonScript();
    new DungeonClearEranikusCombatReleaseScript();
    new DungeonClearEncounterMaskScript();

    // `.dc test` harness: receive each changed STATUS frame for the monitored
    // tank (addon messages only reach real players in the bot's group, and the
    // test-run GM deliberately isn't one). Registered here, once, before any
    // run can exist.
    DcStatusPublisher::SetStatusObserver(
        [](ObjectGuid tank, std::string const& payload)
        {
            DcTestRunManager::Instance().OnStatusPayload(tank, payload);
        });
}
