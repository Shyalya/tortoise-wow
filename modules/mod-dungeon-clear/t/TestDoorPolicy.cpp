/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <initializer_list>

#include "Ai/Dungeon/DungeonClear/Data/DcEventDoorRegistry.h"
#include "DcDoorPolicy.h"

// Fixtures are REAL Lock.dbc rows (decoded from the 3.3.5 client data) for the
// doors that drove the door-handling overhaul:
//
//   lock  85 — Deadmines Factory/Foundry/Mast Room doors, SM doors: an empty
//              lock row, zero requirements. Anyone opens with a click.
//   lock  86 — Deadmines Heavy Doors: single Quick Open slot, skill 0.
//   lock 202 — Deadmines Iron Clad Door: picklock 1 / quick open / blasting 50.
//   lock 299 — SM Herod's Door, Strat Scarlet-side doors: Scarlet Key (7146)
//              or picklock 175 (+ bare-hands and blasting slots).
//   lock 879 — Strat King's Square / Gauntlet / Service gates: Key to the City
//              (12382) or picklock 300 (+ bare-hands and blasting slots).

namespace
{
    using DcDoorPolicy::LockSlot;

    struct LockFixture
    {
        LockSlot slots[DcDoorPolicy::LOCK_SLOT_COUNT];
    };

    LockFixture MakeLock(std::initializer_list<LockSlot> typed)
    {
        LockFixture f;
        std::size_t i = 0;
        for (LockSlot const& s : typed)
            f.slots[i++] = s;
        return f;
    }

    // type / index / requiredSkill per slot, matching the DBC column order.
    LockFixture const LOCK_85 = MakeLock({});
    LockFixture const LOCK_86 = MakeLock({{LOCK_KEY_SKILL, LOCKTYPE_QUICK_OPEN, 0}});
    LockFixture const LOCK_202 = MakeLock({{LOCK_KEY_SKILL, LOCKTYPE_PICKLOCK, 1},
                                           {LOCK_KEY_SKILL, LOCKTYPE_QUICK_OPEN, 0},
                                           {LOCK_KEY_SKILL, LOCKTYPE_BLASTING, 50}});
    LockFixture const LOCK_299 = MakeLock({{LOCK_KEY_ITEM, 7146, 0},
                                           {LOCK_KEY_SKILL, LOCKTYPE_PICKLOCK, 175},
                                           {LOCK_KEY_SKILL, LOCKTYPE_QUICK_OPEN, 0},
                                           {LOCK_KEY_SKILL, LOCKTYPE_QUICK_CLOSE, 0},
                                           {LOCK_KEY_SKILL, LOCKTYPE_BLASTING, 175}});
    LockFixture const LOCK_879 = MakeLock({{LOCK_KEY_ITEM, 12382, 0},
                                           {LOCK_KEY_SKILL, LOCKTYPE_PICKLOCK, 300},
                                           {LOCK_KEY_SKILL, LOCKTYPE_QUICK_OPEN, 0},
                                           {LOCK_KEY_SKILL, LOCKTYPE_QUICK_CLOSE, 0},
                                           {LOCK_KEY_SKILL, LOCKTYPE_BLASTING, 300}});

    bool CanOpen(LockFixture const& f, bool lockEnforced,
                 uint32 heldItem = 0, int32 lockpick = -1)
    {
        return DcDoorPolicy::CanOpenSlots(
            f.slots, DcDoorPolicy::LOCK_SLOT_COUNT, lockEnforced,
            [heldItem](uint32 entry) { return heldItem && entry == heldItem; },
            lockpick);
    }
}

// An empty lock row imposes no requirement: anyone opens it, and the
// GO_FLAG_LOCKED flag changes nothing because there is nothing to enforce.
// This is the Deadmines Factory/Foundry/Mast Room case the old gate refused.
TEST(DcDoorPolicyTest, EmptyLockOpensForAnyone)
{
    EXPECT_TRUE(CanOpen(LOCK_85, /*lockEnforced*/ false));
    EXPECT_TRUE(CanOpen(LOCK_85, /*lockEnforced*/ true));
}

// A bare-hands locktype (Quick Open) opens for anyone when the GO is not
// flagged locked — the Deadmines Heavy Door case.
TEST(DcDoorPolicyTest, QuickOpenOpensBareHandedWhenUnenforced)
{
    EXPECT_TRUE(CanOpen(LOCK_86, /*lockEnforced*/ false));
}

