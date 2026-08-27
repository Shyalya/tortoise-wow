#include "playerbot/PlayerbotAiExtension.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Value.h"
#include "Chat/Chat.h"

using namespace ai;

PlayerbotAiExtension& PlayerbotAiExtension::Instance()
{
    static PlayerbotAiExtension instance;
    return instance;
}

void PlayerbotAiExtension::RegisterStrategyFactory(StrategyFactory factory)
{
    if (factory)
        strategyFactories.push_back(factory);
}

void PlayerbotAiExtension::RegisterActionFactory(ActionFactory factory)
{
    if (factory)
        actionFactories.push_back(factory);
}

void PlayerbotAiExtension::RegisterTriggerFactory(TriggerFactory factory)
{
    if (factory)
        triggerFactories.push_back(factory);
}

void PlayerbotAiExtension::RegisterValueFactory(ValueFactory factory)
{
    if (factory)
        valueFactories.push_back(factory);
}

void PlayerbotAiExtension::RegisterStrategyGate(StrategyGateFn gate)
{
    if (gate)
        strategyGates.push_back(gate);
}

void PlayerbotAiExtension::RegisterStartupHook(StartupFn fn)
{
    if (fn)
        startupHooks.push_back(fn);
}

void PlayerbotAiExtension::ApplyToContext(AiObjectContext* context) const
{
    if (!context)
        return;

    for (StrategyFactory factory : strategyFactories)
        context->AddStrategyContext(factory());

    for (ActionFactory factory : actionFactories)
        context->AddActionContext(factory());

    for (TriggerFactory factory : triggerFactories)
        context->AddTriggerContext(factory());

    for (ValueFactory factory : valueFactories)
        context->AddValueContext(factory());
}

void PlayerbotAiExtension::RunStrategyGates(PlayerbotAI* ai, Player* bot) const
{
    for (StrategyGateFn gate : strategyGates)
        gate(ai, bot);
}

bool PlayerbotAiExtension::ShouldPreventAutoRelease(Player* bot) const
{
    return autoReleaseGuard && autoReleaseGuard(bot);
}

bool PlayerbotAiExtension::HandleDcCommand(ChatHandler* handler, char* args) const
{
    if (dcCommand)
        return dcCommand(handler, args);
    if (handler)
        handler->SendSysMessage("DungeonClear not built (BUILD_DUNGEON_CLEAR=OFF).");
    return true;
}

void PlayerbotAiExtension::RunWorldUpdate(uint32 diff) const
{
    if (worldUpdate)
        worldUpdate(diff);
}

void PlayerbotAiExtension::RunStartupHooks() const
{
    for (StartupFn fn : startupHooks)
        fn();
}

bool PlayerbotAiExtension::HandleAddonMessage(Player* player, std::string const& msg) const
{
    if (addonHandler)
        return addonHandler(player, msg);
    return false;
}
