#pragma once
#include <array>
namespace nuka {
// Profiles invalidate the owning Instance. This key covers per-draw inputs
// that can change independently while the tuning menu freezes simulation time.
struct FrozenSimulationKey {
    std::array<float,10> transform{}; // position, gravity, X axis, scale
    float fill{},motionWeight{};
    bool detail{};
    bool operator==(const FrozenSimulationKey&) const=default;
};
class FrozenSimulationCache {
    FrozenSimulationKey previous{};
    bool valid=false;
public:
    bool Reuse(bool frozen,bool held,const FrozenSimulationKey& key){
        // Held bottles retain their carry-frame/emission tracking updates.
        if(!frozen||held){valid=false;return false;}
        const bool reuse=valid&&previous==key;
        previous=key;valid=true;
        return reuse;
    }
};
}
