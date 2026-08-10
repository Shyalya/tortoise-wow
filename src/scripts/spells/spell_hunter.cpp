#include "scriptPCH.h"

namespace
{
enum HunterSpells
{
    SPELL_HUNTER_VOLLEY_R1                         = 1510,
    SPELL_HUNTER_SERPENT_STING_R1                  = 1978,
    SPELL_HUNTER_RAPTOR_STRIKE_R1                  = 2973,
    SPELL_HUNTER_STEADY_SHOT_R1                    = 3035,
    SPELL_HUNTER_STEADY_SHOT_R2                    = 3036,
    SPELL_HUNTER_STEADY_SHOT_R3                    = 3037,
    SPELL_HUNTER_STEADY_SHOT_R4                    = 3038,
    SPELL_HUNTER_ARCANE_SHOT_R1                    = 3044,
    SPELL_HUNTER_VOLLEY_R2                         = 14294,
    SPELL_HUNTER_VOLLEY_R3                         = 14295,
    SPELL_HUNTER_RAPTOR_STRIKE_R2                  = 14260,
    SPELL_HUNTER_RAPTOR_STRIKE_R3                  = 14261,
    SPELL_HUNTER_RAPTOR_STRIKE_R4                  = 14262,
    SPELL_HUNTER_RAPTOR_STRIKE_R5                  = 14263,
    SPELL_HUNTER_RAPTOR_STRIKE_R6                  = 14264,
    SPELL_HUNTER_RAPTOR_STRIKE_R7                  = 14265,
    SPELL_HUNTER_RAPTOR_STRIKE_R8                  = 14266,
    SPELL_HUNTER_ARCANE_SHOT_R2                    = 14281,
    SPELL_HUNTER_ARCANE_SHOT_R3                    = 14282,
    SPELL_HUNTER_ARCANE_SHOT_R4                    = 14283,
    SPELL_HUNTER_ARCANE_SHOT_R5                    = 14284,
    SPELL_HUNTER_ARCANE_SHOT_R6                    = 14285,
    SPELL_HUNTER_ARCANE_SHOT_R7                    = 14286,
    SPELL_HUNTER_ARCANE_SHOT_R8                    = 14287,
    SPELL_HUNTER_ASPECT_OF_THE_HAWK_R1             = 13165,
    SPELL_HUNTER_ASPECT_OF_THE_HAWK_R2             = 14318,
    SPELL_HUNTER_ASPECT_OF_THE_HAWK_R3             = 14319,
    SPELL_HUNTER_ASPECT_OF_THE_HAWK_R4             = 14320,
    SPELL_HUNTER_ASPECT_OF_THE_HAWK_R5             = 14321,
    SPELL_HUNTER_ASPECT_OF_THE_HAWK_R6             = 14322,
    SPELL_HUNTER_AIMED_SHOT_R1                     = 19434,
    SPELL_HUNTER_SWIFT_ASPECTS_R1                  = 19552,
    SPELL_HUNTER_SWIFT_ASPECTS_R2                  = 19553,
    SPELL_HUNTER_SWIFT_ASPECTS_R3                  = 19554,
    SPELL_HUNTER_SWIFT_ASPECTS_R4                  = 19555,
    SPELL_HUNTER_SWIFT_ASPECTS_R5                  = 19556,
    SPELL_HUNTER_EYES_OF_THE_BEAST_TALENT_R1       = 19557,
    SPELL_HUNTER_EYES_OF_THE_BEAST_TALENT_R2       = 19558,
    SPELL_HUNTER_AIMED_SHOT_R2                     = 20900,
    SPELL_HUNTER_AIMED_SHOT_R3                     = 20901,
    SPELL_HUNTER_AIMED_SHOT_R4                     = 20902,
    SPELL_HUNTER_AIMED_SHOT_R5                     = 20903,
    SPELL_HUNTER_AIMED_SHOT_R6                     = 20904,
    SPELL_HUNTER_IMPROVED_MEND_PET_DISPEL          = 24406,
    SPELL_HUNTER_ASPECT_OF_THE_HAWK_R7             = 25296,
    SPELL_HUNTER_AIMED_SHOT_R6_TURTLE              = 27632,
    SPELL_HUNTER_STEADY_SHOT_R5                    = 3668,
    SPELL_HUNTER_KILL_COMMAND_DAMAGE               = 41828,
    SPELL_HUNTER_ASPECT_OF_THE_WOLF_R1             = 45650,
    SPELL_HUNTER_EYES_OF_THE_BEAST_BOND_R1         = 45662,
    SPELL_HUNTER_EYES_OF_THE_BEAST_BOND_R2         = 45663,
    SPELL_HUNTER_STEADY_SHOT_R6                    = 45970,
    SPELL_HUNTER_STEADY_SHOT_R7                    = 45972,
    SPELL_HUNTER_STEADY_SHOT_R8                    = 45974,
    SPELL_HUNTER_ASPECT_OF_THE_WOLF_R2             = 51496,
    SPELL_HUNTER_ASPECT_OF_THE_WOLF_R3             = 51497,
    SPELL_HUNTER_ASPECT_OF_THE_WOLF_R4             = 51498,
    SPELL_HUNTER_ASPECT_OF_THE_WOLF_R5             = 51499,
    SPELL_HUNTER_ASPECT_OF_THE_WOLF_R6             = 51500,
    SPELL_HUNTER_ASPECT_OF_THE_WOLF_R7             = 51501,
    SPELL_HUNTER_QUICK_STRIKES_R1                  = 51542,
    SPELL_HUNTER_QUICK_STRIKES_R2                  = 51543,
    SPELL_HUNTER_QUICK_STRIKES_R3                  = 51544,
    SPELL_HUNTER_QUICK_STRIKES_R4                  = 51545,
    SPELL_HUNTER_QUICK_STRIKES_R5                  = 51546,
    SPELL_HUNTER_QUICK_SHOTS_R1                    = 51547,
    SPELL_HUNTER_QUICK_SHOTS_R2                    = 51548,
    SPELL_HUNTER_QUICK_SHOTS_R3                    = 51549,
    SPELL_HUNTER_QUICK_SHOTS_R4                    = 51550,
    SPELL_HUNTER_QUICK_SHOTS_R5                    = 51551,
    SPELL_HUNTER_STINGING_NETTLE_R1                = 51579,
    SPELL_HUNTER_STINGING_NETTLE_R2                = 51580,
    SPELL_HUNTER_PIERCING_SHOTS                    = 51514,
    SPELL_HUNTER_SCENT_OF_BLOOD                    = 52995,
    SPELL_HUNTER_LOCK_AND_LOAD_AURA                = 52921,
    SPELL_HUNTER_EXPLOSIVE_AMMUNITION              = 58109,
    SPELL_HUNTER_POISONOUS_AMMUNITION              = 58110,
    SPELL_HUNTER_ENCHANTED_AMMUNITION              = 58111,
    SPELL_HUNTER_EXPLOSIVE_AMMUNITION_TRIGGER      = 58112,
    SPELL_HUNTER_POISONOUS_AMMUNITION_TRIGGER      = 58113,
    SPELL_HUNTER_ENCHANTED_AMMUNITION_TRIGGER      = 58114,
};

template <class T>
SpellScript* GetSpellScript(SpellEntry const*)
{
    return new T();
}

template <class T>
AuraScript* GetAuraScript(SpellEntry const*)
{
    return new T();
}

void RegisterSpellScript(char const* name, SpellScript* (*getter)(SpellEntry const*))
{
    Script* script = new Script;
    script->Name = name;
    script->GetSpellScript = getter;
    script->RegisterSelf();
}

void RegisterAuraScript(char const* name, AuraScript* (*getter)(SpellEntry const*))
{
    Script* script = new Script;
    script->Name = name;
    script->GetAuraScript = getter;
    script->RegisterSelf();
}

bool HasAspectOfTheWolf(Unit const* unit)
{
    return unit && (unit->HasAura(SPELL_HUNTER_ASPECT_OF_THE_WOLF_R1) ||
        unit->HasAura(SPELL_HUNTER_ASPECT_OF_THE_WOLF_R2) ||
        unit->HasAura(SPELL_HUNTER_ASPECT_OF_THE_WOLF_R3) ||
        unit->HasAura(SPELL_HUNTER_ASPECT_OF_THE_WOLF_R4) ||
        unit->HasAura(SPELL_HUNTER_ASPECT_OF_THE_WOLF_R5) ||
        unit->HasAura(SPELL_HUNTER_ASPECT_OF_THE_WOLF_R6) ||
        unit->HasAura(SPELL_HUNTER_ASPECT_OF_THE_WOLF_R7));
}

static constexpr float HUNTER_LOCK_AND_LOAD_LINE_WIDTH = 2.0f;

struct HunterLineShotContext
{
    ObjectGuid casterGuid;
    ObjectGuid targetGuid;
    uint32 spellId;
};

thread_local std::vector<HunterLineShotContext> s_hunterLockAndLoadLineShots;

bool IsAimedShot(SpellEntry const* spell)
{
    return spell && spell->IsFitToFamily<SPELLFAMILY_HUNTER, CF_HUNTER_AIMED_SHOT>();
}

bool IsSameHunterLineShot(HunterLineShotContext const& context, ObjectGuid const& casterGuid, ObjectGuid const& targetGuid, uint32 spellId)
{
    return context.casterGuid == casterGuid &&
        context.targetGuid == targetGuid &&
        context.spellId == spellId;
}

bool IsLockAndLoadLineShot(Unit const* caster, Unit const* target, SpellEntry const* spellInfo)
{
    if (!caster || !target || !spellInfo)
        return false;

    ObjectGuid const casterGuid = caster->GetObjectGuid();
    ObjectGuid const targetGuid = target->GetObjectGuid();
    uint32 const spellId = spellInfo->Id;
    return std::any_of(s_hunterLockAndLoadLineShots.begin(), s_hunterLockAndLoadLineShots.end(),
        [casterGuid, targetGuid, spellId](HunterLineShotContext const& context)
        {
            return IsSameHunterLineShot(context, casterGuid, targetGuid, spellId);
        });
}

void PopLockAndLoadLineShot(Unit const* caster, Unit const* target, SpellEntry const* spellInfo)
{
    if (!caster || !target || !spellInfo)
        return;

    ObjectGuid const casterGuid = caster->GetObjectGuid();
    ObjectGuid const targetGuid = target->GetObjectGuid();
    uint32 const spellId = spellInfo->Id;
    auto itr = std::find_if(s_hunterLockAndLoadLineShots.rbegin(), s_hunterLockAndLoadLineShots.rend(),
        [casterGuid, targetGuid, spellId](HunterLineShotContext const& context)
        {
            return IsSameHunterLineShot(context, casterGuid, targetGuid, spellId);
        });

    if (itr != s_hunterLockAndLoadLineShots.rend())
        s_hunterLockAndLoadLineShots.erase(std::next(itr).base());
}

void ApplyStingingNettle(Spell* spell);

struct spell_hunter_wyvern_sting : public SpellScript
{
    void OnSetTargetMap(Spell* /*spell*/, SpellEffectIndex /*effIdx*/, uint32& /*targetMode*/, float& /*radius*/, uint32& /*unMaxTargets*/, bool& selectClosestTargets) const override
    {
        selectClosestTargets = true;
    }
};

struct spell_hunter_refocus : public SpellScript
{
    bool OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        Player* player = spell->m_caster->ToPlayer();
        if (!player)
            return false;

