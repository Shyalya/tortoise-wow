/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCVALUEKEYS_H
#define _PLAYERBOT_DCVALUEKEYS_H

// Symbolic names for every engine value the dungeon-clear module reads or writes.
//
// The playerbots AiObjectContext resolves values by string name; a misspelled or
// renamed key is NOT a compile error — GetValue<T>("typo") silently returns a
// default-constructed value. Every `GetValue<T>(...)`/`AI_VALUE(...)` call site and
// every value *creator* (DungeonClearValueContext) and value *constructor* uses a
// constant from here, so a typo or rename becomes a compile error and the producer
// and all consumers share one symbol. The underlying string values are unchanged —
// this is a naming layer, not a behavior change.
//
// `char const*` (not std::string_view) so the constants drop straight into the
// GetValue<T>(char const*) / AI_VALUE(char const*) call sites with no `.data()`
// churn and no allocation.
//
// Scope: DungeonClear-owned VALUES only. Trigger/action/strategy names are a separate
// namespace (they fail loud at runtime as "unknown action", unlike values) and are
// not covered here.
namespace DcKey
{
    // --- DungeonClear-owned values (registered by DungeonClearValueContext) -------
    inline constexpr char const* DungeonBosses           = "dungeon bosses";
    inline constexpr char const* NextDungeonBoss         = "next dungeon boss";
    inline constexpr char const* LiveBoss                = "dungeon clear live boss";
    // Leader-owned run-level state (enabled, paused + pause cluster, selected-boss
    // override, the two leader-fight latches) consolidated into one DcRunState value
    // — see DcRunState.h. The former standalone "dungeon clear enabled/paused/pause
    // reason/paused door/selected boss" values were folded in; access via DcRun::Of.
    inline constexpr char const* RunState                = "dungeon clear run state";
    inline constexpr char const* Skipped                 = "dungeon clear skipped";
    inline constexpr char const* ClearedAnchors          = "dungeon clear cleared anchors";
    inline constexpr char const* SeenBosses              = "dungeon clear seen bosses";
    inline constexpr char const* SeenDueEvents           = "dungeon clear seen due events";
    inline constexpr char const* StickyBoss              = "dungeon clear sticky boss";
    inline constexpr char const* RunInstance             = "dungeon clear run instance";
    inline constexpr char const* StallReason             = "dungeon clear stall reason";
    inline constexpr char const* LastSaidReason          = "dungeon clear last said reason";
    inline constexpr char const* Phase                   = "dungeon clear phase";
    inline constexpr char const* FallbackTarget          = "dungeon clear fallback target";
    inline constexpr char const* PartyTank               = "dungeon clear party tank";
    inline constexpr char const* LongPath                = "dungeon clear long path";
    inline constexpr char const* CurrentHop              = "dungeon clear current hop";
    inline constexpr char const* FarTargets              = "dungeon clear far targets";
    inline constexpr char const* Hazards                 = "dungeon clear hazards";
    // Ground pools (persistent-area-aura DynamicObjects) near the bot. A SEPARATE
    // key from Hazards because the guids resolve through a different accessor —
    // ObjectAccessor::GetDynamicObject, not GetUnit — and a DynamicObject guid
    // fed to the unit resolver is a silent nullptr, i.e. a hazard that reads as
    // clean ground. See DungeonClearGroundHazardsValue.
    inline constexpr char const* GroundHazards           = "dungeon clear ground hazards";
    // Ground TRAPS (GameObjects) near the bot — the Shattered Halls Blaze. A
    // third key for the third resolver: a GameObject guid is neither a Unit nor a
    // DynamicObject, so both of the accessors above return nullptr on it and the
    // fire reads as clean ground. See DungeonClearTrapHazardsValue.
    inline constexpr char const* TrapHazards             = "dungeon clear trap hazards";
    inline constexpr char const* RoomTrashRemaining      = "dungeon clear room trash remaining";
    inline constexpr char const* BlockingDoor            = "dungeon clear blocking door";
    inline constexpr char const* EngageTrashTarget       = "dungeon clear engage trash target";
    inline constexpr char const* FollowerState           = "dungeon clear follower state";
    inline constexpr char const* SwimState               = "dungeon clear swim state";
    inline constexpr char const* LootSkip                = "dungeon clear loot skip";
    inline constexpr char const* LootCampGuid            = "dungeon clear loot camp guid";
    inline constexpr char const* LootCampStart           = "dungeon clear loot camp start";
    inline constexpr char const* FollowedTank            = "dungeon clear followed tank";
    inline constexpr char const* PullMode                = "dungeon clear pull mode";
    inline constexpr char const* PullModeCurrent         = "dungeon clear pull mode current";
    inline constexpr char const* PullTarget              = "dungeon clear pull target";
    inline constexpr char const* HealTarget              = "dungeon clear heal target";
    inline constexpr char const* PullSetting             = "dungeon clear pull setting";
    inline constexpr char const* PullContext             = "dungeon clear pull context";
    inline constexpr char const* ApproachState           = "dungeon clear approach state";
    inline constexpr char const* TickMemo                = "dungeon clear tick memo";
    inline constexpr char const* EventProgress           = "dungeon clear event progress";
    inline constexpr char const* ConditionalEventProgress = "dungeon clear conditional event progress";

    // --- Stock-playerbots values we consume --------------------------------------
    // Mirrored here so the runtime dependency on upstream value names is greppable
    // in one place (a rename upstream is a silent break — Plan 4's startup existence
    // probe would key off these).
    namespace Stock
    {
        inline constexpr char const* CurrentTarget     = "current target";
        inline constexpr char const* PossibleTargets   = "possible targets";
        inline constexpr char const* Attackers         = "attackers";
        inline constexpr char const* LootTarget        = "loot target";
        inline constexpr char const* AvailableLoot     = "available loot";
        inline constexpr char const* HasAvailableLoot  = "has available loot";
        inline constexpr char const* CanLoot           = "can loot";
        inline constexpr char const* PartyToHeal       = "party member to heal";
        inline constexpr char const* LastMovement      = "last movement";
        inline constexpr char const* SpellId           = "spell id";
        inline constexpr char const* SelfTarget        = "self target";
        inline constexpr char const* Health            = "health";
        inline constexpr char const* BagSpace          = "bag space";
        // The negative dynamic-object aura the bot is standing in RIGHT NOW (null
        // when it is on clean ground). AreaDebuffValue only returns an aura the bot
        // actually has, so it is an "am I in it", not an "is one nearby" — which is
        // exactly the question a camp leash has to ask before hauling anyone home.
        inline constexpr char const* AreaDebuff        = "area debuff";
    }
}

#endif
