/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcSocialQuarantineRegistry.h"

#include <algorithm>
#include <cmath>

namespace
{
    uint32 constexpr MGT_MAP      = 585;
    uint32 constexpr MGT_DELRISSA = 24560;

    uint32 constexpr MGT_SB_SENTINEL    = 24777;
    uint32 constexpr MGT_SB_MAGE_GUARD  = 24683;
    uint32 constexpr MGT_SB_BLOOD_KNGT  = 24684;
    uint32 constexpr MGT_SB_MAGISTER    = 24685;
    uint32 constexpr MGT_SB_WARLOCK     = 24686;
    uint32 constexpr MGT_SB_PHYSICIAN   = 24687;
    uint32 constexpr MGT_SISTER_TORMENT = 24697;
    uint32 constexpr MGT_ETHEREUM_SMUG  = 24698;

    // ---- the table ------------------------------------------------------
    //
    // ROWS ARE ONLY FOR PACKS WITH NO SCRIPTED-PULL ROW OF THEIR OWN. The rotunda's
    // five formations are NOT here: DcSocialQuarantine derives their cylinders from
    // ScriptedPullRegistry, so the plan's own volumes stay the single source of
    // truth for that room and cannot drift from a copy. This table is for packs the
    // plan never pulls but whose presence decides the fight anyway.
    //
    // --- Magisters' Terrace (585), Priestess Delrissa (24560) ---------------
    //
    // WHAT THE MEASUREMENT SAYS. Delrissa's chamber is not a room with a door: it
    // is the north end of one open hall ~120yd across, and she stands in the middle
    // of it with a five-mob Sunblade formation 27yd to her west and another 32yd to
    // her east. Both are cleared AFTER her in the run's own order (they are on the
    // way to Kael'thas, not on the way to her), so they are alive, awake and inside
    // a chase's walk of the fight for its whole duration.
    //
    //   W  96771  centroid (93.95, 21.66)  4 mobs — Mage Guard, Magister, Warlock,
    //                                               Physician
    //   E  96779  centroid (162.29, 20.82) 5 mobs — Blood Knight, Magister, Warlock,
    //                                               Sister of Torment, Ethereum Smuggler
    //
    // Nearest member to Delrissa's spawn (126.90, 19.15): west Mage Guard 96771 at
    // 27.5yd, east Magister 96790 at 32.3yd.
    //
    // WHY A CAMP CANNOT SOLVE IT, which is the whole reason this row exists rather
    // than another hand-authored anchor. Delrissa is fought pulled back already —
    // the ordinary advanced pull drags her to a camp it improvises ~57yd south of
    // her spawn, and on tp-20260807-203840-1 it did exactly that on every run. The
    // fight does not STAY there: her four helpers are summoned with
    // DoZoneInCombat() and score threat off boss_priestess_lackey_commonAI's own
    // distance-weighted table rather than off a tank's threat, so they peel onto
    // whoever they like and the party follows them. On tr-20260807-203845-2 all
    // five party members died between Y +6 and +20 — at her SPAWN, 55yd north of
    // the camp the tank had just dragged her to — with an eleven-mob observed pull
    // where the encounter is five.
    //
    // And the camp cannot simply be moved further back, because the boss has a
    // boundary of her own: boss_priestess_delrissa::CheckInRoom() evades at
    // GetDistance(GetHomePosition()) >= 75yd. The improvised 57yd camp is already
    // most of the way to it; anything past ~68yd trades a wipe for an evade-reset
    // loop. There is no camp that is both legal and far enough from these two
    // packs, so the packs are what has to change.
    //
    // THE SENTINEL is the third row and a different failure of the same kind.
    // Sunblade Sentinel 96946 patrols waypoint path 969460 — (126.83, -80.82) to
    // (127.08, -48.44), straight up the hall's centre line — with a 5s dwell at
    // each end. Route trash, killed on the way in in the healthy case, but a
    // sentinel that happened to be at the SOUTH end when the party walked past
    // arrives at the north end tens of seconds later, in the middle of whatever the
    // party is doing by then. Same shape as the rotunda's hall patrol, which is a
    // scripted stage for exactly this reason; here the cheaper answer is available
    // because the party is not camping on its path — quarantine it and let the
    // ordinary blocking-trash pass kill it whenever it is actually in the way.
    //
    // CYLINDERS are sized to hold their own formation and clear every neighbour
    // that shares an entry with it, checked against acore_world.creature on map 585:
    //   W  r=10: own spread 8.85, nearest same-entry outsider Physician 96822 at
    //            19.5yd. Broken Sentinel 96950 sits 6.7yd from the centre and is
    //            kept out by the entry filter — the same NullCreatureAI prop trick
    //            the rotunda's east cylinder plays.
    //   E  r=7:  own spread 4.69, nearest same-entry outsider Magister 96794 at
    //            19.9yd.
    //   S  r=20: covers the patrol path end to end (half-length 16.19) with 3.8yd
    //            of slack for a sentinel between waypoints. The z-band is what
    //            keeps the OTHER corridor sentinel 96944 out — same entry, 12yd
    //            higher.
    std::vector<DcQuarantineZone> const& Table()
    {
        static std::vector<DcQuarantineZone> const kZones = []
        {
            std::vector<DcQuarantineZone> t;

            DcQuarantineZone west;
            west.mapId     = MGT_MAP;
            west.bossEntry = MGT_DELRISSA;
            west.name      = "Magisters' Terrace — Delrissa's west flank pack";
            west.x         = 93.95f;  west.y = 21.66f; west.z = -21.34f;
            west.radius    = 10.0f;
            west.zBand     = 10.0f;
            west.entries   = {MGT_SB_MAGE_GUARD, MGT_SB_MAGISTER, MGT_SB_WARLOCK,
                              MGT_SB_PHYSICIAN};
            t.push_back(west);

            DcQuarantineZone east;
            east.mapId     = MGT_MAP;
            east.bossEntry = MGT_DELRISSA;
            east.name      = "Magisters' Terrace — Delrissa's east flank pack";
            east.x         = 162.29f; east.y = 20.82f; east.z = -21.34f;
            east.radius    = 7.0f;
            east.zBand     = 10.0f;
            east.entries   = {MGT_SB_BLOOD_KNGT, MGT_SB_MAGISTER, MGT_SB_WARLOCK,
                              MGT_SISTER_TORMENT, MGT_ETHEREUM_SMUG};
            t.push_back(east);

            DcQuarantineZone patrol;
            patrol.mapId     = MGT_MAP;
            patrol.bossEntry = MGT_DELRISSA;
            patrol.name      = "Magisters' Terrace — the hall sentinel below Delrissa";
            patrol.x         = 126.96f; patrol.y = -64.63f; patrol.z = -21.54f;
            patrol.radius    = 20.0f;
            patrol.zBand     = 6.0f;
            patrol.entries   = {MGT_SB_SENTINEL};
            t.push_back(patrol);

            return t;
        }();
        return kZones;
    }
}

