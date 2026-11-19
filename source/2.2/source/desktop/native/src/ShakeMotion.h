#pragma once
#include "Simulation.h"
#include <algorithm>
#include <cmath>
namespace nuka {
inline Vec ShakeCross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec ShakeLimit(Vec v,float maximum){float length=Length(v);return length>maximum?v*(maximum/length):v;}
// World X/Z expressed in canonical bottle space. Their derivative is -omega x
// axis; using all three basis vectors recovers signed local angular velocity.
// This bounded small-step estimate does not change any rendering transform.
inline Vec ShakeRotation(Vec previousX,Vec previousUp,Vec currentX,Vec currentUp,float dt){
    if(dt<=.00001f)return {};
    Vec previousY=ShakeCross(previousUp,previousX),currentY=ShakeCross(currentUp,currentX);
    return ShakeLimit((ShakeCross(previousX,currentX)+ShakeCross(previousY,currentY)+ShakeCross(previousUp,currentUp))*(-.5f/dt),20.f);
}
// Coordinates supplied by the adapter are world-space for loose objects and
// camera-space for held objects. A rigidly carried bottle has constant position
// and orientation in that frame, irrespective of walking or camera turning.
struct EmissionTracker {
    Vec x{},up{},position{},velocity{};
    float scale{};
    bool valid=false,velocityValid=false,held=false;
    const void* identity{};
    const void* originIdentity{};
    void Sample(Motion& m,Vec newX,Vec newUp,Vec newPosition,Vec vertical,
                float metricScale,float dt,bool carrying,bool frameValid,const void* frameIdentity=nullptr,const void* frameOrigin=nullptr){
        bool continuous=valid&&frameValid&&held==carrying&&dt>.00001f&&dt<=.25f&&
            identity==frameIdentity&&originIdentity==frameOrigin&&
            metricScale>0&&scale>0&&std::abs(metricScale/scale-1)<.05f;
        m.emissionValid=continuous;
        m.emissionOmega=continuous?ShakeRotation(x,up,newX,newUp,dt):Vec{};
        Vec nextVelocity=continuous?(newPosition-position)*(1/dt):Vec{};
        m.emissionTranslationValid=continuous&&velocityValid;
        m.emissionVerticalSpeed=m.emissionTranslationValid?Dot(nextVelocity,vertical)*metricScale:0;
        m.emissionVerticalAcceleration=m.emissionTranslationValid?
            Dot((nextVelocity-velocity)*(1/dt),vertical)*metricScale:0;
        x=newX;up=newUp;position=newPosition;velocity=nextVelocity;scale=metricScale;
        held=carrying;valid=frameValid&&dt>0;velocityValid=continuous;
        identity=frameIdentity;originIdentity=frameOrigin;
    }
};
}
