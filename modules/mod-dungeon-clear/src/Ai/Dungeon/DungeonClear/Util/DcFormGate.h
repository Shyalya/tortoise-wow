/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCFORMGATE_H
#define _PLAYERBOT_DCFORMGATE_H

#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Player.h"
#include "SpellEntry.h"
#include "SpellMgr.h"

// Shapeshift gate for the event system's item-use casts.
//
// Every quest-item interaction in DungeonClear goes through
// Player::CastItemUseSpell (the Deadmines-cannon rule: only the item-use cast
// carries the m_CastItem context an OPEN_LOCK KEY_ITEM lock needs). That cast
// runs the full Spell::CheckCast, which includes SpellEntry::CheckShapeshift —
// and the feral druid forms are flagged CAN_ONLY_CAST_SHAPESHIFT_SPELLS, so a
// bear-form tank's item use is rejected before it ever reaches the target.
//
// Live symptom (Old Hillsbrad, event 560/2): a druid leader tank walks into the
// house, stands on the barrel, and spam-casts the Pack of Incendiary Bombs for
// the whole 120s step timeout without ever planting one — the goober never
// leaves GO_READY, so the step's success latch never trips and the run stalls
// out of the distraction event. Warriors/paladins in the same seat plant fine.
//
// The fix is to shift back to caster form before the cast. Human form is the
// only form these interactions work from, so this is unconditional-by-need:
// drop the form ONLY when the item's on-use spell actually fails the shapeshift
// check, so non-druids (and druids already in caster form) pay nothing and a
// form that happens to permit the cast is left alone.
namespace DcFormGate
{
    // IMPURE: if `bot` is in a shapeshift form that would make `item`'s on-use
    // spell fail CheckShapeshift, cancel the form and return true. Returns false
    // (and touches nothing) when the bot is formless, isn't a shapeshifter, or is
    // in a form the spell is castable from.
    //
    // The aura removal is synchronous — GetShapeshiftForm() reads FORM_NONE
    // immediately after — so the caller can fire CastItemUseSpell in the SAME
    // tick. It costs no GCD and isn't movement-interrupted (the same reason the
    // pull code re-shifts mid-drag). Should the bot's own combat reflexes re-shift
    // it mid-plant and interrupt the ~2s "Opening" cast, the next tick lands here
    // again and retries — the step is idempotent and self-heals.
    inline bool DropBlockingForm(Player* bot, Item* item)
    {
        if (!bot || !item)
            return false;

        ShapeshiftForm const form = bot->GetShapeshiftForm();
        if (form == FORM_NONE)
            return false;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return false;

        // Blocked if ANY of the item's on-use spells is un-castable in this form:
        // the cast we're about to make is the one CastItemUseSpell picks, and the
        // quest items involved carry exactly one on-use spell.
        bool blocked = false;
        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS && !blocked; ++i)
        {
            if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                continue;
            if (proto->Spells[i].SpellId <= 0)
                continue;
            SpellEntry const* info = sSpellMgr.GetSpellEntry(uint32(proto->Spells[i].SpellId));
            if (info && info->CheckShapeshift(uint32(form)) != SPELL_CAST_OK)
                blocked = true;
        }

        if (!blocked)
            return false;

        LOG_INFO("playerbots.dungeonclear",
                 "[dungeon-clear] {} leaving shapeshift form {} to use item {} '{}'",
                 bot->GetName(), uint32(form), proto->ItemId, proto->Name1);
        bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
        return true;
    }
}

#endif
