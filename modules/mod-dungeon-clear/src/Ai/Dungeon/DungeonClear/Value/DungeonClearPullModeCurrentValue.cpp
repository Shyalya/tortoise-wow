/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DungeonClearPullModeCurrentValue.h"

#include "Creature.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/Data/FightInPlaceRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/ScriptedPullRegistry.h"
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTickMemo.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

bool DungeonClearPullModeCurrentValue::Calculate()
{
    DcPullContext& pull = context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();

    // While a PERSISTENT anchored event drives (ZulFarrak's temple), the event
    // owns the tank: force the EFFECTIVE pull mode Off so the whole dynamic/
    // advanced pull system stands down as one — no camp-drag kiting the tank off
    // the waves, no scout-lag stranding the party up-ramp (scout-lag reads the
    // pull setting directly; see DcLeaderSignal::IsLeaderDynamicScouting, gated the
    // same way). The tank engages directly and tanks in place; the party follows
    // close and the leader-fight assist brings it in. This is the single switch
    // that replaces per-mechanic suppressions — the event needs exactly "pull Off"
    // behaviour. The stored pull-setting preference is untouched (the addon status
    // still shows it, and it resumes the instant the event completes).
    //
    // CLEAR THE STANDING VERDICT, don't just stop reporting it. This return skips
    // DcPullPlanner::UpdateDynamicPullMode below — and that function is the only
    // writer of the Dynamic verdict, so a bare `return false` freezes whatever
    // `decision` happened to be latched on the tick the event started rather than
    // standing it down. `decision == PatrolHold` is the one that bites: the pull
    // trigger keeps its rung live on that code by design (pull mode reads off, but
    // the tank must still hold at commit range while it waits a patrol out), so the
    // pull action re-planted the tank at DcRel::Pull (35) every tick, above
    // DcRel::AtObjective (30), and the event never got another tick to drive its own
    // steps. The patrol-wait timeout cannot break the tie either — ShouldWaitForPatrol
    // is only evaluated inside the governor we just skipped.
    //
    // Live: tr-20260817-100413-43/44/45, all three stalled identically in Shattered
    // Halls. The tank latched "patrol-contended" on the Shattered Hand Champion pack
    // (17671) at the assassin hallway's mouth — contended by the very stealthed
    // Assassins (17695) the sweep event exists to kill — one second before the sweep
    // event's step 0 completed and armed this stand-down. `decision` then read
    // PatrolHold for 913 seconds, the sweep never advanced past step 0, and the run
    // failed the 600s no-progress watchdog with the party standing in the hallway.
    if (DungeonEventExecutor::IsPersistentAnchoredEventActive(context))
    {
        pull.ClearDynamicVerdict();
        return false;
    }

    // SCRIPTED PULL STAGE (ScriptedPullRegistry) — the mirror image of the override
    // above: force the pull system ON for the plan's duration, whatever the player's
    // pull setting says. A plan is not a tactical preference. Selin's guard packs
    // cannot be fought where they stand at all, so "pull Off" there would mean the
    // walk-in engage takes the party into the room and onto the boss — the wipe the
    // whole plan exists to avoid. Same reasoning (and the same "the registry row IS
    // the decision" rule) as a BossPullbackRegistry drag.
    //
    // Raising the LATCHED bool rather than only reporting true here is deliberate:
    // the follower camp-hold (DcLeaderSignal::GetLeaderPullInfo / GetLeaderCampHold)
    // and the combat drag-back trigger all read `dungeon clear pull mode` directly,
    // so a plan that only moved the effective value would drag a pack home to a camp
    // nobody was holding. `scriptedForced` remembers that WE raised it, so the
    // handback below can never clobber a setting the player chose.
    //
    // Leader-only, like the Dynamic governor: a follower's own copy of the bool
    // drives nothing, and writing it there would just add churn.
    bool const isLeader = DcLeaderSignal::IsDungeonClearLeader(bot);
    if (isLeader && DcTickMemoAccess::ScriptedStage(bot, context) != nullptr)
    {
        if (!pull.scriptedForced)
        {
            pull.scriptedForced = true;
            context->GetValue<bool>(DcKey::PullMode)->Set(true);
            // The drag-back runs the tank home back-turned; the pull session's daze
            // immunity is what keeps a hit from behind from turning that into a
            // crawl. Armed with the bool everywhere else, so arm it here too.
            DcLeaderSignal::SetLeaderDazeImmunity(bot, true);
        }
        return true;
    }
    if (pull.scriptedForced)
    {
        pull.scriptedForced = false;
        // Hand the bool back to the player's preference. Off (0) / On (1) keep it in
        // lock-step with the setting exactly as ApplyPullSetting does; Dynamic (2) is
        // the governor's to own, so leave both the bool and the immunity to the
        // UpdateDynamicPullMode call immediately below.
        uint32 const setting = context->GetValue<uint32>(DcKey::PullSetting)->Get();
        if (setting != 2u)
        {
            bool const active = (setting == 1u);
            context->GetValue<bool>(DcKey::PullMode)->Set(active);
            DcLeaderSignal::SetLeaderDazeImmunity(bot, active);
        }
    }

    // FIGHT-IN-PLACE ROOM (FightInPlaceRegistry) — force the mode Off, same single
    // switch as the anchored-event override above and for the same reason: the room
    // needs exactly "pull Off" behaviour, and half-applying it is what has been
    // costing us.
    //
    // The registry's rule already lived in DungeonClearPullTrigger's Idle branch —
    // "this target is in a fight-in-place room, defer to the walk-in engage". But
    // that trigger is only half the pull system. The other half is the COMBAT-side
    // maneuver, whose Idle branch retreats to a fresh camp on ANY unplanned aggro
    // and never consulted the registry at all, so the two halves disagreed by one
    // second (tr-20260803-205519-1):
    //
    //   21:01:08  pull trigger: target 24689 is in a fight-in-place room -> defer
    //             to walk-in engage
    //   21:01:09  advanced-pull: unplanned aggro while scouting -> fresh camp
    //             (189.6,3.8,-2.8) drag 27.9yd, party converges
    //
    // The walk-in engage did its job and took the tank in after Selin's centre pair;
    // aggro landed; the maneuver hauled it 28yd straight back out to X=189 — thirty
    // yards below the CanAIAttack plane this registry exists to keep the fight above
    // — and then the route re-formed and walked back in. That in-out-in shuffle is
    // what the player sees, and no per-target veto can fix it, because by the time
    // the maneuver runs the question is no longer "may I pull this target" but "may
    // I retreat at all".
    //
    // Answered at the mode instead, so the pull trigger, the drag-back maneuver, the
    // follower camp-hold and the scout-lag stand down together. Two ways in, because
    // the answer has to survive the walk-in: the TARGET test covers the approach from
    // out in the corridor (it is the same read the trigger vetoes on, one rung
    // earlier), and the BOT test covers the tank once it is inside, where the pull
    // target read goes quiet and nothing may be dragged out regardless of what it is.
    //
    // Below the scripted-stage clause deliberately: a stage IS the authored exception
    // to this room's rule, and it has already returned true above.
    if (FightInPlaceRegistry::IsNoPullZone(bot->GetMapId(), bot->GetPositionX(),
                                           bot->GetPositionY()))
        return false;
    if (Unit* trash = DcTargeting::GetPullTarget(botAI))
    {
        // Judged from a creature's HOME position, never from where it is standing
        // this instant. A room mob that has already run out to meet the tank is
        // still a room mob — and it is precisely then, mid-aggro with the tank a few
        // yards short of the doorway, that both live-position reads go false at once
        // and the drag-back would slip through.
        Creature const* c = trash->ToCreature();
        Position const at = c ? c->GetHomePosition() : trash->GetPosition();
        if (FightInPlaceRegistry::IsNoPullZone(trash->GetMapId(), at.GetPositionX(),
                                               at.GetPositionY()))
            return false;
    }

    // Refresh the Dynamic (pull setting == 2) verdict for THIS tick, then report
    // the behavioural bool. UpdateDynamicPullMode is a no-op for Off/On (where
    // DcPullAction owns the bool) and internally throttles the expensive
    // classification, so running it on every read is cheap and idempotent.
    DcPullPlanner::UpdateDynamicPullMode(botAI, context);
    return AI_VALUE(bool, DcKey::PullMode);
}
