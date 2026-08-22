/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONEVENTREGISTRY_H
#define _PLAYERBOT_DUNGEONEVENTREGISTRY_H

#include <functional>
#include <string>
#include <vector>

#include "Common.h"
#include "SharedDefines.h"  // TeamId

#include "Ai/Dungeon/DungeonClear/Data/DcDifficultyGate.h"

class Player;
namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
// A Conditional event's activation predicate: a pure-ish read-only test over the
// live world, evaluated each tick. When it returns true (and the event is not yet
// latched) the conditional-event trigger drives the event's steps. The predicate
// lives next to the event that uses it (one file per dungeon under Data/Events/,
// cross-dungeon shared predicates in SharedConditions.cpp) and is handed to the
// builder directly by pointer — there is no global id space to keep collision-
// free (a typo is now a compile error, not a silently-never-firing event).
using EventCondition = std::function<bool(Player*, AiObjectContext*)>;

// Declarative framework for scripted dungeon EVENTS the party must perform to
// progress — pull a lever, click an altar, talk to an NPC and pick a gossip
// option, walk to a spot to trigger a spawn, free a prisoner, etc. An event is
// PURE DATA: an ordered list of typed steps the DungeonEventExecutor drives one
// at a time. This replaces hand-written C++ in ObjectiveHookRegistry with a
// reusable step vocabulary (the old freeform hook survives as the `Custom` step).
//
// Authoring mirrors the other DungeonClear registries: a hardcoded table in a
// GLOB'd .cpp (link-safe in the static-lib build). See DungeonEventRegistry.cpp.
//
// Milestone 1 ships the anchored model + the low-risk steps. An anchored event
// is referenced by a travel-objective anchor (DungeonBossInfo::eventId, kind
// Objective): when the tank reaches the anchor the executor runs the event's
// steps to completion before the clear advances. Gossip and the conditional
// activation model land in later milestones (the enum carries their slots now).

