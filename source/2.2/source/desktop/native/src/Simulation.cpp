#include "Simulation.h"
#include "WaveSizing.h"
#include "BubbleFlow.h"
#include "BubbleTrigger.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace nuka {
float Dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float Length(Vec a){return std::sqrt(Dot(a,a));}
Vec Normalize(Vec a){return a*(1/std::max(Length(a),0.00001f));}
static float Radius(const Constants& c,float z){
    for(int i=1;i<c.ringCount;i++)if(z>=c.rings[i-1].x&&z<=c.rings[i].x){auto a=c.rings[i-1],b=c.rings[i];return std::lerp(a.y,b.y,(z-a.x)/(b.x-a.x));}return 0;
}
void MakeSamples(Settings& s){
    s.wetSamples.clear();s.wetPhases.clear();s.wetRingCount=s.base.ringCount;s.wetFrequency=s.waveFrequency;
    std::copy_n(s.base.rings,16,s.wetRings.begin());
    float bottom=s.base.rings[0].x,span=std::max(s.base.rings[s.base.ringCount-1].x-bottom,.001f);
    for(int row=0;row<16;row++){
        float z=bottom+span*row/15,rad=Radius(s.base,z)*.98f;
        for(int k=0;k<16;k++){
            float a=k*6.283185307f/16;Vec q{std::cos(a)*rad,std::sin(a)*rad,z};
            s.wetSamples.push_back({q.x,q.y,q.z,MeniscusHeight(s.base,q)});
            s.wetPhases.push_back({(.8f*q.x+.6f*q.y)*s.waveFrequency,(-.48f*q.x+.64f*q.y+.6f*q.z)*s.waveFrequency*1.73f,(.36f*q.x-.48f*q.y+.8f*q.z)*s.waveFrequency*.4f,0});
        }
    }
    s.samples.clear();s.sampleWaveBasis.clear();s.sampleMediumBasis.clear();s.sampleWaveFrequency=s.waveFrequency;const auto& c=s.base;float dz=(c.rings[c.ringCount-1].x-c.rings[0].x)/64;
    for(int k=0;k<64;k++)for(int j=0;j<5;j++)for(int i=0;i<16;i++){
        // Stratify heights within each slice so an upright plane does not jump by an entire slice's volume.
        float z=c.rings[0].x+(k+(((j*16+i)*37%80)+.5f)/80)*dz;
        float R=Radius(c,z),w=3.14159265f*R*R*dz/80;
        float r=R*std::sqrt((j+.5f)/5),a=6.2831853f*(i+.5f+(k%2)*.25f)/16;s.samples.push_back({r*std::cos(a),r*std::sin(a),z,w});}
    s.sampleWaveBasis.reserve(s.samples.size());
    s.sampleMeniscus.clear();s.sampleMeniscus.reserve(s.samples.size());
    for(auto p:s.samples){float x=(.8f*p.x+.6f*p.y)*s.waveFrequency,y=(-.48f*p.x+.64f*p.y+.6f*p.z)*s.waveFrequency*1.73f;
        s.sampleMeniscus.push_back(MeniscusHeight(c,{p.x,p.y,p.z}));
        s.sampleWaveBasis.push_back({std::sin(x),std::cos(x),std::sin(y),std::cos(y)});float z=(.36f*p.x-.48f*p.y+.8f*p.z)*s.waveFrequency*.4f;s.sampleMediumBasis.push_back({std::sin(z),std::cos(z),0,0});
    }
}
float MeniscusHeight(const Constants& c,Vec p){
    float bottom=c.rings[0].x,top=c.rings[c.ringCount-1].x;
    float wall=std::min(Radius(c,std::clamp(p.z,bottom,top))-std::hypot(p.x,p.y),std::min(p.z-bottom,top-p.z));
    // Match LiquidCore.inc; cached samples retain volume conservation.
    float t=std::clamp(1.f-wall/.35f,0.f,1.f);
    return .06f*t*t*t;
}
static float Wave(Vec p,float time,float amplitude,float secondary,float frequency){if(amplitude==0&&secondary==0)return 0;return amplitude*std::sin((.8f*p.x+.6f*p.y)*frequency+time*5)+secondary*std::sin((-.48f*p.x+.64f*p.y+.6f*p.z)*frequency*1.73f-time*10.f)+.3f*amplitude*std::sin((.36f*p.x-.48f*p.y+.8f*p.z)*frequency*.4f+time*7.5f);}
// Retain existing particles; no new shader resources or unbounded emission.
static bool RetainSurfaceBubble(const Settings& s,Motion& m,Vec position,float radius){
    if(s.foamStrength<=0||s.intensity<=0||s.fill<=0||s.fill>=1||radius<=.001f||m.surfaceBubbles.size()>=m.bubbleLimits.foam)return false;
    m.surfaceBubbles.push_back({position,radius,0,radius});++m.foamRetained;return true;
}
static void ShakingBubbles(const Settings& s,Motion& m,Constants& c,Vec acceleration,Vec up,float dt,bool detail){
    m.shakingBirths=0;
    if(!detail||s.shakingBubbles<=0||s.fill<=0||s.fill>=1||s.samples.empty()){
        m.shaking.clear();m.shakeActivity=m.shakeCredit=m.previousShakeDrive=m.shakeBurstCooldown=0;
        ClearBubbleTrigger(m);m.flowEnergy=0;return;
    }
    if(dt<=0)return;
    const float span=c.rings[c.ringCount-1].x-c.rings[0].x;
    auto trigger=UpdateBubbleTrigger(m,span,dt);float drive=trigger.drive;
    m.shakeActivity+=(drive-m.shakeActivity)*(1-std::exp(-dt/(drive>m.shakeActivity?.015f:.09f)));
    m.flowEnergy=std::max(m.shakeActivity,m.flowEnergy*std::exp(-dt/.45f));
    m.shakeBurstCooldown=std::max(0.f,m.shakeBurstCooldown-dt);
    bool burst=drive>.55f&&m.shakeBurstCooldown==0&&(m.previousShakeDrive<.3f||trigger.reversal);
    // A gentle qualified vertical stroke may last only a few updates. Give its
    // onset one tiny-particle credit rather than requiring a post-motion tail
    // to accumulate enough credit. Strong strokes keep the existing burst.
    bool verticalOnset=drive>0&&m.previousShakeDrive==0&&(m.triggerSources&2);
    m.previousShakeDrive=drive;m.previousShakeOmega=m.emissionOmega;
    if(m.shaking.empty()&&m.shakeActivity<.001f){m.shakeActivity=m.shakeCredit=0;return;}
    const Vec normal{c.plane.x,c.plane.y,c.plane.z};
    const float primary=c.wave.x*c.waveDetail.y,secondary=c.waveDetail.x*c.waveDetail.z;
    const BubbleFlow flow(c,m,up,acceleration);
    auto clearance=[&](Vec q){return c.plane.w-Dot(q,normal)+Wave(q,m.time,primary,secondary,s.waveFrequency);};
    auto walls=[&](Vec q,float radius){
        if(q.z-radius<c.rings[0].x||q.z+radius>c.rings[c.ringCount-1].x)return false;
        float inner=std::min({Radius(c,q.z-radius),Radius(c,q.z),Radius(c,q.z+radius)});
        return std::hypot(q.x,q.y)+radius<inner;
    };
    auto contain=[&](ShakingParticle& p){
        float r=p.baseRadius*BubbleStretchLimit(p.baseRadius);
        float bottom=c.rings[0].x+r+.001f,top=c.rings[c.ringCount-1].x-r-.001f;
        if(bottom>=top)return false;
        if(p.position.z<bottom){p.position.z=bottom;p.velocity.z=std::max(0.f,p.velocity.z);}
        if(p.position.z>top){p.position.z=top;p.velocity.z=std::min(0.f,p.velocity.z);}
        float inner=std::min({Radius(c,p.position.z-r),Radius(c,p.position.z),Radius(c,p.position.z+r)})-r-.001f;
        if(inner<=.001f)return false;
        float radial=std::hypot(p.position.x,p.position.y);
        if(radial>inner){
            Vec n{p.position.x/radial,p.position.y/radial,0};
            p.position.x=n.x*inner;p.position.y=n.y*inner;
            p.velocity=p.velocity-n*std::max(0.f,Dot(p.velocity,n));
        }
        return true;
    };
    int steps=std::max(1,int(std::ceil(dt*120)));float h=dt/steps;
    for(auto& p:m.shaking){
        const float lifetime=ShakingLifetime(p.phase);
        float size=std::clamp(p.baseRadius/.3f,0.f,1.f);
        float response=1-std::exp(-h/(.012f+.035f*size));
        for(int step=0;step<steps;step++){
        p.age+=h;
        p.velocity=p.velocity+(flow.Target(p.position,span*(.30f+.70f*size))-p.velocity)*response;
        const Vec previousPosition=p.position;
        p.position=p.position+p.velocity*h;
        if(!contain(p)){
            // Previous valid location is a bounded, cheap collision fallback.
            p.position=previousPosition;p.velocity={};++m.shakeWallBlocked;
        }
        float gap=clearance(p.position);
            if(gap<=.65f*p.radius && RetainSurfaceBubble(s,m,p.position,p.radius)){
                ++m.shakeTransferred;p.radius=0;break;
            }
        if(gap<=.002f){++m.shakeSurfacePopped;p.radius=0;break;}
        float birth=std::clamp(p.age/.05f,0.f,1.f);birth=birth*birth*(3-2*birth);
        // Keep full size until contact when foam can accept the bubble.
        const bool foamAvailable=s.foamStrength>0&&s.intensity>0&&m.surfaceBubbles.size()<m.bubbleLimits.foam;
        float pop=foamAvailable?1.f:std::clamp(gap/std::max(p.baseRadius,.001f),0.f,1.f);
        p.radius=p.baseRadius*(.25f+.75f*birth)*pop*std::clamp((lifetime-p.age)/1.f,0.f,1.f);
        }
        if(p.age<lifetime&&p.radius>.001f)DeformBubble(p,flow,p.phase,m.time,dt);
    }
    std::erase_if(m.shaking,[&](const auto& p){if(p.age>=ShakingLifetime(p.phase)){++m.shakeTimedOut;return true;}return p.radius<.001f;});
    const float excitation=std::clamp(std::min(m.shakeActivity,drive),0.f,1.f);
    const int cap=std::min(int(m.bubbleLimits.shaking),8+int(std::ceil(56*excitation)));
    if(excitation>.025f&&m.emissionValid){
        if(burst){m.shakeCredit+=s.shakingBubbles*(3+9*excitation);m.shakeBurstCooldown=.06f;}
        else if(verticalOnset)m.shakeCredit+=s.shakingBubbles;
        m.shakeCredit=std::min(m.shakeCredit+dt*s.shakingBubbles*(12+200*excitation),24.f);
        int births=std::max(0,std::min(cap-int(m.shaking.size()),int(m.shakeCredit)));
        m.shakeCredit-=std::floor(m.shakeCredit);
        std::uniform_real_distribution<float> random(0,1);
        float maximum=std::min(.3f,c.rings[0].z*.12f);
        for(int i=0;i<births;i++){
            float category=random(m.shakeRandom),variation=random(m.shakeRandom);
            float radius=maximum*ShakingRadiusFraction(trigger.sizeSpeed,category,variation);
            float bound=radius*BubbleStretchLimit(radius);
            for(int attempt=0;attempt<24;attempt++){
            auto sample=s.samples[std::min(s.samples.size()-1,size_t(random(m.shakeRandom)*s.samples.size()))];
            Vec q{sample.x,sample.y,sample.z};
            float margin=bound*(1+(1.12f*c.wave.x+c.waveDetail.x*1.73f)*s.waveFrequency);
            float depth=clearance(q);
            if(!walls(q,bound)||depth<margin||depth>span*.28f+bound)continue;
            bool overlaps=false;for(const auto& p:m.shaking)if(Length(q-p.position)<(radius+p.baseRadius)*.75f){overlaps=true;break;}
            if(overlaps)continue;
            Vec velocity=flow.Target(q,span*.5f)-normal*(span*.8f);
            m.shaking.push_back({q,ShakeLimit(velocity,span*3),radius*.25f,radius,0,random(m.shakeRandom)*6.2831853f});++m.shakingBirths;break;
            }
        }
    }else m.shakeCredit=0;
}
Constants Simulate(const Settings& s,Motion& m,Vec acceleration,Vec up,float dt,bool detail,bool held,float motionWeight,SimulationTimings* timings){
    SimulationStageTimer timer(timings);
    ApplyBubbleLimits(m,m.bubbleLimits);
    Constants c=s.base;up=Normalize(up);dt=std::clamp(dt,0.f,.05f);float shakeDrive=0;
    if(dt>0){
        // Reject extreme single-update impulses, then integrate in seconds.
        float accelerationLength=Length(acceleration);
        if(accelerationLength>6000)acceleration=acceleration*(6000/accelerationLength);
        // Acceleration is transformed from world space by the adapter, never a rotated world position.
        float angular=Length(up-m.lastUp)/dt;
        float target=std::clamp(angular/2+Length(acceleration)/1500,0.f,1.f);
        shakeDrive=target;
        m.activity=target+(m.activity-target)*std::exp(-dt/(target>m.activity?.08f:s.settle));
        // Steer the primary and secondary weights; the broad third mode follows
        // the primary with its own direction, wavelength and clock.
        Vec lateral=acceleration-up*Dot(acceleration,up);
        Vec drive=lateral*(-1.f/1500)+(m.lastUp-up)*10;
        if(Length(drive)>.01f){
            drive=Normalize(drive);Vec wanted{Dot(drive,{.8f,.6f,0}),Dot(drive,{-.48f,.64f,.6f}),0};
            float peak=std::max(std::abs(wanted.x),std::abs(wanted.y));
            if(peak>.01f){wanted=wanted*(1/peak);float blend=m.waveEnergy==0?1.f:1-std::exp(-dt/.12f);m.waveMix=m.waveMix+(wanted-m.waveMix)*blend;}
        }
        // Test default: fast attack, envelope falls to 10 percent after 10 seconds.
        m.waveEnergy=std::max(target,m.waveEnergy*std::exp(-dt/4.342945f));
        m.rippleEnergy=std::max(target,m.rippleEnergy*std::exp(-dt/4.342945f));
        if(m.waveEnergy<.0001f)m.waveEnergy=0;
        if(m.rippleEnergy<.0001f)m.rippleEnergy=0;
        m.sloshVelocity=m.sloshVelocity+(m.lastUp-up)*(s.gain*4)+lateral*(-s.gain*.006f*dt);
        int steps=std::max(1,int(std::ceil(dt*240)));float step=dt/steps,w=6.2831853f*s.frequency*3.f;
        for(int i=0;i<steps;i++){m.sloshVelocity=m.sloshVelocity+(m.slosh*(-w*w)+m.sloshVelocity*(-.460517f))*step;m.slosh=m.slosh+m.sloshVelocity*step;
            float length=Length(m.slosh);if(length>s.maxTilt){m.slosh=m.slosh*(s.maxTilt/length);m.sloshVelocity=m.sloshVelocity*.3f;}}
        // Fully settled instances can reuse their cached volume solution.
        if(Length(m.slosh)<.00001f&&Length(m.sloshVelocity)<.00001f){m.slosh={};m.sloshVelocity={};}
        m.time+=dt;
    }
    m.lastUp=up;
    motionWeight=std::clamp(motionWeight,0.f,1.f);
    Vec normal=Normalize(up+m.slosh*motionWeight);float radius=WaveReferenceRadius(s);
    float amplitude=.09f*radius*1.1f*m.waveEnergy*WaveImpactBoost(m.waveEnergy)*motionWeight;
    float secondary=.55f*.09f*radius*.8f*(.65f*m.waveEnergy+.35f*std::min(m.rippleEnergy,m.waveEnergy))*WaveImpactBoost(m.waveEnergy)*motionWeight;
    float primaryWave=amplitude*m.waveMix.x,secondaryWave=secondary*m.waveMix.y;
    // Surface detail grows smoothly when a wave is excited. Its release follows
    // the existing fast fine-ripple decay instead of the longer broad slosh.
    float noiseTarget=detail&&s.fill>0&&s.fill<1?std::min(m.rippleEnergy,m.waveEnergy):0.f;
    if(dt>0){
        float response=noiseTarget>m.surfaceNoise?.12f:.06f;
        m.surfaceNoise+=(noiseTarget-m.surfaceNoise)*(1-std::exp(-dt/response));
        if(noiseTarget==0&&m.surfaceNoise<.0001f)m.surfaceNoise=0;
    }
    timer.End(0);
    float height=m.solvedHeight;
    bool reuse=amplitude==0&&m.solvedAmplitude==0&&s.fill==m.solvedFill&&
        normal.x==m.solvedNormal.x&&normal.y==m.solvedNormal.y&&normal.z==m.solvedNormal.z;
    if(!reuse){
        auto& values=m.volumeScratch;values.clear();values.reserve(s.samples.size());
        float low=1e30f,high=-1e30f;double total=0;
        bool cachedWave=s.sampleWaveBasis.size()==s.samples.size()&&s.sampleMediumBasis.size()==s.samples.size()&&s.sampleWaveFrequency==s.waveFrequency;
        float sx=0,cx=0,sy=0,cy=0,sz=0,cz=0;
        if(amplitude!=0&&cachedWave){sx=std::sin(m.time*5);cx=std::cos(m.time*5);sy=std::sin(-m.time*10.f);cy=std::cos(-m.time*10.f);sz=std::sin(m.time*7.5f);cz=std::cos(m.time*7.5f);}
        for(size_t index=0;index<s.samples.size();index++){
            auto p=s.samples[index];float wave=0;
            if(amplitude!=0){
                if(cachedWave){auto b=s.sampleWaveBasis[index];auto z=s.sampleMediumBasis[index];wave=primaryWave*(b.x*cx+b.y*sx)+secondaryWave*(b.z*cy+b.w*sy)+.3f*primaryWave*(z.x*cz+z.y*sz);}
                else wave=Wave({p.x,p.y,p.z},m.time,primaryWave,secondaryWave,s.waveFrequency);
            }
            float meniscus=s.sampleMeniscus.size()==s.samples.size()?s.sampleMeniscus[index]:MeniscusHeight(c,{p.x,p.y,p.z});
            float v=Dot({p.x,p.y,p.z},normal)-wave-meniscus;values.emplace_back(v,p.w);low=std::min(low,v);high=std::max(high,v);total+=p.w;
        }
        if(s.fill<=0)height=low-100;else if(s.fill>=1)height=high+100;else {
            // Bin once to narrow the height range; retain 18-bit bisection precision.
            auto selectedEnd=values.end();double fixedVolume=0;
            std::array<double,256> weights{};float width=(high-low)/256;
            if(width>0){
                for(auto [v,w]:values){int bin=std::clamp(int((v-low)/width),0,255);weights[bin]+=w;}
                double before=0,target=total*s.fill;int bin=0;
                for(;bin<255&&before+weights[bin]<target;bin++)before+=weights[bin];
                // Refinement only needs the selected histogram bin. Lower bins
                // contribute a fixed volume; upper bins cannot enter the result.
                float origin=low;fixedVolume=before;
                selectedEnd=std::partition(values.begin(),values.end(),[&](const auto& p){return std::clamp(int((p.first-origin)/width),0,255)==bin;});
                float start=low+bin*width;high=start+width;low=start;
            }
            for(int i=0;i<10;i++){float mid=(low+high)*.5f;double volume=fixedVolume;for(auto it=values.begin();it!=selectedEnd;++it){auto [v,w]=*it;volume+=(v<=mid?w:0.f);}if(volume/total<s.fill)low=mid;else high=mid;}
            height=(low+high)*.5f;
        }
        m.solvedNormal=normal;m.solvedHeight=height;m.solvedFill=s.fill;m.solvedAmplitude=amplitude;
    }
    c.plane={normal.x,normal.y,normal.z,height};c.wave={amplitude,s.waveFrequency,m.time,0};c.waveDetail={secondary,m.waveMix.x,m.waveMix.y,m.surfaceNoise};c.foam=0;
    timer.End(1);
    // A small per-instance wall history records contact, not merely agitation.
    if(!detail||s.fill<=0||s.fill>=1){m.wetFilm.fill(0);c.wetInfo={};}
    else{
        static const auto directions=[](){std::array<Vec,16> result{};for(int k=0;k<16;k++){float a=k*6.283185307f/16;result[k]={std::cos(a),std::sin(a),0};}return result;}();
        float bottom=c.rings[0].x,span=std::max(c.rings[c.ringCount-1].x-bottom,.001f),exposed=0,decay=std::exp(-dt/std::max(c.wetTuning.w,.1f));
        // Brief contact builds continuously; a zero-time solve cannot wet new glass.
        const float contactBlend=-std::expm1(-dt/.06f);
        bool cachedWall=s.wetSamples.size()==256 && s.wetRingCount==c.ringCount && std::memcmp(s.wetRings.data(),c.rings,sizeof(c.rings))==0;
        bool cachedPhase=cachedWall && s.wetPhases.size()==256 && s.wetFrequency==s.waveFrequency;
        for(int row=0;row<16;row++){
            float z=bottom+span*row/15,rad=cachedWall?0:Radius(c,z)*.98f;
            for(int k=0;k<16;k++){
                auto wall=cachedWall?s.wetSamples[row*16+k]:V4{};
                Vec q=cachedWall?Vec{wall.x,wall.y,wall.z}:Vec{directions[k].x*rad,directions[k].y*rad,z};
                float wave=0;
                if(primaryWave!=0||secondaryWave!=0){
                    if(cachedPhase){auto phase=s.wetPhases[row*16+k];wave=primaryWave*std::sin(phase.x+m.time*5)+secondaryWave*std::sin(phase.y-m.time*10.f)+.3f*primaryWave*std::sin(phase.z+m.time*7.5f);}
                    else wave=Wave(q,m.time,primaryWave,secondaryWave,s.waveFrequency);
                }
                float d=Dot(q,normal)-height-wave-(cachedWall?wall.w:MeniscusHeight(c,q));
                auto& wet=m.wetFilm[row*16+k];wet=d<=0?wet+(1.f-wet)*contactBlend:wet*decay;if(wet<.001f)wet=0;
                if(d>0)exposed=std::max(exposed,wet);
                auto& packed=c.wetFilm[row*4+k/4];switch(k%4){case 0:packed.x=wet;break;case 1:packed.y=wet;break;case 2:packed.z=wet;break;default:packed.w=wet;break;}
            }
        }
        c.wetInfo={exposed,bottom,15/span,0};
    }
    auto valid=[&](const Particle& p){float r=p.radius;auto q=p.position;if(s.fill<=0||q.z-r<c.rings[0].x||q.z+r>c.rings[c.ringCount-1].x)return false;
        float inner=std::min({Radius(c,q.z-r),Radius(c,q.z),Radius(c,q.z+r)});
        return std::hypot(q.x,q.y)+r<inner&&(s.fill>=1||Dot(q,normal)-Wave(q,m.time,primaryWave,secondaryWave,s.waveFrequency)+r*(1+(1.12f*amplitude+secondary*1.73f)*s.waveFrequency)<height);};
    timer.End(2);
    ShakingBubbles(s,m,c,acceleration,up,dt,detail);
    timer.End(3);
    const BubbleFlow bubbleFlow(c,m,up,acceleration);
    if(!detail||s.foamStrength<=0||s.intensity<=0||s.fill<=0||s.fill>=1)m.surfaceBubbles.clear();
    // Bounded tangential relaxation distributes foam without vertical stacking.
    // Four small iterations, at most 64*63/2 pairs each; no per-pixel neighbours.
    if(dt>0)for(int iteration=0;iteration<4;iteration++){
        for(size_t a=0;a<m.surfaceBubbles.size();a++)for(size_t b=a+1;b<m.surfaceBubbles.size();b++){
            auto& x=m.surfaceBubbles[a];auto& y=m.surfaceBubbles[b];
            Vec d=y.position-x.position;d=d-normal*Dot(d,normal);float length=Length(d);
            float target=(x.radius+y.radius)*1.02f;
            if(length>=target)continue;
            if(length<.00001f){
                float angle=float(a*37+b*17)*2.399963f;Vec seed{std::cos(angle),std::sin(angle),.37f};
                d=Normalize(seed-normal*Dot(seed,normal));
                if(Length(d)<.1f)d=Normalize(Vec{normal.z,0,-normal.x});
            }else d=d*(1/length);
            Vec correction=d*((target-length)*.5f*std::min(dt*30.f,1.f));
            x.position=x.position-correction;y.position=y.position+correction;
        }
    }
    auto surfacePoint=[&](Vec q,float radius){
        for(int i=0;i<3;i++){
            float gap=height-Dot(q,normal)+Wave(q,m.time,primaryWave,secondaryWave,s.waveFrequency);
            q=q+normal*(gap-radius*.65f);
        }
        return q;
    };
    auto clearance=[&](Vec q,float extent){
        return std::min({Radius(c,q.z-extent),Radius(c,q.z),Radius(c,q.z+extent)})-std::hypot(q.x,q.y)-extent-.002f;
    };
    // Find an interior surface anchor from the authored cavity, including horizontal
    // bottles where intersecting the bottle axis with the plane is ill-conditioned.
    Vec anchor{};float best=-1e30f;
    if(!m.surfaceBubbles.empty())for(int i=1;i<c.ringCount;i++){
        Vec q=surfacePoint({0,0,(c.rings[i-1].x+c.rings[i].x)*.5f},0);
        float room=clearance(q,.01f);
        if(room>best){best=room;anchor=q;}
    }
    for(size_t index=0;index<m.surfaceBubbles.size();index++){
        auto& p=m.surfaceBubbles[index];p.age+=dt;
        const float extent=p.radius*1.3f;
        Vec center=surfacePoint(anchor,p.radius);
        Vec outward=p.position-center;outward=outward-normal*Dot(outward,normal);
        if(Length(outward)<.001f){
            float angle=float(index)*2.399963f;Vec seed{std::cos(angle),std::sin(angle),.37f};
            outward=seed-normal*Dot(seed,normal);
            if(Length(outward)<.1f)outward={normal.z,0,-normal.x};
        }
        outward=Normalize(outward);
        // The target is just inside the actual tilted liquid/cavity intersection.
        float range=2*(c.rings[c.ringCount-1].x-c.rings[0].x+c.rings[0].z);
        if(clearance(center,extent)<0){++m.foamFitRejected;p.age=10;continue;}
        float lo=0,hi=range;
        for(int step=0;step<12;step++){
            float mid=(lo+hi)*.5f;
            if(clearance(surfacePoint(center+outward*mid,p.radius),extent)>=0)lo=mid;else hi=mid;
        }
        Vec edge=surfacePoint(center+outward*lo,p.radius);
        Vec drift=bubbleFlow.Current(p.position);drift=drift-normal*Dot(drift,normal);
        // Keep only along-rim current near the wall; no independent perpetual spin.
        float nearEdge=std::clamp(Length(p.position-center)/std::max(lo,.001f),0.f,1.f);
        drift=drift-outward*(Dot(drift,outward)*nearEdge);
        Vec candidate=surfacePoint(p.position+(edge-p.position)*(1-std::exp(-dt*1.8f))+drift*(dt*.25f),p.radius);
        if(clearance(candidate,extent)<0){
            // Constrain contact rather than killing foam at the wall.
            float a=0,b=1;
            for(int step=0;step<10;step++){
                float t=(a+b)*.5f;Vec q=surfacePoint(center+(candidate-center)*t,p.radius);
                if(clearance(q,extent)>=0)a=t;else b=t;
            }
            candidate=surfacePoint(center+(candidate-center)*a,p.radius);
        }
        p.position=candidate;
        p.visibleRadius=p.radius*std::clamp((10-p.age)/2.f,0.f,1.f);
    }
    std::erase_if(m.surfaceBubbles,[](const auto& p){return p.age>=10;});
    timer.End(4);
    if(!detail||(!s.shotBubbles&&s.intensity<=0)||s.fill<=0){m.carbCleared+=static_cast<unsigned>(m.bubbles.size());m.bubbles.clear();m.credit=0;m.bubbleSourceValid.fill(false);}else {
        for(auto& p:m.bubbles){
            const Vec previousPosition=p.position;
            if(dt>0){if(s.shakingBubbles>0&&m.flowEnergy>.001f){
                int steps=std::max(1,int(std::ceil(dt*120)));float h=dt/steps;
                for(int step=0;step<steps;step++){
                    Vec current=bubbleFlow.Current(p.position);
                    p.velocity=p.velocity+(current-p.velocity)*(1-std::exp(-h/.012f));
                    p.position=p.position+(up*(s.rise*p.speed)+p.velocity)*h;
                    float maximumRadius=s.shotBubbles?std::min(s.radiusMax,p.baseRadius*1.15f):std::max(p.baseRadius,s.radiusMax);
                    float extent=maximumRadius*BubbleStretchLimit(maximumRadius);
                    float low=c.rings[0].x+extent+.001f,high=c.rings[c.ringCount-1].x-extent-.001f;
                    if(low>=high){++m.carbContain;p.position=previousPosition;p.velocity={};break;}
                    p.position.z=std::clamp(p.position.z,low,high);
                    float inner=std::min({Radius(c,p.position.z-extent),Radius(c,p.position.z),Radius(c,p.position.z+extent)})-extent-.001f;
                    float radial=std::hypot(p.position.x,p.position.y);
                    if(inner<=0){++m.carbContain;p.position=previousPosition;p.velocity={};break;}
                    if(radial>inner){Vec n{p.position.x/radial,p.position.y/radial,0};p.position.x=n.x*inner;p.position.y=n.y*inner;p.velocity=p.velocity-n*std::max(0.f,Dot(p.velocity,n));}
                }
                if(p.age<=20)DeformBubble(p,bubbleFlow,p.speed*13,m.time,dt);
            }else {p.position=p.position+up*(s.rise*p.speed*dt);p.velocity={};p.stretch=1;p.stretchVelocity=0;}}
            // Constrain the cavity in both quiet and agitated motion.
            const float ownRadius=std::min(s.radiusMax,p.baseRadius*1.15f);
            const float extent=s.shotBubbles?ownRadius*(s.shakingBubbles>0&&m.flowEnergy>.001f?BubbleStretchLimit(ownRadius):1.f):s.radiusMax*BubbleStretchLimit(s.radiusMax);
            float lo=c.rings[0].x+extent+.001f,hi=c.rings[c.ringCount-1].x-extent-.001f;
            if(lo<hi){
                p.position.z=std::clamp(p.position.z,lo,hi);
                float inner=std::min({Radius(c,p.position.z-extent),Radius(c,p.position.z),Radius(c,p.position.z+extent)})-extent-.001f;
                float radial=std::hypot(p.position.x,p.position.y);
                if(inner<=0){p.position=previousPosition;p.velocity={};++m.carbContain;}
                else if(radial>inner){p.position.x*=inner/radial;p.position.y*=inner/radial;p.velocity={};}
            }else {p.position=previousPosition;p.velocity={};++m.carbContain;}
            p.age+=dt;
            float grown=std::min(s.radiusMax,p.baseRadius*(1+.15f*std::min(p.age/3,1.f)));
            float birth=std::clamp(p.age/.2f,0.f,1.f);
            float surface=height-Dot(p.position,normal)+Wave(p.position,m.time,primaryWave,secondaryWave,s.waveFrequency);
            if(surface<=.65f*p.radius && RetainSurfaceBubble(s,m,p.position,p.radius)){++m.carbTransfer;p.age=21;p.radius=0;continue;}
            // Shrink near the free surface instead of abruptly removing a full ring.
            float shapeBound=s.shakingBubbles>0&&m.flowEnergy>.001f?BubbleStretchLimit(grown):1.f;
            float pop=s.fill>=1?1.f:std::clamp(surface/std::max(grown*shapeBound,.001f),0.f,1.f);
            if(s.foamStrength>0 && m.surfaceBubbles.size()<m.bubbleLimits.foam && surface>0)pop=1.f;
            // Rate-limit shrinking when a wave crosses a whole group.
            const float target=grown*(.4f+.6f*birth)*pop;
            if(target<p.radius){p.radius=std::max(target,p.radius-dt*grown/.15f);continue;}
            p.radius=grown*(.4f+.6f*birth)*pop;
        }
        std::erase_if(m.bubbles,[&](const auto& p){if(p.age>20){if(p.age<21)++m.carbExpiry;return true;}
            if(p.radius<.001f){++m.carbTiny;return true;}
            return false;});
        // Independent fixed slots: births never evict another category.
        const int cap=int(m.bubbleLimits.carbonation);
        if(!s.shotBubbles)m.credit+=dt*s.intensity*(s.baseline+s.movement*m.activity*(held?2.f:1.f));int spawn=std::min(cap-int(m.bubbles.size()),int(m.credit));m.credit-=std::floor(m.credit);
        std::uniform_real_distribution<float> random(0,1);
        auto sample=[&](){auto q=s.samples[std::min(s.samples.size()-1,size_t(random(m.random)*s.samples.size()))];return Vec{q.x,q.y,q.z};};
        if(s.shotBubbles){
            // Canonical tank axis, just above the bottom feed opening. Reject
            // uncovered sources rather than making bubbles appear in the air.
            const auto& cfg=s.shotConfig;
            const unsigned perShot=cfg.bigCount+cfg.smallCount;
            const unsigned births=std::min(24u,m.pendingShots*perShot);
            for(unsigned i=0;i<births&&m.bubbles.size()<m.bubbleLimits.carbonation;++i){
                const unsigned slot=i%perShot;const bool big=slot<cfg.bigCount;
                const unsigned groupIndex=big?slot:slot-cfg.bigCount;
                float angle=6.2831853f*groupIndex/std::max(1u,big?cfg.bigCount:cfg.smallCount);
                float lo=big?cfg.bigRadiusMin:cfg.smallRadiusMin,hi=big?cfg.bigRadiusMax:cfg.smallRadiusMax;
                float radius=lo+(hi-lo)*random(m.random);
                // Separate the larger spheres around the inlet plume so they do
                // not visually merge into one bubble. Small bubbles start below.
                float spread=big?.36f:.14f;if(!big)angle+=1.0471976f;
                Vec source{spread*std::cos(angle),spread*std::sin(angle),c.rings[0].x+(big?.85f:.45f)+.05f*(i%3)};
                float slow=big?cfg.bigSpeedMin:cfg.smallSpeedMin,fast=big?cfg.bigSpeedMax:cfg.smallSpeedMax;
                Particle particle{source,radius,slow+(fast-slow)*random(m.random),0,radius};
                if(valid(particle))m.bubbles.push_back(particle);
            }
            spawn=0;m.credit=0;
        }
        for(int i=0;i<spawn;i++){
            unsigned site=std::min(3u,unsigned(random(m.random)*4));
            // Recurring sites make trails. Re-select when tilting uncovers a site;
            // choose lower valid samples without a per-frame scan of the cavity.
            Particle source{m.bubbleSources[site],s.radiusMax,1,0};
            if(!m.bubbleSourceValid[site]||!valid(source)){
                m.bubbleSourceValid[site]=false;float lowest=1e30f;
                for(int candidate=0;candidate<12;candidate++){
                    Vec q=sample();Particle test{q,s.radiusMax,1,0};float level=Dot(q,up);
                    if(valid(test)&&level<lowest){lowest=level;m.bubbleSources[site]=q;m.bubbleSourceValid[site]=true;}
                }
            }
            for(int attempt=0;attempt<12;attempt++){
                bool trail=m.bubbleSourceValid[site]&&random(m.random)>(.15f+.7f*m.activity);
                Vec q=trail?m.bubbleSources[site]:sample();
                float size=.65f+.35f*random(m.random);
                float radius=s.radiusMax;
                Particle particle{q,radius*.4f,.7f+.6f*size,0,radius};
                if(valid(particle)){m.bubbles.push_back(particle);break;}
            }
        }
    }
    m.pendingShots=0; // Consume once, including dry/culled/zero-quota draws.
    timer.End(5);
    // Independent caps: 64 agitation + 64 foam + 128 carbonation = 256
    // Screen/distance submission budgets remain independently applied per category.
    c.shakingCount=int(m.shaking.size());c.surfaceCount=int(m.surfaceBubbles.size());c.carbonationCount=int(m.bubbles.size());
    c.bubbleCount=0;
    for(const auto& p:m.shaking){int k=c.bubbleCount++;c.bubbles[k]={p.position.x,p.position.y,p.position.z,p.radius};c.bubbleShape[k]={p.shapeAxis.x,p.shapeAxis.y,p.shapeAxis.z,p.stretch};}
    for(const auto& p:m.surfaceBubbles){int k=c.bubbleCount++;c.bubbles[k]={p.position.x,p.position.y,p.position.z,p.visibleRadius};c.bubbleShape[k]={normal.x,normal.y,normal.z,-.65f};}
    for(const auto& p:m.bubbles){int k=c.bubbleCount++;c.bubbles[k]={p.position.x,p.position.y,p.position.z,p.radius};if(s.shakingBubbles>0&&m.flowEnergy>.001f)c.bubbleShape[k]={p.shapeAxis.x,p.shapeAxis.y,p.shapeAxis.z,p.stretch};}
    if(c.gradientInfo.x>=2){
        float bottom=1e30f,radial=std::hypot(c.plane.x,c.plane.y);
        for(int i=0;i<c.ringCount;i++)bottom=std::min(bottom,c.plane.z*c.rings[i].x-radial*c.rings[i].y);
        c.gradientInfo.y=bottom;c.gradientInfo.z=1/std::max(c.plane.w-bottom,.001f);
    }
    ApplyDetailDistance(c,detail);
    timer.End(6);
    return c;
}
}



