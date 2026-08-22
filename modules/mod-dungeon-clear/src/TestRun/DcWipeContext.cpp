/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcWipeContext.h"

namespace DcTestRun
{
    Engagement UpdateEngagement(Engagement const& prev, EngagementSample const& sample)
    {
        if (!sample.bossName.empty())
            return {true, sample.bossEntry, sample.bossName};

        if (!sample.trashName.empty())
            return {false, sample.trashEntry, sample.trashName};

        // Survivors, nothing on them: the fight is over and won.
        if (sample.anyAlive && !sample.anyAliveInCombat)
            return {};

        // Either the party is down (nobody left to sample) or it is in combat
        // with something that resolved to no creature this tick. Both want the
        // last real engagement kept.
        return prev;
    }

    std::string BlameSuffix(Engagement const& blame)
    {
        if (blame.Empty())
            return {};
        return blame.isBoss ? " \xe2\x80\x94 killed by " + blame.name
                            : " \xe2\x80\x94 killed by trash: " + blame.name;
    }

    std::string StripResumeHint(std::string reason)
    {
        std::size_t cut = reason.find("Type 'dc on'");
        if (cut == std::string::npos)
            return reason;

        // "\xe2\x80\x94" is the em dash the reasons are written with. Only the
        // one INTRODUCING the hint may be cut — bounded lookback, so a reason
        // whose own body contains an em dash keeps it.
        constexpr std::size_t MAX_HINT_LEAD = 40;
        std::string const lead = reason.substr(0, cut);
        std::size_t const dash = lead.rfind("\xe2\x80\x94");
        if (dash != std::string::npos && lead.size() - dash <= MAX_HINT_LEAD)
            cut = dash;

        reason.erase(cut);
        while (!reason.empty() && (reason.back() == ' ' || reason.back() == '.'))
            reason.pop_back();
        return reason;
    }
}
