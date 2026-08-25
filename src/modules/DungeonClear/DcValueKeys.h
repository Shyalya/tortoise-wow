#pragma once

namespace DcKey
{
    inline constexpr char const* DungeonBosses = "dungeon bosses";
    inline constexpr char const* NextDungeonBoss = "next dungeon boss";
    inline constexpr char const* RunState = "dungeon clear run state";
    inline constexpr char const* Skipped = "dungeon clear skipped";
    inline constexpr char const* PartyTank = "dungeon clear party tank";
    inline constexpr char const* PullMode = "dungeon clear pull mode";
    inline constexpr char const* StallReason = "dungeon clear stall reason";
    inline constexpr char const* EventProgress = "dungeon clear event progress";
    inline constexpr char const* ClearedAnchors = "dungeon clear cleared anchors";

    namespace Stock
    {
        inline constexpr char const* CurrentTarget = "current target";
        inline constexpr char const* PossibleTargets = "possible targets";
        inline constexpr char const* Attackers = "attackers";
        inline constexpr char const* AvailableLoot = "available loot";
        inline constexpr char const* HasAvailableLoot = "has available loot";
        inline constexpr char const* Health = "health";
        inline constexpr char const* Mana = "mana";
    }
}
