/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonEventExecutor.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <optional>
#include <unordered_set>
#include <vector>

#include "CombatManager.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectData.h"
#include "GossipDef.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "Ai/Dungeon/DungeonClear/Util/DcFormGate.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Log.h"
#include "MotionMaster.h"
#include "MoveSplineInitArgs.h"
#include "Player.h"
#include "Playerbots.h"
#include "Spell.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcCombatFlag.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"

namespace
{
    // Default arrival radius for an event MoveTo when the step doesn't override
    // it; kept tighter than the objective arrive default since these are usually
    // intra-room hops to a specific interactable.
    constexpr float DC_EVENT_MOVE_RADIUS = 4.0f;
    // How far out to look for a step's GameObject when no search radius is given.
    constexpr float DC_EVENT_GO_SEARCH = 20.0f;
    // Range from which a GameObject may legitimately be Use()d (it has no range
    // check of its own — same rule the door system enforces).
    constexpr float DC_EVENT_GO_USE_RANGE = 5.0f;
    // Absolute floor for the "the event lapsed" gap (see EventStaleGapMs). Never
    // below a few ticks of headroom even if the playerbots delays are configured
    // down to nothing.
    constexpr uint32 DC_EVENT_STALE_FLOOR_MS = 3000;
    // How many step-timeouts an event may run without its high-water step index
    // increasing before it is declared wedged. Generous: a Wait/WaitForSpawn step
    // legitimately sits for its whole timeout, so this only fires well past the
    // point where the per-step timeout should already have escalated.
    constexpr uint32 DC_EVENT_NO_PROGRESS_FACTOR = 3;

    // How long a TeleportParty will wait for the party to finish a fight before
    // relocating anyway (see the step). Sized to cover a real trash camp at the
    // checkpoint with room to spare — Azjol-Nerub's six-plus Skittering Swarmers
    // die in well under a minute. The gate itself no longer arms on a bare combat
    // flag (it asks AnyPartyHeldByLiveEnemy), so this bounds the case a flag test
    // cannot: a fight that is real but unwinnable from the checkpoint, which
    // would otherwise hold a REQUIRED event until the run's no-progress watchdog.
    // Past it the relocation fires and DropCombatLeftBehind cleans up, which is a
    // better outcome than either stalling or teleporting instantly.
    constexpr uint32 DC_RELOCATION_COMBAT_HOLD_MS = 60000;

    // Gap after which a Drive call reads as a FRESH activation rather than a
    // tick-to-tick continuation.
    //
    // This used to be a flat 1000ms, which quietly assumed the driving bot ticks
    // faster than 1s. Stock playerbots does not guarantee that: PlayerbotAI::
    // GetReactDelay() returns reactDelay times a random 10-30 multiplier — so
    // 1000-3000ms at the default 100ms base — for a bot that is out of combat,
    // not resting and has
    // no real-player master, and AllowActivity() going minimal parks it at
    // passiveDelay (10s) outright. A tank standing at an event anchor is exactly
    // that bot. Every Drive then read as a fresh activation, rewound to step 0,
    // and the event could never reach step 1 — a silent permanent livelock that
    // scaled in with server load (it needed 8-10 concurrent test instances to
    // show up, which is why single-instance validation missed it).
    //
    // So derive the threshold from the same config the throttle uses, and give it
    // 2x headroom: a gap only means "lapsed" if it is longer than any tick stock
    // playerbots can legitimately hand us.
    uint32 EventStaleGapMs()
    {
        uint32 const slowestTick = std::max<uint32>(sPlayerbotAIConfig.passiveDelay,
                                                    sPlayerbotAIConfig.reactDelay * 30u);
        return std::max<uint32>(DC_EVENT_STALE_FLOOR_MS, slowestTick * 2u);
    }
    // How far out WaitForSpawn / KillCreature scan for the named creature.
    constexpr float DC_EVENT_CREATURE_SCAN = 250.0f;
    // Range from which a creature can be gossiped (mirrors the core's
    // GetNPCIfCanInteractWith INTERACTION_DISTANCE check; kept a hair tighter).
    constexpr float DC_EVENT_GOSSIP_RANGE = 5.0f;
    // UseItemOnGO cast reach: STRICT world distance (never the object-size-
    // inflated IsWithinDistInMap, which passes at ~6yd+ where the item use
    // already fails). The real gate is GameObject::IsAtInteractDistance — the
    // barrel's model box grown by ~5yd, which live-measured failing at 6.0yd
    // and passing at 5.5 (per-barrel, orientation-dependent). 3.5yd sits well
    // inside every box; the driver's forced walk-in can always deliver it.
    constexpr float DC_EVENT_GO_PLANT_REACH = 3.5f;
    // UseItemOnGO target pick: a candidate GO counts as "the step's GO" only
    // within this of the step's (x,y,z) anchor. POOLED spawns make this load-
    // bearing (Old Hillsbrad's barrels: 5 pools of 3 candidate positions each,
    // max_limit 1 — ONE barrel per house at a random spot ≤21yd from the house
    // centroid the step anchors on, while the nearest NEIGHBOUR house's barrel
    // is ≥38yd away). Without the cap, a mid-approach scan that only sees a
    // neighbour's already-planted barrel matches it and the step false-latches
    // Done (live: steps 2-3 insta-Done'd on house 1's barrel and the run
    // stalled at 3/5 plants). No candidate within the cap = "not loaded /
    // not spawned yet" -> keep walking to the anchor.
    constexpr float DC_EVENT_GO_ANCHOR_MATCH = 25.0f;
    // How far out a Gossip step ACQUIRES its (unique) NPC in order to walk to it.
    // Deliberately wide: the freed crew can settle well beyond the gossip range
    // (the ZulFarrak crew descend to the temple floor), and the approach must
    // start from wherever the prior steps left the tank — so this is the "walk to
    // the NPC" radius, distinct from the 5yd interaction range. The entry is
    // unique, so a wide flat scan can only return the intended NPC.
    constexpr float DC_EVENT_GOSSIP_APPROACH = 100.0f;

    // How close to a ClearRadius centre the tank must be before its "no hostile
    // left" answer is trusted as "the room is clear" (see the ClearRadius case in
    // RunStep). The volume's candidate filter runs a STRICT per-candidate
    // reachability probe FROM THE BOT, so the verdict is only as good as the
    // vantage point: taken from outside the volume it reports clear over live
    // trash. A hair above the 8yd default MoveTo parking radius, so a tank that
    // has settled on the anchor is never ping-ponged by float noise, and tight
    // enough that the probe's reach comfortably spans any sane clear radius.
    constexpr float DC_EVENT_CLEAR_JUDGE_RADIUS = 12.0f;

    // Issue a one-shot move toward (x,y,z) if not already moving there. Short,
    // intra-room hops — the surrounding objective travel got the party into the
    // room; this just closes the last few yards to an interactable.
    void HopTo(Player* bot, float x, float y, float z)
    {
        if (bot->isMoving())
            return;
        bot->GetMotionMaster()->MovePoint(0, x, y, z, FORCED_MOVEMENT_NONE, 0.0f, 0.0f,
                                          /*generatePath*/ true, false);
    }

