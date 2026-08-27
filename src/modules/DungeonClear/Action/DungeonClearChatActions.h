#pragma once
#include "playerbot/strategy/actions/GenericActions.h"

namespace ai
{
    class DcOnAction : public ChatCommandAction
    {
    public:
        DcOnAction(PlayerbotAI* ai) : ChatCommandAction(ai, "dc on") {}
        bool Execute(Event& event) override;
    };
    class DcOffAction : public ChatCommandAction
    {
    public:
        DcOffAction(PlayerbotAI* ai) : ChatCommandAction(ai, "dc off") {}
        bool Execute(Event& event) override;
    };
    class DcPauseAction : public ChatCommandAction
    {
    public:
        DcPauseAction(PlayerbotAI* ai) : ChatCommandAction(ai, "dc pause") {}
        bool Execute(Event& event) override;
    };
    class DcSkipAction : public ChatCommandAction
    {
    public:
        DcSkipAction(PlayerbotAI* ai) : ChatCommandAction(ai, "dc skip") {}
        bool Execute(Event& event) override;
    };
    class DcPullModeAction : public ChatCommandAction
    {
    public:
        DcPullModeAction(PlayerbotAI* ai) : ChatCommandAction(ai, "dc pull") {}
        bool Execute(Event& event) override;
    };
    class DcStatusAction : public ChatCommandAction
    {
    public:
        DcStatusAction(PlayerbotAI* ai) : ChatCommandAction(ai, "dc status") {}
        bool Execute(Event& event) override;
    };
    class DcBossesAction : public ChatCommandAction
    {
    public:
        DcBossesAction(PlayerbotAI* ai) : ChatCommandAction(ai, "dc bosses") {}
        bool Execute(Event& event) override;
    };
    class DcGoAction : public ChatCommandAction
    {
    public:
        DcGoAction(PlayerbotAI* ai) : ChatCommandAction(ai, "dc go") {}
        bool Execute(Event& event) override;
    };
}
