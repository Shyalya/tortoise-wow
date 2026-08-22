/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcHazard.h"

#include "DynamicObject.h"
#include "GameObject.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Data/DcHazardRegistry.h"

#include <cmath>
#include <limits>

namespace
{
    // Resolve the bot's value context, or nullptr. Every walker below needs it.
    AiObjectContext* ContextOf(Player* bot)
    {
        if (!bot)
            return nullptr;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        return botAI ? botAI->GetAiObjectContext() : nullptr;
    }

    // Walk the cached CREATURE emitter set, calling `test(row, emitterPosition)`
    // on each. Returns true on the first hit. Centralises the registry early-out
    // and the guid resolution so the public predicates differ only in geometry.
    template <typename Test>
    bool AnyEmitter(Player* bot, Test&& test)
    {
        if (!DcHazardRegistry::HasEmitters(bot->GetMapId()))
            return false;

        AiObjectContext* ctx = ContextOf(bot);
        if (!ctx)
            return false;

        GuidVector const& hazards = ctx->GetValue<GuidVector>(DcKey::Hazards)->Get();
        for (ObjectGuid guid : hazards)
        {
            Unit* u = ObjectAccessor::GetUnit(*bot, guid);
            if (!u)
                continue;

            // Re-check the registry on the resolved unit rather than trusting the
            // cached set: the value can be up to 500ms stale, and a bot that has
            // changed map in that window would otherwise measure against an
            // emitter from the instance it just left.
            DcHazardEmitter const* e = DcHazardRegistry::Find(u->GetMapId(), u->GetEntry());
            if (!e || u->GetMapId() != bot->GetMapId())
                continue;

            if (test(*e, u))
                return true;
        }
        return false;
    }

    // The GROUND-POOL twin of AnyEmitter. Same shape, but the guids resolve
    // through GetDynamicObject — a persistent area aura is not a unit, so
    // ObjectAccessor::GetUnit on one of these guids returns nullptr and the pool
    // would read as clean ground.
    template <typename Test>
    bool AnyGroundHazard(Player* bot, Test&& test)
    {
        if (!DcHazardRegistry::HasGroundHazards(bot->GetMapId()))
            return false;

        AiObjectContext* ctx = ContextOf(bot);
        if (!ctx)
            return false;

        GuidVector const& pools = ctx->GetValue<GuidVector>(DcKey::GroundHazards)->Get();
        for (ObjectGuid guid : pools)
        {
            DynamicObject* d = ObjectAccessor::GetDynamicObject(*bot, guid);
            if (!d)
                continue;

            // Same staleness guard as the creature walker — plus, for a pool, the
            // guid going dead IS how expiry is observed: a DynamicObject is
            // removed when its duration runs out, so GetDynamicObject failing
            // above is the pool ending, not an error.
            DcGroundHazard const* g = DcHazardRegistry::FindGround(d->GetMapId(), d->GetSpellId());
            if (!g || d->GetMapId() != bot->GetMapId())
                continue;

            if (test(*g, d))
                return true;
        }
        return false;
    }

    // The GAMEOBJECT-TRAP twin of the two walkers above. Same shape again, but
    // the guids resolve through GetGameObject — a trap is neither a unit nor a
    // dynamic object, so BOTH of the resolvers above return nullptr on one of
    // these guids and the fire would read as clean ground.
    template <typename Test>
    bool AnyTrapHazard(Player* bot, Test&& test)
    {
        if (!DcHazardRegistry::HasTrapHazards(bot->GetMapId()))
            return false;

        AiObjectContext* ctx = ContextOf(bot);
        if (!ctx)
            return false;

        GuidVector const& traps = ctx->GetValue<GuidVector>(DcKey::TrapHazards)->Get();
        for (ObjectGuid guid : traps)
        {
            GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
            if (!go)
                continue;

            // Same staleness guard as the other two — and, as with a pool, the
            // guid going dead IS how expiry is observed: a Blaze is removed when
            // its 60s duration runs out, so GetGameObject failing above is the
            // fire burning out, not an error.
            DcTrapHazard const* t = DcHazardRegistry::FindTrap(go->GetMapId(), go->GetEntry());
            if (!t || go->GetMapId() != bot->GetMapId())
                continue;

            if (test(*t, go))
                return true;
        }
        return false;
    }
}