    // A Jump step bridges an OFF-MESH gap (a drop-down ledge): the leader leaps
    // it, but the followers' path-follow has no navmesh across the gap and they
    // strand on the lip. Once the leader has landed, relocate any party BOT still
    // stuck on the far side to the tank. (A follower jump would hit the same
    // missing mesh; the teleport is the robust fix and is user-sanctioned.)
    void PullStrandedFollowersAcross(Player* leader, float lx, float ly, float lz)
    {
        Group* group = leader->GetGroup();
        if (!group)
            return;

        // Past this from the landing => "didn't make the drop". Comfortably wider
        // than the jump span so a follower that DID land is never yanked back.
        constexpr float DC_JUMP_STRANDED_DIST = 15.0f;

        uint32 idx = 0;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (!member || member == leader)
                continue;
            if (!member->IsInWorld() || !member->IsAlive())
                continue;
            if (member->GetMapId() != leader->GetMapId())
                continue;
            if (!GET_PLAYERBOT_AI(member))  // only relocate bots, never a human
                continue;
            if (member->GetExactDist(lx, ly, lz) <= DC_JUMP_STRANDED_DIST)
                continue;                   // already across

            // Land them ON the landing, fanned out a little so they don't stack on
            // one point. Fan around the explicit (lx,ly,lz) rather than the leader's
            // live position: a TeleportParty leader is relocated by a NearTeleportTo
            // whose position update can lag a tick (pending ack), so reading its live
            // coords could scatter the followers back at the checkpoint. (For Jump /
            // DropInHole the leader is already physically on the landing, so this is
            // the same point.) Drop any stale follow spline first so it can't drag
            // them back toward the lip.
            float const angle = leader->GetOrientation() + static_cast<float>(idx) * 0.7f;
            float const off = 1.5f + 0.5f * static_cast<float>(idx);
            float const tx = lx + std::cos(angle) * off;
            float const ty = ly + std::sin(angle) * off;

            member->GetMotionMaster()->Clear();
            member->NearTeleportTo(tx, ty, lz, member->GetOrientation(),
                                   /*casting*/ false, /*vehicle*/ false, /*withPet*/ true);
            ++idx;

            LOG_DEBUG("playerbots.dungeonclear",
                      "[dungeon-clear] {} pulled stranded follower {} across the jump gap",
                      leader->GetName(), member->GetName());
        }
    }

    // Drop the combat the party is carrying across a one-way relocation.
    //
    // A TeleportParty crosses a navmesh break BY DEFINITION — that is the whole
    // reason the step exists — so anything still swinging at the party when it
    // fires is left on the far side of geometry nobody can walk back through. The
    // combat references survive the teleport in both directions: the party stays
    // flagged, and the bots' own combat engine keeps driving them at attackers it
    // can never reach. Live on Azjol-Nerub, whose drop checkpoint sits inside a
    // Skittering Swarmer camp (six spawns within 16yd of it): the party teleported
    // mid-fight and then ran back up the lower kingdom toward mobs 350yd away and
    // 360yd up.
    //
    // The RunStep gate below means this should normally have nothing to do — the
    // relocation waits for combat to end. It is the backstop for the cases the
    // gate cannot cover: a follower flagged by something the leader is not, an
    // add that aggros on the teleport tick, and the bounded-hold expiry.
    //
    // Both directions, or it does not stick: clearing only the party's side leaves
    // the creature's threat reference to re-flag them on its next update.
    //
    // AND THE HOLDERS LIVE IN THE COMBAT MANAGER, NOT IN getAttackers(). That set
    // holds only units whose CURRENT VICTIM is this member; a mob that tagged a
    // bot and then picked someone else — or picked nobody, which is every add
    // whose target just vanished 360yd downward — is not in it at all, while its
    // CombatReference goes on holding the member flagged. The first cut of this
    // function walked getAttackers() alone and cleared NOTHING in the case it was
    // written for: tr-20260818-223003-8's teardown reads
    //
    //   Oschue [engine=combat attackers=0 victim=-] held by Skittering Swarmer
    //   (32593) 346.9yd 100% reachable -> LEGITIMATE
    //
    // — `attackers=0` next to a live holder, eleven minutes after the drop. The
    // PvE combat refs are the authoritative "who has me in combat" list (it is
    // what DcCombatFlag::ScanCombatHolders and the teardown snapshot both walk),
    // so walk those, and keep the attacker set as a superset guard for anything
    // mid-swing that has not registered a reference yet.
    //
    // Holders are collected BEFORE anything is cleared: CombatReference::EndCombat
    // deletes the reference it is iterating and CombatStop mutates the attacker
    // set, so both containers are unsafe to walk while dropping. GUID-deduped
    // because the two sources overlap, and re-checked for IsInWorld because a
    // summon's AI may despawn itself out of JustExitedCombat.
    void DropCombatLeftBehind(Player* leader)
    {
        Group* group = leader->GetGroup();
        uint32 cleared = 0;

        auto dropFor = [&cleared](Player* member)
        {
            if (!member || !member->IsInWorld())
                return;

            std::vector<Unit*> holders;
            std::unordered_set<uint64> seen;
            auto const collect = [&holders, &seen](Unit* u)
            {
                if (u && seen.insert(u->GetObjectGuid().GetRawValue()).second)
                    holders.push_back(u);
            };
            // Tortoise port: hostile refs instead of AzerothCore combat pairs,
            // same set - see DcCombatFlag::ScanCombatHolders.
            for (HostileReference* ref = member->GetHostileRefManager().getFirst(); ref; ref = ref->next())
                collect(ref->getSourceUnit());
            for (Unit* const attacker : member->getAttackers())
                collect(attacker);

            for (Unit* const holder : holders)
            {
                if (!holder->IsInWorld())
                    continue;
                holder->GetThreatMgr().ClearAllThreat();
                holder->CombatStop(true);
                ++cleared;
            }
            member->GetThreatMgr().ClearAllThreat();
            member->CombatStop(true);
        };

        dropFor(leader);
        if (group)
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->getSource();
                if (!member || member == leader)
                    continue;
                if (member->GetMapId() != leader->GetMapId())
                    continue;
                if (!GET_PLAYERBOT_AI(member))  // never touch a human's combat
                    continue;
                dropFor(member);
            }
        }

        if (cleared)
            LOG_INFO("playerbots.dungeonclear",
                     "[dungeon-clear] {}: relocation dropped {} combat holder(s) left on "
                     "the far side of the break",
                     leader->GetName(), cleared);
    }
}

bool DungeonEventExecutor::IsOnDropLanding(Player* bot, EventStep const& step)
{
    if (!bot)
        return false;
    MotionMaster* mm = bot->GetMotionMaster();
    // The MoveFall spline runs in an EFFECT_MOTION_TYPE generator that POPS the
    // moment the spline reaches the floor — a reliable, server-managed "the fall
    // FINISHED" signal, so we fire only once the bot is actually at the bottom (not
    // mid-shaft, which teleported the party into the walls). Do NOT also test
    // MOVEMENTFLAG_FALLING: a server bot has no client to send the fall-land packet
    // that clears it, so it stays stuck on and would wedge this gate forever (the
    // party strands up top and the tank dies — the first observed bug). Require the
    // bot to also be well below the hole mouth (step.z, the ledge height) so a tick
    // before the fall has started (no EFFECT generator yet) doesn't read as landed.
    bool const fallSplineRunning =
        mm && mm->GetCurrentMovementGeneratorType() == EFFECT_MOTION_TYPE;
    return !fallSplineRunning && bot->GetPositionZ() < step.z - 15.0f;
}

bool DungeonEventExecutor::HasGameObjectLos(Player* bot, GameObject* go)
{
    if (!bot || !go)
        return false;
    Map* map = bot->GetMap();
    if (!map)
        return false;
    // Eye-bumped, vmap-only ray (same pattern as FindStandoffPoint): sees over
    // floor clutter and through dynamic GOs, never through a house wall.
    return map->isInLineOfSight(bot->GetPositionX(), bot->GetPositionY(),
                                bot->GetPositionZ() + 2.0f, go->GetPositionX(),
                                go->GetPositionY(), go->GetPositionZ() + 1.0f,
                                bot->GetPhaseMask(), LINEOFSIGHT_CHECK_VMAP,
                                VMAP::ModelIgnoreFlags::Nothing);
}

bool DungeonEventExecutor::SelectGossip(Player* bot, Creature* npc, int32 option)
{
    if (!bot || !npc)
        return false;

    // Open the menu — synchronously populates PlayerTalkClass with this NPC as
    // the gossip-menu sender and its DB options.
    bot->SetFacingToObject(npc);
    WorldPacket hello;
    hello << npc->GetObjectGuid();
    bot->GetSession()->HandleGossipHelloOpcode(hello);

    GossipMenu& menu = bot->PlayerTalkClass->GetGossipMenu();
    // Tortoise port: this menu hands out items by index with no by-id lookup;
    // presence IS the index check.
    if (menu.MenuItemCount() == 0 ||
        static_cast<uint32>(option) >= menu.MenuItemCount())
        return false;  // menu/option not ready yet — caller retries

    // Send the NPC's OWN guid: HandleGossipSelectOptionOpcode rejects the select
    // unless the packet guid equals the open menu's sender GUID, and a real bot's
    // master isn't targeting this NPC. See the Gossip step note below.
    auto sendSelect = [&](uint32 menuId, uint32 opt)
    {
        WorldPacket select;
        select << npc->GetObjectGuid() << menuId << opt;
        select << std::string();  // no coded box
        bot->GetSession()->HandleGossipSelectOptionOpcode(select);
    };

    // Capture the id BEFORE selecting: the select rebuilds PlayerTalkClass's menu
    // in place (opening a submenu), so `menu.GetMenuId()` would already read the
    // submenu's id afterward.
    uint32 lastMenuId = menu.GetMenuId();
    sendSelect(lastMenuId, static_cast<uint32>(option));

    // DRILL DOWN through submenus: some scripted NPCs put the option that fires
    // their action behind one or more nested gossip menus (Old Hillsbrad's Thrall,
    // post-Skarloc: menu 7830 -> 7829 -> 7831, and only 7831's option triggers his
    // DoAction). A single select would just open the next submenu into
    // PlayerTalkClass and never reach the terminal option — so keep selecting
    // option 0 of whatever menu is now open until it CLOSES (the terminal select
    // ClearGossipMenuFor's it). Bounded, and bails if the menu stops changing, so a
    // self-referential menu can't loop. A plain single-level gossip (the common
    // case) closes on the first select and skips the loop entirely.
    for (int guard = 0; guard < 6; ++guard)
    {
        GossipMenu& sub = bot->PlayerTalkClass->GetGossipMenu();
        if (sub.MenuItemCount() == 0)
            break;  // menu closed -> the terminal option fired
        if (sub.GetMenuId() == lastMenuId)
            break;  // no new submenu opened -> nothing more to drill
        lastMenuId = sub.GetMenuId();
        sendSelect(sub.GetMenuId(), 0);
    }
    return true;
}

