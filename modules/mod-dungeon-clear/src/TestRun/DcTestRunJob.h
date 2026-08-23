/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTRUNJOB_H
#define _PLAYERBOT_DCTESTRUNJOB_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ObjectGuid.h"
#include "TestRun/DcTestDungeonRegistry.h"
#include "TestRun/DcTestGearTiers.h"
#include "TestRun/DcTestAreaTriggers.h"
#include "TestRun/DcTestRunLiveJson.h"
#include "TestRun/DcTestRunRecord.h"
#include "TestRun/DcTestRunVerdict.h"
#include "TestRun/DcWipeContext.h"

class Player;

// One `.dc test` run in flight: a single 5-bot party driven through the full
// state machine and watched to a verdict. Extracted verbatim from the old
// singleton DcTestRunManager so any number of runs can execute at once — the
// manager (DcTestRunManager) is now just a registry of these jobs.
//
//   SpawningBots -> Provisioning -> Grouping -> Teleporting
//        -> Starting -> Monitoring -> TearingDown -> (JSONL record) -> Done()
//
// Success/failure is decided by the pure kernel in DcTestRunVerdict.h fed from
// two observers plus per-tick reads of the leader's run state. The observers
// may fire from map-update threads, so the fields they write sit behind this
// job's own _obsMutex (per-job, so N runs' observers never contend). All
// cross-tick bookkeeping is GUIDs + POD (never a stored Player*); the manager
// resolves the map thread's observer callback to the right job by tank guid.
class DcTestRunJob
{
public:
    // Factory: today's Start body after registry/GM validation. Rolls a random
    // comp from `seed` (DcTestComp::BuildComp), then picks one offline
    // addclass-pool character per slot (skipping reservedGuids, the guids
    // already claimed by other live runs; substituting an alternative class of
    // the same role when the drawn class has no free pool char), starts their
    // async logins, logs TESTRUN START and enters SpawningBots. Returns nullptr
    // + err only when a whole role can't be filled from the pool. The seed is
    // stored in the record so the comp can be replayed.
    //
    // `gear` is the run's own gear ceiling (DcTestGearTiers::Spec); a default
    // Spec means "whatever AiPlayerbot.AutoGearScoreLimit / AutoGearQualityLimit
    // say", which is what every run did before the option existed. It is
    // resolved against the conf ONCE here, so a mid-run `.reload config` cannot
    // change what a run was geared to.
    static std::unique_ptr<DcTestRunJob> Create(Player* gm, DcTestDungeonRegistry::Row const& row,
                                                 uint32 levelOverride, uint32 seed, bool heroic,
                                                 DcTestGearTiers::Spec const& gear,
                                                 std::unordered_set<ObjectGuid> const& reservedGuids,
                                                 std::string const& planId, std::string* err);

    // One hand-picked party slot: a real player character and the role the human
    // marked it for. Resolved (name -> guid, faction/liveness checked) by
    // DcTestRunManager::StartRoster before it gets here.
    struct RosterEntry
    {
        ObjectGuid guid;
        std::string name;   // as typed, for log/refusal messages
        char const* role;   // "tank" | "heal" | "dps"
    };

    // Factory for a roster run: the five slots are given, not drawn. Differs
    // from Create in three ways, all because these are somebody's real
    // characters rather than disposable pool bots:
    //
    //   * login goes through the MASTERLESS playerbots path
    //     (sRandomPlayerbotMgr.AddPlayerBot(guid, 0)) — AddPlayerBot's ownership
    //     gate only clears for same-account / same-guild / addclass-pool / linked
    //     characters, none of which a hand-picked party satisfies, and the
    //     masterless branch (isRndbot) skips the gate outright. The GM is still
    //     installed as playerbots master at Grouping, so the HasRealPlayerMaster
    //     fast path is unaffected;
    //   * provisioning does NOT roll the factory — no Randomize, no spec force,
    //     no re-gear, no glyphs, no pet reroll. The characters fight as they are;
    //   * `level` comes from the characters, so there is no level argument.
    //
    // No seed: a roster IS the comp. Returns nullptr + err only if the roster is
    // not exactly the expected party size (the manager has already validated the
    // characters themselves).
    static std::unique_ptr<DcTestRunJob> CreateFromRoster(Player* gm,
                                                          DcTestDungeonRegistry::Row const& row,
                                                          bool heroic,
                                                          std::vector<RosterEntry> const& roster,
                                                          std::string const& planId,
                                                          std::string* err);

