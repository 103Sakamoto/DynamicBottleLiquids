#pragma once
#include "Simulation.h"
#include <algorithm>
#include <cmath>
namespace nuka {
// Skyrim's radius-relative height convention, using the stable authored cavity
// body radius here. No per-frame section rebuild or view-dependent sizing.
inline float WaveReferenceRadius(const Settings& s) {
    if(s.base.ringCount<2 || s.fill<=0 || s.fill>=1)return 0;
    float radius=0;
    for(int i=0;i<s.base.ringCount;++i)radius=std::max(radius,s.base.rings[i].y);
    float height=s.base.rings[s.base.ringCount-1].x-s.base.rings[0].x;
    // Bound shallow/wide cavities and reduce nearly empty puddles smoothly.
    return std::min(radius,std::max(height,0.f)*.25f)*std::sqrt(std::clamp(s.fill/.15f,0.f,1.f));
}
inline float WaveImpactBoost(float energy) {
    float x=std::clamp((energy-.25f)/.75f,0.f,1.f);
    return 1+.6f*x*x*(3-2*x);
}
}
