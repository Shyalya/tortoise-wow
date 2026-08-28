
#include "playerbot/playerbot.h"
#include "UseItemAction.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "Database/DBCStore.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/TravelMgr.h"
#include "CheckMountStateAction.h"
#include "TellLosAction.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/ItemCountValue.h"
#include "playerbot/strategy/values/QuestValues.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/TravelMgr.h"

using namespace ai;

constexpr std::string_view LOS_GOS_PARAM = "los gos";

SpellCastResult BotUseItemSpell::ForceSpellStart(SpellCastTargets const* targets, Aura* triggeredByAura)
{
    // USE-AFTER-FREE FIX.
    //
    // Original bug: this method pre-queued a `SpellEvent` (lines 37-38 in
    // the prior code) and then called `Prepare()`, which calls
    // `Spell::prepare()`, which queues a SECOND SpellEvent for the same
    // Spell*. When the cast finished, the first SpellEvent's destructor
    // called `m_Spell->Delete()` (frees the Spell). The second SpellEvent
    // remained in the EventProcessor queue with a dangling m_Spell —
    // next tick its Execute() crashed reading m_spellState (offset 0x268)
    // on freed memory.
    //
    // Symptom: ~1-2 min into bot combat with consumable use (Dark Rune
    // mana potion / healthstone / dispel trinket) → ACCESS_VIOLATION READ
    // at +0x268 from a freed Spell. Repro 100% under PageHeap.
    //
    // Fix: drop the manual SpellEvent queue. Let `prepare()` be the sole
    // owner of the Spell + SpellEvent lifecycle. The `m_targets` assignment
    // is kept BEFORE PreCastCheck because lock-check / reagent-check read
    // m_targets. On the failure path we must `Delete()` ourselves since no
    // SpellEvent owns this Spell yet.
    m_targets = *targets;

    if (triggeredByAura)
        m_triggeredByAuraSpell = triggeredByAura->GetSpellProto();

    SpellCastResult result = PreCastCheck();
    bool failed = result != SPELL_CAST_OK;
    if (result == SPELL_FAILED_BAD_TARGETS && OpenLockCheck())
    {
        failed = false;
        m_IsTriggeredSpell = true;
        m_ignoreCastTime = true;
    }

    if (result == SPELL_FAILED_REAGENTS && itemCheats)
    {
        failed = false;
    }

    if (failed)
    {
        SendCastResult(result);
        finish(false);
        // No SpellEvent owns this Spell, so the standard destruction path
        // (~SpellEvent → m_Spell->Delete()) won't fire. Delete ourselves.
        Delete();
        return result;
    }

    // Hand off to Spell::prepare which creates exactly one SpellEvent and
    // becomes the sole owner of this Spell.
    return prepare(*targets, triggeredByAura);
}

bool BotUseItemSpell::OpenLockCheck()
{
    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        // for effects of spells that have only one target
        switch (m_spellInfo->Effect[i])
        {
#ifndef MANGOSBOT_TWO
            case SPELL_EFFECT_OPEN_LOCK_ITEM:
#endif
            case SPELL_EFFECT_OPEN_LOCK:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER) // only players can open locks, gather etc.
                    return false;

                // we need a go target in case of TARGET_GAMEOBJECT (for other targets acceptable GO and items)
                if (m_spellInfo->EffectImplicitTargetA[i] == TARGET_GAMEOBJECT)
                {
                    if (!m_targets.getGOTarget())
                        return false;
                }

                // get the lock entry
                uint32 lockId;
                if (GameObject* go = m_targets.getGOTarget())
                {
                    // In BattleGround players can use only flags and banners
                    if (((Player*)m_caster)->InBattleGround() &&
                        !((Player*)m_caster)->CanUseBattleGroundObject())
                        return false;

                    lockId = go->GetGOInfo()->GetLockId();
                    if (!lockId)
                        return false;

                    // check if its in use only when cast is finished (called from spell::cast() with strict = false)
                    if (go->IsInUse())
                        return false;

                    if (go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE))
                        return false;

                    // done in client but we need to recheck anyway
                    if (go->GetGOInfo()->CannotBeUsedUnderImmunity() && m_caster->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE))
                        return false;
                }
                else if (Item* item = m_targets.getItemTarget())
                {
                    // not own (trade?)
                    if (item->GetOwner() != m_caster)
                        return false;

                    lockId = item->GetProto()->LockID;

                    // if already unlocked
                    if (!lockId || item->HasFlag(ITEM_FIELD_FLAGS, ITEM_DYNFLAG_UNLOCKED))
                        return false;
                }
                else
                    return false;

                if (!lockId)                                            // possible case for GO and maybe for items.
                    return false;

                // Get LockInfo
                LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);

                if (!lockInfo)
                    return false;

                // check lock compatibility
                SpellEffectIndex effIdx = SpellEffectIndex(i);
                SpellCastResult res = CanOpenLock(effIdx, lockId, m_effectSkillInfo[effIdx].skillId, m_effectSkillInfo[effIdx].reqSkillValue, m_effectSkillInfo[effIdx].skillValue);
                if (res == SPELL_FAILED_BAD_TARGETS)
                    return true;
            }
        }
    }

    return false;
}

bool IsFoodOrDrink(const ItemPrototype* proto, uint32 spellCategory)
{
    return proto->Class == ITEM_CLASS_CONSUMABLE &&
           (proto->SubClass == ITEM_SUBCLASS_FOOD || proto->SubClass == ITEM_SUBCLASS_CONSUMABLE) &&
           proto->Spells[0].SpellCategory == spellCategory;
}

bool IsFood(const ItemPrototype* proto)
{
    return IsFoodOrDrink(proto, 11);
}

bool IsDrink(const ItemPrototype* proto)
{
    return IsFoodOrDrink(proto, 59);
}

bool IsTargetValidForItemUse(uint32 itemID, Unit* target)
{
    ItemRequiredTargetMapBounds bounds = sObjectMgr.GetItemRequiredTargetMapBounds(itemID);
    if (bounds.first == bounds.second)
    {
        return true;
    }

    if (target)
    {
        for (ItemRequiredTargetMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            if (itr->second.IsFitToRequirements(target))
            {
                return true;
            }
        }
    }

    return false;
}

bool RequiresItemToUse(const ItemPrototype* itemProto, PlayerbotAI* ai, Player* bot)
{
    // If no item cheat
    if (!ai->HasCheat(BotCheatMask::item))
        return true;

    // Exception items                                  Jujus                                            Holy water    
    const std::unordered_set<uint32> itemExceptions = { 12450, 12451, 12455, 12457, 12458, 12459, 12460, 13180, 7189 };
    if (itemExceptions.find(itemProto->ItemId) != itemExceptions.end())
        return false;

    // Required items                                  Hearthstone, Scourgestone
    const std::unordered_set<uint32> itemsRequired = { 6948, 40582 };
    if (itemsRequired.find(itemProto->ItemId) != itemsRequired.end())
        return true;

    // If item must be equipped
    if (itemProto->InventoryType != INVTYPE_NON_EQUIP)
        return true;

    // If item starts quest
    if (itemProto->StartQuest > 0)
        return true;

    // Quest related items
    if (itemProto->Class == ITEM_CLASS_QUEST)
        return true;

#ifndef MANGOSBOT_ZERO
    // If item is a gem
    if (itemProto->Class == ITEM_CLASS_GEM)
        return true;
#endif

    return false;
}