enum class EventStepKind : uint8
{
    MoveTo,                  // walk the leader to (x,y,z) within `radius` — a SHORT
                             // intra-room hop only (plain MovePoint). A far haul
                             // must be done by a BOSS/OBJECTIVE anchor so the boss
                             // navigation + dynamic-pull machinery drives it (an
                             // event step driving a long move fights the pull camp).
    Jump,                    // ballistic MoveJump to (x,y,z) within `radius` — bridge
                             // a navmesh gap a ground move can't (a drop-down ledge
                             // onto a disconnected mesh island). Same SHORT-hop rule
                             // as MoveTo: the anchor navigation gets the bot to the
                             // lip; this clears the one off-mesh leg. Idempotent —
                             // Done once landed (within radius), so a Drive restart
                             // after a combat tick-gap re-runs it safely.
    UseGameObject,           // approach + GameObject::Use(bot) the nearest goEntry
    Gossip,                  // talk creatureEntry + select gossipOption (milestone 2)
    WaitForSpawn,            // hold until creatureEntry is alive (wantAlive) / gone
    WaitForGameObjectState,  // hold until goEntry reaches GOState `wantState`
    KillCreature,            // gate: done when no alive creatureEntry remain in range
    ClearRadius,             // engage + gate: clear every reachable hostile within
                             // `radius` (2D) and a floor `zBand` of (x,y,z); done
                             // when none remain. POSITION-based (any entry), unlike
                             // KillCreature — a point-anchored room pre-clear.
    CastSpell,               // leader casts spellId on self (milestone 2+)
    UseItem,                 // leader uses itemId (milestone 2+)
    Wait,                    // dwell `durationMs` then continue
    Custom,                  // escape hatch -> ObjectiveHookRegistry hookId
    EscortCreature,          // PROTECT a moving NPC (Wailing Caverns' Disciple of
                             // Naralex) the whole way to its destination: start its
                             // scripted escort via gossip, then each tick FOLLOW it
                             // and actively ENGAGE whatever attacks it (mod-playerbots
                             // gives a bot no threat event when only a non-party
                             // escortee is hit, so aggro propagation alone never puts
                             // the tank into the fight). Done only when the final
                             // boss (`escortDoneEntry` / `escortDoneBit`) exists —
                             // never on "reached the end". DRIVEN by the action
                             // (DcObjectiveArriveAction::DriveEscortCreature, which
                             // owns the follow + threat-engage + self-heal gossip +
                             // watchdog); RunStep here is the pure completion gate.
                             // Anchored+Persistent only (the escort spans several
                             // combat gaps that would rewind a non-persistent event).
    DropInHole,              // DROP the party down a narrow vertical hole into the
                             // water below (Wailing Caverns' return-fall off Verdan's
                             // shelf to reach the Disciple). A ground move clamps to
                             // the navmesh edge and a ballistic Jump (Jump kind) clips
                             // the shaft wall — so this glides the leader a few yards
                             // OUT over the open hole-mouth (raw spline, off-mesh),
                             // then MoveFall()s it PURE-VERTICAL into the water (no
                             // horizontal travel => no wall clip; clears the mid-shaft
                             // ledge a lip-MoveFall would catch). Followers can't
                             // reproduce the off-mesh nudge, so they teleport to the
                             // landing (the sanctioned one-way-drop pattern). `x,y,z`
                             // is the over-hole nudge target; `landX,landY,landZ` the
                             // deep-floor landing.
                             // DRIVEN by the action (DriveDropInHole owns the leader's
                             // glide+fall so the at-objective Hold can't cancel the
                             // off-mesh spline); RunStep here pulls the followers and
                             // gates Done once the leader is on the deep floor.
                             // Anchored+Persistent (the drop is one-way — like the
                             // Serpentis Jump, the bot can't path back up).
    TeleportParty,           // TELEPORT the whole party (leader + every bot) across a
                             // navmesh break to a fixed landing, once the leader has
                             // reached the checkpoint the anchor navigation walked it
                             // to. For a one-way DIAGONAL drop where the checkpoint and
                             // landing sit on disconnected mesh islands and the
                             // horizontal offset rules out both a pure-vertical
                             // DropInHole (it would fall in the wrong column) and a
                             // ballistic Jump (big diagonal drops clip/overshoot — the
                             // source of past drop-down grief). User-sanctioned blunt
                             // instrument: nobody glides or falls, everyone is simply
                             // relocated. `x,y,z` is the checkpoint the leader must
                             // reach; `landX,landY,landZ` the landing. Fully synchronous
                             // — one RunStep teleports the leader, pulls every follower
                             // across (PullStrandedFollowersAcross), and returns Done, so
                             // no action driver is needed (unlike DropInHole). Idempotent:
                             // Done immediately if the leader is already on the landing,
                             // so a tick-gap restart never re-teleports. Anchored only
                             // (one-way — the bot can't path back up).
    UseItemOnGO,             // USE a quest item ON a positioned GameObject: grant the
                             // item (itemId) if the bags lack it, then USE it (the full
                             // CastItemUseSpell path) AT the GO of goEntry nearest
                             // (x,y,z), so the GO's SmartAI SMART_EVENT_SPELLHIT
                             // fires. This is the "plant a bomb on a barrel" mechanic
                             // (Old Hillsbrad's Durnholde barrels — item 25853, spell
                             // 32744, GO 182589). The use-spell is OPEN_LOCK against
                             // the GO's ITEM lock, so it only hits when cast FROM the
                             // key item — the Deadmines-cannon gotcha; a bare or
                             // triggered CastSpell never registers. Distinct from
                             // UseGameObject (Use()s the GO — never delivers a
                             // SPELLHIT) and UseItem (self-cast — no GO target). The
                             // (x,y,z) anchor picks a SPECIFIC GO, so five same-entry
                             // barrels are hit as five DISTINCT GOs. Done once the GO
                             // leaves GO_READY (the landed plant Uses the goober — a
                             // stable per-GO success latch, idempotent across restarts).
};

// One typed primitive. Fields are a shared bag — only those relevant to `kind`
// are read (see DungeonEventExecutor::RunStep). Authoring goes through the
// fluent EventBuilder below, not by populating this directly.
struct EventStep
{
    EventStepKind kind{EventStepKind::Wait};

    // Geometry: MoveTo target, and the search anchor for GO/creature steps when
    // (x,y,z) is set; (0,0,0) means "search around the bot".
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float radius{0.0f};      // arrival / search radius; 0 => a per-kind default
    float zBand{0.0f};       // ClearRadius: vertical half-band around z (keeps a
                             // multi-level chamber's balconies/pit out of a clear)

    uint32 goEntry{0};       // UseGameObject / WaitForGameObjectState
    uint32 creatureEntry{0}; // Gossip target / WaitForSpawn / KillCreature
    uint32 spellId{0};       // CastSpell
    uint32 itemId{0};        // UseItem

    int32  gossipOption{-1}; // Gossip: option index to select (-1 = none)
    uint32 count{1};         // KillCreature: alive-count that still blocks
    uint32 wantState{0};     // WaitForGameObjectState: target GOState value
    bool   wantAlive{true};  // WaitForSpawn: wait for alive (true) vs gone (false)

    // KillCreature only. When set, the DRIVING action (DcObjectiveArriveAction /
    // DcRunEventAction) actively walks the leader into the named creature via the
    // engage pipeline (EngageDirect — long-range path + combat) instead of merely
    // GATING on its death while the bot holds. Use it for a kill the bot must seek
    // out (a boss across the room / down the stairs), where waiting for the mob to
    // come to a held bot would deadlock. The step is still Done once no live
    // creature of the entry remains. (Distinct from the room-aggro entry-0 mode,
    // which targets the room-trash value rather than a fixed entry.)
    bool   engage{false};