        SpellCooldowns cooldowns = player->GetSpellCooldownMap();
        for (const auto& cooldown : cooldowns)
        {
            SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(cooldown.first);
            if (spellInfo && spellInfo->IsFitToFamily<SPELLFAMILY_HUNTER, CF_HUNTER_ARCANE_SHOT, CF_HUNTER_MULTI_SHOT, CF_HUNTER_VOLLEY, CF_HUNTER_AIMED_SHOT>() &&
                    spellInfo->Id != spell->m_spellInfo->Id && spellInfo->GetRecoveryTime() > 0)
                player->RemoveSpellCooldown(cooldown.first, true);
        }

        return false;
    }
};

struct spell_hunter_readiness : public SpellScript
{
    bool OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_0)
            return true;

        Player* player = spell->m_caster->ToPlayer();
        if (!player)
            return false;

        SpellCooldowns cooldowns = player->GetSpellCooldownMap();
        for (auto itr = cooldowns.begin(); itr != cooldowns.end();)
        {
            SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(itr->first);
            if (spellInfo && spellInfo->SpellFamilyName == SPELLFAMILY_HUNTER &&
                    spellInfo->Id != spell->m_spellInfo->Id && spellInfo->GetRecoveryTime() > 0)
                player->RemoveSpellCooldown((itr++)->first, true);
            else
                ++itr;
        }

        return false;
    }
};

