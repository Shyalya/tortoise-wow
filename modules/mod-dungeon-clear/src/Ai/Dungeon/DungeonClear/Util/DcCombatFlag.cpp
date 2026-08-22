/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcCombatFlag.h"

#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Util/DcEngageGeometry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"

#include "AiObjectContext.h"
#include "CombatManager.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Timer.h"

namespace DcCombatFlag
{
    bool IsEngaged(Player* p)
    {
        if (!p || !p->IsAlive() || !p->IsInWorld())
            return false;

        // Range-qualified on both sides (see DC_ENGAGEMENT_RADIUS). A combat
        // reference survives the geometry that made it unusable, so "something is
        // on my attacker list" is not the same question as "I am in a fight" the
        // moment anything crosses a navmesh break. 3D distance, not 2D: the
        // Azjol-Nerub case that produced this is 366yd of it VERTICAL.
        //
        // SQUARED distance, and IsInWorld before GetMap, both because of where
        // this sits. It is the module's hottest predicate — every follower rung
        // asks it every tick, AnyPartyEngagement fans it across the group, and a
        // tank in an AoE pack carries a double-digit attacker set — so the sqrt
        // is not worth paying and neither is a needless map compare. GetMap()
        // ASSERTs on a null map, and the attacker set holds raw pointers we now
        // dereference (the old `.empty()` test never did), so nothing here may
        // touch a unit on its way out of the world.
        Map const* const map = p->FindMap();
        constexpr float radiusSq = DC_ENGAGEMENT_RADIUS * DC_ENGAGEMENT_RADIUS;

        auto const inFight = [p, map, radiusSq](Unit const* other)
        {
            return other && other->IsInWorld() && other->FindMap() == map &&
                   p->GetExactDistSq(other) <= radiusSq;
        };

        if (inFight(p->GetVictim()))
            return true;

        for (Unit const* const attacker : p->getAttackers())
            if (inFight(attacker))
                return true;

        return false;
    }