    // KillCreature(engage) only. Bar this step from the COMBAT-side stealth-breaker
    // (DungeonEventExecutor::ActiveEngageStep's `anyStep` fallback, which arms
    // DungeonClearObjectiveEngageCombatTrigger from the event's FIRST engage step
    // even while an earlier step is still active).
    //
    // That fallback exists for a stealthed sapper that flags the party into combat
    // before the tank has reached the anchor: the signature it keys on is "a live
    // creature of the entry is nearby and reachable but this bot cannot see or
    // detect it". A boss that is DESIGNED to be undetectable until a gate opens
    // matches that signature perfectly and permanently — so the rung, which sits
    // above the stock combat movers, hijacks every combat tick and walks the tank
    // at the boss, abandoning whatever the party is actually fighting.
    //
    // Pathaleon the Calculator is exactly that: greater-invisible with
    // CanAIAttack()==false until four bridge deaths. Set this on any engage step
    // whose target is invisible/untargetable by script until the steps before it
    // have run. The step still drives the engage normally once it IS the active
    // step (DcObjectiveArriveAction), which is the only time engaging is correct.
    bool   engageOnlyWhenActive{false};

    // Gossip only. When set, a Gossip step whose target creature is not alive
    // anywhere nearby is SKIPPED (Done) instead of waited on — for an OPTIONAL
    // interaction the run must not deadlock on if the NPC died (a freed ZulFarrak
    // helper the party let die: a dead Weegli leaves the boss unreachable but we
    // still talk to Bly, and if every helper is dead the event simply completes
    // and the clear continues). A wide rescan separates "dead/gone" (skip) from
    // "still walking in / just outside the gossip search radius" (keep approaching).
    bool   skipIfMissing{false};

    // Gossip only. When set, the step WAITS (Running) while the target creature is
    // still moving, so the bot never talks to an NPC mid-walk. Used when a scripted
    // NPC walks to a final position before its gossip is meaningful (ZulFarrak's
    // freed crew descending to the temple floor — their gossip is already offered
    // while they are still walking down). Combined with skipIfMissing, a dead NPC
    // still skips rather than waiting forever.
    bool   waitForStill{false};

    uint32 durationMs{0};    // Wait: dwell length
    uint32 timeoutMs{0};     // 0 => EventStepTimeout config default; else per-step
    uint32 hookId{0};        // Custom -> ObjectiveHookRegistry; on a garrison MoveTo,
                             // the hook to re-run each tick WHILE it holds (see
                             // EventBuilder::WhileHolding). 0 => none.

    // --- EscortCreature only ----------------------------------------------
    // The escortee (creatureEntry) is the NPC to protect; `radius` is the grid
    // scan radius used to (re)resolve it live each tick. `gossipOption` is the
    // menu option that STARTS its scripted escort (re-run for self-heal when it
    // resets to idle). The remaining knobs:
    float  escortStandoff{0.0f};    // follow slot distance to hold BEHIND the
                                    // escortee (offset so the tank never blocks
                                    // its movement spline). 0 => per-kind default.
    float  escortThreatRadius{0.0f};// TIGHT scan radius around the escortee for an
                                    // attacker to engage (the ambushes spawn ON it,
                                    // so a tight radius catches them without
                                    // cross-pulling the cavern's ambient fauna).
                                    // `zBand` reuses the shared field for vertical
                                    // tolerance (the chamber drops ~z -96 -> -102).
    uint32 escortDoneEntry{0};      // completion creature (Mutanus 3654): the step
                                    // is Done once it exists (grid scan) — never on
                                    // "reached the destination".
    int32  escortDoneBit{-1};       // completion encounter bit, as a backstop to the
                                    // grid scan (set once the boss is killed even
                                    // after its corpse despawns). -1 => bit unused.

    // --- DropInHole only --------------------------------------------------
    // The deep-floor LANDING the leader falls onto and the followers teleport to.
    // (`x,y,z` is the over-hole nudge target the leader glides to before MoveFall.)
    float  landX{0.0f};
    float  landY{0.0f};
    float  landZ{0.0f};

    // MoveTo garrison gate, instance-data variant. When instanceDataId >= 0 the
    // step holds at (x,y,z) until the map's InstanceScript GetData(instanceDataId)
    // reaches instanceDataMin. Preferred over the creatureEntry "until alive" gate
    // for a value that the party kills DURING continuous combat (the event engine
    // is dormant in combat, so a transient "boss alive" window can be missed and
    // never recovers once the boss dies): a scripted phase counter only ever
    // climbs, so a >= gate is safe to observe late. -1 => no instance-data gate.
    int32  instanceDataId{-1};
    uint32 instanceDataMin{0};