// The same bare-hands slot does NOT count on a GO_FLAG_LOCKED door: flagged
// gates demand the real key/skill slot.
TEST(DcDoorPolicyTest, QuickOpenSuppressedWhenLockEnforced)
{
    EXPECT_FALSE(CanOpen(LOCK_86, /*lockEnforced*/ true));
}

// Strat King's Square Gate (flagged locked): bare hands fail despite the
// Quick Open slot; the Key to the City or lockpicking 300 succeed.
TEST(DcDoorPolicyTest, StratGateNeedsKeyOrLockpicking)
{
    EXPECT_FALSE(CanOpen(LOCK_879, /*lockEnforced*/ true));
    EXPECT_TRUE(CanOpen(LOCK_879, /*lockEnforced*/ true, /*heldItem*/ 12382));
    EXPECT_TRUE(CanOpen(LOCK_879, /*lockEnforced*/ true, 0, /*lockpick*/ 300));
    EXPECT_FALSE(CanOpen(LOCK_879, /*lockEnforced*/ true, 0, /*lockpick*/ 299));
}

// Herod's Door: Scarlet Key or lockpicking 175. The wrong key does not open.
TEST(DcDoorPolicyTest, HerodsDoorKeyOrLockpicking)
{
    EXPECT_FALSE(CanOpen(LOCK_299, /*lockEnforced*/ true));
    EXPECT_TRUE(CanOpen(LOCK_299, /*lockEnforced*/ true, /*heldItem*/ 7146));
    EXPECT_FALSE(CanOpen(LOCK_299, /*lockEnforced*/ true, /*heldItem*/ 12382));
    EXPECT_TRUE(CanOpen(LOCK_299, /*lockEnforced*/ true, 0, /*lockpick*/ 175));
}

// Iron Clad Door (flagged locked): only a rogue's lockpicking gets through —
// the blasting slot (seaforium) is not modelled, the quick-open slot is
// suppressed by the flag, so everyone else parks and waits for the cannon.
TEST(DcDoorPolicyTest, IronCladDoorOnlyLockpicking)
{
    EXPECT_FALSE(CanOpen(LOCK_202, /*lockEnforced*/ true));
    EXPECT_TRUE(CanOpen(LOCK_202, /*lockEnforced*/ true, 0, /*lockpick*/ 1));
    EXPECT_FALSE(CanOpen(LOCK_202, /*lockEnforced*/ true, 0, /*lockpick*/ 0));
}

// A spell-keyed slot is an unsatisfiable requirement for bots, not a free
// pass through the "no requirement seen" fallthrough.
TEST(DcDoorPolicyTest, SpellSlotIsARequirement)
{
    LockFixture const spellLock = MakeLock({{LOCK_KEY_SPELL, 12345, 0}});
    EXPECT_FALSE(CanOpen(spellLock, /*lockEnforced*/ false));
}

// An item slot with index 0 still marks the lock as requiring something
// (mirrors Spell::CanOpenLock's reqKey bookkeeping).
TEST(DcDoorPolicyTest, ZeroItemIndexStillARequirement)
{
    LockFixture const oddLock = MakeLock({{LOCK_KEY_ITEM, 0, 0}});
    EXPECT_FALSE(CanOpen(oddLock, /*lockEnforced*/ false));
}

// --- Key-exempt allowlist ---------------------------------------------------
//
// SM's Armory (Herod's Door) and Cathedral (Chapel Door) both ride lock 299,
// which the policy above correctly refuses for a keyless, non-rogue tank. The
// registry waives that requirement per GO ENTRY so those wings stay clearable.
TEST(DcDoorPolicyTest, ScarletMonasteryWingDoorsAreKeyExempt)
{
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(101854));   // Herod's Door
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(104591));   // Chapel Door

    // Not a blanket amnesty: everything else still goes through CanOpenSlots.
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(104600));  // High Inquisitor's (lock 85 already)
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(18895));   // SFK courtyard (script-only)
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(175611));  // Scholomance Iron Gate
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(0));
}

