#pragma once

class Player;
class PlayerbotMgr;
class ChatHandler;
class PerformanceMonitorOperation;

#include <memory>

class PlayerbotAIBase
{
public:
    PlayerbotAIBase();

public:
    bool IsActive() const;
    virtual void UpdateAI(uint32 elapsed);
    
    uint32 GetAIInternalUpdateDelay() const { return aiInternalUpdateDelay; }

    // mod-playerbots spelling of SetAIInternalUpdateDelay: how long before this
    // AI is asked again. Public because it is public there and module code
    // outside the class calls it; the protected original stays as it is.
    void SetNextCheckDelay(const uint32 delay);

protected:
    virtual void UpdateAIInternal(uint32 elapsed, bool minimal = false);
    bool CanUpdateAIInternal() const { return aiInternalUpdateDelay < 100U; }
    void SetAIInternalUpdateDelay(const uint32 delay);
    void ResetAIInternalUpdateDelay() { aiInternalUpdateDelay = 0U; }
    void IncreaseAIInternalUpdateDelay(uint32 delay);
    void YieldAIInternalThread(bool minimal = false);
    
protected:
	uint32 aiInternalUpdateDelay;

    std::unique_ptr<PerformanceMonitorOperation> totalPmo;
};
