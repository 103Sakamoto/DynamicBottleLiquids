#pragma once
#include "Simulation.h"
namespace nuka {
// First-person skeleton positions use a translated origin. Keep distances in
// skeleton space and anchor world-light/shadow positions at the actual camera.
inline Vec AnimationWorldOffset(Vec worldEye,Vec animationEye){
    return worldEye-animationEye;
}
}