// Every keyed DOOR in Dire Maul North, Scholomance and Stratholme is exempt too
// (2026-08-08). All are plain traversal gates on a key/lockpicking lock with
// GO_FLAG_LOCKED set — which is precisely what CanOpenSlots refuses — and none
// is driven by an instance script; see the registry header for the per-door
// verification. Without the waiver a keyless party auto-paused at each of them.
TEST(DcDoorPolicyTest, ScholomanceStratholmeAndDireMaulNorthKeyedDoorsAreExempt)
{
    // Scholomance: the one keyed door inside, plus Caer Darrow's entrance door.
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(175167));   // Viewing Room Door
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(174626));   // Scholomance Door (map 0)

    // Stratholme, Scarlet side (lock 299, The Scarlet Key) — these used to be
    // the reason the exemption was kept per-entry rather than per-lock.
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(175967));   // The Bastion Door
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(175968));   // Hoard Door
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(176194));   // Hall of the High Command

    // Stratholme, undead side (lock 879, Key to the City).
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(175352));   // King's Square Gate
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(175353));   // King's Square Gate
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(175356));   // Gauntlet Gate
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(175357));   // Gauntlet Gate
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(175368));   // Service Entrance Gate

    // Dire Maul North: the two Gordok doors (also covered by map-429 events 2/3)
    // and the North wing's Crescent Key door, which has no event.
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(177219));   // Gordok Courtyard Door
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(177217));   // Gordok Inner Door
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(179549));   // DM North Crescent Key door

    // Still scoped to DOORS in those dungeons, and still not a lock-level rule:
    // the script-driven gates and the keyed non-door objects stay untouched.
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(175570));  // Scholo Kirtonos gate (script)
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(177371));  // Scholo Gandling gate (script)
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(175564));  // Scholo Brazier of the Herald (button)
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(175380));  // Strat ziggurat door (instance script)
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(176346));  // Strat Market Row Postbox (button)
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(176216));  // Strat Scarlet Cannon (goober)
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(124372));  // Uldaman Ironaya seal
}

// Every lock-680 (Shadowforge Key 11000 / lockpicking 250) door in Blackrock
// Depths, not just the two on the lever's doorstep. Waiving them one at a time
// only walks the auto-pause down the corridor: test plan tp-20260817-171356-1
// shipped with 170570 + 161460 exempt and still lost 6 of 10 runs at 10/20
// bosses to "can't open ... 170560" — the Shadowforge Gate one room earlier.
// The Lyceum is the same gate again for the back half of the dungeon: Flamelash,
// The Seven, Magmus and the Emperor are all behind it.
TEST(DcDoorPolicyTest, BlackrockLock680TraversalGatesAreKeyExempt)
{
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(170559));   // Shadowforge Gate (west)
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(170560));   // Shadowforge Gate (east)
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(170570));   // East Garrison Door
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(170558));   // The Lyceum
    EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(161460));   // The Shadowforge Lock

    // Still not a lock-level amnesty, and still not a map-level one: the doors
    // instance_blackrock_depths caches AND drives stay script territory whatever
    // lock they ride, and the Vault's loot cells are not a corridor at all.
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(170571));  // Bar Door (GO_BAR_DOOR)
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(170573));  // Golem Room North
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(170575));  // Throne Room Doors (Magmus)
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(170576));  // Tomb of the Seven, in
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(174554));  // Relic Coffer Door
}

// The Detention Block's eight cell doors, lock 699 (Prison Cell Key 11140 /
// lockpicking 250). Same screen as the lock-680 gates: type DOOR, addon flags
// 34, no ScriptName, no AIName, no smart_scripts or conditions row, and absent
// from instance_blackrock_depths entirely — its door enum stops at the Lyceum.
// The boss route runs through the cells, so a shut one is a hard stop: 170567
// auto-paused a tp-20260817-171356-1 run at 2/20 bosses, parked 0.0yd inside the
// doorway on a route the diag still called ok/1seg dev=0.5.
TEST(DcDoorPolicyTest, BlackrockDetentionBlockCellDoorsAreKeyExempt)
{
    for (uint32 cellDoor = 170562; cellDoor <= 170569; ++cellDoor)
        EXPECT_TRUE(DcEventDoorRegistry::IsKeyExempt(cellDoor)) << "cell door " << cellDoor;

    // The entries bracketing the cell-door run are unrelated and stay put.
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(170561));  // Supply Room Door (lock-free)
}

