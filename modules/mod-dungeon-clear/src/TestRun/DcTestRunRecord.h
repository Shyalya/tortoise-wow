/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTRUNRECORD_H
#define _PLAYERBOT_DCTESTRUNRECORD_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "TestRun/DcDiagSnapshot.h"

// One JSON line per completed test run, appended to dc_testruns.jsonl in the
// worldserver working directory (env override DC_TESTRUNS_FILE) — same
// pattern as the decision-capture logs (DungeonClearApproachIo), but with
// real string escaping because reasons/details are free text. The dashboard
// tails this file for the run-history panel.

namespace DcTestRunRecord
{
    struct CompEntry
    {
        std::string name;
        std::string className;   // "warrior", ...
        std::string spec;        // premade-spec name applied; on a roster run, the
                                 // character's own spec as read from its talents
        std::string role;        // "tank" | "heal" | "dps" — as MARKED
        std::uint64_t guid = 0;
        std::uint32_t level = 0; // actual post-factory level (may be clamped)

        // Roster runs only. A hand-picked party is never respecced, so the human
        // can mark a fury warrior as the tank; the run proceeds (that is the
        // policy) and this is how the post-mortem says so. Empty on pool runs,
        // where the spec is forced to match the role by construction.
        std::string detectedRole;    // what the talents say: "tank"|"heal"|"dps"
        bool roleMismatch = false;   // detectedRole != role

        // Where the character was standing when the run claimed it. Teardown
        // sends the party to its BIND point rather than back here (see
        // DcTestRunJob::ReviveAndSendHome — an entry position can be another
        // instance, or a spot the character may no longer legally stand in),
        // so this is what makes a manual recall to the original spot possible.
        std::uint32_t fromMap = 0;
        float fromX = 0.f, fromY = 0.f, fromZ = 0.f, fromO = 0.f;
    };

    struct StatusEntry
    {
        std::uint32_t t = 0;     // seconds since run start
        std::string state;       // publisher stateStr token
        std::string detail;
    };

    struct BossKill
    {
        std::uint32_t t = 0;
        std::uint32_t entry = 0; // 0 for anchor/objective completions
        std::string name;
        std::string via;         // "mask" | "anchor"
    };

    struct PauseEntry
    {
        std::uint32_t t = 0;
        std::string reason;
    };

    // One party member hitting the floor, with what the party was fighting at
    // that moment. A run can end long after (and far from) the death that
    // doomed it — a rez that never lands, a healer who bled out first — and by
    // teardown the corpses cannot say what killed them, so the cause has to be
    // stamped on at the instant of the transition.
    struct DeathEntry
    {
        std::uint32_t t = 0;
        std::string name;          // who died
        std::string opponent;      // what was on the party; "" if out of combat
        std::uint32_t opponentEntry = 0;
        bool onBoss = false;       // opponent is a roster/flagged boss
    };

    // One pull the Dynamic governor took a verdict on, with what the classifier
    // PREDICTED would come and how many mobs actually turned up.
    //
    // This pairing is the whole point. "The tank over-pulls in heroic" is two
    // different defects that look identical from outside the run: the estimate
    // may be right and the ceiling too generous (predicted ~= observed, both
    // high), or the ceiling may be fine and the estimate blind to half the room
    // (observed >> predicted). They need opposite fixes, and nothing else in the
    // record can tell them apart — the verdict only ever reached the debug log.
    //
    // Only produced in Dynamic mode (pull setting 2, the default): Off/On never
    // run the classifier, so there is no prediction to compare and no entries are
    // written. Counts are sampled at the monitor's 1 Hz step, so a mob that
    // joined and died inside one second can be missed — `observedMax` is a floor
    // on what was fought, never an over-count.
    struct PullEntry
    {
        std::uint32_t t = 0;              // seconds since run start, at verdict
        std::uint32_t targetEntry = 0;    // creature entry the verdict was taken on
        std::uint32_t predictedCount = 0; // estimated bodies (EstimateAggroCount)
        std::uint32_t predictedThirds = 0;// estimated weight, thirds of an elite
        std::uint32_t ceilingThirds = 0;  // PullDynamicMaxLeeroyMobs * 3
        std::uint32_t observedMax = 0;    // most distinct hostiles seen on the party
        std::uint32_t observedElites = 0; // ...of which elite, at that same sample
        bool advanced = false;            // final standing verdict was Advanced
        bool wipedHere = false;           // the run ended while this pull was open
    };

