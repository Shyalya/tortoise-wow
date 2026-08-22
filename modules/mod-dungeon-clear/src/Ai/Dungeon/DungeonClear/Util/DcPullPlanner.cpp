/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcPullPlanner.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

#include "DungeonClearUtil.h"   // DC_PULL_* macros + DcTargeting::GetPullTarget (until DcTargeting moves)

#include "DcBreadcrumb.h"
#include "DcCombatFlag.h"
#include "DcHazard.h"
#include "DcZoneLine.h"
#include "DungeonClearMath.h"
#include "DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "AttackersValue.h"
#include "CellImpl.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "ItemTemplate.h"
#include "LootMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "InstanceScript.h"
#include "LootObjectStack.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "Chat.h"
#include "ServerFacade.h"
#include "Timer.h"
#include "World.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcEngageGeometry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPullDecision.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPullDecisionIo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearLiveBossValue.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

namespace
{
    // Camp/trail reachability probe. Now a thin alias for the shared
    // DcEngageGeometry::IsNavReachable (was a byte-identical file-local twin here
    // and in DcLeaderSignal). Kept as a local name so the many call sites below
    // read unchanged.
    inline bool IsNavReachable(Player* bot, Position const& p)
    {
        return DcEngageGeometry::IsNavReachable(bot, p);
    }

    // A pull camp must never sit on the far side of — or inside — a door we have
    // not opened yet. The navmesh (and so IsNavReachable / PathGenerator) is BLIND
    // to script/event doors and to a door's runtime collision, so a candidate the
    // route happens to wind toward through a still-shut door reads as "reachable"
    // and the party walks up onto ground it has not legitimately reached (the
    // Shadowfang courtyard-door symptom: the camp lands on the door while the tank
    // is still down fighting toward the first boss). ClosedDoorBetween catches a
    // door strictly BETWEEN the tank and the camp; ClosedDoorNear additionally
    // catches a camp planted IN the doorway itself (which ClosedDoorBetween's
    // interior-projection test skips). Both are computed fresh and cheaply.
    bool CampBlockedByDoor(Player* bot, Position const& c)
    {
        if (!bot)
            return false;
        return DcEngageGeometry::ClosedDoorBetween(
                   bot, c.GetPositionX(), c.GetPositionY(), c.GetPositionZ()) ||
               DcEngageGeometry::ClosedDoorNear(
                   bot, c.GetPositionX(), c.GetPositionY(), c.GetPositionZ(),
                   DC_DOOR_BAND);
    }

    // True when a candidate camp sits in — or the walk to it transits — a
    // persistent damage aura (DcHazardRegistry). A camp is where the party
    // deliberately STANDS AND FIGHTS for the length of a pull, so it is the
    // worst possible place to be inside an aura: the Arcatraz Sentinel's
    // 563-937/second at 15yd kills a camped party outright, and nothing in the
    // combat AI attributes the damage to a mob it is not fighting.
    //
    // Both halves matter. PointIsHot rejects a camp planted in the cylinder;
    // LegIsHot rejects a clean camp whose drag-back route walks the tank and the
    // pack straight through one on the way. Cheap on maps with no rows (one
    // registry bool), so it sits beside CampBlockedByDoor at every site.
    bool CampInHazard(Player* bot, Position const& c)
    {
        if (!bot)
            return false;
        return DcHazard::PointIsHot(bot, c.GetPositionX(), c.GetPositionY(), c.GetPositionZ()) ||
               DcHazard::LegIsHot(bot, c.GetPositionX(), c.GetPositionY(), c.GetPositionZ());
    }

    // True when a candidate camp sits on — or the walk to it crosses — the
    // instance zone line (DcZoneLine). The first pull of a dungeon is the one
    // that trips this: the only cleared ground behind the tank IS the entrance,
    // so the drag-back walks the camp straight back at the exit trigger, and a
    // self-bot standing in it is teleported out of the instance mid-run. The
    // navmesh has no idea areatriggers exist, so IsNavReachable waves it
    // through; this is the only gate that sees it.
    bool CampOverZoneLine(Player* bot, Position const& c)
    {
        if (!bot)
            return false;
        return DcZoneLine::WouldCrossTheLine(bot, c.GetPositionX(), c.GetPositionY(),
                                             c.GetPositionZ());
    }
}

