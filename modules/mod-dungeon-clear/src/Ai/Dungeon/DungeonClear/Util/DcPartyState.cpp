/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcPartyState.h"

#include "DcTickMemo.h"
#include "DungeonClearMath.h"
#include "DungeonClearTuning.h"
#include "DungeonClearUtil.h"   // DC_PULL_* log macros
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
#include "Config.h"
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
#include "Ai/Dungeon/DungeonClear/Data/ScriptedPullRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/SealedEncounterRegistry.h"
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "Ai/Dungeon/DungeonClear/Util/DcCombatFlag.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRezRecovery.h"
#include "Ai/Dungeon/DungeonClear/Util/DcSmartRest.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Value/DungeonClearLiveBossValue.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

float DcPartyState::RestMinHpPct(Player* bot)
{
    // Per-run override wins: the group set a health rest target for this run, and
    // DungeonClearNeedsEatTrigger makes bots actually eat up to it, so we use it
    // verbatim (no playerbots clamp — even a target above AlmostFullHealth is
    // reachable now). 0 means "inherit" and falls through.
    if (uint32 const target = DcSettings::GetUInt(bot, "RestHealthPct"))
        return static_cast<float>(target);

    // 90% is our "topped up enough to pull" ceiling. Clamp it to the level bots
    // actually eat back up to (AiPlayerbot.AlmostFullHealth, default 85) so the
    // gate never waits on HP a bot won't restore on its own.
    return std::min(90.0f, static_cast<float>(sPlayerbotAIConfig.almostFullHealth));
}
float DcPartyState::RestMinMpPct(Player* bot)
{
    // Per-run override wins; see RestMinHpPct. DungeonClearNeedsDrinkTrigger makes
    // bots drink up to this target, so it stays reachable above HighMana too.
    if (uint32 const target = DcSettings::GetUInt(bot, "RestManaPct"))
        return static_cast<float>(target);

    // 75% ceiling, clamped to the level bots actually drink back up to
    // (AiPlayerbot.HighMana, default 65). Bots stop drinking at HighMana, so a
    // higher gate would strand the tank waiting on slow natural mana regen.
    return std::min(75.0f, static_cast<float>(sPlayerbotAIConfig.highMana));
}
bool DcPartyState::IsPartyReady(Player* bot, float minHpPct, float minMpPct, float maxSpread,
                                Position const* spreadAnchor, float maxTankGap)
{
    if (!bot)
        return false;
    Group* group = bot->GetGroup();
    if (!group)
        return true;  // Solo tank — always "ready."

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member)
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        if (member->isDead())
            continue;  // Dead members hold the run via DcRezRecovery::IsPending
                       // (or, with PostCombatRez off / recovery unviable, the
                       // party-died bailout) — never via these readiness floors.
                       // The moment one is rezzed they re-enter this walk as a
                       // living-but-low member and the floors hold the tank
                       // while they eat/drink back up.

        if (member != bot)
        {
            float const spread = spreadAnchor ? member->GetDistance(*spreadAnchor)
                                              : bot->GetDistance(member);
            if (spread > maxSpread)
                return false;
            // Absolute tank<->member backstop (camp-anchored mode only): a stale
            // camp sitting where the party stands passes the anchored spread
            // forever while the tank glides away — cap the real gap too.
            if (maxTankGap > 0.0f && bot->GetDistance(member) > maxTankGap)
                return false;
        }
        if (member->GetHealthPct() < minHpPct)
            return false;
        if (member->getPowerType() == POWER_MANA)
        {
            uint32 const maxMp = member->GetMaxPower(POWER_MANA);
            if (maxMp > 0)
            {
                float const mpPct = 100.0f * float(member->GetPower(POWER_MANA)) / float(maxMp);
                if (mpPct < minMpPct)
                    return false;
            }
        }
    }
    return true;
}
DcPartyState::SpreadGate DcPartyState::GetSpreadGate(Player* bot, AiObjectContext* context)
{
    // Through DcSettings (NOT raw sConfigMgr) so a per-run addon override of
    // PartyMaxSpread actually takes effect — the registry marks it
    // player-facing, and reading conf directly here silently ignored it.
    SpreadGate gate{DcSettings::GetFloat(bot, "PartyMaxSpread"), nullptr};
    if (!context)
        return gate;

    DcPullContext const& pull =
        context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    // Waive the spread requirement ONLY while a pull maneuver is actually
    // holding the party at camp — see the header comment for the full why.
    if (DcLeaderSignal::IsPullPhaseHolding(static_cast<uint32>(pull.phase)))
    {
        gate.maxSpread = 100000.0f;
        return gate;
    }
    // SEALED ENCOUNTER, final approach: a tighter, TANK-anchored clump that
    // overrides both the setting and the camp anchor below.
    //
    // The boss's room locks the instant the encounter starts (an InstanceScript
    // DOOR_TYPE_ROOM door — see SealedEncounterRegistry), so the party has to cross
    // the threshold WITH the tank, not 25yd behind it. And the camp anchor is
    // actively wrong here: with the tank at the doorway it is still only ~45yd from
    // Selin's camp, inside the 60yd tank-gap backstop, so a party legitimately "set"
    // at a camp 70yd back passes every generic tolerance while being nowhere near
    // able to follow the tank in.
    //
    // Scoped to approachRadius of the boss so nothing else about the run changes, and
    // sized to what follow-tank actually delivers (it trails at min(followDistance,
    // 6yd)), so in the healthy case this costs nothing and only a real straggler
    // holds the tank. The hard "nobody outside the room" guarantee is the muster in
    // DungeonClearAtBossTrigger; this is what makes it satisfiable quickly instead of
    // being a long wait parked inside the boss's aggro radius.
    //
    // NOT while a SCRIPTED STAGE is in flight, and this is load-bearing rather than
    // tidy: a stage ORDERS the followers to hold at its camp, and Selin's camp is
    // 71.6yd from him — so a tank-anchored 10yd gate asks the party to be somewhere it
    // has been forbidden to go, and can never pass. Live (tr-20260803-133734-1): a
    // stage that failed to retire left the party camp-held while the tank sat inside
    // the approach radius, and the log filled with "advance yielding: party not ready
    // — waiting on Emandy, Toogo, Jecini (out of range)" for two and a half minutes.
    // Exactly the circular gate the camp anchor below exists to avoid, reintroduced
    // one branch earlier. The stage's own camp hold owns the party until it retires;
    // only then does the approach clump have any business tightening anything.
    if (std::optional<DungeonBossInfo> const next =
            pull.scriptedStage >= 0
                ? std::nullopt
                : context->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get())
    {
        if (next->kind == DungeonAnchorKind::Boss)
        {
            if (SealedEncounterRow const* const sealed =
                    SealedEncounterRegistry::Find(bot->GetMapId(), next->entry))
            {
                if (SealedEncounterRegistry::InApproachRange(
                        *sealed, bot->GetPositionX(), bot->GetPositionY(),
                        bot->GetPositionZ(), next->x, next->y, next->z))
                {
                    gate.maxSpread = sealed->musterSpread;
                    gate.anchor = nullptr;   // the TANK, never the camp
                    return gate;
                }
            }
        }
    }

    // Pull mode between maneuvers (Idle): hold-at-camp still pins the party at
    // the camp, so "caught up" must be measured against the camp, not the tank —
    // otherwise a camp standoff at/over PartyMaxSpread deadlocks the run (see
    // the header comment on SpreadGate). (0,0,0) camp = unset, fall back.
    if (context->GetValue<bool>(DcKey::PullMode)->Get() && pull.HasCamp())
    {
        gate.anchor = &pull.camp;
        // Camp-anchored backstop: members set at a live camp are by construction
        // within PartyMaxSpread + the camp's own standoff behind the tank
        // (PullSetback normally, drag-extended up to PullMaxDrag for clearance /
        // LOS-break camps), so this cap never trips in healthy states. It only
        // bites when the camp has gone stale at the party's feet — the case
        // where the anchored spread alone passes forever while the tank glides
        // away (the scout-runaway gap).
        float const setback = DcSettings::GetFloat(bot, "PullSetback");
        float const maxDrag = DcSettings::GetFloat(bot, "PullMaxDrag");
        gate.maxTankGap = gate.maxSpread + std::max(setback, maxDrag);

        // STALE-CAMP FALLBACK - the missing half of the runaway backstop
        // above. The camp is only ever re-anchored by the stranded-recovery
        // pass, and that action loses the relevance race against
        // resting/looting on every tick - so on a plain advance the camp
        // stays parked where the last maneuver ended while the party walks
        // on. Members are then measured against ground the party LEFT:
        // live, bots standing next to the tank read as "out of range" and
        // the run crawled in a loot/wait cycle at the Deadmines entrance
        // for its whole 600s window. If the LEADER himself is already
        // beyond the tank-gap from the camp, that camp is history - measure
        // against the tank until the pull machinery plants a fresh one.
        float const leaderCampGap = bot->GetDistance(pull.camp.GetPositionX(),
                                                     pull.camp.GetPositionY(),
                                                     pull.camp.GetPositionZ());
        if (leaderCampGap > gate.maxTankGap)
        {
            gate.anchor = nullptr;
            gate.maxTankGap = 0.0f;
        }
    }
    return gate;
}
float DcPartyState::LeaderEffectiveMaxSpread(Player* bot)
{
    if (!bot)
        return 0.0f;
    float const own = DcSettings::GetFloat(bot, "PartyMaxSpread");
    Player* leader = DcLeaderSignal::FindLeaderTank(bot);
    if (!leader)
        return own;
    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (!leaderAI)
        return own;   // a real-player leader runs no DC gate to be inside of
    return GetSpreadGate(leader, leaderAI->GetAiObjectContext()).maxSpread;
}