StepResult DungeonEventExecutor::RunStep(Player* bot, AiObjectContext* context,
                                         EventStep const& step, DungeonEventProgress& prog,
                                         uint32 nowMs)
{
    if (!bot)
        return StepResult::Blocked;

    switch (step.kind)
    {
        case EventStepKind::MoveTo:
        {
            float const radius = step.radius > 0.0f ? step.radius : DC_EVENT_MOVE_RADIUS;
            if (bot->GetExactDist(step.x, step.y, step.z) > radius)
            {
                HopTo(bot, step.x, step.y, step.z);
                return StepResult::Running;
            }
            // Arrived. A plain MoveTo (no gate) is done. A GARRISON MoveTo holds
            // here until its gate clears — and because distance is re-checked every
            // tick, a later tick that finds the bot displaced (combat pushed the
            // tank off the spot, e.g. chasing a wave down the ramp) re-moves it back.
            //
            // A garrison may also carry a hook to run WHILE it holds (see
            // EventBuilder::WhileHolding), for a gate that can go BACKWARDS behind
            // the party's back. The hook's result is deliberately ignored: the gate
            // below is still the only thing that ends the step.
            if (step.hookId != 0)
            {
                DungeonBossInfo dummy;  // hooks key off bot/context, not info
                ObjectiveHookRegistry::Run(step.hookId, bot, context, dummy);
            }
            //
            // Instance-data gate (preferred for a value killed mid-combat): hold
            // until the map's scripted phase counter reaches the threshold. This is
            // MONOTONIC, so unlike "boss alive" it can't be missed while the event
            // engine is dormant in combat — once the phase climbs past the gate it
            // stays past it, so the step clears the moment the party next ticks
            // out of combat (even if the gated content is already done).
            if (step.instanceDataId >= 0)
            {
                InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
                uint32 const v = inst ? inst->GetData(static_cast<uint32>(step.instanceDataId)) : 0;
                return (v >= step.instanceDataMin) ? StepResult::Done : StepResult::Running;
            }
            // Persistent-data gate: identical contract, different store. Scripts
            // that keep a wave counter in the InstanceScript persistent vector
            // (SetPersistentDataCount/StorePersistentData) usually never override
            // GetData, so the instance-data gate above would read a permanent 0
            // there. The Mechanar bridge gauntlet is exactly that shape — every
            // far-wave death bumps DATA_BRIDGE_MOB_DEATH_COUNT, and 4 is what makes
            // Pathaleon attackable. See EventStep::persistentDataId.
            if (step.persistentDataId >= 0)
            {
                InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
                uint32 const v =
                    inst ? inst->GetPersistentData(static_cast<uint32>(step.persistentDataId)) : 0;
                return (v >= step.persistentDataMin) ? StepResult::Done : StepResult::Running;
            }
            // Creature gate: hold until the gate creature matches wantAlive.
            if (step.creatureEntry != 0)
            {
                Creature* c = bot->FindNearestCreature(step.creatureEntry,
                                                       DC_EVENT_CREATURE_SCAN,
                                                       /*alive*/ step.wantAlive);
                return ((c != nullptr) == step.wantAlive) ? StepResult::Done
                                                          : StepResult::Running;
            }
            return StepResult::Done;
        }

        case EventStepKind::Jump:
        {
            float const radius = step.radius > 0.0f ? step.radius : DC_EVENT_MOVE_RADIUS;
            if (bot->GetExactDist(step.x, step.y, step.z) <= radius)
            {
                // Landed. The followers can't path across the off-mesh gap, so
                // pull any stranded on the far side over to the tank. Idempotent:
                // a follower already across is skipped, so a restart is a no-op.
                PullStrandedFollowersAcross(bot, step.x, step.y, step.z);
                return StepResult::Done;
            }
            // While the jump spline is in flight the bot reads as moving — don't
            // re-issue (MoveJump would restart the arc and never land). Only fire
            // a fresh jump once the bot is settled on the lip.
            if (!bot->isMoving())
            {
                float const speed = bot->GetSpeed(MOVE_RUN);
                MotionMaster* mm = bot->GetMotionMaster();
                mm->Clear();
                mm->MoveJump(step.x, step.y, step.z, speed, speed, 1);
                LOG_DEBUG("playerbots.dungeonclear",
                          "[dungeon-clear] {} event-step Jump -> ({:.1f},{:.1f},{:.1f})",
                          bot->GetName(), step.x, step.y, step.z);
            }
            return StepResult::Running;
        }

        case EventStepKind::UseGameObject:
        {
            float const search = step.radius > 0.0f ? step.radius : DC_EVENT_GO_SEARCH;
            GameObject* go = bot->FindNearestGameObject(step.goEntry, search);
            if (!go)
                return StepResult::Running;  // not in range yet / not spawned
            // Idempotent: a lever/door already in a non-READY (activated) state
            // has been used — toggling it again would undo it (re-close the cell
            // gate). Treat as already done so a restart of the step chain is safe.
            if (go->GetGoState() != GO_STATE_READY)
                return StepResult::Done;
            if (!bot->IsWithinDistInMap(go, DC_EVENT_GO_USE_RANGE))
            {
                HopTo(bot, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
                return StepResult::Running;
            }
            // GameObject::Use() silently early-returns on GO_FLAG_NOT_SELECTABLE,
            // so clicking one is a guaranteed no-op — reporting Done here would
            // latch the objective complete on a click that never happened and send
            // the party on to content the click was supposed to unlock. Several
            // scripted interactables spawn not-selectable and are cleared only by a
            // prerequisite (Steamvault's access panels, flagged until their own
            // mini-boss dies). Stay Running so the step's timeout escalates it into
            // a visible stall the human can act on.
            if (go->HasGameObjectFlag(GO_FLAG_NOT_SELECTABLE))
            {
                LOG_DEBUG("playerbots.dungeonclear",
                          "[dungeon-clear] {} event-step Use GO {} '{}' is NOT_SELECTABLE "
                          "— click would be swallowed, holding",
                          bot->GetName(), go->GetObjectGuid().ToString(), go->GetName());
                return StepResult::Running;
            }
            LOG_DEBUG("playerbots.dungeonclear",
                      "[dungeon-clear] {} event-step Use GO {} '{}'",
                      bot->GetName(), go->GetObjectGuid().ToString(), go->GetName());
            go->Use(bot);
            return StepResult::Done;
        }

        case EventStepKind::WaitForSpawn:
        {
            Creature* c = bot->FindNearestCreature(step.creatureEntry, DC_EVENT_CREATURE_SCAN,
                                                   /*alive*/ step.wantAlive);
            bool const present = c != nullptr;
            // wantAlive: done once it's up. !wantAlive: done once it's gone.
            return (present == step.wantAlive) ? StepResult::Done : StepResult::Running;
        }

        case EventStepKind::WaitForGameObjectState:
        {
            float const search = step.radius > 0.0f ? step.radius : DC_EVENT_GO_SEARCH;
            GameObject* go = bot->FindNearestGameObject(step.goEntry, search);
            if (!go)
                return StepResult::Running;
            return (static_cast<uint32>(go->GetGoState()) == step.wantState)
                       ? StepResult::Done
                       : StepResult::Running;
        }

        case EventStepKind::KillCreature:
        {
            // Gate only: the executor never engages (the surrounding objective
            // action holds position). Killing is the engage pipeline's job — this
            // step just reports Done once the named creature is no longer alive
            // nearby. Used by the conditional room-aggro fold-in (milestone 3),
            // where the event preempts above the engage triggers so the party
            // actually fights. `count` is approximated as "any alive blocks".
            float const search = step.radius > 0.0f ? step.radius : DC_EVENT_CREATURE_SCAN;
            Creature* c = bot->FindNearestCreature(step.creatureEntry, search, /*alive*/ true);
            return c ? StepResult::Running : StepResult::Done;
        }

        case EventStepKind::ClearRadius:
        {
            // Gate only, like KillCreature: Running while any reachable hostile
            // remains inside the point's radius/floor band, Done once none do. The
            // engage itself is driven by DcObjectiveArriveAction (engage pipeline).
            //
            // This used to lean on "the arrive trigger only fires once the tank is
            // AT the objective, so nothing can be judged prematurely". That is an
            // assumption about every caller's arriveRadius, and it does not hold —
            // see the vantage-point check below.
            float const r = step.radius > 0.0f ? step.radius : 50.0f;
            Unit* u = DcTargeting::NearestHostileNearPoint(bot, context, step.x, step.y,
                                                           step.z, r, step.zBand,
                                                           &step.entryFilter);
            if (u)
                return StepResult::Running;

            // NOTHING FOUND — which is only evidence the room is clear if the
            // tank was standing somewhere it could actually SEE the room.
            // NearestHostileNearPoint filters every candidate through the STRICT
            // DcEngageGeometry::IsEngageReachable probe, a single
            // PathGenerator::CalculatePath from the BOT. That probe degrades to
            // "unreachable" well before the volume's own radius runs out: a mob
            // 90yd off is past PathGenerator's poly budget and comes back
            // PATHFIND_INCOMPLETE, and requireDirect additionally rejects any
            // route far longer than the straight line. So a verdict taken from
            // outside the volume reads "clear" over a room full of live trash.
            //
            // Live (Sethekk Halls, heroic): the tank crossed the objective's
            // arrive radius at the room's south doorway, the event advanced to
            // this step while it was still ~80yd from the summon anchor, this
            // gate answered "clear" on the first evaluation, and the very next
            // step poked the Anzu summon into an untouched room — the NE/NW
            // packs were never engaged by anything, because AtObjective (30)
            // outranks BlockingTrash (25) and this step is the only rung that
            // sweeps a room rather than a path corridor.
            //
            // So the verdict is only trusted from INSIDE the judging radius —
            // i.e. essentially at the anchor, where the strict probe's reach
            // covers the whole volume. From anywhere else, walk back and stay
            // Running. This also re-centres the tank after it has chased a mob
            // to the far edge, so the next sweep is judged from the anchor too.
            float const botToCentre = bot->GetExactDist(step.x, step.y, step.z);
            if (botToCentre > DC_EVENT_CLEAR_JUDGE_RADIUS)
            {
                LOG_DEBUG("playerbots.dungeonclear",
                          "[DC:{}] ClearRadius: no hostile in r={:.0f} of "
                          "({:.0f},{:.0f},{:.0f}) but judged from {:.1f}yd out "
                          "(> {:.0f}) — too far to certify, returning to the "
                          "centre",
                          bot->GetName(), r, step.x, step.y, step.z, botToCentre,
                          DC_EVENT_CLEAR_JUDGE_RADIUS);
                HopTo(bot, step.x, step.y, step.z);
                return StepResult::Running;
            }

            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] ClearRadius DONE: no hostile in r={:.0f} of "
                      "({:.0f},{:.0f},{:.0f}); botDistToCentre={:.1f}",
                      bot->GetName(), r, step.x, step.y, step.z, botToCentre);
            return StepResult::Done;
        }

        case EventStepKind::Wait:
        {
            return (getMSTimeDiff(prog.stepStartMs, nowMs) >= step.durationMs)
                       ? StepResult::Done
                       : StepResult::Running;
        }

        case EventStepKind::Custom:
        {
            DungeonBossInfo dummy;  // legacy hooks key off bot/context, not info
            ObjectiveArriveResult const r =
                ObjectiveHookRegistry::Run(step.hookId, bot, context, dummy);
            switch (r)
            {
                case ObjectiveArriveResult::Done:    return StepResult::Done;
                case ObjectiveArriveResult::Running: return StepResult::Running;
                case ObjectiveArriveResult::Blocked: return StepResult::Blocked;
            }
            return StepResult::Done;
        }

        case EventStepKind::Gossip:
        {
            // Talk to creatureEntry and pick gossipOption. We drive the gossip
            // OPCODES directly rather than GossipHelloAction::Execute, because its
            // select path (ProcessGossip) sends GetMaster()->GetTarget() as the
            // packet guid — and the core's HandleGossipSelectOptionOpcode rejects
            // the select unless that guid equals the open menu's sender GUID. For
            // a real bot whose master isn't targeting this NPC, the select is a
            // silent no-op (the prisoner never gets the GOSSIP_SELECT and never
            // opens the door). Sending the NPC's own GUID makes it land.
            // Acquire across a WIDE approach radius and walk to the NPC, instead
            // of gating the approach on a tight search radius. The preceding engage
            // steps often complete instantly — the ZulFarrak temple bosses are
            // already dead from the continuous wave combat — so they never drive
            // the tank's descent and leave it parked up at the ramp, while Weegli /
            // Bly walk down to the temple floor (>40yd). A tight acquire radius then
            // strands this step until timeout (observed: the Weegli gossip never
            // fired and the door to Chief Ukorz never opened). An author-specified
            // larger radius is still honoured.
            float const search = step.radius > DC_EVENT_GOSSIP_APPROACH
                                     ? step.radius
                                     : DC_EVENT_GOSSIP_APPROACH;
            Creature* npc = bot->FindNearestCreature(step.creatureEntry, search, /*alive*/ true);
            if (!npc)
            {
                // Optional target: if it's not merely outside the approach radius
                // but actually gone (no alive one anywhere nearby), SKIP the step
                // rather than waiting forever for an NPC that will never come (a
                // freed ZulFarrak helper the party let die). The wide rescan
                // separates "dead/gone" (skip) from "still walking in" (approach).
                if (step.skipIfMissing &&
                    !bot->FindNearestCreature(step.creatureEntry, DC_EVENT_CREATURE_SCAN,
                                              /*alive*/ true))
                {
                    LOG_DEBUG("playerbots.dungeonclear",
                              "[dungeon-clear] {} event-step Gossip target {} gone — skipping",
                              bot->GetName(), step.creatureEntry);
                    return StepResult::Done;
                }
                return StepResult::Running;  // walking in / not spawned yet
            }

            // Wait out a scripted walk: don't talk to an NPC that is still moving
            // to its final spot (the ZulFarrak crew descending to the temple
            // floor — their gossip is offered before they arrive). Hold until it
            // settles; combat/normal ticks continue meanwhile.
            if (step.waitForStill && npc->isMoving())
            {
                LOG_DEBUG("playerbots.dungeonclear",
                          "[dungeon-clear] {} event-step Gossip target {} still moving — waiting",
                          bot->GetName(), npc->GetObjectGuid().ToString());
                return StepResult::Running;
            }

            if (!bot->IsWithinDistInMap(npc, DC_EVENT_GOSSIP_RANGE))
            {
                HopTo(bot, npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ());
                return StepResult::Running;
            }

            LOG_DEBUG("playerbots.dungeonclear",
                      "[dungeon-clear] {} event-step Gossip {} '{}' option {}",
                      bot->GetName(), npc->GetObjectGuid().ToString(), npc->GetName(),
                      step.gossipOption);

            // Drive the open-menu + select opcodes (shared with the escort step's
            // self-heal start). Running while the menu/option isn't populated yet.
            return SelectGossip(bot, npc, step.gossipOption) ? StepResult::Done
                                                             : StepResult::Running;
        }

        case EventStepKind::EscortCreature:
        {
            // PURE COMPLETION GATE only. The follow + threat-engage + self-heal
            // gossip + watchdog are all driven by the ACTION
            // (DcObjectiveArriveAction::DriveEscortCreature), which owns the tick
            // while the escort is in progress and only falls through to this Drive
            // once the final boss exists. Done strictly when that boss is up (grid
            // scan) or its encounter bit is set — NEVER on "reached the end" (the
            // DM-West / RFD premature-completion class of bug).
            if (step.escortDoneEntry &&
                bot->FindNearestCreature(step.escortDoneEntry, DC_EVENT_CREATURE_SCAN,
                                         /*alive*/ true))
                return StepResult::Done;
            if (step.escortDoneBit >= 0)
            {
                InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
                if (inst && (inst->GetCompletedEncounterMask() &
                             (1u << static_cast<uint32>(step.escortDoneBit))))
                    return StepResult::Done;
            }
            // Instance-data completion (Old Hillsbrad's Thrall escort ends not on a
            // boss going live but on the map's monotonic progress counter reaching
            // FINISHED). Monotonic, so a >= gate can't be missed while the engine is
            // dormant in combat.
            if (step.instanceDataId >= 0)
            {
                InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
                if (inst && inst->GetData(static_cast<uint32>(step.instanceDataId)) >=
                                step.instanceDataMin)
                    return StepResult::Done;
            }
            return StepResult::Running;
        }

        case EventStepKind::DropInHole:
        {
            // PURE gate + follower teleport. The leader's glide-over-the-hole and
            // pure-vertical MoveFall are driven by the ACTION (DriveDropInHole),
            // which owns the tick so the at-objective Hold can't cancel the off-mesh
            // nudge spline; RunStep is reached only once the action hands back — i.e.
            // the leader has finished falling and is down on the deep floor. Pull any
            // follower still held up top down to the landing (they can't reproduce
            // the off-mesh nudge; the teleport is the sanctioned one-way-drop fix),
            // then report Done so the objective latches and the clear advances to the
            // escort. Idempotent: a follower already down is skipped.
            if (!DungeonEventExecutor::IsOnDropLanding(bot, step))
                return StepResult::Running;
            // Clear the stuck fall state. MoveFall set MOVEMENTFLAG_FALLING and no
            // client ever clears it for a bot, so without this the leader stays
            // "falling" forever and can't swim/walk out to the escort afterward.
            bot->RemoveUnitMovementFlag(MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR);
            bot->GetMotionMaster()->Clear();
            // MoveFall targets the vmap GROUND (the lakebed), which in WC sits a few
            // yards below the water-SURFACE navmesh sheet the mmaps bake (the deep
            // floor we routed to). Settle the leader exactly on the landing so stock
            // nav resumes cleanly (and the followers fan out around it there).
            if (std::fabs(bot->GetPositionZ() - step.landZ) > 3.0f)
                bot->NearTeleportTo(step.landX, step.landY, step.landZ,
                                    bot->GetOrientation());
            PullStrandedFollowersAcross(bot, step.landX, step.landY, step.landZ);
            return StepResult::Done;
        }

        case EventStepKind::TeleportParty:
        {
            // User-sanctioned one-way relocation across a navmesh break the bots
            // cannot path (a big DIAGONAL drop: a pure-vertical DropInHole would land
            // in the wrong column and a ballistic Jump clips/overshoots). The
            // objective anchor's navigation walks the leader up the ramp to the
            // checkpoint; once there, teleport the leader down to the landing and pull
            // every party bot across to it. Fully synchronous — no action driver, no
            // mid-fall tick ownership — so it returns Done the same tick it fires.
            float const radius = step.radius > 0.0f ? step.radius : DC_EVENT_MOVE_RADIUS;
            // Idempotent: already on the landing (a tick-gap restart before the
            // objective latched) -> re-pull any straggler and report Done, never
            // re-teleport the leader.
            if (bot->GetExactDist(step.landX, step.landY, step.landZ) <= radius)
            {
                PullStrandedFollowersAcross(bot, step.landX, step.landY, step.landZ);
                return StepResult::Done;
            }

            // NOT MID-FIGHT. The relocation crosses a navmesh break, so every
            // attacker the party is holding when it fires is stranded on the far
            // side — still in each other's combat, with the bots' combat engine
            // driving them back at mobs they cannot reach. Azjol-Nerub made this
            // unmissable: its checkpoint sits inside a Skittering Swarmer camp
            // (six spawns within 16yd), so the party arrived in combat almost
            // every run, teleported anyway, and then ran back toward the swarmers
            // 350yd behind and 360yd above them.
            //
            // ASK THE PARTY, NOT THE LEADER, AND ASK FOR A FIGHT RATHER THAN A
            // FLAG. This step only ever runs from the NON-combat engine
            // (DungeonClearStrategy is STRATEGY_TYPE_NONCOMBAT and owns the
            // at-objective rung), so a leader swinging at something never reaches
            // here at all. The Azjol-Nerub shape is a swarmer chewing on a
            // FOLLOWER while the leader — no victim, and often not even flagged,
            // since the core flag is per-unit and does not propagate to the group
            // — walks up and fires the teleport. A leader-only `IsInCombat()`
            // gate is blind to exactly the case it was written for, and that is
            // how tr-20260818-223003-8 relocated with follower Oschue mid-fight:
            // the hold never armed and the step logged no wait at all.
            //
            // The other half is WHICH combat may hold a required event. A bare
            // flag must not: a phantom flag (an area aura, a stale reference, a
            // holder on the far side of a gate) would park this for the full
            // bound every single run, and the whole point of the bound is that
            // parking is the bad outcome. AnyPartyHeldByLiveEnemy asks the
            // question that matters — is a live, reachable enemy within
            // DC_FIGHT_HOLDER_RADIUS of any member — so a real camp fight holds
            // the relocation and a phantom one goes straight through to the
            // teleport and the scrub below, which is where it gets fixed.
            //
            // Waiting is nearly always right when it does arm — the camp is on
            // the route and the pull pipeline is already killing it — so hold,
            // and DON'T let the wait burn the step's timeout: a fight is not a
            // wedged step, and letting it escalate to Failed would stall a
            // REQUIRED event and end the run. Still bounded, because a fight that
            // genuinely cannot be finished at the checkpoint must not hold the
            // run to the no-progress watchdog: past the bound, relocate anyway
            // and let DropCombatLeftBehind clean up.
            if (DcCombatFlag::AnyPartyHeldByLiveEnemy(bot, DC_FIGHT_HOLDER_RADIUS))
            {
                if (!prog.relocationCombatHoldMs)
                    prog.relocationCombatHoldMs = nowMs;
                if (getMSTimeDiff(prog.relocationCombatHoldMs, nowMs) < DC_RELOCATION_COMBAT_HOLD_MS)
                {
                    prog.stepStartMs = nowMs;
                    prog.progressMs = nowMs;
                    return StepResult::Running;
                }
                LOG_INFO("playerbots.dungeonclear",
                         "[dungeon-clear] {}: party still fighting after {} ms at the "
                         "relocation checkpoint -> teleporting anyway and dropping the "
                         "leftover combat",
                         bot->GetName(), getMSTimeDiff(prog.relocationCombatHoldMs, nowMs));
            }
            prog.relocationCombatHoldMs = 0;
            // The at-objective Hold keeps the leader on the checkpoint; with a
            // generous gate radius the objective's own arrival always satisfies this,
            // so the teleport never fires from mid-ramp. (Reached only if combat
            // somehow displaced the leader.) Actively re-approach the checkpoint —
            // mirroring the MoveTo step — instead of passively waiting on a hold
            // nothing drives: without this a fight at the checkpoint that drags the
            // leader >radius idles the (required) event until its timeout fails it
            // into a stall. By construction the checkpoint is connected mesh the
            // leader stood on seconds ago.
            if (bot->GetExactDist(step.x, step.y, step.z) > radius)
            {
                HopTo(bot, step.x, step.y, step.z);
                return StepResult::Running;
            }
            bot->GetMotionMaster()->Clear();
            bot->NearTeleportTo(step.landX, step.landY, step.landZ, bot->GetOrientation());
            PullStrandedFollowersAcross(bot, step.landX, step.landY, step.landZ);
            // Backstop for whatever combat survived the gate above (a follower
            // flagged by something the leader was not, an add that landed on the
            // teleport tick, or the bounded hold expiring). Everything the party
            // was fighting is now on the far side of the break.
            DropCombatLeftBehind(bot);
            // The leader just moved a long way in zero ticks: every cached route
            // artifact — the long-path polyline, its follower cursor, and any
            // in-flight async build (submitted from the PRE-teleport position) —
            // now describes the wrong start. Left alone, the next Advance re-enters
            // the stale polyline with a straight opening spline back toward the
            // old cursor and the tank sprints the wrong way (seen live: post-Brazen
            // drop-off, tank ran back toward the Old Hillsbrad entrance until a
            // stray mob interrupted it). Invalidate it all; EnsureLongPath rebuilds
            // from the post-teleport position (and its start-drift guard discards
            // any result the old position already baked).
            {
                DcApproachState& appr =
                    context->GetValue<DcApproachState&>(DcKey::ApproachState)->Get();
                appr.longPathTargetEntry = 0;
                appr.longPathExpiresMs = 0;
                appr.pendingPathJob = 0;   // orphaned result reaped by the mailbox sweep
                appr.pendingPathSinceMs = 0;
                context->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Reset();
                context->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get() =
                    DungeonFollowerState{};
                context->GetValue<uint32>(DcKey::CurrentHop)->Set(0u);
                // AND THE TARGET ITSELF. The objective latches cleared the moment
                // this returns Done, but NextDungeonBossValue is a CACHED value: for
                // the rest of its interval it keeps naming the objective we just
                // completed, whose anchor is the checkpoint — now 360yd overhead and
                // on the far side of the break. Advance duly builds a route to it,
                // gets an unreachable partial that wanders off across the landing
                // chamber, and glides it. Live on Azjol-Nerub: one second after the
                // teleport the tank issued a 110yd spline from the landing back
                // NORTH to (565.9, 572.3, 300.8) and ran it. Dropping the cache here
                // makes the next tick re-derive with the latch already in place.
                context->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)
                    ->Reset();
            }
            LOG_DEBUG("playerbots.dungeonclear",
                      "[dungeon-clear] {} TeleportParty: ({:.1f},{:.1f},{:.1f}) -> "
                      "landing ({:.1f},{:.1f},{:.1f})",
                      bot->GetName(), step.x, step.y, step.z,
                      step.landX, step.landY, step.landZ);
            return StepResult::Done;
        }

        case EventStepKind::CastSpell:
        {
            // Leader casts spellId on self, triggered (bypassing cost / cooldown /
            // reagents / cast time / item requirement). Used for a scripted
            // "use a quest item" spell whose effect a bot cannot otherwise reach
            // — Sunken Temple's "Awaken the Soulflayer" (12346), normally fired
            // via Yeh'kinya's Scroll / Egg of Hakkar to summon the Shade of
            // Hakkar. The spell's SEND_EVENT script only checks the instance +
            // event-not-started, so a direct triggered cast summons the Shade
            // without the quest item, and is idempotent (it no-ops once the event
            // has started). One-shot: cast and report Done.
            if (step.spellId == 0)
                return StepResult::Done;
            LOG_DEBUG("playerbots.dungeonclear",
                      "[dungeon-clear] {} event-step CastSpell {}",
                      bot->GetName(), step.spellId);
            bot->CastSpell(bot, step.spellId, true);
            return StepResult::Done;
        }

        case EventStepKind::UseItem:
        {
            // Leader USES a quest item (granting it first if the bag lacks it —
            // bots never ran the questline that awards it). This goes through the
            // full item-use cast path (CastItemUseSpell), which supplies the
            // cast-item context the bare spell lacks: Sunken Temple's Egg of
            // Hakkar (10465) casts "Awaken the Soulflayer" (12346) ONLY when used
            // as an item — a direct CastSpell(12346) is rejected before its
            // SEND_EVENT fires (verified: the instance's TYPE_HAKKAR_EVENT stayed
            // NOT_STARTED). The summon happens at a fixed position, but the egg
            // must be used FROM the encounter room, so the anchor sits at the room
            // centre. One-shot: use and report Done.
            if (step.itemId == 0)
                return StepResult::Done;
            Item* item = bot->GetItemByEntry(step.itemId);
            if (!item)
            {
                bot->AddItem(step.itemId, 1);
                item = bot->GetItemByEntry(step.itemId);
                if (!item)
                    return StepResult::Running;  // bags full this tick — retry
            }
            LOG_INFO("playerbots.dungeonclear",
                     "[dungeon-clear] {} event-step UseItem {} (encounter trigger)",
                     bot->GetName(), step.itemId);
            // A feral-form druid leader can't cast the item's spell at all
            // (CheckShapeshift) — shift back first. See DcFormGate.
            DcFormGate::DropBlockingForm(bot, item);
            SpellCastTargets targets;
            targets.setUnitTarget(bot);
            bot->CastItemUseSpell(item, targets, 0, 0);
            return StepResult::Done;
        }

        case EventStepKind::UseItemOnGO:
        {
            // Plant a bomb on the barrel the step's (x,y,z) anchor sits on. The
            // barrel's only interaction is SMART_EVENT_SPELLHIT of the bomb spell,
            // and that spell (32744) is SPELL_EFFECT_OPEN_LOCK against the barrel's
            // ITEM lock (1682, key = the bomb pack) — the exact Deadmines-cannon
            // shape: the cast only reaches the GO when fired FROM the key item via
            // the full item-use path (CastItemUseSpell). A bare/triggered
            // CastSpell(go, spellId) does not reproduce the item-use cast and never
            // registers. The approach INTO the house is driven tick-owning
            // (DriveUseItemOnGO); this RunStep resolves the target and fires once
            // the tank is at it.
            //
            // Target the goEntry GO NEAREST THE ANCHOR (not nearest the tank): the
            // party bombs one barrel per house and the barrels share an entry, so an
            // anchor-relative pick keeps five barrels five DISTINCT targets.
            if (step.itemId == 0 || step.spellId == 0 || step.goEntry == 0)
                return StepResult::Done;  // mis-authored step: skip loudly in review, not a stall

            bool const haveAnchor = step.x != 0.0f || step.y != 0.0f || step.z != 0.0f;
            // Interaction reach: the successful OPEN_LOCK ends in Player::SendLoot,
            // which range-checks against the GO's interact box — from farther out
            // the cast fails silently, the goober never activates, and the success
            // latch below never trips (live: a tank parked at 6.0yd spam-cast for
            // 151s). STRICT world distance + LOS; see DC_EVENT_GO_PLANT_REACH.
            float const castRange = step.radius > 0.0f ? step.radius : DC_EVENT_GO_PLANT_REACH;

            std::list<GameObject*> gos;
            bot->GetGameObjectListWithEntryInGrid(gos, step.goEntry, 80.0f);
            GameObject* target = nullptr;
            float best = 1e18f;
            for (GameObject* g : gos)
            {
                if (!g)
                    continue;
                float const d = haveAnchor ? g->GetExactDist(step.x, step.y, step.z)
                                           : g->GetExactDist(bot);
                if (haveAnchor && d > DC_EVENT_GO_ANCHOR_MATCH)
                    continue;  // some OTHER house's barrel — never this step's target
                if (d < best)
                {
                    best = d;
                    target = g;
                }
            }
            if (!target)
            {
                // The step's GO isn't in grid range (or its pooled spawn isn't
                // loaded) — walk toward the anchor to load it.
                if (haveAnchor)
                    HopTo(bot, step.x, step.y, step.z);
                return StepResult::Running;
            }

            // SUCCESS LATCH: a landed plant Uses the goober (OPEN_LOCK -> SendLoot
            // -> SetLootState(GO_ACTIVATED)), and the barrel's 86400s autoclose
            // keeps it activated for the rest of the run — so "left GO_READY" is a
            // stable, per-barrel "this one is planted". Also makes the step
            // idempotent across an event restart.
            if (target->getLootState() != GO_READY)
                return StepResult::Done;

            // Arrived = strictly in reach AND vmap-visible. The LOS half is load-
            // bearing: a tank within reach of a barrel on the FAR SIDE of a wall
            // (thin house walls; 3D distance ignores them) would otherwise
            // spam-cast through the wall forever — the cast fails silently and
            // the latch never trips. DriveUseItemOnGO uses the same predicate,
            // and while it is active it owns the approach; the HopTo here is only
            // the fallback when the driver isn't in the loop.
            if (bot->GetExactDist(target) > castRange || !HasGameObjectLos(bot, target))
            {
                HopTo(bot, target->GetPositionX(), target->GetPositionY(),
                      target->GetPositionZ());
                return StepResult::Running;
            }

            // The plant cast (item use, ~2s "Opening") is already in flight — let
            // it finish; re-casting every tick would interrupt it forever.
            if (bot->IsNonMeleeSpellCast(false))
                return StepResult::Running;

            // At the barrel. Grant the quest item (bots never ran the questline
            // that awards it) and USE it on the GO — the item-use cast supplies the
            // lock's key context, the spell hits, and the SmartAI SPELLHIT counts it.
            Item* item = step.itemId ? bot->GetItemByEntry(step.itemId) : nullptr;
            if (step.itemId && !item)
            {
                bot->AddItem(step.itemId, 1);
                item = bot->GetItemByEntry(step.itemId);
            }
            if (!item)
                return StepResult::Running;  // bags full this tick — retry
            LOG_INFO("playerbots.dungeonclear",
                     "[dungeon-clear] {} event-step UseItemOnGO: use item {} (spell {}) "
                     "on GO {} '{}' (dist {:.1f})",
                     bot->GetName(), step.itemId, step.spellId,
                     target->GetObjectGuid().ToString(), target->GetName(),
                     bot->GetExactDist(target));
            // Bear/cat form silently rejects the plant (feral forms are
            // CAN_ONLY_CAST_SHAPESHIFT_SPELLS, so CheckShapeshift fails the
            // item-use cast before it reaches the barrel) — a druid tank would
            // otherwise spam-cast here for the whole step timeout. Shift back to
            // caster form first; the removal is synchronous, so the cast below
            // goes out this same tick. See DcFormGate.
            DcFormGate::DropBlockingForm(bot, item);
            SpellCastTargets goTargets;
            goTargets.setGOTarget(target);
            bot->CastItemUseSpell(item, goTargets, 0, 0);
            return StepResult::Running;  // the lootState latch above confirms + advances
        }

        default:
            // Not yet implemented. Blocked makes an accidentally authored step
            // stall visibly rather than silently pass.
            LOG_WARN("playerbots.dungeonclear",
                     "[dungeon-clear] {} event-step kind {} not yet implemented",
                     bot->GetName(), static_cast<uint32>(step.kind));
            return StepResult::Blocked;
    }
}

