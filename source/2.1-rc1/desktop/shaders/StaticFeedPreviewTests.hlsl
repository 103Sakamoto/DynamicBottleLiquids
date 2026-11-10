#include "OpticalParts.hlsl"
RWStructuredBuffer<float4> Result : register(u0);
[numthreads(1,1,1)] void Main(uint3 id : SV_DispatchThreadID) {
    for(uint i=0;i<8;i++)Result[i]=float4(Optics.w.xxx,Trace.y);
}
