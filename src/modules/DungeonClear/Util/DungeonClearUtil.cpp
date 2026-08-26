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
#include "Value/DungeonBossesValue.h"
#include "Value/NextDungeonBossValue.h"
#include <optional>
#include <unordered_set>
#include <vector>

namespace DcUtil
{
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

    void ResetDungeonClearRun(PlayerbotAI* ai, Player* bot)
    {
        if (!ai || !bot || !ai->GetAiObjectContext())
            return;

        AiObjectContext* context = ai->GetAiObjectContext();
        context->GetValue<DcRunState&>(DcKey::RunState)->Get().Reset();
        context->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get().clear();
        context->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get().clear();
        context->GetValue<std::string&>(DcKey::StallReason)->Get().clear();
        context->GetValue<uint32&>(DcKey::EventProgress)->Get() = 0;
        context->GetValue<uint32&>(DcKey::EventStartedAt)->Get() = 0;
        context->GetValue<uint32&>(DcKey::EventStepStartedAt)->Get() = 0;
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
            if (!u || !u->IsAlive() || u->GetTypeId() != TYPEID_UNIT)
                continue;
            Creature* c = static_cast<Creature*>(u);
            if (c->IsCivilian() || c->IsCritter())
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
