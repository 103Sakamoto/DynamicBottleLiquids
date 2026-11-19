#pragma once
#include "Simulation.h"
namespace nuka {
// Engine rectangle: left/right/top/bottom with Y measured from the bottom.
// worldToShadow already produces D3D UVs, whose Y starts at the top.
inline V4 SunShadowBounds(const int* viewport,unsigned width,unsigned height){
    return {float(viewport[0])/width,1.f-float(viewport[2])/height,
        float(viewport[1])/width,1.f-float(viewport[3])/height};
}
}
