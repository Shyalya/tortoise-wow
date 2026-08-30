/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_PARTY_STATE_H
#define _DC_PARTY_STATE_H

#include <string>

namespace ai { class AiObjectContext; }
using ai::AiObjectContext;
class Player;
struct Position;

class DcPartyState
{
public:
    // The HP/mana percentages the between-pulls rest gate (IsPartyReady) holds
    // for. These default to mod-playerbots' own drink/eat stop thresholds
    // (AiPlayerbot.AlmostFullHealth / AiPlayerbot.HighMana): a stock bot only eats
    // back up to AlmostFullHealth and drinks back up to HighMana, then stops, so
    // we clamp the gate to those targets to keep it reachable by resting alone.
    //
    // When the run sets DungeonClear.RestHealthPct / RestManaPct (> 0) the group
    // overrides those targets for this run: the gate uses the override directly
    // (the matching DungeonClearNeeds{Eat,Drink} triggers make bots eat/drink up
    // to it, so even a target above the playerbots stop value is reachable). The
    // `bot` resolves the run owner for the override lookup; pass any member.
    // See the README's "mod-playerbots interaction" section.
    static float RestMinHpPct(Player* bot = nullptr);

    static float RestMinMpPct(Player* bot = nullptr);

    // Returns true when the party has caught up and recovered enough to pull again:
    //  - every living party member on the bot's map has HP% >= minHpPct,
    //  - every living mana-using party member has mana% >= minMpPct,
    //  - every living member is within maxSpread yards of the bot — or of
    //    *spreadAnchor when given (pull mode measures against the camp the party
    //    is held at, since it is not allowed to stand near the tank there),
    //  - and, when maxTankGap > 0, every living member is ALSO within maxTankGap
    //    yards of the bot itself. Absolute backstop for the anchored case: a camp
    //    gone stale right where the party stands satisfies the anchored spread
    //    forever while the tank glides away unchecked (the scout-runaway gap).
    //    Sized generously (spread + the largest legitimate camp standoff) so it
    //    never trips in healthy pull states — see GetSpreadGate.
    // Dead members are not blocking — the party-died trigger handles them.
    // Callers pass RestMinHpPct()/RestMinMpPct() for the recovery thresholds.
    static bool IsPartyReady(Player* bot, float minHpPct, float minMpPct, float maxSpread,
                             Position const* spreadAnchor = nullptr,
                             float maxTankGap = 0.0f);

    // The effective inputs of the between-pulls spread check for `bot` right now:
    // the live PartyMaxSpread (waived to "infinite" while a pull maneuver is
    // actually HOLDING the party at camp, Forming/Advancing/Returning) and, in
    // pull mode with a camp stamped, the camp the spread is measured against
    // instead of the tank. ONE body shared by the gate (IsBetweenPullsReady) and
    // the status panel so the panel can never report a different wait than the
    // gate enforces.
    //
    // Why the camp anchor exists: in pull mode the party is PINNED at the camp by
    // hold-at-camp even between maneuvers (phase Idle) — it never closes on the
    // tank. A catch-up check measured against the tank then deadlocks the run
    // whenever the camp standoff (PullSetback, safe-camp/LOS extensions up to
    // PullMaxDrag) reaches PartyMaxSpread: the gate can't pass, so the pull
    // trigger never fires, so the camp never advances, so the party never gets
    // any closer — tank and party wait on each other forever. "Caught up" in
    // pull mode means "set at the camp they were told to hold", so the spread is
    // anchored there. The anchor points into the bot's pull-context value
    // (stable storage); treat it as valid for the current tick only.
    // maxTankGap is the camp-anchored backstop described on IsPartyReady: set
    // (> 0) only alongside the camp anchor, to PartyMaxSpread + the largest
    // standoff a live camp may legitimately sit behind the tank
    // (max(PullSetback, PullMaxDrag)). A correctly-trailing camp keeps every
    // member inside it by construction; only a stale camp (the runaway) trips it.
    struct SpreadGate
    {
        float maxSpread = 0.0f;
        Position const* anchor = nullptr;  // nullptr = measure against the bot (tank)
        float maxTankGap = 0.0f;           // 0 = no absolute tank cap
    };
    static SpreadGate GetSpreadGate(Player* bot, AiObjectContext* context);