bool DcHazard::PointIsHot(Player* bot, float x, float y, float z)
{
    if (!bot)
        return false;

    if (AnyEmitter(bot, [&](DcHazardEmitter const& e, Unit* u)
        {
            return DcHazardRegistry::PointInside(e,
                                                 u->GetPositionX(), u->GetPositionY(), u->GetPositionZ(),
                                                 x, y, z);
        }))
        return true;

    if (AnyGroundHazard(bot, [&](DcGroundHazard const& g, DynamicObject* d)
        {
            return DcHazardRegistry::PointInside(g,
                                                 d->GetPositionX(), d->GetPositionY(), d->GetPositionZ(),
                                                 x, y, z);
        }))
        return true;

    return AnyTrapHazard(bot, [&](DcTrapHazard const& t, GameObject* go)
    {
        return DcHazardRegistry::PointInside(t,
                                             go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(),
                                             x, y, z);
    });
}

bool DcHazard::SegmentIsHot(Player* bot, float ax, float ay, float az,
                            float bx, float by, float bz)
{
    if (!bot)
        return false;

    if (AnyEmitter(bot, [&](DcHazardEmitter const& e, Unit* u)
        {
            return DcHazardRegistry::SegmentClips(e,
                                                  u->GetPositionX(), u->GetPositionY(), u->GetPositionZ(),
                                                  ax, ay, az, bx, by, bz);
        }))
        return true;

    if (AnyGroundHazard(bot, [&](DcGroundHazard const& g, DynamicObject* d)
        {
            return DcHazardRegistry::SegmentClips(g,
                                                  d->GetPositionX(), d->GetPositionY(), d->GetPositionZ(),
                                                  ax, ay, az, bx, by, bz);
        }))
        return true;

    return AnyTrapHazard(bot, [&](DcTrapHazard const& t, GameObject* go)
    {
        return DcHazardRegistry::SegmentClips(t,
                                              go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(),
                                              ax, ay, az, bx, by, bz);
    });
}

bool DcHazard::LegIsHot(Player* bot, float x, float y, float z)
{
    if (!bot)
        return false;
    return SegmentIsHot(bot,
                        bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                        x, y, z);
}

namespace
{
    // One live active-vacate emitter, flattened out of whichever table it came
    // from so the two consumers below don't each re-walk both guid lists.
    struct LiveVacate
    {
        float x, y, z;
        float vacateRadius, zBand, holdBand, retreatSlack;
    };

