#include "playerbot/playerbot.h"
#include "Value/DungeonClearPartyTankValue.h"
#include "Util/DungeonClearUtil.h"
#include "Objects/Player.h"

using namespace ai;

ObjectGuid DungeonClearPartyTankValue::Calculate()
{
    Player* tank = DcUtil::FindEnabledTank(bot);
    return tank ? tank->GetObjectGuid() : ObjectGuid();
}

std::string DungeonClearPartyTankValue::Format()
{
    Player* tank = ObjectAccessor::FindPlayer(Get());
    return tank ? tank->GetName() : "<none>";
}