// Deprecated in patch 1.17.2
struct spell_hunter_counterattack : public SpellScript
{
    SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
    {
        if (!spell->m_casterUnit || spell->m_targets.getUnitTargetGuid() == spell->m_casterUnit->GetReactiveTarget(REACTIVE_HUNTER_PARRY))
            return SPELL_CAST_OK;

        return SPELL_FAILED_BAD_TARGETS;
    }
};

struct spell_hunter_mongoose_bite : public SpellScript
{
    void OnAfterHit(Spell* spell) const override
    {
        ApplyStingingNettle(spell);
    }

    void OnEffectDamageCalculate(Spell* spell, SpellEffectIndex /*effIdx*/, float& damage) const override
    {
        Unit* target = spell->GetUnitTarget();
        if (!spell->m_casterUnit || !target || damage <= 0.0f)
            return;

        float weaponDamagePercentMod = 1.0f;
        bool normalized = false;
        for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
        {
            switch (spell->m_spellInfo->Effect[j])
            {
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                    normalized = true;
                    break;
                case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                    weaponDamagePercentMod *= float(spell->CalculateDamage(SpellEffectIndex(j), target)) / 100.0f;
                    break;
                default:
                    break;
            }
        }

        if (weaponDamagePercentMod <= 0.0f || !spell->m_casterUnit->HaveOffhandWeapon() || spell->m_casterUnit->GetWeaponDamageCount(OFF_ATTACK) == 0)
            return;

        for (uint8 i = 0; i < spell->m_casterUnit->GetWeaponDamageCount(OFF_ATTACK); ++i)
        {
            if (target->IsImmuneToDamage(GetSchoolMask(spell->m_casterUnit->GetWeaponDamageSchool(OFF_ATTACK, i))))
                continue;

            damage += int32(spell->m_casterUnit->CalculateDamage(OFF_ATTACK, normalized, i) * weaponDamagePercentMod);
        }
    }
};

struct spell_hunter_stinging_nettle_fire_trap : public SpellScript
{
    void OnAfterHit(Spell* spell) const override
    {
        ApplyStingingNettle(spell);
    }
};

struct spell_hunter_trap : public SpellScript
{
    bool OnCanCastNonCombatSpellInCombat(Spell* spell) const override
    {
        return spell->m_casterUnit && spell->m_casterUnit->HasAura(51586);
    }
};

struct spell_hunter_lacerate : public SpellScript
{
    static bool IsAttackingFromSide(Unit const* caster, Unit const* target)
    {
        return caster && target &&
            !target->HasInArc(caster, M_PI_F / 2.0f) &&
            !target->HasInArc(caster, M_PI_F / 2.0f, M_PI_F);
    }

    void OnEffectDamageCalculate(Spell* spell, SpellEffectIndex effIdx, float& damage) const override
    {
        if (effIdx != EFFECT_INDEX_0 || !spell->m_casterUnit)
            return;

        Unit* target = spell->GetUnitTarget();
        if (!target)
            return;

        damage = spell->m_casterUnit->GetTotalAttackPowerValue(BASE_ATTACK) * 0.40f;
        if (IsAttackingFromSide(spell->m_casterUnit, target))
            damage *= 1.15f;
    }

    void OnAfterHit(Spell* spell) const override
    {
        if (!spell->m_casterUnit)
            return;

        Unit* target = spell->GetUnitTarget();
        if (!target || !target->IsAlive())
            return;

        int32 const damage = int32(spell->GetTotalEffectDamage());
        if (damage <= 0)
            return;

        SpellEntry const* bleedInfo = sSpellMgr.GetSpellEntry(48050);
        if (!bleedInfo)
            return;

        uint32 tickCount = 1;
        int32 const duration = bleedInfo->CalculateDuration(spell->m_casterUnit, target);
        if (duration > 0 && bleedInfo->EffectAmplitude[EFFECT_INDEX_0])
            tickCount = std::max<uint32>(1, uint32(duration) / bleedInfo->EffectAmplitude[EFFECT_INDEX_0]);

        int32 const tickDamage = damage * 20 / 100 / int32(tickCount);
        if (tickDamage <= 0)
            return;

        spell->m_casterUnit->CastCustomSpell(target, 48050, &tickDamage, nullptr, nullptr, true);
    }
};

struct spell_hunter_bestial_wrath : public SpellScript
{
    void OnEffectExecuted(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_0 || !spell || !spell->m_casterUnit)
            return;

        Unit* pet = spell->m_casterUnit->GetPet();
        if (!pet)
            return;

        static constexpr int32 BESTIAL_WRATH_DURATION = 18 * IN_MILLISECONDS;

        SpellAuraHolder* holder = pet->GetSpellAuraHolder(SPELL_HUNTER_SCENT_OF_BLOOD, spell->m_casterUnit->GetObjectGuid());
        if (!holder)
            holder = pet->GetSpellAuraHolder(SPELL_HUNTER_SCENT_OF_BLOOD);
        if (!holder)
            return;

        holder->SetAuraMaxDuration(BESTIAL_WRATH_DURATION);
        holder->SetAuraDuration(BESTIAL_WRATH_DURATION);
    }
};

struct spell_hunter_kill_command : public SpellScript
{
    static Unit* GetKillCommandTarget(Spell* spell, Player* hunter, Pet* pet)
    {
        if (Unit* target = spell->m_targets.getUnitTarget())
            if (target != pet && target->IsAlive() && hunter->IsValidAttackTarget(target))
                return target;

        if (Unit* target = hunter->GetSelectedUnit())
            if (target->IsAlive() && hunter->IsValidAttackTarget(target))
                return target;

        if (Unit* target = pet->GetVictim())
            if (target->IsAlive() && hunter->IsValidAttackTarget(target))
                return target;

        return nullptr;
    }

    SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
    {
        Player* hunter = spell && spell->m_casterUnit ? spell->m_casterUnit->ToPlayer() : nullptr;
        if (!hunter)
            return SPELL_FAILED_BAD_TARGETS;

        Pet* pet = hunter->GetPet();
        if (!pet || !pet->IsAlive())
            return SPELL_FAILED_NO_PET;

        if (!GetKillCommandTarget(spell, hunter, pet))
            return SPELL_FAILED_BAD_TARGETS;

        return SPELL_CAST_OK;
    }

