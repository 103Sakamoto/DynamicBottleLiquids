#include "PCH.h"
#include "Profiles.h"
#include "AcidSoakerLevel.h"
#include <iostream>
using namespace nuka;
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    if(argc!=2)return 2;
    auto profiles=LoadDrinks(argv[1],LoadSettings(argv[1]),LoadTuning(argv[1]));
    auto s=profiles.at("DynamicBottleLiquids:acidsoaker-tank").settings;
    s.shotBubbles=true;s.radiusMin=.10f;s.radiusMax=.30f;
    Motion m;ApplyBubbleLimits(m,{0,0,24});
    AcidSoakerShotTracker tracker;
    need(tracker.Sample(1.f)==0,"equip emitted a shot");
    s.fill=.94f*.95f;m.pendingShots=tracker.Sample(.95f);
    auto c=Simulate(s,m,{},{0,0,1},.016f,true,true);
    need(c.bubbleCount==6&&m.pendingShots==0,"one shot must emit six bubbles once");
    for(const auto& p:m.bubbles)need(p.position.z>=s.base.rings[0].x&&p.position.z<s.base.rings[0].x+1.f,"wrong feed opening");
    unsigned big=0,smallCount=0;for(const auto& p:m.bubbles){if(p.baseRadius>=.24f)++big;else if(p.baseRadius>=.10f&&p.baseRadius<=.14f)++smallCount;}
    need(big==3&&smallCount==3,"expected three big and three small bubbles");
    for(unsigned a=0;a<3;++a)for(unsigned b=a+1;b<3;++b)
        need(Length(m.bubbles[a].position-m.bubbles[b].position)>=m.bubbles[a].radius+m.bubbles[b].radius,"large bubbles overlap at birth");
    float firstZ=m.bubbles[0].position.z;
    float smallZ=m.bubbles[3].position.z;
    c=Simulate(s,m,{},{0,0,1},.05f,true,true);
    need(c.bubbleCount==6&&m.bubbles[0].position.z>firstZ+.40f,"idle emitted bubbles or bubbles did not rise");
    need(m.bubbles[3].position.z>smallZ+.30f,"small bubble stuck at inlet");
    m.pendingShots=tracker.Sample(1.f);s.fill=.94f;
    c=Simulate(s,m,{},{0,0,1},.016f,true,true);
    need(c.bubbleCount==6,"reload emitted new bubbles");
    for(int i=0;i<20;++i){m.pendingShots=1;c=Simulate(s,m,{},{0,0,1},0,true,true);}
    need(c.bubbleCount==24,"local shot-bubble cap failed");
    ApplyBubbleLimits(m,{0,0,0});m.pendingShots=4;
    c=Simulate(s,m,{},{0,0,1},.016f,true,true);
    need(c.bubbleCount==0&&m.pendingShots==0,"zero quota retained deferred bursts");
    ApplyBubbleLimits(m,{0,0,24});
    c=Simulate(s,m,{},{0,0,1},.016f,true,true);
    need(c.bubbleCount==0,"budget recovery emitted an old shot");
    s.fill=.1f;m.pendingShots=1;
    c=Simulate(s,m,{},{0,0,-1},.016f,true,true);
    need(c.bubbleCount==0,"uncovered opening emitted bubbles");
    s.fill=0;m.pendingShots=1;
    c=Simulate(s,m,{},{0,0,1},.016f,true,true);
    need(c.bubbleCount==0,"empty tank emitted bubbles");
    s.fill=.94f;m.pendingShots=1;
    c=Simulate(s,m,{},{0,0,1},.016f,false,true);
    need(c.bubbleCount==0&&m.pendingShots==0,"culled detail retained a shot");
    auto custom=ParseShotBubbleConfig(nlohmann::json::parse(R"({"big":{"count":1,"radius_min":0.2,"radius_max":0.2,"speed_min":3,"speed_max":3},"small":{"count":2,"radius_min":0.1,"radius_max":0.1,"speed_min":2,"speed_max":2}})"));
    s.shotConfig=custom;s.radiusMax=.2f;s.fill=.94f;m=Motion{};ApplyBubbleLimits(m,{0,0,24});m.pendingShots=1;
    c=Simulate(s,m,{},{0,0,1},.016f,true,true);
    need(m.bubbles.size()==3&&m.bubbles[0].baseRadius==.2f&&m.bubbles[0].speed==3.f&&m.bubbles[1].baseRadius==.1f&&m.bubbles[1].speed==2.f,"custom config not applied");
    s.shotConfig.bigCount=s.shotConfig.smallCount=0;m=Motion{};m.pendingShots=1;
    c=Simulate(s,m,{},{0,0,1},.016f,true,true);need(c.bubbleCount==0,"zero counts did not disable births");
    bool rejected=false;try{ParseShotBubbleConfig(nlohmann::json::parse(R"({"big":{"count":-1},"small":{}})"));}catch(...){rejected=true;}need(rejected,"invalid config accepted");
    std::cout<<"PASS: production shot-bubble simulation, three big plus three small per shot, faster rise, no idle/reload births, local/zero quota, dry opening, empty tank and distance culling\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
