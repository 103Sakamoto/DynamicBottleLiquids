#pragma once
#include "Simulation.h"
#include "BubbleFlow.h"
#include <algorithm>
#include <cmath>
namespace nuka {
struct DistanceLod {
    bool detail=true,effects=true,initialized=false;
    float detailWeight=1,effectWeight=1;
    double time=0;
    void Update(float distance,double now){
        if(!std::isfinite(distance)){detail=effects=true;detailWeight=effectWeight=1;time=now;initialized=true;return;}
        if(detail){if(distance>=256)detail=false;}else if(distance<=224)detail=true;
        if(effects){if(distance>=768)effects=false;}else if(distance<768)effects=true;
        auto fade=[](float d,float begin,float end){float t=std::clamp((d-begin)/(end-begin),0.f,1.f);return 1-t*t*(3-2*t);};
        float a=detail?fade(distance,192,256):0,b=effects?fade(distance,512,768):0;
        float rise=initialized?float(std::clamp(now-time,0.0,.25))*5:1;
        detailWeight=std::min(a,detailWeight+rise);effectWeight=std::min(b,effectWeight+rise);
        time=now;initialized=true;
    }
};
inline void FadeBubbles(Constants& c,float pixels,float weight,bool held=false){
    float budget=std::clamp(pixels*(held?1.f:.5f),4.f,held?192.f:96.f)*weight;
    int count=std::min(c.bubbleCount,int(std::ceil(budget)));
    for(int k=0;k<count;k++)c.bubbles[k].w*=std::sqrt(std::clamp(budget-k,0.f,1.f)*weight);
    c.bubbleCount=count;
}
inline void PrepareBubbleGroups(Constants& c){
    // Sort only the submitted copy; particle identities and simulation interpolation stay intact.
    struct Entry{V4 particle,shape;};std::array<Entry,192> entries;
    for(int i=0;i<c.bubbleCount;i++)entries[i]={c.bubbles[i],c.bubbleShape[i]};
    std::sort(entries.begin(),entries.begin()+c.bubbleCount,[](const Entry& a,const Entry& b){return a.particle.z<b.particle.z;});
    for(int i=0;i<c.bubbleCount;i++){c.bubbles[i]=entries[i].particle;c.bubbleShape[i]=entries[i].shape;}
    for(int first=0;first<c.bubbleCount;first+=8){
        Vec lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};
        int end=std::min(first+8,c.bubbleCount);
        for(int k=first;k<end;k++){auto p=c.bubbles[k];float extent=BubbleExtent(p.w,c.bubbleShape[k].w);lo={std::min(lo.x,p.x-extent),std::min(lo.y,p.y-extent),std::min(lo.z,p.z-extent)};hi={std::max(hi.x,p.x+extent),std::max(hi.y,p.y+extent),std::max(hi.z,p.z+extent)};}
        auto center=(lo+hi)*.5f;float radius=Length(hi-lo)*.5f+.0001f;
        c.bubbleGroups[first/8]={center.x,center.y,center.z,radius};
    }
}
inline float ProjectedDiameter(float radius,float distance,float verticalSpan,float horizontalSpan,float width,float height){
    if(!std::isfinite(radius)||!std::isfinite(distance)||radius<=0||distance<=radius||verticalSpan<=0||horizontalSpan<=0||width<=0||height<=0)return 1e6f;
    float pixels=2*radius/(distance-radius)*std::max(height/verticalSpan,width/horizontalSpan);
    return std::isfinite(pixels)?pixels:1e6f;
}
inline int RayQuality(float pixels,int previous){
    // Hysteresis avoids quality chatter near screen-size boundaries.
    if(previous==48&&pixels<56)return 48;
    if(previous==72&&pixels>=40&&pixels<144)return 72;
    if(previous==96&&pixels>=112)return 96;
    return pixels<48?48:(pixels<128?72:96);
}
inline float SimulationInterval(float pixels,bool moved){return moved||pixels>=160?0.f:(pixels>=80?1.f/30:1.f/20);}
inline Constants InterpolateSimulation(const Constants& a,const Constants& b,float t){
    t=std::clamp(t,0.f,1.f);auto c=b;
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
    return c;
}
}
