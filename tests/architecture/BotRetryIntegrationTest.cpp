#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
using uint32 = uint32_t; using uint64 = uint64_t; using int32 = int32_t;
#define CHECK(x) do { if (!(x)) { std::cerr << "failed line " << __LINE__ << ": " #x << '\n'; std::exit(1); } } while (0)
struct Config { uint32 failedActionRetryBase=250, failedActionRetryMax=2000, failedActionCacheTtl=30000, failedActionCacheMaxEntries=64; } sPlayerbotAIConfig;
namespace WorldTimer { uint32 now=100; uint32 getMSTime(){return now;} uint32 getMSTimeDiff(uint32 a,uint32 b){return b-a;} }
enum ActionResult { ACTION_RESULT_IMPOSSIBLE, ACTION_RESULT_FAILED };
enum class BotState { BOT_STATE_NON_COMBAT, BOT_STATE_COMBAT };
constexpr float ACTION_HIGH=100;
struct Player {
    bool combat=false, alive=true; float x=0,y=0,z=0; uint32 money=10,health=100,power=100;
    bool IsInCombat() const{return combat;} bool IsAlive() const{return alive;}
    float GetPositionX() const{return x;} float GetPositionY() const{return y;} float GetPositionZ() const{return z;}
    uint32 GetMoney() const{return money;} uint32 GetHealth() const{return health;}
    int GetPowerType() const{return 0;} uint32 GetPower(int)const{return power;}
};
struct AI {Player p; bool human=false,real=false; Player* GetBot(){return &p;} bool HasRealPlayerMaster()const{return human;} bool IsRealPlayer()const{return real;} };
struct Action {int id=0; bool reaction=false; float relevance=1; bool IsReaction()const{return reaction;} float getRelevance()const{return relevance;} };
struct Event {Player* owner=nullptr; std::vector<int> packet; Player* getOwner(){return owner;} auto& getPacket(){return packet;} };
class Engine {
public:
    AI* ai; BotState state=BotState::BOT_STATE_NON_COMBAT;
    struct FailureState {uint32 failures=0,retryAfter=0,lastFailure=0;};
    std::unordered_map<std::string,FailureState> actionFailures;
    uint32 lastActionFailurePrune=0;
    float failureX=0,failureY=0,failureZ=0;
    uint32 failureMoney=0,failureHealth=0,failurePower=0;
    inline static std::atomic<uint64> suppressedImpossibleActions{0},suppressedFailedActions{0},actionFailureCacheEntries{0},actionFailureCachePeakEntries{0},expiredActionFailureEntries{0},evictedActionFailureEntries{0};
    explicit Engine(AI* p):ai(p){} ~Engine(){ClearActionFailures();}
    std::string GetFailureKey(Action* a,const Event&,ActionResult reason) const {return std::to_string(a->id)+":"+std::to_string(reason);}
    static void UpdateActionFailureCachePeak(uint64 value);
    bool IsFailureBackedOff(Action*,const Event&,ActionResult)const;
    void RecordFailure(Action*,const Event&,ActionResult);
    void ClearFailures(Action*,const Event&);
    void PruneActionFailures(uint32,bool=false);
    void ClearActionFailures();
    bool AllowBackgroundRetry(Action*,Event&)const;
    void RefreshFailureContext();
};
#include "BotRetryPeak.inc"
#include "BotRetryCache.inc"
#include "BotRetryPolicy.inc"
int main() {
    AI ai; Engine e(&ai); Action a; Event event;
    CHECK(e.AllowBackgroundRetry(&a,event));
    ai.human=true; CHECK(!e.AllowBackgroundRetry(&a,event)); ai.human=false;
    ai.real=true; CHECK(!e.AllowBackgroundRetry(&a,event)); ai.real=false;
    ai.p.combat=true; CHECK(!e.AllowBackgroundRetry(&a,event)); ai.p.combat=false;
    e.state=BotState::BOT_STATE_COMBAT; CHECK(!e.AllowBackgroundRetry(&a,event)); e.state=BotState::BOT_STATE_NON_COMBAT;
    a.reaction=true; CHECK(!e.AllowBackgroundRetry(&a,event)); a.reaction=false;
    a.relevance=ACTION_HIGH; CHECK(!e.AllowBackgroundRetry(&a,event)); a.relevance=1;
    event.owner=&ai.p; CHECK(!e.AllowBackgroundRetry(&a,event)); event.owner=nullptr;
    event.packet.push_back(1); CHECK(!e.AllowBackgroundRetry(&a,event)); event.packet.clear();
    e.RecordFailure(&a,event,ACTION_RESULT_FAILED);
    CHECK(e.IsFailureBackedOff(&a,event,ACTION_RESULT_FAILED));
    WorldTimer::now+=249; CHECK(e.IsFailureBackedOff(&a,event,ACTION_RESULT_FAILED));
    ++WorldTimer::now; CHECK(!e.IsFailureBackedOff(&a,event,ACTION_RESULT_FAILED));
    e.RecordFailure(&a,event,ACTION_RESULT_FAILED);
    CHECK(e.actionFailures.begin()->second.retryAfter-WorldTimer::now==500);
    for(int i=0;i<8;++i) e.RecordFailure(&a,event,ACTION_RESULT_FAILED);
    CHECK(e.actionFailures.begin()->second.retryAfter-WorldTimer::now==2000);
    Action other; other.id=1; CHECK(!e.IsFailureBackedOff(&other,event,ACTION_RESULT_FAILED));
    e.ClearFailures(&a,event); CHECK(e.actionFailures.empty());
    sPlayerbotAIConfig.failedActionCacheMaxEntries=2;
    for(int i=0;i<20;++i){a.id=i; ++WorldTimer::now; e.RecordFailure(&a,event,ACTION_RESULT_FAILED); CHECK(e.actionFailures.size()<=2);}
    CHECK(Engine::actionFailureCacheEntries==2); CHECK(Engine::evictedActionFailureEntries==18);
    WorldTimer::now+=30001; e.PruneActionFailures(WorldTimer::now,true); CHECK(e.actionFailures.empty());
    e.RefreshFailureContext(); e.RecordFailure(&a,event,ACTION_RESULT_FAILED);
    ai.p.x+=1; e.RefreshFailureContext(); CHECK(e.actionFailures.empty());
    e.RecordFailure(&a,event,ACTION_RESULT_FAILED); ++ai.p.money; e.RefreshFailureContext(); CHECK(e.actionFailures.empty());
    e.RecordFailure(&a,event,ACTION_RESULT_FAILED); --ai.p.power; e.RefreshFailureContext(); CHECK(e.actionFailures.empty());
    e.RecordFailure(&a,event,ACTION_RESULT_FAILED); ai.human=true; e.RefreshFailureContext(); CHECK(e.actionFailures.empty()); ai.human=false;
    WorldTimer::now=0xfffffff0u; e.RecordFailure(&a,event,ACTION_RESULT_FAILED); CHECK(e.IsFailureBackedOff(&a,event,ACTION_RESULT_FAILED));
    WorldTimer::now+=250; CHECK(!e.IsFailureBackedOff(&a,event,ACTION_RESULT_FAILED));
    sPlayerbotAIConfig.failedActionRetryBase=0; CHECK(!e.AllowBackgroundRetry(&a,event)); e.RefreshFailureContext(); CHECK(e.actionFailures.empty());
    CHECK(Engine::actionFailureCacheEntries==0);
    std::cout << "Native CMaNGOS retry cache + Turtle eligibility policy passed\n";
}
