#include "TransferController.hpp"
#include "Config.hpp"

namespace HttpApi
{
    void RegisterControllers()
    {
        new TransferController(sConfig.GetStringDefault("HttpApi.TransferKey", ""));
    }
}