    // Drive from the world thread. provisionBudget is shared across all runs
    // this tick: the one heavyweight PlayerbotFactory::Randomize roll a run
    // performs consumes it (sets it false), so at most one factory roll runs
    // per world tick across the whole registry.
    void Tick(uint32 diff, bool& provisionBudget);

    bool Done() const { return _done; }
    bool IsMonitoring() const { return _stage.load() == Stage::Monitoring; }

    // Stop while Monitoring: async verdict path (the monitor tick picks up the
    // abort flag and runs Finish).
    void RequestAbort(std::string const& reason);
    // Stop during setup: synchronous FailSetup -> Teardown.
    void AbortSetup(std::string const& reason);

    // Observer forwards from the manager (already matched by tank guid; the
    // manager holds its registry lock across these calls).
    void OnRunDisabled(std::string const& reason);
    void OnStatusPayload(std::string const& payload);

    ObjectGuid TankGuid() const { return _tankGuid; }
    ObjectGuid GmGuid() const { return _gmGuid; }
    std::string const& RunId() const { return _record.runId; }
    std::string const& PlanId() const { return _record.planId; }
    std::string const& DungeonToken() const { return _dungeonToken; }

    // The finished record, for the manager's erase pass to distill a plan
    // child's outcome. Only meaningful once Done() — the record is fully
    // populated (and the observers gated off) by Teardown.
    DcTestRunRecord::Record const& RecordData() const { return _record; }

    // One-line status for `.dc test status` and the start message.
    std::string StatusLine() const;

    // Snapshot for the live-status file (takes _obsMutex).
    DcTestRunLive::RunSnapshot Snapshot() const;

    // The 5 slot guids, for the manager's cross-run reservation set.
    std::vector<ObjectGuid> BotGuids() const;

private:
    enum class Stage : uint8_t
    {
        SpawningBots,
        Provisioning,
        Grouping,
        Teleporting,
        Starting,
        Monitoring,
        TearingDown
    };

    struct Slot
    {
        uint8_t classId = 0;
        char const* specName = "";
        char const* fallbackSpec = "";
        char const* role = "";
        ObjectGuid guid;
        bool provisioned = false;
        // Roster runs only: the name as the human typed it, so a slot can be
        // named in a refusal before its character has ever been resolved to a
        // Player (the pool path reads the name off the resolved bot instead).
        std::string rosterName;
        // The character's guild as of BEFORE the run logged it in, so an
        // unwanted join can be detected and undone. See UndoUnwantedGuild.
        uint32 guildBefore = 0;
    };

    // Boss-roster snapshot taken at Starting so mask deltas can be named
    // without touching live values from the monitor loop.
    struct BossRef
    {
        uint32 entry = 0;
        uint32 encounterIndex = 0;
        std::string name;
        bool isBoss = true;
    };

    DcTestRunJob() = default;

    // Shared prologue of both factories: identity, watchdog limits, record
    // header. Everything that does not depend on how the party was chosen.
    void InitIdentity(Player* gm, DcTestDungeonRegistry::Row const& row, uint32 level,
                      bool heroic, uint32 seed, std::string const& planId);

    void EnterStage(Stage s);
    static char const* StageName(Stage s);

