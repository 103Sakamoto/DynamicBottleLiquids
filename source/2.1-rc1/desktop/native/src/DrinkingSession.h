#pragma once
#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace nuka {
// Compare identities without dereferencing saved ancestors, which may be gone.
template<class Node>
bool SameAttachment(Node* object,const std::vector<const Node*>& saved) {
    if(saved.size()<2)return false;
    for(auto* expected:saved){
        if(!object||object!=expected)return false;
        object=object->parent;
    }
    return object==nullptr;
}
inline bool IsDrinkingProp(std::string_view name) {
    return name.starts_with("DynamicBottleLiquids:iafanimobject");
}
// One-use prop state. Visibility and motion-cache resets never initialize it again.
struct DrinkingSession {
    float remaining=0, tiltedSeconds=0;
    bool draining=false;
    explicit DrinkingSession(float initial=0):remaining(std::clamp(initial,0.f,1.f)){}
    void Update(float uprightCosine,float dt) {
        if(!std::isfinite(uprightCosine)||!std::isfinite(dt)||dt<=0)return;
        // Ignore discontinuities rather than consuming through a load/stall.
        if(dt>.25f)return;
        // Start at 80 degrees, stop below 70; reach full rate at 120 degrees.
        constexpr float start=.17364818f,stop=.34202014f;
        if(uprightCosine>stop){draining=false;tiltedSeconds=0;return;}
        if(!draining){
            if(uprightCosine>start){tiltedSeconds=0;return;}
            float previous=tiltedSeconds;tiltedSeconds+=dt;
            if(tiltedSeconds<.15f)return;
            draining=true;dt=std::max(0.f,tiltedSeconds-std::max(previous,.15f));
        }
        float rate=.75f*std::clamp((stop-uprightCosine)/(stop+.5f),0.f,1.f);
        remaining=std::max(0.f,remaining-rate*dt);
    }
};
}