    bool OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_0 || !spell || !spell->m_casterUnit)
            return false;

        Player* hunter = spell->m_casterUnit->ToPlayer();
        Pet* pet = hunter ? hunter->GetPet() : nullptr;
        if (!hunter || !pet || !pet->IsAlive())
            return false;

        Unit* target = GetKillCommandTarget(spell, hunter, pet);
        if (!target)
            return false;

        pet->CastSpell(target, SPELL_HUNTER_KILL_COMMAND_DAMAGE, true);
        return false;
    }
};

struct spell_hunter_kill_command_damage : public SpellScript
{
    void OnEffectDamageCalculate(Spell* spell, SpellEffectIndex /*effIdx*/, float& damage) const override
    {
        if (!spell || !spell->m_casterUnit)
            return;

        damage = spell->m_casterUnit->GetTotalAttackPowerValue(BASE_ATTACK) * 0.80f;
    }
};

struct spell_hunter_volley : public AuraScript
{
    void OnPeriodicDamageCalculateAmount(Aura* aura, float& amount) override
    {
        Unit* caster = aura->GetCaster();
        if (!caster)
            return;

        float coefficient = 0.0f;
        switch (aura->GetId())
        {
            case SPELL_HUNTER_VOLLEY_R1: coefficient = 0.04f; break;
            case SPELL_HUNTER_VOLLEY_R2: coefficient = 0.05f; break;
            case SPELL_HUNTER_VOLLEY_R3: coefficient = 0.06f; break;
            default:
                return;
        }

        amount += caster->GetTotalAttackPowerValue(RANGED_ATTACK) * coefficient;
    }
};

struct spell_hunter_experimental_ammunition : public AuraScript
{
    uint32 m_nextState = SPELL_HUNTER_EXPLOSIVE_AMMUNITION;

    std::optional<SpellProcEventTriggerCheck> OnCheckProc(Unit const* owner, Unit* victim, SpellAuraHolder* /*holder*/, SpellEntry const* procSpell, uint32 /*procFlag*/, uint32 procExtra, WeaponAttackType /*attType*/, bool isVictim) override
    {
        if (!owner || isVictim || !victim || !victim->IsAlive())
            return SPELL_PROC_TRIGGER_FAILED;

        if (!IsAimedShot(procSpell))
            return SPELL_PROC_TRIGGER_FAILED;

        return (procExtra & (PROC_EX_NORMAL_HIT | PROC_EX_CRITICAL_HIT)) ? SPELL_PROC_TRIGGER_OK : SPELL_PROC_TRIGGER_FAILED;
    }

    std::optional<SpellAuraProcResult> OnProc(Unit* owner, Unit* victim, uint32 damage, int32 /*originalAmount*/, Aura* aura, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/) override
    {
        if (!owner || !victim || !victim->IsAlive() || !damage)
            return SPELL_AURA_PROC_FAILED;

        uint32 nextState = m_nextState;
        if (owner->HasAura(SPELL_HUNTER_EXPLOSIVE_AMMUNITION))
            nextState = SPELL_HUNTER_POISONOUS_AMMUNITION;
        else if (owner->HasAura(SPELL_HUNTER_POISONOUS_AMMUNITION))
            nextState = SPELL_HUNTER_ENCHANTED_AMMUNITION;
        else if (owner->HasAura(SPELL_HUNTER_ENCHANTED_AMMUNITION))
            nextState = SPELL_HUNTER_EXPLOSIVE_AMMUNITION;

        int32 elementalDamage = damage * std::max(0, aura->GetModifier()->m_amount + 1) / 100;
        if (!elementalDamage)
            return SPELL_AURA_PROC_FAILED;

        owner->RemoveAurasDueToSpell(SPELL_HUNTER_EXPLOSIVE_AMMUNITION);
        owner->RemoveAurasDueToSpell(SPELL_HUNTER_POISONOUS_AMMUNITION);
        owner->RemoveAurasDueToSpell(SPELL_HUNTER_ENCHANTED_AMMUNITION);
        owner->CastCustomSpell(victim, nextState, &elementalDamage, nullptr, nullptr, true, nullptr, aura);
        switch (nextState)
        {
            case SPELL_HUNTER_EXPLOSIVE_AMMUNITION:
                m_nextState = SPELL_HUNTER_POISONOUS_AMMUNITION;
                break;
            case SPELL_HUNTER_POISONOUS_AMMUNITION:
                m_nextState = SPELL_HUNTER_ENCHANTED_AMMUNITION;
                break;
            case SPELL_HUNTER_ENCHANTED_AMMUNITION:
            default:
                m_nextState = SPELL_HUNTER_EXPLOSIVE_AMMUNITION;
                break;
        }
        return SPELL_AURA_PROC_OK;
    }
};

struct spell_hunter_experimental_ammunition_trigger : public SpellScript
{
    void OnSuccessfulFinish(Spell* spell) const override
    {
        if (!spell || !spell->m_casterUnit)
            return;

        uint32 stateSpellId = 0;
        switch (spell->m_spellInfo->Id)
        {
            case SPELL_HUNTER_EXPLOSIVE_AMMUNITION_TRIGGER:
                stateSpellId = SPELL_HUNTER_EXPLOSIVE_AMMUNITION;
                break;
            case SPELL_HUNTER_POISONOUS_AMMUNITION_TRIGGER:
                stateSpellId = SPELL_HUNTER_POISONOUS_AMMUNITION;
                break;
            case SPELL_HUNTER_ENCHANTED_AMMUNITION_TRIGGER:
                stateSpellId = SPELL_HUNTER_ENCHANTED_AMMUNITION;
                break;
            default:
                break;
        }
        if (!stateSpellId)
            return;

        Unit* caster = spell->m_casterUnit;
        caster->m_Events.AddLambdaEventAtOffset([caster, stateSpellId]()
        {
            caster->RemoveAurasDueToSpell(stateSpellId);
        }, 1);
    }
};

struct spell_hunter_lock_and_load : public AuraScript
{
    std::optional<SpellProcEventTriggerCheck> OnCheckProc(Unit const* owner, Unit* victim, SpellAuraHolder* /*holder*/, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procExtra*/, WeaponAttackType /*attType*/, bool isVictim) override
    {
        if (!owner || isVictim || !victim || !victim->IsAlive())
            return SPELL_PROC_TRIGGER_FAILED;

        return std::nullopt;
    }

