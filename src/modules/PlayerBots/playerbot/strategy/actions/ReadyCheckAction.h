#pragma once
#include "playerbot/strategy/Action.h"

namespace ai
{
    class ReadyCheckAction : public Action
    {
    public:
        ReadyCheckAction(PlayerbotAI* ai, std::string name = "ready check") : Action(ai, name) {}
        virtual bool Execute(Event& event) override;

    protected:
        bool ReadyCheck(Player* requester, bool deferForRebuff = false);
    };

    class FinishReadyCheckAction : public ReadyCheckAction
    {
    public:
        FinishReadyCheckAction(PlayerbotAI* ai) : ReadyCheckAction(ai, "finish ready check") {}
        virtual bool Execute(Event& event) override;
    };

    class ForceRebuffAction : public Action
    {
    public:
        ForceRebuffAction(PlayerbotAI* ai) : Action(ai, "force rebuff") {}
        bool Execute(Event& event) override;
    };

    class ReadyReplyAction : public Action
    {
    public:
        ReadyReplyAction(PlayerbotAI* ai) : Action(ai, "ready reply") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
    };
}