    struct Record
    {
        // 3: added diag (full failure snapshot) + stallAtEnd/phaseAtEnd
        // 4: added heroic (run executed at DUNGEON_DIFFICULTY_HEROIC)
        // 5: added the wipe post-mortem (wipeOnBoss/wipeOpponent*)
        // 6: added bossRoster + the per-member death log (deaths)
        // 7: added the per-pull predicted-vs-observed log (pulls)
        // 8: added roster (hand-picked real characters) + per-member
        //    detectedRole/roleMismatch and origin position
        // 9: added the per-run gear ceiling (gearIlvl/gearQuality)
        // 10: added combat blame — per-member botState (which ENGINE the bot is
        //     on, which is not the same question as whether it is flagged) plus
        //     attackers/holderRefs/phantomCombat/combatHolders, emitted only for
        //     a flagged member. Readers must key on holderRefs, not on the
        //     holder list: pre-10 records have neither, and treating an absent
        //     list as an empty one reports "nothing was holding them" about the
        //     freezes this was added to explain.
        std::uint32_t schema = 10;
        std::string runId;
        std::string planId;       // owning `.dc test plan`, "" for ad-hoc runs
        std::string dungeon;      // registry token
        std::string dungeonName;
        std::string wing;
        std::uint32_t mapId = 0;
        std::uint32_t instanceId = 0;
        std::uint32_t level = 0;  // requested level
        bool heroic = false;      // run at DUNGEON_DIFFICULTY_HEROIC
        std::uint32_t compSeed = 0;  // seed BuildComp rolled this comp from (for replay)
        // Gear ceiling the bots were rolled to, already resolved against the
        // AutoGear* conf values (0 ilvl = uncapped; quality is 1..5). Both stay
        // 0 on roster runs, which are never re-geared. The pair belongs in the
        // record because a run's outcome is only comparable to another run's at
        // the same ceiling.
        std::uint32_t gearIlvl = 0;
        std::uint32_t gearQuality = 0;
        // Hand-picked party of real player characters (`party=`) rather than a
        // seeded draw from the addclass pool. compSeed is 0 on these runs: the
        // roster IS the comp, so there is nothing to replay from a seed.
        bool roster = false;
        std::vector<CompEntry> comp;
        std::uint64_t startedAtMs = 0;  // unix ms
        std::uint64_t endedAtMs = 0;
        std::uint32_t durationS = 0;
        std::string result;       // DcTestRun::VerdictName token
        std::string failReason;   // human sentence; "" on success
        std::string disableReason;// verbatim DisableDungeonClear reason, if any
        std::uint32_t bossesTotal = 0;
        std::uint32_t bossesKilled = 0;
        // Every BOSS on the run's roster, in progression order — the denominator
        // side of "which bosses does this dungeon actually lose runs to". Without
        // it a plan summary can only aggregate bosses somebody killed or wiped
        // to, so a boss no run ever reached is invisible instead of 0%.
        std::vector<std::string> bossRoster;
        std::vector<BossKill> bossTimeline;
        std::vector<DeathEntry> deaths;
        std::vector<PullEntry> pulls;
        // Pulls past kPullLog that were not kept. A run that re-pulls the same
        // pack forever is exactly the pathology worth seeing, so the overflow is
        // COUNTED rather than silently dropped — a fat number here means the log
        // below is a prefix, not the whole run.
        std::uint32_t pullsElided = 0;
        std::vector<StatusEntry> statusTimeline;
        std::vector<PauseEntry> pauses;
        std::uint32_t finalMap = 0;
        float finalX = 0.f, finalY = 0.f, finalZ = 0.f;
        std::uint32_t pauseGraceS = 0, stallGraceS = 0, noProgressS = 0, overallS = 0;
        std::string setupStage;   // non-empty only for setup_failed:* records

        // The stall reason and phase token as they stood when the run ended.
        // Previously only the stalled_timeout verdict preserved the stall
        // reason (it was folded into failReason); every other failure threw it
        // away, which is exactly the field a no_progress post-mortem wants.
        std::string stallAtEnd;
        std::string phaseAtEnd;

        // What the party was fighting when it went down. The first question
        // anyone asks of a failed run is "did we lose the boss fight, or did a
        // trash pack eat us?", and neither the verdict token nor the status
        // timeline answers it: combat ends the instant the last member dies, so
        // by teardown there is nothing left to read.
        //
        // Populated on the "wipe" verdict and on a death bailout ("disabled"
        // with a corpse still in the party); empty on every other outcome.
        // Falls back to the last entry in `deaths` when the party was already
        // out of combat at the end — a rez that timed out, or a survivor who
        // disengaged and then bled out, both leave the live latch cleared while
        // the deaths that actually ended the run still know their killer. Stays
        // empty only when nothing was on the party at any death (a fall, a
        // hazard, a starvation).
        bool wipeOnBoss = false;
        std::uint32_t wipeOpponentEntry = 0;
        std::string wipeOpponent;  // boss or trash-mob name

        // Full end-of-run diagnostic snapshot, captured before teardown
        // disbands the party. Serialized under "diag"; absent on records
        // written before schema 3 and on setup failures (no party existed).
        DcDiag::Snapshot diag;
    };

    // A thrashing run can flip status every publisher tick; keep the head
    // (how the run opened) and the tail (how it ended) and drop the middle.
    inline constexpr std::size_t kStatusHead = 50;
    inline constexpr std::size_t kStatusTail = 150;

    // Pull-log cap. A full clear is well under a hundred pulls, so this only
    // binds on a run that is re-pulling in a loop; the excess is counted in
    // Record::pullsElided rather than dropped quietly.
    inline constexpr std::size_t kPullLog = 500;

    std::string EscapeJson(std::string_view s);
    std::string ToJsonl(Record const& rec);

    std::string CapturePath();
    void Append(Record const& rec);

    // Live-heartbeat sidecar for the dashboard: while a run is active the
    // manager overwrites this small JSON file every couple of seconds
    // (stage, elapsed, bosses, last status); the dashboard polls it for the
    // in-progress view. Env override DC_TESTRUN_LIVE_FILE.
    std::string LivePath();
}

#endif  // _PLAYERBOT_DCTESTRUNRECORD_H