EventDriveOutcome DungeonEventExecutor::Advance(DungeonEvent const& ev, DungeonEventProgress& prog,
                                                StepResult result, uint32 nowMs,
                                                uint32 defaultTimeoutMs)
{
    if (prog.stepIndex >= ev.steps.size())
        return EventDriveOutcome::Completed;

    EventStep const& step = ev.steps[prog.stepIndex];
    uint32 const timeout = step.timeoutMs ? step.timeoutMs : defaultTimeoutMs;

    // The EscortCreature step has NO flat timeout: a 30s default would mis-fire
    // during the Disciple's 32.5s banish channel and the long ritual hold. Its
    // own dead-air watchdog (DriveEscortCreature) owns liveness instead, so the
    // step is never escalated to Failed here regardless of elapsed time.
    bool const watchdogOwned = step.kind == EventStepKind::EscortCreature;

    // Forward-progress backstop, checked BEFORE the result dispatch because the
    // wedge it catches never reports Running: a stale-gap rewind loop re-runs an
    // already-satisfied leading MoveTo, which reports Done every tick, so both the
    // Running branch below and the per-step timeout are bypassed entirely while
    // stepStartMs is re-stamped on each Done. The high-water step index is the one
    // clock a rewind cannot forge — if it has not moved in this long, the event is
    // going nowhere no matter what the individual steps keep claiming.
    if (!watchdogOwned && timeout > 0 &&
        static_cast<uint32>(nowMs - prog.progressMs) >= timeout * DC_EVENT_NO_PROGRESS_FACTOR)
        result = StepResult::Failed;
    else if (result == StepResult::Running)
    {
        // Escalate a step that has run too long to Failed. uint32 subtraction is
        // wrap-safe for an elapsed interval.
        if (!watchdogOwned && timeout > 0 &&
            static_cast<uint32>(nowMs - prog.stepStartMs) >= timeout)
            result = StepResult::Failed;
        else
            return EventDriveOutcome::Running;
    }

    if (result == StepResult::Done)
    {
        ++prog.stepIndex;
        prog.attempts = 0;
        prog.stepStartMs = nowMs;
        // Only a NEW high-water step index counts as forward progress. Re-running
        // step 0 after a rewind reports Done too, and must not refresh the clock.
        if (prog.stepIndex > prog.maxStepIndex)
        {
            prog.maxStepIndex = prog.stepIndex;
            prog.progressMs = nowMs;
        }
        return (prog.stepIndex >= ev.steps.size()) ? EventDriveOutcome::Completed
                                                   : EventDriveOutcome::Running;
    }

    if (result == StepResult::Blocked)
        return EventDriveOutcome::Stalled;

    // Failed (or timed out): required events stall for the human; optional ones
    // skip the rest of the event and let the clear advance.
    return ev.required ? EventDriveOutcome::Stalled : EventDriveOutcome::Skipped;
}