    bool AnyPartyEngagement(Player* bot)
    {
        if (!bot)
            return false;

        if (IsEngaged(bot))
            return true;
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (member && member != bot && member->GetMapId() == bot->GetMapId() &&
                IsEngaged(member))
                return true;
        }
        return false;
    }

    bool AnyPartyCombatFlag(Player* bot)
    {
        if (!bot)
            return false;
        if (bot->IsInCombat())
            return true;
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (member && member != bot && member->IsAlive() &&
                member->GetMapId() == bot->GetMapId() && member->IsInCombat())
                return true;
        }
        return false;
    }

    HolderScan ScanCombatHolders(Player* p)
    {
        HolderScan scan;
        if (!p)
            return scan;

        // Tortoise port: AzerothCore's CombatManager tracks "everyone I am in
        // combat with" as reference pairs. This core's nearest equivalent is the
        // hostile-ref manager - the list of threat managers that hold p - whose
        // owners are exactly the units fighting him. Evade is asked of the
        // creature itself; there is no per-pair evade state here.
        HostileReference* ref0 = p->GetHostileRefManager().getFirst();
        if (!ref0)
        {
            scan.opaque = true;
            return scan;
        }

        Map* const map = p->FindMap();
        for (HostileReference* ref = ref0; ref; ref = ref->next())
        {
            Unit* const other = ref->getSourceUnit();
            if (!other || !other->IsAlive() || other->FindMap() != map)
                continue;
            Creature* const evadeCheck = other->ToCreature();
            if (evadeCheck && evadeCheck->IsInEvadeMode())
                continue;  // holder is bailing home -> not a real threat
            // RANGE FIRST, and it is not just an optimisation. DcEngageGeometry::
            // IsReachable delegates to the CHUNKED pathfinder, which by design
            // accepts any path with forward progress so the tank can walk a boss
            // route farther than PathGenerator's ~296yd single-call cap. Handed a
            // holder on the far side of a one-way relocation it therefore answers
            // "reachable" — tr-20260818-223003-8's teardown reads `Skittering
            // Swarmer(32593) 346.9yd 100% reachable -> LEGITIMATE` about a mob
            // 350yd overhead through solid rock, and that verdict is what left the
            // phantom-combat hatch inert while the party sat wedged for eleven
            // minutes. Bound it at the same DC_ENGAGEMENT_RADIUS IsEngaged uses:
            // one sanity radius for "a combat reference has outlived the geometry
            // it was made in", asked the same way on both sides of the module.
            // Cheap, too — this runs before the per-reference pathfind.
            if (p->GetExactDistSq(other) > DC_ENGAGEMENT_RADIUS * DC_ENGAGEMENT_RADIUS)
                continue;  // left behind by geometry -> not a fight, whatever the mesh says
            if (!DcEngageGeometry::IsReachable(p, other->GetPositionX(),
                                               other->GetPositionY(), other->GetPositionZ()))
                continue;  // unreachable -> the phantom holder
            if (Creature* const c = other->ToCreature())
                if (c->AI() && !c->AI()->CanAIAttack(p))
                    continue;  // its own script forbids it touching us -> phantom too
            // Keep scanning so nearestDist is the CLOSEST such holder: every caller
            // asks a distance question of whichever holder is most nearly on top of
            // us, not of whichever the map happened to enumerate first.
            float const dist = p->GetExactDist(other);
            if (!scan.found || dist < scan.nearestDist)
                scan.nearestDist = dist;
            scan.found = true;
        }
        return scan;
    }

    bool IsHeldByLiveEnemy(Player* p, float radius)
    {
        // Cheap reads first, in the order that short-circuits most ticks: the
        // scan below costs a pathfind per combat reference.
        if (!p || !p->IsAlive() || !p->IsInCombat())
            return false;
        if (IsEngaged(p))
            return true;
        HolderScan const scan = ScanCombatHolders(p);
        // `opaque` deliberately does NOT count. The hatch reads it as "leave this
        // alone", but here the question is "is there something to fight", and a
        // flag with no reference behind it names nothing that could be fought.
        return scan.found && scan.nearestDist <= radius;
    }

    bool AnyPartyHeldByLiveEnemy(Player* bot, float radius)
    {
        if (!bot)
            return false;
        if (IsHeldByLiveEnemy(bot, radius))
            return true;
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (member && member != bot && member->GetMapId() == bot->GetMapId() &&
                IsHeldByLiveEnemy(member, radius))
                return true;
        }
        return false;
    }

    bool MayDrive(Player* bot, AiObjectContext* context)
    {
        if (!bot || !context)
            return false;
        DcApproachState& appr = context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get();
        return DungeonClearMath::MayDriveWhileFlagged(
            bot->IsInCombat(), AnyPartyEngagement(bot), getMSTime(),
            DC_FLAGGED_NO_ENGAGE_GRACE_MS, appr.flaggedNoEngageSinceMs);
    }

    bool IsPhantomFlag(Player* bot, AiObjectContext* context)
    {
        if (!bot || !context)
            return false;
        // PARTY-wide flag, unlike MayDrive's own-flag test: the between-pulls gate
        // waits for every member to top up, so one member an aura holds in combat
        // is enough to make the wait unsatisfiable — and the tank commonly drops
        // combat a second or two before its followers do.
        //
        // The same kernel and the same grace, on its own latch (see
        // DcApproachState). The grace is what stops this firing in the window at
        // the end of every ordinary fight, where the party is still flagged and
        // nothing is engaged any more — waiving the floors there would send the
        // tank to the next pull instead of drinking. Feeding the flag THROUGH the
        // kernel (rather than early-returning on it) is deliberate: an unflagged
        // tick must clear the streak, or a later phantom flag would inherit a
        // stale timestamp and skip its grace entirely.
        DcApproachState& appr = context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get();
        bool const flagged = AnyPartyCombatFlag(bot);
        return DungeonClearMath::MayDriveWhileFlagged(
                   flagged, AnyPartyEngagement(bot), getMSTime(),
                   DC_FLAGGED_NO_ENGAGE_GRACE_MS, appr.partyFlaggedNoEngageSinceMs) &&
               flagged;
    }
}
