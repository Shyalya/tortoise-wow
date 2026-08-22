/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearRoomTrashValue.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include <map>
#include <optional>
#include <string>

#include "AttackersValue.h"
#include "Creature.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "Timer.h"
#include "Ai/Dungeon/DungeonClear/Data/DcNeverTargetRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/RoomAggroRegistry.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcEngageGeometry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStatusPublisher.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

GuidVector DungeonClearRoomTrashValue::Calculate()
{
    GuidVector out;

    Player* bot = botAI ? botAI->GetBot() : nullptr;
    if (!bot)
        return out;

    // Feature master switch. Off -> the room-clear gate/driver never engages and
    // the boss is pulled exactly as before.
    if (!DcSettings::GetBool(bot, "ClearRoomBeforeBoss"))
        return out;

    // Only meaningful during an active, unpaused run.
    if (!DcRun::Of(context).enabled || DcRun::Of(context).paused)
    {
        trackedBoss = 0;
        gaveUp = false;
        noteSent = false;
        return out;
    }

    std::optional<DungeonBossInfo> next = AI_VALUE(std::optional<DungeonBossInfo>, DcKey::NextDungeonBoss);
    if (!next.has_value())
    {
        trackedBoss = 0;
        gaveUp = false;
        noteSent = false;
        return out;
    }

    RoomAggroBoss const* room =
        RoomAggroRegistry::Find(bot->GetMapId(), next->entry);
    if (!room)
    {
        trackedBoss = 0;
        gaveUp = false;
        noteSent = false;
        return out;
    }

    // Boss change resets the no-progress timeout and the give-up latch so each
    // room starts with a clean clock (self-heals across bosses/runs/skips).
    if (trackedBoss != room->bossEntry)
    {
        trackedBoss = room->bossEntry;
        lastRemaining = 0;
        lastProgressMs = getMSTime();
        gaveUp = false;
        noteSent = false;
    }

    // Already gave up on this room -> report it clear so the boss gate opens and
    // the run proceeds (the straggler comes with the boss). Stays latched until
    // the boss changes.
    if (gaveUp)
        return out;

    // Measure from the LIVE boss (some flagged bosses wander) — the room is its
    // radius-sphere, not the static spawn anchor.
    Creature* liveBoss = DcTargeting::GetLiveBoss(bot, context, room->bossEntry);
    if (!liveBoss)
        return out;  // not loaded/alive yet — nothing to measure or clear

    // Only CLEAR once the tank has actually REACHED the room. The set below is
    // built purely from boss<->trash proximity, with no regard for where the TANK
    // is — so without this gate the conditional room-clear event (relevance 31,
    // above boss-travel at 15) hijacks the approach from across the dungeon and
    // fixates the tank on a single barely-reachable straggler instead of letting
    // Advance route it into the room. The room-clear window is straight-line-blind
    // to walls (a chamber one wall over is "close"), so gate on the PATH distance
    // to the live boss. While the tank is still pathing in — or the room is only
    // reachable the long way around (Scholomance: the room east of Marduk & Vectus
    // is ~55yd straight-line but a 170yd+ walk) — stand down with an empty set so
    // boss-travel drives, and re-arm the no-progress clock so the give-up timer
    // starts when clearing actually begins, not during the walk in.
    if (!DcEngageGeometry::TankReachedRoomByPath(bot, context, *next))
    {
        lastRemaining = 0;
        lastProgressMs = getMSTime();
        return out;
    }

    // Units inside the boss's own aggro sphere come with the boss and are
    // excluded (IsRoomTrash). Sized from the LIVE boss's real aggro range, PLUS
    // both combat reaches and the run's aggro margin: a melee tank that walks up
    // to the nearest still-clearable unit (just outside this sphere) then stands
    // within a combat reach of it, so the reaches keep the tank itself outside
    // the boss's aggro even at point-blank on that unit — the whole point of the
    // exclusion is "don't wake the boss while clearing".
    //
    // This exclusion ring MUST be identical to the skirt's avoid ring and the
    // tank's standoff — they all read the one DcEngageGeometry::RoomAggroSphereRadius
    // source now. Previously each re-derived the formula and they disagreed by
    // exactly RoomAggroPathPadding, leaving a dead band of packs KEPT as room-trash
    // (just outside this ring) but UNREACHABLE by the tank (just inside the skirt's
    // wider avoid ring): the skirt can't deliver the tank to a point inside its own
    // no-go zone, so NearestRoomTrash handed the tank a dead-band pack, the skirt
    // orbited the boss forever, and it never fell through to a genuinely reachable
    // pack. (Live: a small pack at ~30yd from Jammal'an, inside the 33yd skirt ring
    // but outside the 27yd exclusion, locked the room clear.) One source keeps any
    // pack the skirt can't approach "coming with the boss" — cleared by the boss
    // pull, not chased into the aggro range.
    //
    // EXCEPTION: a room with an explicit pullOutRadius (Sepethrea) deliberately
    // SHRINKS this exclusion below the skirt/standoff sphere so a pack parked on the
    // boss's aggro edge is KEPT as clearable room trash instead of coming with the
    // boss. That re-creates the dead band described above for a straight melee walk-
    // in — which is why a non-zero pullOutRadius is COUPLED with forced advanced pull
    // (DcTargeting::RoomClearForcesAdvanced): the tank never walks the kept pack down
    // to melee inside the sphere; it forms up outside the pack's own aggro and drags
    // it OUT to camp, so the skirt's no-go zone is never entered. See
    // RoomAggroBoss::pullOutRadius. When unset (every other row) the exclusion stays
    // identical to the skirt's avoid ring and the tank's standoff.
    float const bossSafe = room->pullOutRadius > 0.0f
        ? room->pullOutRadius
        : DcEngageGeometry::RoomAggroSphereRadius(bot, liveBoss);

    // Per-reason exclusion tallies — logged below when the kept count changes so
    // a premature "room clear" (set empties while trash is alive, opening the boss
    // gate) names the exact filter that dropped the last candidates.
    uint32 nCand = 0, exDead = 0, exHostile = 0, exTarget = 0, exBossEntry = 0,
           exRoomPartner = 0, exSphere = 0, exReach = 0, exDoor = 0, exFar = 0,
           exBand = 0;

    // Diagnostic histograms: every live candidate within the room radius, bucketed
    // by entry and hostility, so a live "kept=0" names EXACTLY which entries sit
    // near the boss and whether they read hostile to the bot. This is what tells
    // a future tuner whether a missing pre-clear is "threat is hostile-but-far"
    // (widen radius) vs "threat is neutral-but-near" (explicit member whitelist).
    std::map<uint32, uint32> diagHostileNear, diagNeutralNear;

    GuidVector const& candidates = AI_VALUE(GuidVector, DcKey::FarTargets);
    nCand = static_cast<uint32>(candidates.size());
    for (ObjectGuid const guid : candidates)
    {
        Unit* u = ObjectAccessor::GetUnit(*bot, guid);
        if (!u || !u->IsAlive())
        {
            ++exDead;
            continue;
        }
        if (u->GetObjectGuid() == liveBoss->GetObjectGuid())
            continue;

        // Diagnostic bucketing (in-radius only) — independent of the exclusion
        // flow below so it captures units dropped early (e.g. the non-hostile
        // gate). Cheap; only the leader formats it in the change-gated log.
        {
            float const dDiag = liveBoss->GetExactDist(u);
            if (dDiag <= room->radius)
            {
                if (bot->IsHostileTo(u))
                    ++diagHostileNear[u->GetEntry()];
                else
                    ++diagNeutralNear[u->GetEntry()];
            }
        }
        // A room with an EXPLICIT member whitelist may stand NEUTRAL (yellow)
        // until struck — Scholomance's Reanimation chamber (entry 10475) and its
        // bosses are all non-hostile, so bot->IsHostileTo() is false for the
        // entire room and the ordinary hostile gate would drop every student,
        // leaving the boss to be pulled with the room still up. For an
        // explicitly-listed member, skip the hostile gate: those entries ARE the
        // room by author intent. IsPossibleTarget still runs below and accepts a
        // neutral unit (it rejects only genuinely friendly / unattackable ones),
        // so a mistargeted friendly NPC is still excluded. Rooms with an EMPTY
        // whitelist (every other RoomAggroRegistry boss) keep the strict
        // hostile-only behaviour — an empty whitelist is "any hostile", never
        // "any neutral".
        bool const explicitMember =
            !room->memberEntries.empty() &&
            RoomAggroRegistry::IsMemberEntry(*room, u->GetEntry());
        if (!explicitMember && !bot->IsHostileTo(u))
        {
            ++exHostile;
            continue;
        }
        if (!AttackersValue::IsPossibleTarget(u, bot))
        {
            ++exTarget;
            continue;
        }
        // Scripted never-dies mobs are not room trash — clearing a room that
        // contains one would never complete. See DcNeverTargetRegistry.
        if (DcNeverTargetRegistry::IsNeverTarget(bot->GetMapId(), u->GetEntry()))
        {
            ++exTarget;
            continue;
        }
        // Never treat another encounter boss as room trash — the dedicated
        // at-boss path owns bosses, and scripted bosses misbehave when pulled.
        if (DcTargeting::IsDungeonBossEntry(context, u->GetEntry()))
        {
            ++exBossEntry;
            continue;
        }
        // ...nor a RoomAggroRegistry boss entry. Some encounter partners are
        // swapped OUT of the boss roster (SM Cathedral's High Inquisitor Whitemane
        // -> Mograine, inheriting the kill-bit) so IsDungeonBossEntry no longer
        // flags them — but they are still bosses that share THIS room pull and
        // stand WITH the live boss. Chasing one as "trash" drags the tank into the
        // boss's aggro sphere (the live SM Cathedral failure: the tank orbited
        // Mograine trying to reach Whitemane, who stands inside his aggro, and woke
        // the room). The registry lists both partners, so skip any of them.
        if (RoomAggroRegistry::Find(bot->GetMapId(), u->GetEntry()))
        {
            ++exRoomPartner;
            continue;
        }

        // Some scripts only force-pull adds inside a fixed world-Y corridor, not
        // the whole radius-sphere (Pandemonius: ROOM_EXIT < Y < ROOM_ENTERANCE —
        // the adds standing BEHIND him toward the next boss are never pulled). For
        // those rooms, an add outside the band is not part of the pull and must NOT
        // be chased: keeping it makes the tank orbit the boss after an unreachable
        // straggler until the timeout fires. No band -> every Y qualifies.
        if (!RoomAggroRegistry::InRoomBand(*room, u->GetPositionY()))
        {
            ++exBand;
            continue;
        }

        float const distToBoss = liveBoss->GetExactDist(u);
        if (!RoomAggroRegistry::IsRoomTrash(*room, u->GetEntry(), distToBoss, bossSafe))
        {
            // Split the two IsRoomTrash rejections: outside the scripted room
            // radius (genuinely not part of this room) vs inside the boss-aggro
            // sphere (glued to the boss). Only the latter shrinks as the tank
            // moves, so it's the one that can falsely "clear" the room.
            if (distToBoss <= bossSafe)
                ++exSphere;
            else
                ++exFar;
            continue;
        }

        // Don't count a unit the tank can't actually reach (different floor with
        // no route) or one behind a closed door — clearing it isn't possible
        // without help, and the no-progress timeout would otherwise livelock.
        if (!DcEngageGeometry::IsLevelReachable(bot, u))
        {
            ++exReach;
            continue;
        }
        if (DcEngageGeometry::ClosedDoorBetween(bot, u->GetPositionX(),
                                                u->GetPositionY(), u->GetPositionZ()))
        {
            ++exDoor;
            continue;
        }

        out.push_back(guid);
    }

    // Diagnostic: when the kept count changes, name the breakdown — so a premature
    // empty (boss gate opens while trash is alive) shows the exact filter that
    // dropped the last candidates. Leader-only and change-gated to avoid spam.
    if (DcLeaderSignal::IsDungeonClearLeader(bot) &&
        out.size() != lastLoggedKept)
    {
        lastLoggedKept = out.size();
        bool const atBossNow =
            DcEngageGeometry::IsAtBossEngage(bot, context, *next, DC_ENGAGE_RANGE);

        // Compact "entryxcount ..." rendering of an in-radius histogram (capped).
        auto const histStr = [](std::map<uint32, uint32> const& m) -> std::string
        {
            std::string s;
            uint32 shown = 0;
            for (auto const& [entry, count] : m)
            {
                if (shown++ >= 8)
                {
                    s += "...";
                    break;
                }
                if (!s.empty())
                    s += " ";
                s += std::to_string(entry) + "x" + std::to_string(count);
            }
            return s.empty() ? "-" : s;
        };

        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] room-trash {}: kept={} of cand={} (boss {} r={:.1f} "
                 "sphere={:.1f} atBoss={}) excl: far={} band={} sphere={} door={} "
                 "reach={} partner={} bossEntry={} dead={} hostile={} target={} | "
                 "near[H]: {} | near[N]: {}",
                 bot->GetName(), next->name, out.size(), nCand, room->bossEntry,
                 room->radius, bossSafe, atBossNow ? 1 : 0, exFar, exBand, exSphere,
                 exDoor, exReach, exRoomPartner, exBossEntry, exDead, exHostile,
                 exTarget, histStr(diagHostileNear), histStr(diagNeutralNear));
    }

    // No-progress give-up valve: if the remaining count hasn't DROPPED within
    // RoomClearTimeout seconds, stop holding the boss pull on an unreachable
    // straggler / a respawn churn and let the run proceed. Progress (a smaller
    // set, or first sighting) re-arms the clock.
    uint32 const now = getMSTime();
    uint32 const remaining = static_cast<uint32>(out.size());
    if (remaining == 0)
    {
        lastRemaining = 0;
        lastProgressMs = now;
        return out;
    }

    // CRITICAL: the room-clear driver only engages trash once the tank is in the
    // room — while it's still walking the long path in, no trash is being engaged
    // and the count can't drop. So keep the clock RE-ARMED until the tank actually
    // arrives; otherwise the whole timeout is burned by the travel leg and the
    // room-clear "gives up" before the first pull (a 192yd approach trivially
    // outlasts 30s). The no-progress window must measure stalls WHILE CLEARING, not
    // the walk in. Use the same room-clear envelope the driver activates on
    // (WithinRoomClearWindow) — NOT the tight IsAtBossEngage standoff, which the
    // tank never reaches while clearing from out on the skirt orbit ring.
    bool const inRoom =
        DcEngageGeometry::WithinRoomClearWindow(bot, context, *next);
    if (!inRoom)
    {
        lastRemaining = remaining;
        lastProgressMs = now;
        return out;
    }

    if (lastRemaining == 0 || remaining < lastRemaining)
    {
        lastRemaining = remaining;
        lastProgressMs = now;
    }

    uint32 const timeoutMs = DcSettings::GetUInt(bot, "RoomClearTimeout") * 1000u;
    if (timeoutMs && (now - lastProgressMs) > timeoutMs)
    {
        gaveUp = true;
        if (!noteSent && DcLeaderSignal::IsDungeonClearLeader(bot))
        {
            noteSent = true;
            DcStatusPublisher::SendAddonMessage(
                botAI, "CHAT\tCouldn't fully clear the room before " + next->name +
                           " — pulling anyway (some adds will join).");
        }
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] room-clear gave up on {} ({} left after {}s) -> pulling boss",
                 bot->GetName(), next->name, remaining,
                 DcSettings::GetUInt(bot, "RoomClearTimeout"));
        out.clear();
    }

    return out;
}
