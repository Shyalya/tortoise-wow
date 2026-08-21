#pragma once

#include "DungeonActions.h"
#include "ChangeStrategyAction.h"
#include "UseItemAction.h"
#include "AttackAction.h"
#include "playerbot/strategy/generic/MoltenCoreHelpers.h"

namespace ai
{
    class MoltenCoreEnableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MoltenCoreEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable molten core strategy", "+molten core") {}
    };

    class MoltenCoreDisableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MoltenCoreDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable molten core strategy", "-molten core") {}
    };

    class MagmadarEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MagmadarEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable magmadar fight strategy", "+magmadar") {}
    };

    class MagmadarDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MagmadarDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable magmadar fight strategy", "-magmadar") {}
    };

    class MagmadarMoveAwayFromLavaBombAction : public MoveAwayFromHazard
    {
    public:
        MagmadarMoveAwayFromLavaBombAction(PlayerbotAI* ai) : MoveAwayFromHazard(ai, "move away from magmadar lava bomb") {}
    };

    class MagmadarMoveAwayAction : public MoveAwayFromCreature
    {
    public:
        MagmadarMoveAwayAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "move away from magmadar", 11982, 31.0f) {}
    };

    class MoveToMCRuneAction : public MoveToAction
    {
    public:
        MoveToMCRuneAction(PlayerbotAI* ai) : MoveToAction(ai, "move to mc rune") { qualifier = "entry filter::{gos in sight,mc runes}"; }
    };

    class DouseMCRuneActionAqual : public UseItemIdAction
    {
    public:
        DouseMCRuneActionAqual(PlayerbotAI* ai) : UseItemIdAction(ai, "douse mc rune aqual") { qualifier = "{17333,entry filter::{gos close,mc runes}}"; }
    };

    class DouseMCRuneActionEternal : public UseItemIdAction
    {
    public:
        DouseMCRuneActionEternal(PlayerbotAI* ai) : UseItemIdAction(ai, "douse mc rune eternal") { qualifier = "{22754,entry filter::{gos close,mc runes}}"; }
    };

    class McMoveFromGroupAction : public MovementAction
    {
    public:
        McMoveFromGroupAction(PlayerbotAI* ai, std::string const name = "mc move from group")
            : MovementAction(ai, name) {}
        bool Execute(Event& event) override;
    };

    class McMoveFromBaronGeddonAction : public MovementAction
    {
    public:
        McMoveFromBaronGeddonAction(PlayerbotAI* ai, std::string const name = "mc move from baron geddon")
            : MovementAction(ai, name) {}
        bool Execute(Event& event) override;
    };

    class McShazzrahMoveAwayAction : public MovementAction
    {
    public:
        McShazzrahMoveAwayAction(PlayerbotAI* ai, std::string const name = "mc shazzrah move away")
            : MovementAction(ai, name) {}
        bool Execute(Event& event) override;
    };

    class McGarrMarkBossAction : public Action
    {
    public:
        McGarrMarkBossAction(PlayerbotAI* ai, std::string const name = "mc garr mark boss")
            : Action(ai, name) {}
        bool Execute(Event& event) override;
    };

    class McGarrMainTankAttackGarrAction : public AttackAction
    {
    public:
        McGarrMainTankAttackGarrAction(PlayerbotAI* ai, std::string const name = "mc garr main tank attack garr")
            : AttackAction(ai, name) {}
        bool Execute(Event& event) override;
    };

    class McGarrAssistTankAttackFireswornAction : public AttackAction
    {
    public:
        McGarrAssistTankAttackFireswornAction(PlayerbotAI* ai, std::string const name = "mc garr assist tank attack firesworn")
            : AttackAction(ai, name) {}
        bool Execute(Event& event) override;
    };

    class McGarrDpsAttackGarrAction : public AttackAction
    {
    public:
        McGarrDpsAttackGarrAction(PlayerbotAI* ai, std::string const name = "mc garr dps attack garr")
            : AttackAction(ai, name) {}
        bool Execute(Event& event) override;
    };

    class McGarrMarkFireswornAction : public Action
    {
    public:
        McGarrMarkFireswornAction(PlayerbotAI* ai, std::string const name = "mc garr mark firesworn")
            : Action(ai, name) {}
        Unit* GetTarget() override;
        bool Execute(Event& event) override;
    };

    class McGolemaggMarkBossAction : public Action
    {
    public:
        McGolemaggMarkBossAction(PlayerbotAI* ai, std::string const name = "mc golemagg mark boss")
            : Action(ai, name) {}
        bool Execute(Event& event) override;
    };

    class McGolemaggTankAction : public AttackAction
    {
    public:
        McGolemaggTankAction(PlayerbotAI* ai, std::string const& name) : AttackAction(ai, name) {}

    protected:
        bool MoveUnitToPosition(Unit* target, float tankX, float tankY, float maxDistance, float stepDistance = 3.0f);
        bool FindCoreRagers(Unit*& coreRager1, Unit*& coreRager2) const;
    };

    class McGolemaggMainTankAttackGolemaggAction : public McGolemaggTankAction
    {
    public:
        McGolemaggMainTankAttackGolemaggAction(PlayerbotAI* ai, std::string const name = "mc golemagg main tank attack golemagg")
            : McGolemaggTankAction(ai, name) {}
        bool Execute(Event& event) override;
    };

    class McGolemaggAssistTankAttackCoreRagerAction : public McGolemaggTankAction
    {
    public:
        McGolemaggAssistTankAttackCoreRagerAction(PlayerbotAI* ai, std::string const name = "mc golemagg assist tank attack core rager")
            : McGolemaggTankAction(ai, name) {}
        bool Execute(Event& event) override;
    };

    class McCoreHoundMarkAction : public Action
    {
    public:
        McCoreHoundMarkAction(PlayerbotAI* ai, std::string const name = "mc core hound mark")
            : Action(ai, name) {}
        Unit* GetTarget() override;
        bool Execute(Event& event) override;
    };
}