    std::optional<SpellAuraProcResult> OnProc(Unit* owner, Unit* /*victim*/, uint32 /*damage*/, int32 /*originalAmount*/, Aura* aura, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/) override
    {
        if (!owner || !aura)
            return SPELL_AURA_PROC_FAILED;

        owner->RemoveSpellCooldown(SPELL_HUNTER_AIMED_SHOT_R1, true);
        owner->RemoveSpellCooldown(SPELL_HUNTER_AIMED_SHOT_R2, true);
        owner->RemoveSpellCooldown(SPELL_HUNTER_AIMED_SHOT_R3, true);
        owner->RemoveSpellCooldown(SPELL_HUNTER_AIMED_SHOT_R4, true);
        owner->RemoveSpellCooldown(SPELL_HUNTER_AIMED_SHOT_R5, true);
        owner->RemoveSpellCooldown(SPELL_HUNTER_AIMED_SHOT_R6, true);
        owner->RemoveSpellCooldown(SPELL_HUNTER_AIMED_SHOT_R6_TURTLE, true);
        owner->CastSpell(owner, SPELL_HUNTER_LOCK_AND_LOAD_AURA, true, nullptr, aura);
        return SPELL_AURA_PROC_OK;
    }
};

struct spell_hunter_aimed_shot : public SpellScript
{
    mutable std::vector<ObjectGuid> m_lockAndLoadLineTargets;
    mutable bool m_isLockAndLoadLineShot = false;

    std::optional<uint32> OnCalculatePowerCost(SpellEntry const* spellInfo, Unit* caster, Spell* spell, Item* /*castItem*/) const override
    {
        if (m_isLockAndLoadLineShot)
            return 0;

        Unit* target = spell ? spell->m_targets.getUnitTarget() : nullptr;
        if (IsLockAndLoadLineShot(caster, target, spellInfo))
            return 0;

        return std::nullopt;
    }

    void OnSuccessfulStart(Spell* spell) const override
    {
        m_lockAndLoadLineTargets.clear();
        m_isLockAndLoadLineShot = false;

        if (!spell->m_casterUnit || !IsAimedShot(spell->m_spellInfo))
            return;

        if (spell->IsTriggered())
        {
            Unit* target = spell->m_targets.getUnitTarget();
            if (IsLockAndLoadLineShot(spell->m_casterUnit, target, spell->m_spellInfo))
            {
                m_isLockAndLoadLineShot = true;
                PopLockAndLoadLineShot(spell->m_casterUnit, target, spell->m_spellInfo);
            }

            return;
        }

        if (!spell->m_casterUnit->HasAura(SPELL_HUNTER_LOCK_AND_LOAD_AURA))
            return;

        Unit* primaryTarget = spell->m_targets.getUnitTarget();
        if (!primaryTarget || !primaryTarget->IsAlive())
            return;

        float const searchRange = spell->m_casterUnit->GetDistance(primaryTarget) + HUNTER_LOCK_AND_LOAD_LINE_WIDTH;
        std::list<Unit*> targets;
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(spell->m_casterUnit, spell->m_casterUnit, searchRange);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(targets, check);
        Cell::VisitAllObjects(spell->m_casterUnit, searcher, searchRange);

        for (Unit* target : targets)
        {
            if (!target || target == primaryTarget || target == spell->m_casterUnit)
                continue;

            if (!spell->m_casterUnit->IsValidAttackTarget(target) || !spell->m_casterUnit->IsWithinLOSInMap(target))
                continue;

            float const casterX = spell->m_casterUnit->GetPositionX();
            float const casterY = spell->m_casterUnit->GetPositionY();
            float const targetX = primaryTarget->GetPositionX();
            float const targetY = primaryTarget->GetPositionY();
            float const unitX = target->GetPositionX();
            float const unitY = target->GetPositionY();

            float const lineX = targetX - casterX;
            float const lineY = targetY - casterY;
            float const lineLengthSq = lineX * lineX + lineY * lineY;
            if (lineLengthSq <= 0.0f)
                continue;

            float const unitLineX = unitX - casterX;
            float const unitLineY = unitY - casterY;
            float const projection = (unitLineX * lineX + unitLineY * lineY) / lineLengthSq;
            if (projection <= 0.0f || projection >= 1.0f)
                continue;

            float const closestX = casterX + projection * lineX;
            float const closestY = casterY + projection * lineY;
            float const distanceX = unitX - closestX;
            float const distanceY = unitY - closestY;
            if (distanceX * distanceX + distanceY * distanceY > HUNTER_LOCK_AND_LOAD_LINE_WIDTH * HUNTER_LOCK_AND_LOAD_LINE_WIDTH)
                continue;

            m_lockAndLoadLineTargets.push_back(target->GetObjectGuid());
        }
    }

    void OnCast(Spell* spell) const override
    {
        if (m_lockAndLoadLineTargets.empty() || !spell->m_casterUnit || !IsAimedShot(spell->m_spellInfo))
            return;

        std::vector<ObjectGuid> const lineTargets = m_lockAndLoadLineTargets;
        m_lockAndLoadLineTargets.clear();

        for (ObjectGuid const& targetGuid : lineTargets)
        {
            Unit* target = ObjectAccessor::GetUnit(*spell->m_casterUnit, targetGuid);
            if (!target || !target->IsAlive() || !spell->m_casterUnit->IsValidAttackTarget(target))
                continue;

            s_hunterLockAndLoadLineShots.push_back({spell->m_casterUnit->GetObjectGuid(), target->GetObjectGuid(), spell->m_spellInfo->Id});
            spell->m_casterUnit->CastSpell(target, spell->m_spellInfo, true);
            PopLockAndLoadLineShot(spell->m_casterUnit, target, spell->m_spellInfo);
        }
    }

    bool OnTakeAmmo(Spell* /*spell*/) const override
    {
        return !m_isLockAndLoadLineShot;
    }
};

struct spell_hunter_piercing_shots : public AuraScript
{
    std::optional<SpellProcEventTriggerCheck> OnCheckProc(Unit const* owner, Unit* victim, SpellAuraHolder* /*holder*/, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procExtra*/, WeaponAttackType /*attType*/, bool isVictim) override
    {
        if (!owner || isVictim || !victim || !victim->IsAlive())
            return SPELL_PROC_TRIGGER_FAILED;

        return std::nullopt;
    }

