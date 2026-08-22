/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearPartyMemberToHealValue.h"

#include "Creature.h"
#include "Playerbots.h"

#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"

Unit* DungeonClearPartyMemberToHealValue::Calculate()
{
    // The stock answer first — the most-hurt group member (LOS/range-gated,
    // raid/party rules, pets, charms, focus-heal-targets). This is exactly what a
    // non-DC bot would heal, and what a DC bot heals when no escort is running.
    Unit* stock = PartyMemberToHeal::Calculate();

    Creature* escortee = DcLeaderSignal::GetLeaderEscortee(bot);
    if (!escortee || !escortee->IsAlive())
        return stock;

    // Same candidacy gate the stock Check() applies to party members: within the
    // heal-candidate radius (healDistance * 2) and in line of sight. A target the
    // healer cannot actually cast on must never be surfaced (the reposition mover
    // — DungeonClearHealTargetValue — walks the healer back into range/LOS).
    if (bot->GetDistance2d(escortee) >= sPlayerbotAIConfig.healDistance * 2.0f ||
        !bot->IsWithinLOSInMap(escortee))
        return stock;

    // Fold the escortee in on the SAME "most hurt wins" basis the stock value uses
    // (its probe is health-percent plus a small distance term, so health percent is
    // the deciding factor). Return whichever of {stock winner, escortee} is more
    // hurt; a full-health escortee never displaces a hurt player, and when nobody
    // in the party needs healing (stock == nullptr => treat as 100%) a hurt
    // escortee is surfaced. The heal TRIGGERS still gate on the returned target's
    // health, so a barely-scratched escortee returned here simply never draws a
    // cast — exactly as for a barely-scratched teammate.
    float const escorteeHp = escortee->GetHealthPct();
    float const stockHp = stock ? stock->GetHealthPct() : 100.0f;
    return escorteeHp < stockHp ? static_cast<Unit*>(escortee) : stock;
}