    void TickSpawning();
    void TickProvisioning(bool& provisionBudget);
    // Roster variant: read the characters out into the record and leave them
    // otherwise untouched. No factory roll, so no per-tick budget.
    void TickProvisioningRoster();
    // Undo the guild stock playerbots silently joins a guildless bot to at login
    // (RandomPlayerbotMgr::OnBotLoginInternal -> PlayerbotFactory::InitGuild).
    // That path has no random-bot gate, so it fires for a hand-picked real
    // character too — which must come out of a run in the guild it went in with.
    void UndoUnwantedGuild(Player* bot, Slot const& slot) const;
    // Unbind every member from this map so the run always gets a FRESH instance.
    // Both difficulties, because a normal-difficulty bind is just as capable of
    // dragging the party into a half-cleared save — and a stale bind poisons the
    // GetCompletedEncounterMask baseline the verdict counts bosses from, which
    // corrupts the test result rather than merely inconveniencing a character.
    void UnbindFromMap() const;
    // Refuse before teleporting when a member's account has burned its
    // AccountInstancesPerHour budget. Without this the core silently refuses the
    // transfer and the run dies as "party did not arrive at the dungeon
    // entrance" — a false diagnosis, and the worst kind in a regression harness.
    // Returns false and calls FailSetup naming the character.
    bool CheckInstanceBudget();
    void TickGrouping();
    void TickTeleporting();
    void TickStarting();
    void TickMonitoring(uint32 dt);
    // Geo fence + altitude sanity over all party slots. Shared by
    // TickStarting AND TickMonitoring: b75 accidentally left it only in
    // TickStarting, so a monitoring-phase wanderer (Westfall grind teleport)
    // was never fenced back.
    void SweepPartyGeometry();
    void TrackDeaths(Player* tank);
    void TrackEngagement(Player* tank);
    void TrackPulls(Player* tank);
    void ClosePull(bool wipedHere = false);
    DcTestRun::Engagement DeathBlame() const;
    static bool AnyMemberDead(Player* tank);

    void FailSetup(std::string const& why);
    void Finish(DcTestRun::Verdict verdict, std::string const& failReason);
    void Teardown();
    // Put the party back the way the run found it: alive, and out of the
    // instance. Resurrects every dead member and sends the party to its bind
    // point (where its own hearthstone would land it). Must run BEFORE
    // LogoutBots — it is the logout that would otherwise save a wiped party as
    // five ghosts at the instance graveyard.
    void ReviveAndSendHome();
    void LogoutBots(Player* gm);