// --- Script-only denylist ---------------------------------------------------
//
// All three Shadowfang Keep gates ride the same empty lock 85 that CanOpenSlots
// correctly rates "opens for anyone" — and all three are opened by SmartAI, not
// by a click. Without the per-entry denylist the door-blocked action force-opens
// each of them and skips its mechanic:
//   18895 Courtyard Door — opened by the freed prisoner (Ashcrombe / Adamant).
//   18972 Sorcerer's Gate — opened when the first of Arugal's Voidwalkers dies,
//         ~6s after Fenrus. Clicking it walked the party out of the room before
//         the adds even existed; they then spawned behind it and the run wedged.
//   18971 Arugal's Lair — opened when Wolf Master Nandos dies. Nandos stands
//         2.6yd in front of it, so clicking it lets the party walk past him
//         straight to Archmage Arugal and skip an encounter.
TEST(DcDoorPolicyTest, ShadowfangKeepEventGatesAreScriptOnly)
{
    EXPECT_TRUE(DcEventDoorRegistry::IsScriptOnly(18895));   // Courtyard Door
    EXPECT_TRUE(DcEventDoorRegistry::IsScriptOnly(18972));   // Sorcerer's Gate
    EXPECT_TRUE(DcEventDoorRegistry::IsScriptOnly(18971));   // Arugal's Lair

    // Not a blanket refusal of lock-85 doors: the plainly clickable ones the
    // tank must keep opening stay openable.
    EXPECT_FALSE(DcEventDoorRegistry::IsScriptOnly(104600));  // SM High Inquisitor's
    EXPECT_FALSE(DcEventDoorRegistry::IsScriptOnly(183049));  // Steamvault Main Chambers
    EXPECT_FALSE(DcEventDoorRegistry::IsScriptOnly(0));
}

// --- Navigation-ignored allowlist -------------------------------------------
//
// The Steamvault's Main Chambers Access Panels are wall CONTROLS wearing a
// GAMEOBJECT_TYPE_DOOR template. They spawn in GO_STATE_READY and never leave
// it (their script's OnGossipHello returns true before GameObject::Use reaches
// UseDoorOrButton), so the closed-door predicate reads them as shut corridor
// gates forever. Left unlisted, the door-blocked action clicks one, sees it
// still "closed", and auto-pauses the run at the panel — which is exactly where
// the map-545 event needs the tank to be standing.
TEST(DcDoorPolicyTest, SteamvaultAccessPanelsAreNavigationIgnored)
{
    EXPECT_TRUE(DcEventDoorRegistry::IsNavigationIgnored(184125));  // Thespia's panel
    EXPECT_TRUE(DcEventDoorRegistry::IsNavigationIgnored(184126));  // Steamrigger's panel

    // The real gate the panels open stays an ordinary blocking door: if it is
    // still shut when the party heads for Kalithresh, that IS a genuine stall
    // worth pausing on.
    EXPECT_FALSE(DcEventDoorRegistry::IsNavigationIgnored(183049));  // Main Chambers Door

    // Still not a blanket amnesty.
    EXPECT_FALSE(DcEventDoorRegistry::IsNavigationIgnored(18895));   // SFK courtyard
    EXPECT_FALSE(DcEventDoorRegistry::IsNavigationIgnored(0));
}

// Blackrock Depths' Giant Doors are ONE machine spread over four
// GAMEOBJECT_TYPE_DOOR entries, and only the lever is meant to be clicked. The
// three moving parts are driven entirely by the lever's SmartAI, and their
// states are inverted with respect to each other, so whichever way the machine
// stands one of them sits in GO_STATE_READY on the corridor and reads as a shut
// gate. Run tr-20260817-044457-30 died on exactly that: the Fake Collision hull
// (161462), flagged "as corridor-blocking" 78.1yd out on the passage to
// Bael'Gar, auto-paused the run at 9/19 bosses. Closing the doors — the whole
// point of map-230 event 2 — would have moved the same stall onto 157923.
TEST(DcDoorPolicyTest, BlackrockGiantDoorApparatusIsNavigationIgnored)
{
    EXPECT_TRUE(DcEventDoorRegistry::IsNavigationIgnored(157923));  // Giant Doors
    EXPECT_TRUE(DcEventDoorRegistry::IsNavigationIgnored(161461));  // Giant Door Mechanism
    EXPECT_TRUE(DcEventDoorRegistry::IsNavigationIgnored(161462));  // Fake Collision
    EXPECT_TRUE(DcEventDoorRegistry::IsNavigationIgnored(161516));  // BigDoorDummyCollision02

    // The lever itself is NOT navigation-ignored — it is the one part of the
    // machine the run must actually reach and click.
    EXPECT_FALSE(DcEventDoorRegistry::IsNavigationIgnored(161460));  // The Shadowforge Lock
    // Nor is the door into the room that holds it.
    EXPECT_FALSE(DcEventDoorRegistry::IsNavigationIgnored(170570));  // East Garrison Door
}

