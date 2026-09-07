// Native boundary fragments with deterministic dependencies, not live gameplay.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using uint32 = std::uint32_t;
static void require(bool v) { if (!v) std::abort(); }

struct Vector3 { float x, y, z; Vector3(float a, float b, float c) : x(a), y(b), z(c) {} };
struct PathOwner { int calls = 0; void GetSafePosition(float& x, float& y, float& z, void*) { ++calls; x=1; y=2; z=3; } };
enum { PATHFIND_NORMAL = 1, PATHFIND_NOPATH = 8 };
struct PathInfo {
    PathOwner* m_sourceUnit = nullptr;
    void* m_transport = nullptr;
    int m_type = PATHFIND_NORMAL;
    std::vector<int> m_pathPoints{1,2};
    int pathCalls = 0;
    bool calculate(float,float,float,bool,bool);
    bool calculate(Vector3 const&,Vector3,bool,bool);
};
#include "ForkPathFloat.inc"
#include "ForkPathVectorPrefix.inc"
    ++pathCalls; // Stand-in for the unchanged native path solver after the guard.
    return true;
}

enum ToChannel { TO_WORLD };
struct PlayerbotAI {};
struct { bool enableBroadcasts = true; } sPlayerbotAIConfig;
struct BroadcastHelper {
    static bool BroadcastToChannelWithGlobalChance(PlayerbotAI*,std::string,std::list<std::pair<ToChannel,uint32>>);
};
static int sends = 0;
#include "ForkBroadcastPrefix.inc"
    ++sends; // Gate test only; channel routing itself is not mocked as verified.
    return true;
}

namespace WorldTimer {
static uint32 now = 0;
uint32 getMSTime() { return now; }
uint32 getMSTimeDiff(uint32 before,uint32 after) { return after-before; }
}
enum { ALIVE=0, DEAD=1 };
struct Pet { int state = ALIVE; };
struct Bot {
    Pet* pet = nullptr;
    Pet* GetPet() { return pet; }
    uint32 GetGUIDLow() { return 34; }
    uint32 GetPetGuid() { return pet ? 7 : 0; }
};
struct QueryResult {};
struct {
    int calls=0; bool exists=false;
    QueryResult* PQuery(const char*,uint32) { ++calls; return exists ? new QueryResult : nullptr; }
} CharacterDatabase;
struct Facade { int GetDeathState(Pet* pet) { return pet->state; } } facade;
#define sServerFacade facade
struct PetIsDeadValue {
    Bot* bot;
    bool petDbCached=false, hasStoredPet=false;
    uint32 lastPetDbCheckMs=0;
    bool Calculate();
};
#include "ForkPetDead.inc"

// Banner-capture implementation is retained from current upstream. Its old
// fork-specific fragment test does not describe that implementation.

int main() {
    PathInfo path;
    require(!path.calculate(4,5,6,false,false));
    require(path.m_pathPoints.empty() && path.m_type==PATHFIND_NOPATH && path.pathCalls==0);
    path.m_pathPoints={9}; path.m_type=PATHFIND_NORMAL;
    require(!path.calculate(Vector3(1,2,3),Vector3(4,5,6),true,true));
    require(path.m_pathPoints.empty() && path.m_type==PATHFIND_NOPATH && path.pathCalls==0);
    PathOwner owner; path.m_sourceUnit=&owner;
    require(path.calculate(4,5,6,true,true) && path.pathCalls==1 && owner.calls==1);

    sPlayerbotAIConfig.enableBroadcasts=false;
    require(!BroadcastHelper::BroadcastToChannelWithGlobalChance(nullptr,"hello",{}) && sends==0);
    sPlayerbotAIConfig.enableBroadcasts=true;
    require(!BroadcastHelper::BroadcastToChannelWithGlobalChance(nullptr,"",{}) && sends==0);
    require(BroadcastHelper::BroadcastToChannelWithGlobalChance(nullptr,"hello",{}) && sends==1);

    Bot bot; Pet pet; PetIsDeadValue value{&bot};
    require(!value.Calculate() && CharacterDatabase.calls==1);
    WorldTimer::now=29999; require(!value.Calculate() && CharacterDatabase.calls==1);
    CharacterDatabase.exists=true; WorldTimer::now=30000;
    require(value.Calculate() && CharacterDatabase.calls==2);
    bot.pet=&pet; require(!value.Calculate() && !value.petDbCached && CharacterDatabase.calls==2);
    pet.state=DEAD; require(value.Calculate() && CharacterDatabase.calls==2);
    pet.state=ALIVE; require(!value.Calculate() && CharacterDatabase.calls==2);
    bot.pet=nullptr; require(value.Calculate() && CharacterDatabase.calls==3);
    value.petDbCached=false; WorldTimer::now=0xfffffff0u;
    require(value.Calculate() && CharacterDatabase.calls==4);
    WorldTimer::now=10; require(value.Calculate() && CharacterDatabase.calls==4);
    WorldTimer::now=30000; require(value.Calculate() && CharacterDatabase.calls==5);

    std::cout << "Fork boundary regressions passed\n";
}
