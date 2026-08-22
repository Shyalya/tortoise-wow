/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcTestRunVerdict.h"

namespace DcTestRun
{
    Verdict Classify(Observation const& o, Limits const& l)
    {
        // The run ending on its own beats every watchdog: DisableDungeonClear
        // already knows why it ended, and a timer firing on the same tick must
        // not overwrite a real success with a timeout.
        if (o.disableFired)
            return o.disableAllCleared ? Verdict::Success : Verdict::FailDisabled;

        if (o.abortRequested || o.leaderMissing || !o.gmOnline)
            return Verdict::FailAborted;

        // Above pause/stall/no-progress: a corpse party reads as paused, stalled
        // AND frozen all at once, and "wipe" is the only one of the four that
        // says anything useful about the run.
        if (o.partyWiped && o.wipedForMs >= l.wipeGraceMs)
            return Verdict::FailPartyWiped;

        if (o.paused && o.pausedForMs >= l.pauseGraceMs)
            return Verdict::FailPausedTimeout;

        if (o.stalled && o.stalledForMs >= l.stallGraceMs)
            return Verdict::FailStalledTimeout;

        if (o.sinceProgressMs >= l.noProgressMs)
            return Verdict::FailNoProgress;

        if (o.elapsedMs >= l.overallTimeoutMs)
            return Verdict::FailOverallTimeout;

        return Verdict::Continue;
    }

    char const* VerdictName(Verdict v)
    {
        switch (v)
        {
            case Verdict::Success:            return "success";
            case Verdict::FailDisabled:       return "disabled";
            case Verdict::FailPartyWiped:     return "wipe";
            case Verdict::FailPausedTimeout:  return "paused_timeout";
            case Verdict::FailStalledTimeout: return "stalled_timeout";
            case Verdict::FailNoProgress:     return "no_progress";
            case Verdict::FailOverallTimeout: return "overall_timeout";
            case Verdict::FailAborted:        return "aborted";
            case Verdict::Continue:           break;
        }
        return "continue";
    }
}