// Utgarde Keep's forge hall is a ring cut into three sectors by three walls of
// fire, and each wall is a GAMEOBJECT_TYPE_DOOR whose state only
// instance_utgarde_keep drives — SetData(DATA_FORGE_n) on the matching forge
// master's death. The master that opens a wall stands on the party's side of
// it, so there is never anything to solve AT the wall, yet the model is a ~60yd
// slab lying across the ring and the closed-door predicate reads it as a shut
// gate on the corridor. Runs tr-20260818-070705-4 and -7 died on exactly that:
// 186692 flagged "as corridor-blocking" 98yd out, walk-in, "can't open ->
// auto-pausing", 0/3 bosses with the tank still 57yd short of forge 1.
TEST(DcDoorPolicyTest, UtgardeKeepForgeFlameWallsAreNavigationIgnored)
{
    EXPECT_TRUE(DcEventDoorRegistry::IsNavigationIgnored(186691));  // ForgeFire_Third
    EXPECT_TRUE(DcEventDoorRegistry::IsNavigationIgnored(186692));  // ForgeFire_First
    EXPECT_TRUE(DcEventDoorRegistry::IsNavigationIgnored(186693));  // ForgeFire_Second

    // The Giant Portcullises that Ingvar's death opens are NOT ignored: they
    // are ordinary progress gates on the way out, and a run still standing at
    // one after the encounter has a real stall worth pausing on.
    EXPECT_FALSE(DcEventDoorRegistry::IsNavigationIgnored(186694));
    EXPECT_FALSE(DcEventDoorRegistry::IsNavigationIgnored(186756));

    // No other list claims them: they carry no lock and no key, and they never
    // reopen on a timer — only a kill opens one.
    for (uint32 entry : { 186691u, 186692u, 186693u })
    {
        EXPECT_FALSE(DcEventDoorRegistry::IsScriptOnly(entry));
        EXPECT_FALSE(DcEventDoorRegistry::IsSelfClearing(entry));
        EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(entry));
        EXPECT_FALSE(DcEventDoorRegistry::IsLockFreeClickable(entry));
    }
}

// --- Self-clearing script barriers ------------------------------------------
//
// Stratholme's two gate traps. instance_stratholme watches (3612.3,-3335.4)
// Scarlet side and (3919.9,-3547.3) undead side; a non-GM player within 5.5yd
// slams the matching PAIR of portcullises shut, spawns plagued critters 2s
// later, and reopens both gates 20s after that. Nothing to click, nothing for a
// player to solve, and a hard 20s ceiling — so the door-blocked action must hold
// rather than auto-pause. Run tr-20260816-151006-14 walked its tank over the
// Crusaders' Square trigger and auto-paused 13.1yd from the portcullis, burning
// 36s of a 60s pause budget waiting for a gate that was always going to reopen.
TEST(DcDoorPolicyTest, StratholmeGateTrapPortcullisesAreSelfClearing)
{
    EXPECT_TRUE(DcEventDoorRegistry::IsSelfClearing(175350));  // trap 1, Scarlet
    EXPECT_TRUE(DcEventDoorRegistry::IsSelfClearing(175351));  // trap 1, Scarlet
    EXPECT_TRUE(DcEventDoorRegistry::IsSelfClearing(175354));  // trap 2, undead
    EXPECT_TRUE(DcEventDoorRegistry::IsSelfClearing(175355));  // trap 2, undead

    // Stratholme's EARNED gates are not self-clearing: nothing reopens them but
    // the party's own progress, so a hold there would never end.
    EXPECT_FALSE(DcEventDoorRegistry::IsSelfClearing(175352));  // King's Square Gate
    EXPECT_FALSE(DcEventDoorRegistry::IsSelfClearing(175353));  // King's Square Gate
    EXPECT_FALSE(DcEventDoorRegistry::IsSelfClearing(175356));  // Gauntlet Gate
    EXPECT_FALSE(DcEventDoorRegistry::IsSelfClearing(175357));  // Gauntlet Gate
    EXPECT_FALSE(DcEventDoorRegistry::IsSelfClearing(175368));  // Service Entrance
    EXPECT_FALSE(DcEventDoorRegistry::IsSelfClearing(0));

    // The trap gates ride the other lists' exclusions too: they are lock-free
    // and script-driven, so nothing else in the registry claims them either.
    EXPECT_FALSE(DcEventDoorRegistry::IsKeyExempt(175351));
    EXPECT_FALSE(DcEventDoorRegistry::IsLockFreeClickable(175351));
    EXPECT_FALSE(DcEventDoorRegistry::IsNavigationIgnored(175351));
}
