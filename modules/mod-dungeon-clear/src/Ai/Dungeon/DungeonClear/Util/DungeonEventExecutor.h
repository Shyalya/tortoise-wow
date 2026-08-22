/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONEVENTEXECUTOR_H
#define _PLAYERBOT_DUNGEONEVENTEXECUTOR_H

#include "Common.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"

class Player;
class Creature;
class GameObject;
namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
// Result of running ONE event step on a tick.
enum class StepResult : uint8
{
    Running,  // still working — call again next tick
    Done,     // this step finished — advance to the next
    Blocked,  // cannot finish unaided (needs the human) — stall the run
    Failed,   // step failed / timed out — required stalls, optional skips
};

// What the driver should do after a step result is folded into progress.
enum class EventDriveOutcome : uint8
{
    Running,    // event still in progress — keep holding at the anchor
    Completed,  // all steps done — latch the anchor cleared and advance
    Stalled,    // a required step is blocked/failed — stall for the human
    Skipped,    // an optional step failed — latch cleared and advance anyway
};

// Per-run progress through one event, owned by a DungeonClear value (leader-
// keyed). Self-healing: when a different event starts (eventId mismatch) Drive
// resets it, so a stale value from a prior run is harmless.
struct DungeonEventProgress
{
    uint32 eventId{0};      // event currently being driven (0 = none)
    uint32 stepIndex{0};    // index of the active step
    uint32 stepStartMs{0};  // ms-time the active step was entered (timeout base)
    uint32 attempts{0};     // per-step attempt counter (re-click cadence, etc.)
    uint32 lastDriveMs{0};  // ms-time Drive last ran this event (gap detector)
    uint32 instanceId{0};   // instance this progress belongs to (new-instance reset)

    // Forward-progress watchdog, independent of stepStartMs.
    //
    // stepStartMs is the timeout base for the ACTIVE step, and it is re-stamped
    // every time a step reports Done — including the harmless re-Done of an
    // already-satisfied leading MoveTo after a stale-gap rewind. So a rewind loop
    // (rewind -> MoveTo Done -> step 1 -> rewind -> ...) keeps stepStartMs fresh
    // forever and the step timeout in Advance can never fire: the event runs
    // Running for the whole instance with no stall, no retry, and no log. That is
    // exactly how Steamvault's access-panel event wedged (see Drive).
    //
    // These two fields measure something a rewind cannot forge: the HIGH-WATER
    // step index and when it last actually increased. Oscillating between steps
    // 0 and 1 never raises the high-water mark, so the wedge is caught even when
    // every other clock keeps being reset. Re-stamped on a genuine (re)activation
    // — new event, new instance, real lapse — so a legitimately dormant event
    // (persistent, driven only between fights) is never charged for the gap.
    uint32 maxStepIndex{0};  // highest stepIndex reached this activation
    uint32 progressMs{0};    // ms-time maxStepIndex last increased (or activation)

    // EscortCreature watchdog: ms-time the escort last made genuine progress
    // (escortee moved, combat occurred, a reachable threat existed, or the final
    // boss is pending within grace). The escort step has no flat timeout (the
    // 32.5s banish channel + the long ritual would mis-fire one); this is its
    // dead-air liveness clock instead. 0 => unset (stamped on the first tick).
    uint32 escortProgressMs{0};

    // EscortCreature combat-wedge clock: ms-time the escortee was first seen IN
    // COMBAT with no attacker and no valid attack target (Old Hillsbrad: Thrall's
    // scripted Knockout on the unattackable Durnholde Armorer — upstream #25617).
    // Held for a debounce window before the driver force-clears the combat, so a
    // transient real-combat transition can never trip it. 0 => not wedged.
    uint32 escortCombatWedgeMs{0};

    // TeleportParty combat hold: ms-time the leader first had to wait for the
    // PARTY's fight to end before a one-way relocation. A relocation that fires
    // mid-fight leaves the party's holders on the far side of a navmesh break,
    // still holding combat references nobody can walk back to — the bots then
    // try. The gate reads AnyPartyHeldByLiveEnemy, so a phantom flag never arms
    // it at all; this bounds a real fight that cannot be finished at the
    // checkpoint. 0 => not waiting.
    uint32 relocationCombatHoldMs{0};

