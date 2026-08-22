/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTCOMP_H
#define _PLAYERBOT_DCTESTCOMP_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// A test run fields a 5-bot party: one tank, one healer, three DPS. Which
// class/spec fills each role is randomised per run (BuildComp) so successive
// runs of the same dungeon exercise different pull/heal/threat paths and shake
// out class-specific edge cases — the earlier fixed prot-warr / holy-priest /
// mage-rogue-hunter comp only ever tested one shape.
//
// Every spec is named by its playerbots premade-spec template name
// (AiPlayerbot.PremadeSpecName.<class>.<n>) and resolved to a specNo at
// provisioning time; a spec with no matching template fails the run loudly
// (tank/heal) rather than silently rolling a random build, so only specs that
// exist as templates appear in the pools below. Death knights are excluded
// (their level floor breaks low-level dungeons); all-Alliance because the
// addclass pool is picked per team and entrances are faction-agnostic
// teleports.
//
// Randomisation is seeded (BuildComp(seed)) and the seed is recorded, so a
// comp that trips a bug can be replayed exactly via `.dc test start <d> seed=N`.

namespace DcTestComp
{
    struct Slot
    {
        std::uint8_t classId;      // 1=warr 2=pala 3=hunt 4=rogue 5=priest 7=sham 8=mage 9=lock 11=druid
        char const* specName;      // premade-spec template to force
        char const* fallbackSpec;  // substring to match if the exact name is absent
        char const* role;          // "tank" | "heal" | "dps" (for the record/UI)
    };

    inline constexpr std::size_t kPartySize = 5;

    // Every class/spec that can legitimately fill each role. BuildComp draws
    // from these; a class appears once per role even when it offers several
    // specs for it (e.g. priest disc + holy) so both get exercised over time.
    inline constexpr Slot kTankPool[] = {
        { 1,  "prot pve", "prot", "tank" },  // warrior
        { 2,  "prot pve", "prot", "tank" },  // paladin
        { 11, "bear pve", "bear", "tank" },  // druid
    };

    inline constexpr Slot kHealPool[] = {
        { 5,  "holy pve",  "holy",  "heal" },  // priest
        { 5,  "disc pve",  "disc",  "heal" },  // priest
        { 2,  "holy pve",  "holy",  "heal" },  // paladin
        { 7,  "resto pve", "resto", "heal" },  // shaman
        { 11, "resto pve", "resto", "heal" },  // druid
    };

    inline constexpr Slot kDpsPool[] = {
        { 1,  "arms pve",     "arms",     "dps" },  // warrior
        { 1,  "fury pve",     "fury",     "dps" },  // warrior
        { 2,  "ret pve",      "ret",      "dps" },  // paladin
        { 3,  "bm pve",       "bm",       "dps" },  // hunter
        { 3,  "mm pve",       "mm",       "dps" },  // hunter
        { 3,  "surv pve",     "surv",     "dps" },  // hunter
        { 4,  "as pve",       "as",       "dps" },  // rogue
        { 4,  "combat pve",   "combat",   "dps" },  // rogue
        { 4,  "subtlety pve", "subtlety", "dps" },  // rogue
        { 5,  "shadow pve",   "shadow",   "dps" },  // priest
        { 7,  "ele pve",      "ele",      "dps" },  // shaman
        { 7,  "enh pve",      "enh",      "dps" },  // shaman
        { 8,  "arcane pve",   "arcane",   "dps" },  // mage
        { 8,  "fire pve",     "fire",     "dps" },  // mage
        { 8,  "frost pve",    "frost",    "dps" },  // mage
        { 9,  "affli pve",    "affli",    "dps" },  // warlock
        { 9,  "demo pve",     "demo",     "dps" },  // warlock
        { 9,  "destro pve",   "destro",   "dps" },  // warlock
        { 11, "balance pve",  "balance",  "dps" },  // druid
        { 11, "cat pve",      "cat",      "dps" },  // druid
    };

    // Deterministically pick a party for the given seed: one tank, one healer,
    // three DPS, all five on DISTINCT classes (maximises class diversity and
    // keeps the addclass-pool draw from needing several chars of one class).
    // Pure — no globals, no I/O — so the same seed always yields the same comp.
    std::array<Slot, kPartySize> BuildComp(std::uint32_t seed);

    // The pool backing a role token ("tank" | "heal" | "dps"), for callers that
    // must substitute an alternative class when the drawn one has no available
    // pool character. Empty for an unknown token.
    std::vector<Slot> RolePool(std::string_view role);
}

#endif  // _PLAYERBOT_DCTESTCOMP_H