    // The max spread the LEADER's advance gate is enforcing right now, for a
    // FOLLOWER that needs to position itself inside it.
    //
    // Exists because "the spread the tank will accept" is NOT the PartyMaxSpread
    // setting. GetSpreadGate overrides it — waived while a maneuver holds, and
    // tightened on a sealed-encounter final approach — and a follower that clamps its
    // own standoff against the raw setting can be ordered to stand outside the gate
    // the tank is actually waiting on. That is a mutual deadlock: the tank will not
    // advance until the party closes, and the party will not close because its own
    // rule says the distance it is holding is fine.
    //
    // Live (tr-20260803-134213-2): both trash stages retired cleanly, then the run
    // hung for three minutes on the walk to Selin. The tank logged "advance yielding:
    // party not ready — waiting on Emandy, Toogo, Ushkuk (out of range)" 365+ times
    // while the followers logged "scout-lag: holding at trail point (18.2yd behind
    // tank, lag 15.0)" — the sealed clump had tightened the gate to 10yd, and the
    // scout lag was still clamping against the 25yd setting. Neither side was wrong
    // on its own terms.
    //
    // Falls back to the bot's own setting when there is no resolvable leader.
    static float LeaderEffectiveMaxSpread(Player* bot);

    // The HP/mana floors the between-pulls gate is ACTUALLY enforcing for `bot`
    // right now. ONE body shared by the gate and every "waiting on…" line, the
    // same way GetSpreadGate is shared, so the panel can never name a wait the
    // gate is not holding for.
    //
    // Both floors drop to 0 (spread-only readiness) in two cases:
    //   * Smart Rest is on — its party latch owns recovery, not these floors.
    //   * The party is PHANTOM-FLAGGED (DcCombatFlag::IsPhantomFlag): flagged in
    //     combat with nothing fighting it. Eating and drinking both require being
    //     out of combat, so HP and mana cannot come back and the floors can never
    //     be met — the gate would hold the party exactly where it stands, forever.
    //     Inside a DAMAGE aura that is not a stall but a death sentence: Arcatraz
    //     heroic, run tr-20260801-194932-20, the tank parked in an Eredar
    //     Deathbringer's 45yd Unholy Aura waiting on mana that could never come
    //     back, taking 750 every 2s the whole time. Waiving the floors lets
    //     Advance move the party OUT, which is the only thing that ends the flag.
    struct RestGate
    {
        float minHp = 0.0f;
        float minMp = 0.0f;
    };
    static RestGate GetRestGate(Player* bot, AiObjectContext* context);

    // Between-pulls gate: party HP/MP recovered (RestMinHpPct/RestMinMpPct) and
    // spread within DungeonClear.PartyMaxSpread — measured per GetSpreadGate
    // (waived mid-maneuver, camp-anchored in pull mode, tank-anchored otherwise).
    //
    // `requireNoLoot` additionally fails the gate while the bot has a corpse to
    // walk to ("has available loot"): the trigger ladder wants that (never start
    // a pull over pending loot), but the advance action must NOT — it handles
    // loot separately in Execute behind a commit-timeout, and folding the loot
    // flag in there would defeat the timeout. One shared body for both sides so
    // they can never drift again (they were two copies, and had).
    static bool IsBetweenPullsReady(Player* bot, AiObjectContext* context, bool requireNoLoot);

