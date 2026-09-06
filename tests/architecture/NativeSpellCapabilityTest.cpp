#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
using uint32 = uint32_t;
enum { PLAYERSPELL_UNCHANGED, PLAYERSPELL_REMOVED };
struct PlayerSpell { int state = PLAYERSPELL_UNCHANGED; bool disabled = false; };
using PlayerSpellMap = std::unordered_map<uint32, PlayerSpell>;
struct Pet
{
    std::unordered_set<uint32> spells;
    bool HasSpell(uint32 id) const { return spells.count(id) != 0; }
};
struct Player
{
    PlayerSpellMap m_spells;
    Pet* pet = nullptr;
    Pet* GetPet() const { return pet; }
    bool HasSpell(uint32 spell) const;
};
#include "NativeHasSpell.inc"
struct { bool LookupSpellInfo(uint32 id) const { return id && id <= 10; } } sServerFacade;
struct PlayerbotAI
{
    Player* bot;
    bool HasSpell(uint32 spellid) const;
};
#include "AICapability.inc"
void Require(bool ok) { if (!ok) std::abort(); }
int main()
{
    Player p; PlayerbotAI ai{&p};
    Require(!ai.HasSpell(0) && !ai.HasSpell(1));
    p.m_spells[1] = {};
    Require(ai.HasSpell(1));
    p.m_spells[1].disabled = true; // same level/count/talents; no TTL wait
    Require(!ai.HasSpell(1));
    p.m_spells[1].disabled = false;
    Require(ai.HasSpell(1));
    p.m_spells[1].state = PLAYERSPELL_REMOVED;
    Require(!ai.HasSpell(1));
    p.m_spells.erase(1); p.m_spells[2] = {}; // count-preserving replacement
    Require(!ai.HasSpell(1) && ai.HasSpell(2));
    Pet pet; pet.spells.insert(1); p.pet = &pet;
    Require(ai.HasSpell(1));
    pet.spells.clear(); Require(!ai.HasSpell(1));
    pet.spells.insert(1); p.pet = nullptr; Require(!ai.HasSpell(1));
    p.m_spells[11] = {}; Require(!ai.HasSpell(11)); // unknown spell data
    std::cout << "Native capability checks: learn, disable, remove, replace, pet transitions passed\n";
}
