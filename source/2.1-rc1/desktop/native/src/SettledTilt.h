#pragma once
#include "Simulation.h"
#include <cstring>
namespace nuka {
// Per-instance cache. Translation, distance and scale do not change local fill.
// Keep a value signature so live tuning also invalidates a retained profile.
struct SettledTiltCache {
    bool valid=false;
    const Settings* profile=nullptr;
    Vec gravity{};
    float fill=0;
    int rings=0;
    std::array<V4,16> geometry{};
    V4 plane{};
    unsigned solves=0;
    V4 Get(const Settings& s,Vec up){
        up=Normalize(up);
        if(!valid||profile!=&s||fill!=s.fill||rings!=s.base.ringCount||
           std::memcmp(geometry.data(),s.base.rings,sizeof(s.base.rings))!=0||
           up.x!=gravity.x||up.y!=gravity.y||up.z!=gravity.z){
            Motion settled{};
            plane=Simulate(s,settled,{},up,0,false,false,0).plane;
            profile=&s;gravity=up;fill=s.fill;rings=s.base.ringCount;
            std::memcpy(geometry.data(),s.base.rings,sizeof(s.base.rings));
            valid=true;++solves;
        }
        return plane;
    }
};
}
