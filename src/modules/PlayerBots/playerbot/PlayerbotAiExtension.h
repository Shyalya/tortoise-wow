#pragma once

// Extension seam for optional modules (e.g. DungeonClear) that register extra
// strategy / action / trigger / value contexts into every bot's AiObjectContext
// without forking the whole playerbots tree.

#include "playerbot/strategy/NamedObjectContext.h"
#include "playerbot/BotState.h"
#include <string>
#include <vector>

class Player;
class PlayerbotAI;
class ChatHandler;

namespace ai
{
    class Strategy;
    class Action;
    class Trigger;
    class UntypedValue;
    class AiObjectContext;

    class PlayerbotAiExtension
    {
    public:
        using StrategyFactory = NamedObjectContext<Strategy>* (*)();
        using ActionFactory = NamedObjectContext<Action>* (*)();
        using TriggerFactory = NamedObjectContext<Trigger>* (*)();
        using ValueFactory = NamedObjectContext<UntypedValue>* (*)();
        using StrategyGateFn = void (*)(PlayerbotAI* ai, Player* bot);
        using DcCommandFn = bool (*)(ChatHandler* handler, char* args);
        using WorldUpdateFn = void (*)(uint32 diff);
        using AddonMessageFn = bool (*)(Player* player, std::string const& msg);
        using StartupFn = void (*)();

        static PlayerbotAiExtension& Instance();

        void RegisterStrategyFactory(StrategyFactory factory);
        void RegisterActionFactory(ActionFactory factory);
        void RegisterTriggerFactory(TriggerFactory factory);
        void RegisterValueFactory(ValueFactory factory);
        void RegisterStrategyGate(StrategyGateFn gate);
        void RegisterDcCommand(DcCommandFn fn) { dcCommand = fn; }
        void RegisterWorldUpdate(WorldUpdateFn fn) { worldUpdate = fn; }
        void RegisterAddonHandler(AddonMessageFn fn) { addonHandler = fn; }
        void RegisterStartupHook(StartupFn fn);

        void ApplyToContext(AiObjectContext* context) const;
        void RunStrategyGates(PlayerbotAI* ai, Player* bot) const;
        bool HandleDcCommand(ChatHandler* handler, char* args) const;
        void RunWorldUpdate(uint32 diff) const;
        bool HandleAddonMessage(Player* player, std::string const& msg) const;
        void RunStartupHooks() const;

    private:
        PlayerbotAiExtension() = default;

        std::vector<StrategyFactory> strategyFactories;
        std::vector<ActionFactory> actionFactories;
        std::vector<TriggerFactory> triggerFactories;
        std::vector<ValueFactory> valueFactories;
        std::vector<StrategyGateFn> strategyGates;
        std::vector<StartupFn> startupHooks;
        DcCommandFn dcCommand = nullptr;
        WorldUpdateFn worldUpdate = nullptr;
        AddonMessageFn addonHandler = nullptr;
    };
}

#define sPlayerbotAiExtension ai::PlayerbotAiExtension::Instance()