    // Re-install the GM as every member's playerbots master, every monitor tick.
    //
    // Grouping installs it once, which was believed to be enough. It is not: live
    // logs (tr-20260727-185517-*) show the LEADER TANK running the whole clear on
    // the no-real-player-master path while its own followers stayed on the fast
    // one. The arithmetic is unambiguous, because PlayerbotAIBase::YieldThread
    // stamps a deterministic per-bot offset (guid % 201) on top of
    // GetReactDelay():
    //
    //   follower Cairion (guid 350, offset 149): ticks 250ms apart
    //       -> GetReactDelay() == 100  == reactDelay          (real master)
    //   tank Tiodo (guid 541, offset 139): ticks 640ms apart in combat,
    //   1100-3100ms apart out of combat
    //       -> GetReactDelay() == 500  == reactDelay * 5      (no real master, combat)
    //       -> GetReactDelay() == 1000-3000 == reactDelay * urand(10,30)
    //                                                          (no real master, idle)
    //
    // So the tank was THINKING ONCE EVERY ONE TO THREE SECONDS for the entire
    // out-of-combat pull sequence — the commit walk, the Forming dwell, the tag,
    // the creep. Every phase transition cost a second or more of standing still,
    // which is the whole "pauses way too long outside aggro range, then takes a
    // couple of steps, then turns" report. No amount of tuning inside the pull
    // FSM can fix a control loop sampled at 0.3-1 Hz.
    //
    // WHAT CLEARS IT (confirmed from the repair diagnostic, tr-20260727-192611-*:
    // every run named its own _slots[0] prot tank, 3-5s into monitoring, master
    // "cleared"): stock's ResetAiAction, which is wired to SMSG_GROUP_LIST and
    // ends with
    //
    //     if (Player* master = botAI->GetMaster())
    //         if (bot->GetGroup() && (!master->GetGroup() ||
    //                                 master->GetGroup() != bot->GetGroup()))
    //             botAI->SetMaster(nullptr);
    //
    // i.e. "a master who is not in my group is not my master". The whole S1062
    // design is a GM master who is deliberately NOT a party member, so that rule
    // is aimed squarely at us. It hits the LEADER ONLY because the action first
    // bails unless the packet reports zero other members, and the only member ever
    // alone in the group is the tank — Group::Create(tank) sends it a solo
    // GROUP_LIST before the other four are added. The bot processes that queued
    // packet a tick or two after Grouping has already installed the master, so the
    // install is overwritten from behind.
    //
    // Once lost it is ABSORBING: FindNewMaster only accepts a real player or a
    // self-mastered leader, and an all-bot party has neither, so nothing ever
    // restores it and the leader runs the entire clear on the slow path.
    //
    // Re-asserting is the fix rather than a workaround: the trigger is stock
    // behaviour we do not edit (module-side rule), it is level-triggered, and
    // SetMaster is a plain setter — a handful of pointer writes per second per
    // run. The first repair per run is still logged so a NEW clearing path would
    // announce itself (a non-tank name, or a repair long after setup) instead of
    // silently costing 10-30x think latency again.
    void ReassertMaster();

    Player* FindGm() const;
    Player* FindTank() const;

    // --- run identity (set in Create) --------------------------------------
    std::atomic<Stage> _stage{Stage::SpawningBots};
    bool _done = false;             // world-thread only; manager erases on true
    std::string _dungeonToken;
    uint32 _mapId = 0;
    float _x = 0.f, _y = 0.f, _z = 0.f, _o = 0.f;
    uint32 _level = 0;
    bool _heroic = false;  // run at DUNGEON_DIFFICULTY_HEROIC
    // Gear ceiling this run's bots are rolled to, already resolved against the
    // playerbots conf (ilvl 0 = no cap, quality 0 = factory default). Frozen at
    // Create so the run is reproducible from its own record. Unused on roster
    // runs — real characters are never re-geared.
    DcTestGearTiers::Resolved _gear;
    // Hand-picked real player characters (`party=`) rather than pool bots. Gates
    // every path that would mutate what a character IS, and switches the login
    // to the masterless holder. See CreateFromRoster.
    bool _realChars = false;
    ObjectGuid _gmGuid;
    ObjectGuid _tankGuid;
    std::vector<Slot> _slots;
    DcTestRunRecord::Record _record;
    DcTestRun::Limits _limits;

    // --- stage bookkeeping --------------------------------------------------
    uint32 _stageMs = 0;      // time in current stage (stage timeouts)
    uint32 _totalMs = 0;      // time since Create (timeline t offsets)
    uint32 _monitorMs = 0;    // time in Monitoring (kernel elapsed)
    uint32 _monitorAccumMs = 0;
    std::size_t _provisionIdx = 0;
    bool _groupFormed = false;
    // Teleporting goes in two waves — leader first, then the rest — so the
    // party cannot be split across instance copies. See TickTeleporting.
    bool _teleportIssued = false;      // wave 1 (the leader) has been sent
    bool _followersTeleported = false; // wave 2 (everybody else) has been sent
    uint32 _destInstanceId = 0;        // the copy the leader landed in; 0 until it does
    bool _dcOnIssued = false;
    // One-shot so ReassertMaster's diagnostic names the first repair per run
    // instead of once a second forever.
    bool _masterRepairLogged = false;