EventDriveOutcome DungeonEventExecutor::Drive(Player* bot, AiObjectContext* context,
                                              DungeonEvent const& ev, DungeonEventProgress& prog)
{
    // Difficulty-gated event on the wrong difficulty: never drive it. The
    // primary gate is upstream (a gated roster anchor / the Conditional
    // difficulty overload), so reaching here means an authoring slip — skip the
    // event so the clear advances rather than stalling on inert content.
    if (bot && bot->GetMap() && !DcGateMatches(ev.gate, bot->GetMap()->GetDifficulty()))
        return EventDriveOutcome::Skipped;

    uint32 const now = getMSTime();
    uint32 const instanceId = bot ? bot->GetInstanceId() : 0;

    // A different instance means this is a brand-new run of the dungeon (the player
    // re-entered a fresh instance), even if the SAME event id is driven again. The
    // eventId-mismatch and >1s-gap branches below both fail to catch this for a
    // PERSISTENT event (the id is unchanged and the gap reset is skipped for
    // persistent events), so without this a COMPLETED progress from the prior
    // instance would carry over and make Drive report the event done on the first
    // tick — latching its objective and skipping it (ZulFarrak's temple read as
    // already cleared after a re-enter). Tie the reset to the instance id, NOT to
    // dc on/off, so toggling dungeon-clear mid-run preserves real progress. Note:
    // instanceId 0 means "not in an instance" — don't churn the reset on it.
    if (instanceId != 0 && prog.instanceId != 0 && prog.instanceId != instanceId)
        prog.Reset();
    prog.instanceId = instanceId;

    // (Re)initialise on a new event — self-heals a stale value from a prior run.
    if (prog.eventId != ev.id)
    {
        prog.eventId = ev.id;
        prog.stepIndex = 0;
        prog.attempts = 0;
        prog.stepStartMs = now;
        prog.maxStepIndex = 0;
        prog.progressMs = now;
    }
    // A gap since the last drive means this is a FRESH activation — a new run /
    // re-enter, or the event lapsed (condition went false) and re-fired — NOT a
    // tick-to-tick continuation. Restart from step 0 so the whole chain (e.g.
    // lever -> gossip -> wait-for-door) runs again. Without this a stale value
    // left mid-event by a prior run resumes on, say, the wait-for-door step and
    // parks there forever because the lever/gossip that would open it never run.
    // Safe because the steps are idempotent (UseGameObject skips an already-
    // activated GO, MoveTo/Gossip/WaitFor* re-run harmlessly).
    //
    // The gap that counts as "lapsed" is EventStaleGapMs(), not a flat 1s — see
    // there for why 1s silently livelocked every passive multi-step event on a
    // loaded server. Both clocks are re-based on a real lapse; only the rewind
    // itself is skipped for a persistent event.
    else if (getMSTimeDiff(prog.lastDriveMs, now) > EventStaleGapMs())
    {
        // A real lapse: for this whole gap nothing was driving the event, so none
        // of that dead time may be charged to the step timeout or the
        // forward-progress watchdog. True for a PERSISTENT event too — it goes
        // dormant for the length of every fight (the bot is on the combat engine),
        // and charging those minutes would fail it the instant combat ended.
        prog.stepStartMs = now;
        prog.progressMs = now;

        // Only a non-persistent event rewinds. A persistent one keeps its step
        // index across the gap: rewinding a long multi-combat anchored event
        // (ZulFarrak's temple) would re-run the whole chain endlessly and strand
        // WaitForSpawn(wantAlive) steps whose creature has since died.
        if (!ev.persistent)
        {
            prog.stepIndex = 0;
            prog.attempts = 0;
            prog.maxStepIndex = 0;
        }
    }
    prog.lastDriveMs = now;

    if (prog.stepIndex >= ev.steps.size())
        return EventDriveOutcome::Completed;

    EventStep const& active = ev.steps[prog.stepIndex];
    StepResult const result = RunStep(bot, context, active, prog, now);

    uint32 const defaultTimeoutMs =
        bot ? DcSettings::GetUInt(bot, "EventStepTimeout") * 1000u : 30000u;

    if (bot)
    {
        // Throttle: log only on a transition (step or result change), or every
        // kLogHeartbeatMs while a step keeps Running — a long WaitForSpawn used to
        // emit one line per tick (~240ms), burying everything else.
        constexpr uint32 kLogHeartbeatMs = 5000;
        bool const changed = prog.lastLoggedStep != static_cast<int32>(prog.stepIndex) ||
                             prog.lastLoggedResult != static_cast<int32>(result);
        bool const heartbeat = (now - prog.lastLogMs) >= kLogHeartbeatMs;
        if (changed || heartbeat)
        {
            uint32 const timeout = active.timeoutMs ? active.timeoutMs : defaultTimeoutMs;
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] event '{}' step {} kind {} result {} elapsed {}ms timeout {}ms",
                      bot->GetName(), ev.name, prog.stepIndex,
                      static_cast<uint32>(active.kind), static_cast<uint32>(result),
                      static_cast<uint32>(now - prog.stepStartMs), timeout);
            // An instance-data gate is the one gate whose reason for not clearing
            // is invisible from the line above — "result 0" for ten minutes reads
            // the same whether the encounter is running or was silently reset under
            // the party. Print what the step is actually reading.
            if (active.instanceDataId >= 0 && result != StepResult::Done)
            {
                InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
                LOG_DEBUG("playerbots.dungeonclear",
                          "[DC:{}] event '{}' step {} instance gate: data({})={} (need >= {})",
                          bot->GetName(), ev.name, prog.stepIndex, active.instanceDataId,
                          inst ? inst->GetData(static_cast<uint32>(active.instanceDataId)) : 0u,
                          active.instanceDataMin);
            }
            // Same for the persistent-data gate — "result 0" for five minutes on
            // the Mechanar bridge camp reads the same whether the waves are still
            // walking down or the gauntlet never tripped at all.
            if (active.persistentDataId >= 0 && result != StepResult::Done)
            {
                InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
                LOG_DEBUG("playerbots.dungeonclear",
                          "[DC:{}] event '{}' step {} persistent gate: data({})={} (need >= {})",
                          bot->GetName(), ev.name, prog.stepIndex, active.persistentDataId,
                          inst ? inst->GetPersistentData(
                                     static_cast<uint32>(active.persistentDataId))
                               : 0u,
                          active.persistentDataMin);
            }
            prog.lastLoggedStep = static_cast<int32>(prog.stepIndex);
            prog.lastLoggedResult = static_cast<int32>(result);
            prog.lastLogMs = now;
        }
    }

    return Advance(ev, prog, result, now, defaultTimeoutMs);
}

