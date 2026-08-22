/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTROSTER_H
#define _PLAYERBOT_DCTESTROSTER_H

#include <string>
#include <vector>

// Pure parser for `.dc test start <dungeon> party=Tank,Heal,Dps1,Dps2,Dps3`:
// the hand-picked-party form of a test run, where real player characters take
// the five slots instead of randomly drawn addclass-pool bots.
//
// Roles are POSITIONAL — slot 0 tank, slot 1 healer, slots 2-4 dps — mirroring
// the DcTestRunJob::_slots convention (slot 0 is the group leader and the run's
// tank). There is no per-name role syntax on purpose: the Command Deck's roster
// builder emits the order, and a positional list is the one form that cannot
// disagree with itself.
//
// Engine-free (names in, names out) so the grammar and its refusals are
// unit-testable in isolation, the same shape as DcTestRunSelect. Turning names
// into guids, and every liveness/faction check, belongs to the caller — this
// only decides whether the string is a well-formed roster.

namespace DcTestRoster
{
    struct Member
    {
        std::string name;   // as typed, trimmed; caller normalizes for lookup
        char const* role;   // "tank" | "heal" | "dps"
    };

    enum class Kind
    {
        Ok,
        Empty,       // nothing but separators/whitespace
        WrongCount,  // not exactly kPartySize names
        Duplicate    // the same character named twice
    };

    struct Result
    {
        Kind kind;
        std::vector<Member> members;  // filled only when Ok
        std::string detail;           // human sentence for every non-Ok kind
    };

    // Split on commas, trim each name, assign positional roles. Rejects an empty
    // list, a list that isn't exactly five names, and a repeated name (compared
    // case-insensitively — WoW names are unique up to case, so "Bob,bob" is one
    // character asked to fill two slots, which would deadlock the group build).
    Result Parse(std::string const& spec);

    // Positional role for slot `i`; "dps" for anything past the healer.
    char const* RoleForIndex(std::size_t i);
}

#endif  // _PLAYERBOT_DCTESTROSTER_H
