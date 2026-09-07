// Execute the complete production trace body, including enabled snapshots.
// The native Duration() body uses a checked stand-in for spline storage so
// violating its initialization precondition fails without crashing a realm.
#include "BoundedBotTrace.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
using uint32=uint32_t;using uint64=uint64_t;using int32=int32_t;
namespace G3D { struct Vector3{float x=0,y=0,z=0;}; }
struct Config{bool behaviorTrace=false;uint32 behaviorTraceMap=0;float behaviorTraceX=0,behaviorTraceY=0,behaviorTraceRadius=120;}sPlayerbotAIConfig;
struct WorldTimer{inline static uint32 now=0;static uint32 getMSTime(){return now;}};
struct Log
{
    uint32 records=0;static Log& Instance(){static Log log;return log;}
    std::string last;
    template<class... Args>void out(int,const char* format,Args... args)
    {
        int size=std::snprintf(nullptr,0,format,args...);
        if(size<0||size>4096)throw std::runtime_error("invalid trace format/size");
        std::vector<char> buffer(size+1);
        std::snprintf(buffer.data(),buffer.size(),format,args...);
        last=buffer.data();++records;
    }
};
constexpr int LOG_PERFORMANCE=0;
struct WorldPosition
{
    uint32 map=0;float x=0,y=0,z=0;
    uint32 getMapId()const{return map;}float getX()const{return x;}float getY()const{return y;}float getZ()const{return z;}
};
struct GuidPosition:WorldPosition
{uint64 GetRawValue()const{return 0;}int32 GetEntry()const{return 0;}};
struct Destination{uint32 GetPurpose()const{return 3;}};
struct TravelTarget
{
    WorldPosition* goal=nullptr;Destination* destination=nullptr;
    WorldPosition* GetPosition(){return goal;}Destination* GetDestination(){return destination;}
    int32 GetStatus(){return 0;}int32 GetEntry(){return 0;}uint32 GetRetryCount(bool){return 0;}int32 GetTimeLeft(){return 0;}
};
struct Step{WorldPosition point;uint32 type=0,entry=0;};
struct Path
{
    std::vector<Step> points;
    bool empty(){return points.empty();}auto& getPath(){return points;}WorldPosition getBack(){return points.back().point;}
};
struct LastMovement{WorldPosition lastMoveShort;Path lastPath;};
template<class T>struct Value{T value{};T Get(){return value;}};
struct AiObjectContext
{
    Value<GuidPosition> rpg;Value<TravelTarget*> travel;LastMovement movement;
    Value<LastMovement&> last{movement};Value<std::string> action;
    template<class T>Value<T>* GetValue(const char*)
    {
        if constexpr(std::is_same_v<T,GuidPosition>)return &rpg;
        else if constexpr(std::is_same_v<T,TravelTarget*>)return &travel;
        else if constexpr(std::is_same_v<T,LastMovement&>)return &last;
        else return &action;
    }
};
struct MoveSpline
{
    struct Storage
    {
        std::vector<int32> lengths;mutable uint32 reads=0;
        int32 length()const
        {
            ++reads;
            if(lengths.empty())throw std::runtime_error("trace read Duration() before the native spline was initialized");
            return lengths.back();
        }
    }spline;
    bool done=true;
    bool Initialized()const{return !spline.lengths.empty();}
    uint32 GetId()const{return 1;}int32 timePassed()const{return 0;}bool Finalized()const{return done;}
    G3D::Vector3 FinalDestination()const{return {};}
#include "BotNativeSplineDuration.inc"
};
struct Guid{uint32 GetCounter()const{return 1;}};
struct Group{Guid GetLeaderGuid(){return {};}};
struct Motion{uint32 GetCurrentMovementGeneratorType(){return 0;}};
struct Player
{
    bool inWorld=true,teleport=false;uint32 map=0,instance=0;float x=0;MoveSpline* movespline=nullptr;
    Group* group=nullptr;Motion motion;
    bool IsInWorld(){return inWorld;}bool IsBeingTeleported(){return teleport;}
    uint32 GetMapId(){return map;}uint32 GetInstanceId(){return instance;}uint32 GetGUIDLow(){return 1;}
    float GetPositionX(){return x;}float GetPositionY(){return 0;}float GetPositionZ(){return 0;}
    const char* GetName(){return "TestBot";}uint32 GetLevel(){return 1;}
    Motion* GetMotionMaster(){return &motion;}bool IsMoving(){return movespline&&!movespline->Finalized();}
    bool IsTaxiFlying(){return IsMoving();}bool IsInCombat(){return false;}bool isAFK(){return false;}
    Group* GetGroup(){return group;}
};
struct PlayerbotAI
{
    Player* bot=nullptr;AiObjectContext context;
    Player* GetBot(){return bot;}Player* GetMaster(){return nullptr;}AiObjectContext* GetAiObjectContext(){return &context;}
};
namespace ai{namespace botdiag{
uint32 taxiProbes=0;
std::string DescribeTaxiInteraction(Player*,uint32,bool){++taxiProbes;return " taxi_path=1 test_probe=1";}
void TraceBehavior(PlayerbotAI*,const char*,const char*,uint32=0);
#include "BotTraceBody.inc"
}}
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main()
{
    try
    {
        Player bot;PlayerbotAI ai;ai.bot=&bot;MoveSpline spline;bot.movespline=&spline;
        auto sample=[&](){WorldTimer::now+=5000;ai::botdiag::TraceBehavior(&ai,"journey","test");};
        sample();Check(Log::Instance().records==0,"disabled trace emitted");
        ai::botdiag::TraceBehavior(&ai,"taxi_reject","test",1);
        Check(ai::botdiag::taxiProbes==0,"disabled trace ran taxi probe");
        sPlayerbotAIConfig.behaviorTrace=true;
        ai::botdiag::TraceBehavior(nullptr,"journey","test");
        ai.bot=nullptr;sample();ai.bot=&bot;
        bot.inWorld=false;sample();bot.inWorld=true;
        bot.teleport=true;sample();bot.teleport=false;
        bot.x=1000;sample();bot.x=0;
        Check(Log::Instance().records==0,"ineligible bot enrolled");
        sample(); // Freshly constructed bot: movespline exists, but no path yet.
        Check(spline.spline.reads==0,"uninitialized spline timing was read");
        Check(Log::Instance().records==1,"fresh bot snapshot missing");
        bot.movespline=nullptr;sample();bot.movespline=&spline;
        spline.spline.lengths={1000};spline.done=false;sample();
        Check(spline.spline.reads==1,"active spline timing not sampled");
        spline.done=true;sample();Check(spline.spline.reads==2,"completed initialized spline lost");
        Group group;bot.group=&group;TravelTarget travel;Destination dest;WorldPosition goal;
        travel.goal=&goal;travel.destination=&dest;ai.context.travel.value=&travel;
        ai.context.movement.lastPath.points.push_back({});sample();
        bot.x=1000;bot.map=1;bot.instance=42;sample();
        Check(Log::Instance().records==6,"sampled bot not retained across map/area exit");
        spline.spline.lengths.clear();sample();Check(spline.spline.reads==4,"cleared spline timing was read");
        WorldTimer::now+=5000;
        ai::botdiag::TraceBehavior(&ai,"taxi_reject","test",1);
        Check(ai::botdiag::taxiProbes==1,"admitted taxi trace did not probe");
        Check(Log::Instance().last.find("taxi_path=1 test_probe=1")!=std::string::npos,"formatted trace lost taxi fields");
        for(int i=0;i<100;++i)ai::botdiag::TraceBehavior(&ai,"taxi_reject","test",1);
        Check(ai::botdiag::taxiProbes<=7,"taxi probes ran beyond per-bot action admission");
        WorldTimer::now+=600001;
        ai::botdiag::TraceBehavior(&ai,"taxi_reject","test",1);
        uint32 probes=ai::botdiag::taxiProbes;
        ai::botdiag::TraceBehavior(&ai,"taxi_reject","test",1);
        Check(ai::botdiag::taxiProbes==probes,"expired trace continued probing");
        std::cout<<"Production trace snapshots passed: disabled, absent, fresh, active, finished, cleared and transferred.\n";
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
