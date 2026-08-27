#include "Util/DungeonClearUtil.h"
#include "Util/DcAddonComm.h"
#include "playerbot/playerbot.h"
#include "playerbot/strategy/values/NearestGameObjects.h"
#include "Group/Group.h"
#include "Maps/GridSearchers.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"
#include "Objects/GameObject.h"
#include "Chat/Chat.h"
#include "DcValueKeys.h"
#include "DcRunState.h"
#include "Settings/DcSettings.h"
#include "Value/DungeonBossesValue.h"
#include "Value/NextDungeonBossValue.h"
#include <optional>
#include <unordered_set>
#include <vector>

namespace
{
    bool IsUsableHostile(Unit* unit)
    {
        if (!unit || !unit->IsAlive() || unit->GetTypeId() != TYPEID_UNIT)
            return false;
        Creature* creature = static_cast<Creature*>(unit);
        return !creature->IsCivilian() && !creature->IsCritter();
    }
}

namespace DcUtil
{
    namespace
    {
        DcRunState* OverrideState(Player* anyMember)
        {
            if (!anyMember)
                return nullptr;
            Player* tank = FindEnabledTank(anyMember);
            if (!tank)
                tank = anyMember;
            PlayerbotAI* tankAi = tank->GetPlayerbotAI();
            if (!tankAi || !tankAi->GetAiObjectContext())
                return nullptr;
            return &tankAi->GetAiObjectContext()->GetValue<DcRunState&>(DcKey::RunState)->Get();
        }
    }

    bool IsRealCommander(Player* owner, Player* bot)
    {
        if (!owner || !bot)
            return false;
        if (PlayerbotAI* oai = owner->GetPlayerbotAI())
        {
            if (oai->GetMaster() != owner)
            {
                if (!owner->GetSession() || owner->GetSession()->GetSecurity() < SEC_MODERATOR)
                    return false;
            }
        }
        if (Group* g = bot->GetGroup())
        {
            if (g->IsMember(owner->GetObjectGuid()))
                return true;
        }
        return owner->GetSession() && owner->GetSession()->GetSecurity() >= SEC_MODERATOR;
    }