    std::optional<SpellAuraProcResult> OnProc(Unit* owner, Unit* victim, uint32 damage, int32 /*originalAmount*/, Aura* aura, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/) override
    {
        if (!owner || !victim || !victim->IsAlive() || !damage || !aura)
            return SPELL_AURA_PROC_FAILED;

        int32 const percent = std::max(0, aura->GetModifier()->m_amount);
        if (!percent)
            return SPELL_AURA_PROC_FAILED;

        uint32 tickCount = 1;
        if (SpellEntry const* piercingShots = sSpellMgr.GetSpellEntry(SPELL_HUNTER_PIERCING_SHOTS))
        {
            int32 const duration = piercingShots->CalculateDuration(owner, victim);
            if (duration > 0 && piercingShots->EffectAmplitude[EFFECT_INDEX_0])
                tickCount = std::max<uint32>(1, uint32(duration) / piercingShots->EffectAmplitude[EFFECT_INDEX_0]);
        }

        int32 const totalDamage = int32(damage * percent / 100);
        int32 const tickDamage = totalDamage / int32(tickCount);
        if (tickDamage <= 0)
            return SPELL_AURA_PROC_FAILED;

        owner->CastCustomSpell(victim, SPELL_HUNTER_PIERCING_SHOTS, &tickDamage, nullptr, nullptr, true, nullptr, aura);
        return SPELL_AURA_PROC_OK;
    }
};

struct spell_hunter_improved_mend_pet : public AuraScript
{
    std::optional<SpellAuraProcResult> OnProc(Unit* owner, Unit* victim, uint32 /*damage*/, int32 /*originalAmount*/, Aura* aura, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/) override
    {
        if (!victim || !victim->IsAlive())
            return SPELL_AURA_PROC_FAILED;

        if (!roll_chance_i(aura->GetModifier()->m_amount))
            return SPELL_AURA_PROC_FAILED;

        owner->CastSpell(victim, SPELL_HUNTER_IMPROVED_MEND_PET_DISPEL, true, nullptr, aura);
        return SPELL_AURA_PROC_OK;
    }
};

struct spell_hunter_swift_aspects : public AuraScript
{
    std::optional<SpellProcEventTriggerCheck> OnCheckProc(Unit const* owner, Unit* /*victim*/, SpellAuraHolder* /*holder*/, SpellEntry const* /*procSpell*/, uint32 procFlag, uint32 /*procExtra*/, WeaponAttackType attType, bool isVictim) override
    {
        if (!owner || isVictim)
            return SPELL_PROC_TRIGGER_FAILED;

        bool const hasHawkAspect = owner->HasAura(SPELL_HUNTER_ASPECT_OF_THE_HAWK_R1) ||
            owner->HasAura(SPELL_HUNTER_ASPECT_OF_THE_HAWK_R2) ||
            owner->HasAura(SPELL_HUNTER_ASPECT_OF_THE_HAWK_R3) ||
            owner->HasAura(SPELL_HUNTER_ASPECT_OF_THE_HAWK_R4) ||
            owner->HasAura(SPELL_HUNTER_ASPECT_OF_THE_HAWK_R5) ||
            owner->HasAura(SPELL_HUNTER_ASPECT_OF_THE_HAWK_R6) ||
            owner->HasAura(SPELL_HUNTER_ASPECT_OF_THE_HAWK_R7);

        if ((procFlag & PROC_FLAG_DEAL_RANGED_ATTACK) && attType == RANGED_ATTACK && hasHawkAspect)
            return std::nullopt;

        if ((procFlag & PROC_FLAG_DEAL_MELEE_SWING) && (attType == BASE_ATTACK || attType == OFF_ATTACK) && HasAspectOfTheWolf(owner))
            return std::nullopt;

        return SPELL_PROC_TRIGGER_FAILED;
    }

    std::optional<SpellAuraProcResult> OnProc(Unit* owner, Unit* /*victim*/, uint32 /*damage*/, int32 /*originalAmount*/, Aura* aura, SpellEntry const* /*procSpell*/, uint32 procFlag, uint32 /*procEx*/, uint32 /*cooldown*/) override
    {
        if (!owner || !aura)
            return SPELL_AURA_PROC_FAILED;

        uint32 rank = 0;
        switch (aura->GetId())
        {
            case SPELL_HUNTER_SWIFT_ASPECTS_R1: rank = 0; break;
            case SPELL_HUNTER_SWIFT_ASPECTS_R2: rank = 1; break;
            case SPELL_HUNTER_SWIFT_ASPECTS_R3: rank = 2; break;
            case SPELL_HUNTER_SWIFT_ASPECTS_R4: rank = 3; break;
            case SPELL_HUNTER_SWIFT_ASPECTS_R5: rank = 4; break;
            default:
                return SPELL_AURA_PROC_FAILED;
        }

        static constexpr uint32 quickShots[] =
        {
            SPELL_HUNTER_QUICK_SHOTS_R1,
            SPELL_HUNTER_QUICK_SHOTS_R2,
            SPELL_HUNTER_QUICK_SHOTS_R3,
            SPELL_HUNTER_QUICK_SHOTS_R4,
            SPELL_HUNTER_QUICK_SHOTS_R5,
        };
        static constexpr uint32 quickStrikes[] =
        {
            SPELL_HUNTER_QUICK_STRIKES_R1,
            SPELL_HUNTER_QUICK_STRIKES_R2,
            SPELL_HUNTER_QUICK_STRIKES_R3,
            SPELL_HUNTER_QUICK_STRIKES_R4,
            SPELL_HUNTER_QUICK_STRIKES_R5,
        };

        if (procFlag & PROC_FLAG_DEAL_RANGED_ATTACK)
        {
            owner->CastSpell(owner, quickShots[rank], true, nullptr, aura);
            return SPELL_AURA_PROC_OK;
        }

        if (procFlag & PROC_FLAG_DEAL_MELEE_SWING)
        {
            owner->CastSpell(owner, quickStrikes[rank], true, nullptr, aura);
            return SPELL_AURA_PROC_OK;
        }

        return SPELL_AURA_PROC_FAILED;
    }
};

struct spell_hunter_improved_primal_aspects : public AuraScript
{
    std::optional<SpellProcEventTriggerCheck> OnCheckProc(Unit const* owner, Unit* /*victim*/, SpellAuraHolder* /*holder*/, SpellEntry const* /*procSpell*/, uint32 procFlag, uint32 /*procExtra*/, WeaponAttackType /*attType*/, bool isVictim) override
    {
        if (!owner || isVictim)
            return SPELL_PROC_TRIGGER_FAILED;

        if (!(procFlag & (PROC_FLAG_DEAL_MELEE_SWING | PROC_FLAG_DEAL_MELEE_ABILITY)))
            return SPELL_PROC_TRIGGER_FAILED;

        if (!HasAspectOfTheWolf(owner))
            return SPELL_PROC_TRIGGER_FAILED;

        return std::nullopt;
    }