Position DcPullPlanner::ComputeCampSlot(Player* bot, Position const& camp)
{
    if (!bot)
        return camp;

    // Deterministic per-bot offset so the slot is identical every tick: MoveTo
    // dedups an unchanging destination, so the follower glides to one spot and
    // parks there instead of re-pathing to a fresh random point each tick (which
    // would read as a nervous shuffle). Spread the party with the golden angle so
    // even a handful of bots fan out evenly around the anchor rather than
    // overlapping, and pick a 1-2yd radius for the requested gentle variance.
    uint32 const seed = static_cast<uint32>(bot->GetObjectGuid().GetCounter());
    float const angle = static_cast<float>(seed) * 2.39996323f;  // golden angle (rad)
    float const radius = 1.0f + static_cast<float>(seed % 101) / 100.0f;  // [1.0, 2.0]

    float const fx = camp.GetPositionX() + radius * std::cos(angle);
    float const fy = camp.GetPositionY() + radius * std::sin(angle);
    float const fz = camp.GetPositionZ();

    // Route the fuzzed point through the navmesh. PathGenerator clamps an
    // off-mesh destination to the nearest walkable poly, so its actual endpoint
    // is guaranteed to be inside the zone geometry — the slot can never land in a
    // wall or off a ledge. Reject a probe that found no real ground path or had
    // to snap the endpoint far off the mesh, and reject a snap that dragged the
    // slot well past the intended 1-2yd (camp wedged against geometry); in those
    // cases fall back to the exact camp so we never displace a follower onto a
    // worse spot than the anchor itself.
    PathGenerator gen(bot);
    gen.CalculatePath(fx, fy, fz, /*forceDest*/ false);
    if (gen.GetPathType() & (PATHFIND_NOPATH | PATHFIND_FARFROMPOLY))
        return camp;

    G3D::Vector3 const end = gen.GetActualEndPosition();
    Position const slot(end.x, end.y, end.z, camp.GetOrientation());
    if (camp.GetExactDist(&slot) > 3.0f)
        return camp;

    // The anchor was screened for hazards when it was chosen, but this fan-out
    // moves each follower 1-2yd off it — enough to push a bot camped just
    // outside an emitter's rim back inside it. Fall back to the exact camp,
    // which is known clean; a couple of stacked followers is a much cheaper
    // problem than one standing in the aura.
    if (DcHazard::PointIsHot(bot, slot.GetPositionX(), slot.GetPositionY(), slot.GetPositionZ()))
        return camp;

    return slot;
}
Position DcPullPlanner::ComputeHealApproach(Player* bot, Unit* healTarget,
                                            Position const& camp, float healRange)
{
    if (!bot || !healTarget)
        return camp;

    Position const tp = healTarget->GetPosition();

    // Standoff: stop short of the heal target by a margin so we have LOS/range
    // slack and never run onto it. Direction is target -> camp, so the landing
    // point is always on the camp (safe) side of the target.
    float const standoff = healRange * 0.85f;

    float const dx = camp.GetPositionX() - tp.GetPositionX();
    float const dy = camp.GetPositionY() - tp.GetPositionY();
    float const campDist = std::sqrt(dx * dx + dy * dy);

    // Camp already inside heal range of the target (the common drag-back case):
    // no need to advance at all — hold at camp, which is itself in range.
    if (campDist < 0.1f || campDist <= standoff)
        return camp;

    float const ux = dx / campDist;
    float const uy = dy / campDist;
    float const px = tp.GetPositionX() + ux * standoff;
    float const py = tp.GetPositionY() + uy * standoff;
    float const pz = tp.GetPositionZ();

    // Snap through the navmesh exactly like ComputeCampSlot so the point can never
    // land in a wall or off a ledge; fall back to camp on a bad probe.
    PathGenerator gen(bot);
    gen.CalculatePath(px, py, pz, /*forceDest*/ false);
    if (gen.GetPathType() & (PATHFIND_NOPATH | PATHFIND_FARFROMPOLY))
        return camp;

    G3D::Vector3 const end = gen.GetActualEndPosition();
    return Position(end.x, end.y, end.z, camp.GetOrientation());
}
bool DcPullPlanner::ClassifyPullAdvanced(PlayerbotAI* botAI, Unit* target,
                                        DcPullClassification* out)
{
    if (!botAI || !target)
        return false;
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    // Search/broad-phase pad ONLY: widens the grid scan below so no mob whose
    // aggro reach could matter is missed. Pack identity in the estimate itself
    // comes from each mob's own aggro reach and the engine packId in the math
    // kernel, NOT from this constant. (ComputeSafeCamp has its own same-valued
    // kPackRadius, and THERE it does decide packmate identity — which mobs come
    // along to camp regardless and so don't count against camp clearance.)
    constexpr float kPackRadius = 12.0f;

    // Dynamic verdict = estimate how many mobs aggro if we Leeroy on top of the
    // target, then compare to the party's Leeroy ceiling. The reach that decides
    // who aggros is each mob's OWN aggro radius (resolved per mob below), not a
    // hand-tuned distance, so the decision self-tunes per zone/level.
    uint32 const maxLeeroy = DcSettings::GetUInt(bot, "PullDynamicMaxLeeroyMobs");
    float const combatSpread = DcSettings::GetFloat(bot, "PullCombatSpread");
    // The assist-hop reach is the ENGINE's own value, not a DC knob: a creature's
    // CallAssistance() pulls same-faction help within CONFIG_CREATURE_FAMILY_-
    // ASSISTANCE_RADIUS. Reading it directly means the estimate tracks whatever the
    // realm actually uses for assist propagation, with nothing to retune per zone.
    float const assistRadius =
        sWorld.getConfig(CONFIG_FLOAT_CREATURE_FAMILY_ASSISTANCE_RADIUS);

    // Aggro reach is computed against the LOWEST-LEVEL living party member: when
    // the party piles onto the target in a Leeroy, that squishiest body attracts
    // proximity aggro from the farthest out, so it is the unit whose presence at
    // the camp decides who pulls. Fall back to the bot (solo, or no lower member).
    Player* lowMember = bot;
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->GetSource();
            if (!m || !m->IsAlive() || m->GetMapId() != bot->GetMapId())
                continue;
            if (m->GetLevel() < lowMember->GetLevel())
                lowMember = m;
        }
    }

    // Candidate hostiles: gather them with a server-side grid search centred on the
    // PACK (target), NOT on the bot's own scan lists. The bot-cached "far targets"
    // value is range-limited to the bot, throttled (500ms), and was the root of the
    // late-ADVANCED flip: a neighbour pack near the target but on the far side of the
    // tank's approach only entered the bot's view as it walked up, so the verdict
    // resolved at point-blank instead of from first sight. A search around the target
    // sees the whole neighbourhood the instant the pack is picked, regardless of where
    // the tank stands or what it can see — the classification becomes a property of
    // the ROOM, stable across the entire approach. Radius must cover the farthest
    // mob the estimate could count: a proximity mob at the aggro-reach ceiling
    // (~45yd notice + reach) plus combat drift, and an assist buddy one ring
    // beyond that, so nothing the math could count is missed.
    constexpr float kMaxAggroReach = 50.0f;  // GetAttackDistance clamp (45) + reach
    float const searchRadius = kMaxAggroReach + combatSpread + assistRadius + kPackRadius;
    std::list<Creature*> nearby;
    Acore::AnyUnitInObjectRangeCheck check(target, searchRadius);
    Acore::CreatureListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(
        target, nearby, check);
    Cell::VisitObjects(target, searcher, searchRadius);

    std::vector<Unit*> hostiles;
    hostiles.reserve(nearby.size() + 1);
    for (Creature* c : nearby)
    {
        if (!c || !c->IsAlive() || !bot->IsHostileTo(c))
            continue;
        // Critters / non-combat pets / passive ambient never form a pull pack or a
        // chaining add — exclude them so they can't pad the pack-size count.
        // A unit displayed as a corpse is the same story: it never joins the
        // fight, so counting it would inflate the aggro estimate and could flip
        // the Leeroy/Advanced verdict on a pack that is really smaller. Live
        // evidence: tr-20260801-204608-7 logged pull verdicts for the Arcatraz
        // corpse props themselves. See DcEngageGeometry::IsDisplayedDead.
        if (c->IsCritter() || c->IsTotem() || DcEngageGeometry::IsDisplayedDead(c))
            continue;
        hostiles.push_back(c);
    }
    // The target itself must be in the set so its pack is well-defined even if the
    // grid visitor somehow skipped it (despawn race / off-grid edge).
    if (std::find(hostiles.begin(), hostiles.end(), target) == hostiles.end())
        hostiles.push_back(target);

    // Whether a NEIGHBOUR mob counts toward the aggro estimate is gated three
    // ways, all measured PACK->neighbour (target as origin) so the verdict is a
    // stable property of the room, independent of where the tank is standing:
    //
    //   1. Line of sight (static VMAP). A Leeroy fight is STATIONARY at the camp,
    //      so a mob proximity-aggros only if it can actually SEE the party there —
    //      a pillar or wall between the camp and the mob means it never pulls.
    //      Scarlet Cathedral is the textbook case: its nave is full of pillars, so
    //      pairs 12-17yd apart (well inside aggro reach) do NOT chain because the
    //      pillars block sight; gating on LOS keeps those a 2-mob Leeroy instead of
    //      counting six through the stone. (This is the opposite choice from the old
    //      pack-adjacency model, which deliberately ignored LOS because it asked a
    //      different question — "will this pack drag in as the MOVING tank rounds the
    //      corner". The estimate asks "who aggros a fight that stays put", and for
    //      that, sight is exactly the right test.)
    //   2. No closed door between (VMAP LOS treats an open door's frame as clear and
    //      a shut door is a game object, not static geometry, so doors need their
    //      own test).
    //   3. Navmesh connectivity: a complete PATHFIND_NORMAL route within a
    //      corner-slack budget, so a mob that is visible across an unwalkable gap
    //      (a chasm, a ledge) and could never join the melee does not count.
    //
    // Formation/linked packmates bypass this gate entirely — they pull atomically
    // by packId (seed + closure), so a formation member behind a pillar still
    // counts. The gate only decides whether an UNRELATED neighbour aggros.
    //
    // Cost is bounded: every candidate was already gathered within searchRadius of
    // the target; the LOS ray and (only if it passes) the path query run for a
    // handful of mobs, often none.
    float const chainPathBudget = searchRadius * 1.5f;
    auto chainConnected = [&](Unit* u) -> bool
    {
        if (target->GetExactDist2d(u) > searchRadius)
            return false;
        // Static-geometry sight: pillars/walls block proximity aggro. VMAP-only
        // (LINEOFSIGHT_CHECK_VMAP) so an open door doesn't read as blocking and so
        // doors are left to the dedicated test below.
        if (!target->IsWithinLOSInMap(u, VMAP::ModelIgnoreFlags::Nothing,
                                      LINEOFSIGHT_CHECK_VMAP))
            return false;
        if (DcEngageGeometry::ClosedDoorBetween(target, u->GetPositionX(), u->GetPositionY(),
                              u->GetPositionZ()))
            return false;
        PathGenerator gen(target);
        gen.CalculatePath(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ());
        if (gen.GetPathType() != PATHFIND_NORMAL)
            return false;
        Movement::PointsArray const& pts = gen.GetPath();
        float len = 0.0f;
        for (std::size_t k = 1; k < pts.size(); ++k)
            len += (pts[k] - pts[k - 1]).length();
        return len <= chainPathBudget;
    };

    // Engine-pack identity: tag each hostile with a small `packId` (0 = none)
    // derived from what the engine says actually pulls together, so the pure
    // classifier can cluster a strung-out formation as one pack and size it up
    // correctly instead of seeing several lone mobs. Pure geometry both over- and
    // under-clusters; the engine knows the truth.
    //   - Creature formations (GetFormation()->GetId()): mobs that move and aggro
    //     as a unit. The formation group id is already unique and stable, so
    //     formation-mates share it directly with no extra bookkeeping.
    //   - creature_linked_respawn links: a spawn/respawn dependency that in
    //     practice also marks "spawned and pulled together". Used only as a
    //     SECONDARY signal — it unions two ends ONLY when neither already belongs
    //     to a formation pack, so a respawn link can never merge or split a real
    //     formation. Most trash has neither and stays packId 0 (geometry-only).
    std::vector<uint32> packIds(hostiles.size(), 0u);
    std::unordered_map<uint32, uint32> formationToPack;  // formation group id -> packId
    uint32 nextPackId = 1;
    std::unordered_map<ObjectGuid, std::size_t> idxByGuid;
    for (std::size_t i = 0; i < hostiles.size(); ++i)
        idxByGuid[hostiles[i]->GetObjectGuid()] = i;
    // Stage 1: formations (authoritative).
    for (std::size_t i = 0; i < hostiles.size(); ++i)
    {
        Creature* c = hostiles[i]->ToCreature();
        if (!c)
            continue;
        CreatureGroup const* grp = c->GetFormation();
        if (!grp)
            continue;
        uint32 const gid = grp->GetId();
        auto const it = formationToPack.find(gid);
        packIds[i] = (it != formationToPack.end()) ? it->second
                                                   : (formationToPack[gid] = nextPackId++);
    }
    // Stage 2: linked respawn (secondary). Only bridges two candidates when at
    // least one carries no formation pack yet, and never overwrites an existing
    // formation pack — formations stay authoritative.
    for (std::size_t i = 0; i < hostiles.size(); ++i)
    {
        Creature* c = hostiles[i]->ToCreature();
        if (!c)
            continue;
        ObjectGuid const linked = sObjectMgr.GetLinkedRespawnGuid(c->GetObjectGuid());
        if (!linked)
            continue;
        auto const jt = idxByGuid.find(linked);
        if (jt == idxByGuid.end())
            continue;
        std::size_t const j = jt->second;
        if (packIds[i] && packIds[j])
            continue;  // both already (formation-)packed — leave them be
        uint32 const pid = packIds[i] ? packIds[i]
                         : packIds[j] ? packIds[j]
                                      : nextPackId++;
        packIds[i] = pid;
        packIds[j] = pid;
    }

    // Resolve game state (positions, per-mob aggro reach, the connectivity/door
    // gate, and the engine pack id) here, then hand the pure aggro-count estimate
    // to DungeonClearMath::EstimateAggroCount (unit-tested). The gate uses the
    // target itself as the pack's stand-in: packmates sit within a pack radius of
    // it, so connectivity to the target ~= connectivity to the pack, and it keeps
    // eligibility precomputable before the estimate.
    std::vector<DungeonClearMath::DynPullMob> mobs;
    mobs.reserve(hostiles.size());

    // The target's own atomic pack is force-counted by EstimateAggroCount via
    // packId closure REGARDLESS of chainEligible (it short-circuits past the
    // eligibility test, and the seed already covers it so the assist/closure
    // passes never read it either). So running the per-mob navmesh probe in
    // chainConnected() for a packmate of the target is pure waste — its result is
    // discarded. Skip it: the path query (the expensive part of this scan, and
    // the spike in dense formation rooms) now runs ONLY for UNRELATED neighbours,
    // which is exactly the set whose eligibility actually changes the verdict.
    // Zero behaviour change — the math counts these mobs identically either way.
    auto const targetEntry = idxByGuid.find(target->GetObjectGuid());
    uint32 const targetPackId =
        (targetEntry != idxByGuid.end()) ? packIds[targetEntry->second] : 0u;

    std::size_t targetIdx = 0;
    for (std::size_t i = 0; i < hostiles.size(); ++i)
    {
        Unit* u = hostiles[i];
        if (u == target)
            targetIdx = i;
        bool const samePackAsTarget = targetPackId != 0u && packIds[i] == targetPackId;
        bool const chainEligible =
            u != target && !samePackAsTarget && chainConnected(u);
        // Per-mob aggro reach vs the squishiest body: the same core value the
        // commit-range / boss handoff use (GetAggroRange + reach, level-diff
        // scaled). Players/totems keep 0 — they never form a pull pack.
        float aggroReach = 0.0f;
        bool patroller = false;
        bool elite = false;
        if (Creature* c = u->ToCreature())
        {
            aggroReach = c->GetAggroRange(lowMember) + c->GetCombatReach();
            // DB-authored waypoint mover: a predictable patrol a human waits out.
            // RANDOM_MOTION wanderers are excluded — their return isn't predictable.
            patroller = c->GetDefaultMovementType() == WAYPOINT_MOTION_TYPE;
            // Elite-tier (elite / rare-elite / worldboss rank) counts full weight;
            // a normal mob a third — see the pull-weight tally below.
            elite = c->isElite();
        }
        mobs.push_back({u->GetPositionX(), u->GetPositionY(), u->GetPositionZ(),
                        chainEligible, packIds[i], aggroReach, patroller, elite});
    }

    // DC_Z_LEVEL_TOLERANCE: a mob more than this far above/below is on another
    // floor (a ledge/ramp) — it must not seed the pack or count as a proximity /
    // assist aggro just because it is near in plan view. Same constant the
    // corridor/boss-arrival floor guards use elsewhere in this file.
    std::vector<std::size_t> counted;
    uint32 weightThirds = 0;
    uint32 const count = DungeonClearMath::EstimateAggroCount(
        mobs, targetIdx, combatSpread, assistRadius, DC_Z_LEVEL_TOLERANCE,
        /*excludeLonePatrollers*/ false, &counted, &weightThirds);
    // The verdict weighs the counted set, not its raw body count: an elite is a
    // full unit, a normal a third. The ceiling (MaxLeeroyMobs, set in elites) is
    // scaled to the same thirds unit, so a room of weak normal trash no longer
    // forces a cautious Advanced pull while an elite pack still does.
    uint32 const ceilingThirds = maxLeeroy * 3;
    bool const advanced = weightThirds > ceilingThirds;

    // Patrol-wait detail (only when the caller asks for it AND a lone patroller is
    // actually present, so the second O(n^2) pass is skipped otherwise): re-run the
    // estimate with lone patrollers excluded. If the full weight is over the ceiling
    // but the reduced is not, a patrol is the sole reason for the heavy verdict and
    // the governor can hold for it. Pure math on data already gathered. Carried in
    // the same thirds unit as the verdict above so the contended condition stays
    // consistent with it.
    if (out)
    {
        bool lonePatroller = false;
        for (DungeonClearMath::DynPullMob const& m : mobs)
            if (m.patroller && m.packId == 0)
            {
                lonePatroller = true;
                break;
            }
        uint32 reducedThirds = weightThirds;
        if (lonePatroller)
            DungeonClearMath::EstimateAggroCount(
                mobs, targetIdx, combatSpread, assistRadius, DC_Z_LEVEL_TOLERANCE,
                /*excludeLonePatrollers*/ true, nullptr, &reducedThirds);
        out->fullCount = weightThirds;
        out->reducedCount = reducedThirds;
        out->ceiling = ceilingThirds;
        out->bodyCount = count;
    }
    DC_PULL_DEBUG("[DC:{}] dynamic: estimated {} aggro on target {} among {} hostiles "
                  "within {:.0f}yd (low-lvl {}, spread {:.0f}, assist {:.0f}, weight "
                  "{}/3 vs ceiling {} elites = {}/3) -> {}",
                  bot->GetName(), count, target->GetObjectGuid().ToString(), mobs.size(),
                  searchRadius, uint32(lowMember->GetLevel()), combatSpread,
                  assistRadius, weightThirds, maxLeeroy, ceilingThirds,
                  advanced ? "ADVANCED" : "LEEROY");
    // On the surprising verdict (Advanced), dump every hostile the estimate saw —
    // distance to the camp, its computed aggro reach, the eligibility gate, and
    // whether it was COUNTED — so a wrong count can be traced to the exact mobs and
    // the reason (proximity reach too far / assist hop / LOS gate / pack id).
    if (advanced)
    {
        std::unordered_set<std::size_t> countedSet(counted.begin(), counted.end());
        for (std::size_t i = 0; i < mobs.size(); ++i)
        {
            Unit* u = hostiles[i];
            Creature* cr = u->ToCreature();
            DC_PULL_DEBUG("[DC:{}]   mob[{}] entry {} lvl {} {} at {:.1f}yd "
                          "reach {:.1f}+{:.0f}={:.1f} elig {} pack {} -> {}",
                          bot->GetName(), i, u->GetEntry(),
                          cr ? uint32(cr->GetLevel()) : 0u,
                          mobs[i].elite ? "elite" : "normal", target->GetExactDist2d(u),
                          mobs[i].aggroReach, combatSpread,
                          mobs[i].aggroReach + combatSpread,
                          mobs[i].chainEligible ? "Y" : "N", mobs[i].packId,
                          countedSet.count(i) ? "COUNTED" : "-");
        }
    }
    return advanced;
}
void DcPullPlanner::UpdateDynamicPullMode(PlayerbotAI* botAI, AiObjectContext* context)
{
    if (!botAI || !context)
        return;
    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    // Leader-only, active-run-only. The caller is now a per-bot value
    // (DungeonClearPullModeCurrentValue::Calculate) that ANY consumer may read on
    // ANY bot's context, and the pull-setting default is 2 (Dynamic) on every bot —
    // so without this gate a follower-side read would run the full grid classifier
    // on the follower, flip ITS pull-mode bool, seed ITS camp, and grant IT daze
    // immunity. Gate here (leader election is 250ms-cached, cheap) rather than
    // trusting every caller to be leader-gated. `enabled && !paused` also stops a
    // disabled/paused run from mutating pull state on a stray read.
    if (!DcRun::Of(context).enabled ||
        DcRun::Of(context).paused ||
        !DcLeaderSignal::IsDungeonClearLeader(bot))
        return;

    // Off / On are driven by DcPullAction; only Dynamic auto-decides per pack.
    if (context->GetValue<uint32>(DcKey::PullSetting)->Get() != 2u)
        return;

    DcPullContext& pull = context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    bool const curBool = context->GetValue<bool>(DcKey::PullMode)->Get();

    // Never flip the verdict mid-engagement: in combat or any non-Idle pull phase
    // the standing decision is latched until the fight resolves.
    if (bot->IsInCombat() || pull.phase != DcPullPhase::Idle)
        return;

    auto apply = [&](bool want, DcPullDecisionCode decision)
    {
        pull.decision = decision;
        if (want == curBool)
            return;
        context->GetValue<bool>(DcKey::PullMode)->Set(want);
        DcLeaderSignal::SetLeaderDazeImmunity(bot, want);
        // Switching to Advanced: seed a camp so followers have an immediate hold
        // point (mirrors DcPullAction's On activation); the pull pipeline
        // overwrites it with the real safe camp on commit. Seed from the
        // breadcrumb trail — setback BEHIND the tank along walked ground — not at
        // the tank's feet: on a mid-approach LEEROY->ADVANCED upgrade the tank is
        // well forward (sometimes near the pack), and a feet-seed briefly surged
        // the party TOWARD the danger until the Idle prospective-camp publish
        // overwrote it a tick or two later. The trail seed keeps the party's
        // motion monotone (toward ground behind the tank, never forward past it);
        // ComputeTrailCamp itself falls back to the tank position when no trail
        // exists yet (fresh run) — identical to the old seed then.
        if (want)
        {
            float const setback = DcSettings::GetFloat(bot, "PullSetback");
            float const maxDrag = DcSettings::GetFloat(bot, "PullMaxDrag");
            std::optional<Position> const seed = ComputeTrailCamp(botAI, setback, maxDrag);
            // Pull-machinery camp write: PublishCamp stamps ownership so Advance's
            // scout camp-trailing defers (see campPublishedMs / DC_CAMP_PUBLISH_FRESH_MS).
            pull.PublishCamp(seed ? *seed
                                  : Position(bot->GetPositionX(), bot->GetPositionY(),
                                             bot->GetPositionZ()),
                             getMSTime());
        }
    };

    // Capture hook: when RecordDecisions is on (off by default, addon-toggleable
    // per run) append this (observation -> verdict) to the pull-decision capture
    // file so a governor freeze reproduced with capture on becomes a JSONL fixture
    // t/replay_pull.cpp pins forever. The verdict is unchanged whether recording
    // is on or off — capture is a pure side effect.
    auto record = [&](DcPullDecision::PullObservation const& obs,
                      DcPullDecision::PullVerdict v)
    {
        if (DcSettings::GetBool(bot, "RecordDecisions"))
            DcPullDecisionIo::Record(bot->GetObjectGuid().GetRawValue(), getMSTime(), obs, v);
    };

    Unit* target = DcTargeting::GetPullTarget(botAI);
    if (!target)
    {
        // No target this tick. Resolve the latch math (a standing verdict survives
        // a TRANSIENT no-target read — door-veto flicker, long-path mid-rebuild,
        // far-targets poll boundary — until ShouldDropPullVerdict says the loss is
        // continuous past the grace), then let the pure governor decide.
        constexpr uint32 kVerdictGraceMs = 1500;
        DcPullDecision::PullObservation obs;
        obs.hasTarget = false;
        obs.hasStandingVerdict = !pull.decisionTarget.IsEmpty();
        if (!obs.hasStandingVerdict)
            pull.targetLostSince = 0;
        else
            obs.verdictGraceExpired = DungeonClearMath::ShouldDropPullVerdict(
                false, pull.targetLostSince, getMSTime(), kVerdictGraceMs,
                pull.targetLostSince);

        DcPullDecision::PullVerdict const v = DcPullDecision::DecidePull(obs);
        record(obs, v);
        if (v == DcPullDecision::PullVerdict::DropToLeeroy)
        {
            if (obs.hasStandingVerdict)
                DC_PULL_DEBUG("[DC:{}] dynamic: pull target {} stayed lost past the "
                              "{}ms grace -> dropping the standing verdict",
                              bot->GetName(), pull.decisionTarget.ToString(),
                              kVerdictGraceMs);
            pull.decisionTarget = ObjectGuid::Empty;
            pull.targetLostSince = 0;
            apply(false, DcPullDecisionCode::None);
        }
        // HoldNoTarget: within grace — keep verdict, camp hold and daze immunity.
        return;
    }
    pull.targetLostSince = 0;

    // Room-clear override: a room whose RoomAggroRegistry row carries a pullOutRadius
    // (the Mechanar's Sepethrea) shrinks the room-trash exclusion to KEEP a pack
    // parked on the boss's aggro edge as clearable trash. That is only safe if the
    // clear DRAGS that pack out with the advanced pull-to-camp maneuver rather than
    // meleeing it in place — so while such a room-clear is active, FORCE the verdict
    // to Advanced regardless of the pack's estimated size. Pack B there is 4 elites
    // (12 thirds, under the 15 ceiling) and would otherwise classify Leeroy, which a
    // shrunk exclusion turns into a dead-band / boss wake. Advanced is always the
    // safe direction, so forcing it for every pack in that room (A, robots, B alike)
    // is fine. See DcTargeting::RoomClearForcesAdvanced + RoomAggroBoss::pullOutRadius.
    //
    // PullForceAdvanced is the second, operator-driven reason: an A/B lever for
    // measuring "always Advanced" against the tuned ceiling (off by default on
    // both difficulties — see the registry row). Same direction as the room-clear
    // force, so they simply OR together.
    // THIRD force: the target is standing inside ANOTHER pack's aggro, so there is
    // no way to fight it where it stands without that pack joining. Leeroy means
    // "walk in and tank in place", and in that geometry tanking in place is the
    // bug — the estimate sizes who joins a fight that STAYS PUT at the target, and
    // the neighbour joins precisely because the fight stays put.
    //
    // Stormwind Stockade (issue #17) is the case this was written for, and the
    // numbers are what make it not a tuning question. Its central corridor is
    // flanked by cells on BOTH sides at 10-21yd against ~29yd of real aggro reach,
    // so every fight spot in that hall is inside two neighbours at once. Live run
    // tr-20260809-201248-2: 35 LEEROY verdicts to 4 ADVANCED, and the one pull the
    // harness sized predicted 3 mobs and fought 7. The pack weighed 9 thirds
    // against the 15 ceiling — comfortably under, and correctly so, because the
    // ceiling is asking about the PACK and the problem is the ROOM.
    //
    // Advanced is what fixes it: the maneuver drags the pack BACK down the
    // corridor to a camp placed in ground the party already cleared, where no
    // neighbour reaches. That is how a human clears the Stockade. Same direction
    // as the other two forces, so all three simply OR together.
    //
    // SWEEP MAPS ONLY (RouteSweepRegistry), and the gate is explicit rather than
    // inherited. TargetInsideBystanderPack is armed on heroic by PullEnRouteAvoid
    // (default on there), so without this line the force would fire on every
    // heroic map — and on nearly every pack, because a heroic room's formations
    // sit 12-23yd apart against ~30yd spheres. That is PullForceAdvanced in all
    // but name, which the registry row says must never ship on: it costs the full
    // pull FSM on every single-mob pack.
    //
    // The same caution is why the scope is a map list rather than "all normal
    // dungeons": forcing Advanced reshapes how every fight in a dungeon happens,
    // and only the Stockade has the run data to justify it so far. See
    // RouteSweepRegistry.h.
    bool const insideNeighbour =
        DcEngageGeometry::EnRouteSweepApplies(bot) &&
        DcEngageGeometry::TargetInsideBystanderPack(bot, target);

    bool const forceAdv = DcTargeting::RoomClearForcesAdvanced(bot, context) ||
                          insideNeighbour ||
                          DcSettings::GetBool(bot, "PullForceAdvanced");

    // Per-pack latch, UPGRADE-ONLY while approaching the SAME pack.
    //
    // ClassifyPullAdvanced now sizes up the pack with a search centred on the PACK
    // itself (positions, navmesh paths and door tests all measured from the target,
    // never from the tank), so the verdict is a stable property of the room: it reads
    // the SAME from 35yd out as from point-blank. The first-sight decision is therefore
    // already correct and no longer needs to be chased down as the tank closes — the
    // old "neighbour only resolves at the last second" blind spot is gone. We still
    // re-check on a throttle and allow a Leeroy verdict to UPGRADE to Advanced (never
    // downgrade), but only to catch genuine CHANGES in the room — a patrol wandering
    // into chain range, a pack pulled by another fight — not to compensate for our own
    // movement. Once Advanced it is locked for the rest of the approach. One stable,
    // non-flapping decision per pack; the party is never churned Leeroy<->Advanced.
    //
    // Re-check cadence: since the verdict is now a stable property of the room
    // (read identically from 35yd out as point-blank), the re-check exists ONLY to
    // catch genuine room changes — a patrol wandering into chain range, a pack
    // pulled by another fight — not our own approach. Each re-check runs the
    // ClassifyPullAdvanced grid scan (LOS + navmesh probes over the neighbours),
    // so 800ms (vs the old 400ms) halves that cost while still catching a wandering
    // patrol well within the walk-in. UPGRADE-only, so a slower cadence can never
    // cause an under-pull — at worst Advanced is recognised ~400ms later.
    constexpr uint32 kRecheckMs = 800;
    bool const sameTarget = (pull.decisionTarget == target->GetObjectGuid());

    // Resolve a fresh classification into a verdict, layering the patrol-wait gate
    // on top of the plain Leeroy/Advanced decision. Patrol-wait (Dynamic only):
    // when the ONLY reason a pack reads ADVANCED is a lone patroller in chain range
    // (full over ceiling, reduced under), a human holds at commit range and waits
    // the patrol out rather than committing the heavier maneuver — but only once
    // the tank has actually closed to commit range, so it approaches via the normal
    // walk-in and holds at the decision point, not 35yd out. While still
    // approaching such a pack it stays provisional LEEROY (never locking ADVANCED).
    // The hold is decision == 3 (pull mode OFF-but-held): the pull trigger keeps the
    // action live, whose Idle branch halts the tank at commit range, and the
    // blocking/room-trash engages stand down on decision 3 so it doesn't tag. A
    // PullPatrolWaitSec timeout proceeds with the real ADVANCED verdict so a
    // stationary/slow patrol can't stall the run.
    auto resolve = [&](bool advanced, DcPullClassification const& cls)
    {
        // Telemetry stamp (no gate reads these — see DcPullContext). Refreshed on
        // every resolve, including the throttled same-pack re-check, so what an
        // observer reads at commit time is the estimate the COMMITTED verdict was
        // taken from, not the one from first sight.
        pull.predictedThirds  = cls.fullCount;
        pull.predictedCount   = cls.bodyCount;
        pull.predictedCeiling = cls.ceiling;

        // Build the pure observation, run the latch math where the live gate did,
        // then let DecidePull arbitrate. Routing every commit through the pure
        // function is what keeps the captured fixtures honest: live and replay
        // can never drift, because live IS the pure function here.
        bool const patrolWaitEnabled = DcSettings::GetBool(bot, "PullPatrolWait");
        DcPullDecision::PullObservation obs;
        obs.hasTarget = true;            // resolve is only reached with a target
        obs.inCombat = false;            // gated above
        obs.phaseIdle = true;            // gated above
        obs.sameTarget = sameTarget;
        obs.currentlyAdvanced = false;   // a committed-Advanced same-target returns before resolve
        obs.recheckElapsed = true;       // the throttle already passed before resolve
        obs.advanced = advanced;
        obs.patrolWaitEnabled = patrolWaitEnabled;
        obs.patrolContended =
            cls.fullCount > cls.ceiling && cls.reducedCount <= cls.ceiling;

        // Only AT commit range do we run (and latch) the patrol-wait clock, exactly
        // as the live gate did. patrolWaitExpired = ShouldWaitForPatrol said proceed.
        if (advanced && patrolWaitEnabled)
        {
            float const commitRange =
                DcEngageGeometry::PullCommitRange(bot, target, DC_PULL_START_RANGE);
            obs.atCommitRange = bot->GetExactDist2d(target) <= commitRange;
            if (obs.atCommitRange)
            {
                uint32 const waitMs =
                    uint32(DcSettings::GetFloat(bot, "PullPatrolWaitSec") * 1000.0f);
                bool const keepWaiting = DungeonClearMath::ShouldWaitForPatrol(
                    cls.fullCount, cls.reducedCount, cls.ceiling,
                    pull.patrolWaitSince, getMSTime(), waitMs, pull.patrolWaitSince);
                obs.patrolWaitExpired = !keepWaiting;
            }
        }

        DcPullDecision::PullVerdict const v = DcPullDecision::DecidePull(obs);
        record(obs, v);
        switch (v)
        {
            case DcPullDecision::PullVerdict::PatrolWaitHold:
                apply(false, DcPullDecisionCode::PatrolHold);  // hold at commit range; pull mode off, no tag
                DC_PULL_INFO("[DC:{}] dynamic: pack {} patrol-contended (full {} > "
                             "ceiling {}, reduced {}) -> WAITING for patrol",
                             bot->GetName(), target->GetObjectGuid().ToString(),
                             cls.fullCount, cls.ceiling, cls.reducedCount);
                break;
            case DcPullDecision::PullVerdict::ApproachAsLeeroy:
                // Still approaching a patrol-contended pack: stay provisional LEEROY
                // and walk in. Don't run the wait clock until the decision point.
                pull.patrolWaitSince = 0;
                apply(false, DcPullDecisionCode::Leeroy);
                break;
            case DcPullDecision::PullVerdict::Advanced:
                apply(true, DcPullDecisionCode::Advanced);
                break;
            case DcPullDecision::PullVerdict::Leeroy:
            default:
                apply(false, DcPullDecisionCode::Leeroy);
                break;
        }
    };

    if (sameTarget)
    {
        // Already committed ADVANCED for this pack: locked, never reconsidered.
        // (decision == 3 is a HOLD, not a commit — curBool is still false there, so
        // it falls through to the throttled re-check below and keeps re-evaluating
        // the patrol contention until the patrol passes or the wait times out.)
        if (curBool)
            return;
        // Standing Leeroy or patrol-hold: re-check on a throttle. The verdict can
        // UPGRADE to Advanced (room changed) or resolve a patrol hold either way.
        uint32 const now = getMSTime();
        if (pull.decisionSince != 0 && (now - pull.decisionSince) < kRecheckMs)
            return;
        pull.decisionSince = now;
        DcPullClassification cls;
        bool const advanced = ClassifyPullAdvanced(botAI, target, &cls) || forceAdv;
        resolve(advanced, cls);
        return;
    }

    // New pack: size it up fresh and stamp the latch + re-check clock.
    pull.patrolWaitSince = 0;
    DcPullClassification cls;
    bool const advanced = ClassifyPullAdvanced(botAI, target, &cls) || forceAdv;
    pull.decisionTarget = target->GetObjectGuid();
    pull.decisionSince = getMSTime();
    // One tick of the telemetry sequence per NEW pack — the edge an observer
    // splits its per-pull records on. Bumped here and nowhere else: the same-pack
    // re-check above re-stamps the estimate but is still the SAME pull.
    ++pull.decisionSeq;
    pull.decisionTargetEntry = target->GetEntry();
    resolve(advanced, cls);
    // Report the verdict actually APPLIED (resolve can hold a patrol-contended pack
    // as a provisional LEEROY while it walks in, or as a WAIT at commit range), not
    // the raw classification.
    DC_PULL_INFO("[DC:{}] dynamic verdict for pack {}: {}", bot->GetName(),
                 target->GetObjectGuid().ToString(),
                 pull.decision == DcPullDecisionCode::PatrolHold ? "WAITING (patrol)"
                     : pull.decision == DcPullDecisionCode::Advanced ? "ADVANCED" : "LEEROY");
}
std::optional<Position> DcPullPlanner::ComputeSafeCamp(PlayerbotAI* botAI, Unit* target,
                                                          float setback, float safeRadius,
                                                          float maxDrag,
                                                          float& clearanceOut, float& dragOut)
{
    clearanceOut = std::numeric_limits<float>::max();
    dragOut = 0.0f;
    if (!botAI || !target)
        return std::nullopt;
    Player* bot = botAI->GetBot();
    if (!bot)
        return std::nullopt;
    AiObjectContext* ctx = botAI->GetAiObjectContext();

    // Grouping radius: hostiles this close to the pulled target are its own
    // packmates — they come along to camp anyway, so they don't count against
    // camp clearance. Everything farther is an "other" pack we must stay clear of.
    constexpr float kPackRadius = 12.0f;
    // How far apart to sample candidate camp points walking back along the route.
    constexpr float kStep = 4.0f;
    if (maxDrag < setback)
        maxDrag = setback;

    // Ranged LOS-break: if the pulled target fights at range (caster / archer /
    // wand), the camp must additionally break line of sight to it so the rangers
    // are forced to close to melee at camp instead of plinking the party across the
    // room. We keep walking the camp back along the cleared route — usually to the
    // doorway/corner the tank entered through — until a point with no LOS to the
    // pack is found, allowing a larger drag (PullRangedMaxDrag) to reach it. Only
    // the CURRENT target's LOS is tested; its packmates cluster around it. When no
    // out-of-sight point is reachable the normal farthest-back fallback still wins.
    bool const losBreak = DcSettings::GetBool(bot, "PullRangedLosBreak") &&
                          DcEngageGeometry::IsRangedAttacker(bot, target);
    if (losBreak)
    {
        maxDrag = std::max(maxDrag, DcSettings::GetFloat(bot, "PullRangedMaxDrag"));
        DC_PULL_DEBUG("[DC:{}] safe-camp: target {} is a ranged attacker -> requiring "
                      "LOS break, maxDrag extended to {:.0f}yd",
                      bot->GetName(), target->GetObjectGuid().ToString(), maxDrag);
    }
    // VMAP-static LOS from the pack to a candidate camp ground point: a wall or
    // pillar between them means a ranger there can't hit the camp and must move in.
    // VMAP-only (no dynamic objects) so an open door's frame doesn't read as cover —
    // matches the chain-aggro gate in ClassifyPullAdvanced.
    auto breaksLos = [&](Position const& p) -> bool
    {
        // Tortoise port: one LOS backend here, nothing to select or ignore.
        return losBreak &&
               !target->IsWithinLOS(p.GetPositionX(), p.GetPositionY(), p.GetPositionZ());
    };

    // Resolve the other-pack hostiles once (alive, hostile, not the target, not a
    // packmate). Same candidate set the pull / trash scans use.
    GuidVector const& farTargets =
        ctx->GetValue<GuidVector>(DcKey::FarTargets)->Get();
    GuidVector const& possibleTargets =
        ctx->GetValue<GuidVector>(DcKey::Stock::PossibleTargets)->Get();
    GuidVector const& candidates = farTargets.empty() ? possibleTargets : farTargets;

    std::vector<Unit*> others;
    others.reserve(candidates.size());
    for (ObjectGuid guid : candidates)
    {
        Unit* u = ObjectAccessor::GetUnit(*bot, guid);
        if (!u || !u->IsAlive() || u == target)
            continue;
        if (!bot->IsHostileTo(u))
            continue;
        // On another floor (a pack on a ledge/ramp directly above or below): it
        // can't aggro the camp fight, so it must not push the camp farther back.
        // Same height gate the Dynamic decision and the corridor scans use, so
        // placement and classification agree about what "near" means in 3D.
        if (std::fabs(u->GetPositionZ() - target->GetPositionZ()) > DC_Z_LEVEL_TOLERANCE)
            continue;
        if (u->GetExactDist2d(target) <= kPackRadius)
            continue;  // packmate — fought regardless of where camp sits
        others.push_back(u);
    }

    auto clearanceAt = [&others](Position const& p) -> float
    {
        float nearest = std::numeric_limits<float>::max();
        for (Unit* u : others)
            nearest = std::min(nearest, p.GetExactDist2d(u));
        return nearest;
    };

    std::vector<Position> const& crumbs =
        ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get().breadcrumbs;

    Position const tankPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

    // --- Preferred: walk BACK along the breadcrumb trail ------------------
    // The trail is the ground the tank actually walked (oldest -> newest), so it
    // stays valid even though the pull drag-back resets the long-path cursor (the
    // bug that made a cursor-based "point behind" find only a few yards after the
    // first pull). Accumulate corridor distance from the tank backward through it.
    // Dungeon mobs have no leash, so take the first point at least `setback` back
    // that also clears safeRadius, walking further (up to maxDrag) only if a
    // neighbour is still within safeRadius. A gap bigger than kJumpGuard means the
    // trail isn't contiguous there (a drag/teleport boundary) — stop, nothing
    // beyond it is really "behind us". Track the farthest point as the fallback.
    Position best = tankPos;
    float bestClear = clearanceAt(tankPos);
    float bestDrag = 0.0f;
    float bestAlong = 0.0f;
    // Best reachable LOS-breaking point (ranged pulls only): tracked separately so
    // that if no point satisfies BOTH clearance and LOS-break at the full setback,
    // we still prefer the farthest out-of-sight point over a closer in-sight one —
    // breaking the rangers' line is the whole goal even at imperfect clearance.
    Position bestLos = tankPos;
    float bestLosClear = std::numeric_limits<float>::max();
    float bestLosDrag = 0.0f;
    float bestLosAlong = -1.0f;  // <0 => none found
    std::optional<Position> accepted;
    DungeonClearMath::WalkTrailBack(
        crumbs, tankPos, DungeonClearMath::TrailJumpGuard,
        [&](DungeonClearMath::TrailStep const& s) -> bool
        {
            Position const& c = s.crumb;
            float const along = s.along;
            // Only ever return / fall back to a crumb the move can reach over a
            // complete generated path. A crumb within the jump guard but across a
            // navmesh seam would otherwise be committed and the move to it would
            // straight-line under the map. Likewise reject a crumb on the far side
            // of (or inside) a still-shut door: walked-distance "back" along a
            // doubling-back route can land spatially FORWARD, on ground gated by a
            // door we have not opened — the navmesh is blind to it. (Note: an
            // unreachable crumb skips the maxDrag cap below, matching the original
            // `continue`.)
            // Zone line first: it is pure arithmetic over a handful of cached
            // volumes, while IsNavReachable is a full PathGenerator build. At a
            // dungeon's first pull the oldest crumbs are ALL over the line, so
            // short-circuiting here is the difference between rejecting them for
            // free and paying a path build apiece to reject them anyway.
            if (CampOverZoneLine(bot, c) || !IsNavReachable(bot, c) ||
                CampBlockedByDoor(bot, c) || CampInHazard(bot, c))
                return true;
            float const clear = clearanceAt(c);
            float const drag = tankPos.GetExactDist(&c);
            bool const breaks = breaksLos(c);
            if (along > bestAlong)  // farthest reachable back so far (fallback)
            {
                best = c;
                bestClear = clear;
                bestDrag = drag;
                bestAlong = along;
            }
            if (breaks && along > bestLosAlong)  // farthest reachable out-of-sight point
            {
                bestLos = c;
                bestLosClear = clear;
                bestLosDrag = drag;
                bestLosAlong = along;
            }
            // Accept the first point that is far enough back, clears other packs,
            // AND (for a ranged pull) breaks LOS to the pack.
            if (along >= setback && clear >= safeRadius && (!losBreak || breaks))
            {
                clearanceOut = clear;
                dragOut = drag;
                accepted = c;
                return false;
            }
            if (along >= maxDrag)
                return false;
            return true;
        });
    if (accepted)
        return *accepted;

    // Ranged pull, no point cleared every gate: take the farthest out-of-sight
    // point we did find (at least half the setback back), since forcing the rangers
    // to close matters more than perfect pack clearance.
    if (losBreak && bestLosAlong >= setback * 0.5f)
    {
        clearanceOut = bestLosClear;
        dragOut = bestLosDrag;
        return bestLos;
    }

    // Got a meaningful distance back along the trail (at least half the setback):
    // use the farthest point even if a neighbour is within safeRadius — no leash,
    // so far-and-imperfect beats close. (For a ranged pull with no reachable
    // out-of-sight point, this is the graceful "room has no cover" fallback.)
    if (bestAlong >= setback * 0.5f)
    {
        clearanceOut = bestClear;
        dragOut = bestDrag;
        return best;
    }

    // --- Route-anchored fallback: walk back along the actual escort route -----
    // The breadcrumb trail above is starved (a doubling-back, crowded entrance
    // keeps tripping RecordBreadcrumb's jump-guard via short resnap hops, so it
    // resets to a crumb or two). Before any blind geometric projection, ask the
    // long-path follower for a point `setback` yards BACK ALONG THE CLEARED
    // ROUTE behind the cursor. Crucially this can NEVER point forward onto
    // un-walked ground — PointBehind only ever walks the already-travelled
    // polyline behind the cursor — so it is the correct "behind" even when the
    // trail tells us nothing, and it does not depend on a door check that a door
    // up a ramp (a different Z to the chord) can defeat. (PointBehind can come up
    // short once a drag-back has reset the cursor, which is exactly why the
    // breadcrumb trail is preferred ABOVE; at plan time the cursor is still on
    // the forward route, so it reads true here.)
    {
        ChunkedPathfinder::Result const& path =
            ctx->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Get();
        DungeonFollowerState const& follower =
            ctx->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get();
        if (std::optional<G3D::Vector3> back =
                DungeonPathFollower::PointBehind(bot, path, follower, setback))
        {
            Position cand(back->x, back->y, back->z);
            if (IsNavReachable(bot, cand) && !CampBlockedByDoor(bot, cand) &&
                !CampInHazard(bot, cand) && !CampOverZoneLine(bot, cand))
            {
                clearanceOut = clearanceAt(cand);
                dragOut = tankPos.GetExactDist(&cand);
                return cand;
            }
        }
    }

    // --- Fallback: cleared route behind is too short (e.g. first pull) ------
    // Back the party off the pack, snapped to the navmesh, trying the full
    // setback first and shrinking until a walkable point is found.
    //
    // Direction matters and is the crux of the doubling-back bug: projecting
    // "directly away from the pack" is only "back" on a straight approach. On a
    // winding / switchback route the away-from-pack vector can point FORWARD onto
    // ground we have not walked — e.g. straight at the still-shut courtyard door
    // — and the door-blind navmesh snap accepts it. So when we have ANY trail at
    // all, project back along it (tank -> farthest reachable crumb): the way the
    // tank actually came in. Only with no usable trail (a genuine first pull on a
    // straight approach) do we fall back to the away-from-pack vector.
    float dx;
    float dy;
    if (bestAlong > kStep && best.GetExactDist2d(&tankPos) > 0.1f)
    {
        dx = best.GetPositionX() - tankPos.GetPositionX();
        dy = best.GetPositionY() - tankPos.GetPositionY();
    }
    else
    {
        dx = tankPos.GetPositionX() - target->GetPositionX();
        dy = tankPos.GetPositionY() - target->GetPositionY();
    }
    float const len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.1f)
    {
        dx /= len;
        dy /= len;
        for (float d = setback; d >= kStep; d -= kStep)
        {
            NavmeshSnap::Result snap = NavmeshSnap::Snap(
                bot, tankPos.GetPositionX() + dx * d,
                tankPos.GetPositionY() + dy * d, tankPos.GetPositionZ(), 10.0f);
            if (!snap.ok)
                continue;
            Position cand(snap.x, snap.y, snap.z);
            // The projection ignores walls: the snapped point can land on-mesh
            // but on the far side of a wall / on another level. Only keep it if a
            // complete generated path reaches it, so the move never straight-lines
            // through the geometry in between — and never across/into a shut door.
            if (!IsNavReachable(bot, cand) || CampBlockedByDoor(bot, cand) ||
                CampInHazard(bot, cand) || CampOverZoneLine(bot, cand))
                continue;
            float const c = clearanceAt(cand);
            float const drag = tankPos.GetExactDist(&cand);
            if (drag > bestDrag)
            {
                best = cand;
                bestClear = c;
                bestDrag = drag;
            }
            if (c >= safeRadius)
            {
                clearanceOut = c;
                dragOut = drag;
                return cand;
            }
        }
    }

    clearanceOut = bestClear;
    dragOut = bestDrag;
    return best;
}
std::optional<Position> DcPullPlanner::ComputeTrailCamp(PlayerbotAI* botAI,
                                                           float setback, float maxDrag)
{
    if (!botAI)
        return std::nullopt;
    Player* bot = botAI->GetBot();
    if (!bot)
        return std::nullopt;
    AiObjectContext* ctx = botAI->GetAiObjectContext();

    if (maxDrag < setback)
        maxDrag = setback;

    Position const tankPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

    std::vector<Position> const& crumbs =
        ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get().breadcrumbs;

    // Walk BACK along the trail (newest -> oldest) accumulating corridor distance,
    // exactly like ComputeSafeCamp's preferred branch but without the clearance
    // gate: take the first reachable point at least `setback` behind us, stopping
    // at a trail discontinuity (kJumpGuard). Fall back to the farthest reachable
    // contiguous point when the trail is shorter than the full setback (e.g. right
    // after a fight, before the tank has laid much new trail).
    //
    // Reachability (a full PathGenerator build per probe) is tested LAZILY,
    // mirroring DcLeaderSignal::GetLeaderScoutTrailPoint: only crumbs at/past the
    // setback are probed on the walk, and the pre-setback crumbs only as a
    // farthest-first fallback when no post-setback crumb was reachable. The
    // previous version probed every crumb as it walked — one navmesh path build
    // PER CRUMB per scout tick. The selected crumb is the same: the first
    // bucket probes the post-setback crumbs in walk order (matching the eager
    // loop's "first reachable >= setback"), and the fallback probes
    // farthest-first, which is exactly the eager loop's "farthest reachable"
    // running maximum.
    //
    // maxDrag caps how far past the setback the unreachable-crumb continuation
    // may keep probing before giving up and falling back. (The eager version's
    // maxDrag break was dead — it sat after the probe's `continue` and the
    // setback `return`, so the walk used to run to the end of the trail; the
    // cap is live deliberately now, bounding the worst case at a couple of
    // probes instead of the whole 128-crumb buffer.)
    std::vector<std::pair<float, Position>> preSetback;  // (along, crumb), nearest-first
    std::optional<Position> result;
    DungeonClearMath::WalkTrailBack(
        crumbs, tankPos, DungeonClearMath::TrailJumpGuard,
        [&](DungeonClearMath::TrailStep const& s) -> bool
        {
            if (s.along < setback)
            {
                preSetback.emplace_back(s.along, s.crumb);
                return true;
            }
            // Only trail to a crumb the party can reach over a complete generated
            // path — a seam crumb would make the follower move straight-line under
            // the map. Also reject a crumb across/inside a still-shut door: on a
            // doubling-back route walked-distance "back" can land spatially forward,
            // on door-gated ground the party has not legitimately reached. And
            // reject a crumb on/past the zone line: the tank DID walk in through
            // the entrance, so the oldest crumbs of a run sit on the exit trigger
            // and trailing back onto them ports a self-bot out of the instance.
            // Zone line tested first — pure arithmetic, and it spares the
            // PathGenerator build on exactly the entrance crumbs it rejects.
            if (!CampOverZoneLine(bot, s.crumb) && IsNavReachable(bot, s.crumb) &&
                !CampBlockedByDoor(bot, s.crumb))
            {
                result = s.crumb;
                return false;
            }
            if (s.along >= maxDrag)
                return false;  // searched past the cap without a reachable crumb — fall back
            return true;
        });
    if (result)
        return result;

    // Trail too short to reach the full setback (or nothing reachable past it):
    // trail the farthest reachable point we have (the party simply stacks closer
    // behind the tank until more trail accrues).
    for (auto it = preSetback.rbegin(); it != preSetback.rend(); ++it)
        if (IsNavReachable(bot, it->second) && !CampBlockedByDoor(bot, it->second) &&
            !CampOverZoneLine(bot, it->second))
            return it->second;
    return tankPos;
}
void DcPullPlanner::MaintainScoutCamp(PlayerbotAI* botAI, AiObjectContext* ctx)
{
    if (!botAI || !ctx)
        return;
    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    // Lay down the breadcrumb trail the advanced pull places its camp from. Only
    // while out of combat (forward route progress) so the trail stays the cleared
    // path, not a combat-chase scribble.
    if (!bot->IsInCombat())
        DcRecordBreadcrumb(ctx, bot);

    DcPullContext& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();

    // In pull mode the party holds at a camp and leapfrogs camp-to-camp while the
    // tank scouts ahead alone. Make sure a camp always exists for them to hold at:
    // seed it at our current spot whenever it's unset (pull mode just toggled on,
    // or a reset cleared it). Real pulls overwrite it with the computed safe camp.
    if (!ctx->GetValue<bool>(DcKey::PullModeCurrent)->Get())
    {
        // Effective pull mode is OFF but a pull is still standing: RELEASE it.
        // The pull's own FSM cannot do this — DungeonClearPullTrigger gates on the
        // effective mode, so with the mode forced off (a PERSISTENT anchored event
        // driving the tank) the Engage->Idle cleanup is unreachable, and a phase
        // frozen at Engage also blocks the camp trailing below. Meanwhile the
        // followers' hold-at-camp and the party-spread gate read the LATCHED pull
        // bool and keep obeying that frozen camp: the party parks 100yd behind the
        // tank, the event drive holds for a party that was told to stand there, and
        // stranded-recovery undoes/redoes the same rescue every 60s. Dismantling the
        // camp here reverts the party to plain follow, which is exactly the posture
        // the persistent-event override asks for. See DungeonClearMath::
        // ShouldReleaseStandingPull for the guards (never mid-maneuver).
        //
        // PARTY-WIDE combat, not the tank's own flag. The tank in Engage is often
        // flag-clear while the followers fight the pack it just dragged home — a
        // scripted stage tags at range, so the pack arrives strung out and lands on
        // whoever it reaches first. Asking only the tank tore the camp down mid
        // camp-fight and sent it off to form the next pull with the last pack still
        // standing. DcCombatFlag::AnyPartyEngagement is the module's one definition
        // of "somebody is actually fighting" and already carries this exact warning.
        if (DungeonClearMath::ShouldReleaseStandingPull(
                /*effectiveOn*/ false, /*standing*/ pull.phase != DcPullPhase::Idle || pull.HasCamp(),
                DcCombatFlag::AnyPartyEngagement(bot),
                DcLeaderSignal::IsPullPhaseHolding(static_cast<uint32>(pull.phase)),
                pull.bossPullback) &&
            DcLeaderSignal::IsDungeonClearLeader(bot))
        {
            DC_PULL_INFO("[DC:{}] pull released: mode off with a pull still standing "
                         "(phase {}, camp {:.1f}yd) -> phase idle, camp cleared, party "
                         "follows", bot->GetName(), static_cast<uint32>(pull.phase),
                         pull.HasCamp() ? bot->GetExactDist(&pull.camp) : 0.0f);
            pull.Transition(DcPullPhase::Idle, getMSTime());
            pull.PublishCamp(Position(), getMSTime());
        }
        return;
    }

    Position& camp = pull.camp;
    // Capture the unset state BEFORE seeding: the trail block below adopts the
    // trailing point unconditionally on the first tick a camp was just seeded.
    bool const campUnset = !pull.HasCamp();
    float const setback = DcSettings::GetFloat(bot, "PullSetback");
    float const maxDrag = DcSettings::GetFloat(bot, "PullMaxDrag");
    if (campUnset)
    {
        // Seed from the trail (setback behind the tank along walked ground)
        // rather than at the tank's feet, for the same monotone-party-motion
        // reason as the dynamic-upgrade seed in UpdateDynamicPullMode: a
        // feet-seed has the party walk forward TO the tank instead of holding
        // behind it. ComputeTrailCamp falls back to the tank position itself
        // when no trail exists yet (mode just toggled on, tank hasn't moved).
        std::optional<Position> const seed = ComputeTrailCamp(botAI, setback, maxDrag);
        camp = seed ? *seed
                    : Position(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    }

    // TRAIL the camp forward while merely scouting (phase Idle, out of combat).
    // Without this the camp stays frozen at the LAST fight's spot until a new
    // pull commits, so after every camp fight the tank glides ahead to the next
    // pack while the party runs all the way BACK to the stale camp — the huge
    // tank/party gap the player reported. By creeping the camp to a point
    // PullSetback behind the moving tank each tick, hold-at-camp re-issues the
    // followers toward it so they walk ALONG behind the tank and pause at its
    // trailing position, exactly as a real party would.
    //
    // Ownership is by TIMESTAMP, not by "is a pack in pull-scan range": the
    // pull action stamps campPublishedMs on every camp write, and this trail
    // defers only while that stamp is fresh (DC_CAMP_PUBLISH_FRESH_MS). The
    // old GetPullTarget probe was a weaker condition than the gates the pull
    // TRIGGER actually needs to fire (no tank loot, abort-target pack, party
    // ready) — any tick the two disagreed NOBODY moved the camp, and with the
    // spread gate anchored at that frozen camp (right where the party stood,
    // post-fight) the tank kept gliding away unchecked: the scout-runaway gap.
    // Forward-only: adopt the new trailing point only when it sits closer to
    // the tank than the current camp (i.e. more forward), with a few yards of
    // hysteresis, so tick jitter never churns it or drags the party backward.
    bool const pullOwnsCamp =
        getMSTimeDiff(pull.campPublishedMs, getMSTime()) < DC_CAMP_PUBLISH_FRESH_MS;
    if (bot->IsInCombat() || pull.phase != DcPullPhase::Idle || pullOwnsCamp)
        return;

    if (std::optional<Position> trail = ComputeTrailCamp(botAI, setback, maxDrag))
    {
        Position const tankPos(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        if (campUnset ||
            trail->GetExactDist2d(&tankPos) + 3.0f < camp.GetExactDist2d(&tankPos))
        {
            camp = *trail;
            DC_PULL_TRACE("[DC:{}] scout: trailing camp -> ({:.1f},{:.1f},{:.1f}) "
                          "{:.1f}yd behind tank", bot->GetName(),
                          camp.GetPositionX(), camp.GetPositionY(),
                          camp.GetPositionZ(), tankPos.GetExactDist2d(&camp));
        }
    }
}
bool DcPullPlanner::IsPartySetAtCamp(Player* leader, Position const& camp, float setRadius)
{
    if (!leader)
        return false;
    Group* group = leader->GetGroup();
    if (!group)
        return true;  // solo tank — nobody to wait on

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == leader)
            continue;
        if (member->GetMapId() != leader->GetMapId())
            continue;
        if (member->isDead())
            continue;  // dead members handled by the party-died trigger
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI)
            continue;  // real player — never gate the pull on them
        // 3D distance, matching DcPartyState::IsPartyReady's spread gate against the
        // same camp anchor. A camp can land on a ramp/stairs via the breadcrumb
        // walk; a 2D measure here would disagree with the 3D between-pulls gate about
        // the same follower at the same camp — the module's repeat-offender
        // metric-mismatch class ("arrived? test + target must share a metric").
        if (member->GetExactDist(&camp) > setRadius)
            return false;
        // Healers are deliberately never made fully passive (ApplyFollowerPassive
        // pins them with "stay" instead so they can heal the tank through the
        // drag-back), so requiring the passive strategy from them here would wait
        // out the full Forming timeout on EVERY pull with a healer in the group —
        // the gate would block on a flag the camp-hold code is designed never to
        // set on a healer. Mirror that: a healer counts as "set" on position
        // alone; every other member must also carry the passive strategy (parked
        // AND holding fire) before the party is ready to tag.
        if (!PlayerbotAI::IsHeal(member) &&
            !memberAI->HasStrategy("passive", BOT_STATE_COMBAT))
            return false;
    }
    return true;
}
