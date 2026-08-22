/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestRoster.h"

#include <algorithm>
#include <cctype>

#include "TestRun/DcTestComp.h"

namespace DcTestRoster
{
    namespace
    {
        std::string Trim(std::string const& in)
        {
            std::size_t const b = in.find_first_not_of(" \t");
            if (b == std::string::npos)
                return {};
            std::size_t const e = in.find_last_not_of(" \t");
            return in.substr(b, e - b + 1);
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }
    }

    char const* RoleForIndex(std::size_t i)
    {
        if (i == 0)
            return "tank";
        if (i == 1)
            return "heal";
        return "dps";
    }

    Result Parse(std::string const& spec)
    {
        std::vector<std::string> names;
        std::size_t from = 0;
        while (from <= spec.size())
        {
            std::size_t const comma = spec.find(',', from);
            std::string const piece =
                Trim(comma == std::string::npos ? spec.substr(from) : spec.substr(from, comma - from));
            // Skip empties rather than counting them: a trailing comma or a
            // double separator is a typo in the list, not a sixth member.
            if (!piece.empty())
                names.push_back(piece);
            if (comma == std::string::npos)
                break;
            from = comma + 1;
        }

        if (names.empty())
            return {Kind::Empty, {}, "party= needs 5 character names, e.g. "
                                     "party=Tankname,Healname,Dps1,Dps2,Dps3"};

        if (names.size() != DcTestComp::kPartySize)
            return {Kind::WrongCount, {},
                    "party= needs exactly " + std::to_string(DcTestComp::kPartySize) +
                        " names (tank,heal,dps,dps,dps) — got " + std::to_string(names.size())};

        for (std::size_t i = 0; i < names.size(); ++i)
            for (std::size_t j = i + 1; j < names.size(); ++j)
                if (Lower(names[i]) == Lower(names[j]))
                    return {Kind::Duplicate, {},
                            "'" + names[i] + "' is named twice — one character cannot fill two slots"};

        Result out{Kind::Ok, {}, {}};
        out.members.reserve(names.size());
        for (std::size_t i = 0; i < names.size(); ++i)
            out.members.push_back({names[i], RoleForIndex(i)});
        return out;
    }
}
