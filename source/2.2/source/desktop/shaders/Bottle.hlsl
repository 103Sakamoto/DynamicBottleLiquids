// Standalone SM5 wrapper. This is NOT a drop-in replacement for FO4 Effect.hlsl.
// All matrices are supplied by the host; shell vertices are in unmodified NIF local space.
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define mix lerp
#define TRACE_STEPS 96
#define BUBBLE_CAPACITY 256
cbuffer BottleConstants : register(b0) {
    row_major float4x4 LocalToClip;
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
};
#define uWave Wave.xyz
#define uOptics Optics.xyz
#define uTrace Trace.xy
#define uAbsorption Absorption.xyz
#define uScatter Scatter.xyz
#define uEmission Emission.xyz
#define uGlassAbsorption GlassAbsorption.xyz
#define uFoamColor FoamColor.xyz
#include "LiquidCore.inc"
struct VertexIn { float3 position : POSITION; float3 normal : NORMAL; };
struct VertexOut { float4 position : SV_Position; float3 local : TEXCOORD0; float3 normal : TEXCOORD1; };
VertexOut VSMain(VertexIn v) {
    VertexOut o;
    o.position=mul(float4(v.position,1.0),LocalToClip);
    o.local=v.position; o.normal=v.normal; return o;
}
float4 PSMain(VertexOut v) : SV_Target {
    return float4(shadeBottle(v.local, v.normal, EyeLocal.xyz),1.0);
}
