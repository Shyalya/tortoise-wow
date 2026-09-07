// Native calculation fragments; fixture services do not simulate Spell/Aura lifecycle.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>
using uint32 = uint32_t;
using int32 = int32_t;
#include "SpellAuraDefines.h"

struct Modifier { int32 m_amount, m_miscvalue; };
struct Aura
{
    mutable Modifier modifier;
    Modifier* GetModifier() const { return &modifier; }
};
using AuraList = std::vector<Aura const*>;
struct SpellEntry
{
    uint32 school = 1;
    uint32 EffectChainTarget[3] = {0, 0, 0};
    uint32 GetSpellSchoolMask() const { return school; }
};
class Unit
{
public:
    std::array<AuraList, TOTAL_AURAS> auras;
    AuraList const& GetAurasByType(AuraType type) const { return auras.at(type); }
    float GetTotalAuraMultiplier(AuraType) const;
    float GetTotalAuraMultiplierByMiscMask(AuraType, uint32) const;
    float ChainSpell(SpellEntry const* spellProto, uint32 effectIndex, uint32 schoolMask)
    {
        float takenTotalMod = 1.0f;
#include "NativeChainSpell.inc"
        return takenTotalMod;
    }
    float ChainWeapon(SpellEntry const* spellProto, uint32 effectIndex, uint32 schoolMask)
    {
        float TakenPercent = 1.0f;
#include "NativeChainWeapon.inc"
        return TakenPercent;
    }
};
#include "NativeAuraMultiplier.inc"
#include "NativeAuraMaskMultiplier.inc"
enum { CONFIG_FLOAT_RATE_POWER_RAGE_INCOME, POWER_RAGE };
struct World { float rate = 1; float getConfig(int) const { return rate; } } sWorld;
class Player : public Unit
{
public:
    uint32 rage = 0;
    uint32 GetLevel() const { return 60; }
    void ModifyPower(int, uint32 amount) { rage = amount; }
    void RewardRage(uint32, bool);
};
#include "NativeRewardRage.inc"
struct Skill { uint32 skillId; };
struct SpellMgr
{
    std::multimap<uint32, Skill const*> skills;
    unsigned lookups = 0;
    auto GetSkillLineAbilityMapBoundsBySpellId(uint32 id) { ++lookups; return skills.equal_range(id); }
} sSpellMgr;
int32 SkillCast(Unit* pUnit, uint32 Id, int32 castTime)
{
#include "NativeSkillCastTime.inc"
    return castTime;
}
enum DamageEffectType { DIRECT, DOT };
float Periodic(Unit* pUnit, SpellEntry const* spellProto, DamageEffectType damagetype)
{
    float DoneTotalMod = 1.0f;
#include "NativePeriodicBonus.inc"
    return DoneTotalMod;
}
void Check(bool ok, char const* message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool Near(float a, float b) { return std::abs(a-b) < 0.00001f; }
int main()
{
    static_assert(SPELL_AURA_MOD_ATTACKING_RAGE_PERCENT == 227 && SPELL_AURA_MOD_SKILL_CAST_TIME == 228);
    static_assert(SPELL_AURA_MOD_PERIODIC_DAMAGE_PERCENT_DONE == 229 && SPELL_AURA_MOD_CHAIN_DAMAGE_PERCENT_TAKEN == 230);
    static_assert(TOTAL_AURAS == 231);
    Player player;
    Aura rage{{3, 0}}, cooker{{-50, 185}}, shadow{{2, 32}}, chain{{-40, 1}}, aoe{{-80, 127}};
    player.RewardRage(10000, true); uint32 outgoing = player.rage;
    player.RewardRage(10000, false); uint32 incoming = player.rage;
    player.auras[SPELL_AURA_MOD_ATTACKING_RAGE_PERCENT].push_back(&rage);
    player.RewardRage(10000, true);
    Check(std::abs(double(player.rage) - outgoing * 1.03) <= 2, "attacking rage +3%");
    player.RewardRage(10000, false); Check(player.rage == incoming, "incoming rage unchanged");
    player.auras[SPELL_AURA_MOD_ATTACKING_RAGE_PERCENT].clear();
    player.RewardRage(10000, true); Check(player.rage == outgoing, "rage aura removal");
    sWorld.rate = 2; player.RewardRage(10000, true);
    Check(std::abs(double(player.rage) - outgoing * 2) <= 1, "configured rage income retained");
    player.RewardRage(0, true); Check(player.rage == 0, "zero damage");

    Skill cooking{185}, other{164};
    sSpellMgr.skills.emplace(100, &cooking); sSpellMgr.skills.emplace(100, &cooking);
    sSpellMgr.skills.emplace(200, &other);
    Check(SkillCast(&player, 100, 3000) == 3000 && sSpellMgr.lookups == 0, "no aura means no skill lookup");
    player.auras[SPELL_AURA_MOD_SKILL_CAST_TIME].push_back(&cooker);
    Check(SkillCast(&player, 100, 3000) == 1500, "cooking bonus applied once despite duplicate skill rows");
    Check(SkillCast(&player, 200, 3000) == 3000, "other profession unchanged");
    Check(SkillCast(&player, 300, 3000) == 3000, "unmapped combat spell unchanged");
    Check(SkillCast(&player, 100, 0) == 0, "instant spell remains instant");
    player.auras[SPELL_AURA_MOD_SKILL_CAST_TIME].clear();
    Check(SkillCast(&player, 100, 3000) == 3000, "cooker removal");

    SpellEntry spell; spell.school = 32;
    player.auras[SPELL_AURA_MOD_PERIODIC_DAMAGE_PERCENT_DONE].push_back(&shadow);
    Check(Near(Periodic(&player, &spell, DOT), 1.02f), "shadow periodic bonus");
    Check(Near(Periodic(&player, &spell, DIRECT), 1), "direct damage unchanged");
    spell.school = 4; Check(Near(Periodic(&player, &spell, DOT), 1), "other school unchanged");
    Check(Near(Periodic(nullptr, &spell, DOT), 1), "non-unit source unchanged");
    player.auras[SPELL_AURA_MOD_PERIODIC_DAMAGE_PERCENT_DONE].clear();
    spell.school = 32; Check(Near(Periodic(&player, &spell, DOT), 1), "periodic aura removal");

    player.auras[SPELL_AURA_MOD_CHAIN_DAMAGE_PERCENT_TAKEN].push_back(&chain);
    player.auras[SPELL_AURA_MOD_AOE_DAMAGE_PERCENT_TAKEN].push_back(&aoe);
    spell.EffectChainTarget[0] = 3;
    for (uint32 school : {0u, 1u, 4u, 32u})
    {
        float expected = school == 1 ? .6f : 1.f;
        Check(Near(player.ChainSpell(&spell, 0, school), expected), "spell chain school mask");
        Check(Near(player.ChainWeapon(&spell, 0, school), expected), "weapon chain school mask");
    }
    Check(Near(player.ChainSpell(&spell, 1, 1), 1), "non-chain sibling effect unchanged");
    Check(Near(player.ChainWeapon(nullptr, 0, 1), 1), "ordinary melee unchanged");
    Check(Near(player.GetTotalAuraMultiplier(SPELL_AURA_MOD_AOE_DAMAGE_PERCENT_TAKEN), .2f), "native AoE reduction retained independently");
    player.auras[SPELL_AURA_MOD_CHAIN_DAMAGE_PERCENT_TAKEN].clear();
    Check(Near(player.ChainSpell(&spell, 0, 1), 1), "chain aura removal");
    std::cout << "PASS: native custom aura calculations and boundary variants\n";
}