    // Drive-log throttle: the per-tick step line is logged only on a transition
    // (step or result change) or every kLogHeartbeatMs while Running, so a long
    // WaitForSpawn doesn't spam one line per tick.
    int32  lastLoggedStep{-1};
    int32  lastLoggedResult{-1};
    uint32 lastLogMs{0};

    void Reset()
    {
        eventId = 0;
        stepIndex = 0;
        stepStartMs = 0;
        attempts = 0;
        lastDriveMs = 0;
        instanceId = 0;
        maxStepIndex = 0;
        progressMs = 0;
        escortProgressMs = 0;
        escortCombatWedgeMs = 0;
        relocationCombatHoldMs = 0;
        lastLoggedStep = -1;
        lastLoggedResult = -1;
        lastLogMs = 0;
    }
};

class DungeonEventExecutor
{
public:
    // PURE state transition: fold a step's `result` into `prog` and report what
    // the driver should do. Handles step advancement, the per-step timeout
    // (escalates a too-long Running to Failed using `nowMs - stepStartMs`), and
    // the required/optional terminal mapping. No game state — unit-tested
    // directly. `defaultTimeoutMs` is used when the step's own timeoutMs is 0.
    static EventDriveOutcome Advance(DungeonEvent const& ev, DungeonEventProgress& prog,
                                     StepResult result, uint32 nowMs, uint32 defaultTimeoutMs);

    // IMPURE driver: (re)initialise progress for `ev`, run the active step
    // against the live bot/world, and Advance(). Returns the driver outcome.
    static EventDriveOutcome Drive(Player* bot, AiObjectContext* context,
                                   DungeonEvent const& ev, DungeonEventProgress& prog);

    // IMPURE: run a single step against the live world. Exposed for the driver;
    // separated from Advance so the latter stays pure/testable.
    static StepResult RunStep(Player* bot, AiObjectContext* context,
                              EventStep const& step, DungeonEventProgress& prog, uint32 nowMs);

    // True once the leader has fallen onto a DropInHole step's deep-floor landing
    // (settled at/below landing Z and no longer falling). Shared by RunStep's gate
    // and the action's DriveDropInHole so the "still dropping vs. landed" decision
    // is single-sourced. Z-based: the MoveFall is pure-vertical, so the leader's
    // X/Y is already over the landing — only the descent has to finish.
    static bool IsOnDropLanding(Player* bot, EventStep const& step);

    // IMPURE: drive the gossip OPCODES to open `npc`'s menu and select `option`,
    // returning true once the select has been sent (false while the menu/option
    // is not yet populated). Shared by the Gossip step and the EscortCreature
    // step's self-heal start (DriveEscortCreature), so the one subtle bit — the
    // core rejects a select whose packet guid isn't the open menu's sender, so we
    // send the NPC's OWN guid rather than the master's target — lives in one
    // place. The caller is responsible for being in interact range and (if it
    // matters) facing the NPC.
    static bool SelectGossip(Player* bot, Creature* npc, int32 option);

    // Static-geometry (vmap-only) line of sight from the bot to a step's
    // GameObject, eye-bumped on both ends. Shared by the UseItemOnGO RunStep and
    // its approach driver (DriveUseItemOnGO) so "arrived" means the SAME thing in
    // both: in reach AND visible. Vmap-only because the check must see through
    // other dynamic GOs but never through a house wall — a bot standing within
    // cast reach of a barrel on the FAR SIDE of a wall would otherwise spam-cast
    // through it forever (live deadlock).
    static bool HasGameObjectLos(Player* bot, GameObject* go);

    // --- Conditional activation (milestone 2) ----------------------------

    // Synthetic "dungeon clear cleared anchors" latch key for a Conditional
    // event of `eventId`. Conditional events have no boss-list anchor entry, so
    // they latch under a key in a high range that can never collide with a real
    // creature/anchor entry. Keep this pure so the trigger, action and tests all
    // agree on the key.
    static constexpr uint32 ConditionalLatchKey(uint32 eventId)
    {
        return 0x7F000000u + eventId;
    }