std::vector<DcQuarantineZone> const& DcSocialQuarantineRegistry::AllRows()
{
    return Table();
}

std::vector<DcQuarantineZone const*> DcSocialQuarantineRegistry::Zones(uint32 mapId,
                                                                      uint32 bossEntry)
{
    std::vector<DcQuarantineZone const*> out;
    for (DcQuarantineZone const& z : Table())
        if (z.mapId == mapId && z.bossEntry == bossEntry)
            out.push_back(&z);
    return out;
}

std::vector<DcQuarantineZone const*> DcSocialQuarantineRegistry::AllZones(uint32 mapId)
{
    std::vector<DcQuarantineZone const*> out;
    for (DcQuarantineZone const& z : Table())
        if (z.mapId == mapId)
            out.push_back(&z);
    return out;
}

bool DcSocialQuarantineRegistry::HasRows(uint32 mapId)
{
    for (DcQuarantineZone const& z : Table())
        if (z.mapId == mapId)
            return true;
    return false;
}

bool DcSocialQuarantineRegistry::InZone(DcQuarantineZone const& z, float x, float y,
                                        float zz)
{
    if (std::fabs(zz - z.z) > z.zBand)
        return false;
    float const dx = x - z.x;
    float const dy = y - z.y;
    return (dx * dx + dy * dy) <= (z.radius * z.radius);
}

bool DcSocialQuarantineRegistry::IsZoneEntry(DcQuarantineZone const& z, uint32 entry)
{
    return std::find(z.entries.begin(), z.entries.end(), entry) != z.entries.end();
}
