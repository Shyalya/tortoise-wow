#include "playerbot/playerbot.h"
#include "CooperativeGameObjectTrigger.h"
#include "playerbot/strategy/actions/CooperativeGameObjectActions.h"
using namespace ai;
bool CooperativeGameObjectTrigger::IsActive()
{
    Action* action = context->GetAction("use cooperative game object");
    UseCooperativeGameObjectAction* cooperative = dynamic_cast<UseCooperativeGameObjectAction*>(action);
    return cooperative && cooperative->HasPendingIntent();
}