DungeonEvent const* DungeonEventExecutor::FindDueConditionalEvent(Player* bot,
                                                                  AiObjectContext* context,
                                                                  uint32 mapId,
                                                                  bool requireDrivesInCombat)
{
    if (!bot || !context)
        return nullptr;

    Difficulty const difficulty =
        bot->GetMap() ? bot->GetMap()->GetDifficulty() : DUNGEON_DIFFICULTY_NORMAL;
    std::vector<DungeonEvent const*> const conditional =
        DungeonEventRegistry::Conditional(mapId, difficulty);
    if (conditional.empty())
        return nullptr;

    auto const& cleared =
        context->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get();

    for (DungeonEvent const* ev : conditional)
    {
        // The combat-engine copy of the rung only ever drives an opted-in event.
        // Tested FIRST because this runs on every combat tick of every DC leader
        // on every map: on a map with no wave event it rejects here, before any
        // activation predicate is evaluated.
        if (requireDrivesInCombat && !ev->drivesInCombat)
            continue;
        // Already done this run — its synthetic latch key is set.
        if (cleared.count(ConditionalLatchKey(ev->id)))
            continue;
        if (ev->condition && ev->condition(bot, context))
            return ev;
    }
    return nullptr;
}

void DungeonEventExecutor::SweepCompletedConditionalEvents(Player* bot,
                                                           AiObjectContext* context,
                                                           uint32 mapId)
{
    if (!bot || !context)
        return;

    Difficulty const difficulty =
        bot->GetMap() ? bot->GetMap()->GetDifficulty() : DUNGEON_DIFFICULTY_NORMAL;
    std::vector<DungeonEvent const*> const conditional =
        DungeonEventRegistry::Conditional(mapId, difficulty);
    if (conditional.empty())
        return;

    auto& cleared =
        context->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get();
    auto& seenDue =
        context->GetValue<std::unordered_set<uint32>&>(DcKey::SeenDueEvents)->Get();

    for (DungeonEvent const* ev : conditional)
    {
        // A room-aggro pre-clear repeats per boss (its "room trash remains"
        // condition toggles each pull), so a momentary clear is NOT completion —
        // its folded note tracks the gated boss's death instead (DcBossesAction).
        // Genuinely repeatable events likewise never reach a terminal "done".
        if (ev->repeatable || DungeonEventRegistry::IsRoomAggroPreClear(*ev))
            continue;

        uint32 const lk = ConditionalLatchKey(ev->id);
        if (cleared.count(lk))
            continue;

        if (ev->condition && ev->condition(bot, context))
        {
            // Currently due: remember we saw it active so a later transition to
            // not-due reads as completion rather than not-yet-started.
            seenDue.insert(lk);
        }
        else if (seenDue.count(lk))
        {
            // Was due, now isn't, and the executor never latched it: its gating
            // condition WAS the latch (e.g. a Stratholme ziggurat whose instance
            // data flips 1 -> 2 the instant the Ash'ari Crystal topples, mid-
            // combat, before the dormant executor can run its completion tick).
            // Latch it so the folded panel note flips to (done) and — because the
            // boss signature counts cleared.size() — the boss list re-pushes.
            cleared.insert(lk);
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] conditional event '{}' (id {}) completed via condition "
                      "transition -> latched done", bot->GetName(), ev->name, ev->id);
        }
    }
}