    // MoveTo garrison gate, instance PERSISTENT-data variant. Same contract as
    // instanceDataId (monotonic counter, safe to observe late), but read from
    // InstanceScript::GetPersistentData instead of GetData.
    //
    // The two stores are unrelated: GetData is a virtual an instance script must
    // OVERRIDE, while the persistent-data vector is a plain SetPersistentDataCount
    // /StorePersistentData slot that every InstanceScript exposes for free. Several
    // scripts keep their wave counters only in the persistent store and never
    // override GetData at all (instance_mechanar is one: DATA_BRIDGE_MOB_DEATH_COUNT
    // lives in the persistent vector, and GetData returns the base-class 0 forever),
    // so an instanceDataId gate on such a map would hold until it times out.
    // -1 => no persistent-data gate.
    int32  persistentDataId{-1};
    uint32 persistentDataMin{0};

    // ClearRadius only. When non-empty, the volume clears ONLY creatures whose
    // entry is in this allow-list — both the RunStep completion gate and the
    // EngageDirect the driving action issues. Empty (the default) keeps the
    // plain position-based behaviour: everything hostile in the volume.
    //
    // For a set-piece fought inside ambient wildlife this is the difference
    // between the encounter and a safari. Black Morass is the case that forced
    // it: the wave volumes sit in open swamp (the arena catch-all is r=160),
    // and unfiltered they sent the party chasing crocolisks and tarantulas
    // across the map while the rift's infinites walked unopposed into Medivh.
    // Ambient fauna is still fought when it aggros the party — that is the
    // normal combat engine's job, not the clear's.
    std::vector<uint32> entryFilter;
};

// How an event enters the clear. Milestone 1 only uses Anchored; Conditional is
// reserved for off-path levers / pre-boss gates (milestone 2).
enum class EventActivation : uint8
{
    Anchored,     // referenced by a boss-list objective anchor (orderIndex)
    Conditional,  // fired by a predicate (condition) each tick
};

struct DungeonEvent
{
    uint32 mapId{0};
    uint32 id{0};            // stable per-map id (objective eventId / latch key)
    std::string name;

    EventActivation activation{EventActivation::Anchored};
    uint32 orderIndex{0};    // Anchored: the objective's encounter slot (doc only)
    EventCondition condition{};  // Conditional: the per-tick activation predicate

    // Difficulty gate. A HeroicOnly event never fires (and never surfaces in the
    // panel) on a normal run, and vice versa. For an ANCHORED event the primary
    // gate is the roster anchor that references it (gate the BossRosterPatch);
    // this field additionally hard-stops Drive as a belt-and-suspenders, and is
    // what the conditional-event paths filter on directly.
    DcDifficultyGate gate{DcDifficultyGate::Any};

    std::vector<EventStep> steps;

    // Required => a Blocked/timed-out step stalls the run for the human;
    // Optional => such a step is skipped and the clear advances anyway (used for
    // best-effort events whose scripted trigger may not fire for bots).
    bool required{true};

    // Conditional events only. A normal conditional event latches DONE the moment
    // its step list completes (DcRunEventAction inserts its ConditionalLatchKey),
    // so it never fires again that run. A Repeatable event is NOT latched on
    // completion — it re-fires every time its condition reads true again. Use it
    // for an action the party must repeat an unknown number of times, where some
    // OTHER live signal (not a one-shot latch) gates each repeat and ends the
    // loop. Example: Razorfen Downs' gong, which must be rung once per wave until
    // the boss spawns — the gong's own selectable flag gates each ring and the
    // boss going live ends the loop, so a fixed latch would stop it after ring 1.
    bool repeatable{false};

    // Conditional events only. A conditional event is normally driven ONLY out of
    // combat (DungeonClearEventDueTrigger stands down on bot->IsInCombat(), so the
    // stock combat engine owns every fight uncontested). That is right for the
    // usual shape — a lever, a gossip, a gate — where the event's work happens
    // between pulls.
    //
    // It is WRONG for a continuous WAVE encounter, where the party is in combat
    // essentially from the first pull to the last and the event IS the encounter.
    // There the non-combat-only driver runs only in the shrinking gaps between
    // waves, and stops running altogether the moment the party falls behind and
    // combat stops dropping — exactly when the encounter most needs steering.
    // Black Morass is the case that forced this flag: with two rifts open the
    // party never left combat, so nothing ever walked it to a portal to kill the
    // rift keeper, so the rifts never closed — a spiral with no floor. (Sunken
    // Temple's Avatar of Hakkar hit the same wall and was fixed the same way, by
    // hand, with a second copy of its handlers in DungeonClearCombatStrategy; this
    // is that fix generalised onto the events framework.)
    //
    // When set, the event is ALSO driven by the combat-engine copy of the
    // conditional-event rung (DungeonClearEventDueCombatTrigger ->
    // DcRunEventCombatAction, DcRel::EventDueCombat), which sits above the stock
    // combat movers so the driver can actually reposition the tank mid-fight.
    // Reserve it for encounters that genuinely need steering under fire: it takes
    // ticks away from the stock combat engine, which is the right owner everywhere
    // else.
    bool drivesInCombat{false};