    // --- monitoring state ----------------------------------------------------
    // Stands in for the game clients this party does not have, so scripted
    // areatriggers still fire during a headless run. Armed on entry to
    // Monitoring, ticked unthrottled from Tick(). See DcTestAreaTriggers.h for
    // why the harness has to do this at all.
    DcTestAreaTriggers _areaTriggers;
    std::vector<BossRef> _roster;
    uint32 _lastMask = 0;
    std::size_t _lastAnchors = 0;
    uint32 _sinceProgressMs = 0;

    // One-shot live freeze dump, fired partway INTO the no-progress window
    // rather than at teardown. Teardown is too late for the question it answers:
    // by then the fight is over, and a holder that leashed home, despawned or
    // evaded in the intervening minutes is a different unit (or no unit) from
    // the one that actually wedged the run. This samples while the party is
    // still standing in the freeze. Re-armed whenever the run makes progress, so
    // a run that stalls twice reports both.
    bool _frozenDumpLogged = false;

    // Closing-distance progress: the nearest the party has ever been to the
    // CURRENT target, and which target that was. Re-armed on target change, so
    // each anchor gets its own approach measured from scratch.
    uint32 _bestDistEntry = 0;
    float _bestDist = -1.f;

    // Last tick's combat picture. Progress is a CHANGE in these, never a value:
    // "in combat" held true is the signature of the phantom-combat ghost-flag
    // deadlock, so treating combat itself as progress would make that bug
    // undetectable by construction. A real fight moves the victim's health
    // every sample; a wedged one moves nothing.
    bool _lastInCombat = false;
    ObjectGuid _lastVictim;
    uint32 _lastVictimHpPct = 0;
    // Party-wide combat fingerprint (see the progress-evidence block): the
    // tank alone parked out of combat while the party fought made healthy
    // runs read as frozen.
    uint32 _lastPartyCombatSig = 0;

    uint32 _pausedForMs = 0;
    uint32 _stalledForMs = 0;
    uint32 _wipedForMs = 0;
    // Who was still standing the tick before the wipe latched — the last dead
    // member's name is the one worth naming in the fail reason.
    std::string _lastAliveMember;

    // What the party is fighting, latched so a wipe can be attributed to it.
    // TrackEngagement gathers the sample off live players; the fold rule
    // (including "a wipe never clears the latch") is DcTestRun::UpdateEngagement.
    DcTestRun::Engagement _engaged;

    // Alive/dead as of the previous monitor sample, per member, so TrackDeaths
    // fires on the alive->dead EDGE (and again after a rez, which is the point:
    // "died three times to the same pack" is the diagnosis). A guid absent from
    // the map is being seeded, never reported — the first sample must not file
    // a death for someone who was already down when monitoring opened.
    std::unordered_map<ObjectGuid, bool> _aliveLast;
    // The engagement latched at the most recent death. The live latch clears the
    // moment the survivors drop combat, so a run that ends on a failed rez has
    // nothing left to blame; this keeps the killer alive for the post-mortem.
    DcTestRun::Engagement _lastDeathEngaged;

    // Pull observation in flight. `_pullSeq` mirrors the leader's
    // DcPullContext::decisionSeq: a change is the edge that closes the previous
    // pull's record and opens the next, which is why the counter (and not the
    // target GUID) is what the governor exposes — a re-pull of the same pack
    // after a fizzle is a new pull and must not be folded into the old one.
    std::uint32_t _pullSeq = 0;
    bool _pullOpen = false;
    DcTestRunRecord::PullEntry _pullEntry;

    bool _wasPaused = false;

    // --- observer-written (any thread) --------------------------------------
    mutable std::mutex _obsMutex;
    bool _abortRequested = false;
    std::string _abortReason;
    bool _disableFired = false;
    bool _disableAllCleared = false;
    std::string _disableReason;
    std::string _lastStatusState;
};

#endif  // _PLAYERBOT_DCTESTRUNJOB_H
