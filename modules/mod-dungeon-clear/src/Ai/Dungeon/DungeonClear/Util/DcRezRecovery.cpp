/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRezRecovery.h"

#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcCombatFlag.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStatusPublisher.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"

#include <string>
#include <vector>

#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Timer.h"

namespace
{
    using DcRezDecision::Member;

    bool IsRezClass(Player const* p)
    {
        switch (p->getClass())
        {
            case CLASS_PRIEST:
            case CLASS_PALADIN:
            case CLASS_SHAMAN:
            case CLASS_DRUID:
                return true;
            default:
                return false;
        }
    }

    // The member whose DcRunState owns this run — DEAD OR ALIVE. This used to be a
    // copy of the walk; it now shares one body with the terminal rungs, which need
    // exactly the same dead-tolerance for exactly the same reason. See
    // DcLeaderSignal::FindRunOwner and the header comment.
    Player* ResolveRunOwner(Player* bot) { return DcLeaderSignal::FindRunOwner(bot); }

    // Same-map group snapshot, KEEPING dead members (unlike the Smart Rest
    // snapshot — the corpses are the whole point here). `players` receives the
    // matching Player* per row so verdict indices resolve to live identities.
    void BuildSnapshot(Player* bot, Player* owner, std::vector<Member>& out,
                       std::vector<Player*>& players)
    {
        auto add = [&](Player* member)
        {
            Member m;
            m.isDead = member->isDead();
            m.canRezClass = IsRezClass(member);
            m.isHealerRole = PlayerbotAI::IsHeal(member);
            m.isTankRole = PlayerbotAI::IsTank(member);
            m.isBot = GET_PLAYERBOT_AI(member) != nullptr;
            out.push_back(m);
            players.push_back(member);
        };

        Group* group = bot->GetGroup();
        if (!group)
        {
            add(owner ? owner : bot);
            return;
        }
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member->GetMapId() != bot->GetMapId())
                continue;
            add(member);
        }
    }

    // Instantaneous "is the party fighting" read for the clock freeze. The
    // latched variant (IsPartyEngagedLatched) is file-local to DcLeaderSignal;
    // a bare read is safe HERE because a false flicker merely clears and
    // re-stamps the clock — which RESETS the timeout budget, the forgiving
    // direction — it can never expire recovery early.
    //
    // ENGAGEMENT, not the combat flag: a resurrection cast is refused while
    // flagged ("rez party: cast 'resurrection' -> not possible yet"), so under a
    // phantom flag — a 45yd hostile area aura with nothing aggroed — the recovery
    // can never complete AND the flag froze this very clock, so the timeout that
    // is supposed to bound it never arrived. The run then held over the corpse
    // forever, inside the aura (Arcatraz heroic, tr-20260801-194932-20: hand-
    // rezzed by the player to unstick it). A real fight still freezes the clock.
    bool AnyMemberEngaged(std::vector<Player*> const& players)
    {
        for (Player* p : players)
            if (DcCombatFlag::IsEngaged(p))
                return true;
        return false;
    }

    // Same read, from a bare group walk instead of a built snapshot — for
    // IsElectedRezzer, which answers a stand-down question and has no reason to
    // pay for the snapshot EvaluateImpl builds.
    bool AnySameMapMemberEngaged(Player* bot)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return DcCombatFlag::IsEngaged(bot);
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member->GetMapId() != bot->GetMapId())
                continue;
            if (DcCombatFlag::IsEngaged(member))
                return true;
        }
        return false;
    }

    DcRezRecovery::Plan EvaluateImpl(Player* bot, bool mutate)
    {
        DcRezRecovery::Plan plan;
        if (!bot)
            return plan;

        Player* owner = ResolveRunOwner(bot);
        PlayerbotAI* ownerAI = owner ? GET_PLAYERBOT_AI(owner) : nullptr;
        if (!ownerAI)
            return plan;
        DcRunState& run = DcRun::Of(ownerAI);
        if (!run.enabled || run.paused)
            return plan;  // a pause holds recovery too; clocks stay as-is

        std::vector<Member> members;
        std::vector<Player*> players;
        BuildSnapshot(bot, owner, members, players);

        uint32 const now = getMSTime();

        bool anyDead = false;
        for (Member const& m : members)
            anyDead = anyDead || m.isDead;

        if (!anyDead)
        {
            if (mutate && (run.rezPendingSinceMs || run.rezAnnounceMs))
            {
                // A recovery episode just completed — everyone is back up. The
                // rezzed member's low HP/mana now holds the tank via the
                // default between-pulls readiness floors until they recover.
                if (run.rezAnnounceMs)
                    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                        DcStatusPublisher::SendAddonMessage(
                            botAI, "CHAT\tParty restored \xe2\x80\x94 resuming.");
                run.rezPendingSinceMs = 0;
                run.rezAnnounceMs = 0;
            }
            return plan;  // None / NoDeaths
        }

        // First corpse by group order, for the disable messages.
        for (std::size_t i = 0; i < members.size(); ++i)
            if (members[i].isDead)
            {
                plan.deadName = players[i]->GetName();
                break;
            }

        if (!DcRezRecovery::Enabled(bot))
        {
            // Feature off: hand the party-died trigger its classic verdict.
            plan.verdict.outcome = DcRezDecision::Outcome::Disable;
            plan.verdict.reason = DcRezDecision::Reason::Disabled;
            return plan;
        }

        // THE PULLER DIED — retire the pull it was running.
        //
        // Nothing else can. The whole pull FSM is leader-driven and the leader is
        // the corpse: DungeonClearPullTrigger and the maneuver both stand down on a
        // dead bot, and MaintainScoutCamp (which owns the orphan release) is only
        // reached through the leader election, which excludes the dead. So the
        // phase, the camp and any latched scripted stage simply persist — in the
        // MgT heroic audit the teardown snapshots show phase 3/4 held for 606-708k
        // ms, and tr-20260805-191834-3 ended at phase=4 with camp=True and all five
        // members corpses.
        //
        // That residue is not inert. A latched stage pins the pull target, forces
        // the pull mode on and stands the advance rung down; the camp keeps the
        // followers' hold-at-camp reading a camp nobody is dragging to. Both come
        // back with the tank when it is rezzed, pointed at a fight that is over.
        //
        // Clearing it here rather than in the pull code because this is the one rung
        // that runs on EVERY bot, dead leader included, and already resolves the
        // owner. Idempotent and self-limiting: the guard goes false after the first
        // pass and cannot re-fire until a live pull is re-formed.
        if (mutate && owner->isDead())
        {
            DcPullContext& pull =
                ownerAI->GetAiObjectContext()->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
            if (pull.phase != DcPullPhase::Idle || pull.HasCamp() || pull.scriptedStage >= 0)
            {
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] the puller died mid-pull (phase {}, stage {}, camp {}) "
                         "-> pull FSM reset; the party stops holding a camp nobody is "
                         "dragging to", owner->GetName(), static_cast<uint32>(pull.phase),
                         pull.scriptedStage, pull.HasCamp() ? "set" : "none");
                pull.Reset();
            }
        }

        // The recovery clock: runs only while nobody is FIGHTING (a fight clears
        // it, so a mid-recovery add pull resets the budget instead of burning it).
        bool const partyEngaged = AnyMemberEngaged(players);
        if (mutate)
        {
            if (partyEngaged)
                run.rezPendingSinceMs = 0;
            else if (run.rezPendingSinceMs == 0)
                run.rezPendingSinceMs = now ? now : 1;
        }

        // The NoRezzer floor's other input: is any LIVING survivor still carrying
        // the core combat flag? Deliberately the raw flag rather than engagement —
        // the shape it exists to catch is a boss that has stopped fighting and not
        // yet released the party (Kael'thas' 11s defeat sequence), where every
        // survivor is flagged and nothing is swinging.
        bool anySurvivorCombatFlagged = false;
        for (Player* p : players)
            if (p && p->IsAlive() && p->IsInCombat())
            {
                anySurvivorCombatFlagged = true;
                break;
            }

        DcRezDecision::Inputs in;
        in.enabled = true;
        in.nowMs = now;
        in.pendingSinceMs = run.rezPendingSinceMs;
        in.timeoutMs = DcSettings::GetUInt(bot, "PostCombatRezTimeoutSecs") * 1000;
        in.partyEngaged = partyEngaged;
        in.anySurvivorCombatFlagged = anySurvivorCombatFlagged;
        in.noRezzerSinceMs = run.noRezzerSinceMs;
        in.noRezzerQuietSinceMs = run.noRezzerQuietSinceMs;
        in.noRezzerQuietGraceMs = DC_NO_REZZER_QUIET_GRACE_MS;
        in.noRezzerHoldMaxMs = DC_NO_REZZER_HOLD_MAX_MS;

        plan.verdict = DcRezDecision::Decide(in, members);

        // Advance the floor's clocks from the verdict we just got. They can only be
        // stamped after the fact — whether a rezzer is left is precisely what the
        // kernel decides — so both run a tick behind, which is immaterial against
        // graces measured in seconds. A verdict that is not the no-rezzer branch
        // clears them, so a rezzer coming back up (or a fresh episode) starts clean.
        if (mutate)
        {
            bool const noRezzer = plan.verdict.reason == DcRezDecision::Reason::NoRezzer ||
                                  plan.verdict.reason == DcRezDecision::Reason::NoRezzerInFight;
            if (!noRezzer)
            {
                run.noRezzerSinceMs = 0;
                run.noRezzerQuietSinceMs = 0;
            }
            else
            {
                if (run.noRezzerSinceMs == 0)
                    run.noRezzerSinceMs = now ? now : 1;
                if (partyEngaged || anySurvivorCombatFlagged)
                    run.noRezzerQuietSinceMs = 0;
                else if (run.noRezzerQuietSinceMs == 0)
                    run.noRezzerQuietSinceMs = now ? now : 1;
            }
        }

        if (plan.verdict.rezzerIdx >= 0 &&
            plan.verdict.rezzerIdx < static_cast<int>(players.size()))
        {
            plan.rezzer = players[plan.verdict.rezzerIdx]->GetObjectGuid();
            plan.rezzerName = players[plan.verdict.rezzerIdx]->GetName();
        }
        if (plan.verdict.targetIdx >= 0 &&
            plan.verdict.targetIdx < static_cast<int>(players.size()))
        {
            plan.target = players[plan.verdict.targetIdx]->GetObjectGuid();
            plan.targetName = players[plan.verdict.targetIdx]->GetName();
        }

        // One announcement per recovery episode, from whichever member
        // evaluates first (the stamp on the shared run state dedupes the rest).
        if (mutate && plan.verdict.outcome == DcRezDecision::Outcome::Hold &&
            run.rezAnnounceMs == 0)
        {
            run.rezAnnounceMs = now ? now : 1;
            std::string line;
            if (plan.verdict.reason == DcRezDecision::Reason::WaitingOnHuman)
                line = plan.targetName + " died \xe2\x80\x94 waiting for you to resurrect them (" +
                       std::to_string(in.timeoutMs / 1000) + "s).";
            else if (plan.verdict.reason == DcRezDecision::Reason::NoRezzerInFight)
                // No rezzer elected — there is none left. The hold is only until the
                // fight ends, at which point the verdict becomes the classic disable.
                line = plan.targetName + " died and no one left can resurrect \xe2\x80\x94 "
                       "finishing this fight first.";
            else
                line = plan.targetName + " died \xe2\x80\x94 " + plan.rezzerName +
                       " is coming to resurrect them.";
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                DcStatusPublisher::SendAddonMessage(botAI, "CHAT\t" + line);
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] post-combat rez: holding the run \xe2\x80\x94 {}",
                     owner->GetName(), line);
        }

        return plan;
    }
}