namespace
{
    struct QuestItemUseCandidate
    {
        Item* item = nullptr;
        Unit* unitTarget = nullptr;
        GameObject* goTarget = nullptr;
        std::string retryKey;
    };

    static constexpr time_t QUEST_ITEM_RETRY_SECONDS = 15;

    std::string QuestItemRetryKey(Item* item, ObjectGuid targetGuid)
    {
        if (!item || !targetGuid)
            return {};

        return "quest item retry " + std::to_string(item->GetEntry()) + " " + std::to_string(targetGuid.GetRawValue());
    }

    bool IsQuestItemRetryBlocked(PlayerbotAI* ai, const std::string& retryKey)
    {
        return ai && !retryKey.empty() &&
               ai->GetAiObjectContext()->GetValue<time_t>("manual time", retryKey)->Get() > time(0);
    }

    bool HasTargetedOnUseSpell(const ItemPrototype* proto)
    {
        if (!proto)
            return false;

        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            const _Spell& spellData = proto->Spells[i];
            if (!spellData.SpellId)
                continue;

#ifdef MANGOSBOT_ZERO
            if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE && spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
#else
            if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
#endif
                continue;

            SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellData.SpellId);
            if (!spellInfo)
                continue;

            uint32 spellTargets = spellInfo->Targets;
            if (spellTargets == 0)
            {
                if ((spellInfo->EffectImplicitTargetA[0] == TARGET_UNIT || spellInfo->EffectImplicitTargetA[0] == TARGET_UNIT_ENEMY) ||
                    (proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_SCROLL) ||
                    (proto->Class == ITEM_CLASS_TRADE_GOODS && proto->SubClass == ITEM_SUBCLASS_EXPLOSIVES) ||
                    spellData.SpellCategory == 150 ||
                    spellData.SpellCategory == 831)
                {
                    spellTargets |= TARGET_FLAG_UNIT;
                }

                if (spellInfo->EffectImplicitTargetA[0] == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC ||
                    (proto->Class == ITEM_CLASS_TRADE_GOODS && proto->SubClass == ITEM_SUBCLASS_EXPLOSIVES))
                {
                    spellTargets |= TARGET_FLAG_DEST_LOCATION;
                }
            }

