/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestGearTiers.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace DcTestGearTiers
{
    namespace
    {
        // Level at which an expansion's gear ladder stops being about leveling
        // and starts being about raid tiers.
        std::uint32_t CapLevelOf(Expansion exp)
        {
            switch (exp)
            {
                case Expansion::Classic: return 60;
                case Expansion::Tbc:     return 70;
                case Expansion::Wotlk:   return 80;
            }
            return 60;
        }

        std::uint32_t Lerp(std::uint32_t loLevel, std::uint32_t loIlvl, std::uint32_t hiLevel,
                           std::uint32_t hiIlvl, std::uint32_t level)
        {
            level = std::clamp(level, loLevel, hiLevel);
            std::uint32_t const span = hiLevel - loLevel;
            if (span == 0)
                return hiIlvl;
            return loIlvl + ((hiIlvl - loIlvl) * (level - loLevel) + span / 2) / span;
        }

        std::string WithIlvl(std::string const& text, std::uint32_t ilvl)
        {
            return text + " (" + std::to_string(ilvl) + ")";
        }

        // The named ceilings from the AiPlayerbot.AutoGearScoreLimit comment
        // block, verbatim — a person picking one has read that table (or the
        // wiki page it came from) and expects these exact numbers. Tier and
        // phase names are merged where they land on the same item level.
        std::vector<Choice> NamedTiers(Expansion exp)
        {
            switch (exp)
            {
                case Expansion::Classic:
                    return {
                        {66, "Tier 1 (66)"},
                        {76, "Tier 2 (76)"},
                        {78, "Phase 1 — MC/Ony/ZG (78)"},
                        {83, "Phase 2 — BWL (83)"},
                        {88, "Tier 2.5 — AQ40 (88)"},
                        {92, "Tier 3 — Naxx40 (92)"},
                    };
                case Expansion::Tbc:
                    return {
                        {120, "Tier 4 (120)"},
                        {125, "Phase 1 — Kara/Gruul/Mag (125)"},
                        {133, "Tier 5 (133)"},
                        {141, "Phase 2 — SSC/TK/ZA (141)"},
                        {156, "Phase 3 — Hyjal/BT (156)"},
                        {164, "Tier 6 — Sunwell (164)"},
                    };
                case Expansion::Wotlk:
                    return {
                        {200, "Tier 7 10-man (200)"},
                        {213, "Tier 7 25-man (213)"},
                        {224, "Phase 1 — Naxx (224)"},
                        {232, "Tier 8 25-man (232)"},
                        {245, "Phase 2 — Ulduar (245)"},
                        {251, "Tier 10 10-man (251)"},
                        {258, "Phase 3 — ToC (258)"},
                        {264, "Tier 10 25-man (264)"},
                        {290, "Phase 4 — ICC heroic (290)"},
                    };
            }
            return {};
        }
    }

    Expansion ExpansionOf(std::uint32_t mapId)
    {
        // Same map ids DcTestDungeonRegistry lists, grouped by expansion. The
        // Black Morass (269) and Old Hillsbrad (560) are Caverns of Time maps
        // but TBC content, and are geared as such.
        static std::set<std::uint32_t> const tbc =
            {269, 540, 542, 543, 545, 546, 547, 552, 553, 554, 555, 556, 557, 558, 560, 585};
        static std::set<std::uint32_t> const wotlk =
            {574, 575, 576, 578, 595, 599, 600, 601, 602, 604, 608, 619, 632, 650, 658, 668};

        if (tbc.count(mapId))
            return Expansion::Tbc;
        if (wotlk.count(mapId))
            return Expansion::Wotlk;
        return Expansion::Classic;
    }

    std::uint32_t DungeonGearIlvl(Expansion exp, std::uint32_t runLevel)
    {
        switch (exp)
        {
            case Expansion::Classic: return Lerp(10, 15, 60, 65, runLevel);
            case Expansion::Tbc:     return Lerp(58, 65, 70, 115, runLevel);
            case Expansion::Wotlk:   return Lerp(70, 138, 80, 187, runLevel);
        }
        return 0;
    }

    std::vector<Choice> Ladder(std::uint32_t mapId, std::uint32_t runLevel)
    {
        Expansion const exp = ExpansionOf(mapId);
        std::uint32_t const anchor = DungeonGearIlvl(exp, runLevel);

        if (runLevel < CapLevelOf(exp))
        {
            // Leveling dungeon: no named tier is wearable here, so offer three
            // steps around what the dungeon itself drops. The percentages are
            // one rule for all three expansions rather than three hand-tuned
            // tables — ±10% of the anchor is about one gear grade either way.
            //
            // Floored at ±3 item levels: in classic's low teens a percentage
            // band collapses to one or two ilvl, which is three choices that
            // gear the party identically. 3 is enough to be a different grade
            // of item there (a level-18 party in ilvl 20 vs 26 is visibly
            // different), and the percentage takes over well before it matters.
            std::uint32_t const under = std::min(anchor - 3, (anchor * 94 + 50) / 100);
            std::uint32_t const over = std::max(anchor + 3, (anchor * 110 + 50) / 100);
            return {
                {under, WithIlvl("under-geared — quest greens", under)},
                {anchor, WithIlvl("in-kind — dungeon blues", anchor)},
                {over, WithIlvl("over-geared — best available", over)},
            };
        }

        // At the level cap: the named raid ceilings, led by the dungeon gear a
        // party arrives in (the pre-raid floor — TBC's is 115, i.e. heroic
        // 5-mans). Dropped when a named tier already covers it, so the list
        // never carries two entries meaning the same thing.
        std::vector<Choice> out = NamedTiers(exp);
        if (!out.empty() && anchor < out.front().ilvl)
            out.insert(out.begin(), {anchor, WithIlvl("pre-raid — dungeon gear", anchor)});
        return out;
    }

    Resolved Resolve(Spec const& spec, std::int32_t confIlvl, std::int32_t confQuality)
    {
        Resolved out;
        if (spec.ilvl == kNoLimit)
            out.ilvl = 0;
        else if (spec.ilvl > 0)
            out.ilvl = static_cast<std::uint32_t>(spec.ilvl);
        else
            out.ilvl = confIlvl > 0 ? static_cast<std::uint32_t>(confIlvl) : 0;

        // Quality must never resolve to 0. PlayerbotFactory treats a 0 quality
        // argument as "nothing was asked for" and replaces BOTH the quality and
        // the gear-score limit with the RandomGear* config values — which would
        // throw away the item-level cap resolved just above. 3 (rare) is the
        // documented AutoGearQualityLimit default, so an unset/zeroed conf
        // lands on the same place the conf comment says it does.
        if (spec.quality > 0)
            out.quality = spec.quality;
        else
            out.quality = confQuality > 0 ? static_cast<std::uint32_t>(confQuality) : 3;
        return out;
    }

    std::uint32_t ParseQuality(std::string const& word)
    {
        if (word == "1" || word == "normal")
            return 1;
        if (word == "2" || word == "uncommon" || word == "green")
            return 2;
        if (word == "3" || word == "rare" || word == "blue")
            return 3;
        if (word == "4" || word == "epic" || word == "purple")
            return 4;
        if (word == "5" || word == "legendary")
            return 5;
        return 0;
    }

    char const* QualityName(std::uint32_t quality)
    {
        switch (quality)
        {
            case 1: return "normal";
            case 2: return "uncommon";
            case 3: return "rare";
            case 4: return "epic";
            case 5: return "legendary";
            default: return "server default";
        }
    }

    std::int32_t ParseIlvl(std::string const& word, bool* ok)
    {
        auto done = [&](std::int32_t v, bool good) -> std::int32_t
        {
            if (ok)
                *ok = good;
            return good ? v : 0;
        };

        if (word == "none" || word == "off" || word == "unlimited" || word == "0")
            return done(kNoLimit, true);

        char* end = nullptr;
        unsigned long const n = std::strtoul(word.c_str(), &end, 10);
        if (word.empty() || !end || *end != '\0')
            return done(0, false);
        // Upper bound is the highest ceiling the conf documents (ICC heroic,
        // 290) with headroom for custom content; a typo'd 12500 is refused
        // rather than silently meaning "no limit".
        if (n < 1 || n > 400)
            return done(0, false);
        return done(static_cast<std::int32_t>(n), true);
    }

    std::string Describe(Spec const& spec)
    {
        if (spec.IsDefault())
            return "server default";

        std::string out;
        if (spec.ilvl == kNoLimit)
            out = "unlimited ilvl";
        else if (spec.ilvl > 0)
            out = "ilvl<=" + std::to_string(spec.ilvl);

        if (spec.quality > 0)
        {
            if (!out.empty())
                out += ' ';
            out += QualityName(spec.quality);
        }
        return out;
    }
}