    // Walk every live emitter on this map that carries a vacate radius — creature
    // rows and ground pools alike — calling `fn(LiveVacate)` on each. Rows with
    // vacateRadius 0 (a fought creature, a plain placement avoid) never reach `fn`.
    template <typename Fn>
    void ForEachVacate(Player* bot, AiObjectContext* ctx, Fn&& fn)
    {
        if (DcHazardRegistry::HasEmitters(bot->GetMapId()))
        {
            GuidVector const& hazards = ctx->GetValue<GuidVector>(DcKey::Hazards)->Get();
            for (ObjectGuid guid : hazards)
            {
                Unit* u = ObjectAccessor::GetUnit(*bot, guid);
                if (!u || !u->IsAlive())
                    continue;

                DcHazardEmitter const* e = DcHazardRegistry::Find(u->GetMapId(), u->GetEntry());
                if (!e || e->vacateRadius <= 0.0f || u->GetMapId() != bot->GetMapId())
                    continue;

                fn(LiveVacate{ u->GetPositionX(), u->GetPositionY(), u->GetPositionZ(),
                               e->vacateRadius, e->zBand, e->holdBand, e->retreatSlack });
            }
        }

        if (DcHazardRegistry::HasGroundHazards(bot->GetMapId()))
        {
            // Ground pools have no liveness flag to test — a persistent area aura
            // exists until its duration expires and the DynamicObject is removed,
            // so resolving the guid IS the liveness check.
            GuidVector const& pools = ctx->GetValue<GuidVector>(DcKey::GroundHazards)->Get();
            for (ObjectGuid guid : pools)
            {
                DynamicObject* d = ObjectAccessor::GetDynamicObject(*bot, guid);
                if (!d)
                    continue;

                DcGroundHazard const* g = DcHazardRegistry::FindGround(d->GetMapId(), d->GetSpellId());
                if (!g || g->vacateRadius <= 0.0f || d->GetMapId() != bot->GetMapId())
                    continue;

                fn(LiveVacate{ d->GetPositionX(), d->GetPositionY(), d->GetPositionZ(),
                               g->vacateRadius, g->zBand, g->holdBand, g->retreatSlack });
            }
        }

        if (DcHazardRegistry::HasTrapHazards(bot->GetMapId()))
        {
            // Traps have no liveness flag either — a Blaze exists until its 60s
            // duration expires and the GameObject is removed, so resolving the
            // guid IS the liveness check. Its loot state is deliberately not
            // consulted: a trap cycles READY -> ACTIVATED -> READY every couple
            // of seconds, and "mid-cast right now" is not the difference between
            // safe and unsafe ground.
            GuidVector const& traps = ctx->GetValue<GuidVector>(DcKey::TrapHazards)->Get();
            for (ObjectGuid guid : traps)
            {
                GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
                if (!go)
                    continue;

                DcTrapHazard const* t = DcHazardRegistry::FindTrap(go->GetMapId(), go->GetEntry());
                if (!t || t->vacateRadius <= 0.0f || go->GetMapId() != bot->GetMapId())
                    continue;

                fn(LiveVacate{ go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(),
                               t->vacateRadius, t->zBand, t->holdBand, t->retreatSlack });
            }
        }
    }

    // Is (px,py,pz) inside `v`'s danger band? Same-floor, and within the row's own
    // pulse + holdBand — so a bot already parked past the rim still reads clear
    // and one that has drifted back toward it does not.
    bool InDangerBand(LiveVacate const& v, float px, float py, float pz)
    {
        if (std::fabs(v.z - pz) > v.zBand)
            return false;

        float const reach = v.vacateRadius + v.holdBand;
        float const dx = px - v.x, dy = py - v.y;
        return dx * dx + dy * dy <= reach * reach;
    }
}

DcHazard::VacateEmitter DcHazard::NearestVacate(Player* bot)
{
    VacateEmitter best;
    if (!bot)
        return best;
    if (!DcHazardRegistry::HasAnyHazard(bot->GetMapId()))
        return best;

    AiObjectContext* ctx = ContextOf(bot);
    if (!ctx)
        return best;

    float bestDistSq = std::numeric_limits<float>::max();

    ForEachVacate(bot, ctx, [&](LiveVacate const& v)
    {
        if (!InDangerBand(v, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
            return;

        float const dx = bot->GetPositionX() - v.x, dy = bot->GetPositionY() - v.y;
        float const distSq = dx * dx + dy * dy;
        if (distSq >= bestDistSq)
            return;

        bestDistSq = distSq;
        best.ok = true;
        best.x = v.x;
        best.y = v.y;
        best.z = v.z;
        best.pulseRadius = v.vacateRadius;
        best.retreatSlack = v.retreatSlack;
    });

    return best;
}

bool DcHazard::PointIsInVacateBand(Player* bot, float x, float y, float z)
{
    if (!bot)
        return false;
    if (!DcHazardRegistry::HasAnyHazard(bot->GetMapId()))
        return false;

    AiObjectContext* ctx = ContextOf(bot);
    if (!ctx)
        return false;

    bool hot = false;
    ForEachVacate(bot, ctx, [&](LiveVacate const& v)
    {
        if (!hot && InDangerBand(v, x, y, z))
            hot = true;
    });
    return hot;
}