    // IMPURE: the first un-latched Conditional event registered for `mapId`
    // whose activation predicate (DungeonEvent::condition) is currently true; nullptr if none
    // is due. Shared by the conditional-event trigger (gate) and DcRunEventAction
    // (driver) so the two never disagree about which event is active.
    //
    // `requireDrivesInCombat` restricts the search to events flagged
    // DungeonEvent::drivesInCombat. The COMBAT-engine copy of the rung passes true,
    // so it can only ever drive an event that opted in to being steered under fire
    // — a normal conditional event stays the non-combat engine's business and the
    // stock combat engine keeps every fight it owns today.
    static DungeonEvent const* FindDueConditionalEvent(Player* bot, AiObjectContext* context,
                                                       uint32 mapId,
                                                       bool requireDrivesInCombat = false);

    // IMPURE: detect conditional events whose completion is signalled by their
    // own gating condition going false (instance state, not a ConditionalLatchKey)
    // and latch them into "dungeon clear cleared anchors" so the panel can show
    // them done. Some events (e.g. Stratholme ziggurat acolyte clears) flip their
    // instance data 1 -> 2 during combat, before the dormant in-combat executor
    // can run its own completion tick, so they never latch the normal way. Using
    // "dungeon clear seen due events", an event that was once due and now reads
    // not-due (and is neither repeatable nor a per-boss room-aggro pre-clear) is
    // treated as complete. Called from the throttled status-publisher tick so it
    // runs regardless of any bot's combat state. No-op when the map has no
    // conditional events.
    static void SweepCompletedConditionalEvents(Player* bot, AiObjectContext* context,
                                                uint32 mapId);

    // True if `context`'s run is currently driving a PERSISTENT anchored event
    // that has started (stepIndex past 0) — i.e. a long multi-phase set-piece
    // (ZulFarrak's temple) owns the tank. Single source of truth used to stand
    // other systems down for the event's whole duration:
    //   - the PULL pipeline (no advanced-pull camp-drag mid-event),
    //   - the follower SCOUT-LAG (followers stay tight on the tank instead of
    //     lagging up-ramp to rest and arriving late for the next wave), and
    //   - the at-objective trigger stays sticky (tank may roam from the anchor).
    // Reads the run's "next dungeon boss" + "dungeon clear event progress" values,
    // so pass the context of the bot whose run state you mean (the leader's).
    static bool IsPersistentAnchoredEventActive(AiObjectContext* context);

    // If `context`'s current objective drives an event with a KillCreature ENGAGE
    // step (KillCreatureEngage — .engage set), report true and fill `outEntry` /
    // `outSearchRadius` with the creature entry to seek and the radius to seek it
    // in. Pure lookup of the run's "next dungeon boss" + "event progress" values
    // against the registry; no world state. Used by both the non-combat objective
    // driver and the combat-side stealth-sapper rung so they seek the same target.
    //
    // `anyStep` controls WHICH step is consulted:
    //   * false (default): only the ACTIVE step (the idx=(matching?stepIndex:0)
    //     fallback used in DcObjectiveArriveAction — a stale/foreign progress
    //     resolves to step 0). Returns false when the active step is a MoveTo/gate.
    //   * true: the active step is preferred if it IS an engage step, else the
    //     event is SCANNED for its first engage step. The combat-side stealth-
    //     breaker passes true so it can arm during a leading MoveTo (step 0) — the
    //     window where a stealthed sapper flags the party into combat BEFORE the
    //     tank reaches the anchor and the non-combat driver advances to the engage
    //     step. Without this the combat rung stands down for exactly that deadlock.
    // Returns false (leaving the outs untouched) when the event has no engage step
    // or there is no active objective event.
    static bool ActiveEngageStep(AiObjectContext* context, uint32& outEntry,
                                 float& outSearchRadius, bool anyStep = false);
};

#endif