    // This event's STEPS are the sole driver: they issue their own movement, and
    // they decide per tick whether they need the tick at all.
    //
    // Two consequences, both of which a continuous driver event needs:
    //
    //  (a) the driving action does NOT hold position for it (below); and
    //  (b) when a step reports Done — "nothing to steer this tick" — the action
    //      YIELDS the tick instead of claiming it. Engine::DoNextAction runs
    //      exactly ONE action per tick (it breaks out of its loop on the first
    //      Execute returning true), so a driver registered above the stock combat
    //      movers that returns true every tick starves the combat engine outright:
    //      the bot never picks a target, never swings, never casts, never builds
    //      threat. Black Morass wiped parties that way — the tank drove to the
    //      portal, the keeper engaged it, and the tank stood there taking hits
    //      with no rotation while the DPS pulled aggro and died. The step still
    //      RUNS every tick (the trigger is unchanged), so its side effects stay
    //      responsive; it just stops claiming ticks it does not need.
    //
    // DcRunEventAction normally calls DcMovement::ResolveEscortConflict before
    // every Drive, to park the tank while a step does its work — right for a
    // lever, a gossip, a wait. But ResolveEscortConflict CANCELS any escort glide
    // in flight, and it runs BEFORE the step does, so a Custom hook that delivers
    // the tank across the map with its own long-range spline has that spline
    // killed on the very next tick, re-issues it, has it killed again... and the
    // bot creeps roughly one tick's worth of movement per cycle while logging a
    // perfectly healthy "spline issued" each time.
    //
    // Black Morass is where this was found. Its rift camp was moved onto
    // LongRangePathfinder precisely because the portals are 80-102yd out and a
    // bare MovePoint silently truncates — and it STILL never arrived: 151 camp
    // attempts in a 10-run batch, none within 20yd of the rift, 111 logging
    // moving=false. The route was fine; it was being cancelled from under the
    // hook. Set this only for an event whose steps genuinely own the tank's
    // movement, or it will fight whatever else is trying to hold position.
    bool stepsOwnMovement{false};

    // ANCHORED events only. A normal anchored event is driven only while the tank
    // sits at its objective anchor, and DungeonEventExecutor::Drive RESTARTS it
    // from step 0 whenever Drive hasn't run for >1s (treated as a fresh
    // activation). That is right for a quick one-room event, but wrong for a long
    // multi-combat event that spans the dungeon (ZulFarrak's temple): every wave /
    // boss fight is a >1s gap on the combat engine, which would rewind the step
    // list each time combat ends. A Persistent event keeps its progress across
    // those gaps — Drive only (re)initialises it on a real eventId mismatch (new
    // run / different event), never on a tick gap — and its "at objective" trigger
    // stays sticky once started so the tank can roam far from the anchor (down the
    // stairs to the bosses) while the event drives, instead of being held at it.
    bool persistent{false};

    // Conditional events only, panel cosmetics. By default an off-path
    // conditional event renders last in the `dc bosses` panel (index 99). When
    // this names a boss entry, the event instead sorts just BEFORE that boss —
    // for a prerequisite the party performs before the boss is reachable (e.g.
    // the gong before Tuten'kash). 0 => default (sort last). Does not affect
    // engine ordering, only the status panel.
    uint32 panelGatesBossEntry{0};

    // Conditional events only, panel cosmetics. Like panelGatesBossEntry but
    // sorts the event just AFTER the named boss and keeps it as its OWN numbered
    // row (panelGatesBossEntry instead FOLDS the event into the boss's row as a
    // sub-line). Use for a standalone progression gate that follows a specific
    // boss but isn't owned by the next one — e.g. Shadowfang Keep's courtyard
    // door, which the party opens right after Rethilgore. 0 => not used.
    uint32 panelSortAfterBossEntry{0};

    // Conditional events only, panel cosmetics. When set to a real team the event
    // is shown in the panel ONLY for that team — a faction-specific gate whose
    // mechanic only one side ever performs (Shadowfang Keep frees Ashcrombe for
    // Alliance, Adamant for Horde). The activation predicate already team-gates
    // execution; this hides the irrelevant faction's row from the status panel.
    // TEAM_NEUTRAL => shown to both factions.
    TeamId panelTeam{TEAM_NEUTRAL};
};

// Fluent builder for readable registry rows, mirroring MakeBoss/MakeObjective.
//
//   Event(mapId, id, "Free the prisoner")
//       .MoveTo(x, y, z, 12)
//       .UseGO(GO_LEVER, 10)
//       .WaitForGOState(GO_GATE, GO_STATE_ACTIVE, 8000)
//       .Build();
class EventBuilder
{
public:
    EventBuilder(uint32 mapId, uint32 id, std::string name);

