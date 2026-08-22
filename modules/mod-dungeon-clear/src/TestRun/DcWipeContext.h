/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCWIPECONTEXT_H
#define _PLAYERBOT_DCWIPECONTEXT_H

#include <cstdint>
#include <string>

// "Did we lose the boss fight, or did a trash pack eat us?" — the first
// question anyone asks of a failed test run, and the one neither the verdict
// token nor the status timeline answers.
//
// It cannot be answered at verdict time. The wipe is only called once every
// member has been dead for the grace window, and death clears combat: by then
// nobody has a victim, nobody has attackers, and the corpses cannot say what
// killed them. So the harness latches the party's current opponent on every
// monitor sample and lets the wipe read the last one.
//
// This is the pure rule half of that latch — no engine types, so the invariant
// that matters (a wipe must never clear the latch) is unit-testable. The job
// owns the impure half: walking live players to fill in a sample.
namespace DcTestRun
{
    // What the party was last seen fighting. Empty name == not engaged.
    struct Engagement
    {
        bool isBoss = false;
        std::uint32_t entry = 0;
        std::string name;

        bool Empty() const { return name.empty(); }
    };

    // One monitor sample of the party's combat picture. Only members that are
    // ALIVE and on the leader's map contribute — a corpse holds neither a
    // victim nor an attacker list, so a wiped party yields an all-false sample.
    struct EngagementSample
    {
        bool anyAlive = false;          // somebody is still standing
        bool anyAliveInCombat = false;  // ...and something is on the party

        // The opponents seen this sample. A boss anywhere in the fight outranks
        // trash: adds are part of a boss fight, and "wiped on Ghaz'an" is the
        // useful sentence even when a whelp landed the killing blow.
        std::uint32_t bossEntry = 0;
        std::string bossName;
        std::uint32_t trashEntry = 0;
        std::string trashName;
    };

    // Fold one sample into the latch:
    //
    //   a boss on the party      -> latch the boss
    //   only trash on the party  -> latch the trash mob
    //   alive and out of combat  -> CLEAR (a clean disengage; whatever comes
    //                               next is a fresh pull and the old fight must
    //                               not outlive it)
    //   anything else            -> hold what we had
    //
    // That last rung is the whole point: a party being wiped stops producing
    // samples with anyone alive in them, so the engagement recorded while
    // somebody was still standing survives to be reported.
    Engagement UpdateEngagement(Engagement const& prev, EngagementSample const& sample);

    // Which engagement a post-mortem should blame: the live latch when the party
    // went down mid-fight, else the one stamped on the last death.
    //
    // The second rung is what the rez-recovery bailouts need. "no one left alive
    // can resurrect" and "couldn't get X resurrected in time" both fire AFTER
    // combat has ended — the survivors disengaged, or there are none — so the
    // live latch has legitimately cleared by then and the run would otherwise
    // report no cause at all for the death that ended it.
    //
    // `atLastDeath` is the engagement as of the most recent death, empty
    // included: a member who dropped combat and then fell off a ledge must not
    // be filed against the boss the party disengaged from ten minutes ago.
    inline Engagement BlameFor(Engagement const& live, Engagement const& atLastDeath)
    {
        return live.Empty() ? atLastDeath : live;
    }

    // " — killed by Anzu" / " — killed by trash: Avian Ripper", or nothing at
    // all when the party was not fighting anything when it went down.
    std::string BlameSuffix(Engagement const& blame);

    // Disable reasons are written to be *said to a player*, so they end in a
    // call to action ("… — dungeon clear disabled. Type 'dc on' when ready to
    // resume."). In a test report that tail is noise on every row, and it is
    // part of the string a plan summary groups fail reasons BY — it pads every
    // reason equally without distinguishing any of them. Cut it, along with the
    // em dash that introduces it. Reasons without the hint pass through whole.
    std::string StripResumeHint(std::string reason);
}

#endif  // _PLAYERBOT_DCWIPECONTEXT_H
