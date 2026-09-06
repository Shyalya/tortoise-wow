// Production dispatch + native MovePath and point-generator initialization/update.
// Spline transport, unit data and hazard geometry are deterministic stand-ins.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using uint32 = uint32_t;
#define DEBUG_FILTER_LOG(...) ((void)0)
enum ForcedMovement { FORCED_MOVEMENT_WALK, FORCED_MOVEMENT_RUN, FORCED_MOVEMENT_FLIGHT };
enum { MOVE_WALK_MODE=1, MOVE_RUN_MODE=2, MOVE_PATHFINDING=4, MOVE_FLY_MODE=8,
       MOVE_FALLING=16, MOVE_CYCLIC=32, UNIT_STAT_ROAMING=64,
       UNIT_STAT_ROAMING_MOVE=128, UNIT_STAT_CAN_NOT_MOVE=256 };
namespace G3D { struct Vector3 { float x=0,y=0,z=0; }; }
namespace Movement { using PointsArray=std::vector<G3D::Vector3>; }
struct Player;
struct Transport
{
    void CalculatePassengerPosition(float&,float&,float&) {}
    void CalculatePassengerOffset(float&,float&,float&) {}
};
struct Spline
{
    bool done=true,walk=false;
    uint32 launches=0;
    Movement::PointsArray points;
    bool Finalized() const { return done; }
};
template<class T> struct PointMovementGenerator
{
    float m_x,m_y,m_z,m_speed=0,m_o=-10;
    uint32 m_options;
    bool m_recalculateSpeed=false;
    PointMovementGenerator(float x,float y,float z,uint32 options):m_x(x),m_y(y),m_z(z),m_options(options){}
    void Initialize(T&);
    bool Update(T&,uint32 const&);
};
struct MotionMaster
{
    Player* m_owner;
    uint32 clears=0;
    std::unique_ptr<PointMovementGenerator<Player>> point;
    void Clear(){++clears;point.reset();}
    void MovePoint(uint32,float,float,float,uint32,float=0,float=-10);
    void MovePath(Movement::PointsArray const&,uint32,bool,bool=false);
};
struct Player
{
    G3D::Vector3 position{1,2,3};
    MotionMaster mm{this};
    Spline spline;
    Spline* movespline=&spline;
    bool freeFlying=false,blocked=false;
    uint32 options=0;
    MotionMaster* GetMotionMaster(){return &mm;}
    bool IsFreeFlying() const{return freeFlying;}
    bool IsFlying() const{return freeFlying;}
    bool IsStopped() const{return spline.done;}
    void StopMoving(){spline.done=true;}
    bool HasUnitState(uint32) const{return blocked;}
    void AddUnitState(uint32){}
    void ClearUnitState(uint32){}
    Transport* GetTransport(){return nullptr;}
    void UpdateAllowedPositionZ(float&,float&,float&){}
};
namespace Movement
{
    struct MoveSplineInit
    {
        Player& owner;
        PointsArray points;
        bool walk=false;
        MoveSplineInit(Player& p,const char*):owner(p){}
        void MovebyPath(const PointsArray& path){points=path;}
        void MoveTo(float x,float y,float z,uint32 options)
        {owner.options=options;points={owner.position,{x,y,z}};}
        void SetWalk(bool value){walk=value;}
        void SetVelocity(float){}
        void SetFly(){}
        void SetFall(){}
        void SetCyclic(){}
        void SetFacing(float){}
        void Launch()
        {
            if(points.empty())throw std::runtime_error("empty spline");
            // Native Launch replaces the first point with the live position.
            points.front()=owner.position;
            owner.spline.points=points;owner.spline.walk=walk;
            owner.spline.done=false;++owner.spline.launches;
        }
    };
}
#include "BotNativePointInit.inc"
#include "BotNativePointUpdate.inc"
#include "BotNativeMovePath.inc"
void MotionMaster::MovePoint(uint32,float x,float y,float z,uint32 options,float,float)
{point=std::make_unique<PointMovementGenerator<Player>>(x,y,z,options);point->Initialize(*m_owner);}

struct WorldPosition
{
    G3D::Vector3 value;
    WorldPosition()=default;
    WorldPosition(Player* bot):value(bot->position){}
    WorldPosition(float x,float y,float z):value{x,y,z}{}
    float getX()const{return value.x;}float getY()const{return value.y;}float getZ()const{return value.z;}
    uint32 getMapId()const{return 0;}
    float distance(Player* p)const
    {return std::sqrt(std::pow(getX()-p->position.x,2)+std::pow(getY()-p->position.y,2)+std::pow(getZ()-p->position.z,2));}
    float getPathLength(const std::vector<WorldPosition>& p)const{return float(p.size()*5);}
    Movement::PointsArray toPointsArray(const std::vector<WorldPosition>& p)const
    {Movement::PointsArray result;for(auto& v:p)result.push_back(v.value);return result;}
};
struct TravelPath {std::vector<WorldPosition> points;std::vector<WorldPosition> getPointPath()const{return points;}};
struct MovementAction
{
    Player* bot;bool hazard=false;uint32 avoids=0,waits=0;
    void DispatchMovement(TravelPath,bool,bool);
    void GeneratePathAvoidingHazards(std::vector<WorldPosition>& path)
    {++avoids;if(hazard&&path.size()>2)path[1].value.y+=10;}
    void WaitForReach(float){++waits;}
};
#include "BotMoveDispatch.inc"
void Check(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
int main()
{
    try
    {
        for(bool walk:{false,true})for(bool generate:{false,true})for(bool flying:{false,true})
        {
            Player bot;bot.freeFlying=flying;MovementAction action{&bot};
            action.DispatchMovement({{{5,2,3},{9,3,3},{15,2,3}}},generate,walk);
            Check(bot.spline.launches==1,"one move request launched multiple competing splines");
            Check(bot.spline.walk==walk,"requested walking mode was overwritten");
            const bool direct=!generate||flying;
            Check(bool(bot.mm.point)==direct,"wrong movement owner for direct/generated path");
            Check(action.avoids==(direct?0u:1u),"hazard processing crossed the direct movement branch");
            if(direct)
            {
                bot.mm.point->m_recalculateSpeed=true;
                bot.mm.point->Update(bot,1);
                Check(bot.spline.launches==2,"native point movement must process speed changes");
                Check(bool(bot.options&MOVE_PATHFINDING)==generate,"native pathfinding option lost");
            }
            else
            {
                Check(bot.spline.points.size()==4,"the first route vertex was lost at spline launch");
                Check(bot.spline.points[1].x==5,"first route segment was overwritten");
                Check(!bot.mm.point,"a stale point generator can restart the replaced path");
            }
            bot.StopMoving();action.DispatchMovement({{{20,2,3}}},generate,walk);
            Check(bool(bot.mm.point),"a one-point path must use the native point generator");
            Check(bot.spline.points.back().x==20,"short path did not reach requested destination");
            auto launches=bot.spline.launches;auto clears=bot.mm.clears;
            action.DispatchMovement({},generate,walk);
            Check(bot.spline.launches==launches&&bot.mm.clears==clears,"empty path must not interrupt current movement");
        }
        Player bot;MovementAction action{&bot};action.hazard=true;
        action.DispatchMovement({{{5,2,3},{9,2,3},{15,2,3}}},true,false);
        Check(bot.spline.launches==1,"hazard route double launch");
        Check(bot.spline.points[1].y==12,"hazard detour was discarded");
        std::cout<<"Native bot movement dispatch boundaries passed.\n";
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