            if (spellTargets & (TARGET_FLAG_UNIT | TARGET_FLAG_GAMEOBJECT | TARGET_FLAG_LOCKED | TARGET_FLAG_DEST_LOCATION))
                return true;
        }

        return false;
    }

    bool IsQuestItemOwnedAndUsable(Player* bot, Item* item)
    {
        if (!bot || !item)
            return false;

        const ItemPrototype* proto = item->GetProto();
        if (!proto)
            return false;

        if (item->GetOwnerGuid() != bot->GetObjectGuid())
            return false;

        if (item->IsInTrade())
            return false;

        if (proto->InventoryType != INVTYPE_NON_EQUIP && !item->IsEquipped())
            return false;

        return bot->CanUseItem(proto) == EQUIP_ERR_OK;
    }

    bool HasQuestItemCooldown(Player* bot, const ItemPrototype* proto)
    {
        if (!bot || !proto)
            return false;

        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
#ifdef MANGOSBOT_ZERO
            if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE &&
                proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
#else
            if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
#endif
                continue;

            if (proto->Spells[i].SpellId > 0)
            {
                if (!sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId))
                    return true;

                if (!sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId, proto->ItemId))
                    return true;
            }
        }

        return false;
    }

    bool HasRequiredUnitTargetForItem(uint32 itemId, Unit* target)
    {
        if (!target)
            return false;

        ItemRequiredTargetMapBounds bounds = sObjectMgr.GetItemRequiredTargetMapBounds(itemId);
        if (bounds.first == bounds.second)
            return false;

        for (ItemRequiredTargetMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            if (itr->second.IsFitToRequirements(target))
                return true;
        }

        return false;
    }

    bool IsQuestObjectiveStillNeeded(const Quest* quest, const QuestStatusData* questStatus, uint32 objective)
    {
        if (!quest || !questStatus || objective >= QUEST_OBJECTIVES_COUNT || questStatus->m_status != QUEST_STATUS_INCOMPLETE)
            return false;

        return quest->ReqCreatureOrGOCount[objective] &&
               questStatus->m_creatureOrGOcount[objective] < quest->ReqCreatureOrGOCount[objective];
    }

    bool RelationNeedsIncompleteObjective(const Quest* quest, const QuestStatusData* questStatus, uint32 relationFlags)
    {
        for (uint32 objective = 0; objective < QUEST_OBJECTIVES_COUNT; ++objective)
        {
            const uint32 objectiveFlag = 1u << (objective + 1u);
            if (!(relationFlags & objectiveFlag))
                continue;

            if (IsQuestObjectiveStillNeeded(quest, questStatus, objective))
                return true;
        }

        return false;
    }

    bool IsValidQuestUseTarget(Unit* target)
    {
        return target && target->GetTypeId() == TYPEID_UNIT;
    }

    bool IsValidQuestUseTarget(GameObject* target)
    {
        return target && sServerFacade.isSpawned(target) && !target->IsInUse() && target->GetGoState() == GO_STATE_READY;
    }

    bool CanCheckQuestItemCast(Player* bot, Item* item, SpellEntry const* spellInfo, SpellCastTargets const& targets)
    {
        if (!bot || !item || !spellInfo || spellInfo != sSpellTemplate.LookupEntry<SpellEntry>(spellInfo->Id))
            return false;

        Spell* spell = new Spell(bot, spellInfo, false);
        spell->m_targets = targets;
        spell->SetCastItem(item);
        spell->m_targets.setItemTarget(item);

        SpellCastResult result = spell->CheckCast(true);
        delete spell;

        switch (result)
        {
            case SPELL_FAILED_NOT_INFRONT:
            case SPELL_FAILED_NOT_STANDING:
            case SPELL_FAILED_UNIT_NOT_INFRONT:
            case SPELL_FAILED_MOVING:
            case SPELL_FAILED_TRY_AGAIN:
            case SPELL_CAST_OK:
                return true;
            default:
                return false;
        }
    }

    bool CanQuestItemSpellTarget(PlayerbotAI* ai, Player* bot, Item* item, Unit* unitTarget, GameObject* goTarget)
    {
        if (!ai || !bot || !item || (!unitTarget && !goTarget))
            return false;

        const ItemPrototype* proto = item->GetProto();
        if (!proto)
            return false;

        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            const _Spell& spellData = proto->Spells[i];
            if (!spellData.SpellId)
                continue;

#ifdef MANGOSBOT_ZERO
            if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE && spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
#else
            if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
#endif
                continue;

            SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellData.SpellId);
            if (!spellInfo)
                continue;

            if (IsNonCombatSpell(spellInfo) && bot->IsInCombat())
                continue;

            uint32 spellTargets = spellInfo->Targets;
            if (spellTargets == 0)
            {
                if ((spellInfo->EffectImplicitTargetA[0] == TARGET_UNIT || spellInfo->EffectImplicitTargetA[0] == TARGET_UNIT_ENEMY) ||
                    (proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_SCROLL) ||
                    (proto->Class == ITEM_CLASS_TRADE_GOODS && proto->SubClass == ITEM_SUBCLASS_EXPLOSIVES) ||
                    spellData.SpellCategory == 150 ||
                    spellData.SpellCategory == 831)
                {
                    spellTargets |= TARGET_FLAG_UNIT;
                }

                if (spellInfo->EffectImplicitTargetA[0] == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC ||
                    (proto->Class == ITEM_CLASS_TRADE_GOODS && proto->SubClass == ITEM_SUBCLASS_EXPLOSIVES))
                {
                    spellTargets |= TARGET_FLAG_DEST_LOCATION;
                }
            }

            if (unitTarget && IsValidQuestUseTarget(unitTarget))
            {
                if ((spellTargets & TARGET_FLAG_UNIT) && IsTargetValidForItemUse(proto->ItemId, unitTarget))
                {
                    SpellCastTargets targets;
                    targets.setUnitTarget(unitTarget);
                    if (CanCheckQuestItemCast(bot, item, spellInfo, targets) &&
                        ai->CanCastSpell(spellData.SpellId, unitTarget, 0, false, item))
                    {
                        return true;
                    }
                }

                if (spellTargets & TARGET_FLAG_DEST_LOCATION)
                {
                    SpellCastTargets targets;
                    targets.setDestination(unitTarget->GetPositionX(), unitTarget->GetPositionY(), unitTarget->GetPositionZ());
                    if (CanCheckQuestItemCast(bot, item, spellInfo, targets) &&
                        ai->CanCastSpell(spellData.SpellId, unitTarget->GetPositionX(), unitTarget->GetPositionY(), unitTarget->GetPositionZ(), 0, false, item))
                    {
                        return true;
                    }
                }
            }

            if (goTarget && IsValidQuestUseTarget(goTarget))
            {
                if (spellTargets & (TARGET_FLAG_GAMEOBJECT | TARGET_FLAG_LOCKED))
                {
                    SpellCastTargets targets;
                    targets.setGOTarget(goTarget);
                    targets.m_targetMask = TARGET_FLAG_GAMEOBJECT;
                    if (CanCheckQuestItemCast(bot, item, spellInfo, targets) &&
                        ai->CanCastSpell(spellData.SpellId, goTarget, 0, false))
                    {
                        return true;
                    }
                }

                if (spellTargets & TARGET_FLAG_DEST_LOCATION)
                {
                    SpellCastTargets targets;
                    targets.setDestination(goTarget->GetPositionX(), goTarget->GetPositionY(), goTarget->GetPositionZ());
                    if (CanCheckQuestItemCast(bot, item, spellInfo, targets) &&
                        ai->CanCastSpell(spellData.SpellId, goTarget->GetPositionX(), goTarget->GetPositionY(), goTarget->GetPositionZ(), 0, false, item))
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    bool HasQuestItemUseForUnitTarget(PlayerbotAI* ai, Player* bot, Item* item, Unit* target, int32 relationEntry, const EntryQuestRelationMap& relationMap)
    {
        if (!ai || !bot || !item || !target)
            return false;

        auto relationItr = relationMap.find(relationEntry);
        if (relationItr == relationMap.end())
            return false;

        const ItemPrototype* proto = item->GetProto();
        if (!proto)
            return false;

        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = bot->GetQuestSlotQuestId(slot);
            if (!questId)
                continue;

            const Quest* quest = sObjectMgr.GetQuestTemplate(questId);
            const QuestStatusData* questStatus = bot->GetQuestStatusData(questId);
            if (!quest || !questStatus || questStatus->m_status != QUEST_STATUS_INCOMPLETE)
                continue;

            auto questRelationItr = relationItr->second.find(questId);
            if (questRelationItr == relationItr->second.end())
                continue;

            if (!RelationNeedsIncompleteObjective(quest, questStatus, questRelationItr->second))
                continue;

            const bool isSourceItem = quest->GetSrcItemId() == proto->ItemId;
            const bool hasRequiredUnitTarget = HasRequiredUnitTargetForItem(proto->ItemId, target);
            if (!isSourceItem && !hasRequiredUnitTarget)
                continue;

            if (CanQuestItemSpellTarget(ai, bot, item, target, nullptr))
            {
                return true;
            }
        }

        return false;
    }

    bool HasQuestItemUseForGameObjectTarget(PlayerbotAI* ai, Player* bot, Item* item, GameObject* target, int32 relationEntry, const EntryQuestRelationMap& relationMap)
    {
        if (!ai || !bot || !item || !target)
            return false;

        auto relationItr = relationMap.find(relationEntry);
        if (relationItr == relationMap.end())
            return false;

        const ItemPrototype* proto = item->GetProto();
        if (!proto)
            return false;

        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = bot->GetQuestSlotQuestId(slot);
            if (!questId)
                continue;

            const Quest* quest = sObjectMgr.GetQuestTemplate(questId);
            const QuestStatusData* questStatus = bot->GetQuestStatusData(questId);
            if (!quest || !questStatus || questStatus->m_status != QUEST_STATUS_INCOMPLETE)
                continue;

            auto questRelationItr = relationItr->second.find(questId);
            if (questRelationItr == relationItr->second.end())
                continue;

            if (!RelationNeedsIncompleteObjective(quest, questStatus, questRelationItr->second))
                continue;

            if (quest->GetSrcItemId() != proto->ItemId)
                continue;

            if (CanQuestItemSpellTarget(ai, bot, item, nullptr, target))
                return true;
        }

        return false;
    }

    std::vector<QuestItemUseCandidate> CollectQuestItemUseCandidates(PlayerbotAI* ai, Player* bot)
    {
        std::vector<QuestItemUseCandidate> candidates;
        if (!ai || !bot)
            return candidates;

        AiObjectContext* context = ai->GetAiObjectContext();
        if (!context)
            return candidates;

        auto* relationValue = dynamic_cast<SingleCalculatedValue<EntryQuestRelationMap>*>(
            sSharedObjectContext.GetUntypedValue("entry quest relation"));
        if (!relationValue)
            return candidates;

        const EntryQuestRelationMap& relationMap = relationValue->GetReference();
        std::list<Item*> questItems = AI_VALUE2(std::list<Item*>, "inventory items", "quest");
        if (questItems.empty())
            return candidates;

        std::list<ObjectGuid> friendlyOrNeutralUnits = AI_VALUE(std::list<ObjectGuid>, "nearest npcs no los");
        std::list<ObjectGuid> hostileOrNeutralUnits = AI_VALUE(std::list<ObjectGuid>, "possible targets no los");
        std::list<ObjectGuid> deadUnits = AI_VALUE(std::list<ObjectGuid>, "nearest corpses");
        std::list<ObjectGuid> gameObjects = bot->GetMap()->IsDungeon() ? AI_VALUE(std::list<ObjectGuid>, "nearest game objects")
                                                                       : AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");

        for (Item* item : questItems)
        {
            const ItemPrototype* proto = item ? item->GetProto() : nullptr;
            if (!proto || !IsQuestItemOwnedAndUsable(bot, item))
                continue;

            if (HasQuestItemCooldown(bot, proto))
                continue;

            if (!HasTargetedOnUseSpell(proto))
                continue;

            for (const ObjectGuid& guid : friendlyOrNeutralUnits)
            {
                Unit* target = ai->GetUnit(guid);
                if (!IsValidQuestUseTarget(target))
                    continue;

                std::string retryKey = QuestItemRetryKey(item, guid);
                if (IsQuestItemRetryBlocked(ai, retryKey))
                    continue;

                if (HasQuestItemUseForUnitTarget(ai, bot, item, target, int32(target->GetEntry()), relationMap))
                {
                    candidates.push_back({ item, target, nullptr, retryKey });
                    return candidates;
                }
            }

            for (const ObjectGuid& guid : hostileOrNeutralUnits)
            {
                Unit* target = ai->GetUnit(guid);
                if (!IsValidQuestUseTarget(target))
                    continue;

                std::string retryKey = QuestItemRetryKey(item, guid);
                if (IsQuestItemRetryBlocked(ai, retryKey))
                    continue;

                if (HasQuestItemUseForUnitTarget(ai, bot, item, target, int32(target->GetEntry()), relationMap))
                {
                    candidates.push_back({ item, target, nullptr, retryKey });
                    return candidates;
                }
            }

            for (const ObjectGuid& guid : deadUnits)
            {
                Unit* target = ai->GetUnit(guid);
                if (!IsValidQuestUseTarget(target))
                    continue;

                std::string retryKey = QuestItemRetryKey(item, guid);
                if (IsQuestItemRetryBlocked(ai, retryKey))
                    continue;

                if (HasQuestItemUseForUnitTarget(ai, bot, item, target, int32(target->GetEntry()), relationMap))
                {
                    candidates.push_back({ item, target, nullptr, retryKey });
                    return candidates;
                }
            }

            for (const ObjectGuid& guid : gameObjects)
            {
                GameObject* target = ai->GetGameObject(guid);
                if (!IsValidQuestUseTarget(target))
                    continue;

                std::string retryKey = QuestItemRetryKey(item, guid);
                if (IsQuestItemRetryBlocked(ai, retryKey))
                    continue;

                if (HasQuestItemUseForGameObjectTarget(ai, bot, item, target, -int32(target->GetEntry()), relationMap))
                {
                    candidates.push_back({ item, nullptr, target, retryKey });
                    return candidates;
                }
            }
        }

        return candidates;
    }
}