    Player* FindEnabledTank(Player* anyMember)
    {
        if (!anyMember)
            return nullptr;
        Group* g = anyMember->GetGroup();
        if (!g)
        {
            if (PlayerbotAI* pai = anyMember->GetPlayerbotAI())
            {
                if (!pai->GetAiObjectContext())
                    return nullptr;
                DcRunState& st = pai->GetAiObjectContext()->GetValue<DcRunState&>(DcKey::RunState)->Get();
                if (st.enabled && pai->IsTank(anyMember))
                    return anyMember;
            }
            return nullptr;
        }
        for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->getSource();
            if (!m || !m->IsInWorld())
                continue;
            PlayerbotAI* mai = m->GetPlayerbotAI();
            if (!mai || !mai->IsTank(m))
                continue;
            AiObjectContext* ctx = mai->GetAiObjectContext();
            if (!ctx)
                continue;
            Value<DcRunState&>* v = ctx->GetValue<DcRunState&>(DcKey::RunState);
            if (v && v->Get().enabled)
                return m;
        }
        return nullptr;
    }

    Player* FindGroupTankBot(Player* anyMember)
    {
        if (!anyMember)
            return nullptr;
        if (Player* t = FindEnabledTank(anyMember))
            return t;
        Group* g = anyMember->GetGroup();
        if (!g)
            return anyMember->GetPlayerbotAI() && anyMember->GetPlayerbotAI()->IsTank(anyMember) ? anyMember : nullptr;
        for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->getSource();
            if (m && m->GetPlayerbotAI() && m->GetPlayerbotAI()->IsTank(m))
                return m;
        }
        return nullptr;
    }

    DcRunState* LeaderRunState(Player* bot)
    {
        Player* tank = FindEnabledTank(bot);
        if (!tank)
            tank = bot;
        PlayerbotAI* ai = tank->GetPlayerbotAI();
        if (!ai || !ai->GetAiObjectContext())
            return nullptr;
        return &ai->GetAiObjectContext()->GetValue<DcRunState&>(DcKey::RunState)->Get();
    }

    bool IsDungeonClearLeader(PlayerbotAI* ai, Player* bot)
    {
        if (!ai || !bot || !ai->GetAiObjectContext())
            return false;
        DcRunState& st = ai->GetAiObjectContext()->GetValue<DcRunState&>(DcKey::RunState)->Get();
        if (!st.enabled)
            return false;
        return ai->IsTank(bot);
    }

    bool IsEnabledRun(Player* bot)
    {
        DcRunState* st = LeaderRunState(bot);
        return st && st->enabled;
    }

    bool IsPausedRun(Player* bot)
    {
        DcRunState* st = LeaderRunState(bot);
        return st && st->enabled && st->paused;
    }

    bool EffectivePreventBotRelease(Player* anyMember)
    {
        DcRunState* state = OverrideState(anyMember);
        return state && state->enabled && state->hasPreventBotReleaseOverride
            ? state->preventBotReleaseOverride : sDcSettings.preventBotRelease;
    }

    bool DungeonClearShouldPreventAutoRelease(Player* bot)
    {
        return IsEnabledRun(bot) && EffectivePreventBotRelease(bot);
    }

    uint32 EffectiveLootQualityMin(Player* anyMember)
    {
        DcRunState* state = OverrideState(anyMember);
        return state && state->enabled && state->hasLootQualityOverride
            ? state->lootQualityOverride : sDcSettings.lootQualityMin;
    }

    bool EffectiveIgnoreChests(Player* anyMember)
    {
        DcRunState* state = OverrideState(anyMember);
        return state && state->enabled && state->hasIgnoreChestsOverride
            ? state->ignoreChestsOverride : true;
    }

    float EffectivePartyMaxSpread(Player* anyMember)
    {
        DcRunState* state = OverrideState(anyMember);
        return state && state->enabled && state->hasPartyMaxSpreadOverride
            ? state->partyMaxSpreadOverride : sDcSettings.partyMaxSpread;
    }

    bool PartyNeedsRegroup(Player* anyMember)
    {
        if (!anyMember || !anyMember->GetGroup())
            return false;

        float const maxSpread = EffectivePartyMaxSpread(anyMember);
        uint32 const mapId = anyMember->GetMapId();
        for (GroupReference* ref = anyMember->GetGroup()->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (!member || member == anyMember || !member->IsInWorld()
                || member->GetMapId() != mapId || !member->IsAlive())
                continue;
            if (anyMember->GetDistance(member) > maxSpread)
                return true;
        }
        return false;
    }

    float EffectiveRestHealth(Player* anyMember)
    {
        DcRunState* state = OverrideState(anyMember);
        return state && state->enabled && state->hasRestHealthOverride
            ? state->restHealthOverride : sDcSettings.restHealth;
    }

    float EffectiveRestMana(Player* anyMember)
    {
        DcRunState* state = OverrideState(anyMember);
        return state && state->enabled && state->hasRestManaOverride
            ? state->restManaOverride : sDcSettings.restMana;
    }

    uint32 EffectivePullMax(Player* anyMember)
    {
        DcRunState* state = OverrideState(anyMember);
        return state && state->enabled && state->hasPullMaxOverride
            ? state->pullMaxOverride : sDcSettings.pullDynamicMaxLeeroyMobs;
    }

    bool PartyNeedsRest(Player* anyMember)
    {
        if (!anyMember)
            return false;

        Group* group = anyMember->GetGroup();
        if (!group)
        {
            if (!anyMember->IsAlive())
                return false;
            if (anyMember->GetHealthPercent() < EffectiveRestHealth(anyMember))
                return true;
            uint32 maxMana = anyMember->GetMaxPower(POWER_MANA);
            return anyMember->GetPowerType() == POWER_MANA && maxMana
                && (100.0f * anyMember->GetPower(POWER_MANA) / maxMana) < EffectiveRestMana(anyMember);
        }

        uint32 const mapId = anyMember->GetMapId();
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (!member || !member->IsInWorld() || member->GetMapId() != mapId || !member->IsAlive())
                continue;
            if (member->GetHealthPercent() < EffectiveRestHealth(anyMember))
                return true;
            uint32 maxMana = member->GetMaxPower(POWER_MANA);
            if (member->GetPowerType() == POWER_MANA && maxMana
                && (100.0f * member->GetPower(POWER_MANA) / maxMana) < EffectiveRestMana(anyMember))
                return true;
        }
        return false;
    }

    bool PartyReadyToResume(Player* anyMember)
    {
        if (!anyMember || !anyMember->IsInWorld() || anyMember->IsBeingTeleported())
            return false;

        Group* group = anyMember->GetGroup();
        if (!group)
            return anyMember->IsInWorld() && anyMember->IsAlive() && !PartyNeedsRest(anyMember);

        uint32 const mapId = anyMember->GetMapId();
        uint32 const instanceId = anyMember->GetInstanceId();
        bool foundMember = false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            // Only online, in-world members participate in recovery.  Offline
            // members have no GroupReference and are intentionally excluded
            // so a disconnected player cannot deadlock a bot-only run, while
            // every online participant must be back in this exact instance
            // before the route resumes.
            if (!member || !member->IsInWorld())
                continue;
            foundMember = true;
            if (member->IsBeingTeleported() || member->GetMapId() != mapId
                || member->GetInstanceId() != instanceId)
                return false;
            if (!member->IsAlive() || member->IsInCombat())
                return false;
            if (member->GetHealthPercent() < EffectiveRestHealth(anyMember))
                return false;
            uint32 maxMana = member->GetMaxPower(POWER_MANA);
            if (member->GetPowerType() == POWER_MANA && maxMana
                && (100.0f * member->GetPower(POWER_MANA) / maxMana) < EffectiveRestMana(anyMember))
                return false;
        }
        return foundMember;
    }

    void CancelDungeonClearEvent(PlayerbotAI* ai)
    {
        if (!ai || !ai->GetAiObjectContext())
            return;

        AiObjectContext* context = ai->GetAiObjectContext();
        DcRunState& state = context->GetValue<DcRunState&>(DcKey::RunState)->Get();
        state.eventId = 0;
        state.eventInstanceId = 0;
        state.eventLastDriveAt = 0;
        state.eventMaxStep = 0;
        state.eventProgressAt = 0;
        state.eventActionSent = false;
        state.eventActionMenuId = 0;
        context->GetValue<uint32&>(DcKey::EventProgress)->Get() = 0;
        context->GetValue<uint32&>(DcKey::EventStartedAt)->Get() = 0;
        context->GetValue<uint32&>(DcKey::EventStepStartedAt)->Get() = 0;
    }

    void ResetDungeonClearRun(PlayerbotAI* ai, Player* bot)
    {
        if (!ai || !bot || !ai->GetAiObjectContext())
            return;

        AiObjectContext* context = ai->GetAiObjectContext();
        context->GetValue<DcRunState&>(DcKey::RunState)->Get().Reset();
        context->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get().clear();
        context->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get().clear();
        context->GetValue<std::string&>(DcKey::StallReason)->Get().clear();
        CancelDungeonClearEvent(ai);
        context->GetValue<std::vector<DungeonBossInfo>>(DcKey::DungeonBosses)->Reset();
        context->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Reset();
    }

    void DisableDungeonClear(PlayerbotAI* ai, Player* bot, char const* reason)
    {
        if (!ai || !bot || !ai->GetAiObjectContext())
            return;
        if (bot)
            DcAddonComm::UnmarkActiveTank(bot->GetObjectGuid());
        ResetDungeonClearRun(ai, bot);
        if (reason)
            TellGroup(ai, bot, std::string("Dungeon clear stopped: ") + reason);
    }

    void TellGroup(PlayerbotAI* ai, Player* bot, std::string const& msg)
    {
        if (!ai || !bot)
            return;
        // Quiet path for the companion addon; avoid party-chat spam.
        DcAddonComm::SendToGroup(ai, bot, "CHAT\t" + msg);
        if (Player* master = ai->GetMaster())
            ai->TellPlayerNoFacing(master, msg);
    }

    Unit* FindHostileNear(Player* bot, float range)
    {
        if (!bot)
            return nullptr;
        std::list<Unit*> list;
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(bot, range);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(list, check);
        Cell::VisitAllObjects(bot, searcher, range);
        Unit* best = nullptr;
        float bestDist = range + 1.0f;
        for (Unit* u : list)
        {
            if (!IsUsableHostile(u))
                continue;
            float d = bot->GetDistance(u);
            if (d < bestDist)
            {
                bestDist = d;
                best = u;
            }
        }
        return best;
    }

    uint32 CountHostileNear(Player* bot, float range)
    {
        return CountHostileNear(bot, bot, range);
    }

    uint32 CountHostileNear(Player* observer, Unit* center, float range)
    {
        if (!observer || !center)
            return 0;
        std::list<Unit*> list;
        // Search around the prospective target, but evaluate friendliness
        // from the pulling player's perspective.  Mobs in one pack are often
        // friendly to one another, so using `center` as the observer would
        // omit exactly the units this safety check must count.
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(center, observer, range);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(list, check);
        Cell::VisitAllObjects(center, searcher, range);

        uint32 count = 0;
        for (Unit* unit : list)
            if (IsUsableHostile(unit) && observer->IsHostileTo(unit))
                ++count;
        return count;
    }

    GameObject* FindGONear(Player* bot, uint32 entry, float range)
    {
        if (!bot || !entry)
            return nullptr;
        std::list<GameObject*> list;
        ai::GameObjectsInObjectRangeCheck check(bot, range, entry);
        MaNGOS::GameObjectListSearcher<ai::GameObjectsInObjectRangeCheck> searcher(list, check);
        Cell::VisitAllObjects(bot, searcher, range);
        return list.empty() ? nullptr : list.front();
    }

    bool CastRezOn(PlayerbotAI* ai, Player* caster, Player* target)
    {
        if (!ai || !caster || !target || target->IsAlive())
            return false;
        uint32 spell = 0;
        switch (caster->getClass())
        {
            case CLASS_PRIEST: spell = 2006; break;
            case CLASS_PALADIN: spell = 7328; break;
            case CLASS_SHAMAN: spell = 2008; break;
            default: break;
        }
        if (spell && ai->CastSpell(spell, target))
            return true;
        if (ai->CastSpell("resurrection", target)) return true;
        if (ai->CastSpell("redemption", target)) return true;
        if (ai->CastSpell("ancestral spirit", target)) return true;
        if (ai->CastSpell("rebirth", target)) return true;
        return false;
    }
}
