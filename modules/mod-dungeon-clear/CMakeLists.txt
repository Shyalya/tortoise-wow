# mod-dungeon-clear — AzerothCore module build registration.
#
# Sources are auto-collected from src/ (recursively) by AzerothCore's module
# system; AC_ADD_SCRIPT registers the script-loader path. The module compiles
# into the shared `modules` static library alongside mod-playerbots, so its
# subclasses of the playerbots engine classes link directly.
#
# REQUIRES mod-playerbots to be present and enabled — this is a playerbots
# extension, not a standalone module.
#
# NOTE: sources are GLOBed at configure time (no CONFIGURE_DEPENDS), so adding
# or removing a .cpp requires CMake to re-run. Editing this file bumps its
# mtime, which CMake treats as a configure dependency and reconfigures on the
# next build — the reliable way to force a re-glob after the file set changes.

AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src")

AC_ADD_CONFIG_FILE("${CMAKE_CURRENT_LIST_DIR}/conf/mod_dungeon_clear.conf.dist")

# New files added: Value/DungeonClearGroundHazardsValue.{h,cpp} (live
#   persistent-area-aura ground pools — DynamicObjects, not creatures — near the
#   bot; the DcHazard half that covers Scholomance's Cloud of Disease),
# Value/DungeonClearTrapHazardsValue.{h,cpp} (live GAMEOBJECT_TYPE_TRAP hazards
#   near the bot — the third resolver, for objects that are neither units nor
#   dynamic objects; covers the Shattered Halls flame-gauntlet Blaze 181915),
# Util/DcFirstContact.{h,cpp} (records the enemy that STARTED
#   each fight, off the OnPlayerEnterCombat `enemy` parameter DC had been
#   discarding — the pull record only covers fights DC pulled),
#   DungeonClearDispatch.h, DungeonClearAddonHook.cpp,
# Action/BetterLootRollAction.{h,cpp} (self-bot no-auto-roll override),
# Data/DungeonWingRegistry.{h,cpp}, Action/StayDeadAction.{h,cpp},
# Util/CorridorCenter.{h,cpp}, Value/DungeonClearLiveBossValue.{h,cpp},
# Util/DcPathWorker.{h,cpp} (async pathfinding worker),
# Util/DungeonClearGeometry.{h,cpp} (shared LOS/distance primitives),
# Settings/DcSettingsRegistry.h + Settings/DcSettings.{h,cpp}
#   (per-run settings registry + override accessor)
# Value/DungeonClearPullModeCurrentValue.{h,cpp}
#   (order-independent live pull-mode / Dynamic-verdict accessor)
# Util/SwimPathfinder.{h,cpp}
#   (Tier A greedy 3D swim navigator for submerged tunnels; driven via a raw
#    3D escort spline off the new DungeonClearSwimState value)
# Util/DungeonClearTuning.h
#   (single home for shared DC_* tuning constants — constant-centralization)
# Util/DcEngageGeometry.{h,cpp}
#   (DungeonClearUtil decomposition: ranges/doors/reachability/corridor unit)
# Util/DcPartyState.{h,cpp}
#   (DungeonClearUtil decomposition: readiness/rest gate/status text unit)
# Util/DcLootPolicy.{h,cpp}
#   (DungeonClearUtil decomposition: loot quality floor/skip/give-up unit)
# Util/DcLeaderSignal.{h,cpp}
#   (DungeonClearUtil decomposition: cross-bot leader/pull broadcast unit)
# Util/DcFollowerLifecycle.{h,cpp}
#   (DungeonClearUtil decomposition: follow/passive marks + reapers; owns g_dc* statics)
# Util/DcStatusPublisher.{h,cpp}
#   (DungeonClearUtil decomposition: addon comms + event-driven status push)
# Util/DcPullPlanner.{h,cpp}
#   (DungeonClearUtil decomposition: camp placement + dynamic-pull classification)
# Util/DcTargeting.{h,cpp}
#   (DungeonClearUtil decomposition: find-the-thing-to-fight unit; owns the
#    corridor trash-scan helpers. Completes the split — DungeonClearUtil class
#    is now empty; DungeonClearUtil.h is the umbrella + DC_PULL_* macros.)
# Util/DungeonClearApproach.{h,cpp}
#   (advance phase-machine §C: pure, engine-free decision core for the boss-
#    approach tail ladder — DecideApproach(Observation) -> Verdict, gtested at
#    every threshold edge; mirrors DungeonClearAdvanceAction's tail order.)
# Util/DungeonClearApproachIo.{h,cpp}
#   (orchestration replay harness: engine-free JSONL serialize/parse of the
#    DecideApproach (observation -> verdict) record + the live capture hook,
#    gated by DcSettings RecordDecisions. Replayed offline by t/replay_decisions
#    against t/fixtures/*.jsonl to lock every historical freeze as a regression.)
# src/Util/DcSpectator.{h,cpp}
#   (module-root src/Util/ — session plumbing, not bot AI: spectator
#    free-camera via WORLD_TRIGGER possession, `.dc spectate` / addon button)
# src/Util/DcWatchHop.{h,cpp}
#   (pure instance-bind planner for `.dc test watch`: which binds to release,
#    whether to re-bind, and whether the teleport must be forced far so a
#    second watch lands in the LIVE copy instead of the finished one)
# Data/RoomAggroRegistry.{h,cpp}
#   (room-wide-aggro boss table: bosses that force the whole room into combat on
#    engage; the tank clears the room before the pull — ClearRoomBeforeBoss)
# Value/DungeonClearRoomTrashValue.{h,cpp}
#   ("dungeon clear room trash remaining" set + the no-progress give-up valve)
# Data/DungeonEventRegistry.{h,cpp} + Util/DungeonEventExecutor.{h,cpp}
#   (declarative dungeon-events framework: typed step list + state-machine
#    executor; milestone 1 anchored events, milestone 2 Gossip + conditional)
# Data/EventConditionRegistry.{h,cpp}
#   (milestone 2: conditionId predicate table for off-path Conditional events —
#    e.g. SFK "courtyard door shut & prisoner alive"; mirrors ObjectiveHookRegistry)
# Data/Events/DungeonEventTables.h + Data/Events/*.cpp
#   (the event ROWS + activation conditions split one-file-per-dungeon:
#    SunkenTemple / ZulFarrak / ShadowfangKeep / ScarletMonastery /
#    RazorfenDowns / Stratholme Events.cpp + SharedConditions.cpp. Stratholme =
#    the dead-side "Baron run": 3 conditional ziggurat acolyte clears (conditions
#    5/6/7) + a persistent Slaughterhouse chain (abominations -> Ramstein -> 2
#    add waves -> Baron). DungeonEventRegistry.cpp /
#    EventConditionRegistry.cpp keep only the builder/lookup machinery and call
#    each file's Register<Dungeon>Events/Conditions appender — explicit calls so
#    every per-dungeon TU stays linked into the module static lib. Add a dungeon
#    = one new file + one call.)
#   (fable2 T2.3: the per-dungeon file is now the dungeon's whole "definition
#    unit" — its BossRosterPatch (Register<Dungeon>Roster, aggregated by
#    BossRosterRegistry) and any wing layout (Register<Dungeon>Wings, aggregated
#    by DungeonWingRegistry) moved in alongside the event rows, built with the
#    shared DcRoster helpers in Data/Events/DungeonRosterBuilders.h. NEW file:
#    Data/Events/MaraudonEvents.cpp — wings-only definition unit for map 349.)
# Action/DcActionShared.{h,cpp}, Action/DcAdvanceAction.cpp,
# Action/DcEngageActions.cpp, Action/DcPullActions.cpp,
# Action/DcFollowerActions.cpp
#   (nav/pull review Phase C Part 2 — the 4,608-line Action/DungeonClearActions.cpp
#    split along its four family seams: advance navigation / engage family /
#    pull FSM + maneuver / follower behaviors, with the cross-family
#    anon-namespace helpers + tuning constants promoted into namespace
#    DcActionShared (DcActionShared.{h,cpp}). DungeonClearActions.cpp is REMOVED;
#    DungeonClearActions.h stays as the umbrella class-declaration header.)
# Util/DcDecisionJson.h
#   (headless-sim Tier 1: shared flat-JSONL writer/parser primitives for the
#    decision capture/replay IO — header-only, used by the pull-decision IO)
# Util/DcPullDecision.{h,cpp} + Util/DcPullDecisionIo.{h,cpp}
#   (headless-sim Tier 1: pure Dynamic-pull governor decision core
#    DecidePull(PullObservation) -> PullVerdict, extracted from
#    DcPullPlanner::UpdateDynamicPullMode + its JSONL capture/replay IO, gated by
#    DcSettings RecordDecisions. Replayed offline by t/replay_pull against
#    t/fixtures/pull/*.jsonl; edge-pinned by t/TestPullDecisions; driven through
#    a scripted pull by t/TestScenarioDriver. Tier 2 nav harness (t/NavHarness,
#    t/TestNavGeometry, tools/slice_mapdata.py) is test-only, not GLOBed here.)
# Value/DungeonClearHealTargetValue.{h,cpp}
#   (LOS-blind, tank-biased most-hurt heal target — feeds the healer LOS
#    reposition fix: DungeonClearHealReposition{Trigger,Action})
# Data/Events/DireMaulEvents.cpp
#   (Dire Maul East: Ironbark the Redeemed anchored objective/event — gossip him
#    to open the Conservatory Door to Alzzin; ordered just before Alzzin via the
#    BossRosterRegistry map-429 patch + East-wing objective entry)
# src/DcStrategyGate.{h,cpp}
#   (dungeon-gated strategy install: the DC strategies are installed on a bot iff
#    it is on a dungeon/raid map — Map::IsDungeon() — instead of on every bot in
#    the realm. Removes the realm-wide DC trigger-ladder idle cost. Driven by
#    OnPlayerLogin + OnPlayerMapChanged (responsive) and a throttled world-tick
#    sweep in DungeonClearReaperScript (correctness net across ResetStrategies).
#    Pure decision kernel DcStrategyGate::Decide is headless-tested by
#    t/TestStrategyGate.cpp.)
# Data/DcNavPenaltyRegistry.{h,cpp} + Util/DcRouteFilter.{h,cpp}
#   (no-go route volumes: DcRouteFilter extends dtQueryFilterExt with a per-map
#    hand-authored no-go AABB table (DcNavPenaltyRegistry), so the long-range A*
#    corridor avoids navmesh shortcuts a real player can't follow — e.g. the LBRS
#    chasm wall-climb. Wired into LongRangePathfinder::BuildCoreFromMesh (now takes
#    a mapId) and screened in the StridedPathfinder fallback too.)
# Action/DcHakkar.cpp (Sunken Temple Avatar of Hakkar orchestration split out of
#   DcEngageActions.cpp — fable2 T2.5)
# REMOVED Data/EventConditionRegistry.{h,cpp}: conditional-event activation is now
#   a function pointer on DungeonEvent (.Conditional(&Predicate)), so the global
#   condition-id registry + id ledger are gone (fable2 T2.3 / events F7)
# Util/DcRegroupDecision.{h,cpp}
#   (combat-regroup Option B: pure contribution-gate kernel DecideCombatRegroup(
#    RegroupInputs) -> RegroupVerdict — "reconnect only when a follower truly can't
#    contribute". Engine-free, edge-pinned by t/TestCombatRegroup. Drives the
#    rewritten DungeonClearRegroupCombat{Trigger,Action}.)
# Data/Events/OldHillsbradEvents.cpp
#   (Escape from Durnholde, map 560: Brazen ride -> bomb 5 barrels + Lt. Drake ->
#    ONE EscortCreature step freeing Thrall and assisting him through Skarloc, the
#    mounted Tarren Mill ride, the barn/church/inn waves and Epoch Hunter, gated on
#    the instance progress counter. New step kind UseItemOnGO (cast a quest-item
#    spell AT a positioned GO — the barrel-bomb SPELLHIT); EscortCreature gains an
#    instance-data completion gate, a gossip-flag resume, and party mount speed-match.)
# Util/DcSmartRestDecision.{h,cpp} + Util/DcSmartRest.{h,cpp}
#   (Smart Rest hysteresis: pure latch kernel Decide(Inputs, members) -> Result
#    — enter on role-based low triggers, release at full hp/mana — edge-pinned
#    by t/TestSmartRest; DcSmartRest is the engine glue owning the DcRunState
#    latch fields + the cross-bot IsLatched read.)
# Util/DcWaitAtBossDecision.{h,cpp}
#   (Wait at Boss: pure pre-pull hold kernel — auto-pause the run right before
#    a boss pull and hold for the human's resume, once per boss per run via the
#    DcRunState::waitedBossGuid stamp; edge-pinned by t/TestWaitAtBoss. The gate
#    lives in DungeonClearEngageBossAction just before EngageDirect.)
# Value/DungeonClearPartyMemberToHealValue.{h,cpp}
#   (decorator over the stock `party member to heal` value, registered under the
#    same name so it fronts it: delegates to stock, then folds the DC escortee
#    into the heal candidate set while an escort is live — so the healer heals
#    Thrall / the Disciple / any escortee exactly like a party member. Paired with
#    DcLeaderSignal::GetLeaderEscortee (generic active-EscortCreature-step lookup)
#    and the escortee fold-in on DungeonClearHealTargetValue (reposition mover).)
# Util/DcBreadcrumb.h
#   (RecordBreadcrumb hoisted out of DcAdvanceAction.cpp so the item-on-GO
#    objective drive records the centered trail too — followers inherit it during
#    the Old Hillsbrad barrel run. Paired with a party-cohesion spread gate in
#    DriveUseItemOnGO so the tank waits for the party between houses instead of
#    sprinting off while the panel reports "waiting".)
# TestRun/DcTestRunVerdict.{h,cpp}, TestRun/DcTestDungeonRegistry.{h,cpp},
# TestRun/DcTestComp.{h,cpp}, TestRun/DcTestRunRecord.{h,cpp},
# TestRun/DcTestRunManager.{h,cpp}
#   (`.dc test` automated dungeon test harness: pure verdict kernel + supported-
#    dungeon/entrance registry + randomised party comp (DcTestComp::BuildComp
#    rolls a distinct-class tank/heal/3-dps party from a recorded seed; pinned
#    by t/TestTestComp) + JSONL run-result sink + the state-machine manager that
#    spawns/gears/groups/teleports a 5-bot party, starts the run, watches it,
#    and records the outcome.)
# TestRun/DcTestRunSelect.{h,cpp}, TestRun/DcTestRunLiveJson.{h,cpp}
#   (concurrent-run refactor pure helpers: DcTestRunSelect resolves the
#    `.dc test stop <selector>` precedence (bare/all/runId/dungeon-token) to a
#    set of run indices, and NextWatchIndex holds the `.dc test watch next`
#    cycle rule (wrap, cold start, stay put when there is nowhere to hop);
#    DcTestRunLiveJson builds the multi-run live-heartbeat
#    JSON ({"active","ts","runs":[…]}) the dashboard polls. Both engine-free and
#    edge-pinned by t/TestTestRunSelect + t/TestTestRunLiveJson. Paired with the
#    DcTestRunJob per-run FSM extraction + DcTestRunManager registry rewrite.)
# TestRun/DcTestRunJob.{h,cpp}
#   (per-run FSM extracted verbatim from the old singleton DcTestRunManager so N
#    runs can execute at once; DcTestRunManager is now a run registry.)
# Util/DcFormGate.h
#   (header-only shapeshift gate for the item-use casts: a feral-form druid tank
#    fails Spell::CheckCast's CheckShapeshift, so every quest-item interaction
#    — Old Hillsbrad's barrel bombs, the Deadmines cannon, the Mechanar cache —
#    silently no-ops until the form is dropped. Called just before each
#    CastItemUseSpell.)
# TestRun/DcDiagSnapshot.{h,cpp}
#   (end-of-run failure diagnostics: one read-only Snapshot of the whole run —
#    party positions/liveness/combat, the current boss target, route + wedge
#    watchdog counters, the stall reason, and the roster with WHICH completion
#    path fired per boss. Captured in DcTestRunJob::Teardown before the party is
#    disbanded, serialized under "diag" in dc_testruns.jsonl (schema 3).)
# Data/DcHazardRegistry.{h,cpp}
#   (per-map table of creatures emitting a persistent damaging aura bots must
#    stand outside of — the Arcatraz Sentinel's 15yd/1s Energy Discharge is the
#    forcing case. Split in two halves because they run on different threads:
#    the ROUTE half is hand-authored DcNavPenaltyRegistry boxes (the emitters are
#    rooted, and DcRouteFilter runs on the worker thread where no Map* exists);
#    the LIVE half is DcHazard::PointIsHot/LegIsHot over DungeonClearHazardsValue,
#    consumed by the camp/standoff solvers on the map thread.)
# Value/DungeonClearHazardsValue.{h,cpp}
#   (500ms-cached NearestUnitsValue of live hazard emitters near the bot. Needs a
#    PERMISSIVE AcceptUnit — the Arcatraz corpse bombs carry UNIT_DYNFLAG_DEAD and
#    are filtered out by AttackersValue::IsPossibleTarget, so DcKey::FarTargets
#    cannot be reused for this.)
# Util/DcHazard.{h,cpp}
#   (the live predicates the camp/standoff solvers call: PointIsHot/SegmentIsHot
#    for the persistent pulse, plus NearestVacate for the active retreat, and
#    PointIsInVacateBand — the "will the retreat trigger re-fire if I land here"
#    test the retreat validates its destination with. That is a DIFFERENT question
#    from PointIsHot, which asks about placement; conflating them thrashed the
#    Maraudon sludge packs.)
# Trigger/DungeonClearHazardVacateTrigger + Action/DungeonClearHazardVacateAction
#   (survival move: walk every party bot out of an active-vacate emitter's pulse.
#    Registered in BOTH strategies at DcRel::HazardVacate = 55 — the Arcatraz
#    Destroyed Sentinel pulses after the kill, once combat has usually dropped.
#    Per-row hysteresis decides what happens next: a thin holdBand means leave and
#    move on (the unattackable summon), a wide one means stay out while it lives
#    (Maraudon's Creeping Sludge, which melee are meant to abandon rather than
#    trade with). The action COMMITS to one validated point rather than recomputing
#    per tick — recomputing reverses the bearing whenever a nearer emitter is
#    elected, which is how a sludge pack turned the retreat into a treadmill.)
# Data/Events/ArcatrazEvents.cpp
#   (map 552: Warden Mellichar's stasis-pod wave finale + roster patch.)
# Data/Events/SethekkHallsEvents.cpp
#   (map 556: force-summon & kill Anzu — send-event 14797 via ObjectiveHook 7 —
#    + Objective-anchor roster patch. Bonus boss, works on normal difficulty.)
# Util/DcRezDecision.h + Util/DcRezRecovery.{h,cpp}
#   (post-combat party resurrection: a death HOLDS the run while a living
#    Priest/Paladin/Shaman/Druid can recover it instead of disabling on the
#    spot. Pure header-only election/timeout kernel Decide(Inputs, members) ->
#    Result, edge-pinned by t/TestPostCombatRez; DcRezRecovery is the engine
#    glue — dead-tolerant run-owner resolution, same-map snapshot KEEPING dead
#    members, the DcRunState rezPendingSinceMs/rezAnnounceMs clocks, and the
#    IsPending hold-gate read used by the between-pulls + event-rest gates.
#    Paired with the relaxed party-died trigger and the all-bots
#    DungeonClearRezParty{Trigger,Action} rung at DcRel::RezParty = 31.5.)
# Util/DcStrandedDecision.h + Util/DcStrandedRecovery.{h,cpp}
#   (stranded-member recovery failsafe: when the run has shown no progress for
#    DungeonClear.StrandedRecoveryNoProgressSecs while a bot member is stuck out
#    of range — fell under the world / wedged — the leader teleports the strays to
#    itself. Pure header-only Decide(Inputs, members) -> Result kernel, edge-pinned
#    by t/TestStrandedRecovery; DcStrandedRecovery is the engine glue — leader-only
#    clock ownership, the DcRunState progress* fields, a harness-mirroring progress
#    detector (encounter mask / cleared anchors / closing distance) and the group
#    teleport. Paired with the leader-only DungeonClearRecoverStranded{Trigger,
#    Action} rung at DcRel::StrandedRecovery = 42.)
# Data/Events/BlackMorassEvents.cpp
#   (map 269: the fully event-driven CoT Opening of the Dark Portal — Sa'at
#    gossip, portal-grounds pre-clear, Medivh proximity start + garrison via
#    ObjectiveHook 8, and the repeatable conditional wave-kill event that
#    closes all 18 time rifts. Objectives-only roster patch — every boss is a
#    rift TempSummon.)
# TestRun/DcWipeContext.{h,cpp}
#   (wipe post-mortem: the pure latch rule that answers "did we lose the boss
#    fight or did a trash pack eat us?". Combat ends when the last member dies,
#    so the harness samples the party's opponent every monitor step and only a
#    clean disengage clears it — a wipe never does. Edge-pinned by
#    t/TestWipeContext; DcTestRunJob::TrackEngagement is the engine glue, and
#    the result rides out on the run record (wipeOnBoss/wipeOpponent) and the
#    live heartbeat.)
# Overrides/BlackMorassDriver.cpp
#   (map 269's CONTROLLER half, lifted out of ObjectiveHookRegistry.cpp. Hooks 8
#    and 12 are the only on-arrival behaviour in the module that re-decides from
#    live world state every tick instead of doing one thing on arrival, so they
#    carry their own movement (StepsOwnMovement + repath/reissue throttling),
#    engagement (REACT_DEFENSIVE rift adds the normal pipeline cannot see) and
#    rift/keeper/drainer selection layers. Registered via RegisterBlackMorassHooks,
#    called explicitly from Hooks() — a static-lib TU with only constructor side
#    effects gets dropped by the linker. The DECLARATIVE half of 269 stays in
#    Data/Events/BlackMorassEvents.cpp.)
# TestRun/DcTestRoster.{h,cpp}
#   (`.dc test start <d> party=Tank,Heal,D1,D2,D3` — hand-picked parties of REAL
#    player characters, from any account, launchable from the Command Deck. This
#    is only the pure grammar (split, trim, positional roles, refuse a bad list),
#    edge-pinned by t/TestTestRoster; the engine half is
#    DcTestRunJob::CreateFromRoster + DcTestRunManager::StartRoster. Roster runs
#    log in MASTERLESS (AddPlayerBot with masterAccountId 0 — the ownership gate
#    refuses a hand-picked party on any other path), never roll the factory (no
#    respec/regear/relevel: the characters fight as they are), and always unbind
#    both difficulties first so every run gets a fresh instance.)
# TestRun/DcTestGearTiers.{h,cpp}
#   (per-run gear ceiling: `.dc test start <d> ilvl=N quality=epic`, the same two
#    options on `.dc test plan start`, and the ladder of ceilings worth offering
#    for a given dungeon — named raid tiers from the AutoGearScoreLimit conf
#    table at the level cap, three steps around the dungeon's own gear below it.
#    Pure kernel, edge-pinned by t/TestTestGearTiers; the engine half is
#    DcTestRunJob::TickProvisioning building PlayerbotFactory with the resolved
#    pair, and DcTestDungeonRegistry::WriteSidecar publishing the ladders for the
#    Command Deck's start form.)
# Util/DcCombatFlag.{h,cpp}
#   (flag-vs-fight: the one shared answer for every rung that used to stand down
#    on a bare bot->IsInCombat(). A 45yd hostile area aura flags the whole party
#    in with nothing aggroed, and each rung that keyed on the raw flag froze on
#    it in turn — the leader's driving ladder (fixed S1356), then the follower's
#    follow-tank rung, the between-pulls rest floors, the stranded failsafe and
#    the post-combat rez clock. MayDrive / IsPhantomFlag / AnyPartyEngagement,
#    all over DungeonClearMath::MayDriveWhileFlagged's grace.)
# (this comment bumps mtime to force CMake re-glob on next build)
# Util/DcTankForm.{h,cpp}
#   (pre-pull shapeshift gate — the mirror of DcFormGate. A feral druid is only
#    ever shifted by BearDruidStrategy's "bear form" trigger, which is
#    combat-engine only, while every DC pull is opened on the NON-combat engine;
#    so the druid tank tagged in caster form, ate the pack's opener at caster
#    armour, and shifted a beat late. EnsureBearForm is called fire-and-forget
#    from DungeonClearPullAction (whenever a pull is live) and from EngageDirect
#    (inside DC_PULL_START_RANGE), so nothing can wait on a form that won't go
#    up.)
# Data/ScriptedPullRegistry.{h,cpp}
#   (hand-authored "peel this pack out of that room, in this order, from this
#    exact spot" plans — the third member of the positional-override family, and
#    the middle ground between FightInPlaceRegistry, which FORBIDS a pull, and
#    BossPullbackRegistry, which MANDATES one for a boss. A row names the party
#    camp, the tank's stand spot (the one place with line of sight to exactly one
#    pack), the pack's cylinder + entry filter, and its position in the pull
#    sequence. Selin Fireheart's room is the case it exists for: every pack in it
#    shares sight-lines with the boss, so the safe spots are facts about the
#    room's walls that no aggro-radius or navmesh probe can derive.)
# Data/SealedEncounterRegistry.{h,cpp}
#    (SEALED ENCOUNTERS: bosses whose room LOCKS on encounter start, via an
#    InstanceScript DOOR_TYPE_ROOM door held `open &= (state != IN_PROGRESS)`.
#    Magisters' Terrace registers one for Selin Fireheart — the Assembly Chamber
#    Door 188065 at (215.1, 0.4) — so engaging him shuts the only way in and any
#    follower still in the corridor spends the whole fight at a closed door. A row
#    names the sealed VOLUME, because the tank crosses the threshold before it
#    engages and so a radius around the tank cannot express "will be locked out";
#    plus a tank-anchored clump radius for the final approach, which is what makes
#    the muster satisfiable without a long wait inside the boss's aggro range.)
# Util/DcPlayerbotCompat.h
#   (header-only: IsSelfBot / IsRealPlayer / IsHumanControlled written against
#    only the mod-playerbots API that is identical on master and test-staging,
#    so this module builds on both across PR #2592. No .cpp, so no re-glob is
#    needed for it — listed here to keep the file inventory complete.)
# Data/DcSocialQuarantineRegistry.{h,cpp} + Util/DcSocialQuarantine.{h,cpp}
#   (SOCIAL QUARANTINE — the fourth positional override, and the only one that
#    changes the MOBS rather than the party. While a named boss is the run's
#    objective, every pack the plan is not currently pulling is held at
#    REACT_DEFENSIVE, which is the first thing Creature::CanAssistTo tests and
#    the gate CreatureAI/SmartAI::MoveInLineOfSight passes before AttackStart —
#    so a quarantined pack can be neither RECRUITED by a call for help nor woken
#    by walking past it, while CreatureGroup::MemberEngagingTarget (which does
#    not test react state) keeps it fighting as a formation the instant anything
#    actually attacks it. Volumes come from the scripted-pull plan's own pack
#    cylinders, minus the due stage, plus hand-authored zones for packs with no
#    pull row. Built for the Magisters' Terrace rotunda, whose east/north-east
#    and centre/north-east pairs are inside CreatureFamilyAssistanceRadius AT
#    SPAWN — a T=0 bond no stand spot, opener or ordering can break.)
# TestRun/DcTestAreaTriggers.{h,cpp}
#   (AREATRIGGER RELAY for headless runs. An areatrigger is a client-side
#    volume: the only thing that ever runs an at_* script is
#    WorldSession::HandleAreaTriggerOpcode fed by a client's CMSG_AREATRIGGER,
#    and stock playerbots only relays a HUMAN MASTER's packet
#    (masterIncomingPacketHandlers) — its position-based WithinAreaTrigger
#    returns false for anything without an areatrigger_teleport row. So a
#    5-bot test party fires NO scripted areatriggers at all. The Underbog's
#    Ghaz'an is the proof: at_underbog_ghazan (4302) is the only caller of his
#    ACTION_MOVE_TO_PLATFORM, so without it he swims his underwater DB patrol
#    (path 1383920, z 32-47) all run instead of climbing to his platform (path
#    1383921, z 81.4). The job sends the packet for whichever member is
#    standing in the volume, edge-triggered like a client. Scoped hard: test
#    runs only, bot-only groups only (a human-controlled member's own client is
#    already sending it — relaying would double-fire), and scripted
#    NON-teleport triggers only (the instance exit is an areatrigger).)
# Util/DcZoneLine.{h,cpp}
#   (ZONE-LINE KEEP-OUT for camp placement. An advanced pull drags the pack back
#    onto cleared ground, and at a dungeon's FIRST pull the only cleared ground
#    behind the tank is the entrance it walked in through — so ComputeSafeCamp /
#    ComputeTrailCamp / GetLeaderScoutTrailPoint would plant the camp on, or
#    past, the instance exit areatrigger. Nothing downstream catches it: the
#    navmesh has no idea areatriggers exist, so IsNavReachable says yes. The cost
#    is asymmetric — a headless bot sends no CMSG_AREATRIGGER and shrugs it off,
#    but a SELF-BOT is a real client, reports the trigger, and the player is
#    teleported out of the instance mid-run. This indexes every map-changing
#    areatrigger by the map it stands on (ObjectMgr, snapshot by value, built
#    once) and offers a point test padded by CampMargin plus a segment test
#    padded by the much thinner WalkMargin — a camp can sit cleanly BEYOND a zone
#    line with both endpoints clear while the leg to it goes straight through.
#    Margins pad in XY only, never Z: a fall-catcher slab's ceiling sits a couple
#    of yards UNDER the floor beside it, and padding its height would make a
#    third of Shadowfang un-campable.)
# Data/Events/NexusEvents.cpp
#   (The Nexus, map 576. Three anchored objectives — one per Containment Sphere
#    (188526/188527/188528) — that FREE KERISTRASZA. She spawns
#    UNIT_FLAG_NON_ATTACKABLE inside a frozen prison and boss_keristrasza only
#    drops the flag once all three DATA_*_ORB boss states are DONE, which nothing
#    but a click on each sphere ever sets. Each objective sits at its sphere so
#    boss-nav walks the tank in; the event is a one-step UseGameObject, the
#    Sunken Temple statue idiom. The roster patch reorders the four bosses onto a
#    2..8 scale so the spheres have somewhere to sit between Ormorok and
#    Keristrasza — the DBC packs those two into adjacent bits on both
#    difficulties.)
# Data/Events/AzjolNerubEvents.cpp
#   (Azjol-Nerub, map 601. Two structural events plus a roster patch.
#    (1) HADRONOX. Her add swarm is INFINITE — three world triggers (23472)
#    summoned in her Reset() carry periodic summon auras that fire for as long as
#    the encounter is not done, and every add she kills while it carries her Leech
#    Poison heals her 10% of max HP, so she is not killable until the swarm stops.
#    The only off-switch is EVENT_HADRONOX_MOVE3, gated on all three Anub'ar
#    Crushers (28922) being dead, which walks her onto the z~733 platform and
#    casts Web Front Doors. The event garrisons the party on that platform until
#    the crushers are dead AND the doors are actually webbed (hook 13 reads
#    boss_hadronox::GetData(28921), the 'Hadronox Denied' probe — the only
#    exposure _doorsWebbed has), and the roster patch moves her anchor off her
#    spawn ledge (522.5, 544.9, 674.7 — 10yd from where every add-swarm waypoint
#    path ends) onto her own MOVE3 destination.
#    (2) THE DROP. Flood-filling the four 601 mmtiles splits the map into two
#    components that do not touch: z 645-833 (everything down to the pit floor)
#    and z 195-310 (the lower kingdom out to Anub'arak). The join is an ~11x13yd
#    HOLE in the pit floor at x 528-539 / y 543-556 with a single surface 360yd
#    below it. TeleportParty, not DropInHole — nothing makes a fall that long
#    survivable. Checkpoint (522, 548, 648.9), landing (533.5, 549.5, 288.7),
#    both column-probed against the live mmaps.
#    NO CLIENT PACKET IS INVOLVED: AreaTrigger.dbc has three rows on map 601 and
#    none of them carries a script, teleport, involvedrelation or smart_scripts
#    row, and the map has no gossip and no clickable gameobject at all.)
# Data/DcNeverTargetRegistry.{h,cpp}
#   (Creatures the CLEAR must never pick as a target — the "is killing this
#    progress" half of target selection, which AttackersValue::IsPossibleTarget
#    ("may this bot legally attack it") cannot answer. First row: The Nexus's
#    Crystalline Frayer (26793), which npc_crystalline_frayer makes unkillable
#    until Ormorok dies — a lethal hit is discarded, the mob spends 90s in a
#    NOT_SELECTABLE seed pod regenerating, then returns at full health — and
#    which instance_nexus::KillAllFrayers then kills outright, so an ALIVE frayer
#    is never worth fighting. Wired into the five clear-side candidate gates
#    (FarTargets AcceptUnit, PickBlockingTrash, the en-route sweep spheres,
#    NearestHostileNearPoint, room-trash); the stock combat engine is untouched,
#    so bots still fight back when a frayer attacks them.)
