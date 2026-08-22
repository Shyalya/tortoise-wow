#pragma once

#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/NamedObjectContext.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelNode.h"
#include "Movement/WaypointMovementGenerator.h"
#include "playerbot/strategy/values/HazardsValue.h"
#include "playerbot/strategy/values/LastMovementValue.h"

// Move priorities, for code ported from mod-playerbots, which arbitrates moves
// by them. Nothing in this tree arbitrates: MoveTo takes the move and issues
// it. The type exists so ported call sites keep their shape, and every place
// that drops the value says so.
enum class MovementPriority : uint8
{
    MOVEMENT_IDLE   = 0,
    MOVEMENT_NORMAL = 1,
    MOVEMENT_COMBAT = 2,
    MOVEMENT_FORCED = 3
};

namespace ai
{
    class MovementAction : public Action
    {
    public:
        MovementAction(PlayerbotAI* ai, std::string name) : Action(ai, name) {}

        static bool MinimalMove(PlayerbotAI* ai);
        static bool UseTransport(PlayerbotAI* ai, uint32 entry, WorldPosition dockPosition, WorldPosition exitPosition, bool doTeleport);
    protected:
        static bool MoveOnTransport(PlayerbotAI* ai, GenericTransport* transport, bool doTeleport);
        static bool MoveOffTransport(PlayerbotAI* ai, WorldPosition exitPos, bool doTeleport);

        bool ChaseTo(WorldObject *obj, float distance = 0.0f, float angle = 0.0f);
        bool MoveNear(uint32 mapId, float x, float y, float z, float distance = sPlayerbotAIConfig.contactDistance);
        bool FlyDirect(const WorldPosition &startPosition,  const WorldPosition &endPosition , WorldPosition& movePosition, TravelPath movePath);

        inline bool MoveTo(const WorldLocation& location, bool idle = false, bool react = false, bool noPath = false, bool ignoreEnemyTargets = false)
        {
           return MoveTo(location.mapid, location.coord_x, location.coord_y, location.coord_z, idle, react, noPath, ignoreEnemyTargets);
        }

        static bool UseTaxi(PlayerbotAI* ai, uint32 entry = 0, bool needNpc = true);


        bool WaitForTransport();


        TravelPath ResolveMovePath(const WorldPosition& startPosition,
            const WorldPosition& endPosition,
            Unit* mover,
            LastMovement& lastMove);

        bool HandleSpecialMovement(TravelPath& path);

        void UpdateFlyingState(WorldPosition& movePosition,
            float totalDistance,
            float originalZ,
            float maxDist,
            bool isWalking);

        void DispatchMovement(TravelPath movePath, bool generatePath, bool masterWalking);

        Unit* GetMover(Player* bot);

        bool MoveTo2(const WorldPosition& endPos, bool idle = false, bool react = false, bool noPath = false, bool ignoreEnemyTargets = false);

        bool MoveTo(uint32 mapId, float x, float y, float z, bool idle = false, bool react = false, bool noPath = false, bool ignoreEnemyTargets = false);

        // mod-playerbots carries four more arguments on this call. Three are
        // accepted and dropped - exact_waypoint, lessDelay and backwards have no
        // counterpart in this generator - and the priority goes the way every
        // other priority here goes, see MovementPriority. normal_only maps onto
        // noPath, which is the same request: do not route, go straight.
        bool MoveTo(uint32 mapId, float x, float y, float z, bool idle, bool react,
                    bool normal_only, bool /*exact_waypoint*/,
                    MovementPriority /*priority*/, bool /*lessDelay*/ = false,
                    bool /*backwards*/ = false)
        { return MoveTo(mapId, x, y, z, idle, react, normal_only); }

        // mod-playerbots refuses a move to a spot the bot was just sent to.
        // Nothing here records that, so nothing is a duplicate. False keeps the
        // move flowing, which is the safe direction - see IsWaitingForLastMove.
        bool IsDuplicateMove(float /*x*/, float /*y*/, float /*z*/) const { return false; }
        bool MoveTo(Unit* target, float distance = 0.0f);

        // mod-playerbots spelling: jump the bot to a point. Sits on
        // MotionMaster::MoveJump rather than on JumpAction::JumpTowards, which
        // belongs to a different action class and cannot be reached from here.
        // The mapId is accepted and ignored - a jump never leaves the map, and
        // ported callers pass it because their MoveTo takes one.
        // The priority is dropped; see MovementPriority.
        bool JumpTo(uint32 /*mapId*/, float x, float y, float z,
                    MovementPriority /*priority*/ = MovementPriority::MOVEMENT_NORMAL);

        // mod-playerbots records a wait sized to the last leg and refuses a
        // later move whose priority is not strictly greater. This tree records
        // no such wait - LastMovement carries no timestamp and no priority - so
        // there is nothing to answer from and this says "not waiting".
        //
        // False is the safe answer, not merely the convenient one. The failure
        // it avoids is the one the porting module documents against its own
        // upstream: a leftover wait silently refusing the NEXT leg, leaving the
        // tank standing in the pack it just pulled. Erring the other way only
        // means a move is reissued sooner than upstream would.
        bool IsWaitingForLastMove(MovementPriority /*priority*/ = MovementPriority::MOVEMENT_NORMAL) const { return false; }
        bool MoveNear(WorldObject* target, float distance = sPlayerbotAIConfig.contactDistance);
        float GetFollowAngle();
        bool Follow(Unit* target, float distance = 0);
        bool Follow(Unit* target, float distance, float angle);
        float MoveDelay(float distance);
        bool FollowOnTransport(Unit* target);

        void WaitForReach(float distance);
        void WaitForReach(const Movement::PointsArray& path);

