#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <list>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
using uint32 = uint32_t; using uint16 = uint16_t; using ObjectGuid = uint32;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x << '\n'; std::exit(1); } } while (0)
#define MANGOS_ASSERT(x) CHECK(x)
constexpr int VERTEX_SIZE=3, NAV_GROUND=1, NAV_MAGMA_SLIME=2, NAV_GROUND_STEEP=4, NAV_WATER=8;
using dtPolyRef=uint32; using dtStatus=uint32;
constexpr dtPolyRef INVALID_POLYREF=0;
bool dtStatusSucceed(dtStatus s) { return s==1; }
struct dtQueryFilter { void setIncludeFlags(uint16){} void setExcludeFlags(uint16){} };
struct dtNavMeshQuery {
    bool attached=true, write=true; dtStatus status=1; dtPolyRef poly=1;
    float point[3]={21,31,11};
    const void* getAttachedNavMesh() const { return attached?this:nullptr; }
    dtStatus findNearestPoly(float*,float*,dtQueryFilter*,dtPolyRef* p,float* out) const {
        *p=poly; if(write) for(int i=0;i<3;++i) out[i]=point[i]; return status;
    }
} query;
namespace MMAP {
struct MMapManager { bool available=true; const dtNavMeshQuery* GetNavMeshQuery(uint32,uint32){return available?&query:nullptr;} } manager;
struct MMapFactory {static MMapManager* createOrGetMMapManager(){return &manager;}};
}
constexpr int TYPEID_UNIT=3, TYPEID_PLAYER=4, IDLE_MOTION_TYPE=0, GRIND_ACTIVITY=1;
constexpr float INTERACTION_DISTANCE=5;
struct Motion {int pauses=0; int GetCurrentMovementGeneratorType(){return 1;} void PauseWaypoints(uint32){++pauses;}};
struct Unit {virtual ~Unit()=default; virtual int GetTypeId(){return TYPEID_PLAYER;} bool IsMoving(){return true;} Motion motion; Motion* GetMotionMaster(){return &motion;}};
using WorldObject=Unit;
struct Creature : Unit {int GetTypeId() override{return TYPEID_UNIT;} uint32 GetInteractionPauseTimer(){return 100;}};
struct PlayerbotAI;
struct Player : Unit {PlayerbotAI* brain=nullptr; bool safe=true; uint32 GetInstanceId(){return 0;} float GetDistance(Unit*){return 1;}};
struct GuidPosition {
    uint32 id=1; bool present=true;
    operator uint32() const{return id;}
    float distance(Player*){return 100;}
    Unit* GetWorldObject(uint32){static Creature c;return present?&c:nullptr;}
};
struct WorldPosition {
    float coord_x=10,coord_y=20,coord_z=30; uint32 map=0;
    WorldPosition(uint32 m,float x,float y,float z):coord_x(x),coord_y(y),coord_z(z),map(m){}
    WorldPosition(GuidPosition const&){}
    uint32 getMapId() const{return map;}
    float getX() const{return coord_x;} float getY() const{return coord_y;} float getZ() const{return coord_z;}
    float distance(Player*) const{return 10;}
    bool ClosestCorrectPoint(float,float,uint32);
};
#include "BotRpgPoint.inc"
enum class BotState { BOT_STATE_NON_COMBAT };
struct PlayerbotAI {
    bool master=false,active=true; GuidPosition target;
    bool HasRealPlayerMaster(){return master;} bool IsSafe(Player* p){return p->safe;}
    bool AllowActivity(int){return active;}
    bool HasStrategy(const char*,BotState){return false;}
    void TellDebug(Player*,std::string,const char*){} void TellPlayerNoFacing(Player*,const char*){}
};
PlayerbotAI* GetBotAI(Player* p){return p->brain;}
namespace ai { namespace botdiag { void TraceBehavior(PlayerbotAI*, const char*, const char*) {} } }
struct {std::unordered_map<ObjectGuid,Player*> players; int lookups=0; Player* GetPlayer(ObjectGuid id){++lookups;auto i=players.find(id);return i==players.end()?nullptr:i->second;}} sObjectMgr;
#define PAI_VALUE(type,name) player->brain->target
struct ChooseRpgTargetAction {PlayerbotAI* ai; std::unordered_map<ObjectGuid,uint32> GetTargetCounts(std::list<ObjectGuid> const&);};
#include "BotRpgCrowd.inc"
struct {float sightDistance=60;} sPlayerbotAIConfig;
struct Chat {std::string formatWorldobject(Unit*){return "target";}} chatObject;
std::set<ObjectGuid> ignored; GuidPosition rpgTarget;
template<class T> T& MockValue();
template<> std::set<ObjectGuid>& MockValue<std::set<ObjectGuid>&>() { return ignored; }
template<> GuidPosition& MockValue<GuidPosition>() { return rpgTarget; }
#define AI_VALUE(type,name) MockValue<type>()
#define RESET_AI_VALUE(type,name) (rpgTarget.id=0)
struct MoveToRpgTargetAction {
    Player* bot; PlayerbotAI* ai; Chat* chat=&chatObject; int moves=0; bool moveResult=true;
    float movedX=0,movedY=0,movedZ=0;
    Player* GetMaster(){return nullptr;}
    bool MoveTo(uint32,float x,float y,float z,bool,bool){++moves;movedX=x;movedY=y;movedZ=z;return moveResult;}
    bool Approach(Unit* unit){
        GuidPosition guidP=rpgTarget; float x=10,y=20,z=30; uint32 mapId=0;
#include "BotRpgApproach.inc"
        return couldMove;
    }
    bool isUseful();
};
#include "BotRpgUsefulPrefix.inc"
    return true;
}
int main(){
    WorldPosition p(0,10,20,30);
    CHECK(p.ClosestCorrectPoint(5,5,0)); CHECK(p.getX()==11&&p.getY()==21&&p.getZ()==31);
    for(int failure=0;failure<5;++failure){
        query={};MMAP::manager.available=true;p=WorldPosition(0,10,20,30);
        if(failure==0){query.status=0;query.write=false;}
        if(failure==1)query.poly=0;
        if(failure==2)query.point[0]=std::numeric_limits<float>::quiet_NaN();
        if(failure==3)query.attached=false;
        if(failure==4)MMAP::manager.available=false;
        CHECK(!p.ClosestCorrectPoint(5,5,0));CHECK(p.getX()==10&&p.getY()==20&&p.getZ()==30);
    }
    query={};MMAP::manager.available=true;
    Player player;PlayerbotAI ai;Creature npc;MoveToRpgTargetAction action{&player,&ai};
    CHECK(action.Approach(&player));CHECK(player.motion.pauses==0);CHECK(action.movedX==11&&action.movedY==21&&action.movedZ==31);
    CHECK(action.Approach(&npc));CHECK(npc.motion.pauses==1);
    int before=action.moves;query.status=0;rpgTarget.id=7;ignored.clear();
    CHECK(!action.Approach(&npc));CHECK(action.moves==before&&rpgTarget.id==0&&ignored.count(7));
    query={};query.point[1]=50;rpgTarget.id=8;
    CHECK(!action.Approach(&npc));CHECK(action.moves==before&&rpgTarget.id==0&&ignored.count(8));
    query={};rpgTarget.id=9;action.moveResult=false;
    CHECK(!action.Approach(&npc));CHECK(rpgTarget.id==0&&ignored.count(9));
    rpgTarget.id=1;rpgTarget.present=false;CHECK(!action.isUseful());CHECK(rpgTarget.id==0);
    rpgTarget={};CHECK(action.isUseful());
    ChooseRpgTargetAction choose{&ai};std::vector<Player> players(1000);std::vector<PlayerbotAI> brains(1000);std::list<ObjectGuid> nearby;
    for(uint32 i=0;i<1000;++i){players[i].brain=&brains[i];brains[i].target.id=i%2?12:13;sObjectMgr.players[i+1]=&players[i];nearby.push_back(i+1);}
    auto counts=choose.GetTargetCounts(nearby);CHECK(counts[12]==500&&counts[13]==500);CHECK(sObjectMgr.lookups==1000);
    players[0].safe=false;players[1].brain=nullptr;brains[2].active=false;brains[3].target.id=0;nearby.push_back(1001);
    counts=choose.GetTargetCounts(nearby);CHECK(counts[12]==498&&counts[13]==498);CHECK(sObjectMgr.lookups==2001);
    ai.master=true;before=sObjectMgr.lookups;CHECK(choose.GetTargetCounts(nearby).empty());CHECK(sObjectMgr.lookups==before);
    std::cout<<"Native RPG approach, failed nav query, stale target and 1000-neighbor crowd regressions passed\n";
}