    // SCRIPTED-STAGE MUSTER. True to HOLD the plan: a ScriptedPullRegistry stage is
    // due, no stage is in flight yet, the party is short of the muster floors
    // (DC_SCRIPTED_PULL_MUSTER_HP/_MP), and the bounded wait has not run out.
    //
    // Separate from IsBetweenPullsReady rather than folded into it because it must
    // bind in BOTH of that gate's branches — the Smart Rest branch deliberately
    // passes 0/0 floors and hands recovery to its latch — and because it carries a
    // clock, which a readiness predicate should not. Mutates the latch on the
    // leader's DcPullContext::scriptedMusterSince; call once per tick from the
    // pull trigger. False (and the latch cleared) whenever no stage is due, so
    // every dungeon without a plan pays one memoised registry lookup.
    //
    // See DungeonClearMath::ShouldMusterForScriptedStage for the wait contract and
    // ScriptedPullRegistry.h's muster block for why the floors are what they are.
    static bool IsScriptedStageMustering(Player* bot, AiObjectContext* context);

    // READ-ONLY view of the muster latch for the leader's OTHER driving rungs
    // (advance, blocking-trash). True while a muster is actively holding: latch
    // armed and inside the budget. The muster only stood the PULL trigger down —
    // the ordinary floors are lower than the muster floors, so advance stayed
    // green in the gap and walked the tank into the very room the stage was about
    // to pull (tp-20260806-212646-1: 32 unplanned rotunda pulls, 19 run-fatal).
    // Never mutates and never logs: IsScriptedStageMustering owns the latch and
    // its two log edges, and the pull trigger stays its single caller.
    static bool IsScriptedMusterHolding(Player* bot, AiObjectContext* context);

    // Any dead group member on the bot's map. IsPartyReady deliberately skips
    // the dead (rez recovery holds the run); the scripted-stage muster must NOT
    // — when recovery is not pending (no viable rezzer, or the death is one tick
    // old), "topped up" over a corpse armed stages short-handed within 10s of a
    // death. Bounded by the muster budget, so an unrecoverable death cannot
    // deadlock the run through this.
    static bool HasDeadSameMapMember(Player* bot);

    // Returns true if any LIVING bot party member on the bot's map (excluding
    // `bot` itself) currently has a corpse it intends to loot — in any phase,
    // walking in (has available loot) or within reach (can loot). Reads each
    // member's own loot values cross-context (same pattern as
    // DungeonClearPartyTankValue); real players (no PlayerbotAI) are skipped
    // since we neither drive nor wait on their looting. Lets the dungeon-clear
    // tank hold its advance after a pull until the whole party has finished
    // looting; the caller bounds the wait with a commit-timeout.
    static bool IsAnyPartyMemberLooting(Player* bot, std::string* whoOut = nullptr);

    // True when ANY alive same-map groupmate (the bot itself included) is in
    // combat. Broader on purpose than DcLeaderSignal::IsLeaderShouldAssistFight,
    // which additionally demands the tank see no target of its own and no pull
    // maneuver be holding: those extra guards are right for DRIVING the tank
    // into a fight, but they are the wrong question for "may the between-pulls
    // rest gate hold?". If anyone is swinging, we are not between pulls.
    static bool IsAnyMemberInCombat(Player* bot, std::string* whoOut = nullptr);

    // Builds a short, human-readable account of who the tank is waiting on to
    // become pull-ready, using the SAME thresholds IsPartyReady is called with
    // (so the description always matches the gate that actually holds the
    // advance). Lists each living on-map member that is too far, low on health,
    // or low on mana, with the limiting reason — e.g. "Bob (low HP), Alice (out
    // of range)". Caps the list so the addon line stays short; extra members
    // collapse to "+N more". Returns "" when the party is ready (nobody to wait
    // on). Used by DcStatusAction to fill the addon "resting" detail.
    static std::string DescribePartyNotReady(Player* bot,
                                             float minHpPct, float minMpPct,
                                             float maxSpread,
                                             Position const* spreadAnchor = nullptr,
                                             float maxTankGap = 0.0f);

    // Names the living bot party members currently looting (walking to or
    // standing on a corpse), comma-joined and capped like DescribePartyNotReady.
    // Returns "" when only the tank itself is looting / nobody is. Used to fill
    // the addon "looting" detail so the player can see who is holding up the
    // advance.
    static std::string DescribePartyLooting(Player* bot);

};

#endif  // _DC_PARTY_STATE_H
