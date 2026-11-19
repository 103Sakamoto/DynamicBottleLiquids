#pragma once
#include "AdaptiveQuality.h"
#include <cstring>
namespace nuka {
// Retain only fields read by interpolation, and copy only live particles.
struct CompactHistory {
    V4 plane{},wave{},waveDetail{},wetFilm[64]{};
    V4 bubbles[256]{},bubbleShape[256]{};
    int bubbleCount{};float foam{};
    void Capture(const Constants& c){
        plane=c.plane;wave=c.wave;waveDetail=c.waveDetail;foam=c.foam;
        std::memcpy(wetFilm,c.wetFilm,sizeof(wetFilm));bubbleCount=c.bubbleCount;
        std::copy_n(c.bubbles,bubbleCount,bubbles);
        std::copy_n(c.bubbleShape,bubbleCount,bubbleShape);
    }
};
struct PreparedDraw {
    const Constants& current;
    const CompactHistory& previous;
    float fraction;bool interpolate;int raySteps;
    void Write(Constants& c) const {
        c=current;
        if(interpolate)Blend(previous,current,fraction,c);
        c.wave.w=float(raySteps);
    }
private:
    static void Blend(const CompactHistory& a,const Constants& b,float t,Constants& c){
    t=std::clamp(t,0.f,1.f);
    auto blend=[&](V4 x,V4 y){return V4{std::lerp(x.x,y.x,t),std::lerp(x.y,y.y,t),std::lerp(x.z,y.z,t),std::lerp(x.w,y.w,t)};};
    c.plane=blend(a.plane,b.plane);c.wave=blend(a.wave,b.wave);c.waveDetail=blend(a.waveDetail,b.waveDetail);c.wetInfo=b.wetInfo;for(int k=0;k<64;k++)c.wetFilm[k]=blend(a.wetFilm[k],b.wetFilm[k]);c.foam=std::lerp(a.foam,b.foam,t);
    // Only blend nearby corresponding particles; never bridge a removal/reorder.
    if(a.bubbleCount==b.bubbleCount)for(int k=0;k<b.bubbleCount;k++){
        auto x=a.bubbles[k],y=b.bubbles[k];float dx=x.x-y.x,dy=x.y-y.y,dz=x.z-y.z;
        if(dx*dx+dy*dy+dz*dz<.09f){c.bubbles[k]=blend(x,y);
            auto sx=a.bubbleShape[k],sy=b.bubbleShape[k];
            if(sx.w>0&&sy.w>0){if(sx.x*sy.x+sx.y*sy.y+sx.z*sy.z<0){sx.x=-sx.x;sx.y=-sx.y;sx.z=-sx.z;}auto shape=blend(sx,sy);auto axis=Normalize({shape.x,shape.y,shape.z});c.bubbleShape[k]={axis.x,axis.y,axis.z,shape.w};}
        }
    }
    }
};
}
