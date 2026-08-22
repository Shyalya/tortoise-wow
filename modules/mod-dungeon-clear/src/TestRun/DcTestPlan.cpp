/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestPlan.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace DcTestPlan
{
    std::uint32_t LaunchesWanted(Spec const& s, Counters const& c,
                                 std::uint32_t globalHeadroom, std::uint32_t backoffMs)
    {
        if (backoffMs > 0)
            return 0;
        std::uint32_t const remaining = s.total > c.launched ? s.total - c.launched : 0;
        std::uint32_t const planHeadroom =
            s.concurrent > c.activeNow ? s.concurrent - c.activeNow : 0;
        return std::min({remaining, planHeadroom, globalHeadroom});
    }

    ParseResult ParseStartArgs(std::string const& args)
    {
        ParseResult out;
        auto usage = [&](std::string const& why) -> ParseResult&
        {
            out.ok = false;
            out.err = why +
                " — usage: .dc test plan start <dungeon> [heroic] total=N [concurrent=N] [level=N]"
                " [seed=N] [ilvl=N|none] [quality=rare|epic|…]";
            return out;
        };

        std::istringstream in{args};
        std::string word;
        while (in >> word)
        {
            std::size_t const eq = word.find('=');
            if (eq == std::string::npos)
            {
                if (word == "heroic")
                {
                    out.spec.heroic = true;
                    continue;
                }
                if (!out.spec.dungeonToken.empty())
                    return usage("unexpected '" + word + "'");
                out.spec.dungeonToken = word;
                continue;
            }

            std::string const key = word.substr(0, eq);
            std::string const val = word.substr(eq + 1);

            // The two gear options take words as well as numbers ("ilvl=none",
            // "quality=epic"), so they are read before the numeric parse below
            // rejects them.
            if (key == "ilvl")
            {
                bool ok = false;
                std::int32_t const ilvl = DcTestGearTiers::ParseIlvl(val, &ok);
                if (!ok)
                    return usage("bad item level in '" + word + "' (1-400, or none)");
                out.spec.gear.ilvl = ilvl;
                continue;
            }
            if (key == "quality")
            {
                std::uint32_t const q = DcTestGearTiers::ParseQuality(val);
                if (q == 0)
                    return usage("bad quality in '" + word +
                                 "' (normal|uncommon|rare|epic|legendary, or 1-5)");
                out.spec.gear.quality = q;
                continue;
            }

            char* end = nullptr;
            unsigned long const n = std::strtoul(val.c_str(), &end, 10);
            if (val.empty() || !end || *end != '\0')
                return usage("bad value in '" + word + "'");

            if (key == "total")
                out.spec.total = static_cast<std::uint32_t>(n);
            else if (key == "concurrent")
                out.spec.concurrent = static_cast<std::uint32_t>(n);
            else if (key == "level")
                out.spec.level = static_cast<std::uint32_t>(n);
            else if (key == "seed")
                out.spec.seedBase = static_cast<std::uint32_t>(n);
            else
                return usage("unknown option '" + key + "'");
        }

        if (out.spec.dungeonToken.empty())
            return usage("missing dungeon");
        if (out.spec.total == 0)
            return usage("missing total=N");

        out.ok = true;
        return out;
    }
}
