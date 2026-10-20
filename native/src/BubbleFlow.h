#pragma once
#include "ShakeMotion.h"

namespace nuka {
inline float ShakeSmooth(float low,float high,float value){
    float t=std::clamp((value-low)/(high-low),0.f,1.f);return t*t*(3-2*t);
}
// Hill's spherical vortex, evaluated in the stationary frame. This is the
// analytical stream-function construction used by Thuerey et al., SCA 2007,
// adapted to a small shared set of vortices instead of all bubble-pair forces.
// Dimensionless q uses the core radius; the exterior is a decaying dipole.
// Inside/outside velocities agree at |q|=1 and each branch is divergence free.
inline Vec HillVortex(Vec q,Vec axis){
    float z=Dot(q,axis),r2=Dot(q,q);Vec radial=q-axis*z;
    if(r2<1)return radial*(1.5f*z)+axis*(2.5f-3*(r2-z*z)-1.5f*z*z);
    float inv=1/std::sqrt(r2),inv3=inv*inv*inv,inv5=inv3*inv*inv;
    return radial*(1.5f*z*inv5)+axis*(1.5f*z*z*inv5-.5f*inv3);
}
struct BubbleFlow {
    Vec centres[2]{},axis{0,1,0},up{0,0,1},pressure{0,0,1500};
    Vec riseDirection{0,0,1};float riseFactor=1;
    float core{},speed{},span{},energy{};
    BubbleFlow(const Constants& c,const Motion& m,Vec gravityUp,Vec acceleration){
        up=gravityUp;span=c.rings[c.ringCount-1].x-c.rings[0].x;energy=m.flowEnergy;
        float radius=c.rings[0].z;if(radius<=0)for(int i=0;i<c.ringCount;i++)radius=std::max(radius,c.rings[i].y);
        core=std::max(.25f,std::min(span*.27f,radius*.9f));
        Vec circulation=ShakeCross(up,m.sloshVelocity)*1.8f-m.shakeOmega*.55f;
        axis=Length(circulation)>.05f?Normalize(circulation):Normalize(Vec{.3f,.9f,.15f});
        Vec side=Normalize(ShakeCross(axis,up));if(Length(side)<.1f)side={1,0,0};
        Vec mid{0,0,(c.rings[0].x+c.rings[c.ringCount-1].x)*.5f};
        centres[0]=mid-up*(span*.14f)+side*(radius*.20f);
        centres[1]=mid+up*(span*.17f)-side*(radius*.25f);
        speed=span*energy*(.18f+.07f*std::min(Length(circulation),5.f));
        pressure=up*1500.f+acceleration; // existing visual acceleration reference, not game gravity
        float pressureLength=Length(pressure);riseDirection=pressure*(1/std::max(pressureLength,.00001f));
        riseFactor=std::sqrt(std::clamp(pressureLength/1500.f,0.f,4.f));
    }
    Vec Current(Vec q)const{
        if(energy<.0001f)return {};
        return (HillVortex((q-centres[0])*(1/core),axis)-
                HillVortex((q-centres[1])*(1/core),axis)*.7f)*speed;
    }
    Vec Target(Vec q,float rise)const{
        return ShakeLimit(Current(q)+riseDirection*(rise*riseFactor),span*3);
    }
};
inline float BubbleExtent(float radius,float stretch){
    return stretch>.001f?radius*std::max(stretch,1/std::sqrt(stretch)):radius;
}
inline float BubbleStretchLimit(float radius){return 1+.65f*std::clamp(radius/.3f,0.f,1.f);}
template<class ParticleType> inline void DeformBubble(ParticleType& p,const BubbleFlow& flow,float phase,float time,float dt){
    float size=std::clamp(p.baseRadius/.3f,0.f,1.f),limit=BubbleStretchLimit(p.baseRadius);
    Vec direction=Normalize(p.velocity);if(Length(direction)<.1f)direction=flow.up;
    // A directional velocity gradient supplies local strain, rather than adding
    // independent per-particle position noise. Small bubbles resist deformation.
    Vec shear=(flow.Current(p.position+direction*p.baseRadius)-flow.Current(p.position-direction*p.baseRadius));
    Vec wanted=Normalize(direction+shear*(1/std::max(flow.span*.15f,.01f)));
    if(Dot(wanted,p.shapeAxis)<0)wanted=wanted*(-1);
    p.shapeAxis=Normalize(p.shapeAxis+(wanted-p.shapeAxis)*(1-std::exp(-dt/.06f)));
    float target=1+size*flow.energy*(.16f+.44f*std::sin(time*(9+5*(1-size))+phase));
    target=std::clamp(target,1/limit,limit);
    float frequency=18+8*(1-size);
    int steps=std::max(1,int(std::ceil(dt*120)));float h=dt/steps;
    for(int step=0;step<steps;step++){
        p.stretchVelocity+=(frequency*frequency*(target-p.stretch)-1.6f*frequency*p.stretchVelocity)*h;
        p.stretch=std::clamp(p.stretch+p.stretchVelocity*h,1/limit,limit);
        if((p.stretch==limit&&p.stretchVelocity>0)||(p.stretch==1/limit&&p.stretchVelocity<0))p.stretchVelocity=0;
    }
}
}