namespace DcRezRecovery
{
    bool Enabled(Player* bot)
    {
        return DcSettings::GetBool(bot, "PostCombatRez");
    }

    Plan Evaluate(Player* bot)
    {
        return EvaluateImpl(bot, /*mutate*/ true);
    }

    bool IsPending(Player* leaderTank)
    {
        if (!leaderTank || !Enabled(leaderTank))
            return false;
        PlayerbotAI* ai = GET_PLAYERBOT_AI(leaderTank);
        if (!ai)
            return false;
        DcRunState const& run = DcRun::Of(ai);
        if (!run.enabled || run.paused)
            return false;

        if (leaderTank->isDead())
            return true;
        Group* group = leaderTank->GetGroup();
        if (!group)
            return false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member->GetMapId() != leaderTank->GetMapId())
                continue;
            if (member->isDead())
                return true;
        }
        return false;
    }

    bool CanRecover(Player* bot)
    {
        if (!bot || !Enabled(bot))
            return false;
        Group* group = bot->GetGroup();
        if (!group)
            return bot->IsAlive() && IsRezClass(bot);
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member->GetMapId() != bot->GetMapId())
                continue;
            if (member->IsAlive() && IsRezClass(member))
                return true;
        }
        return false;
    }

    bool IsElectedRezzer(Player* bot)
    {
        // Cheapest read first — this runs on every follower rung, every tick,
        // and only a living rez class is ever elected. Non-rez classes (and
        // every bot mid-fight) never reach the group walk in EvaluateImpl.
        if (!bot || bot->isDead() || bot->IsInCombat() || !IsRezClass(bot))
            return false;
        Map* map = bot->FindMap();
        if (!map || !map->IsDungeon())
            return false;

        // DELIBERATELY NARROWER THAN THE TRIGGER: the trigger only asks whether THIS
        // bot is flagged, but this predicate hands the bot's movement to the rez rung
        // by taking it away from every other mover. Doing that while the party is
        // still swinging would let an unflagged healer walk out of a live fight to
        // start a corpse run. Recovery loses nothing by waiting — the glue clears the
        // pending clock for exactly as long as anyone is engaged, so the budget this
        // defers is not being spent.
        if (AnySameMapMemberEngaged(bot))
            return false;

        // mutate=false: see the header. The trigger owns the clock; this is the
        // same verdict read without the side effects.
        Plan const plan = EvaluateImpl(bot, /*mutate*/ false);
        if (plan.verdict.outcome != DcRezDecision::Outcome::Hold ||
            plan.verdict.reason != DcRezDecision::Reason::Recovering ||
            plan.rezzer != bot->GetObjectGuid())
            return false;

        // ...AND NOTHING ALIVE IS STILL ON THE PARTY. The engagement test above is
        // `victim || attackers`, a snapshot of who is mid-swing this instant: it
        // goes false in the gap between a pack's swings, and this predicate then
        // hands the rezzer's movement to the corpse walk — out of the camp, across
        // the room, alone, into mobs that are still very much alive.
        //
        // Six of the 45 rez approaches in tp-20260808-162331-1 killed the rezzer
        // within 40s and four of those ended the run. tr-20260808-162337-8 is the
        // clean read: the tank died at 10:41 mid-pack, the healer was released the
        // same second, and was dead at 10:45 to a Sunblade Blood Knight — with the
        // healer the only rez class in the group, that was the run.
        //
        // Placed LAST on purpose. The scan costs a pathfind per combat reference,
        // and everything above already narrows this to the elected rezzer of a
        // pending recovery, which is rare. Radius-bounded so a hostile area aura
        // (45yd, nothing actually on us) cannot stall recovery forever.
        return !DcCombatFlag::AnyPartyHeldByLiveEnemy(bot, DC_FIGHT_HOLDER_RADIUS);
    }

    std::string DescribeWait(Player* bot)
    {
        Plan const plan = EvaluateImpl(bot, /*mutate*/ false);
        if (plan.verdict.outcome != DcRezDecision::Outcome::Hold)
            return "";
        if (plan.verdict.reason == DcRezDecision::Reason::WaitingOnHuman)
            return "Waiting for you to resurrect " + plan.targetName + ".";
        return plan.rezzerName + " is coming to resurrect " + plan.targetName + ".";
    }
}