DcPartyState::RestGate DcPartyState::GetRestGate(Player* bot, AiObjectContext* context)
{
    RestGate gate;  // 0/0 — spread-only readiness
    if (!bot)
        return gate;
    // Smart Rest's latch owns recovery; the floors stay 0 there (unchanged
    // behaviour, just centralised so the panel and the gate read one body).
    if (DcSmartRest::Enabled(bot))
        return gate;
    // Flagged with no fight behind it: nobody can eat or drink, so waiting on
    // HP/mana is a deadlock. See the header.
    if (DcCombatFlag::IsPhantomFlag(bot, context))
        return gate;
    gate.minHp = RestMinHpPct(bot);
    gate.minMp = RestMinMpPct(bot);
    return gate;
}
bool DcPartyState::IsBetweenPullsReady(Player* bot, AiObjectContext* context, bool requireNoLoot)
{
    if (!bot || !context)
        return false;
    // Post-combat rez recovery: a dead same-map member holds the tank outright.
    // Placed AHEAD of the Smart Rest branch split so it binds in BOTH branches
    // (Smart Rest is off by default) — with the party-died bailout relaxed,
    // this is what keeps advance/at-boss/pull parked over a corpse while the
    // elected rezzer works. See DcRezRecovery.
    if (DcRezRecovery::IsPending(bot))
        return false;
    if (DcSmartRest::Enabled(bot))
    {
        // Update the latch BEFORE the loot early-out, so it stays live (and
        // the followers keep drinking toward full) even while the party loots.
        // This gate is the latch's one update site — both memo slots (strict
        // trigger-side, loose action-side) land here, and UpdateLatch is
        // idempotent within a tick, so double evaluation is safe. Do NOT move
        // the update into DcTickMemo: the loot-yield path must refresh it too.
        bool const latched = DcSmartRest::UpdateLatch(bot, context);
        if (requireNoLoot && context->GetValue<bool>(DcKey::Stock::HasAvailableLoot)->Get())
            return false;
        SpreadGate const gate = GetSpreadGate(bot, context);
        // Thresholds 0 = spread-only readiness; recovery is the latch's job.
        return !latched &&
               IsPartyReady(bot, 0.0f, 0.0f, gate.maxSpread, gate.anchor, gate.maxTankGap);
    }
    if (requireNoLoot && context->GetValue<bool>(DcKey::Stock::HasAvailableLoot)->Get())
        return false;
    SpreadGate const gate = GetSpreadGate(bot, context);
    RestGate const rest = GetRestGate(bot, context);
    return IsPartyReady(bot, rest.minHp, rest.minMp, gate.maxSpread, gate.anchor,
                        gate.maxTankGap);
}
bool DcPartyState::IsScriptedStageMustering(Player* bot, AiObjectContext* context)
{
    if (!bot || !context)
        return false;

    DcPullContext& pull = context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();

    // Only ever gates the START of a stage. A maneuver already in flight has the
    // pack tagged and is running it home; holding there would strand the tank
    // mid-drag, which is the failure this whole pipeline is built to avoid.
    bool const stagePending = pull.scriptedStage < 0 && pull.phase == DcPullPhase::Idle &&
                              DcTickMemoAccess::ScriptedStage(bot, context) != nullptr;

    // Phantom flag: nobody can eat or drink while flagged, so a floor waited on
    // there can never be met. Same waiver, same reason, as GetRestGate's. (An
    // ARMED muster still holds its substance floor first — see minMs below —
    // which gives the phantom-recovery hatch a few seconds to clear the flag.)
    //
    // HP AND MANA ONLY. The spread limit is waived (0 would mean "everyone stands
    // exactly on the tank" — IsPartyReady compares `spread > maxSpread` — not "no
    // limit"), because spread is the business of IsBetweenPullsReady one rung
    // earlier and has already passed by the time we are asked.
    //
    // A corpse on this map is NOT "topped up". IsPartyReady skips the dead by
    // design (rez recovery holds the run), but when recovery is not pending — no
    // viable rezzer, or the death is one tick old — the gate passed over the
    // corpse and stages armed short-handed within 10s of a death
    // (tr-20260806-172345-34: healer dies, "muster over after 1s", tank dead 8s
    // into the stage). The muster budget still bounds the wait.
    bool const toppedUp =
        !stagePending || DcCombatFlag::IsPhantomFlag(bot, context) ||
        (!HasDeadSameMapMember(bot) &&
         IsPartyReady(bot, DC_SCRIPTED_PULL_MUSTER_HP, DC_SCRIPTED_PULL_MUSTER_MP,
                      /*maxSpread*/ 100000.0f, /*spreadAnchor*/ nullptr,
                      /*maxTankGap*/ 0.0f));

    uint32 const now = getMSTime();
    uint32 const prev = pull.scriptedMusterSince;
    uint32 musterSince = prev;
    bool const hold = DungeonClearMath::ShouldMusterForScriptedStage(
        stagePending, toppedUp, musterSince, now, DC_SCRIPTED_PULL_MUSTER_MS,
        DC_SCRIPTED_PULL_MUSTER_MIN_MS, musterSince);
    pull.scriptedMusterSince = musterSince;

    // Log the two EDGES only. The latch deliberately stays armed past the timeout
    // (so one stage cannot muster twice), which means "latched and not holding" is
    // the steady state after the budget is spent, not an event — deriving the old
    // hold from the old stamp is what keeps this to two lines per muster instead of
    // one per tick.
    bool const wasHolding = prev != 0 && now >= prev && (now - prev) < DC_SCRIPTED_PULL_MUSTER_MS;
    if (!wasHolding && hold)
        DC_PULL_INFO("[DC:{}] scripted-pull: mustering before the next stage — the party "
                     "is below {:.0f}% HP / {:.0f}% mana (up to {}s)", bot->GetName(),
                     DC_SCRIPTED_PULL_MUSTER_HP, DC_SCRIPTED_PULL_MUSTER_MP,
                     DC_SCRIPTED_PULL_MUSTER_MS / 1000);
    else if (wasHolding && !hold)
        DC_PULL_INFO("[DC:{}] scripted-pull: muster over after {}s ({})",
                     bot->GetName(), (now - prev) / 1000,
                     toppedUp ? "party topped up — arming the stage"
                              : "budget spent — arming on the ordinary floors");
    return hold;
}

