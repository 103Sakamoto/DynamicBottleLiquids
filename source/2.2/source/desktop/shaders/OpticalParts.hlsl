#include "DirectAlpha.hlsli"
#include "NativeFog.hlsli"
#if defined(DIRECT_DISTANT) && !defined(OPAQUE_PROTOTYPE) && !defined(INVENTORY_PREVIEW)
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
#define BUBBLE_CAPACITY 256
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
    float4 uBubbles[256];        // local-space sphere center + radius; no bubble meshes
    int uBubbleCount;
    float3 BubblePadding;
    int uRingCount;
    float uCapZ;
    float uFoam;
    int uDebug;
    float4 MeshToWorld[3];
    float4 SurfaceDirection[4];
    float4 SurfaceLight[4];
    row_major float4x4 SunMatrices[4];
    float4 SunBounds[4],SunInfo,SunSlices,SunVolume,WorldOrigin,MaterialFlags;
    float4 BubbleGroups[32];
    float4 WetFilm[64];
    float4 WetInfo;
    float4 WetTuning;
    float4 WaveDetail;
    float4 SurfaceEnvironment; // authored cube strength, lighting influence, LIGHTING, reserved
    row_major float3x4 SimulationToMesh;
    float4 SurfaceSpecular,SurfaceResponse,SurfaceAlpha,SurfaceFalloff;
    float4 BubbleShape[256];
    float4 LiquidGradient[16];
    float4 GradientInfo;
    int4 CpuSubmissionCounts;
    float4 DropletSize; // minimum, maximum, small-size bias, density
    float4 DropletShape; // spacing, jitter, elongation, taper
    float4 DropletLighting; // normal strength, internal weight, smoothness, internal slope
};
#ifdef INVENTORY_PREVIEW
// Menu models live in a separate render space. Their world-distance constants
// can select the distant fallback even while the bottle fills the preview.
// Restore optical surface detail locally; never alter opacity, fill or IOR.
static const float4 PreviewOptics=float4(Optics.xyz,Optics.w==3?3:0);
static const float3 PreviewDetail=float3(0,0,BubblePadding.z);
#define Optics PreviewOptics
#define BubblePadding PreviewDetail
#endif
float EffectSimplification(){
#if defined(INVENTORY_PREVIEW) || defined(DIRECT_DISTANT)
    return BubblePadding.x;
#else
    return BubblePadding.x;
#endif
}
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
    // Bound all three signed wave modes. Above this plane
    // every liquid field endpoint is outside. Keep a full lattice segment
    // of padding so endpoint interpolation and grazing rays remain intact.
    if(Trace.y>0 && Trace.y<1){
        float slope=dot(direction,uPlane.xyz);
        float height=uPlane.w+1.3*abs(Wave.x*WaveDetail.y)+abs(WaveDetail.x*WaveDetail.z)+0.06+0.0001;
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
#define LIQUID_SCATTER_AT(p) LayeredScatter(p)
#include "LiquidCore.inc"
#include "BottleLens.hlsli"
// Explicit snapshot of the original colour-pass transform. Never bind native utility b2 here.
cbuffer NativeGeometry : register(b10) {
    row_major float4x4 WorldViewProj;
    row_major float3x4 ModelView;
};
Texture2D<float4> SurfaceNoise : register(t16);
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
struct VertexOut { float4 position : SV_Position; float3 local : TEXCOORD0; float3 normal : TEXCOORD1; nointerpolation float3 eye : TEXCOORD2; float3 viewNormal : TEXCOORD3; float3 worldNormal : TEXCOORD4; float3 worldIncoming : TEXCOORD5; float2 uv:TEXCOORD6; float3 worldPosition:TEXCOORD7; nointerpolation float4 clipX:TEXCOORD8; nointerpolation float4 clipY:TEXCOORD9; nointerpolation float4 clipZ:TEXCOORD10; nointerpolation float4 clipOrigin:TEXCOORD11; float3 cubeTangent:TEXCOORD12; float3 cubeBitangent:TEXCOORD13; float3 cubeNormal:TEXCOORD14; float4 cubeEyeFalloff:TEXCOORD15; float4 fog:TEXCOORD16; nointerpolation float fogLod:TEXCOORD17; nointerpolation float4 rippleWeights:TEXCOORD18; };
VertexOut VSMain(VertexIn v) {
    VertexOut o;
    o.position=mul(WorldViewProj,float4(v.position.xyz,1));
    o.fog=NativeBottleFog(o.position);
    o.local=mul(MeshToSimulation,float4(v.position.xyz,1)).xyz;
    // Invert the affine simulation transform once per vertex. Clip basis is
    // constant per draw and handles rotated/translated compatibility meshes.
    float3 a=MeshToSimulation[0].xyz,b=MeshToSimulation[1].xyz,c=MeshToSimulation[2].xyz;
    float determinant=dot(a,cross(b,c));
    float invDet=abs(determinant)>1e-8?1/determinant:0;
    float3 ix=cross(b,c)*invDet,iy=cross(c,a)*invDet,iz=cross(a,b)*invDet;
    float3 center=-(ix*MeshToSimulation[0].w+iy*MeshToSimulation[1].w+iz*MeshToSimulation[2].w);
    o.clipX=mul(WorldViewProj,float4(ix,0));o.clipY=mul(WorldViewProj,float4(iy,0));o.clipZ=mul(WorldViewProj,float4(iz,0));o.clipOrigin=mul(WorldViewProj,float4(center,1));
    o.fogLod=0;
#ifndef INVENTORY_PREVIEW
    // Same per-frame fog constants and transforms as visible fog shading.
    // Centre plus eight cavity-bound corners. Use the clearest sample so a
    // bottle crossing a height/distance transition does not vanish all at once.
    float radius=0;
    [loop] for(int ring=0;ring<uRingCount;ring++)radius=max(radius,uRings[ring].y);
    radius+=max(Optics.z,0);
    if(uRingCount>0 && abs(determinant)>1e-8){
        float low=uRings[0].x-max(Optics.z,0),high=uRings[uRingCount-1].x+max(Optics.z,0);
        o.fogLod=0; // No automatic fog simplification in the diagnostic build.
    }
#endif
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
    o.rippleWeights=float4(cos(Wave.z*3),sin(Wave.z*3),cos(Wave.z*4.11),sin(Wave.z*4.11));
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
struct CondensationField { float mask; float height; };
// Height drives curvature; coverage controls material blending independently.
CondensationField condensationField(float3 p){
    float height=saturate((p.z-WetInfo.y)*WetInfo.z);
    float angle=atan2(p.y,p.x)/6.283185307+1;
    // Use surface distances rather than treating angular and height cells as
    // equal lengths. This keeps small beads round on cylindrical bottle bodies.
    float span=1/max(WetInfo.z,1e-5);
    float circumference=6.283185307*max(length(p.xy),span*.01);
    // Below one, density controls occupancy. Above one it increases the
    // number of cells, without changing the physical droplet size range.
    float countScale=sqrt(max(1,DropletSize.w));
    float columns=max(16,floor(48*countScale/DropletShape.x+.5));
    float rows=36*countScale/DropletShape.x;
    float2 cellSize=float2(circumference/columns,span/rows);
    // Preserve the previous vertical size range on the body, but limit size
    // around narrow necks so large beads cannot wrap across many cells.
    float radiusUnit=min(span/72,circumference/96);
    // Spacing changes count independently of droplet size.
    float2 grid=float2(angle*columns,height*rows);

    float2 cell=floor(grid);
    CondensationField field=(CondensationField)0;
    // Uniform bounds cover maximum size, shape, jitter and the AA fringe.
    // radiusUnit <= circumference/96 and <= span/72, so these bounds also
    // cover narrow necks. Do not truncate large or displaced droplets.
    int2 searchRadius=int2(ceil(DropletSize.y*float2(columns/96*(1+DropletShape.w),rows/72*DropletShape.z)*1.5+DropletShape.y*.5+.5));
    [loop] for(int y=-searchRadius.y;y<=searchRadius.y;y++){
        [loop] for(int x=-searchRadius.x;x<=searchRadius.x;x++){
            float2 neighbour=cell+float2(x,y);
            float2 wrapped=float2(neighbour.x-columns*floor(neighbour.x/columns),neighbour.y);
            float seed=frac(sin(dot(wrapped,float2(127.1,311.7)))*43758.5453);
            float seed2=frac(sin(dot(wrapped,float2(269.5,183.3)))*43758.5453);
                        float occupancy=frac(sin(dot(wrapped,float2(419.2,97.3)))*43758.5453);
            float sizeSeed=frac(sin(dot(wrapped,float2(73.7,523.9)))*43758.5453);
            // Jitter is adjustable independently of spacing and occupancy.
            float2 centre=neighbour+.5+DropletShape.y*(float2(seed,seed2)-.5);
            float sizeVariation=pow(sizeSeed,DropletSize.z);
            float radius=lerp(DropletSize.x,DropletSize.y,sizeVariation)*radiusUnit;
            float largeDrop=smoothstep(.18,.75,sizeVariation);
            float2 beadPosition=(grid-centre)*cellSize/float2(radius,radius*lerp(1,DropletShape.z,largeDrop));
            // Small beads are circular; large beads narrow toward the bottle top.
            beadPosition.x/=1-DropletShape.w*largeDrop*clamp(beadPosition.y,-1,1);
            float distance=length(beadPosition);
            float edge=clamp(fwidth(distance),.08,.5);
            float bead=1-smoothstep(1-edge,1+edge,distance);
            float occupied=(1-occupancy)<saturate(DropletSize.w)?1:0;
            field.mask=max(field.mask,occupied*bead);
            field.height=max(field.height,occupied*bead*sqrt(saturate(1-distance*distance)));
        }
    }
    float fade=smoothstep(0,.04,height)*(1-smoothstep(.94,1,height))*(1-BubblePadding.y);
    field.mask*=fade;
    field.height*=fade;
    return field;
}
float3 condensationSlope(float3 normal,float3 position,float height){
    float3 dx=ddx(position),dy=ddy(position);
    float3 tangentX=cross(dy,normal),tangentY=cross(normal,dx);
    float determinant=dot(dx,tangentX);
    float3 gradient=ddx(height)*tangentX+ddy(height)*tangentY;
    float strength=.004*DropletLighting.x/max(WetInfo.z,1e-5);
    float3 slope=sign(determinant)*gradient*strength/max(abs(determinant),1e-6);
    slope*=min(1.0,1.1/max(length(slope),1e-5));
    return slope;
}
float wetCoverage(float3 p){
    if(WetInfo.z<=0 || BubblePadding.y>=1 || Trace.y<=0 || Trace.y>=1 || OPTICS_MODE>=2)return 0;
    // Use the local history, not a global exposed-sample switch.
    float above=liquidDistance(p);
    // Pixel-scale transition meets the liquid instead of leaving a fixed dry strip.
    float contactWidth=max(fwidth(above),1e-5);
    float row=clamp((p.z-WetInfo.y)*WetInfo.z,0,15);
    float angle=(atan2(p.y,p.x)/6.283185307+1)*16;
    int r=(int)floor(row),a=(int)floor(angle);float2 f=frac(float2(angle,row));
    float wet=lerp(lerp(wetSample(r,a),wetSample(r,a+1),f.x),lerp(wetSample(min(r+1,15),a),wetSample(min(r+1,15),a+1),f.x),f.y);
    float radius=max(cavityRadius(p.z),.001);
    float wall=smoothstep(.7,.95,length(p.xy)/radius);
    return wet*wall*smoothstep(-contactWidth,contactWidth,above)*(1-BubblePadding.y)*(1-EffectSimplification());
}
#ifdef DIRECT_DISTANT
struct PixelOut { float4 light : SV_Target0; float4 transmission : SV_Target1; };
#else
struct PixelOut { float4 light : SV_Target0; float4 transmission : SV_Target1; float4 distortion : SV_Target2; };
#endif
Texture2D<float> NativeDepth : register(t17);
#ifdef FORWARD_REFRACTION
Texture2D<float4> ForwardScene : register(t18);

#endif
#ifdef DIRECT_DISTANT
PixelOut PSMain(VertexOut v,bool front:SV_IsFrontFace) {
    if(!front)discard;
#else
PixelOut PSMain(VertexOut v) {

#endif
#if !defined(DIRECT_DISTANT) && !defined(INVENTORY_PREVIEW)
#endif
    uint nativeW,nativeH;NativeDepth.GetDimensions(nativeW,nativeH);
    uint2 pixel=min(uint2(v.position.xy/TargetSize*float2(nativeW,nativeH)),uint2(nativeW-1,nativeH-1));
#if !defined(DIRECT_DISTANT) && !defined(ENB_DEPTH_BYPASS)
    // Retain the accepted ENB test: producer scene-depth rejection disabled.
#endif
    PixelOut o;o.light=float4(0,0,0,0);o.transmission=float4(0,0,0,0);
#ifndef DIRECT_DISTANT
    o.distortion=encodeNativeDistortion(float2(0,0));
#endif
float3 transmission=0;
    // Isolation mode: no lighting, reflection, refraction, tint or transmission.
    if(uDebug==10){o.light=float4(0,0,0,1);o.transmission=0;return o;}
    float3 refractionDelta=0;
    float3 light=0;float liquidPresence=0;float4 surfaceHit=0;float liquidPath=0;
    [branch] if(OPTICS_MODE<2)light=shadeBottleParts(v.local,v.normal,v.eye,transmission,liquidPresence,surfaceHit,liquidPath);
    if(uDebug==11){o.light=float4(lerp(float3(.1,0,.1),float3(0,.1,0),liquidPresence),1);o.transmission=0;return o;}
    [branch] if(OPTICS_MODE>=2 || EffectSimplification()>0){
        float filled=Trace.y<=0?0:(Trace.y>=1?1:step(dot(v.local,uPlane.xyz),uPlane.w));
        float3 glass=exp(-GlassAbsorption.rgb*Optics.z);
        float3 liquidTransmission=exp(-Absorption.rgb*2)*(1-Absorption.w);
        float3 simpleTransmission=lerp(float3(1,1,1),liquidTransmission,filled)*glass;
        float3 simpleLight=LayeredScatter(v.local)*(1-liquidTransmission)*filled*glass;
        if(OPTICS_MODE>=2){light=simpleLight;transmission=simpleTransmission;liquidPresence=filled;}
        else{liquidPresence=lerp(liquidPresence,filled,EffectSimplification());light=lerp(light,simpleLight,EffectSimplification());transmission=lerp(transmission,simpleTransmission,EffectSimplification());}
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
    float wet=wetCoverage(v.local);
    float filmHeight=wet;
    float3 dropN=surfaceN,internalN=surfaceN;
    float dropMask=0;
    if(WetInfo.w>0){
        CondensationField drops=condensationField(v.local);
        wet=drops.mask;filmHeight=drops.height;dropMask=drops.mask;
        float3 slope=condensationSlope(surfaceN,v.worldPosition,drops.height);
        dropN=normalize(surfaceN-slope);
        // Opposing tangent slope approximates the internal lens highlight.
        // Keep the normal component facing outwards, rather than flipping N.
        internalN=normalize(surfaceN+slope*DropletLighting.w);
    }else{
        smoothness=lerp(smoothness,max(smoothness,.98),wet*WetTuning.y);
    }
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
    float surfaceCoverage=MaterialFlags.x>.5?saturate(baseTex.a*saturate(GlassAbsorption.w)*.1):0;
    light=lerp(light,baseTex.rgb*diffuseLighting,surfaceCoverage);
    transmission*=1-surfaceCoverage;
    // Outer glass is independent of every liquid/detail distance weight.
    if(EyeLocal.w>0 && SurfaceEnvironment.x>0){
        float2 cubeNormalXY=MaterialFlags.y>.5?SurfaceNormal.Sample(SurfaceSampler,v.uv).xy*2-1:float2(0,0);
        float3 cubeNormal=normalize(v.cubeTangent*cubeNormalXY.x+v.cubeBitangent*cubeNormalXY.y+v.cubeNormal*sqrt(saturate(1-dot(cubeNormalXY,cubeNormalXY))));
        float3 reflection=surfaceEnvironment(v.cubeEyeFalloff.xyz,cubeNormal,v.local,specTex,normalReflection,baseTex.a,v.cubeEyeFalloff.w);
        if(WetInfo.w>0){
            float3 slope=condensationSlope(cubeNormal,-v.cubeEyeFalloff.xyz,filmHeight);
            float3 dropCubeNormal=normalize(cubeNormal-slope);
            // Independent droplet smoothness, without the former 4x gain.
            float3 dropReflection=surfaceEnvironment(v.cubeEyeFalloff.xyz,dropCubeNormal,v.local,specTex,normalReflection,baseTex.a,v.cubeEyeFalloff.w,DropletLighting.z);
            reflection=lerp(reflection,dropReflection,dropMask);
        }
        light+=reflection;
    }
    // Directional surface highlights are separate from volume-average lighting.
    // Bounded, unshadowed Blinn lobe: no texture copies or extra draw calls.
    if(OPTICS_MODE<2){
        float3 N=surfaceN,V=-normalize(v.worldIncoming);
        if(dot(N,V)<0)N=-N;
        float3 outerN=dot(dropN,V)<0?-dropN:dropN;
        float3 innerN=dot(internalN,V)<0?-internalN:internalN;
        [unroll] for(int i=0;i<4;i++){
            float3 L=SurfaceDirection[i].xyz,H=L+V;
            float h2=dot(H,H);
            if(SurfaceLight[i].w>0 && h2>1e-6){
                H*=rsqrt(h2);
                float3 irradiance=max(SurfaceLight[i].rgb,0)*(SurfaceDirection[i].w>.5?sunShadow:1);
                irradiance*=4/(4+max(max(irradiance.r,max(irradiance.g,irradiance.b))-1,0));
                float exponent=lerp(8,64,smoothness);
                float highlight=pow(saturate(dot(N,H)),exponent)*(exponent+8)/25.132741*maskTex*saturate(dot(N,L))*fresnel(saturate(dot(V,H)),Optics.x);
                if(WetInfo.w>0){
                    float dropExponent=lerp(8,64,DropletLighting.z);
                    float internalHighlightStrength=DropletLighting.y;
                    float outer=pow(saturate(dot(outerN,H)),dropExponent)*saturate(dot(outerN,L));
                    float inner=pow(saturate(dot(innerN,H)),dropExponent)*saturate(dot(innerN,L));
                    float dropletHighlight=(outer+internalHighlightStrength*inner)*(dropExponent+8)/25.132741*maskTex*fresnel(saturate(dot(V,H)),Optics.x);
                    // Replace the glass lobe within droplets; no former 3x boost.
                    highlight=lerp(highlight,dropletHighlight,dropMask);
                }
                light+=irradiance*highlight*(1-EffectSimplification());
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
        // Two stationary slope fields evolve independently in simulation time;
        // only the reflection/highlight normal changes, with a motion envelope.
        float noiseMotion=liquidSurfaceNoiseStrength(Wave,WaveDetail);
        [branch] if(noiseMotion>0){
            float2 noiseUV=liquidSurfaceNoiseUV(p,Wave);
            float2 footprint=max(abs(ddx(noiseUV)),abs(ddy(noiseUV)))*128;
            float lod=max(0,log2(max(max(footprint.x,footprint.y),1)));
            float4 fieldA=SurfaceNoise.SampleLevel(NoiseSampler,noiseUV,lod);float4 fieldB=SurfaceNoise.SampleLevel(NoiseSampler,noiseUV*1.37+float2(.31,.73),lod+.455);float2 slope=(fieldA.xy*v.rippleWeights.x+fieldA.zw*v.rippleWeights.y+fieldB.xy*v.rippleWeights.z+fieldB.zw*v.rippleWeights.w)*.5;
            float3 perturbation=liquidSurfaceNoiseSlope(normalize(uPlane.xyz),slope,Wave,WaveDetail);
            gradient=normalize(gradient)+perturbation*.65*noiseMotion*(1-BubblePadding.y);
        }
        float3 meshN=mul(gradient,(float3x3)MeshToSimulation);
        float3x3 world=float3x3(MeshToWorld[0].xyz,MeshToWorld[1].xyz,MeshToWorld[2].xyz);
        float3 N=normalize(mul(world,meshN)),V=-normalize(v.worldIncoming);
        // Broaden highlights during sloshing and filter unresolved normal changes.
        float variation=max(dot(ddx(N),ddx(N)),dot(ddy(N),ddy(N)));
        float roughness=liquidSurfaceRoughness(Wave,WaveDetail,variation);
        float fresnelLiquid=fresnel(saturate(dot(N,V)),Optics.y);
        float glassPath=Optics.z/max(abs(dot(normal,-incoming)),.15);
        float3 weight=surfaceHit.w*(1-surfaceCoverage)*(1-EffectSimplification())*(1-BubblePadding.y)*(1-rim)*exp(-GlassAbsorption.rgb*glassPath);
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
#if !defined(DIRECT_DISTANT) || defined(FORWARD_REFRACTION)
#ifdef FORWARD_REFRACTION
    uint fw,fh;ForwardScene.GetDimensions(fw,fh);
    float2 RefractionSize=float2(fw,fh);
#else
    float2 RefractionSize=TargetSize;
#endif
    if(Trace.z>0 && all(RefractionSize>0)){
        float width=RefractionSize.x,height=RefractionSize.y;
        float2 fullSize=RefractionSize;
        float3 n=normalize(v.viewNormal);
        // Glass retains a small wall offset; liquid uses a two-interface lens.
        // Both share one scene sample and the existing transmission/LOD gates.
        float wallPath=Optics.z/max(abs(dot(normal,-incoming)),.25);
        float glassBend=max(0.0,1.0-1.0/max(Optics.x,1.0));

        float glassShift=glassBend*(1.0-exp(-wallPath*4.0))*2.0;
        float2 shift=float2(n.x,-n.y)*Trace.z*glassShift/fullSize;
        // A smooth coating changes the wall bend and its local thickness gradient.
        // Reuses the existing scene sample; no extra copy, trace or pass.
        float2 filmGradient=float2(ddx(filmHeight),ddy(filmHeight));
        shift+=(float2(n.x,-n.y)*filmHeight*3.0+clamp(filmGradient*24.0,-2.0,2.0))*WetTuning.z*min(Trace.z,3.0)/fullSize;
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
        // Fade displacement with detail distance. Opaque liquid contributes no
        // background displacement; the forward path samples scene colour below.
        shift*=refractionWeight;
        if(!any(transmission>1e-5))shift=0;
#ifdef FORWARD_REFRACTION
        float2 uv=v.position.xy/fullSize;
        float2 edge=.5/fullSize;
        float3 centre=ForwardScene.SampleLevel(EnvironmentSampler,clamp(uv,edge,1-edge),0).rgb;
        float3 displaced=ForwardScene.SampleLevel(EnvironmentSampler,clamp(uv+shift,edge,1-edge),0).rgb;
        refractionDelta=(displaced-centre)*transmission;
#else
        o.distortion=encodeNativeDistortion(shift);
#endif

    }
#endif
#ifndef INVENTORY_PREVIEW
    // Background colour already includes scene fog. Fog only the bottle's own
    // contribution, weighted by RGB extinction, to avoid fogging it twice.
    float fogWeight=saturate(v.fog.w);
    light=light*(1-fogWeight)+v.fog.rgb*fogWeight*(1-transmission);
#ifndef DIRECT_DISTANT
    o.distortion=encodeNativeDistortion(decodeNativeDistortion(o.distortion)*(1-fogWeight));
#endif
#endif
#ifdef FORWARD_REFRACTION
    // Difference correction preserves live-target transmission and native fog.
    light+=refractionDelta*(1-saturate(v.fog.w));
#endif
    o.light=float4(light,OpticalCoverage(transmission));
    o.transmission=float4(transmission,v.position.z);
#if defined(DIRECT_DISTANT) && !defined(INVENTORY_PREVIEW)
    o.light.a=0;o.transmission.a=1;
#endif

    return o;
}

// Opaque-liquid mask only: no bubbles, scene copy sampling or light evaluation.
// Writes shell depth only where the straight sightline crosses opaque liquid.
void DepthMain(VertexOut v) {
    if(uDebug==10 || uDebug==11)return;
    clip(Absorption.w-0.99999);
    clip(Trace.y-0.000001);
    float3 direction=normalize(v.local-v.eye);
    // Match the color pass lattice and boundary coverage at every quality tier.
    int steps=colorTraceSteps();
    float stepSize=Trace.x/float(steps),lengthLiquid=0;
    int first,last;traceRange(v.local,direction,steps,first,last);
    vec2 previous=boundaryFields(v.local+direction*(float(first)*stepSize-Trace.x*0.5));
    for(int j=first;j<last;j++){
        vec2 next=boundaryFields(v.local+direction*(float(j+1)*stepSize-Trace.x*0.5));
        lengthLiquid+=boundaryFraction(previous,next)*stepSize;
        previous=next;
        if(lengthLiquid>=0.15)return;
    }
    clip(lengthLiquid-0.15);
}
// Volumetric compositing coverage, independent of bubbles/reflection/refraction.




#include "OpaqueLiquidDepth.hlsli"
float OpaquePrototypeDepth(VertexOut v):SV_Depth {
    float3 hit;float extinction;
    if(!LiquidFrontExtinction(v.local,v.eye,hit,extinction))discard;
    if(extinction<1 && LiquidDepthCoverageThreshold(v.position.xy)>=extinction)discard;
    float4 projected=v.clipOrigin+v.clipX*hit.x+v.clipY*hit.y+v.clipZ*hit.z;
    if(projected.w<=0)discard;
    float depth=projected.z/projected.w;
    if(depth<0 || depth>1)discard;
    return depth;
}
// Legacy entry retained for shader build compatibility; no runtime handover pass.
float NativeMaskPS(VertexOut v):SV_Depth { return 0; }





