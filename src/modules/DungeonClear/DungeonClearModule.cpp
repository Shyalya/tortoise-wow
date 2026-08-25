#include "DungeonClearModule.h"
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAiExtension.h"
#include "DungeonClearStrategyContext.h"
#include "DungeonClearActionContext.h"
#include "DungeonClearTriggerContext.h"
#include "DungeonClearValueContext.h"
#include "DcStrategyGate.h"
#include "Settings/DcSettings.h"
#include "Data/DungeonEventRegistry.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "Chat/Chat.h"
#include "Log.h"

using namespace ai;

// Forward decls implemented in sibling TUs.
bool ChatHandler_HandleDungeonClearCommand(ChatHandler* handler, char* args);
bool DungeonClear_HandleAddonMessage(Player* player, std::string const& msg);

namespace
{
    NamedObjectContext<Strategy>* MakeStrategyCtx() { return new DungeonClearStrategyContext(); }
    NamedObjectContext<Action>* MakeActionCtx() { return new DungeonClearActionContext(); }
    NamedObjectContext<Trigger>* MakeTriggerCtx() { return new DungeonClearTriggerContext(); }
    NamedObjectContext<UntypedValue>* MakeValueCtx() { return new DungeonClearValueContext(); }

    uint32 s_gateAccum = 0;

    bool DcCommandThunk(ChatHandler* handler, char* args)
    {
        return ChatHandler_HandleDungeonClearCommand(handler, args);
    }

    void WorldUpdateThunk(uint32 diff)
    {
        DungeonClearModule::Update(diff);
    }
}

namespace DungeonClearModule
{
    void Initialize()
    {
        sDcSettings.Load();
        DungeonEventRegistry::Instance().Initialize();

        sPlayerbotAiExtension.RegisterStrategyFactory(&MakeStrategyCtx);
        sPlayerbotAiExtension.RegisterActionFactory(&MakeActionCtx);
        sPlayerbotAiExtension.RegisterTriggerFactory(&MakeTriggerCtx);
        sPlayerbotAiExtension.RegisterValueFactory(&MakeValueCtx);
        DcStrategyGate::Register();
        sPlayerbotAiExtension.RegisterDcCommand(&DcCommandThunk);
        sPlayerbotAiExtension.RegisterWorldUpdate(&WorldUpdateThunk);
        sPlayerbotAiExtension.RegisterAddonHandler(&DungeonClear_HandleAddonMessage);

        sLog.outString("DungeonClear: module initialized");
    }

    void Update(uint32 diff)
    {
        if (!sDcSettings.moduleEnabled)
            return;
        s_gateAccum += diff;
        if (s_gateAccum < sDcSettings.strategyGateSweepMs)
            return;
        s_gateAccum = 0;

        sRandomPlayerbotMgr.ForEachPlayerbot([](Player* bot)
        {
            if (!bot || !bot->GetPlayerbotAI())
                return;
            DcStrategyGate::Reconcile(bot->GetPlayerbotAI(), bot);
        });
    }
}

namespace
{
    struct DcAutoInit
    {
        DcAutoInit() { DungeonClearModule::Initialize(); }
    };
    DcAutoInit s_dcAutoInit;
}
