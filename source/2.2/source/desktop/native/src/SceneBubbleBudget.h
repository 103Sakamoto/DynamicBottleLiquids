#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace nuka {
struct BubbleLimits {
    unsigned shaking=64,foam=64,carbonation=128;
    unsigned Total()const{return shaking+foam+carbonation;}
    bool operator==(const BubbleLimits&)const=default;
};
struct BubbleBudgetRequest {
    std::uintptr_t id{};
    float pixels{},distance{};
    bool held{};
    BubbleLimits demand{0,0,0},allocation{0,0,0};
    // Optional per-call destination. Must remain valid through allocation;
    // it travels with the request when priority sorting reorders the vector.
    BubbleLimits* destination=nullptr;
};
inline BubbleLimits DivideBubbleQuota(unsigned quota,BubbleLimits demand){
    unsigned caps[]{demand.shaking,demand.foam,demand.carbonation},out[3]{};
    const unsigned total=demand.Total();quota=std::min(quota,total);
    if(!total)return {0,0,0};
    unsigned used=0;for(unsigned i=0;i<3;++i){out[i]=quota*caps[i]/total;used+=out[i];}
    for(unsigned i=0;used<quota;i=(i+1)%3)if(out[i]<caps[i]){++out[i];++used;}
    return {out[0],out[1],out[2]};
}
inline void AllocateSceneBubbles(std::vector<BubbleBudgetRequest>& requests){
    constexpr unsigned maximum=1280;
    auto score=[](const BubbleBudgetRequest& r){
        double p=std::isfinite(r.pixels)?std::clamp(r.pixels,1.f,1024.f):1.f;
        double d=std::isfinite(r.distance)?std::max(0.f,r.distance):768.f;
        return p*p/(1+d/128);
    };
    std::sort(requests.begin(),requests.end(),[&](const auto& a,const auto& b){
        if(a.held!=b.held)return a.held>b.held;
        const auto x=score(a),y=score(b);return x!=y?x>y:a.id<b.id;
    });
    unsigned remaining=maximum;
    std::vector<unsigned> quotas(requests.size());
    for(unsigned i=0;i<requests.size();++i)if(requests[i].held){
        quotas[i]=std::min(remaining,requests[i].demand.Total());remaining-=quotas[i];
    }
    // Weighted distribution with saturation: unused capacity flows to other
    // bottles. Deterministic rank resolves integer remainders, not draw order.
    while(remaining){
        double total=0;for(unsigned i=0;i<requests.size();++i)if(!requests[i].held&&quotas[i]<requests[i].demand.Total())total+=score(requests[i]);
        if(total==0)break;
        const unsigned round=remaining;unsigned granted=0;
        for(unsigned i=0;i<requests.size();++i)if(!requests[i].held&&quotas[i]<requests[i].demand.Total()){
            unsigned grant=std::min(requests[i].demand.Total()-quotas[i],unsigned(round*score(requests[i])/total));
            grant=std::min(grant,remaining);quotas[i]+=grant;remaining-=grant;granted+=grant;
        }
        if(!granted)for(unsigned i=0;i<requests.size()&&remaining;++i)if(!requests[i].held&&quotas[i]<requests[i].demand.Total()){++quotas[i];--remaining;}
    }
    for(unsigned i=0;i<requests.size();++i){
        auto& r=requests[i];r.allocation=DivideBubbleQuota(quotas[i],r.demand);
        if(r.destination)*r.destination=r.allocation;
    }
}
}