        bool IsMovingAllowed(Unit* target);
        bool IsMovingAllowed(uint32 mapId, float x, float y, float z);
        // mod-playerbots also asks the question with no destination: may this
        // bot move at all right now. Answered against the bot's own position,
        // which is the part of the check that does not depend on a target.
        bool IsMovingAllowed();
        bool Flee(Unit *target);
        void ClearIdleState();
        void UpdateMovementState();

        bool isPossible() override;
        bool isUseful() override;

        void CreateWp(Player* wpOwner, float x, float y, float z, float o, uint32 entry, bool important = false);
        float GetAngle(const float x1, const float y1, const float x2, const float y2);

        // Used when this action is executed as a reaction
        bool ShouldReactionInterruptCast() const override { return true; }
        bool ShouldReactionInterruptMovement() const override { return true; }

    private:
        bool IsValidPosition(const WorldPosition& position, const WorldPosition& visibleFromPosition);
        bool IsHazardNearPosition(const WorldPosition& position, HazardPosition* outHazard = nullptr);
        bool GeneratePathAvoidingHazards(std::vector<WorldPosition>& movePath);
    };

    class FleeAction : public MovementAction
    {
    public:
        FleeAction(PlayerbotAI* ai, float distance = sPlayerbotAIConfig.spellDistance) : MovementAction(ai, "flee"), distance(distance) {}
        virtual bool Execute(Event& event) override;

    private:
        float distance;
    };

    class FleeWithPetAction : public MovementAction
    {
    public:
        FleeWithPetAction(PlayerbotAI* ai) : MovementAction(ai, "flee with pet") {}
        virtual bool Execute(Event& event) override;
    };

    class RunAwayAction : public MovementAction
    {
    public:
        RunAwayAction(PlayerbotAI* ai) : MovementAction(ai, "runaway") {}
        virtual bool Execute(Event& event) override;
    };

    class MoveToLootAction : public MovementAction
    {
    public:
        MoveToLootAction(PlayerbotAI* ai) : MovementAction(ai, "move to loot") {}
        virtual bool Execute(Event& event) override;
    };

    class MoveOutOfEnemyContactAction : public MovementAction
    {
    public:
        MoveOutOfEnemyContactAction(PlayerbotAI* ai) : MovementAction(ai, "move out of enemy contact") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

    class SetFacingTargetAction : public Action
    {
    public:
        SetFacingTargetAction(PlayerbotAI* ai) : Action(ai, "set facing") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        virtual bool isPossible() override;
    };

    class SetBehindTargetAction : public MovementAction
    {
    public:
        SetBehindTargetAction(PlayerbotAI* ai) : MovementAction(ai, "set behind") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        virtual bool isPossible() override;
    };

    class MoveOutOfCollisionAction : public MovementAction
    {
    public:
        MoveOutOfCollisionAction(PlayerbotAI* ai) : MovementAction(ai, "move out of collision") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

    class MoveRandomAction : public MovementAction
    {
    public:
        MoveRandomAction(PlayerbotAI* ai) : MovementAction(ai, "move random") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

    class MoveToAction : public MovementAction, public Qualified
    {
    public:
        MoveToAction(PlayerbotAI* ai, std::string name = "move to") : MovementAction(ai, "name"), Qualified() {}
        virtual bool Execute(Event& event) override;
    };

    class JumpAction : public MovementAction, public Qualified
    {
    public:
        JumpAction(PlayerbotAI* ai) : MovementAction(ai, "jump"), Qualified() {}
        bool Execute(Event& event) override;
        bool isUseful() override;

        static WorldPosition CalculateJumpParameters(const WorldPosition& src, Unit* jumper, float angle, float vSpeed, float hSpeed, float &timeToLand, float &distanceToLand, float &maxHeight, bool &goodLanding, std::vector<WorldPosition> &path, float maxJumpHeight = sPlayerbotAIConfig.jumpHeightLimit);

    private:
        bool DoJump(const WorldPosition& dest, const WorldPosition& highestPoint, float angle, float vSpeed, float hSpeed, float timeToLand, float distanceToLand, float maxHeight, bool goodLanding, bool jumpInPlace, bool jumpBackward, bool showOnly);
        bool JumpTowards(const WorldPosition& src, const WorldPosition& dest, Unit* jumper, float jumpSpeed, bool preSetLanding = false);
        static float CalculateJumpTime(float srcZ, float destZ, float vSpeed, float hSpeed, float distance);
        static float CalculateJumpTime(float z_diff, float vSpeed, bool ascending);

        WorldPosition GetPossibleJumpStartFor(const WorldPosition& src, const WorldPosition& dest, WorldPosition& possiblelanding, Unit* jumper, float &requiredSpeed, float distanceTo, float distanceFrom);
        WorldPosition GetPossibleJumpStartForInRange(const WorldPosition& src, const WorldPosition& dest, WorldPosition& possiblelanding, Unit* jumper, float& requiredSpeed, float distanceTo, float distanceFrom);
        bool CanJumpTo(const WorldPosition& src, const WorldPosition& dest, WorldPosition& possiblelanding, float& jumpAngle, Unit* jumper, float jumpSpeed, float maxDistance = 10.0f);
        bool CanWalkTo(const WorldPosition& src, const WorldPosition& dest, Unit* jumper, float maxDistance = sPlayerbotAIConfig.sightDistance);
        bool IsJumpFasterThanWalking(const WorldPosition& src, const WorldPosition& dest, const WorldPosition& jumpLanding, Unit* jumper, float maxDistance = sPlayerbotAIConfig.sightDistance);

        static bool IsJumpSafe(const WorldPosition& src, const WorldPosition& dest, Unit* jumper);
        static bool CanLand(const WorldPosition& dest, Unit* jumper);
        static bool IsNotMagmaSlime(const WorldPosition& dest, Unit* jumper);
    };
}