bool DcPartyState::IsScriptedMusterHolding(Player* bot, AiObjectContext* context)
{
    if (!bot || !context)
        return false;
    DcPullContext const& pull = context->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
    if (pull.scriptedMusterSince == 0)
        return false;  // no muster armed (or released topped-up)
    uint32 const now = getMSTime();
    // A stale stamp past the budget is the "armed forever so it never re-fires"
    // steady state, not a hold.
    return now >= pull.scriptedMusterSince &&
           now - pull.scriptedMusterSince < DC_SCRIPTED_PULL_MUSTER_MS;
}

bool DcPartyState::HasDeadSameMapMember(Player* bot)
{
    if (!bot)
        return false;
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member->GetMapId() == bot->GetMapId() && member->isDead())
            return true;
    }
    return false;
}

bool DcPartyState::IsAnyPartyMemberLooting(Player* bot, std::string* whoOut)
{
    if (!bot)
        return false;
    Group* group = bot->GetGroup();
    if (!group)
        return false;  // Solo tank — no followers to wait on.

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot)
            continue;
        if (!member->IsAlive())
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;

        // Only bot members loot under our coordination; a real player has no
        // PlayerbotAI, so we can't read their loot intent and don't wait on it.
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI)
            continue;

        AiObjectContext* memberCtx = memberAI->GetAiObjectContext();
        if (memberCtx->GetValue<bool>(DcKey::Stock::CanLoot)->Get() ||
            memberCtx->GetValue<bool>(DcKey::Stock::HasAvailableLoot)->Get())
        {
            if (whoOut)
                *whoOut = member->GetName();
            return true;
        }
    }
    return false;
}
std::string DcPartyState::DescribePartyNotReady(Player* bot,
                                                    float minHpPct, float minMpPct,
                                                    float maxSpread,
                                                    Position const* spreadAnchor,
                                                    float maxTankGap)
{
    if (!bot)
        return "";
    Group* group = bot->GetGroup();
    if (!group)
        return "";  // Solo tank — nobody to wait on.

    // Keep the addon line short: name a few members, then collapse the rest.
    constexpr size_t MAX_NAMED = 3;
    std::vector<std::string> parts;
    size_t extra = 0;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member)
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        if (member->isDead())
            continue;  // Dead members are the rez recovery's to report, not a
                       // readiness reason (mirrors IsPartyReady's skip).

        // Mirror IsPartyReady's checks, but record the limiting reason. Order
        // matters only for which single reason we surface first; distance reads
        // most intuitively, then health, then mana.
        std::string reason;
        if (member != bot &&
            ((spreadAnchor ? member->GetDistance(*spreadAnchor)
                           : bot->GetDistance(member)) > maxSpread ||
             (maxTankGap > 0.0f && bot->GetDistance(member) > maxTankGap)))
            reason = "out of range";
        else if (member->GetHealthPct() < minHpPct)
            reason = "low HP";
        else if (member->getPowerType() == POWER_MANA)
        {
            uint32 const maxMp = member->GetMaxPower(POWER_MANA);
            if (maxMp > 0)
            {
                float const mpPct = 100.0f * float(member->GetPower(POWER_MANA)) / float(maxMp);
                if (mpPct < minMpPct)
                    reason = "low mana";
            }
        }

        if (reason.empty())
            continue;  // This member is ready — not blocking.

        if (parts.size() < MAX_NAMED)
            parts.push_back(std::string(member->GetName()) + " (" + reason + ")");
        else
            ++extra;
    }

    if (parts.empty())
        return "";

    std::string out = "Waiting on ";
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i)
            out += ", ";
        out += parts[i];
    }
    if (extra)
        out += " +" + std::to_string(extra) + " more";
    return out;
}
std::string DcPartyState::DescribePartyLooting(Player* bot)
{
    if (!bot)
        return "";
    Group* group = bot->GetGroup();
    if (!group)
        return "";

    constexpr size_t MAX_NAMED = 1;
    std::vector<std::string> names;
    size_t extra = 0;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot)
            continue;
        if (!member->IsAlive())
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI)
            continue;  // Real player — we don't drive or wait on their loot.

        AiObjectContext* memberCtx = memberAI->GetAiObjectContext();
        if (!memberCtx->GetValue<bool>(DcKey::Stock::CanLoot)->Get() &&
            !memberCtx->GetValue<bool>(DcKey::Stock::HasAvailableLoot)->Get())
            continue;

        if (names.size() < MAX_NAMED)
            names.push_back(member->GetName());
        else
            ++extra;
    }

    if (names.empty())
        return "";

    std::string out;
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i)
            out += ", ";
        out += names[i];
    }
    if (extra)
        out += " +" + std::to_string(extra) + " more";
    out += " looting";
    return out;
}