bool DungeonEventExecutor::IsPersistentAnchoredEventActive(AiObjectContext* context)
{
    if (!context)
        return false;

    std::optional<DungeonBossInfo> const next =
        context->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next.has_value() || next->kind != DungeonAnchorKind::Objective || !next->eventId)
        return false;

    DungeonEvent const* ev = DungeonEventRegistry::Find(next->mapId, next->eventId);
    if (!ev || !ev->persistent)
        return false;

    DungeonEventProgress const& prog =
        context->GetValue<DungeonEventProgress&>(DcKey::EventProgress)->Get();

    // The progress must belong to the CURRENT instance. Drive() resets a prior
    // run's progress on an instance change — but that reset runs only AFTER this
    // trigger gate has fired, so on the first tick of a new run the leftover
    // progress (stepIndex from the previous instance) is still present here. Were
    // this sticky to fire off that stale stepIndex, the at-objective trigger would
    // activate with the tank nowhere near the anchor, Drive would then advance the
    // event's first step (a KillCreature gate false-completes when its creature is
    // merely out of the 250yd scan range, i.e. the tank is far away), and the run
    // would pin on a half-started event whose later steps can never reach their GO
    // — a permanent "Blocked". Tying the latch to the progress's stamped instance
    // (set by Drive only once the tank has genuinely arrived and driven a step)
    // keeps it false until the event has actually begun in THIS instance.
    Unit* const self = context->GetValue<Unit*>(DcKey::Stock::SelfTarget)->Get();
    uint32 const instanceId = self ? self->GetInstanceId() : 0;
    if (instanceId == 0 || prog.instanceId != instanceId)
        return false;

    // stepIndex >= 1 means the event has advanced past its first step, so this is
    // false until the tank has actually arrived and the event has begun running.
    return prog.eventId == ev->id && prog.stepIndex >= 1 &&
           prog.stepIndex < ev->steps.size();
}

