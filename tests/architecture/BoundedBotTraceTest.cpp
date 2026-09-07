#include "BoundedBotTrace.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#define CHECK(x) do {if(!(x)){std::cerr<<__LINE__<<": " #x <<'\n';std::exit(1);}}while(0)
int main()
{
    BoundedBotTrace trace;
    CHECK(!trace.Take(0,0));
    for(uint32_t i=1;i<=12;++i) CHECK(trace.Take(0,i));
    CHECK(!trace.Take(0,13));
    for(int i=1;i<7;++i) CHECK(trace.Take(0,1));
    CHECK(!trace.Take(0,1));
    CHECK(trace.Take(1000,1));
    CHECK(!trace.Take(29999,13));
    CHECK(trace.Take(30000,13)); // rotates an absent bot, not the recently seen bot 1
    CHECK(trace.Take(30000,1));
    CHECK(trace.Suppressed()==3);
    BoundedBotTrace budget;
    uint32_t accepted=0;
    for(uint32_t t=0;t<100000&&!budget.Finished();t+=1000)
        for(uint32_t bot=1;bot<=12;++bot)
            for(int n=0;n<8;++n) if(budget.Take(t,bot))++accepted;
    CHECK(accepted==8000&&budget.Emitted()==8000&&budget.Finished());
    CHECK(!budget.Take(600000,99));
    BoundedBotTrace timeout;
    CHECK(timeout.Take(100,1));CHECK(timeout.Take(600099,2));
    CHECK(!timeout.Take(600100,2)&&timeout.Finished());
    BoundedBotTrace wrap;
    uint32_t start=std::numeric_limits<uint32_t>::max()-50;
    CHECK(wrap.Take(start,1));CHECK(wrap.Take(start+1000,1));
    CHECK(!wrap.Take(start+600000,1)&&wrap.Finished());
    BoundedBotTrace journey;
    CHECK(!journey.Take(1,42,false,true)); // No sampling of unrelated remote bots.
    CHECK(journey.Take(100,42,true,true));
    CHECK(!journey.Take(101,42,false,true));
    CHECK(journey.Take(5100,42,false,true)); // Follow the same bot outside the hub.
    CHECK(journey.Take(5100,42,false,false,true));
    CHECK(!journey.Take(5101,42,false,false,true)); // Generic action quota.
    for(uint32_t t=10100;t<60100;t+=5000) CHECK(journey.Take(t,42,false,true));
    CHECK(!journey.Take(60100,43,false,true));
    BoundedBotTrace reserved;
    for(int i=0;i<7;++i) CHECK(reserved.Take(0,1));
    CHECK(!reserved.Take(0,1));
    CHECK(reserved.Take(0,1,true,true)); // Events cannot starve the physical sample.
    CHECK(!reserved.Take(1,1,true,true));
    CHECK(!reserved.Take(1,1));
    std::cout<<"Bounded bot trace caps, turnover, rate limit, timeout and clock wrap passed\n";
}
