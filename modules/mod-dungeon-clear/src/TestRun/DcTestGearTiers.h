/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTGEARTIERS_H
#define _PLAYERBOT_DCTESTGEARTIERS_H

#include <cstdint>
#include <string>
#include <vector>

// Per-run gear ceiling for `.dc test` runs: which item-level caps are worth
// offering for a given dungeon, and how a per-run choice resolves against the
// server-wide playerbots settings.
//
// Engine-free on purpose (like DcTestPlan / DcTestRunVerdict): the ladder is
// static 3.3.5 data plus arithmetic, so it is unit-testable and the dashboard
// can be served the same list the console would accept.
//
// Why this exists: test bots are geared by PlayerbotFactory to
// AiPlayerbot.AutoGearScoreLimit / AutoGearQualityLimit, which are server-wide.
// Comparing a party in Karazhan-era gear against the same party in Sunwell gear
// meant editing the conf and reloading between runs. A run now carries its own
// ceiling.
namespace DcTestGearTiers
{
    // One offered ceiling. `ilvl` 0 means "no limit" (the same value the
    // playerbots conf uses for it).
    struct Choice
    {
        std::uint32_t ilvl = 0;
        std::string label;
    };

    enum class Expansion : std::uint8_t
    {
        Classic,
        Tbc,
        Wotlk,
    };

    // Expansion a registry map belongs to. Hand-listed rather than read from
    // MapEntry::Expansion so this stays a pure kernel; the lists are the same
    // map ids DcTestDungeonRegistry already enumerates, and 3.3.5 gains no new
    // dungeons.
    Expansion ExpansionOf(std::uint32_t mapId);

    // Item level of the gear a party of `runLevel` would be wearing in that
    // expansion's dungeons — the anchor the "leveling" ladder is built around.
    //
    // Piecewise-linear between two anchors per expansion, checked against
    // acore_world item_template (max ItemLevel for weapons/armour whose
    // RequiredLevel sits in the band PlayerbotFactory draws from):
    //   Classic  level 10 -> ilvl 15   .. level 60 -> ilvl 65   (i.e. level + 5)
    //   TBC      level 58 -> ilvl 65   .. level 70 -> ilvl 115  (heroic-dungeon gear)
    //   WotLK    level 70 -> ilvl 138  .. level 80 -> ilvl 187  (normal-dungeon gear)
    std::uint32_t DungeonGearIlvl(Expansion exp, std::uint32_t runLevel);

    // The ceilings worth offering for a run of `mapId` at `runLevel`.
    //
    // At (or above) an expansion's level cap the ladder is the named raid
    // ceilings from the AiPlayerbot.AutoGearScoreLimit documentation, because
    // those are the numbers a person testing an endgame dungeon thinks in.
    // Below the cap none of those ceilings can be worn, so the ladder is three
    // steps around the dungeon-gear anchor instead — enough to run a leveling
    // dungeon under-geared, in-kind, or over-geared.
    //
    // Never includes "no limit" or "server default"; those are presentation
    // choices the caller adds, not ceilings.
    std::vector<Choice> Ladder(std::uint32_t mapId, std::uint32_t runLevel);

    // What a run asks for. Both fields are overrides: 0 = "whatever the server
    // is configured for", which is what every run did before this existed.
    struct Spec
    {
        // >0 caps at that item level, kNoLimit removes the cap for this run,
        // 0 inherits AiPlayerbot.AutoGearScoreLimit.
        std::int32_t ilvl = 0;
        // 1..5 (normal..legendary), 0 inherits AiPlayerbot.AutoGearQualityLimit.
        std::uint32_t quality = 0;

        bool IsDefault() const { return ilvl == 0 && quality == 0; }
    };

    static constexpr std::int32_t kNoLimit = -1;

    // Resolved, conf-free values the factory is built with. `ilvl` 0 here means
    // no cap — the conf's own encoding, so the resolved pair can be handed
    // straight to PlayerbotFactory. `quality` is always 1..5 (see Resolve).
    struct Resolved
    {
        std::uint32_t ilvl = 0;
        std::uint32_t quality = 3;
    };

    Resolved Resolve(Spec const& spec, std::int32_t confIlvl, std::int32_t confQuality);

    // "epic" / "4" -> 4. 0 when the word names no quality, so callers can tell
    // a bad argument from an omitted one.
    std::uint32_t ParseQuality(std::string const& word);
    char const* QualityName(std::uint32_t quality);

    // "125" -> 125, and "none"/"off"/"unlimited"/"0" -> kNoLimit ("0 = no
    // limit" is the conf's own encoding, so it reads the same way here).
    // Anything else, including out-of-range numbers, -> 0 with *ok false.
    std::int32_t ParseIlvl(std::string const& word, bool* ok);

    // Compact human summary for log lines and status text: "ilvl<=125 epic",
    // "unlimited ilvl", "server default".
    std::string Describe(Spec const& spec);
}

#endif  // _PLAYERBOT_DCTESTGEARTIERS_H