    std::optional<SpellAuraProcResult> OnProc(Unit* owner, Unit* /*victim*/, uint32 damage, int32 /*originalAmount*/, Aura* aura, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/) override
    {
        if (!owner || !aura || !damage)
            return SPELL_AURA_PROC_FAILED;

        if (aura->GetEffIndex() != EFFECT_INDEX_1 || aura->GetModifier()->m_auraname != SPELL_AURA_DUMMY)
            return SPELL_AURA_PROC_CANT_TRIGGER;

        uint32 const heal = damage * uint32(aura->GetModifier()->m_amount) / 100;
        if (!heal)
            return SPELL_AURA_PROC_FAILED;

        owner->DealHeal(owner, heal, aura->GetSpellProto());
        return SPELL_AURA_PROC_OK;
    }
};

struct spell_hunter_alone_against_the_world : public AuraScript
{
    void OnAuraInit(Aura* aura) override
    {
        if (aura && aura->GetEffIndex() == EFFECT_INDEX_1 && aura->GetModifier()->periodictime)
            aura->SetPeriodicTimer(aura->GetModifier()->periodictime);
    }

    static void SyncDamageAura(Aura* aura)
    {
        if (!aura)
            return;

        Player* player = aura->GetTarget()->ToPlayer();
        SpellAuraHolder* holder = aura->GetHolder();
        Aura* damageAura = holder ? holder->GetAuraByEffectIndex(EFFECT_INDEX_0) : nullptr;
        if (!player || !damageAura)
            return;

        bool const shouldApplyDamage = !player->GetPet();
        if (damageAura->IsApplied() != shouldApplyDamage)
            damageAura->ApplyModifier(shouldApplyDamage, true);
    }

    void OnAfterApply(Aura* aura, bool apply) override
    {
        if (apply)
            SyncDamageAura(aura);
    }

    void OnPeriodicDummy(Aura* aura) override
    {
        if (!aura || aura->GetEffIndex() != EFFECT_INDEX_1)
            return;

        SyncDamageAura(aura);
    }
};

struct spell_hunter_strike_together_damage : public SpellScript
{
    void OnEffectDamageCalculate(Spell* spell, SpellEffectIndex /*effIdx*/, float& damage) const override
    {
        if (!spell || !spell->m_casterUnit)
            return;

        Player* hunter = ToPlayer(spell->GetAffectiveCaster());
        if (!hunter)
            hunter = spell->m_casterUnit->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!hunter || hunter->GetClass() != CLASS_HUNTER)
            return;

        damage = hunter->GetTotalAttackPowerValue(BASE_ATTACK) * 0.20f;
    }
};

struct spell_hunter_coordinated_assault : public AuraScript
{
    std::optional<SpellProcEventTriggerCheck> OnCheckProc(Unit const* owner, Unit* victim, SpellAuraHolder* /*holder*/, SpellEntry const* procSpell, uint32 /*procFlag*/, uint32 /*procExtra*/, WeaponAttackType /*attType*/, bool isVictim) override
    {
        if (!owner || isVictim || !victim || !victim->IsAlive())
            return SPELL_PROC_TRIGGER_FAILED;

        if (!procSpell || procSpell->SpellFamilyName != SPELLFAMILY_HUNTER)
            return SPELL_PROC_TRIGGER_FAILED;

        switch (procSpell->Id)
        {
            case SPELL_HUNTER_RAPTOR_STRIKE_R1:
            case SPELL_HUNTER_RAPTOR_STRIKE_R2:
            case SPELL_HUNTER_RAPTOR_STRIKE_R3:
            case SPELL_HUNTER_RAPTOR_STRIKE_R4:
            case SPELL_HUNTER_RAPTOR_STRIKE_R5:
            case SPELL_HUNTER_RAPTOR_STRIKE_R6:
            case SPELL_HUNTER_RAPTOR_STRIKE_R7:
            case SPELL_HUNTER_RAPTOR_STRIKE_R8:
            case SPELL_HUNTER_ARCANE_SHOT_R1:
            case SPELL_HUNTER_ARCANE_SHOT_R2:
            case SPELL_HUNTER_ARCANE_SHOT_R3:
            case SPELL_HUNTER_ARCANE_SHOT_R4:
            case SPELL_HUNTER_ARCANE_SHOT_R5:
            case SPELL_HUNTER_ARCANE_SHOT_R6:
            case SPELL_HUNTER_ARCANE_SHOT_R7:
            case SPELL_HUNTER_ARCANE_SHOT_R8:
            case SPELL_HUNTER_STEADY_SHOT_R1:
            case SPELL_HUNTER_STEADY_SHOT_R2:
            case SPELL_HUNTER_STEADY_SHOT_R3:
            case SPELL_HUNTER_STEADY_SHOT_R4:
            case SPELL_HUNTER_STEADY_SHOT_R5:
            case SPELL_HUNTER_STEADY_SHOT_R6:
            case SPELL_HUNTER_STEADY_SHOT_R7:
            case SPELL_HUNTER_STEADY_SHOT_R8:
                return std::nullopt;
            default:
                return SPELL_PROC_TRIGGER_FAILED;
        }
    }
};

