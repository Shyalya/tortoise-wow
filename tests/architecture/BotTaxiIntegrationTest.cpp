#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <vector>
using uint32=uint32_t; using ObjectGuid=uint32;
#define CHECK(x) do {if(!(x)){std::cerr<<"line "<<__LINE__<<": " #x <<'\n';std::exit(1);}}while(0)
constexpr int ALLIANCE=1, UNIT_NPC_FLAG_FLIGHTMASTER=1;
constexpr float INTERACTION_DISTANCE=5;
struct Player;
struct Creature {uint32 node=1; bool interactable=true; float GetPositionX(){return float(node);} float GetPositionY(){return 0;} float GetPositionZ(){return 0;} uint32 GetMapId(){return 0;} float GetAngle(Player*){return 0;} void SetFacingTo(float){} };
struct TaxiPathEntry {uint32 from=1,to=2,price=10;};
struct TaxiNodesEntry {uint32 MountCreatureID[2]={1,1};};
template<class T> struct Store {std::map<uint32,T> rows; const T* LookupEntry(uint32 id){auto i=rows.find(id); return i==rows.end()?nullptr:&i->second;}};
Store<TaxiPathEntry> sTaxiPathStore; Store<TaxiNodesEntry> sTaxiNodesStore;
struct {uint32 GetNearestTaxiNode(float x,float,float,uint32,int){return uint32(x);}} sObjectMgr;
struct TaxiMask {std::set<uint32> known; bool IsTaximaskNodeKnown(uint32 n){return known.count(n)!=0;}};
struct Session {TaxiMask* mask=nullptr; bool learn=true; int learns=0; void SendLearnNewTaxiNode(Creature* c){++learns;if(learn)mask->known.insert(c->node);}};
struct Point {float distance=0; float sqDistance(Player*){return distance*distance;}};
struct Player {
    TaxiMask m_taxi; Session session{&m_taxi}; Creature npc; uint32 money=100; bool cheat=false, activation=true; int activates=0,teleports=0,liveLookups=0,team=ALLIANCE;
    int GetTeam(){return team;} bool isTaxiCheater(){return cheat;}
    Creature* GetNPCIfCanInteractWith(ObjectGuid,int){return npc.interactable?&npc:nullptr;}
    Creature* FindNearestInteractableNpcWithFlag(uint32){++liveLookups;return npc.interactable?&npc:nullptr;}
    Session* GetSession(){return &session;} uint32 GetMoney(){return money;} void SetMoney(uint32 n){money=n;}
    void OnTaxiFlightEject(bool){} void TeleportTo(Point){++teleports;}
    bool ActivateTaxiPathTo(std::vector<uint32> nodes,Creature*,uint32){++activates; CHECK(cheat||(m_taxi.IsTaximaskNodeKnown(nodes[0])&&m_taxi.IsTaximaskNodeKnown(nodes[1]))); if(!activation)return false; money-=10; return true;}
};
enum class BotCheatMask {gold,taxi};
struct AiObjectContext {};
struct PlayerbotAI {
    Player bot; AiObjectContext context; bool gold=false,taxi=false,click=false;
    Player* GetBot(){return &bot;} AiObjectContext* GetAiObjectContext(){return &context;}
    bool HandleSpellClick(uint32){return click;} void Unmount(){}
    bool HasCheat(BotCheatMask m){return m==BotCheatMask::gold?gold:taxi;}
};
std::list<ObjectGuid> taxiNpcs{1};
#define AI_VALUE(type,name) ::taxiNpcs
class MovementAction {public:static bool UseTaxi(PlayerbotAI*,uint32,bool,Creature* sourceNpc=nullptr);};
namespace ai { namespace botdiag { void TraceBehavior(PlayerbotAI*, const char*, const char*, uint32 = 0) {} } }
#include "BotTaxiUse.inc"
enum class PathNodeType {NODE_FLIGHTPATH,NODE_WALK};
struct Step {PathNodeType type=PathNodeType::NODE_FLIGHTPATH;uint32 entry=1;Point point;};
struct Path {std::vector<Step> nodes{{},{PathNodeType::NODE_WALK,0,{}}};int cuts=0;void cutTo(Step,bool){++cuts;}};
struct LastMove {Path lastPath;};
bool minimal(PlayerbotAI* ai,LastMove& lastMove){
    Player* bot=ai->GetBot(); auto& path=lastMove.lastPath.nodes; auto nextStep=path.begin();
    using MovementActionAlias=MovementAction;
    auto UseTaxi=[](PlayerbotAI* a,uint32 n,bool npc){return MovementActionAlias::UseTaxi(a,n,npc);};
#include "BotTaxiLeg.inc"
    return false;
}
int main(){
    sTaxiPathStore.rows[1]={};sTaxiNodesStore.rows[1]={};sTaxiNodesStore.rows[2]={};
    PlayerbotAI ai; ai.bot.m_taxi.known={2};
    // The established nearby-GUID interaction path must win when it can see
    // the source flight master. This is the Southshore/Darla regression.
    CHECK(MovementAction::UseTaxi(&ai,1,false)); CHECK(ai.bot.liveLookups==0); CHECK(ai.bot.session.learns==1); CHECK(ai.bot.money==90);
    // RPG taxi hands the already-validated target NPC through to activation;
    // it must not depend on a second nearby-NPC cache lookup.
    taxiNpcs.clear(); ai.bot.m_taxi.known={2}; int lookups=ai.bot.liveLookups;
    CHECK(MovementAction::UseTaxi(&ai,1,true,&ai.bot.npc)); CHECK(ai.bot.liveLookups==lookups);
    // Callers that have not populated nearby NPCs still get the spatial
    // fallback and can discover the source node.
    taxiNpcs.clear(); ai.bot.m_taxi.known={2};
    CHECK(MovementAction::UseTaxi(&ai,1,false)); CHECK(ai.bot.liveLookups==1); CHECK(ai.bot.session.learns==3);
    taxiNpcs={1};
    ai.bot.m_taxi.known={2};ai.bot.npc.interactable=false;int calls=ai.bot.activates;
    CHECK(!MovementAction::UseTaxi(&ai,1,false));CHECK(ai.bot.activates==calls);
    ai.bot.npc.interactable=true;ai.bot.npc.node=3;
    CHECK(!MovementAction::UseTaxi(&ai,1,false));CHECK(!ai.bot.m_taxi.IsTaximaskNodeKnown(1));
    ai.bot.npc.node=1;ai.bot.session.learn=false;
    CHECK(!MovementAction::UseTaxi(&ai,1,true));CHECK(ai.bot.activates==calls);
    ai.bot.session.learn=true;ai.bot.m_taxi.known={1};
    CHECK(!MovementAction::UseTaxi(&ai,1,true)); CHECK(ai.bot.activates==calls);
    ai.bot.m_taxi.known={1,2};sTaxiNodesStore.rows[2].MountCreatureID[1]=0;
    CHECK(!MovementAction::UseTaxi(&ai,1,true));sTaxiNodesStore.rows[2].MountCreatureID[1]=1;
    ai.bot.activation=false;ai.gold=true;uint32 funds=ai.bot.money;
    CHECK(!MovementAction::UseTaxi(&ai,1,true));CHECK(ai.bot.money==funds);
    LastMove move;CHECK(!minimal(&ai,move));CHECK(move.lastPath.cuts==0);
    ai.bot.activation=true;CHECK(minimal(&ai,move));CHECK(move.lastPath.cuts==1);
    ai.bot.team=0;ai.bot.m_taxi.known={1,2};CHECK(MovementAction::UseTaxi(&ai,1,true));
    ai.bot.cheat=true;ai.bot.m_taxi.known.clear();CHECK(MovementAction::UseTaxi(&ai,1,false));
    sTaxiNodesStore.rows.erase(2);CHECK(!MovementAction::UseTaxi(&ai,1,false));
    ai.click=true;CHECK(MovementAction::UseTaxi(&ai,999,false));
    // Captured live mismatch: the old cached 272 is 71->5, not Southshore.
    // Correcting the graph to native 99 (14->7) must satisfy the existing
    // source/knowledge checks without weakening or bypassing the adapter.
    PlayerbotAI shore; shore.bot.npc.node=14; shore.bot.m_taxi.known={14,7,71,5};
    for (uint32 id : {14u,7u,71u,5u}) sTaxiNodesStore.rows[id]={};
    sTaxiPathStore.rows[272]={71,5,10}; sTaxiPathStore.rows[99]={14,7,10};
    CHECK(!MovementAction::UseTaxi(&shore,272,true)); CHECK(shore.bot.activates==0);
    CHECK(MovementAction::UseTaxi(&shore,99,true)); CHECK(shore.bot.activates==1);
    std::cout<<"Native bot taxi adapter and failed-leg retention passed\n";
}
