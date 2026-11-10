#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
namespace nuka {
// Search by controller capability, never by a runtime node's display name.
// The same traversal is exercised by the engine-free regression tests.
template<class Node,class Probe>
auto* FindAcidSoakerSequence(Node* first,Probe probe){
    for(unsigned depth=0;first&&depth<64;++depth,first=first->parent)
        if(auto* sequence=probe(first))return sequence;
    return decltype(probe(first))(nullptr);
}

// One emission per discrete consumed round. Smoothed animation samples do not
// produce repeated bursts; first observation, missing state and refills rebase.
struct AcidSoakerShotTracker {
    int rounds=-1;
    unsigned Sample(std::optional<float> fraction){
        if(!fraction||!std::isfinite(*fraction)){rounds=-1;return 0;}
        int next=int(std::lround(std::clamp(*fraction,0.f,1.f)*20.f));
        unsigned shots=rounds<0||next>=rounds?0u:unsigned(rounds-next);
        rounds=next;return std::min(shots,4u);
    }
};
// Native lowering clip: 0 = full, 20/30 seconds = empty. The game owns
// sequencing, ammo changes and reload timing; DBL only samples its current time.
inline std::optional<float> AcidSoakerClipFraction(float time,float begin,float end){
    if(!std::isfinite(time)||!std::isfinite(begin)||!std::isfinite(end)||
       end<=begin||time<begin-.001f||time>end+.001f)return {};
    return std::clamp(1.f-(time-begin)/(end-begin),0.f,1.f);
}
// Native initial pose can precede the first animation evaluation. Only accept
// the two exact authored endpoint poses; do not guess ammo from an arbitrary pose.
inline std::optional<float> AcidSoakerEndpointFraction(float z){
    if(!std::isfinite(z))return {};
    if(std::abs(z-7.8400206566f)<.0001f)return 1.f;
    if(std::abs(z-2.6392197609f)<.0001f)return 0.f;
    return {};
}
}
