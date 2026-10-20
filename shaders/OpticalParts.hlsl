#ifdef DIRECT_DISTANT
#define OPTICS_MODE 2
#else
#define OPTICS_MODE Optics.w
#endif
#include "LiquidHighlight.hlsli"
#include "NativeDistortion.hlsli"
// FO4 1.11.240 vanilla BSEffectShader adapter. Geometry constants stay owned by the game.
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define mix lerp
#define TRACE_STEPS 96
#define BUBBLE_CAPACITY 192
#define BUBBLE_GROUP_CULL
#define BUBBLE_DEFORMATION
#define NUKA_SCENE_TRANSMISSION
cbuffer BottleConstants : register(b13) {
    row_major float4x4 MeshToSimulation;
    float4 EyeLocal;
    float4 uPlane;
    float4 uRings[16];
    float4 Wave;                // amplitude, spatial frequency, time, unused
    float4 Optics;              // glass IOR, liquid IOR, glass thickness, unused
    float4 Trace;               // max local ray length, fill fraction, unused, unused
    float4 Absorption;
    float4 Scatter;
    float4 Emission;
    float4 GlassAbsorption;
    float4 FoamColor;
    float4 uBubbles[192];        // local-space sphere center + radius; no bubble meshes
    int uBubbleCount;
    float3 BubblePadding;
    int uRingCount;
    float uCapZ;
    float uFoam;
    int ReservedDebug; // Preserve the established constant-buffer layout.
    float4 MeshToWorld[3];
    float4 SurfaceDirection[4];
    float4 SurfaceLight[4];
    row_major float4x4 SunMatrices[4];
    float4 SunBounds[4],SunInfo,SunSlices,SunVolume,WorldOrigin,MaterialFlags;
    float4 BubbleGroups[24];
    float4 WetFilm[64];
    float4 WetInfo;
    float4 WetTuning;
    float4 WaveDetail;
    float4 SurfaceEnvironment; // authored cube strength, lighting influence, LIGHTING, reserved
    row_major float3x4 SimulationToMesh;
    float4 SurfaceSpecular,SurfaceResponse,SurfaceAlpha,SurfaceFalloff;
    float4 BubbleShape[192];
};
#define uWave Wave.xyz
#define SECONDARY_WAVE_AMPLITUDE WaveDetail.x
#define PRIMARY_WAVE_GAIN WaveDetail.y
#define SECONDARY_WAVE_GAIN WaveDetail.z
#define uOptics Optics.xyz
#define uTrace Trace.xy
#define uAbsorption Absorption.xyz
#define uScatter Scatter.xyz
#define uEmission float3(0,0,0) // Native emission is applied after incident lighting.
#define uGlassAbsorption GlassAbsorption.xyz
#define uFoamColor FoamColor.xyz
// Conservative cavity box: retain the original sample lattice, skip only
// sample positions that cannot lie inside any profile ring.
int colorTraceSteps(){return Wave.w>0?clamp((int)Wave.w,48,96):96;}
void traceRange(float3 origin,float3 direction,int steps,out int first,out int last) {
    float radius=uRings[0].z;
    if(radius<=0)for(int i=0;i<uRingCount;i++)radius=max(radius,uRings[i].y);
    float3 low=float3(-radius,-radius,uRings[0].x),high=float3(radius,radius,uRings[uRingCount-1].x);
    float nearT=-Trace.x*.5,farT=Trace.x*.5;
    for(int axis=0;axis<3;axis++){
        if(abs(direction[axis])<1e-8){if(origin[axis]<low[axis]||origin[axis]>high[axis]){first=0;last=0;return;}}
        else{float a=(low[axis]-origin[axis])/direction[axis],b=(high[axis]-origin[axis])/direction[axis];nearT=max(nearT,min(a,b));farT=min(farT,max(a,b));}
    }
    float stepSize=Trace.x/steps;
#ifndef LIQUID_RANGE_REFERENCE
    // A wave is bounded by |amplitude| * (1 + .5). Above this plane
    // every liquid field endpoint is outside. Keep a full lattice segment
    // of padding so endpoint interpolation and grazing rays remain intact.
    if(Trace.y>0 && Trace.y<1){
        float slope=dot(direction,uPlane.xyz);
        float height=uPlane.w+1.5*abs(Wave.x)+0.06+0.0001;
        float startHeight=dot(origin,uPlane.xyz);
        if(abs(slope)>1e-8){
            float crossing=(height-startHeight)/slope;
            if(slope>0)farT=min(farT,crossing+stepSize);
            else nearT=max(nearT,crossing-stepSize);
        }else if(startHeight-abs(slope)*Trace.x*.5>height){first=0;last=0;return;}
    }
#endif
    first=clamp((int)floor((nearT+Trace.x*.5)/stepSize),0,steps);
    last=clamp((int)ceil((farT+Trace.x*.5)/stepSize),0,steps);
    last=max(first,last);
}
#include "LiquidCore.inc"
#include "BottleLens.hlsli"
// Explicit snapshot of the original colour-pass transform. Never bind native utility b2 here.
cbuffer NativeGeometry : register(b10) {
    row_major float4x4 WorldViewProj;
    row_major float3x4 ModelView;
};
Texture2D<float2> SurfaceNoise : register(t16);
SamplerState NoiseSampler : register(s13);
cbuffer OpticalViewport : register(b11) { float2 TargetSize; float2 ViewportOrigin; };
SamplerState SceneSampler : register(s15);
TextureCube<float4> Environment : register(t14);
SamplerState EnvironmentSampler : register(s14);
Texture2D<float4> SurfaceBase : register(t0);
Texture2D<float4> SurfaceNormal : register(t1);
Texture2D<float4> SurfaceMask : register(t7);
#define SurfaceSampler EnvironmentSampler
#include "SurfaceEnvironment.hlsli"
Texture2DArray<float> SunDepth : register(t20);
SamplerComparisonState SunSampler : register(s10);
struct VertexIn { float4 position : POSITION; float4 normal : NORMAL; float2 uv:TEXCOORD0; float4 bitangent:BINORMAL; };
struct VertexOut { float4 position : SV_Position; float3 local : TEXCOORD0; float3 normal : TEXCOORD1; nointerpolation float3 eye : TEXCOORD2; float3 viewNormal : TEXCOORD3; float3 worldNormal : TEXCOORD4; float3 worldIncoming : TEXCOORD5; float2 uv:TEXCOORD6; float3 worldPosition:TEXCOORD7; nointerpolation float4 clipX:TEXCOORD8; nointerpolation float4 clipY:TEXCOORD9; nointerpolation float4 clipZ:TEXCOORD10; nointerpolation float4 clipOrigin:TEXCOORD11; float3 cubeTangent:TEXCOORD12; float3 cubeBitangent:TEXCOORD13; float3 cubeNormal:TEXCOORD14; float4 cubeEyeFalloff:TEXCOORD15; };
VertexOut VSMain(VertexIn v) {
    VertexOut o;
    o.position=mul(WorldViewProj,float4(v.position.xyz,1));
    o.local=mul(MeshToSimulation,float4(v.position.xyz,1)).xyz;
    // Invert the affine simulation transform once per vertex. Clip basis is
    // constant per draw and handles rotated/translated compatibility meshes.
    float3 a=MeshToSimulation[0].xyz,b=MeshToSimulation[1].xyz,c=MeshToSimulation[2].xyz;
    float determinant=dot(a,cross(b,c));
    float invDet=abs(determinant)>1e-8?1/determinant:0;
    float3 ix=cross(b,c)*invDet,iy=cross(c,a)*invDet,iz=cross(a,b)*invDet;
    float3 center=-(ix*MeshToSimulation[0].w+iy*MeshToSimulation[1].w+iz*MeshToSimulation[2].w);
    o.clipX=mul(WorldViewProj,float4(ix,0));o.clipY=mul(WorldViewProj,float4(iy,0));o.clipZ=mul(WorldViewProj,float4(iz,0));o.clipOrigin=mul(WorldViewProj,float4(center,1));
    o.normal=mul((float3x3)MeshToSimulation,v.normal.xyz*2-1);
    float3 t=float3(ModelView[0].w,ModelView[1].w,ModelView[2].w);
    float3x3 r=(float3x3)ModelView;
    float3 eye=-mul(t,r)/max(dot(r[0],r[0]),1e-8);
    if(BubblePadding.z>0)eye=float3(MeshToWorld[0].w,MeshToWorld[1].w,MeshToWorld[2].w);
    o.eye=mul(MeshToSimulation,float4(eye,1)).xyz;
    o.viewNormal=mul(r,v.normal.xyz*2-1);
    float3x3 world=float3x3(MeshToWorld[0].xyz,MeshToWorld[1].xyz,MeshToWorld[2].xyz);
    o.worldNormal=mul(world,v.normal.xyz*2-1);
    o.worldIncoming=mul(world,v.position.xyz-eye);
    o.worldPosition=mul(world,v.position.xyz*WorldOrigin.w)+WorldOrigin.xyz;o.uv=v.uv;
    // Surface-only native Effect frame. Clip/local/eye transforms above are unchanged.
    float3 packedTangent=float3(v.position.w,v.normal.w*2-1,v.bitangent.w*2-1);
    o.cubeTangent=mul(r,packedTangent);
    o.cubeBitangent=mul(r,v.bitangent.xyz*2-1);
    o.cubeNormal=mul(r,v.normal.xyz*2-1);
    float3 nativePosition=mul(ModelView,float4(v.position.xyz,1));
    float3 nativeEye=-nativePosition*rsqrt(max(dot(nativePosition,nativePosition),1e-12));
    float falloff=1;
    if(SurfaceAlpha.y>.5){
        float denominator=SurfaceFalloff.y-SurfaceFalloff.x;
        float x=abs(denominator)>1e-8?saturate((abs(dot(normalize(o.cubeNormal),nativeEye))-SurfaceFalloff.x)/denominator):0;
        falloff=lerp(SurfaceFalloff.z,SurfaceFalloff.w,x*x*(3-2*x));
    }
    o.cubeEyeFalloff=float4(nativeEye,falloff);
    return o;
}
float sunVisibility(float3 position){
    for(int i=0;i<(int)SunInfo.x;i++){
        float4 p=mul(float4(position,1),SunMatrices[i]);
        if(abs(p.w)<1e-6)continue;
        float3 q=p.xyz/p.w;float2 pad=1.5/SunInfo.zw;
        if(all(q.xy>SunBounds[i].xy+pad)&&all(q.xy<SunBounds[i].zw-pad)&&q.z>0&&q.z<1){
            return SunDepth.SampleCmpLevelZero(SunSampler,float3(q.xy,SunSlices[i]),q.z-SunInfo.y);
        }
    }
    return 1;
}
float3 mappedNormal(VertexOut v,float3 n){
    if(MaterialFlags.y<.5)return n;
    float2 xy=SurfaceNormal.Sample(SurfaceSampler,v.uv).xy*2-1;
    float3 dp1=ddx(v.worldIncoming),dp2=ddy(v.worldIncoming);
    float2 uv1=ddx(v.uv),uv2=ddy(v.uv);
    float3 a=cross(dp2,n),b=cross(n,dp1);
    float3 T=a*uv1.x+b*uv2.x,B=a*uv1.y+b*uv2.y;
    float scale=rsqrt(max(max(dot(T,T),dot(B,B)),1e-12));
    return normalize(T*scale*xy.x+B*scale*xy.y+n*sqrt(saturate(1-dot(xy,xy))));
}
float wetSample(int row,int angle){int wrapped=angle&15;return WetFilm[row*4+(wrapped>>2)][wrapped&3];}
float wetCoverage(float3 p){
    if(WetInfo.x<=0 || BubblePadding.y>=1 || Trace.y<=0 || Trace.y>=1 || OPTICS_MODE>=2)return 0;
    float above=liquidDistance(p);
    if(above<=0)return 0;
    float row=clamp((p.z-WetInfo.y)*WetInfo.z,0,15);
    float angle=(atan2(p.y,p.x)/6.283185307+1)*16;
    int r=(int)floor(row),a=(int)floor(angle);float2 f=frac(float2(angle,row));
    float wet=lerp(lerp(wetSample(r,a),wetSample(r,a+1),f.x),lerp(wetSample(min(r+1,15),a),wetSample(min(r+1,15),a+1),f.x),f.y);
    float radius=max(cavityRadius(p.z),.001);
    float wall=smoothstep(.7,.95,length(p.xy)/radius);
    return wet*wall*smoothstep(0,.12,above)*(1-BubblePadding.y)*(1-BubblePadding.x);
}
#ifdef DIRECT_DISTANT
struct PixelOut { float4 light : SV_Target0; float4 transmission : SV_Target1; };
#else
struct PixelOut { float4 light : SV_Target0; float4 transmission : SV_Target1; float4 distortion : SV_Target2; };
#endif
Texture2D<float> NativeDepth : register(t17);
#ifdef DIRECT_DISTANT
PixelOut PSMain(VertexOut v,bool front:SV_IsFrontFace) {
    if(!front)discard;
#else
PixelOut PSMain(VertexOut v) {
#endif
    uint nativeW,nativeH;NativeDepth.GetDimensions(nativeW,nativeH);
    uint2 pixel=min(uint2(v.position.xy/TargetSize*float2(nativeW,nativeH)),uint2(nativeW-1,nativeH-1));
#ifndef DIRECT_DISTANT
    if(v.position.z>NativeDepth.Load(int3(pixel,0))+1e-7)discard;
#endif
    PixelOut o;o.light=float4(0,0,0,0);o.transmission=float4(0,0,0,0);
#ifndef DIRECT_DISTANT
    o.distortion=encodeNativeDistortion(float2(0,0));
#endif
float3 transmission=0;
    // Isolation mode: no lighting, reflection, refraction, tint or transmission.
    float3 light=0;float liquidPresence=0;float4 surfaceHit=0;float liquidPath=0;
    if(OPTICS_MODE<2)light=shadeBottleParts(v.local,v.normal,v.eye,transmission,liquidPresence,surfaceHit,liquidPath);
    [branch] if(OPTICS_MODE>=2 || BubblePadding.x>0){
        float filled=Trace.y<=0?0:(Trace.y>=1?1:step(v.local.z,uPlane.w));
        float3 glass=exp(-GlassAbsorption.rgb*Optics.z);
        float3 liquidTransmission=exp(-Absorption.rgb*2)*(1-Absorption.w);
        float3 simpleTransmission=lerp(float3(1,1,1),liquidTransmission,filled)*glass;
        float3 simpleLight=Scatter.rgb*(1-liquidTransmission)*filled*glass;
        if(OPTICS_MODE>=2){light=simpleLight;transmission=simpleTransmission;liquidPresence=filled;}
        else{liquidPresence=lerp(liquidPresence,filled,BubblePadding.x);light=lerp(light,simpleLight,BubblePadding.x);transmission=lerp(transmission,simpleTransmission,BubblePadding.x);}
    }
    // Volume-averaged incident illumination from the engine adapter.
    float sunShadow=sunVisibility(v.worldPosition);
    float3 lighting=max(EyeLocal.rgb-SunVolume.rgb*(1-sunShadow),0.0); // CPU engine illumination, not property tint
    // Bounded volume-light response, normalized at unit illumination. Preserve
    // hue and thickness contrast under HDR sunlight instead of whitening the slab.
    // This is an artistic finite-volume approximation, not a shadowed light solve.
    float peak=max(lighting.r,max(lighting.g,lighting.b));
    lighting*=4.0/(4.0+max(peak-1.0,0.0));
    float3 incoming=normalize(v.local-v.eye),normal=normalize(v.normal);
    float rim=fresnel(abs(dot(normal,-incoming)),Optics.x);
    float3 surfaceN=mappedNormal(v,normalize(v.worldNormal));
    float4 baseTex=MaterialFlags.x>.5?SurfaceBase.Sample(SurfaceSampler,v.uv):float4(1,1,1,1);
    float2 specTex=MaterialFlags.z>.5?SurfaceMask.Sample(SurfaceSampler,v.uv).rg:float2(1,1);
    float maskTex=specTex.r,smoothness=specTex.g;
    float normalReflection=MaterialFlags.y>.5?SurfaceNormal.Sample(SurfaceSampler,v.uv).a:1;
    float wearCoverage=0;float3 wearColor=baseTex.rgb;
    // Explicit opt-in from the mesh profile; existing Nuka paths remain untouched.
    [branch] if(MaterialFlags.w>.5 && OPTICS_MODE<2 && BubblePadding.y<1){
        float fade=(1-BubblePadding.y)*(1-BubblePadding.x);
        if(MaterialFlags.x>.5){
            float strength=MaterialFlags.w<1.5?.5:.25;
            float3 localBase=SurfaceBase.SampleLevel(SurfaceSampler,v.uv,4).rgb;
            float contrast=length(baseTex.rgb-localBase)/max(length(localBase),.025);
            float dirt=saturate(contrast*1.6);
            wearCoverage=dirt*.28*fade*strength;
            wearColor=clamp(baseTex.rgb/max(max(baseTex.r,baseTex.g),max(baseTex.b,.01))*.35,.08,.45);
            smoothness*=1-dirt*.35*fade*strength;
        }
    }

    float wet=wetCoverage(v.local);
    smoothness=lerp(smoothness,max(smoothness,.98),wet*WetTuning.y);
    float3 surfaceDiffuse=0;
    [unroll] for(int d=0;d<4;d++)surfaceDiffuse+=SurfaceLight[d].rgb*saturate(dot(surfaceN,SurfaceDirection[d].xyz))*(SurfaceDirection[d].w>.5?sunShadow:1);
    float3 diffuseLighting=max(EyeLocal.rgb-SunVolume.rgb,0)+surfaceDiffuse*.25;
    diffuseLighting*=4/(4+max(max(diffuseLighting.r,max(diffuseLighting.g,diffuseLighting.b))-1,0));
    // A tinted shell absorbs transmitted light and reflects a small lit fraction.
    // Zero w preserves the existing clear Nuka glass path.
    float3 glassLight=0;
    if(GlassAbsorption.w>0){
        float3 glassTransmission=exp(-GlassAbsorption.rgb*Optics.z);
        float3 tint=glassTransmission/max(1-GlassAbsorption.w,.001);
        // Apply surface textures to glass only, not to the liquid volume.
        glassLight=tint*baseTex.rgb*(1-glassTransmission)*.15*diffuseLighting;
    }
    light=light*lighting+glassLight;
    // Thin tinted film: absorption only; highlights still use actual incident lights.
    float3 filmTransmission=lerp(float3(1,1,1),1-WetTuning.x+WetTuning.x*saturate(Scatter.rgb),wet);
    light*=filmTransmission;transmission*=filmTransmission;
    // Wet film never adds diffuse light or emission.
    float surfaceCoverage=MaterialFlags.x>.5?saturate(baseTex.a*(GlassAbsorption.w>0?.1:1)):0;
    light=lerp(light,baseTex.rgb*diffuseLighting,surfaceCoverage);
    transmission*=1-surfaceCoverage;
    // Wear is lit material on the outer shell, never emission or liquid tint.
    light=lerp(light,wearColor*diffuseLighting,wearCoverage);
    transmission*=1-wearCoverage;
    // Outer glass is independent of every liquid/detail distance weight.
    if(EyeLocal.w>0 && SurfaceEnvironment.x>0){
        float2 cubeNormalXY=MaterialFlags.y>.5?SurfaceNormal.Sample(SurfaceSampler,v.uv).xy*2-1:float2(0,0);
        float3 cubeNormal=normalize(v.cubeTangent*cubeNormalXY.x+v.cubeBitangent*cubeNormalXY.y+v.cubeNormal*sqrt(saturate(1-dot(cubeNormalXY,cubeNormalXY))));
        light+=surfaceEnvironment(v.cubeEyeFalloff.xyz,cubeNormal,v.local,specTex,normalReflection,baseTex.a,v.cubeEyeFalloff.w);
    }
    // Directional surface highlights are separate from volume-average lighting.
    // Bounded, unshadowed Blinn lobe: no texture copies or extra draw calls.
    if(OPTICS_MODE<2){
        float3 N=surfaceN,V=-normalize(v.worldIncoming);
        if(dot(N,V)<0)N=-N;
        [unroll] for(int i=0;i<4;i++){
            float3 L=SurfaceDirection[i].xyz,H=L+V;
            float h2=dot(H,H);
            if(SurfaceLight[i].w>0 && h2>1e-6){
                H*=rsqrt(h2);
                float3 irradiance=max(SurfaceLight[i].rgb,0)*(SurfaceDirection[i].w>.5?sunShadow:1);
                irradiance*=4/(4+max(max(irradiance.r,max(irradiance.g,irradiance.b))-1,0));
                float exponent=lerp(8,64,smoothness);
                float highlight=pow(saturate(dot(N,H)),exponent)*(exponent+8)/25.132741*maskTex*saturate(dot(N,L))*fresnel(saturate(dot(V,H)),Optics.x);
                light+=irradiance*highlight*(1-BubblePadding.x);
            }
        }
    }
    // Shade the actual air-to-liquid crossing, not a bright band on the shell.
    // The shared field includes waves and the contact meniscus in every pass.
    if(surfaceHit.w>0 && Trace.y>0 && Trace.y<1 && OPTICS_MODE<2 && BubblePadding.y<1){
        float3 p=surfaceHit.xyz;
        float e=.01;
        float3 gradient=float3(
            liquidDistance(p+float3(e,0,0))-liquidDistance(p-float3(e,0,0)),
            liquidDistance(p+float3(0,e,0))-liquidDistance(p-float3(0,e,0)),
            liquidDistance(p+float3(0,0,e))-liquidDistance(p-float3(0,0,e)));
        // Phase-locked detail uses the same two wave clocks and mode gains as
        // the liquid surface, with a smooth onset and fast fine-ripple release.
        float noiseMotion=liquidSurfaceNoiseStrength(Wave,WaveDetail);
        [branch] if(noiseMotion>0){
            float2 noiseUV=liquidSurfaceNoiseUV(p,Wave);
            float2 footprint=max(abs(ddx(noiseUV)),abs(ddy(noiseUV)))*128;
            float lod=max(0,log2(max(max(footprint.x,footprint.y),1)));
            float2 slope=SurfaceNoise.SampleLevel(NoiseSampler,noiseUV,lod);
            float3 perturbation=liquidSurfaceNoiseSlope(normalize(uPlane.xyz),slope,Wave,WaveDetail);
            gradient=normalize(gradient)+perturbation*.22*noiseMotion*(1-BubblePadding.y);
        }
        float3 meshN=mul(gradient,(float3x3)MeshToSimulation);
        float3x3 world=float3x3(MeshToWorld[0].xyz,MeshToWorld[1].xyz,MeshToWorld[2].xyz);
        float3 N=normalize(mul(world,meshN)),V=-normalize(v.worldIncoming);
        // Broaden highlights during sloshing and filter unresolved normal changes.
        float variation=max(dot(ddx(N),ddx(N)),dot(ddy(N),ddy(N)));
        float roughness=liquidSurfaceRoughness(Wave,WaveDetail,variation);
        float fresnelLiquid=fresnel(saturate(dot(N,V)),Optics.y);
        float glassPath=Optics.z/max(abs(dot(normal,-incoming)),.15);
        float3 weight=surfaceHit.w*(1-surfaceCoverage)*(1-BubblePadding.x)*(1-BubblePadding.y)*(1-rim)*exp(-GlassAbsorption.rgb*glassPath);
        float3 reflected=0;
        if(EyeLocal.w>0){
            reflected=max(Environment.SampleLevel(EnvironmentSampler,reflect(-V,N),roughness*3).rgb,0);
            reflected/=max(1.0,max(reflected.r,max(reflected.g,reflected.b)));
            reflected*=lighting*fresnelLiquid;
        }
        [unroll] for(int i=0;i<4;i++){
            float3 H=SurfaceDirection[i].xyz+V;float h2=dot(H,H);
            if(SurfaceLight[i].w>0 && h2>1e-6){
                H*=rsqrt(h2);
                float3 irradiance=max(SurfaceLight[i].rgb,0)*(SurfaceDirection[i].w>.5?sunShadow:1);
                irradiance*=4/(4+max(max(irradiance.r,max(irradiance.g,irradiance.b))-1,0));
                reflected+=liquidHighlight(N,V,SurfaceDirection[i].xyz,irradiance,Optics.y,roughness);
            }
        }
        light+=reflected*weight;
        transmission*=1-saturate(weight*fresnelLiquid);
    }
    // Emission is explicitly opt-in; ordinary cola loads a zero emission vector.
    light+=Emission.rgb*liquidPresence;
#ifndef DIRECT_DISTANT
    if(Trace.z>0 && all(TargetSize>0)){
        float width=TargetSize.x,height=TargetSize.y;
        float2 fullSize=float2(nativeW,nativeH);
        float3 n=normalize(v.viewNormal);
        // Glass retains a small wall offset; liquid uses a two-interface lens.
        // Both share one scene sample and the existing transmission/LOD gates.
        float wallPath=Optics.z/max(abs(dot(normal,-incoming)),.25);
        float glassBend=max(0.0,1.0-1.0/max(Optics.x,1.0));

        float glassShift=glassBend*(1.0-exp(-wallPath*4.0))*2.0;
        float2 shift=float2(n.x,-n.y)*Trace.z*glassShift/fullSize;
        // A smooth coating changes the wall bend and its local thickness gradient.
        // Reuses the existing scene sample; no extra copy, trace or pass.
        float2 filmGradient=float2(ddx(wet),ddy(wet));
        shift+=(float2(n.x,-n.y)*wet*3.0+clamp(filmGradient*24.0,-2.0,2.0))*WetTuning.z*min(Trace.z,3.0)/fullSize;
        float4 currentClip=v.clipOrigin+v.clipX*v.local.x+v.clipY*v.local.y+v.clipZ*v.local.z;
        float2 currentNdc=currentClip.xy/max(abs(currentClip.w),1e-6)*sign(currentClip.w);
        // Derivatives convert clip displacement to pixels for the actual viewport,
        // including viewport offsets and non-fullscreen render targets.
        float2 ndcPerPixel=float2(ddx(currentNdc.x),ddy(currentNdc.y));
        float radius=cavityRadius(clamp(v.local.z,uRings[0].x,uRings[uRingCount-1].x));
        float3 lensTarget;
        if(liquidPath>0 && any(transmission>0) && liquidLensTarget(v.local,incoming,radius,Optics.y,lensTarget)){
            float4 targetClip=v.clipOrigin+v.clipX*lensTarget.x+v.clipY*lensTarget.y+v.clipZ*lensTarget.z;
            if(targetClip.w*currentClip.w>0 && abs(targetClip.w)>1e-6 && all(abs(ndcPerPixel)>1e-8)){
                float2 lensPixels=(targetClip.xy/targetClip.w-currentNdc)/ndcPerPixel;
                float coverage=smoothstep(0,.15,liquidPath);
                float endOnFade=smoothstep(.04,.25,dot(incoming.xy,incoming.xy));
                shift+=lensPixels/float2(width,height)*coverage*endOnFade*min(Trace.z/3,2);
            }
        }
        // Only transmitted light is displaced; opaque cola remains opaque.
        float refractionWeight=saturate(1-BubblePadding.y);
        // Scene colour is deliberately absent. Preserve L and RGB transmission T.
        // Opaque liquid retains appearance but contributes no background displacement.
        shift*=refractionWeight;
        if(!any(transmission>1e-5))shift=0;
        o.distortion=encodeNativeDistortion(shift);

    }
#endif
    o.light=float4(light,1-dot(transmission,float3(.2126,.7152,.0722)));
    o.transmission=float4(transmission,v.position.z);
#ifdef DIRECT_DISTANT
    o.light.a=0;o.transmission.a=1;
#endif
    return o;
}
