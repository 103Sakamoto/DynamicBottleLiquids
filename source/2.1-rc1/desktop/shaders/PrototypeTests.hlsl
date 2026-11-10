#include "OpticalParts.hlsl"
VertexOut TestVS(uint id:SV_VertexID){
    float2 uv=float2((id<<1)&2,id&2);
    VertexOut o=(VertexOut)0;
    o.position=float4(uv*float2(2,-2)+float2(-1,1),.48,1);
    o.local=float3(uv.x*4-2,-2,(1-uv.y)*4);
    o.eye=float3(0,-10,2);o.normal=float3(0,-1,0);
    o.worldNormal=o.normal;o.viewNormal=o.normal;o.worldIncoming=o.local-o.eye;
    o.clipX=float4(.5,0,0,0);o.clipY=float4(0,0,.01,0);
    o.clipZ=float4(0,.5,0,0);o.clipOrigin=float4(0,-1,.5,1);
    o.uv=uv;
    return o;
}
float4 TestFogPS(VertexOut v):SV_Target{return float4(.8,.7,.6,.4);}
