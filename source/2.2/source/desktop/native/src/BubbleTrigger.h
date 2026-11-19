#pragma once
#include "BubbleFlow.h"

namespace nuka {
inline constexpr float ShakeRadians(float degrees){return degrees*.0174532925199433f;}
// Preserve ShakeRotation's existing 20 rad/s safety cap (1145.9 degrees/s).
inline constexpr float ShakeUpperSpeed=20.f;
struct BubbleMix {float tiny=1,smallWeight=0,medium=0,big=0;};
inline BubbleMix ShakingMix(float speed){
    // Size selection only: require 25% more qualified motion speed for the
    // previous mixture. Emission, stroke gates, movement and safety cap stay put.
    // Applies to rotation and the existing vertical equivalent-speed signal.
    speed/=1.25f;
    constexpr BubbleMix anchors[]={{1,0,0,0},{.45f,.35f,.20f,0},
                                  {.25f,.15f,.35f,.25f},{.20f,0,.35f,.45f}};
    constexpr float speeds[]={ShakeRadians(360),ShakeRadians(600),ShakeRadians(900),ShakeUpperSpeed};
    if(speed<=speeds[0])return anchors[0];
    for(int i=1;i<4;i++)if(speed<speeds[i]){
        float t=ShakeSmooth(speeds[i-1],speeds[i],speed);auto a=anchors[i-1],b=anchors[i];
        return {std::lerp(a.tiny,b.tiny,t),std::lerp(a.smallWeight,b.smallWeight,t),
                std::lerp(a.medium,b.medium,t),std::lerp(a.big,b.big,t)};
    }
    return anchors[3];
}
inline float ShakingRadiusFraction(float speed,float category,float variation){
    auto mix=ShakingMix(speed);float r;
    if(category<mix.tiny)r=.12f+.16f*variation;
    else if(category<mix.tiny+mix.smallWeight)r=.28f+.14f*variation;
    else if(category<mix.tiny+mix.smallWeight+mix.medium)r=.42f+.23f*variation;
    else r=.65f+.35f*variation;
    // Slow rotation emits genuinely tiny bubbles, never acceleration-selected
    // large ones. At 360 degrees/s the full tiny category is available.
    return r*(.3f+.7f*ShakeSmooth(ShakeRadians(15),ShakeRadians(360),speed));
}
inline void ClearBubbleTrigger(Motion& m){
    m.rotationMemory=m.filteredAngularAcceleration=m.rotationStroke=0;
    m.verticalStroke=m.verticalStrokeSpeed=m.previousVerticalSpeed=0;
    m.bubbleSizeSpeed=m.emissionSpeedHold=m.bubbleSizeHold=0;
    m.verticalPeakAge=m.triggerSizeSpeed=0;m.triggerSources=0;
    m.previousEmissionOmega={};
}
inline void RememberShakeSpeed(float candidate,float& memory,float& hold,float dt){
    if(candidate>=memory&&candidate>0){memory=candidate;hold=.10f;}
    else {hold=std::max(0.f,hold-dt);if(hold==0)memory*=std::exp(-dt/.06f);}
}
struct BubbleExcitation {float drive{},sizeSpeed{};bool reversal=false;};
inline BubbleExcitation UpdateBubbleTrigger(Motion& m,float span,float dt){
    if(!m.emissionValid){ClearBubbleTrigger(m);return {};}
    float speed=std::min(ShakeUpperSpeed,Length(m.emissionOmega));
    bool reversed=Dot(m.emissionOmega,m.previousEmissionOmega)<0;
    if(reversed||speed<ShakeRadians(15))m.rotationStroke=0;
    if(speed>=ShakeRadians(15))m.rotationStroke=std::min(3.14159265f,m.rotationStroke+speed*dt);
    float travel=ShakeSmooth(ShakeRadians(3),ShakeRadians(8),m.rotationStroke);
    float rotation=speed*travel;
    // A one-frame jerk can create tiny bubbles; larger sizes need an actual
    // 15--35 degree stroke. Remember the qualified speed through a reversal.
    float sizeSpeed=std::lerp(std::min(speed,ShakeRadians(360)),speed,
        ShakeSmooth(ShakeRadians(15),ShakeRadians(35),m.rotationStroke))*travel;
    float vertical=0;
    if(m.emissionTranslationValid){
        float v=m.emissionVerticalSpeed/std::max(span,.001f);
        float a=m.emissionVerticalAcceleration/std::max(span,.001f);
        bool reverse=v*m.previousVerticalSpeed<0;
        m.verticalPeakAge+=dt;
        // A peak belongs to a recent, deliberate vertical stroke. Slow drift or
        // a late deceleration cannot spend an old fast stroke a second time.
        if(std::abs(v)<.5f||m.verticalPeakAge>.15f)m.verticalStroke=m.verticalStrokeSpeed=0;
        auto qualify=[&](){
            float stroke=ShakeSmooth(.04f,.12f,m.verticalStroke);
            float acceleration=ShakeSmooth(3,10,std::abs(a));
            // 0.5--5 cavity heights/s maps to the same size progression as
            // rotation. For a 20 cm cavity the upper speed is about 1 m/s.
            return ShakeUpperSpeed*std::clamp((m.verticalStrokeSpeed-.5f)/4.5f,0.f,1.f)*stroke*acceleration;
        };
        if(reverse&&std::abs(v)>=.5f){vertical=qualify();m.verticalStroke=m.verticalStrokeSpeed=0;}
        m.verticalStroke=std::min(2.f,m.verticalStroke+std::abs(v)*dt);
        if(std::abs(v)>=m.verticalStrokeSpeed){m.verticalStrokeSpeed=std::abs(v);m.verticalPeakAge=0;}
        if(std::abs(v)>=.5f&&v*a<0)vertical=std::max(vertical,qualify());
        if(std::abs(v)<.5f)m.verticalStroke=m.verticalStrokeSpeed=0;
        m.previousVerticalSpeed=v;reversed|=reverse&&vertical>0;
    }else m.verticalStroke=m.verticalStrokeSpeed=m.previousVerticalSpeed=m.verticalPeakAge=0;
    RememberShakeSpeed(std::max(rotation,vertical),m.rotationMemory,m.emissionSpeedHold,dt);
    RememberShakeSpeed(std::max(sizeSpeed,vertical),m.bubbleSizeSpeed,m.bubbleSizeHold,dt);
    m.previousEmissionOmega=m.emissionOmega;
    // Peak memory can bridge the stroke-length gate during a fast reversal,
    // but may never promote a currently slow rotation to the large size tier.
    float selectedSize=std::max(vertical,std::max(sizeSpeed,std::min(m.bubbleSizeSpeed,speed)*travel));
    m.triggerSizeSpeed=selectedSize;m.triggerSources=(rotation>ShakeRadians(15)?1u:0u)|(vertical>ShakeRadians(15)?2u:0u);
    // New particles require current qualified motion. The visual/current decay
    // is independent and can continue moving particles after this reaches zero.
    float drive=std::clamp((std::max(rotation,vertical)-ShakeRadians(15))/
        (ShakeRadians(600)-ShakeRadians(15)),0.f,1.f);
    return {drive,selectedSize,reversed};
}
}
