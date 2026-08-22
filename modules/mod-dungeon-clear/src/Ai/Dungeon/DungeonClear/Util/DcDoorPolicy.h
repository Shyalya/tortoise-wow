/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCDOORPOLICY_H
#define _PLAYERBOT_DCDOORPOLICY_H

#include <cstddef>

#include "SharedDefines.h"

// Pure, testable kernel answering "could a human player at this bot's keyboard
// open this lock?" for door gameobjects. Mirrors the core's lock adjudication
// (Spell::CanOpenLock) rather than inventing a stricter policy — the previous
// hand-rolled gate refused locks the game itself hands to every player:
//
//   - A lock entry with NO typed slots at all (e.g. lock 85, the Deadmines
//     Factory/Foundry/Mast Room doors) imposes no requirement; CanOpenLock
//     returns OK for anyone. reqKey never gets set, so the empty lock falls
//     through to success.
//   - A LOCK_KEY_SKILL slot whose locktype is a bare-hands interaction
//     (Open / Quick Open / Slow Open / kneeling / attacking variants, e.g.
//     lock 86 on the Deadmines Heavy Doors) maps to SKILL_NONE — no skill
//     check, any player's click performs it.
//
// The one place we are deliberately stricter than a raw CanOpenLock mirror:
// when the GO carries GO_FLAG_LOCKED the bare-hands slots do NOT count. A
// flagged-locked gate keeps its bare-hands slot only as cast-time/animation
// data; the client requires the real key or skill (Stratholme's King's Square
// Gate, lock 879, carries a Quick Open slot yet demands the Key to the City —
// same shape on the Deadmines Iron Clad Door, lock 202, which only a rogue
// picks or the cannon event blows open).
//
// Locktypes needing consumables or professions we don't model for doors
// (Blasting = seaforium, Open Tinkering = engineering, gathering skills)
// never satisfy a slot here; a door gated only by those parks the run and
// asks the human, which is the safe default.
namespace DcDoorPolicy
{
    // One decoded Lock.dbc case: LOCK_KEY_* discriminator, its Index (item
    // entry for ITEM, LockType for SKILL), and the required skill value.
    struct LockSlot
    {
        uint8 keyType = 0;
        uint32 index = 0;
        uint32 requiredSkill = 0;
    };

    constexpr std::size_t LOCK_SLOT_COUNT = 8;  // MAX_LOCK_CASE

    // Locktypes any player performs empty-handed by clicking. CLOSE variants
    // included: a slot authored as Quick/Slow Close is still a bare-hands
    // interaction on the same door.
    inline bool IsBareHandsLockType(uint32 lockType)
    {
        switch (lockType)
        {
            case LOCKTYPE_OPEN:
            case LOCKTYPE_CLOSE:
            case LOCKTYPE_QUICK_OPEN:
            case LOCKTYPE_QUICK_CLOSE:
            case LOCKTYPE_OPEN_KNEELING:
            case LOCKTYPE_OPEN_ATTACKING:
            case LOCKTYPE_SLOW_OPEN:
            case LOCKTYPE_SLOW_CLOSE:
                return true;
            default:
                return false;
        }
    }

    // Slot walk mirroring Spell::CanOpenLock: any satisfied slot opens; a lock
    // whose slots are all untyped imposed no requirement and opens for anyone.
    //
    //   lockEnforced  — the GO carries GO_FLAG_LOCKED (suppresses bare-hands
    //                   slots, see the header comment).
    //   hasKeyItem    — callable, uint32 item entry -> bool (in the bot's bags).
    //   lockpickSkill — the bot's lockpicking value, or -1 when unskilled.
    template <typename HasItemFn>
    bool CanOpenSlots(LockSlot const* slots, std::size_t count, bool lockEnforced,
                      HasItemFn&& hasKeyItem, int32 lockpickSkill)
    {
        bool requirementSeen = false;
        for (std::size_t i = 0; i < count; ++i)
        {
            LockSlot const& slot = slots[i];
            switch (slot.keyType)
            {
                case LOCK_KEY_ITEM:
                    if (slot.index && hasKeyItem(slot.index))
                        return true;
                    requirementSeen = true;
                    break;
                case LOCK_KEY_SKILL:
                    requirementSeen = true;
                    if (slot.index == LOCKTYPE_PICKLOCK)
                    {
                        if (lockpickSkill >= 0 &&
                            lockpickSkill >= static_cast<int32>(slot.requiredSkill))
                            return true;
                    }
                    else if (IsBareHandsLockType(slot.index))
                    {
                        if (!lockEnforced)
                            return true;
                    }
                    // Consumable / profession locktypes: never satisfied here.
                    break;
                case LOCK_KEY_SPELL:
                    // Bots don't model the opening-spell inventory (seaforium
                    // etc.); treat as an unsatisfiable requirement.
                    requirementSeen = true;
                    break;
                default:
                    break;
            }
        }
        return !requirementSeen;
    }
}

#endif
