#include "native/src/Simulation.h"
#include <iostream>
#include <stdexcept>
#include <map>
#include <cstring>
using namespace nuka;
void need(bool b,const char* message){if(!b)throw std::runtime_error(message);}
unsigned Validate(const std::vector<BubbleBudgetRequest>& requests){
    unsigned total=0;for(auto& r:requests){total+=r.allocation.Total();need(r.allocation.shaking<=r.demand.shaking&&r.allocation.foam<=r.demand.foam&&r.allocation.carbonation<=r.demand.carbonation,"per-category cap");}
    need(total<=1280,"scene budget exceeded");return total;
}
int main(){try{
    // Compare direct destinations with the previous per-instance ID search.
    // Destinations refer to stable instance storage, never to the sorted vector.
    std::mt19937 routingRandom(1303);
    for(unsigned trial=0;trial<1000;++trial){
        struct Entry {unsigned frame;BubbleBudgetRequest request;BubbleLimits next{0,0,0};};
        std::vector<Entry> entries;
        const unsigned frame=trial%2?0u:100u; // includes unsigned frame wrap
        for(unsigned j=0;j<trial%129;++j){
            entries.push_back({frame-unsigned(routingRandom()%4),
                {j+1,float(routingRandom()%1025),float(routingRandom()%2000),routingRandom()%9==0,
                 {unsigned(routingRandom()%65),unsigned(routingRandom()%65),unsigned(routingRandom()%129)},{}}});
        }
        std::vector<BubbleBudgetRequest> direct,reference;
        for(auto& e:entries)if(frame-e.frame<=1){
            reference.push_back(e.request);direct.push_back(e.request);direct.back().destination=&e.next;
        }
        std::shuffle(direct.begin(),direct.end(),routingRandom);
        AllocateSceneBubbles(reference);AllocateSceneBubbles(direct);
        for(const auto& e:entries){
            BubbleLimits oldQuota{0,0,0};
            auto found=std::find_if(reference.begin(),reference.end(),[&](const auto& r){return r.id==e.request.id;});
            if(found!=reference.end())oldQuota=found->allocation;
            need(e.next==oldQuota,"direct destination differs from previous ID lookup");
        }
        for(unsigned j=0;j<direct.size();++j){
            need(direct[j].id==reference[j].id&&direct[j].allocation==reference[j].allocation,
                "direct destination changes priority or allocation");
        }
    }
    std::cout<<"PASS: 1000 seeded direct-destination versus old-lookup cases, stale instances, frame wrap, shuffled order and all quota categories\n";
    std::vector<BubbleBudgetRequest> requests;
    for(unsigned i=0;i<100;++i)requests.push_back({i+1,100,100,i==99,{64,64,128},{}});
    AllocateSceneBubbles(requests);need(Validate(requests)==1280,"crowded scene must use allowance");
    need(requests.front().held&&requests.front().allocation.Total()==256,"held priority");
    std::map<std::uintptr_t,unsigned> first;for(auto& r:requests)first[r.id]=r.allocation.Total();
    std::reverse(requests.begin(),requests.end());AllocateSceneBubbles(requests);for(auto& r:requests)need(first[r.id]==r.allocation.Total(),"allocation depends on draw order");
    auto priority=requests;priority[1].pixels=300;priority[1].distance=30;const auto prominent=priority[1].id;
    AllocateSceneBubbles(priority);Validate(priority);unsigned large=0,small=256;for(auto& r:priority)if(r.id==prominent)large=r.allocation.Total();else if(!r.held)small=std::min(small,r.allocation.Total());need(large>small,"prominent bottle priority");
    for(unsigned count:{0u,1u,4u,10u,128u,500u}){
        std::vector<BubbleBudgetRequest> many;for(unsigned i=0;i<count;++i)many.push_back({i,50,100,true,{64,64,128},{}});
        AllocateSceneBubbles(many);Validate(many);
    }
    std::vector<BubbleBudgetRequest> sparse{{1,100,20,false,{0,0,128},{}}};AllocateSceneBubbles(sparse);need(Validate(sparse)==128&&sparse[0].allocation.carbonation==128,"disabled categories waste allowance");
    // Run the production simulation for 100 bottles with saturated starting
    // populations, active births, surface transfers and a changed held bottle.
    Settings s{};s.base.ringCount=2;s.base.rings[0]={0,1,1,0};s.base.rings[1]={4,1,0,0};s.base.trace.x=8;
    s.base.optics={1.5f,1.33f,.1f,0};s.fill=.5f;s.frequency=3;s.damping=2;s.maxTilt=.4f;
    s.waveFrequency=1;s.intensity=1;s.baseline=1000;s.shakingBubbles=1;s.foamStrength=1;
    s.radiusMin=.01f;s.radiusMax=.03f;s.rise=1;s.settle=1;MakeSamples(s);
    std::map<std::uintptr_t,Motion> motions;
    for(auto& r:requests){auto& m=motions[r.id];m.bubbles.resize(128,Particle{{0,0,1},.02f,1,0,.02f});m.surfaceBubbles.resize(64,SurfaceBubble{{0,0,2},.02f,0,.02f});m.shaking.resize(64);}
    for(unsigned frame=0;frame<120;++frame){
        if(frame==60){for(auto& r:requests)r.held=r.id==1;AllocateSceneBubbles(requests);}
        for(auto& r:requests)ApplyBubbleLimits(motions[r.id],r.allocation);
        unsigned live=0,exported=0;
        for(auto& r:requests){auto& m=motions[r.id];m.emissionOmega={15,0,0};
            auto reference=m;SimulationTimings times{};
            auto c=Simulate(s,m,{10,0,0},{0,0,1},1.f/60,true,r.held,1,&times);
            auto plain=Simulate(s,reference,{10,0,0},{0,0,1},1.f/60,true,r.held);
            need(std::memcmp(&c,&plain,sizeof(c))==0,"timing changes reference simulation output");
            double elapsed=0;for(auto t:times){need(t>=0,"negative simulation stage time");elapsed+=t;}need(elapsed>0,"simulation timing missing");
            need(m.bubbles.size()<=r.allocation.carbonation&&m.shaking.size()<=r.allocation.shaking&&m.surfaceBubbles.size()<=r.allocation.foam,"production spawn exceeded quota");
            live+=unsigned(m.bubbles.size()+m.shaking.size()+m.surfaceBubbles.size());exported+=c.bubbleCount;
        }
        need(live<=1280&&exported<=1280,"production scene exceeds 1280");
    }
    for(bool necked:{false,true})for(float fill:{.1f,.5f,.9f})for(float tilt:{0.f,.7f,1.5707963f}){
        Settings fixture=s;fixture.fill=fill;
        if(necked){fixture.base.ringCount=4;fixture.base.rings[0]={0,1,1,0};fixture.base.rings[1]={1,.6f,0,0};fixture.base.rings[2]={2,1,0,0};fixture.base.rings[3]={4,.8f,0,0};}
        MakeSamples(fixture);Motion motion;
        for(int step=0;step<60;++step){
            if(step%10==0)motion.surfaceBubbles.push_back({{.2f,0,1},.02f,0,.02f});
            if(step==20){fixture.waveAmplitude=.05f;motion.waveEnergy=.5f;motion.rippleEnergy=.3f;}
            if(step==40){fixture.waveAmplitude=0;fixture.fill=1-fill;}
            Vec up{std::sin(tilt),0,std::cos(tilt)};auto reference=motion;
            SimulationTimings times{};auto cached=Simulate(fixture,motion,{},up,1.f/60,true,false,1,&times);
            auto plain=Simulate(fixture,reference,{},up,1.f/60);
            need(std::memcmp(&cached,&plain,sizeof(cached))==0,"tilt/neck/wave/fill timing reference mismatch");
        }
    }
    std::cout<<"PASS: timed/reference equivalence across upright, tilted, horizontal, necked profiles, waves and changing fills\n";
    for(auto& [id,m]:motions){ApplyBubbleLimits(m,{0,0,0});auto c=Simulate(s,m,{},{0,0,1},.05f);need(c.bubbleCount==0,"zero allocation spawns particles");}
    std::cout<<"PASS: 1280 global cap, per-category limits, held/prominence priority, draw-order independence, 100-bottle production simulation, reassignment, zero quota and timed/untimed output equivalence\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