    EventBuilder& Anchored(uint32 orderIndex);
    EventBuilder& Conditional(EventCondition condition);
    EventBuilder& Optional();
    EventBuilder& Repeatable();
    EventBuilder& Persistent();
    // Keep driving this conditional event while the party is IN COMBAT (see
    // DungeonEvent::drivesInCombat). For continuous wave encounters only.
    EventBuilder& DrivesInCombat();
    // Do not hold position for this event's steps — they issue their own movement
    // (see DungeonEvent::stepsOwnMovement).
    EventBuilder& StepsOwnMovement();
    // Difficulty gates (see DungeonEvent::gate). Default is both difficulties.
    EventBuilder& HeroicOnly();
    EventBuilder& NormalOnly();
    EventBuilder& PanelBeforeBoss(uint32 bossEntry);
    // Sort this standalone event just AFTER `bossEntry` in the panel, keeping it
    // as its own numbered row (unlike PanelBeforeBoss, which folds the event into
    // the boss's row). See DungeonEvent::panelSortAfterBossEntry.
    EventBuilder& PanelAfterBoss(uint32 bossEntry);
    // Show this event in the panel only for `team` (a faction-specific gate). See
    // DungeonEvent::panelTeam.
    EventBuilder& PanelTeam(TeamId team);

    // Override the LAST-added step's timeout (0 on the step => the
    // EventStepTimeout config default). Chain it right after the step it tunes:
    //   .WaitForSpawn(entry).Timeout(900000)
    EventBuilder& Timeout(uint32 ms);

    // Mark the LAST-added step's target as optional: a Gossip step whose creature
    // is dead/gone is skipped rather than waited on. Chain after the step:
    //   .Gossip(npc, 0).SkipIfTargetMissing()
    EventBuilder& SkipIfTargetMissing();

    // Make the LAST-added Gossip step wait until its target NPC stops moving (has
    // finished a scripted walk) before talking to it. Chain after the step:
    //   .Gossip(npc, 0).WaitTargetStill()
    EventBuilder& WaitTargetStill();