void ApplyStingingNettle(Spell* spell)
{
    Unit* target = spell->GetUnitTarget();
    Player* caster = ToPlayer(spell->GetAffectiveCaster());
    if (!target || !target->IsAlive() || !caster)
        return;

    uint32 nettleSpellId = 0;
    if (caster->HasSpell(SPELL_HUNTER_STINGING_NETTLE_R2))
        nettleSpellId = SPELL_HUNTER_STINGING_NETTLE_R2;
    else if (caster->HasSpell(SPELL_HUNTER_STINGING_NETTLE_R1))
        nettleSpellId = SPELL_HUNTER_STINGING_NETTLE_R1;

    if (!nettleSpellId)
        return;

    int32 durationPct = 0;
    if (SpellEntry const* nettleInfo = sSpellMgr.GetSpellEntry(nettleSpellId))
        durationPct = nettleInfo->CalculateSimpleValue(EFFECT_INDEX_0);

    if (durationPct <= 0)
        durationPct = nettleSpellId == SPELL_HUNTER_STINGING_NETTLE_R2 ? 40 : 20;

    uint32 const serpentFirstRank = sSpellMgr.GetFirstSpellInChain(SPELL_HUNTER_SERPENT_STING_R1);
    uint32 serpentId = caster->HasSpell(serpentFirstRank) ? serpentFirstRank : 0;

    struct HighestSerpentWorker
    {
        Player* player;
        uint32& spellId;

        void operator()(uint32 id) const
        {
            if (player->HasSpell(id))
                spellId = id;
        }
    };

    HighestSerpentWorker worker{ caster, serpentId };
    sSpellMgr.doForHighRanks(serpentFirstRank, worker);

    SpellEntry const* serpentInfo = serpentId ? sSpellMgr.GetSpellEntry(serpentId) : nullptr;
    if (!serpentInfo)
        return;

    int32 const baseDuration = serpentInfo->CalculateDuration(caster);
    if (baseDuration <= 0)
        return;

    int32 newDuration = std::max(1, (baseDuration * durationPct) / 100);

    if (SpellAuraHolder* existing = target->GetSpellAuraHolder(serpentId))
        if (existing->GetCasterGuid() == caster->GetObjectGuid() && existing->GetAuraDuration() > newDuration)
            return;

    if (SpellAuraHolder* holder = target->AddAura(serpentId, 0, caster))
    {
        holder->SetAuraMaxDuration(newDuration);
        holder->SetAuraDuration(newDuration);
    }
}

struct spell_hunter_frost_trap_aura : public AuraScript
{
    void OnPeriodicTrigger(Aura* aura, Unit* /*caster*/, Unit* target, WorldObject* /*targetObject*/, SpellEntry const*& spellInfo) override
    {
        Unit* caster = aura->GetCaster();
        if (!caster)
        {
            spellInfo = nullptr;
            return;
        }

        caster->ProcDamageAndSpell(target, PROC_FLAG_ON_TRAP_ACTIVATION, PROC_FLAG_NONE, PROC_EX_NORMAL_HIT, 1, 1, BASE_ATTACK, aura->GetSpellProto());
        spellInfo = nullptr;
    }
};

struct spell_hunter_eyes_of_the_beast : public AuraScript
{
    void OnAfterApply(Aura* aura, bool apply) override
    {
        Unit* caster = aura->GetCaster();
        if (!caster)
            return;

        if (caster->HasSpell(SPELL_HUNTER_EYES_OF_THE_BEAST_TALENT_R1))
        {
            if (apply)
                caster->CastSpell(caster, SPELL_HUNTER_EYES_OF_THE_BEAST_BOND_R1, true);
            else
                caster->RemoveAurasDueToSpell(SPELL_HUNTER_EYES_OF_THE_BEAST_BOND_R1);
        }
        else if (caster->HasSpell(SPELL_HUNTER_EYES_OF_THE_BEAST_TALENT_R2))
        {
            if (apply)
                caster->CastSpell(caster, SPELL_HUNTER_EYES_OF_THE_BEAST_BOND_R2, true);
            else
                caster->RemoveAurasDueToSpell(SPELL_HUNTER_EYES_OF_THE_BEAST_BOND_R2);
        }
    }
};
}

void AddSC_hunter_spell_scripts()
{
    RegisterSpellScript("spell_hunter_wyvern_sting", &GetSpellScript<spell_hunter_wyvern_sting>);
    RegisterSpellScript("spell_hunter_refocus", &GetSpellScript<spell_hunter_refocus>);
    RegisterSpellScript("spell_hunter_readiness", &GetSpellScript<spell_hunter_readiness>);
    RegisterSpellScript("spell_hunter_counterattack", &GetSpellScript<spell_hunter_counterattack>);
    RegisterSpellScript("spell_hunter_mongoose_bite", &GetSpellScript<spell_hunter_mongoose_bite>);
    RegisterSpellScript("spell_hunter_stinging_nettle_fire_trap", &GetSpellScript<spell_hunter_stinging_nettle_fire_trap>);
    RegisterSpellScript("spell_hunter_trap", &GetSpellScript<spell_hunter_trap>);
    RegisterSpellScript("spell_hunter_lacerate", &GetSpellScript<spell_hunter_lacerate>);
    RegisterSpellScript("spell_hunter_bestial_wrath", &GetSpellScript<spell_hunter_bestial_wrath>);
    RegisterSpellScript("spell_hunter_kill_command", &GetSpellScript<spell_hunter_kill_command>);
    RegisterSpellScript("spell_hunter_kill_command_damage", &GetSpellScript<spell_hunter_kill_command_damage>);
    RegisterSpellScript("spell_hunter_strike_together_damage", &GetSpellScript<spell_hunter_strike_together_damage>);
    RegisterSpellScript("spell_hunter_experimental_ammunition_trigger", &GetSpellScript<spell_hunter_experimental_ammunition_trigger>);
    RegisterAuraScript("spell_hunter_coordinated_assault", &GetAuraScript<spell_hunter_coordinated_assault>);
    RegisterAuraScript("spell_hunter_volley", &GetAuraScript<spell_hunter_volley>);
    RegisterAuraScript("spell_hunter_experimental_ammunition", &GetAuraScript<spell_hunter_experimental_ammunition>);
    RegisterAuraScript("spell_hunter_lock_and_load", &GetAuraScript<spell_hunter_lock_and_load>);
    RegisterSpellScript("spell_hunter_aimed_shot", &GetSpellScript<spell_hunter_aimed_shot>);
    RegisterAuraScript("spell_hunter_piercing_shots", &GetAuraScript<spell_hunter_piercing_shots>);
    RegisterAuraScript("spell_hunter_improved_mend_pet", &GetAuraScript<spell_hunter_improved_mend_pet>);
    RegisterAuraScript("spell_hunter_swift_aspects", &GetAuraScript<spell_hunter_swift_aspects>);
    RegisterAuraScript("spell_hunter_improved_primal_aspects", &GetAuraScript<spell_hunter_improved_primal_aspects>);
    RegisterAuraScript("spell_hunter_alone_against_the_world", &GetAuraScript<spell_hunter_alone_against_the_world>);
    RegisterAuraScript("spell_hunter_frost_trap_aura", &GetAuraScript<spell_hunter_frost_trap_aura>);
    RegisterAuraScript("spell_hunter_eyes_of_the_beast", &GetAuraScript<spell_hunter_eyes_of_the_beast>);
}
