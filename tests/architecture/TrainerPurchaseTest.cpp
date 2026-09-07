// Compiles the production handler, not a copied implementation. Native spell
// effects/DB/client behavior are mocked and still require integration testing.
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <iostream>
using uint32 = uint32_t;
using int32 = int32_t;
enum { EFFECT_INDEX_0, UNIT_NPC_FLAG_TRAINER, TRAIN_FAIL_UNAVAILABLE,
    TRAIN_FAIL_NOT_ENOUGH_SKILL, TRAIN_FAIL_NOT_ENOUGH_MONEY,
    AURA_INTERRUPT_FLAG_TALK, SPELL_AURA_MOUNTED, SMSG_PLAY_SPELL_IMPACT,
    SPELL_EFFECT_LEARN_SPELL, SPELL_EFFECT_LEARN_PET_SPELL };
enum SpellCastResult { SPELL_CAST_OK, SPELL_CAST_FAILED };
enum TrainerSpellState { TRAINER_SPELL_GREEN, TRAINER_SPELL_RED };
struct ObjectGuid { std::string GetString() const { return "trainer"; } };
struct WorldPacket {
    WorldPacket(int = 0, int = 0) {}
    template<class T> WorldPacket& operator>>(T&) { return *this; }
    template<class T> WorldPacket& operator<<(T const&) { return *this; }
};
struct SpellEntry { uint32 Effect[1]{SPELL_EFFECT_LEARN_SPELL}; uint32 EffectTriggerSpell[1]{2}; };
struct TrainerSpell { uint32 spell = 1, spellCost = 10; };
struct TrainerSpellData { TrainerSpell row; bool listed = true;
    TrainerSpell const* Find(uint32) const { return listed ? &row : nullptr; } };
