// Execute the production NPC predicate and taxi trace formatter. Map storage,
// reputation and DBC lookups are deterministic stand-ins; no realm is started.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using uint32 = uint32_t;
constexpr uint32 UNIT_NPC_FLAGS=1, UNIT_FIELD_FLAGS=2;
constexpr uint32 UNIT_NPC_FLAG_FLIGHTMASTER=8, UNIT_NPC_FLAG_STABLEMASTER=16;
constexpr uint32 UNIT_FLAG_NOT_SELECTABLE=1, CREATURE_TYPEFLAGS_GHOST_VISIBLE=1;
constexpr uint32 UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL=1, CLASS_HUNTER=3;
constexpr int REP_UNFRIENDLY=1;
constexpr float INTERACTION_DISTANCE=5;
struct WorldObject
{
    float x=0,y=0,z=0;uint32 map=0;
    float GetPositionX()const{return x;}float GetPositionY()const{return y;}float GetPositionZ()const{return z;}
    uint32 GetMapId()const{return map;}
    float GetDistance(WorldObject const* other)const
    {float dx=x-other->x,dy=y-other->y,dz=z-other->z;return std::sqrt(dx*dx+dy*dy+dz*dz);}
    bool IsWithinDistInMap(WorldObject const* other,float range)const{return map==other->map&&GetDistance(other)<range;}
};
struct Player;
struct Creature:WorldObject
{
    uint32 guid=15324,entry=2432,npcFlags=8,flags=0,charm=0,faction=1,typeFlags=0;
    bool alive=true,invisible=false,hostile=false,combat=false;
    uint32 GetGUIDLow()const{return guid;}uint32 GetEntry()const{return entry;}
    uint32 GetUInt32Value(uint32 field)const{return field==UNIT_NPC_FLAGS?npcFlags:flags;}
    bool HasFlag(uint32 field,uint32 mask)const{return (GetUInt32Value(field)&mask)!=0;}
    bool HasTypeFlag(uint32 mask)const{return (typeFlags&mask)!=0;}
    bool IsAlive()const{return alive;}bool IsInvisibleForAlive()const{return invisible;}
    uint32 GetCharmerGuid()const{return charm;}bool IsHostileTo(Player const*)const{return hostile;}
    bool IsInCombat()const{return combat;}uint32 GetFactionTemplateId()const{return faction;}
};
struct FactionTemplateEntry{uint32 faction=1;};
struct FactionEntry{int reputationListID=0;};
struct Reputation{int rank=2;int GetRank(FactionEntry const*)const{return rank;}};
struct Player:WorldObject
{
    bool inWorld=true,taxi=false,lostControl=false,alive=true;uint32 playerClass=CLASS_HUNTER;
    Reputation reputation;
    bool IsInWorld()const{return inWorld;}bool IsTaxiFlying()const{return taxi;}
    bool HasUnitState(uint32)const{return lostControl;}uint32 GetClass()const{return playerClass;}
    bool IsAlive()const{return alive;}Reputation const& GetReputationMgr()const{return reputation;}
    int GetTeam()const{return 1;}
    bool CanInteractWithNPC(Creature const*,uint32,char const** = nullptr)const;
};
struct ObjectMgr
{
    FactionTemplateEntry factionTemplate;FactionEntry faction;bool hasTemplate=true,hasFaction=true;uint32 node=14;
    FactionTemplateEntry const* GetFactionTemplateEntry(uint32)const{return hasTemplate?&factionTemplate:nullptr;}
    FactionEntry const* GetFactionEntry(uint32)const{return hasFaction?&faction:nullptr;}
    uint32 GetNearestTaxiNode(float,float,float,uint32,int)const{return node;}
}sObjectMgr;
#include "NativeNpcInteraction.inc"
struct TaxiPathEntry{uint32 from=14,to=6;};
struct TaxiStore
{
    std::map<uint32,TaxiPathEntry> rows{{26,{}}};
    TaxiPathEntry const* LookupEntry(uint32 id)const{auto it=rows.find(id);return it==rows.end()?nullptr:&it->second;}
}sTaxiPathStore;
namespace MaNGOS
{
    template<class Check>struct CreatureLastSearcher
    {Creature*& result;Check& check;CreatureLastSearcher(Creature*& result,Check& check):result(result),check(check){}};
}
namespace Cell
{
    std::vector<Creature*> grid,world;uint32 scans=0;
    template<class Search>void VisitAllObjects(Player*,Search& search,float)
    {
        ++scans;
        for(auto const* list:{&grid,&world})for(auto* npc:*list)
            if(npc&&search.check(npc))search.result=npc;
    }
}
#include "TaxiInteractionTrace.inc"
void Check(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
void Expect(Player const& player,Creature const* npc,uint32 flags,char const* expected)
{
    char const* reason="stale";
    bool accepted=player.CanInteractWithNPC(npc,flags,&reason);
    Check(reason&&std::string(reason)==expected,"wrong native rejection reason");
    Check(accepted==(std::string(expected)=="accepted"),"reason disagrees with result");
    Check(player.CanInteractWithNPC(npc,flags)==accepted,"diagnostics changed eligibility");
}
void Contains(std::string const& text,std::string const& expected)
{if(text.find(expected)==std::string::npos)throw std::runtime_error("missing field: "+expected+" in "+text);}
int main()
{
    try
    {
        Player player;Creature npc;
        auto test=[&](char const* reason){Expect(player,&npc,8,reason);player=Player{};npc=Creature{};};
        Expect(player,nullptr,8,"npc_missing");test("accepted");
        player.inWorld=false;test("player_not_in_world_or_on_taxi");
        player.taxi=true;test("player_not_in_world_or_on_taxi");
        player.lostControl=true;test("player_cannot_react");
        npc.npcFlags=0;test("npc_service_flag_missing");
        npc.npcFlags=16;player.playerClass=1;Expect(player,&npc,16,"stable_requires_hunter");
        player.playerClass=CLASS_HUNTER;Expect(player,&npc,16,"accepted");player=Player{};npc=Creature{};
        npc.alive=false;test("npc_dead");npc.invisible=true;test("npc_invisible_for_alive");
        player.alive=false;test("npc_not_ghost_visible");
        player.alive=false;npc.typeFlags=1;test("accepted");
        npc.charm=1;test("npc_charmed");npc.hostile=true;test("npc_hostile");
        npc.combat=true;test("npc_in_combat");npc.flags=1;test("npc_not_selectable");
        player.reputation.rank=REP_UNFRIENDLY;test("reputation_unfriendly");
        npc.x=6;test("npc_out_of_range_or_map");npc.map=1;test("npc_out_of_range_or_map");
        npc.npcFlags=0;Expect(player,&npc,0,"accepted");npc=Creature{};
        sObjectMgr.hasTemplate=false;player.reputation.rank=0;test("accepted");sObjectMgr.hasTemplate=true;
        sObjectMgr.hasFaction=false;player.reputation.rank=0;test("accepted");sObjectMgr.hasFaction=true;
        sObjectMgr.factionTemplate.faction=0;player.reputation.rank=0;test("accepted");sObjectMgr.factionTemplate.faction=1;
        sObjectMgr.faction.reputationListID=-1;player.reputation.rank=0;test("accepted");sObjectMgr.faction.reputationListID=0;
        // Combined failures retain native short-circuit priority and bool results.
        for(uint32 bits=0;bits<4096;++bits)
        {
            player=Player{};npc=Creature{};
            player.inWorld=!(bits&1);player.taxi=bits&2;player.lostControl=bits&4;
            npc.npcFlags=(bits&8)?0:8;npc.alive=!(bits&16);npc.invisible=bits&32;
            npc.charm=(bits&64)?1:0;npc.hostile=bits&128;npc.combat=bits&256;
            npc.flags=(bits&512)?1:0;player.reputation.rank=(bits&1024)?0:2;npc.x=(bits&2048)?6:0;
            char const* reason=nullptr;bool ok=player.CanInteractWithNPC(&npc,8,&reason);
            Check(ok==(bits==0),"combined failure accepted");Check(ok==player.CanInteractWithNPC(&npc,8),"optional reason altered combined result");
            Check(reason!=nullptr,"combined case left reason unset");
        }
        player=Player{};npc=Creature{};
        Cell::grid.clear();Cell::world={&npc};
        auto details=DescribeTaxiInteraction(&player,26,true);
        Contains(details,"taxi_path=26 taxi_from=14 taxi_to=6 taxi_probe=1");
        Contains(details,"npc_guid=15324 npc_entry=2432 npc_node=14 npc_source_match=1");
        Contains(details,"npc_reject=accepted");
        npc.combat=true;Contains(DescribeTaxiInteraction(&player,26,true),"npc_reject=npc_in_combat");npc.combat=false;
        npc.alive=false;Contains(DescribeTaxiInteraction(&player,26,true),"npc_reject=npc_dead");npc.alive=true;
        sObjectMgr.node=99;Contains(DescribeTaxiInteraction(&player,26,true),"npc_source_match=0");sObjectMgr.node=14;
        npc.x=10;Contains(DescribeTaxiInteraction(&player,26,true),"npc_reject=npc_out_of_range_or_map");
        npc.x=30;Contains(DescribeTaxiInteraction(&player,26,true),"npc_reject=npc_missing");npc.x=0;
        Creature farther;farther.x=3;farther.guid=2;Cell::grid={&farther};
        Contains(DescribeTaxiInteraction(&player,26,true),"npc_guid=15324");
        Cell::grid.clear();Cell::world.clear();Contains(DescribeTaxiInteraction(&player,26,true),"npc_guid=0");
        uint32 scans=Cell::scans;
        Contains(DescribeTaxiInteraction(&player,999,true),"taxi_from=0 taxi_to=0");
        Contains(DescribeTaxiInteraction(&player,26,false),"taxi_probe=0");
        Check(scans==Cell::scans,"invalid or post-activation record scanned NPCs");
        std::cout<<"Native NPC rejection reasons, 4096 combinations and taxi trace snapshots passed.\n";
    }
    catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}
}