bool UseAction::Execute(Event& event)
{
    Player* requester = event.getOwner();
    std::string useName = event.getParam();

    if (useName.empty())
    {
        useName = getQualifier();
    }

    if (useName.empty())
    {
        useName = name;
    }

    MakeVerbose(requester != nullptr);

    uint32 itemID = 0;
    Item* targetItem = nullptr;
    GameObject* targetGameObject = nullptr;

    if (useName == "go")
    {
        float closest = 9999.0f;
        std::list<ObjectGuid> nearestGOs = AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");
        for (const ObjectGuid& goGUID : nearestGOs)
        {
            GameObject* go = ai->GetGameObject(goGUID);
            if (go)
            {
                const float distance = bot->GetDistance(go);
                if (distance < closest)
                {
                    targetGameObject = go;
                    closest = distance;
                }
            }
        }
    }
    else if (useName.find(LOS_GOS_PARAM) == 0)
    {
       std::list<GameObject*> gos;
       std::vector<LosModifierStruct> mods = TellLosAction::ParseLosModifiers(useName.substr(LOS_GOS_PARAM.size()));
       gos = TellLosAction::FilterGameObjects(requester, TellLosAction::GoGuidListToObjList(ai, AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los")), mods);

       for (GameObject* go : gos)
       {
          if (go)
          {
             targetGameObject = go;
             break;
          }
       }
    }
    else
    {
        std::vector<uint32> items = chat->parseItemsUnordered(useName, false);
        if (items.empty())
        {
            std::list<Item*> inventoryItems = AI_VALUE2(std::list<Item*>, "inventory items", useName);
            for (Item* inventoryItem : inventoryItems)
            {
                if (inventoryItem)
                {
                    items.push_back(inventoryItem->GetEntry());
                }
            }
        }

        if (!items.empty())
        {
            itemID = items[0];
            if (items.size() > 1)
            {
                targetItem = bot->GetItemByEntry(items[1]);
            }
        }

        std::list<ObjectGuid> gos = chat->parseGameobjects(useName);
        if (!gos.empty())
        {
            targetGameObject = ai->GetGameObject(*gos.begin());
        }

        float closest = 9999.0f;
        std::list<ObjectGuid> nearestGOs = AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");
        for (const ObjectGuid& goGUID : nearestGOs)
        {
            GameObject* go = ai->GetGameObject(goGUID);
            if (go && std::string(go->GetName()).find(useName))
            {
                const float distance = bot->GetDistance(go);
                if (distance < closest)
                {
                    targetGameObject = go;
                    closest = distance;
                }
            }
        }
    }

    if (targetGameObject != nullptr)
    {
        if (itemID != 0)
        {
            return UseItem(requester, itemID, targetGameObject);
        }
        else
        {
            return UseGameObject(requester, event, targetGameObject);
        }
    }
    else
    {
        if (itemID != 0)
        {
            if (targetItem)
            {
                return UseItem(requester, itemID, targetItem);
            }
            else
            {
                Unit* target = nullptr;
                if (requester && ai->HasActivePlayerMaster() && requester->GetSelectionGuid())
                {
                    target = ai->GetUnit(requester->GetSelectionGuid());
                }

                return UseItem(requester, itemID, target);
            }
        }
    }

    ai->TellPlayerNoFacing(requester, "No items or game objects available");
    return false;
}

bool UseAction::UseItem(Player* requester, uint32 itemId, Unit* target)
{
    return UseItemInternal(requester, itemId, target, nullptr, nullptr);
}

bool UseAction::UseItem(Player* requester, uint32 itemId, GameObject* target)
{
    return UseItemInternal(requester, itemId, nullptr, target, nullptr);
}

bool UseAction::UseItem(Player* requester, uint32 itemId, Item* target)
{
    return UseItemInternal(requester, itemId, nullptr, nullptr, target);
}

bool UseAction::UseItemInternal(Player* requester, uint32 itemId, Unit* unit, GameObject* gameObject, Item* item)
{
    // Check for valid item ID
    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
    {
        if (verbose)
        {
            std::map<std::string, std::string> replyArgs;
            replyArgs["%item"] = itemId;
            ai->TellPlayerNoFacing(requester, BOT_TEXT2("use_command_invalid_item", replyArgs), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        return false;
    }

    if (proto->Flags & ITEM_FLAG_HAS_LOOT)
    {
        std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", ChatHelper::formatQItem(itemId));
        if (!items.empty())
        {
            return OpenItem(requester, items.front());
        }
    }

    // If bot has no item cheat (or other conditions) it needs to own the item to cast
    Item* itemUsed = nullptr;
    if (RequiresItemToUse(proto, ai, bot))
    {
        std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", ChatHelper::formatQItem(itemId));
        if (!items.empty())
        {
            itemUsed = items.front();
        }
        else
        {
            if (verbose)
            {
                std::map<std::string, std::string> replyArgs;
                replyArgs["%item"] = ChatHelper::formatItem(proto);
                ai->TellPlayerNoFacing(requester, BOT_TEXT2("use_command_item_not_owned", replyArgs), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            }

            return false;
        }
    }

    // Check for items that give quests on use (must have item in inventory)
    if (proto->StartQuest > 0)
    {
        return UseQuestGiverItem(requester, itemUsed);
    }

#ifndef MANGOSBOT_ZERO
    // Check for gem items
    if (proto->Class == ITEM_CLASS_GEM)
    {
        return UseGemItem(requester, item, itemUsed, true);
    }
#endif

    // Check for item equipped, skip exceptions listed in RequiresItemToUse
    if (proto->InventoryType != INVTYPE_NON_EQUIP && itemUsed && !itemUsed->IsEquipped())
    {
        if (verbose)
        {
            std::map<std::string, std::string> replyArgs;
            replyArgs["%item"] = chat->formatItem(proto);
            ai->TellPlayerNoFacing(requester, BOT_TEXT2("use_command_item_error", replyArgs), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        return false;
    }

    // Check for item usable
    InventoryResult invResult = bot->CanUseItem(proto);
    if (invResult != EQUIP_ERR_OK)
    {
        if (verbose)
        {
            std::map<std::string, std::string> replyArgs;
            replyArgs["%item"] = chat->formatItem(proto);
            ai->TellPlayerNoFacing(requester, BOT_TEXT2("use_command_item_error", replyArgs), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        return false;
    }

    // Check for trade
    if (itemUsed && itemUsed->IsInTrade())
    {
        if (verbose)
        {
            std::map<std::string, std::string> replyArgs;
            replyArgs["%item"] = chat->formatItem(proto);
            ai->TellPlayerNoFacing(requester, BOT_TEXT2("use_command_item_error", replyArgs), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        return false;
    }

    // Check for item cooldown
    if (HasItemCooldown(itemId))
    {
        if (verbose)
        {
            std::map<std::string, std::string> replyArgs;
            replyArgs["%item"] = chat->formatItem(proto);
            ai->TellPlayerNoFacing(requester, BOT_TEXT2("use_command_item_cooldown", replyArgs), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        return false;
    }

    Unit* unitTarget = nullptr;
    Item* itemTarget = nullptr;
    GameObject* gameObjectTarget = nullptr;

    uint8 successCasts = 0;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        // No spell
        const auto& spellData = proto->Spells[i];
        if (!spellData.SpellId)
        {
            continue;
        }

        // Wrong triggering type
#ifdef MANGOSBOT_ZERO
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE && spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
#else
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
#endif
        {
            continue;
        }

        const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellData.SpellId);
        if (!spellInfo)
        {
            continue;
        }

        if (IsNonCombatSpell(spellInfo) && bot->IsInCombat())
        {
            continue;
        }

        // Check if valid targets
        bool validTarget = false;
        SpellCastTargets targets;
        
        // Try to figure out which targets are allowed if the spell doesn't provide it
        uint32 spellTargets = spellInfo->Targets;
        if (spellTargets == 0)
        {
            // Unit target
            if ((spellInfo->EffectImplicitTargetA[0] == TARGET_UNIT || spellInfo->EffectImplicitTargetA[0] == TARGET_UNIT_ENEMY) || // Unit Target
                (proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_SCROLL) || // Scrolls
                (proto->Class == ITEM_CLASS_TRADE_GOODS && proto->SubClass == ITEM_SUBCLASS_EXPLOSIVES) || // Explosives
                (spellData.SpellCategory == 150) || // First aid
                (spellData.SpellCategory == 831)) // Soulstone 
            {
                spellTargets |= TARGET_FLAG_UNIT;
            }

            // Location target
            if ((spellInfo->EffectImplicitTargetA[0] == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC) || // Hostile Aoe Spell
                (proto->Class == ITEM_CLASS_TRADE_GOODS && proto->SubClass == ITEM_SUBCLASS_EXPLOSIVES)) // Explosives
            {
                spellTargets |= TARGET_FLAG_DEST_LOCATION;
            }

            // No target
            if (spellTargets == 0 && 
                (spellInfo->EffectImplicitTargetA[0] == TARGET_NONE || // No target
                 spellInfo->EffectImplicitTargetA[0] == TARGET_UNIT_CASTER)) // Self Target
            {
                validTarget = true;
            }
        }

        if (spellTargets & TARGET_FLAG_DEST_LOCATION)
        {
            if (unit)
            {
                unitTarget = unit;
                targets.setDestination(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ());
                validTarget = true;
            }
            else if (gameObject && gameObject->IsSpawned())
            {
                gameObjectTarget = gameObject;
                targets.setDestination(gameObject->GetPositionX(), gameObject->GetPositionY(), gameObject->GetPositionZ());
                validTarget = true;
            }
        }
        
        if (spellTargets & TARGET_FLAG_UNIT && !validTarget)
        {
            if (unit && IsTargetValidForItemUse(itemId, unit))
            {
                unitTarget = unit;
                targets.setUnitTarget(unit);
                validTarget = true;
            }
        }
        
        if ((spellTargets & TARGET_FLAG_GAMEOBJECT || spellTargets & TARGET_FLAG_LOCKED) && !validTarget)
        {
            if (gameObject && gameObject->IsSpawned())
            {
                gameObjectTarget = gameObject;
                targets.setGOTarget(gameObject);
                targets.m_targetMask = TARGET_FLAG_GAMEOBJECT;
                validTarget = true;
            }
        }

        if (spellTargets & TARGET_FLAG_ITEM && !validTarget)
        {
            if (item)
            {
                itemTarget = item;
                targets.setItemTarget(item);
                validTarget = true;
            }
            else
            {
                // Try to figure out the item target
                Item* itemForSpell = AI_VALUE2(Item*, "item for spell", spellInfo->Id);
                if (itemForSpell)
                {
                    if (!itemForSpell->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
                    {
                        validTarget = true;
                        itemTarget = itemForSpell;

                        if (bot->GetTrader())
                        {
                            targets.setTradeItemTarget(bot);
                        }
                        else
                        {
                            targets.setItemTarget(itemForSpell);
                        }
                    }
                }
            }
        }
        
        if (!validTarget)
        {
            targets.setUnitTarget(bot);
            targets.m_targetMask = TARGET_FLAG_SELF;
            validTarget = true;
        }

        if (validTarget)
        {
            // Validate spellInfo is a proper sSpellTemplate entry before constructing (mirrors MANGOS_ASSERT in Spell::Spell)
            if (!spellInfo || spellInfo != sSpellTemplate.LookupEntry<SpellEntry>(spellInfo->Id))
            {
                continue;
            }

            // Use triggered flag only for items with many spell casts and for not first cast
            BotUseItemSpell* spell = new BotUseItemSpell(bot, spellInfo, (successCasts > 0) ? TRIGGERED_OLD_TRIGGERED : TRIGGERED_NONE);
            spell->m_clientCast = true;
            
#ifdef MANGOSBOT_ONE
            // used in item_template.spell_2 with spell_id with SPELL_GENERIC_LEARN in spell_1
            if (spellInfo->Id == SPELL_ID_GENERIC_LEARN && proto->Spells[1].SpellTrigger == ITEM_SPELLTRIGGER_LEARN_SPELL_ID)
                spell->m_currentBasePoints[EFFECT_INDEX_0] = proto->Spells[1].SpellId; 
#endif
#ifdef MANGOSBOT_TWO
            // used in item_template.spell_2 with spell_id with SPELL_GENERIC_LEARN in spell_1
            if ((spellInfo->Id == SPELL_ID_GENERIC_LEARN
                || spellInfo->Id == SPELL_ID_GENERIC_LEARN_PET) && proto->Spells[1].SpellTrigger == ITEM_SPELLTRIGGER_LEARN_SPELL_ID)
                spell->m_currentBasePoints[EFFECT_INDEX_0] = proto->Spells[1].SpellId;
#endif

            // Spend the item if used in the spell
            if (itemUsed)
            {
                spell->SetCastItem(itemUsed);
                itemUsed->SetUsedInSpell(true);
            }

            // Stop the movement for casted items
            const bool isCastedSpell = spell->GetCastedTime() > 0 || IsChanneledSpell(spellInfo);
            if (isCastedSpell)
            {
                ai->StopMoving();
            }

            const SpellCastResult result = spell->ForceSpellStart(&targets);

            // Check for using item on trade item
            bool successCast = result == SPELL_CAST_OK;
            if (result == SPELL_FAILED_DONT_REPORT && (targets.m_targetMask & TARGET_FLAG_TRADE_ITEM))
            {
                successCast = true;
            }

            if (successCast)
            {
                if (itemUsed == nullptr && ai->HasCheat(BotCheatMask::item))
                {
                    if (!HasItemCooldown(itemId))
                    {
                        bot->RemoveSpellCooldown(*spellInfo, false);
                        bot->AddCooldown(*spellInfo, proto, false);
                    }
                }

                if (IsFood(proto) || IsDrink(proto))
                {
                    SetDuration(24000);
                }
                else
                {
                    SetDuration(ai->GetSpellCastDuration(spell));
                }

                ++successCasts;
            }
            else
            {
                break;
            }
        }
    }

    if (successCasts > 0)
    {
        if (verbose)
        {
            std::map<std::string, std::string> replyArgs;
            replyArgs["%target"] = chat->formatItem(proto);
            std::ostringstream replyStr; replyStr << BOT_TEXT("use_command");

            // Target
            if (itemTarget)
            {
                replyArgs["%item"] = chat->formatItem(itemTarget);
                replyStr << " " << BOT_TEXT("command_target_item");
            }
            else if (unitTarget)
            {
                replyArgs["%unit"] = unitTarget->GetName();
                replyStr << " " << BOT_TEXT("command_target_unit");
            }
            else if (gameObjectTarget)
            {
                replyArgs["%gameobject"] = chat->formatGameobject(gameObjectTarget);
                replyStr << " " << BOT_TEXT("command_target_go");
            }
            else
            {
                replyStr << " " << BOT_TEXT("command_target_self");
            }

            if (itemUsed && proto->Stackable > 1)
            {
                uint32 count = itemUsed->GetCount();
                if (count > 1)
                {
                    replyArgs["%amount"] = count;
                    replyStr << " " << BOT_TEXT("use_command_remaining");
                }
                else
                {
                    replyStr << " " << BOT_TEXT("use_command_last");
                }
            }

            ai->TellPlayerNoFacing(requester, BOT_TEXT2(replyStr.str(), replyArgs), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        RESET_AI_VALUE2(uint32, "item count", itemId);

        return true;
    }
    else
    {
        if (verbose)
        {
            std::map<std::string, std::string> replyArgs;
            replyArgs["%item"] = chat->formatItem(proto);
            ai->TellPlayerNoFacing(requester, BOT_TEXT2("use_command_item_error", replyArgs), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }
    }

    return false;
}

bool UseAction::UseGameObject(Player* requester, Event& event, GameObject* gameObject)
{
    if (gameObject == nullptr)
    {
        ai->TellPlayerNoFacing(requester, "Invalid game object", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return false;
    }

    ObjectGuid guid = gameObject->GetObjectGuid();
    if (!sServerFacade.isSpawned(gameObject) || gameObject->IsInUse() || gameObject->GetGoState() != GO_STATE_READY)
    {
        std::ostringstream out; out << "I can't use " << chat->formatGameobject(gameObject);
        ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return false;
    }

    if (bot->GetDistance(gameObject) > INTERACTION_DISTANCE)
    {
        std::ostringstream out; out << "I'm too far away from " << chat->formatGameobject(gameObject);
        ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return false;
    }

    if (gameObject->GetGoType() == GAMEOBJECT_TYPE_CHEST)
    {
        SET_AI_VALUE(LootObject, "loot target", LootObject(bot, guid));

        if (ai->DoSpecificAction("open loot", event, true))
        {
            std::ostringstream out; out << "Looting " << chat->formatGameobject(gameObject);
            ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            return true;
        }
        else
        {
            std::ostringstream out; out << "Failed to loot " << chat->formatGameobject(gameObject);
            ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            return false;
        }
    }
    else if(gameObject->GetGoType() == GAMEOBJECT_TYPE_DOOR)
    {
        uint32 lockId = gameObject->GetGOInfo()->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (lockInfo)
        {
            uint32 keyRequired = 0;
            auto CanOpenLock = [&](const SpellEntry* pSpellInfo, GameObject* go) -> bool
            {
                if (!pSpellInfo)
                    return false;

                for (int effIndex = 0; effIndex <= EFFECT_INDEX_2; effIndex++)
                {
                    if (pSpellInfo->Effect[effIndex] != SPELL_EFFECT_OPEN_LOCK && pSpellInfo->Effect[effIndex] != SPELL_EFFECT_SKINNING)
                        return false;

                    for (int j = 0; j < 8; ++j)
                    {
                        if (lockInfo->Type[j] == LOCK_KEY_ITEM)
                        {
                            keyRequired = lockInfo->Index[j];
                        }
                        else if (lockInfo->Type[j] == LOCK_KEY_SKILL)
                        {
                            if (uint32(pSpellInfo->EffectMiscValue[effIndex]) != lockInfo->Index[j])
                            {
                                continue;
                            }

                            uint32 skillId = SkillByLockType(LockType(lockInfo->Index[j]));
                            if (skillId == SKILL_NONE)
                            {
                                continue;
                            }

                            uint32 skillValue = bot->GetSkillValue(skillId);
                            uint32 reqSkillValue = lockInfo->Skill[j];
                            if (skillValue >= reqSkillValue || !reqSkillValue)
                            {
                                return true;
                            }
                        }
                    }
                }

                return false;
            };

            // Find a spell that can open the door
            uint32 spellId = 0;
            for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
            {
                uint32 possibleSpellId = itr->first;
                if (itr->second.state == PLAYERSPELL_REMOVED || itr->second.disabled || IsPassiveSpell(possibleSpellId))
                    continue;

                if (CanOpenLock(sServerFacade.LookupSpellInfo(possibleSpellId), gameObject))
                {
                    spellId = possibleSpellId;
                    break;
                }
            }

            if (spellId)
            {
                uint32 spellDuration = sPlayerbotAIConfig.globalCoolDown;
                if (ai->CastSpell(spellId, gameObject, nullptr, true, &spellDuration))
                {
                    std::ostringstream out; out << "Opening " << chat->formatGameobject(gameObject);
                    ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                    SetDuration(spellDuration);
                    return true;
                }
            }
            else if (keyRequired > 0)
            {
                std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", chat->formatQItem(keyRequired));
                if (!items.empty())
                {
                    gameObject->Use(bot);
                    std::ostringstream out; out << "Opening " << chat->formatGameobject(gameObject);
                    ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                    return true;
                }
            }

            std::ostringstream out; out << "I can't open " << chat->formatGameobject(gameObject);
            ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            return false;
        }
    }
    else if (gameObject->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER)
    {
        if (gameObject->ActivateToQuest(bot))
        {
            Event event("use game object", gameObject->GetObjectGuid(), bot);
            ai->DoSpecificAction("talk to quest giver", event, true);
        }
    }

    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
    *packet << guid;
    bot->GetSession()->QueuePacket(std::move(packet));
    
    std::ostringstream out; out << "Using " << chat->formatGameobject(gameObject);
    ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    return true;
}

bool UseAction::UseQuestGiverItem(Player* requester, Item* item)
{
    if (!item)
        return false;

    const ItemPrototype* proto = item->GetProto();
    if (!proto || !proto->StartQuest)
        return false;

    const Quest* quest = sObjectMgr.GetQuestTemplate(proto->StartQuest);
    if (!quest)
        return false;

    if (!bot->CanTakeQuest(quest, false))
        return false;

    if (item->GetOwnerGuid() != bot->GetObjectGuid())
        return false;

    if (!bot->HasItemCount(proto->ItemId, 1))
        return false;

    Item* validItem = bot->GetItemByGuid(item->GetObjectGuid());
    if (!validItem || validItem != item)
        return false;

    if (!bot->CanAddQuest(quest, true))
        return false;

    bot->AddQuest(quest, nullptr);

    if (bot->CanCompleteQuest(quest->GetQuestId()))
        bot->CompleteQuest(quest->GetQuestId());

    bot->DestroyItemCount(proto->ItemId, 1, true);

    if (verbose)
    {
        std::map<std::string, std::string> replyArgs;
        replyArgs["%quest"] = chat->formatQuest(quest);
        ai->TellPlayerNoFacing(requester, BOT_TEXT2("quest_accepted", replyArgs), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }

    return true;
}

bool UseAction::OpenItem(Player* requester, Item* item)
{
    if (!item)
        return false;

    uint32 spellId = 0;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (item->GetProto()->Spells[i].SpellId > 0)
        {
            spellId = item->GetProto()->Spells[i].SpellId;
            break;
        }
    }

    if (spellId)
        return false;

    if (!(item->GetProto()->Flags & ITEM_FLAG_HAS_LOOT))
        return false;

        // Open quest item in inventory, containing related items (e.g Gnarlpine necklace, containing Tallonkai's Jewel)
        std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_OPEN_ITEM, 2));
        *packet << item->GetBagSlot();
        *packet << item->GetSlot();
        bot->GetSession()->QueuePacket(std::move(packet)); // queue the packet to get around race condition
        return true;
}


bool UseAction::HasItemCooldown(uint32 itemId) const
{
    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
    if (proto)
    {
        uint32 spellId = 0;
        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
            {
                continue;
            }

            if (proto->Spells[i].SpellId > 0)
            {
                if (!sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId))
                    return true;

                if (!sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId, itemId))
                    return true;
            }
        }
    }

    return false;
}

#ifndef MANGOSBOT_ZERO
bool UseAction::UseGemItem(Player* requester, Item* item, Item* gem, bool replace)
{
    if (!item || !gem)
    {
        return false;
    }

    WorldPacket* const packet = new WorldPacket(CMSG_SOCKET_GEMS);
    *packet << item->GetObjectGuid();

    bool fits = false;
    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT + MAX_GEM_SOCKETS; ++enchant_slot)
    {
        uint8 SocketColor = item->GetProto()->Socket[enchant_slot - SOCK_ENCHANTMENT_SLOT].Color;
        GemPropertiesEntry const* gemProperty = sGemPropertiesStore.LookupEntry(gem->GetProto()->GemProperties);
        if (gemProperty && (gemProperty->color & SocketColor))
        {
            if (fits)
            {
                *packet << ObjectGuid();
                continue;
            }

            uint32 enchant_id = item->GetEnchantmentId(EnchantmentSlot(enchant_slot));
            if (!enchant_id)
            {
                *packet << gem->GetObjectGuid();
                fits = true;
                continue;
            }

            SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
            if (!enchantEntry || !enchantEntry->GemID)
            {
                *packet << gem->GetObjectGuid();
                fits = true;
                continue;
            }

            if (replace && enchantEntry->GemID != gem->GetProto()->ItemId)
            {
                *packet << gem->GetObjectGuid();
                fits = true;
                continue;
            }
        }

        *packet << ObjectGuid();
    }

    if (fits)
    {
        if (verbose)
        {
            std::map<std::string, std::string> replyArgs;
            replyArgs["%item"] = chat->formatItem(item);
            replyArgs["%gem"] = chat->formatItem(gem);
            ai->TellPlayerNoFacing(requester, BOT_TEXT2("use_command_socket", replyArgs), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        bot->GetSession()->HandleSocketOpcode(*packet);

        return true;
    }
    else
    {
        if (verbose)
        {
            ai->TellPlayerNoFacing(requester, BOT_TEXT("use_command_socket_error"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        return false;
    }
}
#endif

bool UseItemIdAction::Execute(Event& event)
{
    Player* requester = event.getOwner();
    MakeVerbose(requester != nullptr);

    uint32 itemId = 0;
    Unit* unitTarget = nullptr;
    GameObject* gameObjectTarget = nullptr;

    if (getQualifier().empty())
    {
        itemId = GetItemId();
        unitTarget = GetTarget();
        std::string params = event.getParam();
        std::list<ObjectGuid> gos = chat->parseGameobjects(params);
        if (!gos.empty())
        {
            gameObjectTarget = ai->GetGameObject(*gos.begin());
        }
    }
    else
    {
        std::vector<std::string> params = getMultiQualifiers(getQualifier(), ",");
        itemId = stoi(params[0]);
        if (params.size() > 1)
        {
            std::list<GuidPosition> guidPs = AI_VALUE(std::list<GuidPosition>, params[1]);
            if (!guidPs.empty())
            {
                GuidPosition guidP = *guidPs.begin();
                if (guidP.IsGameObject())
                {
                    gameObjectTarget = guidP.GetGameObject(bot->GetInstanceId());
                }
                else
                {
                    unitTarget = guidP.GetUnit(bot->GetInstanceId());
                }
            }
        }
    }

    if (gameObjectTarget)
    {
        return UseItem(requester, itemId, gameObjectTarget);
    }
    else
    {
        return UseItem(requester, itemId, unitTarget);
    }
}

bool UseItemIdAction::isPossible()
{
    uint32 itemId = GetItemId();
    if (!itemId)
        return false;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
        return false;

    if (HasItemCooldown(itemId))
        return false;

        if (!ai->HasCheat(BotCheatMask::item) && !bot->HasItemCount(itemId, 1))
        return false;


    uint32 spellCount = 0;

    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        _Spell const& spellData = proto->Spells[i];

        // no spell
        if (!spellData.SpellId)
            continue;

        // wrong triggering type
#ifdef MANGOSBOT_ZERO
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE && spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
#else
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
#endif
            continue;

        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellData.SpellId);
        if (!spellInfo)
        {
            continue;
        }

        spellCount++;        
    }

    return spellCount;
}

bool UseItemIdAction::isUseful()
{
    if (!getQualifier().empty() && (getMultiQualifierStr(getQualifier(), 0, ",").find_first_not_of("0123456789") != std::string::npos))
    {
        sLog.outError("UseItemIdAction::isUseful with qualifier %s, should be number.", getQualifier().c_str());
        return false;
    }

    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(GetItemId());
    if (proto)
    {
        std::set<uint32>& skipSpells = AI_VALUE(std::set<uint32>&, "skip spells list");
        if(!skipSpells.empty())
        {
            for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                const _Spell& spellData = proto->Spells[i];
                if (spellData.SpellId)
                {
                    if (skipSpells.find(spellData.SpellId) != skipSpells.end())
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    return false;
}

bool UseSpellItemAction::isUseful()
{
   return AI_VALUE2(bool, "spell cast useful", getName());
}

bool UseHearthStoneAction::Execute(Event& event)
{
    if (bot->IsMoving())
    {
        ai->StopMoving();
    }

    if (AI_VALUE2(uint32, "current mount speed", "self target"))
    {
        if (bot->IsFlying() && WorldPosition(bot).currentHeight() > 10.0f)
            return false;

        ai->Unmount();
    }

    ai->RemoveShapeshift();

    if (!bot->HasItemCount(6948, 1)) //Hearthstone
    {
        if (!bot->HasItemCount(40582, 1)) //Scourgestone
            return false;

        event = Event(event.getSource(), "scourgestone");
    }

    const bool used = UseAction::Execute(event);
    if (used)
    {
        sPlayerbotAIConfig.logEvent(ai, "UseHearthStoneAction", event.getParam(), event.getSource());
        RESET_AI_VALUE(bool, "combat::self target");
        RESET_AI_VALUE(WorldPosition, "current position");
    }

    return used;
}

bool UseHearthStoneAction::isUseful() 
{
    uint32 spellId = 8690;
    if (!bot->HasItemCount(6948, 1)) //Hearthstone
    {
        if (!bot->HasItemCount(40582, 1)) //Scourgestone
            return false;

        spellId = 54403;
    }

    if (!ai->HasActivePlayerMaster() && ai->IsGroupLeader()) //Only hearthstone if entire group can use it.
    {
        if (AI_VALUE2(bool, "group or", "not::spell ready::" + std::to_string(spellId)))
            return false;
    }
    else if (!AI_VALUE2(bool, "spell ready", spellId))
        return false;

    if (bot->InBattleGround())
        return false;

    Player* master = ai->GetMaster();

    //Do not HS in dungeons when master is inside the dungeon or dead.
    if (master && master != bot && !WorldPosition(bot).isOverworld() && (bot->GetMapId() == master->GetMapId() || !master->IsAlive()))
        return false;

    if (bot->IsFlying() && WorldPosition(bot).currentHeight() > 10.0f)
        return false;

    return true;
}

bool UseRandomRecipeAction::isUseful()
{
   return !bot->IsInCombat() && !bot->InBattleGround();
}

bool UseRandomRecipeAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    std::list<Item*> recipes = AI_VALUE2(std::list<Item*>, "inventory items", "recipe"); 

    std::string recipeName = "";
    for (auto& recipe : recipes)
    {
        if (bot->HasSpell(ItemUsageValue::GetRecipeSpell(recipe->GetProto())))
            continue;

        recipeName = chat->formatItem(recipe);
        if (!urand(0, 10))
            break;
    }

    if (recipeName.empty())
        return false;

    if (bot->IsMoving())
    {
        ai->StopMoving();
    }

    Event rEvent = Event(name, recipeName);

    bool didUse = UseAction::Execute(rEvent);

    if (didUse && bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        ai->TellPlayerNoFacing(requester, "Learning " + recipeName, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    return didUse;
}

bool OpenRandomItemAction::isUseful()
{
    return !bot->IsInCombat() && !ai->HasActivePlayerMaster() && !bot->InBattleGround();
}

bool OpenRandomItemAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", "open");

    std::string itemName = "";
    for (auto& item : items)
    {
        if (!urand(0, 10))
            break;

        itemName = chat->formatItem(item);
    }

    if (itemName.empty())
        return false;

    if (bot->IsMoving())
    {
        ai->StopMoving();
    }

    Event rEvent = Event(name, itemName);

    bool didUse = UseAction::Execute(rEvent);

    if (didUse)
        ai->TellPlayerNoFacing(requester, "Opening " + itemName, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    return didUse;
}


bool UseRandomQuestItemAction::isUseful()
{
    if (ai->HasActivePlayerMaster() || bot->InBattleGround() || bot->IsTaxiFlying())
        return false;

    std::list<Item*> questItems = AI_VALUE2(std::list<Item*>, "inventory items", "quest");
    for (Item* item : questItems)
    {
        const ItemPrototype* proto = item ? item->GetProto() : nullptr;
        if (!proto)
            continue;

        if (IsQuestItemOwnedAndUsable(bot, item) && !HasQuestItemCooldown(bot, proto) && HasTargetedOnUseSpell(proto))
            return true;

        if (proto->StartQuest)
        {
            Quest const* qInfo = sObjectMgr.GetQuestTemplate(proto->StartQuest);
            if (qInfo && bot->CanTakeQuest(qInfo, false))
                return true;
        }
    }

    return false;
}

bool UseRandomQuestItemAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::vector<QuestItemUseCandidate> candidates = CollectQuestItemUseCandidates(ai, bot);

    bool success = false;
    if (!candidates.empty())
    {
        QuestItemUseCandidate& candidate = candidates.front();
        if (candidate.goTarget)
            success = UseItem(requester, candidate.item->GetEntry(), candidate.goTarget);
        else
            success = UseItem(requester, candidate.item->GetEntry(), candidate.unitTarget);

        context->GetValue<time_t>("manual time", candidate.retryKey)->Set(
            success ? time_t(0) : time(0) + QUEST_ITEM_RETRY_SECONDS);
    }
    else
    {
        std::list<Item*> questItems = AI_VALUE2(std::list<Item*>, "inventory items", "quest");
        for (Item* questItem : questItems)
        {
            const ItemPrototype* proto = questItem ? questItem->GetProto() : nullptr;
            if (!proto || !proto->StartQuest)
                continue;

            Quest const* qInfo = sObjectMgr.GetQuestTemplate(proto->StartQuest);
            if (qInfo && bot->CanTakeQuest(qInfo, false))
            {
                success = UseItem(requester, questItem->GetEntry());
                if (success)
                    break;
            }
        }
    }

    if (success)
    {
        SetDuration(sPlayerbotAIConfig.globalCoolDown);
    }

    return success;
}