struct Player;
struct Creature {
    TrainerSpellData data; bool available = true;
    bool IsTrainerOf(Player*, bool) { return available; }
    bool IsWithinLOSInMap(Player*) { return available; }
    TrainerSpellData const* GetTrainerSpells() { return &data; }
    TrainerSpellData const* GetTrainerTemplateSpells() { return nullptr; }
};
struct Pet { bool known = false; bool HasSpell(uint32) const { return known; } };
struct Player {
    Creature trainer; Pet pet; bool hasPet = true, known = false, learns = true;
    uint32 money = 100, directLearns = 0, charges = 0;
    TrainerSpellState state = TRAINER_SPELL_GREEN;
    Creature* GetNPCIfCanInteractWith(ObjectGuid, int) { return &trainer; }
    TrainerSpellState GetTrainerSpellState(TrainerSpell const*) { return known ? TRAINER_SPELL_RED : state; }
    float GetReputationPriceDiscount(Creature*) { return 1.0f; }
    uint32 GetMoney() const { return money; }
    void ModifyMoney(int32 value) { money += value; ++charges; }
    void RemoveAurasWithInterruptFlags(int) {}
    void RemoveSpellsCausingAura(int) {}
    Pet* GetPet() { return hasPet ? &pet : nullptr; }
    void LearnSpell(uint32, bool) { ++directLearns; known = learns; }
    bool HasSpell(uint32) const { return known; }
    std::string GetGuidStr() const { return "player"; }
    ObjectGuid GetObjectGuid() const { return {}; }
};
struct SpellMgr {
    SpellEntry service, taught; bool valid = true, missingService = false, missingTaught = false;
    SpellEntry const* GetSpellEntry(uint32 id) const {
        return id == 1 ? (missingService ? nullptr : &service) : (missingTaught ? nullptr : &taught);
    }
    static bool IsSpellValid(SpellEntry const*, Player*, bool);
} sSpellMgr;
bool SpellMgr::IsSpellValid(SpellEntry const*, Player*, bool) { return sSpellMgr.valid; }
struct Logger { template<class... T> void outError(char const*, T...) {} } sLog;
#define DEBUG_LOG(...) ((void)0)
struct SpellCastTargets { void setUnitTarget(Player*) {} };
struct Spell {
    static inline Spell* allocated = nullptr;
    static inline SpellCastResult result = SPELL_CAST_OK;
    static inline bool effectCompletes = true;
    static inline unsigned casts = 0, updates = 0, cancels = 0;
    Player* owner;
    Spell(Player* player, SpellEntry const*, bool triggered) : owner(player) {
        if (triggered) throw std::runtime_error("Pet training must preserve non-triggered checks");
        allocated = this;
    }
    SpellCastResult prepare(SpellCastTargets) { ++casts; return result; }
    void update(uint32) { ++updates; if (effectCompletes && owner->GetPet()) owner->pet.known = true; }
    void cancel() { ++cancels; }
};
struct WorldSession {
    Player player; Player* _player = &player;
    unsigned failures = 0, successes = 0;
    Player* GetPlayer() { return _player; }
    void SendTrainingFailure(ObjectGuid, uint32, int) { ++failures; }
    void SendTrainingSuccess(ObjectGuid, uint32) { ++successes; }
    void SendPlaySpellVisual(ObjectGuid, int) {}
    void SendPacket(WorldPacket*) {}
    void HandleTrainerBuySpellOpcode(WorldPacket&);
};
#include "TrainerPurchaseHandler.inc"
void Require(bool condition, char const* message) { if (!condition) throw std::runtime_error(message); }
void Reset(bool pet) {
    delete Spell::allocated; Spell::allocated = nullptr;
    Spell::casts = Spell::updates = Spell::cancels = 0;
    Spell::result = SPELL_CAST_OK; Spell::effectCompletes = true;
    sSpellMgr = SpellMgr{};
    sSpellMgr.service.Effect[0] = pet ? SPELL_EFFECT_LEARN_PET_SPELL : SPELL_EFFECT_LEARN_SPELL;
}
int main() {
    WorldPacket packet;
    try {
        Reset(false); WorldSession normal;
        normal.HandleTrainerBuySpellOpcode(packet);
        Require(normal.player.known && normal.player.money == 90 && normal.successes == 1 && !Spell::casts, "Player training changed");
        normal.HandleTrainerBuySpellOpcode(packet);
        Require(normal.player.money == 90 && normal.player.charges == 1, "Repeated player purchase charged twice");

        Reset(false); WorldSession failedLearn; failedLearn.player.learns = false;
        failedLearn.HandleTrainerBuySpellOpcode(packet);
        Require(failedLearn.failures == 1 && failedLearn.player.money == 100, "Failed player learning charged money");

        Reset(true); WorldSession pet;
        pet.HandleTrainerBuySpellOpcode(packet);
        Require(pet.player.pet.known && !pet.player.directLearns && pet.player.money == 90 && pet.successes == 1 && Spell::casts == 1, "Pet training did not use native cast/postcondition");
        pet.HandleTrainerBuySpellOpcode(packet);
        Require(pet.player.money == 90 && Spell::casts == 1, "Repeated pet purchase recast or charged twice");

        Reset(true); WorldSession absent; absent.player.hasPet = false;
        absent.HandleTrainerBuySpellOpcode(packet);
        Require(absent.failures == 1 && !Spell::casts && absent.player.money == 100, "No-pet purchase accepted");

        Reset(true); WorldSession rejected; Spell::result = SPELL_CAST_FAILED;
        rejected.HandleTrainerBuySpellOpcode(packet);
        Require(rejected.failures == 1 && rejected.player.money == 100 && !Spell::updates && Spell::cancels == 1, "Native rejection bypassed");

        Reset(true); WorldSession incomplete; Spell::effectCompletes = false;
        incomplete.HandleTrainerBuySpellOpcode(packet);
        Require(incomplete.failures == 1 && incomplete.player.money == 100 && Spell::cancels == 1, "Incomplete service charged or left pending");

        for (bool petService : {false, true}) {
            for (unsigned invalid = 0; invalid < 7; ++invalid) {
                Reset(petService); WorldSession session;
                if (invalid == 0) session.player.money = 0;
                if (invalid == 1) session.player.trainer.available = false;
                if (invalid == 2) session.player.trainer.data.listed = false;
                if (invalid == 3) session.player.state = TRAINER_SPELL_RED;
                if (invalid == 4) sSpellMgr.missingTaught = true;
                if (invalid == 5) sSpellMgr.service.Effect[0] = 999;
                if (invalid == 6) sSpellMgr.valid = false;
                session.HandleTrainerBuySpellOpcode(packet);
                Require(session.failures == 1 && !session.player.charges && !Spell::casts && !session.player.directLearns, "Invalid purchase reached learning/payment");
            }
        }
        Reset(false);
        std::cout << "PASS: production trainer handler dispatch, failure, payment and repeated-purchase checks (native services mocked)\n";
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; delete Spell::allocated; return 1; }
}
