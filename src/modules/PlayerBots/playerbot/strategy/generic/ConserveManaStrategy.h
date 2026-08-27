#pragma once
#include "playerbot/strategy/Multiplier.h"
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class ConserveManaMultiplier : public Multiplier
    {
    public:
        ConserveManaMultiplier(PlayerbotAI* ai) : Multiplier(ai, "conserve mana") {}

    public:
        virtual float GetValue(Action* action) override;
    };

    class SaveManaMultiplier : public Multiplier
    {
    public:
        SaveManaMultiplier(PlayerbotAI* ai) : Multiplier(ai, "save mana") {}

    public:
        virtual float GetValue(Action* action) override;
    };

    // AC-style: below a mana threshold, skip inefficient heals on lightly injured targets.
    class HealerAutoSaveManaMultiplier : public Multiplier
    {
    public:
        HealerAutoSaveManaMultiplier(PlayerbotAI* ai) : Multiplier(ai, "save mana") {}
        virtual float GetValue(Action* action) override;
    };

    class HealerAutoSaveManaStrategy : public Strategy
    {
    public:
        HealerAutoSaveManaStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "save mana"; }

    private:
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class ConserveManaStrategy : public Strategy
    {
    public:
        ConserveManaStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "conserve mana"; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "conserve mana"; } //Must equal iternal name
        virtual std::string GetHelpDescription() {
            return "This strategy will make bots wait longer between casting the same spell twice.\n"
                   "the delay is based on [h:value|mana save level].\n"
                   "Healers can separately skip inefficient heals when mana is below the save-mana threshold.";
        }
        virtual std::vector<std::string> GetRelatedStrategies() { return { }; }
#endif
    private:
        void InitCombatMultipliers(std::list<Multiplier*> &multipliers) override;
    };
}
