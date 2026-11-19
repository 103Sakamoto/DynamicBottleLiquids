#pragma once
#include "WorldDirection.h"
#include <cmath>
namespace nuka {
// PlayerCamera::cameraRoot is the player view's scene-graph root. The graphics
// referenceCamera can belong to an auxiliary render and must not drive motion.
// FO4 1.11.240 GetCameraPosition also reads cameraRoot->world.translate on its
// general path; first-person origins are handled by the existing eye adapter.
struct CarryFrame {
    RE::NiMatrix3 rotation{};
    Vec eye{};
    const void* identity{};
    const void* originIdentity{};
    bool valid=false;
};
inline CarryFrame MakeCarryFrame(const RE::NiMatrix3* playerRootRotation,Vec eye,
                                const void* identity,bool haveEye){
    CarryFrame frame;frame.eye=eye;frame.identity=identity;
    if(!playerRootRotation||!identity||!haveEye||!std::isfinite(eye.x+eye.y+eye.z))return frame;
    frame.rotation=*playerRootRotation;
    Vec axes[3];
    for(int k=0;k<3;k++){
        auto a=frame.rotation[k];axes[k]={a.x,a.y,a.z};
        if(!std::isfinite(Dot(axes[k],axes[k]))||std::abs(Dot(axes[k],axes[k])-1)>.02f)return frame;
    }
    if(std::abs(Dot(axes[0],axes[1]))>.02f||std::abs(Dot(axes[0],axes[2]))>.02f||
       std::abs(Dot(axes[1],axes[2]))>.02f)return frame;
    frame.valid=true;return frame;
}
inline Vec CarryPosition(const CarryFrame& frame,Vec position){
    return WorldToMeshDirection(frame.rotation,position-frame.eye);
}
}
