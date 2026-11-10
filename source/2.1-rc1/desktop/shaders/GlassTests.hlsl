TextureCube<float4> Environment : register(t0);
static float4 SurfaceEnvironment=float4(1,0,0,0);
static float4 SurfaceResponse=float4(1,5,0,1);
#include "SurfaceEnvironment.hlsli"
RWStructuredBuffer<float4> Results : register(u0);
[numthreads(8,1,1)]
void Main(uint3 id:SV_DispatchThreadID) {
    uint i=id.x;
    SurfaceResponse.z=(i==1||i==4)?1:0;
    float2 mask=float2(i==4?.25:1,1);
    float normalAlpha=i==3?.5:1;
    float alpha=i==5?0:1;
    if(i==2){SurfaceResponse.y=99;SurfaceResponse.w=10;SurfaceEnvironment.y=1;}
    if(i==7)SurfaceEnvironment.x=2;
    float3 eye=i==6?float3(1,0,0):float3(0,0,1);
    Results[i]=float4(surfaceEnvironment(eye,float3(0,0,1),0,mask,normalAlpha,alpha,0),1);
}