    EventBuilder& MoveTo(float x, float y, float z, float radius = 0.0f);
    // Ballistic jump to (x,y,z): MotionMaster::MoveJump at run speed. Bridges a
    // navmesh gap a ground move can't (drop-down ledge / off-mesh island). The
    // preceding anchor/MoveTo must place the bot on the jump LIP; this clears the
    // gap. Done once the bot is within `radius` of the landing.
    EventBuilder& Jump(float x, float y, float z, float radius = 4.0f);
    // MoveTo that GARRISONS: walk to (x,y,z) and HOLD there — re-moving back if a
    // later tick finds the bot displaced (combat pushed it off the spot) — until
    // `creatureEntry` matches `wantAlive`. Keeps the tank anchored at a staging
    // point between waves (ZulFarrak's ramp head) instead of holding wherever the
    // last fight ended (down on the wave spawn). Internally a MoveTo step with a
    // spawn gate; creatureEntry 0 would be a plain one-shot MoveTo.
    EventBuilder& MoveToHoldUntilSpawn(float x, float y, float z, float radius,
                                       uint32 creatureEntry, bool wantAlive = true);
    // Garrison variant gated on a monotonic InstanceScript phase counter: hold at
    // (x,y,z) until GetData(dataId) >= minValue. Robust where the party fights
    // through the gate during continuous combat (ZulFarrak's temple — hold the
    // ramp until DATA_PYRAMID reaches WAVE_3, then descend; works even if the
    // bosses are already dead by the time the party drops combat).
    EventBuilder& MoveToHoldUntilInstanceData(float x, float y, float z, float radius,
                                              uint32 dataId, uint32 minValue);
    // Garrison variant gated on the InstanceScript PERSISTENT-data store: hold at
    // (x,y,z) until GetPersistentData(dataId) >= minValue. Use it when the map's
    // counter lives in the persistent vector and the script never overrides GetData
    // — the Mechanar bridge gauntlet's DATA_BRIDGE_MOB_DEATH_COUNT is the case it
    // exists for. See EventStep::persistentDataId.
    EventBuilder& MoveToHoldUntilPersistentData(float x, float y, float z, float radius,
                                                uint32 dataId, uint32 minValue);
    // Run ObjectiveHookRegistry `hookId` every tick while the PRECEDING garrison
    // step holds, in addition to its gate. The hook's result is ignored — the gate
    // alone still ends the step. Chain it right after the step it arms:
    //   .MoveToHoldUntilInstanceData(x, y, z, r, dataId, min).WhileHolding(hook)
    //
    // For a garrison whose gate can regress BEHIND THE PARTY'S BACK, where the
    // usual "start it with a Custom step, then hold for the finish" pair silently
    // stops being true. The Ring of Law is the case it exists for: npc_grimstone's
    // own 30s "no summon has a victim" watchdog SetData(FAIL)s the arena, which
    // despawns Grimstone and every wave and puts TYPE_RING_OF_LAW back to
    // NOT_STARTED. Nothing then restarts it — the Custom step that fired the
    // areatrigger latched Done minutes ago, and the test-run areatrigger relay is
    // EDGE-triggered on a volume the party is already standing in — so the hold
    // burns its whole timeout on an encounter that is no longer running. Re-running
    // the start hook from inside the hold closes that: it is a no-op while the
    // encounter is live and re-fires the real trigger once it isn't.
    EventBuilder& WhileHolding(uint32 hookId);
    EventBuilder& UseGO(uint32 goEntry, float searchRadius = 0.0f,
                        float x = 0.0f, float y = 0.0f, float z = 0.0f);
    // Leader casts `spellId` on itself (triggered: no cost/cooldown/reagent/cast
    // time). For a scripted "use a quest item" spell whose effect a bot cannot
    // otherwise reach — e.g. Sunken Temple's "Awaken the Soulflayer" (12346),
    // which a player normally fires via Yeh'kinya's Scroll / Egg of Hakkar to
    // summon the Shade of Hakkar. The spell's SEND_EVENT script only checks the
    // instance + event-not-started, so a direct triggered cast summons the Shade
    // without the quest item (and no-ops if the event already started).
    EventBuilder& CastSpell(uint32 spellId);
    // Leader USES `itemId` (granted first if not in bags), via the full item-use
    // cast path. For a scripted "use a quest item" the bot never earned — Sunken
    // Temple's Egg of Hakkar (10465), which fires Awaken the Soulflayer to summon
    // the Shade ONLY when used as an item (a bare CastSpell is rejected).
    EventBuilder& UseItem(uint32 itemId);
    EventBuilder& Gossip(uint32 creatureEntry, int32 option, float searchRadius = 0.0f);
    EventBuilder& WaitForSpawn(uint32 creatureEntry, bool wantAlive = true,
                               uint32 timeoutMs = 0);
    EventBuilder& WaitForGOState(uint32 goEntry, uint32 wantState,
                                 uint32 timeoutMs = 0, float searchRadius = 0.0f);
    EventBuilder& KillCreature(uint32 creatureEntry, uint32 count = 1,
                               float searchRadius = 0.0f);
    // KillCreature variant whose driving action actively seeks out and engages
    // the creature (EngageDirect) rather than only gating on its death. See
    // EventStep::engage.
    EventBuilder& KillCreatureEngage(uint32 creatureEntry, uint32 count = 1,
                                     float searchRadius = 0.0f);
    // Bar the LAST-added engage step from the combat-side stealth-breaker's
    // "arm from any engage step" fallback. Chain it right after the step:
    //   .KillCreatureEngage(boss, 1, 130).EngageOnlyWhenActive()
    // For a target that is invisible/untargetable by script until the earlier
    // steps have run. See EventStep::engageOnlyWhenActive.
    EventBuilder& EngageOnlyWhenActive();
    // Point-anchored room pre-clear: the driving action engages every reachable
    // hostile within `radius` (2D) and `zBand` (vertical, floor-keeping) of the
    // centre (x,y,z); the step is Done once none remain. Use on an OBJECTIVE
    // anchor placed at the centre so boss-nav travels the tank in first.
    EventBuilder& ClearRadius(float x, float y, float z, float radius,
                              float zBand = 20.0f);
    // Restrict the PRECEDING ClearRadius to the given creature entries (see
    // EventStep::entryFilter). Chain it right after the step it narrows:
    //   .ClearRadius(x, y, z, r).OnlyEntries({ENTRY_A, ENTRY_B})
    EventBuilder& OnlyEntries(std::vector<uint32> entries);
    EventBuilder& Wait(uint32 durationMs);
    EventBuilder& Custom(uint32 hookId);
    // Protect a moving escortee (see EventStepKind::EscortCreature). `escortee` is
    // the NPC entry, `startGossipOption` the menu option that starts (and, when the
    // escortee re-offers a gossip mid-route, RESUMES) its scripted escort, and
    // `doneEntry`/`doneBit` the final boss whose existence/kill completes the step.
    //
    // `doneDataId`/`doneDataMin` are an ALTERNATIVE completion gate for an escort
    // whose end is not marked by a boss going live but by the map's monotonic
    // progress counter reaching a value (Old Hillsbrad's whole Thrall escort — one
    // step from freeing him through Epoch Hunter's death — completes when
    // DATA_ESCORT_PROGRESS reaches FINISHED). -1 => unused (boss-entry gate only).
    // The two gates are OR'd: either satisfies completion.
    //
    // The geometry knobs carry sensible defaults; `searchRadius` is the live-
    // escortee grid scan radius (widen it for a mounted escort that outpaces the
    // party so the escortee is never lost off-grid).
    EventBuilder& EscortCreature(uint32 escortee, int32 startGossipOption,
                                 uint32 doneEntry, int32 doneBit,
                                 float standoff = 5.0f, float threatRadius = 18.0f,
                                 float threatZBand = 20.0f, float searchRadius = 80.0f,
                                 int32 doneDataId = -1, uint32 doneDataMin = 0);
    // Use a quest item ON a positioned GameObject (see EventStepKind::UseItemOnGO):
    // approach the `goEntry` GO nearest (x,y,z), then USE `itemId` on it via the
    // full item-use cast path (granting the item first if the bags lack it;
    // `spellId` is the item's use-spell, kept for validation and logging), so its
    // SmartAI SPELLHIT fires (the Durnholde barrel-bomb). `radius` is the CAST
    // REACH (0 => 3.5yd STRICT world distance — the landed OPEN_LOCK range-checks
    // the GO's interact box, live-measured failing at 6yd; arrival additionally
    // requires vmap LINE OF SIGHT so a barrel across a thin wall never reads as
    // reachable). The approach INTO the house is driven tick-owning
    // (DriveUseItemOnGO), which threads the doorway, detours to the anchor when
    // the barrel is walled off, and force-walks the final un-meshed yards.
    EventBuilder& UseItemOnGO(uint32 itemId, uint32 spellId, uint32 goEntry,
                              float x, float y, float z, float radius = 0.0f);
    // Drop the party down a narrow vertical hole (see EventStepKind::DropInHole).
    // (overX,overY,overZ) is the over-hole nudge target the leader glides to (a
    // point whose column is open straight to the deep floor — NOT the lip, whose
    // column catches a mid-shaft ledge); (landX,landY,landZ) the deep-floor landing
    // the leader falls onto and the followers teleport to. The preceding step must
    // settle the leader on the lip (a short MoveTo), exactly like the Serpentis
    // Jump's MoveTo→Jump pair.
    EventBuilder& DropInHole(float overX, float overY, float overZ,
                             float landX, float landY, float landZ);
    // Teleport the whole party across a navmesh break (see
    // EventStepKind::TeleportParty). (checkpointX,Y,Z) is the spot the leader must
    // reach — the objective anchor's navigation drives it there up the ramp; once
    // there (within `radius`), the leader and every party bot are relocated to
    // (landX,Y,Z). `radius` defaults generous so the objective arrival always
    // satisfies the gate (no mid-ramp teleport, no stutter-step closing move).
    EventBuilder& TeleportParty(float checkpointX, float checkpointY, float checkpointZ,
                                float landX, float landY, float landZ,
                                float radius = 12.0f);

