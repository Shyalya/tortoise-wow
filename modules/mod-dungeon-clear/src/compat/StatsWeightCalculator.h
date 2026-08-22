#ifndef DC_COMPAT_STATSWEIGHTCALCULATOR_H
#define DC_COMPAT_STATSWEIGHTCALCULATOR_H

// mod-playerbots scores an item for a bot through StatsWeightCalculator. The
// ike3 tree here does the same job in RandomItemMgr::GetStatWeight, so this is
// that call wearing the other name.
//
// The three setters are no-ops. Upstream they tune the scoring - count set
// bonuses, penalise stats past a cap, switch to a pvp weighting - and this
// tree's scorer has none of those knobs. The module uses the result for one
// decision, need or greed on a loot roll, and a plain stat weight answers it:
// above zero the item is worth something to this class, at zero it is not.
// Losing the tuning changes how good that answer is, not what it means.

#include "playerbot/RandomItemMgr.h"

class Player;

class StatsWeightCalculator
{
    public:
        explicit StatsWeightCalculator(Player* owner) : m_owner(owner) {}

        void SetItemSetBonus(bool /*enable*/) {}
        void SetOverflowPenalty(bool /*enable*/) {}
        void SetPvpSpec(bool /*enable*/) {}

        float CalculateItem(uint32 itemId, int32 /*randomProperty*/ = 0)
        {
            if (!m_owner)
                return 0.0f;

            return float(sRandomItemMgr.GetStatWeight(m_owner, itemId));
        }

    private:
        Player* m_owner;
};

#endif