bool DungeonEventExecutor::ActiveEngageStep(AiObjectContext* context, uint32& outEntry,
                                            float& outSearchRadius, bool anyStep)
{
    if (!context)
        return false;

    std::optional<DungeonBossInfo> const next =
        context->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
    if (!next.has_value() || next->kind != DungeonAnchorKind::Objective || !next->eventId)
        return false;

    DungeonEvent const* ev = DungeonEventRegistry::Find(next->mapId, next->eventId);
    if (!ev)
        return false;

    auto reportEngage = [&](EventStep const& step) -> bool
    {
        if (step.kind != EventStepKind::KillCreature || !step.engage || !step.creatureEntry)
            return false;
        outEntry = step.creatureEntry;
        outSearchRadius = step.radius > 0.0f ? step.radius : 250.0f;
        return true;
    };

    DungeonEventProgress const& prog =
        context->GetValue<DungeonEventProgress&>(DcKey::EventProgress)->Get();
    // Same fallback as DcObjectiveArriveAction: a progress that doesn't belong to
    // THIS event resolves to step 0 (always a MoveTo for the engage events), which
    // is not a KillCreature engage step and so reports false — no stale-instance
    // guard needed.
    uint32 const idx = (prog.eventId == ev->id) ? prog.stepIndex : 0;
    if (idx < ev->steps.size() && reportEngage(ev->steps[idx]))
        return true;

    // Active step is a MoveTo/gate (not yet the engage step). For the combat-side
    // stealth-breaker, fall back to the event's FIRST engage step: a stealthed
    // sapper can flag the party into combat DURING the leading MoveTo, before the
    // tank reaches the anchor and the non-combat driver advances stepIndex to the
    // engage step. Without this, the combat rung stands down for exactly that
    // window and the run wedges "in combat, no detectable victim".
    //
    // Steps flagged engageOnlyWhenActive opt OUT of this fallback: their target is
    // undetectable BY DESIGN until the earlier steps have run, which is the same
    // signature the stealth-breaker keys on but the opposite situation — engaging
    // is wrong, not overdue. See EventStep::engageOnlyWhenActive (the Mechanar
    // bridge, where it sent the tank sprinting the length of the bridge at an
    // invisible Pathaleon the moment the party took a scratch anywhere on floor 2).
    if (anyStep)
        for (EventStep const& step : ev->steps)
            if (!step.engageOnlyWhenActive && reportEngage(step))
                return true;

    return false;
}