    DungeonEvent Build() const { return _ev; }

private:
    EventStep& Add(EventStepKind kind);
    DungeonEvent _ev;
};

class DungeonEventRegistry
{
public:
    // The full event table, in registration order. Exposed as a seam for the
    // registry-integrity gtests (t/TestEventRegistry.cpp) — cross-reference and
    // persistence lint over the authored data. Not used by runtime callers, which
    // key on Find/Conditional.
    static std::vector<DungeonEvent> const& AllEvents();

    // The event with `id` on `mapId`, or nullptr if none is registered.
    static DungeonEvent const* Find(uint32 mapId, uint32 id);

    // True if the map has any registered event (cheap gate for callers).
    static bool HasEvents(uint32 mapId);

    // Every Conditional-activation event registered for `mapId`, in table order.
    // Empty for maps with only Anchored events. UNFILTERED by difficulty — an
    // authoring/test seam like AllEvents. Runtime callers use the difficulty
    // overload below so a heroic-only event never surfaces on a normal run.
    static std::vector<DungeonEvent const*> Conditional(uint32 mapId);

    // The Conditional events whose difficulty gate matches `difficulty`. This is
    // the runtime lookup — every live consumer (executor, panel, targeting) has
    // the bot's map in hand and must pass its difficulty.
    static std::vector<DungeonEvent const*> Conditional(uint32 mapId, Difficulty difficulty);

    // --- Room-aggro pre-clear (milestone 3) ------------------------------

    // True if `ev` is a room-aggro PRE-CLEAR event: a Conditional gate whose lone
    // KillCreature step uses creatureEntry 0 ("room-trash" mode — the target set
    // is resolved live through DungeonClearRoomTrashValue / the room-trash
    // targeting seam, not a fixed entry). These re-express the RoomAggroRegistry
    // bosses on the events framework: the condition (room trash remains) gates the
    // boss pull and DcRunEventAction drives the engage (nearest room trash first).
    // They are NEVER latched — the gate re-fires for each room-aggro boss on the
    // map (the condition simply reads false once a room is clear).
    static bool IsRoomAggroPreClear(DungeonEvent const& ev);

    // True if `mapId` has a room-aggro pre-clear event authored. The legacy
    // standalone room-clear trigger (relevance 26) stands down for such maps so
    // the conditional-event path (relevance 31) is the single room-clear driver.
    static bool HasRoomAggroEvent(uint32 mapId);
};

#endif
