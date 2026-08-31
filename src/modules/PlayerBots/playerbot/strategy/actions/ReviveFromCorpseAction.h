#pragma once
#include "MovementActions.h"

namespace ai
{
	class ReviveFromCorpseAction : public MovementAction 
    {
	public:
		ReviveFromCorpseAction(PlayerbotAI* ai) : MovementAction(ai, "revive from corpse") {}
        virtual bool Execute(Event& event) override;
    };

    class FindCorpseAction : public MovementAction 
    {
    public:
        FindCorpseAction(PlayerbotAI* ai) :
            MovementAction(ai, "find corpse"), trackedCorpseGuid(), trackedCorpseGhostTime(0),
            moveToFailures(0), lastMoveToFailureTime(0), lastRecoveryAttemptTime(0),
            moveRetryPending(false), recoveryPending(false), recoveryAttempted(false) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;

    private:
        static const uint32 MAX_MOVE_TO_FAILURES = 5;
        static const uint32 MOVE_TO_RETRY_DELAY = 2 * IN_MILLISECONDS;
        static const uint32 RECOVERY_RETRY_DELAY = 30 * IN_MILLISECONDS;

        bool TryUnreachableCorpseRecovery();

        void ResetFailureState()
        {
            trackedCorpseGuid = ObjectGuid();
            trackedCorpseGhostTime = 0;
            moveToFailures = 0;
            lastMoveToFailureTime = 0;
            lastRecoveryAttemptTime = 0;
            moveRetryPending = false;
            recoveryPending = false;
            recoveryAttempted = false;
        }

        void ResetMoveFailures()
        {
            moveToFailures = 0;
            lastMoveToFailureTime = 0;
            lastRecoveryAttemptTime = 0;
            moveRetryPending = false;
            recoveryPending = false;
            recoveryAttempted = false;
        }

        ObjectGuid trackedCorpseGuid;
        time_t trackedCorpseGhostTime;
        uint32 moveToFailures;
        uint32 lastMoveToFailureTime;
        uint32 lastRecoveryAttemptTime;
        bool moveRetryPending;
        bool recoveryPending;
        bool recoveryAttempted;
    };

	class SpiritHealerAction : public MovementAction
    {
	public:
	    SpiritHealerAction(PlayerbotAI* ai, std::string name = "spirit healer") : MovementAction(ai,name) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };
}
